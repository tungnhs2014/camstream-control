#include <camstream/camera/camera_hal.h>

#include "camera_backend_runtime.hpp"

#include <camstream/camera/camera_backend.h>

#include <cstddef>
#include <cstring>
#include <new>

namespace {

bool valid_header(uint32_t abi_version, uint32_t struct_size, std::size_t required_size) noexcept {
    return abi_version == CAMSTREAM_CAMERA_ABI_VERSION_V1 && struct_size >= required_size;
}

bool valid_source_identifier(const char* source_identifier) noexcept {
    if (source_identifier == nullptr || source_identifier[0] == '\0') {
        return false;
    }
    std::size_t length = 0U;
    while (length < CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE && source_identifier[length] != '\0') {
        ++length;
    }
    return length < CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE;
}

} // namespace

struct camstream_camera {
    const camstream_camera_backend_v1* operations;
    camstream_camera_instance* instance;
};

camstream_camera_status_t camstream_camera_hal_load_backend(const char* backend_path) {
    return camstream::camera::detail::load_backend(backend_path);
}

camstream_camera_status_t camstream_camera_hal_unload_backend(void) {
    return camstream::camera::detail::unload_backend();
}

camstream_camera_status_t camstream_camera_hal_get_last_error(char* buffer, uint32_t buffer_size) {
    return camstream::camera::detail::copy_runtime_error(buffer, buffer_size);
}

camstream_camera_status_t camstream_camera_create(camstream_camera** camera) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    *camera = nullptr;
    const camstream_camera_backend_v1* const operations = camstream::camera::detail::active_backend();
    if (operations == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_STATE;
    }

    camstream_camera_instance* instance = nullptr;
    camstream_camera_status_t status = CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    try {
        status = operations->create(&instance);
    } catch (...) {
        camstream::camera::detail::set_runtime_error("backend create crossed the C ABI with an exception");
        if (instance != nullptr) {
            try {
                operations->destroy(instance);
            } catch (...) {
            }
        }
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        camstream::camera::detail::set_runtime_error("backend create failed without an instance diagnostic");
        if (instance != nullptr) {
            char diagnostic[256]{};
            try {
                if (operations->get_last_error(instance, diagnostic, static_cast<uint32_t>(sizeof(diagnostic))) ==
                    CAMSTREAM_CAMERA_STATUS_OK) {
                    camstream::camera::detail::set_runtime_error(diagnostic);
                }
            } catch (...) {
            }
            try {
                operations->destroy(instance);
            } catch (...) {
            }
        }
        return status;
    }
    if (instance == nullptr) {
        camstream::camera::detail::set_runtime_error("backend create returned success with a null instance");
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }

    camstream_camera* const created = new (std::nothrow) camstream_camera{operations, instance};
    if (created == nullptr) {
        try {
            operations->destroy(instance);
        } catch (...) {
        }
        camstream::camera::detail::set_runtime_error("Camera HAL camera allocation failed");
        return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    }
    camstream::camera::detail::record_instance_created();
    *camera = created;
    return status;
}

void camstream_camera_destroy(camstream_camera* camera) {
    if (camera == nullptr) {
        return;
    }
    try {
        camera->operations->destroy(camera->instance);
    } catch (...) {
    }
    delete camera;
    camstream::camera::detail::record_instance_destroyed();
}

camstream_camera_status_t camstream_camera_get_backend_name(camstream_camera* camera,
                                                            char* buffer,
                                                            uint32_t buffer_size) {
    if (camera == nullptr || buffer == nullptr || buffer_size == 0U) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    const char* const name = camera->operations->backend_name;
    const std::size_t name_length = std::strlen(name);
    if (name_length >= buffer_size) {
        buffer[0] = '\0';
        return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    }
    std::memcpy(buffer, name, name_length + 1U);
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t camstream_camera_get_backend_abi_version(camstream_camera* camera, uint32_t* abi_version) {
    if (camera == nullptr || abi_version == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    *abi_version = camera->operations->abi_version;
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t camstream_camera_open(camstream_camera* camera, const char* source_identifier) {
    if (camera == nullptr || !valid_source_identifier(source_identifier)) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->open(camera->instance, source_identifier);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_close(camstream_camera* camera) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->close(camera->instance);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_get_capabilities(camstream_camera* camera,
                                                            camstream_camera_capabilities_v1* capabilities) {
    if (camera == nullptr || capabilities == nullptr ||
        !valid_header(capabilities->abi_version, capabilities->struct_size, sizeof(*capabilities))) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->get_capabilities(camera->instance, capabilities);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_get_stream_configuration(
    camstream_camera* camera, uint32_t index, camstream_camera_stream_config_v1* configuration) {
    if (camera == nullptr || configuration == nullptr ||
        !valid_header(configuration->abi_version, configuration->struct_size, sizeof(*configuration))) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->get_stream_configuration(camera->instance, index, configuration);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_configure(
    camstream_camera* camera,
    const camstream_camera_stream_config_v1* requested,
    camstream_camera_stream_config_v1* active) {
    if (camera == nullptr || requested == nullptr || active == nullptr ||
        !valid_header(requested->abi_version, requested->struct_size, sizeof(*requested)) ||
        !valid_header(active->abi_version, active->struct_size, sizeof(*active))) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->configure(camera->instance, requested, active);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_start(camstream_camera* camera) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->start(camera->instance);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_wait_frame(camstream_camera* camera, uint32_t timeout_ms) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->wait_frame(camera->instance, timeout_ms);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_acquire_frame(camstream_camera* camera, camstream_camera_frame_v1* frame) {
    if (camera == nullptr || frame == nullptr ||
        !valid_header(frame->abi_version, frame->struct_size, sizeof(*frame))) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->acquire_frame(camera->instance, frame);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_release_frame(camstream_camera* camera, uint64_t frame_token) {
    if (camera == nullptr || frame_token == 0U) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->release_frame(camera->instance, frame_token);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_stop(camstream_camera* camera) {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->stop(camera->instance);
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t camstream_camera_get_last_error(camstream_camera* camera,
                                                          char* buffer,
                                                          uint32_t buffer_size) {
    if (camera == nullptr || buffer == nullptr || buffer_size == 0U) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        return camera->operations->get_last_error(camera->instance, buffer, buffer_size);
    } catch (...) {
        buffer[0] = '\0';
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}
