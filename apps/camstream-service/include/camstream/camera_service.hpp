#ifndef CAMSTREAM_CAMERA_SERVICE_HPP
#define CAMSTREAM_CAMERA_SERVICE_HPP

#include <csignal>
#include <pthread.h>

namespace camstream {

/**
 * @brief Observable lifecycle states for the foreground camera service.
 */
enum class CameraServiceState {
    Created,
    Initialized,
    Running,
    StopRequested,
    Stopped,
};

/**
 * @brief Owns the minimal foreground service lifecycle and signal event source.
 *
 * The service is single-threaded and not thread-safe. It synchronously receives
 * blocked SIGINT and SIGTERM events through an owned signalfd descriptor. All
 * lifecycle operations and destruction must run on the thread that successfully
 * calls initialize(). The caller must not change that thread's signal mask or
 * the SIGINT/SIGTERM dispositions while the service owns them. Copying and moving
 * are prohibited so descriptor and signal-mask ownership remain attached to one
 * stable object. Destruction performs idempotent cleanup.
 *
 * Stopped is a process-lifetime terminal state. During shutdown the termination
 * signals are ignored before the prior mask is restored, preventing repeated
 * termination requests from bypassing cleanup. The process must exit after the
 * object reaches Stopped; the prior signal dispositions are not restored.
 */
class CameraService final {
public:
    /** @brief Creates a service without acquiring operating-system resources. */
    CameraService() noexcept = default;

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
     * @brief Blocks termination signals and creates the owned signalfd.
     * @return true after the Created-to-Initialized transition; false on a
     * repeated call or operating-system failure.
     *
     * On partial failure, the prior signal mask is restored before returning.
     * Every later lifecycle operation and destruction must use this thread.
     */
    bool initialize();

    /**
     * @brief Runs the blocking poll loop until a termination signal is received.
     * @return true after a clean Running-to-StopRequested transition; false on
     * invalid state, poll failure, or signalfd read failure.
     *
     * initialize() must succeed exactly once before this call. This method does
     * not release resources; the caller must subsequently call shutdown().
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
     * @brief Releases all owned resources and enters Stopped.
     * @return true when descriptor cleanup and signal-mask restoration succeed.
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
    bool drain_signal_events() noexcept;
    bool ignore_termination_signals() noexcept;
    bool close_signal_fd() noexcept;
    bool restore_signal_mask() noexcept;

    CameraServiceState state_ = CameraServiceState::Created;
    int signal_fd_ = -1;
    sigset_t previous_signal_mask_ {};
    pthread_t owner_thread_ {};
    bool owner_thread_set_ = false;
    bool owns_signal_mask_ = false;
};

} // namespace camstream

#endif
