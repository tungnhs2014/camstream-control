#ifndef CAMSTREAM_CAMERA_CAMERA_SESSION_HPP
#define CAMSTREAM_CAMERA_CAMERA_SESSION_HPP

#include <camstream/camera/camera_ppi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace camstream::camera {

/**
 * @brief Exception describing a loader or backend operation failure.
 */
class CameraError final : public std::runtime_error {
  public:
    /**
     * @brief Creates an error with its originating PPI status.
     * @param message Complete diagnostic context suitable for logging.
     * @param status C-compatible status returned by the failed operation.
     */
    CameraError(std::string message, camstream_camera_status_t status);

    /** @brief Returns the originating PPI status. */
    camstream_camera_status_t status() const noexcept;

  private:
    camstream_camera_status_t status_;
};

/** @brief Platform-neutral camera stream request or active configuration. */
struct CameraStreamConfig {
    std::string source_identifier;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pixel_format = 0;
    std::uint32_t frame_rate_numerator = 0;
    std::uint32_t frame_rate_denominator = 0;
};

/** @brief Bounded capability summary used for indexed format discovery. */
struct CameraCapabilities {
    std::uint32_t stream_config_count = 0;
    std::uint32_t maximum_plane_count = 0;
};

/**
 * @brief Borrowed view of one backend-owned image plane.
 *
 * The view remains valid only while its enclosing CameraFrame is outstanding.
 * Callers must not free or modify the storage.
 */
struct CameraPlane {
    const std::uint8_t* data = nullptr;
    std::uint64_t allocation_size = 0;
    std::uint64_t bytes_used = 0;
    std::uint32_t stride = 0;
};

/**
 * @brief Non-copyable temporary view of one acquired backend frame.
 *
 * The frame does not own its image storage. It must be returned to the same
 * CameraSession with release_frame() before the session stops or closes. The
 * session also releases any still-outstanding tokens during destructor cleanup.
 * Access after successful release_frame() or session destruction is invalid.
 */
class CameraFrame final {
  public:
    /**
     * @brief Destroys only the C++ view; it does not release the backend token.
     *
     * Call release_frame() before this view leaves scope. If it is not called,
     * CameraSession retains the token for best-effort session cleanup.
     */
    ~CameraFrame() noexcept = default;

    CameraFrame(const CameraFrame&) = delete;
    CameraFrame& operator=(const CameraFrame&) = delete;
    CameraFrame(CameraFrame&&) = delete;
    CameraFrame& operator=(CameraFrame&&) = delete;

    /** @brief Returns whether the frame still represents an outstanding token. */
    bool valid() const noexcept;
    /** @brief Returns the backend-provided sequence number. */
    std::uint64_t sequence_number() const noexcept;
    /** @brief Returns the monotonic capture timestamp in nanoseconds. */
    std::uint64_t monotonic_timestamp_ns() const noexcept;
    /** @brief Returns the active frame width. */
    std::uint32_t width() const noexcept;
    /** @brief Returns the active frame height. */
    std::uint32_t height() const noexcept;
    /** @brief Returns the project-owned numeric pixel-format code. */
    std::uint32_t pixel_format() const noexcept;
    /** @brief Returns the number of valid plane views. */
    std::uint32_t plane_count() const noexcept;

    /**
     * @brief Returns one borrowed plane view.
     * @throws std::out_of_range when index is outside plane_count().
     */
    const CameraPlane& plane(std::size_t index) const;

  private:
    friend class CameraSession;

    explicit CameraFrame(const camstream_camera_frame_v1& frame);
    void invalidate() noexcept;

    std::uint64_t frame_token_ = 0;
    std::uint64_t sequence_number_ = 0;
    std::uint64_t monotonic_timestamp_ns_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t pixel_format_ = 0;
    std::uint32_t plane_count_ = 0;
    std::array<CameraPlane, CAMSTREAM_CAMERA_MAX_PLANES> planes_{};
};

/**
 * @brief Owns one dynamically loaded camera backend module and instance.
 *
 * Valid order is load, open, query/configure, start, wait/acquire/release,
 * stop, and close. The object is single-threaded: callers must serialize every
 * operation and destruction. It is non-copyable and non-movable so its module,
 * opaque instance, lifecycle state, and outstanding frame tokens retain one
 * stable owner. The module remains loaded until all frames are released and the
 * backend instance is destroyed.
 *
 * Public operations throw CameraError for loader, lifecycle, or backend
 * failures. No C++ exception is allowed to cross the underlying C ABI.
 * Destruction never throws and performs best-effort release, stop, close,
 * instance destruction, and module unload in that order.
 */
class CameraSession final {
  public:
    /**
     * @brief Loads and validates a backend module, then creates one instance.
     * @param backend_path Explicit path passed to dlopen().
     * @return A session owning the loaded module and opaque backend instance.
     * @throws CameraError on loader, descriptor, ABI, or create failure.
     */
    static CameraSession load(const std::string& backend_path);

    /** @brief Performs non-throwing release, stop, close, destroy, and unload cleanup. */
    ~CameraSession() noexcept;

    CameraSession(const CameraSession&) = delete;
    CameraSession& operator=(const CameraSession&) = delete;
    CameraSession(CameraSession&&) = delete;
    CameraSession& operator=(CameraSession&&) = delete;

    /** @brief Returns the validated module-owned backend name. */
    const std::string& backend_name() const noexcept;
    /** @brief Returns the validated backend ABI version. */
    std::uint32_t backend_abi_version() const noexcept;

    /** @brief Opens a nonempty backend-specific source from Created state. */
    void open(const std::string& source_identifier);
    /** @brief Queries capabilities while the source is open. */
    CameraCapabilities capabilities() const;
    /** @brief Returns one indexed supported configuration while open. */
    CameraStreamConfig stream_configuration(std::uint32_t index) const;
    /** @brief Configures the open source and returns the active configuration. */
    CameraStreamConfig configure(const CameraStreamConfig& requested);
    /** @brief Starts the configured stream. */
    void start();

    /**
     * @brief Waits up to timeout_ms for a frame.
     * @return true when a frame can be acquired; false on normal timeout.
     */
    bool wait_frame(std::uint32_t timeout_ms);

    /**
     * @brief Acquires temporary access to one backend-owned frame.
     * @return A non-copyable frame that must be passed to release_frame().
     */
    CameraFrame acquire_frame();

    /**
     * @brief Returns an outstanding frame token to this backend instance.
     *
     * On success the supplied frame is invalidated. A callback failure leaves
     * it valid so cleanup or a later retry can still release the token.
     */
    void release_frame(CameraFrame& frame);

    /**
     * @brief Stops a running stream after all frames have been released.
     *
     * Repeated calls after a successful stop are harmless.
     */
    void stop();

    /**
     * @brief Closes an open, non-running source.
     *
     * Repeated calls after a successful close are harmless.
     */
    void close();

  private:
    class Impl;

    explicit CameraSession(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

} // namespace camstream::camera

#endif
