#include "camstream/camera_service.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace camstream {
namespace {

const char* camera_service_state_name(CameraServiceState state) noexcept
{
    switch (state) {
    case CameraServiceState::Created:
        return "Created";
    case CameraServiceState::Initialized:
        return "Initialized";
    case CameraServiceState::Running:
        return "Running";
    case CameraServiceState::StopRequested:
        return "StopRequested";
    case CameraServiceState::Stopped:
        return "Stopped";
    }
    return "Unknown";
}

} // namespace

CameraService::~CameraService() noexcept
{
    if (!shutdown()) {
        std::cerr << "Error: camera service destructor cleanup failed\n";
    }
}

bool CameraService::initialize()
{
    if (state_ != CameraServiceState::Created) {
        std::cerr << "Error: camera service initialize() requires Created state;"
                  << " current state is " << state_name() << '\n';
        return false;
    }

    sigset_t termination_signals {};
    if (sigemptyset(&termination_signals) != 0
        || sigaddset(&termination_signals, SIGINT) != 0
        || sigaddset(&termination_signals, SIGTERM) != 0) {
        std::cerr << "Error: failed to construct termination signal set: "
                  << std::strerror(errno) << '\n';
        return false;
    }

    const int mask_result = pthread_sigmask(
        SIG_BLOCK, &termination_signals, &previous_signal_mask_);
    if (mask_result != 0) {
        std::cerr << "Error: failed to block termination signals: "
                  << std::strerror(mask_result) << '\n';
        return false;
    }
    owns_signal_mask_ = true;
    owner_thread_ = pthread_self();
    owner_thread_set_ = true;

    signal_fd_ = signalfd(-1, &termination_signals,
                          SFD_CLOEXEC | SFD_NONBLOCK);
    if (signal_fd_ < 0) {
        const int saved_errno = errno;
        std::cerr << "Error: signalfd creation failed: "
                  << std::strerror(saved_errno) << '\n';
        const bool restored = restore_signal_mask();
        if (!restored) {
            std::cerr << "Error: signal mask cleanup failed after signalfd error\n";
            state_ = CameraServiceState::StopRequested;
        }
        return false;
    }

    state_ = CameraServiceState::Initialized;
    std::cout << "Camera service initialized" << std::endl;
    return true;
}

bool CameraService::run()
{
    if (!called_from_owner_thread()) {
        std::cerr << "Error: camera service run() must execute on the"
                  << " initialization thread\n";
        return false;
    }
    if (state_ != CameraServiceState::Initialized) {
        std::cerr << "Error: camera service run() requires Initialized state;"
                  << " current state is " << state_name() << '\n';
        return false;
    }

    state_ = CameraServiceState::Running;
    std::cout << "Camera service running" << std::endl;

    while (state_ == CameraServiceState::Running) {
        pollfd signal_event {
            signal_fd_,
            POLLIN,
            0,
        };

        const int poll_result = poll(&signal_event, 1, -1);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Error: service poll failed: "
                      << std::strerror(errno) << '\n';
            return false;
        }

        if ((signal_event.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            std::cerr << "Error: signalfd reported poll events 0x" << std::hex
                      << signal_event.revents << std::dec << '\n';
            return false;
        }

        if ((signal_event.revents & POLLIN) == 0) {
            continue;
        }

        int signal_number = 0;
        if (!read_signal_event(signal_number)) {
            return false;
        }
        if (signal_number == 0) {
            continue;
        }

        if (signal_number == SIGINT) {
            std::cout << "SIGINT received" << std::endl;
        } else if (signal_number == SIGTERM) {
            std::cout << "SIGTERM received" << std::endl;
        } else {
            std::cerr << "Error: unexpected signal " << signal_number
                      << " received through signalfd\n";
            return false;
        }

        if (!request_stop()) {
            return false;
        }
    }

    return state_ == CameraServiceState::StopRequested;
}

