#ifndef CAMSTREAM_CAMERA_SERVICE_HPP
#define CAMSTREAM_CAMERA_SERVICE_HPP

#include <camstream/gstreamer_pipeline.hpp>

#include <chrono>
#include <csignal>
#include <pthread.h>

namespace camstream {

/**
 * @brief Observable lifecycle states for the foreground camera service.
 */
enum class CameraServiceState {
    Created,
    Initialized,
    PipelineReady,
    Running,
    StopRequested,
    Failed,
    Stopped,
};

/**
 * @brief Owns the foreground service lifecycle, signal source, and pipeline.
 *
 * The service is single-threaded and not thread-safe. It synchronously receives
 * blocked SIGINT and SIGTERM events through an owned signalfd descriptor and
 * consumes pipeline messages through a borrowed GstBus poll descriptor. It owns
 * one GstreamerPipeline and never calls its blocking wait() mode. All lifecycle
 * operations and destruction must run on the thread that successfully calls
 * initialize(). The caller must not change that thread's signal mask or the
 * SIGINT/SIGTERM dispositions while the service owns them. Copying and moving
 * are prohibited so ownership remains attached to one stable object.
 *
 * Stopped is a process-lifetime terminal state. During shutdown the termination
 * signals are ignored before the prior mask is restored, preventing repeated
 * termination requests from bypassing cleanup. The process must exit after the
 * object reaches Stopped; the prior signal dispositions are not restored.
 */
class CameraService final {
public:
    /**
     * @brief Creates a service and stores the immutable pipeline configuration.
     * @param config Camera pipeline configuration validated during initialize().
     *
     * No operating-system or GStreamer resources are acquired by construction.
     * A zero buffer count selects continuous service mode; a positive count
     * selects bounded validation mode.
     */
    explicit CameraService(GstreamerPipelineConfig config);

    /**
     * @brief Releases any signal descriptor and restores the previous mask.
     *
     * Cleanup is idempotent and does not throw. Failures are reported to
     * standard error because a destructor cannot return them.
     */
    ~CameraService() noexcept;

    CameraService(const CameraService&) = delete;
    CameraService& operator=(const CameraService&) = delete;
    CameraService(CameraService&&) = delete;
    CameraService& operator=(CameraService&&) = delete;

    /**
     * @brief Creates signal resources, builds the pipeline, and obtains its bus.
     * @return true after reaching PipelineReady; false on a repeated call,
     * invalid configuration, or resource/pipeline failure.
     *
     * A failure after signal resources are acquired leaves them owned for the
     * mandatory shutdown() call or destructor fallback. Every later lifecycle
     * operation and destruction must use the initialization thread.
     */
    bool initialize();

    /**
     * @brief Polls signal and GstBus descriptors until normal or failed exit.
     * @return true after a signal or expected finite EOS requests a clean stop;
     * false on invalid state, pipeline error, unexpected continuous-mode EOS,
     * finite-run deadline expiry, poll failure, or signalfd failure.
     *
     * initialize() must succeed exactly once before this call. GstBus messages
     * are drained through GstreamerPipeline and never read from the borrowed bus
     * descriptor. This method does not release resources; the caller must
     * subsequently call shutdown().
     */
    bool run();

    /**
     * @brief Requests a transition from Running to StopRequested.
     * @return true when stop is requested, already pending, or already
     * complete; false when the lifecycle has not entered Running.
     *
     * Calls are serialized by the single-threaded event loop.
     */
    bool request_stop() noexcept;

    /**
     * @brief Stops the pipeline, releases service resources, and enters Stopped.
     * @return true when pipeline and descriptor cleanup plus signal-mask
     * restoration succeed.
     *
     * The operation is deterministic and idempotent after normal, partial, or
     * failed initialization and runtime paths. It must run on the initialization
     * thread. Before restoring the mask, it terminally ignores SIGINT/SIGTERM so
     * repeated shutdown requests cannot interrupt cleanup before process exit.
     */
    bool shutdown() noexcept;

    /** @brief Returns the current lifecycle state for diagnostics. */
    CameraServiceState state() const noexcept;

    /** @brief Returns a stable textual name for the current lifecycle state. */
    const char* state_name() const noexcept;

private:
    bool called_from_owner_thread() const noexcept;
    bool read_signal_event(int& signal_number) noexcept;
    bool handle_signal_event() noexcept;
    bool handle_bus_event() noexcept;
    bool drain_signal_events() noexcept;
    bool ignore_termination_signals() noexcept;
    bool close_signal_fd() noexcept;
    bool restore_signal_mask() noexcept;

    CameraServiceState state_ = CameraServiceState::Created;
    bool finite_pipeline_ = false;
    std::chrono::steady_clock::duration finite_run_timeout_ {};
    GstreamerPipeline pipeline_;
    int signal_fd_ = -1;
    int bus_poll_fd_ = -1;
    sigset_t previous_signal_mask_ {};
    pthread_t owner_thread_ {};
    bool owner_thread_set_ = false;
    bool owns_signal_mask_ = false;
};

} // namespace camstream

#endif
