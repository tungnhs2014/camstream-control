#include <camstream/camera/camera_hal.h>

#include "camera_backend_runtime.hpp"

#include <camstream/camera/camera_backend.h>
#include <camstream/logging.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace {

enum class CameraState {
    Created,
    Open,
    Configured,
    Started,
    Stopped,
};

struct OutstandingFrame {
    std::uint64_t public_token;
    std::uint64_t backend_token;
};

std::atomic<std::uint64_t> next_public_frame_token{UINT64_C(1)};

bool valid_header(std::uint32_t abi_version, std::uint32_t struct_size, std::size_t required_size) noexcept {
    return abi_version == CAMSTREAM_CAMERA_ABI_VERSION_V1 && struct_size >= required_size;
}

std::size_t bounded_string_length(const char* text, std::size_t maximum_length) noexcept {
    if (text == nullptr) {
        return maximum_length;
    }

    std::size_t length = 0U;
    while (length < maximum_length && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool valid_source_identifier(const char* source_identifier) noexcept {
    const std::size_t length = bounded_string_length(source_identifier, CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE);
    return length > 0U && length < CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE;
}

bool valid_configuration(const camstream_camera_stream_config_v1& configuration) noexcept {
    return valid_header(configuration.abi_version, configuration.struct_size, sizeof(configuration)) &&
           valid_source_identifier(configuration.source_identifier) && configuration.width > 0U &&
           configuration.height > 0U && configuration.pixel_format != 0U && configuration.frame_rate_numerator > 0U &&
           configuration.frame_rate_denominator > 0U;
}

bool valid_capabilities(const camstream_camera_capabilities_v1& capabilities) noexcept {
    return valid_header(capabilities.abi_version, capabilities.struct_size, sizeof(capabilities)) &&
           capabilities.stream_config_count > 0U && capabilities.maximum_plane_count > 0U &&
           capabilities.maximum_plane_count <= CAMSTREAM_CAMERA_MAX_PLANES;
}

bool valid_frame(const camstream_camera_frame_v1& frame) noexcept {
    if (!valid_header(frame.abi_version, frame.struct_size, sizeof(frame)) || frame.frame_token == 0U ||
        frame.width == 0U || frame.height == 0U || frame.pixel_format == 0U || frame.plane_count == 0U ||
        frame.plane_count > CAMSTREAM_CAMERA_MAX_PLANES) {
        return false;
    }

    for (std::uint32_t index = 0U; index < frame.plane_count; ++index) {
        const camstream_camera_plane_v1& plane = frame.planes[index];
        if (!valid_header(plane.abi_version, plane.struct_size, sizeof(plane)) || plane.data == nullptr ||
            plane.allocation_size == 0U || plane.bytes_used == 0U || plane.bytes_used > plane.allocation_size ||
            plane.stride == 0U) {
            return false;
        }
    }
    return true;
}

void initialize_configuration(camstream_camera_stream_config_v1& configuration) noexcept {
    configuration = {};
    configuration.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    configuration.struct_size = sizeof(configuration);
}

void initialize_capabilities(camstream_camera_capabilities_v1& capabilities) noexcept {
    capabilities = {};
    capabilities.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    capabilities.struct_size = sizeof(capabilities);
}

void initialize_frame(camstream_camera_frame_v1& frame) noexcept {
    frame = {};
    frame.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    frame.struct_size = sizeof(frame);
    for (camstream_camera_plane_v1& plane : frame.planes) {
        plane.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
        plane.struct_size = sizeof(plane);
    }
}

std::uint64_t allocate_public_frame_token() noexcept {
    std::uint64_t candidate = next_public_frame_token.load(std::memory_order_relaxed);
    while (candidate != 0U) {
        const std::uint64_t following = candidate == std::numeric_limits<std::uint64_t>::max() ? 0U : candidate + 1U;
        if (next_public_frame_token.compare_exchange_weak(candidate, following, std::memory_order_relaxed,
                                                          std::memory_order_relaxed)) {
            return candidate;
        }
    }
    return 0U;
}

} // namespace

struct camstream_camera {
    camstream_camera(const camstream_camera_backend_v1* backend_operations,
                     camstream_camera_instance* backend_instance) noexcept
        : operations(backend_operations), instance(backend_instance) {}

    const camstream_camera_backend_v1* operations;
    camstream_camera_instance* instance;
    CameraState state = CameraState::Created;
    std::vector<OutstandingFrame> outstanding_frames;
    std::optional<std::uint64_t> pending_cleanup_token;
    std::string hal_error;
    bool ownership_faulted = false;
};

namespace {

camstream_camera_status_t set_hal_error(camstream_camera* camera, camstream_camera_status_t status,
                                        const char* message) noexcept {
    if (camera != nullptr) {
        try {
            camera->hal_error = message != nullptr ? message : "Camera HAL operation failed";
        } catch (...) {
            LOGE("Camera HAL could not retain diagnostic text");
        }
    }
    return status;
}

void clear_hal_error(camstream_camera* camera) noexcept {
    if (camera != nullptr) {
        camera->hal_error.clear();
    }
}

template <typename Callback>
camstream_camera_status_t invoke_backend_callback(camstream_camera* camera, const char* operation,
                                                  const char* error_message, Callback&& callback) noexcept {
    try {
        return callback();
    } catch (...) {
        LOGE("Backend " << operation << " crossed the C ABI with an exception");
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR, error_message);
    }
}

template <typename Callback> void invoke_backend_cleanup(const char* error_message, Callback&& callback) noexcept {
    try {
        callback();
    } catch (...) {
        LOGE(error_message);
    }
}

camstream_camera_status_t release_backend_token(camstream_camera* camera, std::uint64_t backend_token) noexcept {
    return invoke_backend_callback(
        camera, "release_frame", "backend release_frame crossed the C ABI",
        [camera, backend_token] { return camera->operations->release_frame(camera->instance, backend_token); });
}

camstream_camera_status_t rollback_acquired_frame(camstream_camera* camera, std::uint64_t backend_token,
                                                  camstream_camera_status_t original_status,
                                                  const char* original_error) noexcept {
    const camstream_camera_status_t rollback_status = release_backend_token(camera, backend_token);
    if (rollback_status == CAMSTREAM_CAMERA_STATUS_OK) {
        return set_hal_error(camera, original_status, original_error);
    }

    camera->pending_cleanup_token = backend_token;
    return set_hal_error(camera, rollback_status,
                         "acquired-frame validation failed and rollback release_frame also failed");
}

camstream_camera_status_t retry_pending_cleanup(camstream_camera* camera) noexcept {
    if (!camera->pending_cleanup_token.has_value()) {
        return CAMSTREAM_CAMERA_STATUS_OK;
    }

    const camstream_camera_status_t status = release_backend_token(camera, *camera->pending_cleanup_token);
    if (status == CAMSTREAM_CAMERA_STATUS_OK) {
        camera->pending_cleanup_token.reset();
    }
    return status;
}

void cleanup_camera(camstream_camera* camera) noexcept {
    if (camera->pending_cleanup_token.has_value()) {
        static_cast<void>(release_backend_token(camera, *camera->pending_cleanup_token));
        camera->pending_cleanup_token.reset();
    }

    for (auto frame = camera->outstanding_frames.rbegin(); frame != camera->outstanding_frames.rend(); ++frame) {
        static_cast<void>(release_backend_token(camera, frame->backend_token));
    }
    camera->outstanding_frames.clear();

    if (camera->state == CameraState::Started) {
        invoke_backend_cleanup("Backend stop crossed the C ABI during Camera HAL cleanup", [camera] {
            if (camera->operations->stop(camera->instance) == CAMSTREAM_CAMERA_STATUS_OK) {
                camera->state = CameraState::Stopped;
            }
        });
    }
    if (camera->state == CameraState::Open || camera->state == CameraState::Configured ||
        camera->state == CameraState::Stopped) {
        invoke_backend_cleanup("Backend close crossed the C ABI during Camera HAL cleanup",
                               [camera] { static_cast<void>(camera->operations->close(camera->instance)); });
    }
}

} // namespace

