#include <camstream/camera/camera_hal.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

constexpr char kSimulatedSourceIdentifier[] = "simulated0";
constexpr auto kBlockingCreateTimeout = std::chrono::seconds(5);

struct BlockingCreateControl {
    std::mutex mutex;
    std::condition_variable state_changed;
    bool create_entered = false;
    bool create_released = false;
    bool create_fails = false;
};

BlockingCreateControl blocking_create_control;

void reset_blocking_create(bool create_fails) {
    std::lock_guard<std::mutex> lock(blocking_create_control.mutex);
    blocking_create_control.create_entered = false;
    blocking_create_control.create_released = false;
    blocking_create_control.create_fails = create_fails;
}

bool wait_for_blocking_create() {
    std::unique_lock<std::mutex> lock(blocking_create_control.mutex);
    return blocking_create_control.state_changed.wait_for(
        lock, kBlockingCreateTimeout, [] { return blocking_create_control.create_entered; });
}

void release_blocking_create() {
    std::lock_guard<std::mutex> lock(blocking_create_control.mutex);
    blocking_create_control.create_released = true;
    blocking_create_control.state_changed.notify_all();
}

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

void prepare(camstream_camera* camera) {
    require_status(camstream_camera_open(camera, kSimulatedSourceIdentifier), CAMSTREAM_CAMERA_STATUS_OK, "open");

    camstream_camera_capabilities_v1 capabilities{};
    capabilities.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    capabilities.struct_size = sizeof(capabilities);
    require_status(camstream_camera_get_capabilities(camera, &capabilities), CAMSTREAM_CAMERA_STATUS_OK,
                   "get capabilities");
    require(capabilities.stream_config_count > 0U, "simulated backend reported no stream configurations");

    camstream_camera_stream_config_v1 supported{};
    initialize_configuration(supported);
    require_status(camstream_camera_get_stream_configuration(camera, 0U, &supported), CAMSTREAM_CAMERA_STATUS_OK,
                   "get stream configuration");

    camstream_camera_stream_config_v1 active{};
    initialize_configuration(active);
    require_status(camstream_camera_configure(camera, &supported, &active), CAMSTREAM_CAMERA_STATUS_OK, "configure");
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
            require_status(camstream_camera_hal_load_backend(backend_path.c_str()), CAMSTREAM_CAMERA_STATUS_OK,
                           "load simulated backend");
            backend_loaded = true;

            require_status(camstream_camera_hal_load_backend(different_backend_path.c_str()),
                           CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "load different active backend");
            require_status(camstream_camera_create(&camera_a), CAMSTREAM_CAMERA_STATUS_OK, "create camera A");
            require(camera_a != nullptr, "create camera A returned null");
            require_status(camstream_camera_create(&camera_b), CAMSTREAM_CAMERA_STATUS_OK, "create camera B");
            require(camera_b != nullptr, "create camera B returned null");

            char backend_name[32]{};
            std::uint32_t abi_version = 0U;
            require_status(camstream_camera_get_backend_name(camera_a, backend_name, sizeof(backend_name)),
                           CAMSTREAM_CAMERA_STATUS_OK, "get backend name");
            require(std::string(backend_name) == "simulated", "unexpected constructor-registered backend identity");
            require_status(camstream_camera_get_backend_abi_version(camera_a, &abi_version), CAMSTREAM_CAMERA_STATUS_OK,
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

    void confirm_cross_camera_frame_protection() {
        prepare(camera_a);
        prepare(camera_b);

        camstream_camera_frame_v1 frame_a{};
        camstream_camera_frame_v1 frame_b{};
        initialize_frame(frame_a);
        initialize_frame(frame_b);
        require_status(camstream_camera_acquire_frame(camera_a, &frame_a), CAMSTREAM_CAMERA_STATUS_OK,
                       "acquire frame A");
        require_status(camstream_camera_acquire_frame(camera_b, &frame_b), CAMSTREAM_CAMERA_STATUS_OK,
                       "acquire frame B");
        require(frame_a.frame_token != 0U, "frame A token is zero");
        require(frame_b.frame_token != 0U, "frame B token is zero");
        require(frame_a.frame_token != frame_b.frame_token, "HAL frame tokens are not camera-unique");

        require_status(camstream_camera_stop(camera_a), CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                       "reject stop with an outstanding frame");
        require_status(camstream_camera_close(camera_a), CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                       "reject close while started");
        require_status(camstream_camera_release_frame(camera_b, frame_a.frame_token),
                       CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "reject cross-camera release");
        require_status(camstream_camera_release_frame(camera_a, frame_a.frame_token), CAMSTREAM_CAMERA_STATUS_OK,
                       "release frame A");
        require_status(camstream_camera_release_frame(camera_a, frame_a.frame_token),
                       CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "reject stale frame A token");
        require_status(camstream_camera_release_frame(camera_b, frame_b.frame_token), CAMSTREAM_CAMERA_STATUS_OK,
                       "release frame B");
        finish(camera_a);
        finish(camera_b);

        require_status(camstream_camera_hal_unload_backend(), CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
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

void test_failed_load_recovery(const std::string& missing_registration_path, const std::string& invalid_descriptor_path,
                               const std::string& duplicate_registration_path,
                               const std::string& simulated_backend_path) {
    require(camstream_camera_hal_load_backend("/camstream/does-not-exist.so") != CAMSTREAM_CAMERA_STATUS_OK,
            "nonexistent backend path was accepted");
    require(camstream_camera_hal_load_backend(missing_registration_path.c_str()) != CAMSTREAM_CAMERA_STATUS_OK,
            "backend without constructor registration was accepted");
    require(camstream_camera_hal_load_backend(invalid_descriptor_path.c_str()) != CAMSTREAM_CAMERA_STATUS_OK,
            "backend with invalid descriptor was accepted");
    require_status(camstream_camera_hal_load_backend(duplicate_registration_path.c_str()),
                   CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "duplicate constructor registration");

    require_status(camstream_camera_hal_load_backend(simulated_backend_path.c_str()), CAMSTREAM_CAMERA_STATUS_OK,
                   "load simulated backend after failures");
    camstream_camera* recovered = nullptr;
    require_status(camstream_camera_create(&recovered), CAMSTREAM_CAMERA_STATUS_OK, "create recovered camera");
    require(recovered != nullptr, "recovered camera is null");
    char backend_name[32]{};
    require_status(camstream_camera_get_backend_name(recovered, backend_name, sizeof(backend_name)),
                   CAMSTREAM_CAMERA_STATUS_OK, "get recovered backend name");
    require(std::string(backend_name) == "simulated", "HAL runtime did not recover after failed backend loads");
    camstream_camera_destroy(recovered);
    require_status(camstream_camera_hal_unload_backend(), CAMSTREAM_CAMERA_STATUS_OK, "unload recovered backend");
}

void test_destroy_before_unload(const std::string& backend_path) {
    require_status(camstream_camera_hal_load_backend(backend_path.c_str()), CAMSTREAM_CAMERA_STATUS_OK,
                   "load backend for unload ordering");
    camstream_camera* camera = nullptr;
    require_status(camstream_camera_create(&camera), CAMSTREAM_CAMERA_STATUS_OK, "create camera for unload ordering");
    require_status(camstream_camera_hal_unload_backend(), CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                   "reject unload before camera destruction");
    camstream_camera_destroy(camera);
    require_status(camstream_camera_hal_unload_backend(), CAMSTREAM_CAMERA_STATUS_OK,
                   "unload after camera destruction");
    require_status(camstream_camera_hal_load_backend(backend_path.c_str()), CAMSTREAM_CAMERA_STATUS_OK,
                   "reload backend after final unload");
    require_status(camstream_camera_hal_unload_backend(), CAMSTREAM_CAMERA_STATUS_OK, "second normal final unload");
}

void test_destroy_with_outstanding_frame_allows_cleanup(const std::string& backend_path) {
    require_status(camstream_camera_hal_load_backend(backend_path.c_str()), CAMSTREAM_CAMERA_STATUS_OK,
                   "load backend for destroy cleanup");
    camstream_camera* camera = nullptr;
    require_status(camstream_camera_create(&camera), CAMSTREAM_CAMERA_STATUS_OK, "create camera for destroy cleanup");
    prepare(camera);

    camstream_camera_frame_v1 frame{};
    initialize_frame(frame);
    require_status(camstream_camera_acquire_frame(camera, &frame), CAMSTREAM_CAMERA_STATUS_OK,
                   "acquire frame for destroy cleanup");
    camstream_camera_destroy(camera);
    require_status(camstream_camera_hal_unload_backend(), CAMSTREAM_CAMERA_STATUS_OK,
                   "destroy cleanup allowed final backend unload");
}

void test_create_reservation(const std::string& backend_path, bool create_fails) {
    reset_blocking_create(create_fails);
    require_status(camstream_camera_hal_load_backend(backend_path.c_str()), CAMSTREAM_CAMERA_STATUS_OK,
                   "load blocking-create backend");

    camstream_camera* camera = nullptr;
    camstream_camera_status_t create_status = CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    std::thread create_thread([&camera, &create_status] { create_status = camstream_camera_create(&camera); });

    const bool create_entered = wait_for_blocking_create();
    camstream_camera_status_t unload_status = CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    if (create_entered) {
        unload_status = camstream_camera_hal_unload_backend();
    }
    release_blocking_create();
    create_thread.join();

    require(create_entered, "blocking backend create callback was not entered");
    require_status(unload_status, CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                   "reject unload while backend create is reserved");

    if (create_fails) {
        require_status(create_status, CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR, "blocked backend create failure");
        require(camera == nullptr, "failed blocked create returned a camera");
    } else {
        require_status(create_status, CAMSTREAM_CAMERA_STATUS_OK, "blocked backend create success");
        require(camera != nullptr, "successful blocked create returned no camera");
        camstream_camera_destroy(camera);
    }

    require_status(camstream_camera_hal_unload_backend(), CAMSTREAM_CAMERA_STATUS_OK,
                   "unload after blocked create resolution");
}

} // namespace

extern "C" int camstream_test_blocking_backend_enter_and_wait(void) {
    std::unique_lock<std::mutex> lock(blocking_create_control.mutex);
    blocking_create_control.create_entered = true;
    blocking_create_control.state_changed.notify_all();
    blocking_create_control.state_changed.wait(
        lock, [] { return blocking_create_control.create_released; });
    return blocking_create_control.create_fails ? 1 : 0;
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: camstream-camera-hal-regression-tests <simulated-backend-path> "
                     "<no-registration-backend-path> <invalid-descriptor-backend-path> "
                     "<duplicate-registration-backend-path> <blocking-create-backend-path>\n";
        return 2;
    }

    try {
        const std::string simulated_backend_path(argv[1]);
        const std::string missing_registration_path(argv[2]);
        const std::string invalid_descriptor_path(argv[3]);
        const std::string duplicate_registration_path(argv[4]);
        const std::string blocking_create_backend_path(argv[5]);
        test_failed_load_recovery(missing_registration_path, invalid_descriptor_path, duplicate_registration_path,
                                  simulated_backend_path);
        test_destroy_before_unload(simulated_backend_path);
        test_destroy_with_outstanding_frame_allows_cleanup(simulated_backend_path);
        {
            DirectHalProbe probe(simulated_backend_path, invalid_descriptor_path);
            probe.confirm_cross_camera_frame_protection();
        }
        test_create_reservation(blocking_create_backend_path, false);
        test_create_reservation(blocking_create_backend_path, true);
        std::cout << "Camera HAL constructor/runtime regressions: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Camera HAL regression failure: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Camera HAL regression failure: unknown exception\n";
    }
    return 1;
}
