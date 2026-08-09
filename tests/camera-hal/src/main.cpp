#include <camstream/camera/camera_hal.h>
#include <camstream/camera/camera_session.hpp>

#include <cstdint>
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
        throw std::runtime_error(std::string(operation) + " returned status " + std::to_string(actual) +
                                 ", expected " + std::to_string(expected));
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

void prepare(camstream_camera* camera) {
    require_status(camstream_camera_open(camera, kSimulatedSourceIdentifier), CAMSTREAM_CAMERA_STATUS_OK, "open");

    camstream_camera_capabilities_v1 capabilities{};
    capabilities.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    capabilities.struct_size = sizeof(capabilities);
    require_status(camstream_camera_get_capabilities(camera, &capabilities),
                   CAMSTREAM_CAMERA_STATUS_OK,
                   "get capabilities");
    require(capabilities.stream_config_count > 0U, "simulated backend reported no stream configurations");

    camstream_camera_stream_config_v1 supported{};
    initialize_configuration(supported);
    require_status(camstream_camera_get_stream_configuration(camera, 0U, &supported),
                   CAMSTREAM_CAMERA_STATUS_OK,
                   "get stream configuration");

    camstream_camera_stream_config_v1 active{};
    initialize_configuration(active);
    require_status(camstream_camera_configure(camera, &supported, &active),
                   CAMSTREAM_CAMERA_STATUS_OK,
                   "configure");
    require_status(camstream_camera_start(camera), CAMSTREAM_CAMERA_STATUS_OK, "start");
    require_status(camstream_camera_wait_frame(camera, 0U), CAMSTREAM_CAMERA_STATUS_OK, "wait frame");
}

void finish(camstream_camera* camera) {
    require_status(camstream_camera_stop(camera), CAMSTREAM_CAMERA_STATUS_OK, "stop");
    require_status(camstream_camera_close(camera), CAMSTREAM_CAMERA_STATUS_OK, "close");
}