camstream_camera_status_t camstream_camera_hal_load_backend(const char* backend_path) {
    return camstream::camera::detail::load_backend(backend_path);
}

camstream_camera_status_t camstream_camera_hal_unload_backend(void) {
    return camstream::camera::detail::unload_backend();
}

camstream_camera_status_t camstream_camera_hal_get_last_error(char* buffer, std::uint32_t buffer_size) {
    return camstream::camera::detail::copy_runtime_error(buffer, buffer_size);
}

camstream_camera_status_t camstream_camera_create(camstream_camera** camera) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    *camera = nullptr;
    const camstream_camera_backend_v1* operations = nullptr;
    const camstream_camera_status_t reservation_status =
        camstream::camera::detail::reserve_backend_for_create(&operations);
    if (reservation_status != CAMSTREAM_CAMERA_STATUS_OK) {
        return reservation_status;
    }

    camstream_camera_instance* instance = nullptr;
    camstream_camera_status_t status = CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    try {
        status = operations->create(&instance);
    } catch (...) {
        LOGE("Backend create crossed the C ABI with an exception");
        camstream::camera::detail::set_runtime_error("backend create crossed the C ABI with an exception");
        if (instance != nullptr) {
            invoke_backend_cleanup("Backend destroy crossed the C ABI during failed create cleanup",
                                   [operations, instance] { operations->destroy(instance); });
        }
        const camstream_camera_status_t cancel_status = camstream::camera::detail::cancel_backend_creation();
        return cancel_status == CAMSTREAM_CAMERA_STATUS_OK ? CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR : cancel_status;
    }
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        camstream::camera::detail::set_runtime_error("backend create failed without an instance diagnostic");
        if (instance != nullptr) {
            char diagnostic[256]{};
            const camstream_camera_status_t diagnostic_status = invoke_backend_callback(
                nullptr, "get_last_error after create failure",
                "backend get_last_error crossed the C ABI after create failure", [operations, instance, &diagnostic] {
                    if (operations->get_last_error(instance, diagnostic,
                                                   static_cast<std::uint32_t>(sizeof(diagnostic))) ==
                        CAMSTREAM_CAMERA_STATUS_OK) {
                        return CAMSTREAM_CAMERA_STATUS_OK;
                    }
                    return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
                });
            if (diagnostic_status == CAMSTREAM_CAMERA_STATUS_OK) {
                camstream::camera::detail::set_runtime_error(diagnostic);
            }
            invoke_backend_cleanup("Backend destroy crossed the C ABI after create failure",
                                   [operations, instance] { operations->destroy(instance); });
        }
        const camstream_camera_status_t cancel_status = camstream::camera::detail::cancel_backend_creation();
        return cancel_status == CAMSTREAM_CAMERA_STATUS_OK ? status : cancel_status;
    }
    if (instance == nullptr) {
        camstream::camera::detail::set_runtime_error("backend create returned success with a null instance");
        const camstream_camera_status_t cancel_status = camstream::camera::detail::cancel_backend_creation();
        return cancel_status == CAMSTREAM_CAMERA_STATUS_OK ? CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR : cancel_status;
    }

    camstream_camera* created = nullptr;
    try {
        created = new camstream_camera(operations, instance);
    } catch (const std::bad_alloc&) {
        LOGE("Camera HAL camera allocation failed");
        invoke_backend_cleanup("Backend destroy crossed the C ABI after Camera HAL allocation failure",
                               [operations, instance] { operations->destroy(instance); });
        camstream::camera::detail::set_runtime_error("Camera HAL camera allocation failed");
        const camstream_camera_status_t cancel_status = camstream::camera::detail::cancel_backend_creation();
        return cancel_status == CAMSTREAM_CAMERA_STATUS_OK ? CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR : cancel_status;
    } catch (...) {
        LOGE("Camera HAL camera construction failed unexpectedly");
        invoke_backend_cleanup("Backend destroy crossed the C ABI after Camera HAL construction failure",
                               [operations, instance] { operations->destroy(instance); });
        camstream::camera::detail::set_runtime_error("Camera HAL camera construction failed");
        const camstream_camera_status_t cancel_status = camstream::camera::detail::cancel_backend_creation();
        return cancel_status == CAMSTREAM_CAMERA_STATUS_OK ? CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR : cancel_status;
    }

    const camstream_camera_status_t commit_status = camstream::camera::detail::commit_backend_creation();
    if (commit_status != CAMSTREAM_CAMERA_STATUS_OK) {
        invoke_backend_cleanup("Backend destroy crossed the C ABI after Camera HAL creation commit failure",
                               [operations, instance] { operations->destroy(instance); });
        delete created;
        const camstream_camera_status_t cancel_status = camstream::camera::detail::cancel_backend_creation();
        return cancel_status == CAMSTREAM_CAMERA_STATUS_OK ? commit_status : cancel_status;
    }

    *camera = created;
    return CAMSTREAM_CAMERA_STATUS_OK;
}