bool CameraService::request_stop() noexcept
{
    if (!called_from_owner_thread()) {
        std::cerr << "Error: camera service request_stop() must execute on the"
                  << " initialization thread\n";
        return false;
    }
    if (state_ == CameraServiceState::Running) {
        state_ = CameraServiceState::StopRequested;
        return true;
    }
    if (state_ == CameraServiceState::StopRequested
        || state_ == CameraServiceState::Stopped) {
        return true;
    }

    std::cerr << "Error: camera service request_stop() requires Running state;"
              << " current state is " << state_name() << '\n';
    return false;
}

bool CameraService::shutdown() noexcept
{
    if (owns_signal_mask_ && !called_from_owner_thread()) {
        std::cerr << "Error: camera service shutdown() must execute on the"
                  << " initialization thread\n";
        return false;
    }

    if (state_ != CameraServiceState::Stopped) {
        state_ = CameraServiceState::StopRequested;
        std::cout << "Camera service stopping" << std::endl;
    }

    const bool safe_to_restore_mask = ignore_termination_signals();
    bool success = safe_to_restore_mask;
    if (!drain_signal_events()) {
        success = false;
    }
    if (!close_signal_fd()) {
        success = false;
    }
    if (safe_to_restore_mask && !restore_signal_mask()) {
        success = false;
    }

    if (state_ != CameraServiceState::Stopped) {
        state_ = CameraServiceState::Stopped;
        std::cout << "Camera service stopped" << std::endl;
    }
    return success;
}

CameraServiceState CameraService::state() const noexcept
{
    return state_;
}

const char* CameraService::state_name() const noexcept
{
    return camera_service_state_name(state_);
}

bool CameraService::called_from_owner_thread() const noexcept
{
    return owner_thread_set_ && pthread_equal(owner_thread_, pthread_self()) != 0;
}

bool CameraService::read_signal_event(int& signal_number) noexcept
{
    signal_number = 0;
    signalfd_siginfo signal_info {};

    ssize_t bytes_read = -1;
    do {
        bytes_read = read(signal_fd_, &signal_info, sizeof(signal_info));
    } while (bytes_read < 0 && errno == EINTR);

    if (bytes_read < 0 && errno == EAGAIN) {
        return true;
    }
    if (bytes_read < 0) {
        std::cerr << "Error: signalfd read failed: "
                  << std::strerror(errno) << '\n';
        return false;
    }
    if (bytes_read != static_cast<ssize_t>(sizeof(signal_info))) {
        std::cerr << "Error: signalfd returned " << bytes_read
                  << " bytes; expected " << sizeof(signal_info) << '\n';
        return false;
    }

    signal_number = static_cast<int>(signal_info.ssi_signo);
    return true;
}

bool CameraService::drain_signal_events() noexcept
{
    if (signal_fd_ < 0) {
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

bool CameraService::ignore_termination_signals() noexcept
{
    if (!owns_signal_mask_) {
        return true;
    }

    struct sigaction ignore_action {};
    ignore_action.sa_handler = SIG_IGN;
    if (sigemptyset(&ignore_action.sa_mask) != 0) {
        std::cerr << "Error: failed to construct ignored-signal action: "
                  << std::strerror(errno) << '\n';
        return false;
    }
    if (sigaction(SIGINT, &ignore_action, nullptr) != 0) {
        std::cerr << "Error: failed to ignore SIGINT during shutdown: "
                  << std::strerror(errno) << '\n';
        return false;
    }
    if (sigaction(SIGTERM, &ignore_action, nullptr) != 0) {
        std::cerr << "Error: failed to ignore SIGTERM during shutdown: "
                  << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

bool CameraService::close_signal_fd() noexcept
{
    if (signal_fd_ < 0) {
        return true;
    }

    const int descriptor = signal_fd_;
    signal_fd_ = -1;
    if (close(descriptor) != 0) {
        std::cerr << "Error: failed to close signalfd: "
                  << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

bool CameraService::restore_signal_mask() noexcept
{
    if (!owns_signal_mask_) {
        return true;
    }

    const int mask_result = pthread_sigmask(
        SIG_SETMASK, &previous_signal_mask_, nullptr);
    if (mask_result != 0) {
        std::cerr << "Error: failed to restore signal mask: "
                  << std::strerror(mask_result) << '\n';
        return false;
    }

    owns_signal_mask_ = false;
    return true;
}

} // namespace camstream