class DirectHalProbe final {
  public:
    DirectHalProbe(const std::string& backend_path, const std::string& different_backend_path) {
        try {
            require_status(camstream_camera_hal_load_backend(backend_path.c_str()),
                           CAMSTREAM_CAMERA_STATUS_OK,
                           "load simulated backend");
            backend_loaded = true;

            require_status(camstream_camera_hal_load_backend(different_backend_path.c_str()),
                           CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                           "load different active backend");
            require_status(camstream_camera_create(&camera_a), CAMSTREAM_CAMERA_STATUS_OK, "create camera A");
            require(camera_a != nullptr, "create camera A returned null");
            require_status(camstream_camera_create(&camera_b), CAMSTREAM_CAMERA_STATUS_OK, "create camera B");
            require(camera_b != nullptr, "create camera B returned null");

            char backend_name[32]{};
            std::uint32_t abi_version = 0U;
            require_status(camstream_camera_get_backend_name(camera_a, backend_name, sizeof(backend_name)),
                           CAMSTREAM_CAMERA_STATUS_OK,
                           "get backend name");
            require(std::string(backend_name) == "simulated", "unexpected constructor-registered backend identity");
            require_status(camstream_camera_get_backend_abi_version(camera_a, &abi_version),
                           CAMSTREAM_CAMERA_STATUS_OK,
                           "get backend ABI version");
            require(abi_version == CAMSTREAM_CAMERA_ABI_VERSION_V1, "simulated backend ABI version mismatch");
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~DirectHalProbe() noexcept {
        cleanup();
    }

    DirectHalProbe(const DirectHalProbe&) = delete;
    DirectHalProbe& operator=(const DirectHalProbe&) = delete;
    DirectHalProbe(DirectHalProbe&&) = delete;
    DirectHalProbe& operator=(DirectHalProbe&&) = delete;

    void confirm_instance_local_token_collision() {
        prepare(camera_a);
        prepare(camera_b);

        camstream_camera_frame_v1 frame_a{};
        camstream_camera_frame_v1 frame_b{};
        initialize_frame(frame_a);
        initialize_frame(frame_b);
        require_status(camstream_camera_acquire_frame(camera_a, &frame_a),
                       CAMSTREAM_CAMERA_STATUS_OK,
                       "acquire frame A");
        require_status(camstream_camera_acquire_frame(camera_b, &frame_b),
                       CAMSTREAM_CAMERA_STATUS_OK,
                       "acquire frame B");
        require(frame_a.frame_token != 0U, "frame A token is zero");
        require(frame_a.frame_token == frame_b.frame_token,
                "fresh simulated backend instances did not reproduce the token collision");

        require_status(camstream_camera_release_frame(camera_b, frame_b.frame_token + UINT64_C(1000)),
                       CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                       "release untracked token");
        require_status(camstream_camera_release_frame(camera_a, frame_a.frame_token),
                       CAMSTREAM_CAMERA_STATUS_OK,
                       "release frame A");
        require_status(camstream_camera_release_frame(camera_b, frame_b.frame_token),
                       CAMSTREAM_CAMERA_STATUS_OK,
                       "release frame B");
        finish(camera_a);
        finish(camera_b);

        require_status(camstream_camera_hal_unload_backend(),
                       CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                       "reject unload with live HAL instances");
    }

  private:
    void cleanup() noexcept {
        camstream_camera_destroy(camera_b);
        camstream_camera_destroy(camera_a);
        camera_b = nullptr;
        camera_a = nullptr;
        if (backend_loaded) {
            static_cast<void>(camstream_camera_hal_unload_backend());
            backend_loaded = false;
        }
    }

    camstream_camera* camera_a = nullptr;
    camstream_camera* camera_b = nullptr;
    bool backend_loaded = false;
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

void test_failed_load_recovery(const std::string& missing_registration_path,
                               const std::string& invalid_descriptor_path,
                               const std::string& duplicate_registration_path,
                               const std::string& simulated_backend_path) {
    require(camstream_camera_hal_load_backend("/camstream/does-not-exist.so") != CAMSTREAM_CAMERA_STATUS_OK,
            "nonexistent backend path was accepted");
    require(camstream_camera_hal_load_backend(missing_registration_path.c_str()) != CAMSTREAM_CAMERA_STATUS_OK,
            "backend without constructor registration was accepted");
    require(camstream_camera_hal_load_backend(invalid_descriptor_path.c_str()) != CAMSTREAM_CAMERA_STATUS_OK,
            "backend with invalid descriptor was accepted");
    require_status(camstream_camera_hal_load_backend(duplicate_registration_path.c_str()),
                   CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                   "duplicate constructor registration");

    camstream::camera::CameraSession recovered = camstream::camera::CameraSession::load(simulated_backend_path);
    require(recovered.backend_name() == "simulated", "HAL runtime did not recover after failed backend loads");
}

void test_destroy_before_unload(const std::string& backend_path) {
    require_status(camstream_camera_hal_load_backend(backend_path.c_str()),
                   CAMSTREAM_CAMERA_STATUS_OK,
                   "load backend for unload ordering");
    camstream_camera* camera = nullptr;
    require_status(camstream_camera_create(&camera), CAMSTREAM_CAMERA_STATUS_OK, "create camera for unload ordering");
    require_status(camstream_camera_hal_unload_backend(),
                   CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                   "reject unload before camera destruction");
    camstream_camera_destroy(camera);
    require_status(camstream_camera_hal_unload_backend(),
                   CAMSTREAM_CAMERA_STATUS_OK,
                   "unload after camera destruction");
    require_status(camstream_camera_hal_load_backend(backend_path.c_str()),
                   CAMSTREAM_CAMERA_STATUS_OK,
                   "reload backend after final unload");
    require_status(camstream_camera_hal_unload_backend(),
                   CAMSTREAM_CAMERA_STATUS_OK,
                   "second normal final unload");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: camstream-camera-hal-regression-tests <simulated-backend-path> "
                     "<no-registration-backend-path> <invalid-descriptor-backend-path> "
                     "<duplicate-registration-backend-path>\n";
        return 2;
    }

    try {
        const std::string simulated_backend_path(argv[1]);
        const std::string missing_registration_path(argv[2]);
        const std::string invalid_descriptor_path(argv[3]);
        const std::string duplicate_registration_path(argv[4]);
        test_failed_load_recovery(
            missing_registration_path, invalid_descriptor_path, duplicate_registration_path, simulated_backend_path);
        test_destroy_before_unload(simulated_backend_path);
        DirectHalProbe probe(simulated_backend_path, invalid_descriptor_path);
        probe.confirm_instance_local_token_collision();
        test_cross_session_rejection(simulated_backend_path);
        test_invalid_and_double_release(simulated_backend_path);
        std::cout << "Camera HAL constructor/runtime regressions: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Camera HAL regression failure: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Camera HAL regression failure: unknown exception\n";
    }
    return 1;
}