void camstream_camera_destroy(camstream_camera* camera) {
    if (camera == nullptr) {
        return;
    }

    cleanup_camera(camera);
    invoke_backend_cleanup("Backend destroy crossed the C ABI during Camera HAL destruction",
                           [camera] { camera->operations->destroy(camera->instance); });
    delete camera;
    camstream::camera::detail::record_instance_destroyed();
}

camstream_camera_status_t camstream_camera_get_backend_name(camstream_camera* camera, char* buffer,
                                                            std::uint32_t buffer_size) {
    if (camera == nullptr || buffer == nullptr || buffer_size == 0U) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    const char* const name = camera->operations->backend_name;
    const std::size_t name_length = std::strlen(name);
    if (name_length >= buffer_size) {
        buffer[0] = '\0';
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR, "backend-name buffer is too small");
    }
    std::memcpy(buffer, name, name_length + 1U);
    clear_hal_error(camera);
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t camstream_camera_get_backend_abi_version(camstream_camera* camera,
                                                                   std::uint32_t* abi_version) {
    if (camera == nullptr || abi_version == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    *abi_version = camera->operations->abi_version;
    clear_hal_error(camera);
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t camstream_camera_open(camstream_camera* camera, const char* source_identifier) {
    if (camera == nullptr || !valid_source_identifier(source_identifier)) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "open requires a bounded source");
    }
    if (camera->state != CameraState::Created) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "open requires Created state");
    }

    clear_hal_error(camera);
    const camstream_camera_status_t status = invoke_backend_callback(
        camera, "open", "backend open crossed the C ABI",
        [camera, source_identifier] { return camera->operations->open(camera->instance, source_identifier); });
    if (status == CAMSTREAM_CAMERA_STATUS_OK) {
        camera->state = CameraState::Open;
    }
    return status;
}

