#include "camstream/camera_service.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <poll.h>
#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <utility>

namespace camstream {
namespace {

constexpr auto kFiniteStallAllowance = std::chrono::seconds{120};

std::chrono::steady_clock::duration calculate_finite_run_timeout(const GstreamerPipelineConfig& config) noexcept {
    if (config.buffer_count == 0U || config.fps == 0U) {
        return std::chrono::steady_clock::duration::zero();
    }

    const auto buffer_count = static_cast<std::uint64_t>(config.buffer_count);
    const auto fps = static_cast<std::uint64_t>(config.fps);
    const auto nominal_seconds = (buffer_count + fps - 1U) / fps;
    return std::chrono::seconds{static_cast<std::chrono::seconds::rep>(nominal_seconds)} + kFiniteStallAllowance;
}

int remaining_poll_timeout(std::chrono::steady_clock::time_point deadline) noexcept {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }

    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
    constexpr auto maximum_timeout = static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<int>::max());
    if (remaining > maximum_timeout) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(remaining);
}

const char* camera_service_state_name(CameraServiceState state) noexcept {
    switch (state) {
    case CameraServiceState::Created:
        return "Created";
    case CameraServiceState::Initialized:
        return "Initialized";
    case CameraServiceState::PipelineReady:
        return "PipelineReady";
    case CameraServiceState::Running:
        return "Running";
    case CameraServiceState::StopRequested:
        return "StopRequested";
    case CameraServiceState::Failed:
        return "Failed";
    case CameraServiceState::Stopped:
        return "Stopped";
    }
    return "Unknown";
}

} // namespace

CameraService::CameraService(GstreamerPipelineConfig pipeline_config)
    : finite_pipeline(pipeline_config.buffer_count > 0U),
      finite_run_timeout(calculate_finite_run_timeout(pipeline_config)),
      pipeline(std::move(pipeline_config)) {}

CameraService::~CameraService() noexcept {
    if (!shutdown()) {
        std::cerr << "Error: camera service destructor cleanup failed\n";
    }
}

bool CameraService::initialize() {
    if (lifecycle_state != CameraServiceState::Created) {
        std::cerr << "Error: camera service initialize() requires Created state;"
                  << " current state is " << state_name() << '\n';
        return false;
    }

    sigset_t termination_signals{};
    if (sigemptyset(&termination_signals) != 0 || sigaddset(&termination_signals, SIGINT) != 0 ||
        sigaddset(&termination_signals, SIGTERM) != 0) {
        std::cerr << "Error: failed to construct termination signal set: " << std::strerror(errno) << '\n';
        return false;
    }

    const int mask_result = pthread_sigmask(SIG_BLOCK, &termination_signals, &previous_signal_mask);
    if (mask_result != 0) {
        std::cerr << "Error: failed to block termination signals: " << std::strerror(mask_result) << '\n';
        return false;
    }
    owns_signal_mask = true;
    owner_thread = pthread_self();
    owner_thread_set = true;

    signal_fd = signalfd(-1, &termination_signals, SFD_CLOEXEC | SFD_NONBLOCK);
    if (signal_fd < 0) {
        const int saved_errno = errno;
        std::cerr << "Error: signalfd creation failed: " << std::strerror(saved_errno) << '\n';
        const bool restored = restore_signal_mask();
        if (!restored) {
            std::cerr << "Error: signal mask cleanup failed after signalfd error\n";
            lifecycle_state = CameraServiceState::StopRequested;
        }
        return false;
    }

    lifecycle_state = CameraServiceState::Initialized;
    std::cout << "Camera service initialized" << std::endl;

    if (!pipeline.build()) {
        lifecycle_state = CameraServiceState::Failed;
        return false;
    }
    bus_poll_fd = pipeline.bus_poll_fd();
    if (bus_poll_fd < 0) {
        lifecycle_state = CameraServiceState::Failed;
        return false;
    }

    lifecycle_state = CameraServiceState::PipelineReady;
    std::cout << "Camera service pipeline ready" << std::endl;
    return true;
}

