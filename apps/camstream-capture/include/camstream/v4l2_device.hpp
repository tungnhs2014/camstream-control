#ifndef CAMSTREAM_V4L2_DEVICE_HPP
#define CAMSTREAM_V4L2_DEVICE_HPP

#include "camstream/capture_config.hpp"
#include "camstream/mapped_buffer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace camstream {

/**
 * @brief Owns one V4L2 device descriptor and its capture resources.
 *
 * Construction opens the requested device. The class owns the descriptor,
 * MMAP mappings, driver buffer pool, and streaming state until explicit
 * cleanup or destruction. Copying and moving are prohibited so those resources
 * always have one stable owner.
 */
class V4l2Device final {
public:
    /**
     * @brief Opens a V4L2 device for nonblocking read/write access.
     * @param device_path Device node to open.
     */
    explicit V4l2Device(std::string device_path);
    ~V4l2Device();

    V4l2Device(const V4l2Device&) = delete;
    V4l2Device& operator=(const V4l2Device&) = delete;
    V4l2Device(V4l2Device&&) = delete;
    V4l2Device& operator=(V4l2Device&&) = delete;

    bool is_open() const noexcept;

    /**
     * @brief Prints QUERYCAP data and validates capture/streaming support.
     * @return true only for a video-capture node supporting streaming I/O.
     */
    bool query_and_validate_capabilities();

    /**
     * @brief Enumerates advertised formats, frame sizes, and frame intervals.
     * @return true when enumeration completes without an ioctl/data error.
     */
    bool enumerate_capture_formats();

    /**
     * @brief Applies format and optional frame-rate negotiation.
     * @param config Validated nonzero capture configuration from the CLI.
     *
     * The driver may normalize requested values; TRY_FMT, S_FMT, and the final
     * G_FMT are reported independently rather than assumed to match.
     *
     * @return true when TRY_FMT/S_FMT/G_FMT and any FPS request succeed.
     */
    bool negotiate_capture_format(const CaptureConfig& config);

    /**
     * @brief Requests and maps the driver MMAP buffer pool.
     *
     * The returned mappings remain owned by this device until release_buffers()
     * or destruction, and therefore outlive any subsequent streaming period.
     *
     * @return true only when every granted buffer is validated and mapped.
     */
    bool prepare_mmap_buffers();

    /**
     * @brief Queues every mapped buffer exactly once before STREAMON.
     * @return true only when all granted buffers are accepted by VIDIOC_QBUF.
     */
    bool queue_all_buffers();

    /**
     * @brief Starts streaming after all mapped buffers are queued.
     * @return true only when VIDIOC_STREAMON succeeds and state becomes active.
     */
    bool start_streaming();

    /**
     * @brief Dequeues, validates, reports, and requeues capture buffers.
     * @param frame_count Positive number of valid frames to process.
     * @param skip_frames Number of initial valid frames to discard; error
     * buffers never satisfy this count.
     * @param output_path Optional path for the final valid MJPEG or YUYV
     * payload.
     *
     * Each successful DQBUF transfers one buffer to userspace but does not
     * guarantee that its payload is valid. Metadata is validated before the
     * mapping can be considered safe, and the same index is returned with QBUF
     * before the next frame. Buffers carrying V4L2_BUF_FLAG_ERROR are requeued
     * but excluded from both the skip and captured counts. Initial valid
     * buffers satisfy skip_frames and are requeued without being captured or
     * saved. A bounded consecutive-error policy terminates capture when bad
     * frames persist. When output_path is nonempty, only the final valid
     * post-skip payload is read and saved before its re-QBUF. The active G_FMT
     * result determines whether MJPEG or raw YUYV output is used.
     *
     * @return true only after exactly skip_frames valid buffers were skipped,
     * exactly frame_count later valid buffers were captured, and every
     * dequeued buffer was requeued while streaming remained active.
     */
    bool capture_frames(std::uint32_t frame_count,
                        std::uint32_t skip_frames,
                        const std::string& output_path);

    /**
     * @brief Stops a stream previously started by this object.
     * @return true only when VIDIOC_STREAMOFF succeeds; failure retains active
     * state so cleanup closes the device before unmapping.
     */
    bool stop_streaming();

    /**
     * @brief Unmaps application buffers before releasing the driver pool.
     *
     * If streaming is still active, the descriptor is closed first. Cleanup is
     * idempotent and is also invoked by the destructor as a failure fallback.
     *
     * @return true when every required explicit cleanup operation succeeds.
     */
    bool release_buffers();

    /**
     * @brief Releases remaining capture resources and closes the descriptor.
     * @return true when cleanup and close complete without a reported error.
     */
    bool close();

private:
    bool negotiate_frame_rate(std::uint32_t requested_fps);
    bool release_driver_buffers();
    bool close_descriptor();

    std::string device_path_;
    int fd_ = -1;
    std::vector<MappedBuffer> mappings_;
    std::uint32_t active_pixel_format_ = 0;
    std::uint32_t active_width_ = 0;
    std::uint32_t active_height_ = 0;
    std::uint32_t active_bytes_per_line_ = 0;
    std::uint32_t active_size_image_ = 0;
    std::uint32_t granted_buffer_count_ = 0;
    bool driver_buffers_allocated_ = false;
    bool buffers_queued_ = false;
    bool streaming_ = false;
};

} // namespace camstream

#endif