camstream_camera_status_t camstream_camera_close(camstream_camera* camera) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (camera->state == CameraState::Created) {
        clear_hal_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    }
    if (camera->state == CameraState::Started) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "close requires stop first");
    }
    if (camera->state != CameraState::Open && camera->state != CameraState::Configured &&
        camera->state != CameraState::Stopped) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "close requires an open source");
    }
    if (camera->pending_cleanup_token.has_value() || !camera->outstanding_frames.empty()) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "close requires every frame released");
    }

    clear_hal_error(camera);
    const camstream_camera_status_t status = invoke_backend_callback(
        camera, "close", "backend close crossed the C ABI",
        [camera] { return camera->operations->close(camera->instance); });
    if (status == CAMSTREAM_CAMERA_STATUS_OK) {
        camera->state = CameraState::Created;
    }
    return status;
}

camstream_camera_status_t camstream_camera_get_capabilities(camstream_camera* camera,
                                                            camstream_camera_capabilities_v1* capabilities) {
    if (camera == nullptr || capabilities == nullptr ||
        !valid_header(capabilities->abi_version, capabilities->struct_size, sizeof(*capabilities))) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "invalid capabilities output storage");
    }
    if (camera->state != CameraState::Open && camera->state != CameraState::Configured) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "capabilities require an open source");
    }

    camstream_camera_capabilities_v1 result{};
    initialize_capabilities(result);
    clear_hal_error(camera);
    const camstream_camera_status_t status = invoke_backend_callback(
        camera, "get_capabilities", "backend get_capabilities crossed the C ABI",
        [camera, &result] { return camera->operations->get_capabilities(camera->instance, &result); });
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return status;
    }
    if (!valid_capabilities(result)) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR, "backend returned invalid capabilities");
    }
    *capabilities = result;
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t camstream_camera_get_stream_configuration(camstream_camera* camera, std::uint32_t index,
                                                                    camstream_camera_stream_config_v1* configuration) {
    if (camera == nullptr || configuration == nullptr ||
        !valid_header(configuration->abi_version, configuration->struct_size, sizeof(*configuration))) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                             "invalid stream-configuration output storage");
    }
    if (camera->state != CameraState::Open) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                             "stream-configuration discovery requires Open state");
    }

    camstream_camera_stream_config_v1 result{};
    initialize_configuration(result);
    clear_hal_error(camera);
    const camstream_camera_status_t status = invoke_backend_callback(
        camera, "get_stream_configuration", "backend get_stream_configuration crossed the C ABI",
        [camera, index, &result] {
            return camera->operations->get_stream_configuration(camera->instance, index, &result);
        });
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return status;
    }
    if (!valid_configuration(result)) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR,
                             "backend returned an invalid stream configuration");
    }
    *configuration = result;
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t camstream_camera_configure(camstream_camera* camera,
                                                     const camstream_camera_stream_config_v1* requested,
                                                     camstream_camera_stream_config_v1* active) {
    if (camera == nullptr || requested == nullptr || active == nullptr || !valid_configuration(*requested) ||
        !valid_header(active->abi_version, active->struct_size, sizeof(*active))) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "configure received invalid arguments");
    }
    if (camera->state != CameraState::Open) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "configure requires Open state");
    }

    camstream_camera_stream_config_v1 result{};
    initialize_configuration(result);
    clear_hal_error(camera);
    const camstream_camera_status_t status = invoke_backend_callback(
        camera, "configure", "backend configure crossed the C ABI",
        [camera, requested, &result] { return camera->operations->configure(camera->instance, requested, &result); });
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return status;
    }
    if (!valid_configuration(result)) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR,
                             "backend returned an invalid active configuration");
    }
    *active = result;
    camera->state = CameraState::Configured;
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t camstream_camera_start(camstream_camera* camera) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (camera->state != CameraState::Configured) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "start requires Configured state");
    }

    clear_hal_error(camera);
    const camstream_camera_status_t status = invoke_backend_callback(
        camera, "start", "backend start crossed the C ABI",
        [camera] { return camera->operations->start(camera->instance); });
    if (status == CAMSTREAM_CAMERA_STATUS_OK) {
        camera->state = CameraState::Started;
    }
    return status;
}