bool CameraService::run() {
    if (!called_from_owner_thread()) {
        std::cerr << "Error: camera service run() must execute on the"
                  << " initialization thread\n";
        return false;
    }
    if (lifecycle_state != CameraServiceState::PipelineReady) {
        std::cerr << "Error: camera service run() requires PipelineReady state;"
                  << " current state is " << state_name() << '\n';
        return false;
    }

    if (!pipeline.start()) {
        static_cast<void>(pipeline.drain_bus_messages());
        lifecycle_state = CameraServiceState::Failed;
        return false;
    }

    lifecycle_state = CameraServiceState::Running;
    std::cout << "Camera service running" << std::endl;

    const auto finite_deadline = std::chrono::steady_clock::now() + finite_run_timeout;

    while (lifecycle_state == CameraServiceState::Running) {
        pollfd events[] = {
            {signal_fd, POLLIN, 0},
            {bus_poll_fd, POLLIN, 0},
        };

        const int poll_timeout = finite_pipeline ? remaining_poll_timeout(finite_deadline) : -1;
        if (finite_pipeline && poll_timeout == 0) {
            std::cerr << "Error: finite pipeline exceeded its nominal duration "
                         "plus the 120-second stall allowance\n";
            lifecycle_state = CameraServiceState::Failed;
            return false;
        }

        const int poll_result = poll(events, 2, poll_timeout);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Error: service poll failed: " << std::strerror(errno) << '\n';
            lifecycle_state = CameraServiceState::Failed;
            return false;
        }
        if (poll_result == 0) {
            std::cerr << "Error: finite pipeline exceeded its nominal duration "
                         "plus the 120-second stall allowance\n";
            lifecycle_state = CameraServiceState::Failed;
            return false;
        }

        if ((events[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            std::cerr << "Error: signalfd reported poll events 0x" << std::hex << events[0].revents << std::dec << '\n';
            lifecycle_state = CameraServiceState::Failed;
            return false;
        }
        if ((events[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            std::cerr << "Error: GstBus reported poll events 0x" << std::hex << events[1].revents << std::dec << '\n';
            lifecycle_state = CameraServiceState::Failed;
            return false;
        }

        if ((events[0].revents & POLLIN) != 0 && !handle_signal_event()) {
            lifecycle_state = CameraServiceState::Failed;
            return false;
        }
        if ((events[1].revents & POLLIN) != 0 && !handle_bus_event()) {
            return false;
        }
    }

    return lifecycle_state == CameraServiceState::StopRequested;
}

bool CameraService::request_stop() noexcept {
    if (!called_from_owner_thread()) {
        std::cerr << "Error: camera service request_stop() must execute on the"
                  << " initialization thread\n";
        return false;
    }
    if (lifecycle_state == CameraServiceState::Running) {
        lifecycle_state = CameraServiceState::StopRequested;
        return true;
    }
    if (lifecycle_state == CameraServiceState::StopRequested || lifecycle_state == CameraServiceState::Stopped) {
        return true;
    }

    std::cerr << "Error: camera service request_stop() requires Running state;"
              << " current state is " << state_name() << '\n';
    return false;
}

bool CameraService::shutdown() noexcept {
    if (owns_signal_mask && !called_from_owner_thread()) {
        std::cerr << "Error: camera service shutdown() must execute on the"
                  << " initialization thread\n";
        return false;
    }

    if (lifecycle_state != CameraServiceState::Stopped) {
        if (lifecycle_state != CameraServiceState::Failed) {
            lifecycle_state = CameraServiceState::StopRequested;
        }
        std::cout << "Camera service stopping" << std::endl;
    }

    bool success = pipeline.stop();
    bus_poll_fd = -1;

    const bool safe_to_restore_mask = ignore_termination_signals();
    if (!safe_to_restore_mask) {
        success = false;
    }
    if (!drain_signal_events()) {
        success = false;
    }
    if (!close_signal_fd()) {
        success = false;
    }
    if (safe_to_restore_mask && !restore_signal_mask()) {
        success = false;
    }

    if (lifecycle_state != CameraServiceState::Stopped) {
        lifecycle_state = CameraServiceState::Stopped;
        std::cout << "Camera service stopped" << std::endl;
    }
    return success;
}

CameraServiceState CameraService::state() const noexcept {
    return lifecycle_state;
}

const char* CameraService::state_name() const noexcept {
    return camera_service_state_name(lifecycle_state);
}

bool CameraService::called_from_owner_thread() const noexcept {
    return owner_thread_set && pthread_equal(owner_thread, pthread_self()) != 0;
}

bool CameraService::read_signal_event(int& signal_number) noexcept {
    signal_number = 0;
    signalfd_siginfo signal_info{};

    ssize_t bytes_read = -1;
    do {
        bytes_read = read(signal_fd, &signal_info, sizeof(signal_info));
    } while (bytes_read < 0 && errno == EINTR);

    if (bytes_read < 0 && errno == EAGAIN) {
        return true;
    }
    if (bytes_read < 0) {
        std::cerr << "Error: signalfd read failed: " << std::strerror(errno) << '\n';
        return false;
    }
    if (bytes_read != static_cast<ssize_t>(sizeof(signal_info))) {
        std::cerr << "Error: signalfd returned " << bytes_read << " bytes; expected " << sizeof(signal_info) << '\n';
        return false;
    }

    signal_number = static_cast<int>(signal_info.ssi_signo);
    return true;
}

bool CameraService::handle_signal_event() noexcept {
    int signal_number = 0;
    if (!read_signal_event(signal_number)) {
        return false;
    }
    if (signal_number == 0) {
        return true;
    }

    if (signal_number == SIGINT) {
        std::cout << "SIGINT received" << std::endl;
    } else if (signal_number == SIGTERM) {
        std::cout << "SIGTERM received" << std::endl;
    } else {
        std::cerr << "Error: unexpected signal " << signal_number << " received through signalfd\n";
        return false;
    }

    return request_stop();
}

bool CameraService::handle_bus_event() noexcept {
    const GstreamerBusOutcome outcome = pipeline.drain_bus_messages();
    if (outcome == GstreamerBusOutcome::Continue) {
        return true;
    }
    if (outcome == GstreamerBusOutcome::Error) {
        lifecycle_state = CameraServiceState::Failed;
        return false;
    }
    if (!finite_pipeline) {
        std::cerr << "Error: continuous pipeline reached unexpected EOS\n";
        lifecycle_state = CameraServiceState::Failed;
        return false;
    }

    return request_stop();
}

bool CameraService::drain_signal_events() noexcept {
    if (signal_fd < 0) {
        return true;
    }

    while (true) {
        int signal_number = 0;
        if (!read_signal_event(signal_number)) {
            return false;
        }
        if (signal_number == 0) {
            return true;
        }
    }
}

bool CameraService::ignore_termination_signals() noexcept {
    if (!owns_signal_mask) {
        return true;
    }

    struct sigaction ignore_action {};
    ignore_action.sa_handler = SIG_IGN;
    if (sigemptyset(&ignore_action.sa_mask) != 0) {
        std::cerr << "Error: failed to construct ignored-signal action: " << std::strerror(errno) << '\n';
        return false;
    }
    if (sigaction(SIGINT, &ignore_action, nullptr) != 0) {
        std::cerr << "Error: failed to ignore SIGINT during shutdown: " << std::strerror(errno) << '\n';
        return false;
    }
    if (sigaction(SIGTERM, &ignore_action, nullptr) != 0) {
        std::cerr << "Error: failed to ignore SIGTERM during shutdown: " << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

bool CameraService::close_signal_fd() noexcept {
    if (signal_fd < 0) {
        return true;
    }

    const int descriptor = signal_fd;
    signal_fd = -1;
    if (close(descriptor) != 0) {
        std::cerr << "Error: failed to close signalfd: " << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

bool CameraService::restore_signal_mask() noexcept {
    if (!owns_signal_mask) {
        return true;
    }

    const int mask_result = pthread_sigmask(SIG_SETMASK, &previous_signal_mask, nullptr);
    if (mask_result != 0) {
        std::cerr << "Error: failed to restore signal mask: " << std::strerror(mask_result) << '\n';
        return false;
    }

    owns_signal_mask = false;
    return true;
}

} // namespace camstream
