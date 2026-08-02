#include <camstream/camera/camera_ppi.h>
#include <camstream/camera/camera_session.hpp>

#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr char kSimulatedSourceIdentifier[] = "simulated0";

void require(bool condition, const char* failure) {
    if (!condition) {
        throw std::runtime_error(failure);
    }
}

void require_status(camstream_camera_status_t actual, camstream_camera_status_t expected, const char* operation) {
    if (actual != expected) {
        throw std::runtime_error(std::string(operation) + " returned status " + std::to_string(actual) + ", expected " +
                                 std::to_string(expected));
    }
}

void initialize_configuration(camstream_camera_stream_config_v1& configuration) noexcept {
    configuration = {};
    configuration.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    configuration.struct_size = sizeof(configuration);
}

void initialize_frame(camstream_camera_frame_v1& frame) noexcept {
    frame = {};
    frame.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    frame.struct_size = sizeof(frame);
}

class DirectBackendProbe final {
  public:
    explicit DirectBackendProbe(const std::string& backend_path) {
        try {
            handle = dlopen(backend_path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (handle == nullptr) {
                const char* const error = dlerror();
                throw std::runtime_error(std::string("dlopen failed: ") + (error != nullptr ? error : "unknown error"));
            }

            dlerror();
            void* const symbol = dlsym(handle, CAMSTREAM_CAMERA_BACKEND_ENTRYPOINT_V1);
            const char* const symbol_error = dlerror();
            if (symbol == nullptr || symbol_error != nullptr) {
                throw std::runtime_error(std::string("dlsym failed: ") +
                                         (symbol_error != nullptr ? symbol_error : "missing entry point"));
            }

            camstream_camera_get_backend_v1_fn entrypoint = nullptr;
            static_assert(sizeof(entrypoint) == sizeof(symbol),
                          "POSIX function and data pointers must be equally sized");
            std::memcpy(&entrypoint, &symbol, sizeof(entrypoint));
            backend = entrypoint();
            require(backend != nullptr, "simulated backend entry point returned null");
            require(backend->abi_version == CAMSTREAM_CAMERA_ABI_VERSION_V1, "simulated backend ABI version mismatch");
            require(backend->backend_name != nullptr, "simulated backend identity is null");
            require(std::strcmp(backend->backend_name, "simulated") == 0, "unexpected backend identity");

            require_status(backend->create(&instance_a), CAMSTREAM_CAMERA_STATUS_OK, "create instance A");
            require(instance_a != nullptr, "create instance A returned null");
            require_status(backend->create(&instance_b), CAMSTREAM_CAMERA_STATUS_OK, "create instance B");
            require(instance_b != nullptr, "create instance B returned null");
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~DirectBackendProbe() noexcept {
        cleanup();
    }

    DirectBackendProbe(const DirectBackendProbe&) = delete;
    DirectBackendProbe& operator=(const DirectBackendProbe&) = delete;
    DirectBackendProbe(DirectBackendProbe&&) = delete;
    DirectBackendProbe& operator=(DirectBackendProbe&&) = delete;

    void confirm_instance_local_token_collision() {
        prepare(instance_a);
        prepare(instance_b);

        camstream_camera_frame_v1 frame_a{};
        camstream_camera_frame_v1 frame_b{};
        initialize_frame(frame_a);
        initialize_frame(frame_b);
        require_status(backend->acquire_frame(instance_a, &frame_a), CAMSTREAM_CAMERA_STATUS_OK, "acquire frame A");
        require_status(backend->acquire_frame(instance_b, &frame_b), CAMSTREAM_CAMERA_STATUS_OK, "acquire frame B");
        require(frame_a.frame_token != 0U, "frame A token is zero");
        require(frame_a.frame_token == frame_b.frame_token,
                "fresh simulated backend instances did not reproduce the token collision");

        require_status(backend->release_frame(instance_b, frame_b.frame_token + UINT64_C(1000)),
                       CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                       "release untracked token");
        require_status(backend->release_frame(instance_a, frame_a.frame_token),
                       CAMSTREAM_CAMERA_STATUS_OK,
                       "release frame A");
        require_status(backend->release_frame(instance_b, frame_b.frame_token),
                       CAMSTREAM_CAMERA_STATUS_OK,
                       "release frame B");
        finish(instance_a);
        finish(instance_b);
    }

  private:
    void cleanup() noexcept {
        if (backend != nullptr) {
            backend->destroy(instance_b);
            backend->destroy(instance_a);
            instance_b = nullptr;
            instance_a = nullptr;
        }
        if (handle != nullptr) {
            static_cast<void>(dlclose(handle));
            handle = nullptr;
        }
    }

    void prepare(camstream_camera_instance* instance) {
        require_status(backend->open(instance, kSimulatedSourceIdentifier), CAMSTREAM_CAMERA_STATUS_OK, "open");

        camstream_camera_stream_config_v1 supported{};
        initialize_configuration(supported);
        require_status(backend->get_stream_configuration(instance, 0U, &supported),
                       CAMSTREAM_CAMERA_STATUS_OK,
                       "get stream configuration");

        camstream_camera_stream_config_v1 active{};
        initialize_configuration(active);
        require_status(backend->configure(instance, &supported, &active), CAMSTREAM_CAMERA_STATUS_OK, "configure");
        require_status(backend->start(instance), CAMSTREAM_CAMERA_STATUS_OK, "start");
    }

    void finish(camstream_camera_instance* instance) {
        require_status(backend->stop(instance), CAMSTREAM_CAMERA_STATUS_OK, "stop");
        require_status(backend->close(instance), CAMSTREAM_CAMERA_STATUS_OK, "close");
    }

    void* handle = nullptr;
    const camstream_camera_backend_v1* backend = nullptr;
    camstream_camera_instance* instance_a = nullptr;
    camstream_camera_instance* instance_b = nullptr;
};

void prepare(camstream::camera::CameraSession& session) {
    session.open(kSimulatedSourceIdentifier);
    const camstream::camera::CameraStreamConfig supported = session.stream_configuration(0U);
    static_cast<void>(session.configure(supported));
    session.start();
}

void test_cross_session_rejection(const std::string& backend_path) {
    camstream::camera::CameraSession session_a = camstream::camera::CameraSession::load(backend_path);
    camstream::camera::CameraSession session_b = camstream::camera::CameraSession::load(backend_path);
    prepare(session_a);
    prepare(session_b);

    camstream::camera::CameraFrame frame_a = session_a.acquire_frame();
    camstream::camera::CameraFrame frame_b = session_b.acquire_frame();

    bool rejected = false;
    try {
        session_b.release_frame(frame_a);
    } catch (const camstream::camera::CameraError& error) {
        rejected = error.status() == CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    require(rejected, "cross-session release was not rejected as invalid input");
    require(frame_a.valid(), "rejected release invalidated frame A");
    require(frame_b.valid(), "rejected release invalidated frame B");

    session_a.release_frame(frame_a);
    session_b.release_frame(frame_b);
    require(!frame_a.valid(), "same-session release did not invalidate frame A");
    require(!frame_b.valid(), "same-session release did not invalidate frame B");
    session_a.stop();
    session_b.stop();
    session_a.close();
    session_b.close();
}

void test_invalid_and_double_release(const std::string& backend_path) {
    camstream::camera::CameraSession session = camstream::camera::CameraSession::load(backend_path);
    prepare(session);
    camstream::camera::CameraFrame frame = session.acquire_frame();
    session.release_frame(frame);
    require(!frame.valid(), "released frame remained valid");

    bool rejected = false;
    try {
        session.release_frame(frame);
    } catch (const camstream::camera::CameraError& error) {
        rejected = error.status() == CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    require(rejected, "double release of an invalid frame was not rejected");
    session.stop();
    session.close();
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: camstream-camera-ppi-regression-tests <simulated-backend-path>\n";
        return 2;
    }

    try {
        const std::string backend_path(argv[1]);
        DirectBackendProbe probe(backend_path);
        probe.confirm_instance_local_token_collision();
        test_cross_session_rejection(backend_path);
        test_invalid_and_double_release(backend_path);
        std::cout << "Camera PPI frame-ownership regressions: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Camera PPI regression failure: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Camera PPI regression failure: unknown exception\n";
    }
    return 1;
}