camstream_camera_status_t camstream_camera_wait_frame(camstream_camera* camera, std::uint32_t timeout_ms) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (camera->state != CameraState::Started) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "wait_frame requires Started state");
    }
    if (camera->pending_cleanup_token.has_value() || camera->ownership_faulted) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "frame ownership cleanup is unresolved");
    }

    clear_hal_error(camera);
    return invoke_backend_callback(camera, "wait_frame", "backend wait_frame crossed the C ABI", [camera, timeout_ms] {
        return camera->operations->wait_frame(camera->instance, timeout_ms);
    });
}

camstream_camera_status_t camstream_camera_acquire_frame(camstream_camera* camera, camstream_camera_frame_v1* frame) {
    if (camera == nullptr || frame == nullptr ||
        !valid_header(frame->abi_version, frame->struct_size, sizeof(*frame))) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "invalid frame output storage");
    }
    if (camera->state != CameraState::Started) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "acquire_frame requires Started state");
    }
    if (camera->pending_cleanup_token.has_value() || camera->ownership_faulted) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "frame ownership cleanup is unresolved");
    }

    camstream_camera_frame_v1 acquired{};
    initialize_frame(acquired);
    clear_hal_error(camera);
    try {
        const camstream_camera_status_t status = camera->operations->acquire_frame(camera->instance, &acquired);
        if (status != CAMSTREAM_CAMERA_STATUS_OK) {
            return status;
        }
    } catch (...) {
        LOGE("Backend acquire_frame crossed the C ABI with an exception");
        if (acquired.frame_token != 0U) {
            return rollback_acquired_frame(camera, acquired.frame_token, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR,
                                           "backend acquire_frame crossed the C ABI");
        }
        camera->ownership_faulted = true;
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR, "backend acquire_frame crossed the C ABI");
    }

    if (!valid_frame(acquired)) {
        if (acquired.frame_token == 0U) {
            camera->ownership_faulted = true;
            return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR,
                                 "backend returned invalid acquired-frame metadata");
        }
        return rollback_acquired_frame(camera, acquired.frame_token, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR,
                                       "backend returned invalid acquired-frame metadata");
    }

    const auto duplicate = std::find_if(
        camera->outstanding_frames.begin(), camera->outstanding_frames.end(),
        [&acquired](const OutstandingFrame& outstanding) { return outstanding.backend_token == acquired.frame_token; });
    if (duplicate != camera->outstanding_frames.end()) {
        camera->ownership_faulted = true;
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR,
                             "backend returned a duplicate outstanding frame token");
    }

    const std::uint64_t backend_token = acquired.frame_token;
    const std::uint64_t public_token = allocate_public_frame_token();
    if (public_token == 0U) {
        return rollback_acquired_frame(camera, backend_token, CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR,
                                       "Camera HAL frame-token space is exhausted");
    }

    try {
        camera->outstanding_frames.push_back({public_token, backend_token});
    } catch (const std::bad_alloc&) {
        LOGE("Camera HAL frame-tracking allocation failed");
        return rollback_acquired_frame(camera, backend_token, CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR,
                                       "Camera HAL frame tracking allocation failed");
    } catch (...) {
        LOGE("Camera HAL frame tracking failed unexpectedly");
        return rollback_acquired_frame(camera, backend_token, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR,
                                       "Camera HAL frame tracking failed");
    }

    acquired.frame_token = public_token;
    *frame = acquired;
    clear_hal_error(camera);
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t camstream_camera_release_frame(camstream_camera* camera, std::uint64_t frame_token) {
    if (camera == nullptr || frame_token == 0U) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "release_frame requires a token");
    }
    if (camera->state != CameraState::Started) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "release_frame requires Started state");
    }

    const auto frame = std::find_if(
        camera->outstanding_frames.begin(), camera->outstanding_frames.end(),
        [frame_token](const OutstandingFrame& outstanding) { return outstanding.public_token == frame_token; });
    if (frame == camera->outstanding_frames.end()) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                             "frame token is unknown, stale, or camera-owned elsewhere");
    }

    clear_hal_error(camera);
    const camstream_camera_status_t status = release_backend_token(camera, frame->backend_token);
    if (status == CAMSTREAM_CAMERA_STATUS_OK) {
        camera->outstanding_frames.erase(frame);
    }
    return status;
}

