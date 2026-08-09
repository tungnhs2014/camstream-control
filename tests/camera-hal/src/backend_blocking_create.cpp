#include <camstream/camera/camera_backend.h>

#include <cstdint>
#include <cstdio>
#include <new>

extern "C" int camstream_test_blocking_backend_enter_and_wait(void);

namespace {

constexpr char kBackendName[] = "blocking-create-test";

struct BlockingCamera {};

camstream_camera_status_t unsupported_operation() noexcept {
    return CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED;
}

extern "C" {

static camstream_camera_status_t blocking_create(camstream_camera_instance** instance) {
    if (instance == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    *instance = nullptr;
    if (camstream_test_blocking_backend_enter_and_wait() != 0) {
        return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    }

    BlockingCamera* const camera = new (std::nothrow) BlockingCamera();
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    }
    *instance = reinterpret_cast<camstream_camera_instance*>(camera);
    return CAMSTREAM_CAMERA_STATUS_OK;
}

static void blocking_destroy(camstream_camera_instance* instance) {
    delete reinterpret_cast<BlockingCamera*>(instance);
}

static camstream_camera_status_t blocking_open(camstream_camera_instance* instance, const char* source_identifier) {
    static_cast<void>(instance);
    static_cast<void>(source_identifier);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_close(camstream_camera_instance* instance) {
    static_cast<void>(instance);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_get_capabilities(camstream_camera_instance* instance,
                                                           camstream_camera_capabilities_v1* capabilities) {
    static_cast<void>(instance);
    static_cast<void>(capabilities);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_get_stream_configuration(
    camstream_camera_instance* instance, std::uint32_t index, camstream_camera_stream_config_v1* configuration) {
    static_cast<void>(instance);
    static_cast<void>(index);
    static_cast<void>(configuration);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_configure(camstream_camera_instance* instance,
                                                    const camstream_camera_stream_config_v1* requested,
                                                    camstream_camera_stream_config_v1* active) {
    static_cast<void>(instance);
    static_cast<void>(requested);
    static_cast<void>(active);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_start(camstream_camera_instance* instance) {
    static_cast<void>(instance);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_wait_frame(camstream_camera_instance* instance, std::uint32_t timeout_ms) {
    static_cast<void>(instance);
    static_cast<void>(timeout_ms);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_acquire_frame(camstream_camera_instance* instance,
                                                        camstream_camera_frame_v1* frame) {
    static_cast<void>(instance);
    static_cast<void>(frame);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_release_frame(camstream_camera_instance* instance,
                                                        std::uint64_t frame_token) {
    static_cast<void>(instance);
    static_cast<void>(frame_token);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_stop(camstream_camera_instance* instance) {
    static_cast<void>(instance);
    return unsupported_operation();
}

static camstream_camera_status_t blocking_get_last_error(camstream_camera_instance* instance, char* buffer,
                                                         std::uint32_t buffer_size) {
    static_cast<void>(instance);
    if (buffer == nullptr || buffer_size == 0U) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    const int result = std::snprintf(buffer, buffer_size, "%s", "blocking create test backend failure");
    return result < 0 ? CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR : CAMSTREAM_CAMERA_STATUS_OK;
}

} // extern "C"

const camstream_camera_backend_v1 kBackendDescriptor{
    CAMSTREAM_CAMERA_ABI_VERSION_V1,
    sizeof(camstream_camera_backend_v1),
    kBackendName,
    blocking_create,
    blocking_destroy,
    blocking_open,
    blocking_close,
    blocking_get_capabilities,
    blocking_get_stream_configuration,
    blocking_configure,
    blocking_start,
    blocking_wait_frame,
    blocking_acquire_frame,
    blocking_release_frame,
    blocking_stop,
    blocking_get_last_error,
    {},
};

} // namespace

__attribute__((constructor)) static void register_blocking_backend() noexcept {
    static_cast<void>(camstream_camera_hal_register_backend_v1(&kBackendDescriptor));
}