camstream_camera_status_t camstream_camera_stop(camstream_camera* camera) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (camera->state == CameraState::Stopped) {
        clear_hal_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    }
    if (camera->state != CameraState::Started) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "stop requires Started state");
    }

    clear_hal_error(camera);
    const camstream_camera_status_t cleanup_status = retry_pending_cleanup(camera);
    if (cleanup_status != CAMSTREAM_CAMERA_STATUS_OK) {
        return cleanup_status;
    }
    if (!camera->outstanding_frames.empty()) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "stop requires every frame released");
    }
    if (camera->ownership_faulted) {
        return set_hal_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "frame ownership state is faulted");
    }

    const camstream_camera_status_t status = invoke_backend_callback(
        camera, "stop", "backend stop crossed the C ABI",
        [camera] { return camera->operations->stop(camera->instance); });
    if (status == CAMSTREAM_CAMERA_STATUS_OK) {
        camera->state = CameraState::Stopped;
    }
    return status;
}

camstream_camera_status_t camstream_camera_get_last_error(camstream_camera* camera, char* buffer,
                                                          std::uint32_t buffer_size) {
    if (camera == nullptr || buffer == nullptr || buffer_size == 0U) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (!camera->hal_error.empty()) {
        const std::size_t maximum = static_cast<std::size_t>(buffer_size - 1U);
        const std::size_t length = std::min(camera->hal_error.size(), maximum);
        std::memcpy(buffer, camera->hal_error.data(), length);
        buffer[length] = '\0';
        return CAMSTREAM_CAMERA_STATUS_OK;
    }

    try {
        return camera->operations->get_last_error(camera->instance, buffer, buffer_size);
    } catch (...) {
        LOGE("Backend get_last_error crossed the C ABI with an exception");
        buffer[0] = '\0';
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}
