#include "camera_backend_runtime.hpp"

#include <condition_variable>
#include <cstddef>
#include <dlfcn.h>
#include <limits>
#include <mutex>
#include <string>

namespace camstream::camera::detail {
namespace {

constexpr std::size_t kMaximumBackendNameLength = 127U;

enum class LoadState {
    Unloaded,
    Loading,
    Loaded,
    Unloading,
    Faulted,
};

struct BackendRuntime {
    std::mutex mutex;
    std::condition_variable state_changed;
    LoadState state = LoadState::Unloaded;
    void* module_handle = nullptr;
    const camstream_camera_backend_v1* backend = nullptr;
    std::string backend_name;
    std::string backend_path;
    std::string last_error;
    camstream_camera_status_t registration_status = CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    bool registration_attempted = false;
    std::uint32_t client_count = 0U;
    std::uint32_t live_instance_count = 0U;
};

BackendRuntime& runtime() {
    static BackendRuntime instance;
    return instance;
}

std::size_t bounded_string_length(const char* text, std::size_t maximum_length) noexcept {
    std::size_t length = 0U;
    while (length < maximum_length && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool callbacks_are_complete(const camstream_camera_backend_v1& backend) noexcept {
    return backend.create != nullptr && backend.destroy != nullptr && backend.open != nullptr &&
           backend.close != nullptr && backend.get_capabilities != nullptr &&
           backend.get_stream_configuration != nullptr && backend.configure != nullptr && backend.start != nullptr &&
           backend.wait_frame != nullptr && backend.acquire_frame != nullptr && backend.release_frame != nullptr &&
           backend.stop != nullptr && backend.get_last_error != nullptr;
}

camstream_camera_status_t validate_backend(const camstream_camera_backend_v1* backend, std::string& error) {
    if (backend == nullptr) {
        error = "backend registration supplied a null descriptor";
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (backend->abi_version != CAMSTREAM_CAMERA_ABI_VERSION_V1) {
        error = "backend ABI version does not match Camera HAL ABI v1";
        return CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED;
    }
    if (backend->struct_size < sizeof(camstream_camera_backend_v1)) {
        error = "backend descriptor is smaller than ABI v1";
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (backend->backend_name == nullptr) {
        error = "backend name is null";
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    const std::size_t name_length = bounded_string_length(backend->backend_name, kMaximumBackendNameLength + 1U);
    if (name_length == 0U || name_length > kMaximumBackendNameLength) {
        error = "backend name is empty or not bounded";
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (!callbacks_are_complete(*backend)) {
        error = "backend descriptor has a missing mandatory callback";
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    return CAMSTREAM_CAMERA_STATUS_OK;
}

void reset_loaded_fields(BackendRuntime& value) noexcept {
    value.module_handle = nullptr;
    value.backend = nullptr;
    value.backend_name.clear();
    value.backend_path.clear();
    value.registration_status = CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    value.registration_attempted = false;
    value.client_count = 0U;
    value.live_instance_count = 0U;
}

bool finish_failed_load(BackendRuntime& value, void* handle, const std::string& load_error) noexcept {
    try {
        if (handle == nullptr) {
            std::lock_guard<std::mutex> lock(value.mutex);
            reset_loaded_fields(value);
            value.last_error = load_error;
            value.state = LoadState::Unloaded;
            value.state_changed.notify_all();
            return true;
        }

        {
            std::lock_guard<std::mutex> lock(value.mutex);
            value.module_handle = handle;
            value.client_count = 0U;
            value.live_instance_count = 0U;
            value.state = LoadState::Unloading;
            value.last_error = load_error;
        }

        dlerror();
        const int close_result = dlclose(handle);
        const char* const close_error = close_result == 0 ? nullptr : dlerror();
        {
            std::lock_guard<std::mutex> lock(value.mutex);
            if (close_result == 0) {
                reset_loaded_fields(value);
                value.last_error = load_error;
                value.state = LoadState::Unloaded;
            } else {
                value.backend = nullptr;
                value.registration_status = CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
                value.registration_attempted = false;
                value.state = LoadState::Faulted;
                value.last_error = load_error + "; cleanup dlclose failed: " +
                                   (close_error != nullptr ? close_error : "unknown error");
            }
            value.state_changed.notify_all();
        }
        return close_result == 0;
    } catch (...) {
        try {
            std::lock_guard<std::mutex> lock(value.mutex);
            value.module_handle = handle;
            value.backend = nullptr;
            value.registration_status = CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
            value.registration_attempted = false;
            value.client_count = 0U;
            value.live_instance_count = 0U;
            value.state = LoadState::Faulted;
            value.state_changed.notify_all();
        } catch (...) {
        }
        return false;
    }
}

} // namespace

camstream_camera_status_t load_backend(const char* backend_path) noexcept {
    if (backend_path == nullptr || backend_path[0] == '\0') {
        try {
            BackendRuntime& value = runtime();
            std::lock_guard<std::mutex> lock(value.mutex);
            value.last_error = "camera backend path must not be empty";
        } catch (...) {
        }
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }

    BackendRuntime& value = runtime();
    void* handle = nullptr;
    bool loading_started = false;
    try {
        const std::string requested_path(backend_path);
        {
            std::unique_lock<std::mutex> lock(value.mutex);
            value.state_changed.wait(lock, [&value] {
                return value.state != LoadState::Loading && value.state != LoadState::Unloading;
            });
            if (value.state == LoadState::Faulted) {
                return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
            }
            if (value.state == LoadState::Loaded) {
                if (value.backend_path != requested_path) {
                    value.last_error = "a different camera backend is already active";
                    return CAMSTREAM_CAMERA_STATUS_INVALID_STATE;
                }
                if (value.client_count == std::numeric_limits<std::uint32_t>::max()) {
                    value.last_error = "camera backend client-reference limit reached";
                    return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
                }
                ++value.client_count;
                return CAMSTREAM_CAMERA_STATUS_OK;
            }

            value.backend = nullptr;
            value.backend_name.clear();
            value.backend_path = requested_path;
            value.last_error = "backend constructor did not register a descriptor";
            value.registration_status = CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
            value.registration_attempted = false;
            value.state = LoadState::Loading;
            loading_started = true;
        }

        // The module constructor re-enters registration, so dlopen must run without the runtime mutex held.
        dlerror();
        handle = dlopen(backend_path, RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            const char* const loader_error = dlerror();
            static_cast<void>(
                finish_failed_load(value, nullptr, loader_error != nullptr ? loader_error : "dlopen failed"));
            loading_started = false;
            return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
        }

        std::string load_error;
        camstream_camera_status_t load_status = CAMSTREAM_CAMERA_STATUS_OK;
        {
            std::lock_guard<std::mutex> lock(value.mutex);
            if (value.registration_status != CAMSTREAM_CAMERA_STATUS_OK || value.backend == nullptr) {
                load_status = value.registration_status;
                load_error = value.last_error;
            } else {
                value.module_handle = handle;
                value.client_count = 1U;
                value.live_instance_count = 0U;
                value.state = LoadState::Loaded;
                value.last_error.clear();
                loading_started = false;
                value.state_changed.notify_all();
            }
        }
        if (load_status != CAMSTREAM_CAMERA_STATUS_OK) {
            if (!finish_failed_load(value, handle, load_error)) {
                load_status = CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
            }
            loading_started = false;
        }
        return load_status;
    } catch (...) {
        if (loading_started) {
            if (!finish_failed_load(value, handle, "internal exception while loading camera backend")) {
                return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
            }
        }
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t unload_backend() noexcept {
    try {
        BackendRuntime& value = runtime();
        void* handle = nullptr;
        std::string backend_path;
        {
            std::unique_lock<std::mutex> lock(value.mutex);
            value.state_changed.wait(lock, [&value] {
                return value.state != LoadState::Loading && value.state != LoadState::Unloading;
            });
            if (value.state == LoadState::Faulted) {
                return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
            }
            if (value.state != LoadState::Loaded || value.client_count == 0U) {
                value.last_error = "camera backend is not loaded";
                return CAMSTREAM_CAMERA_STATUS_INVALID_STATE;
            }
            if (value.client_count > 1U) {
                --value.client_count;
                return CAMSTREAM_CAMERA_STATUS_OK;
            }
            if (value.live_instance_count != 0U) {
                value.last_error = "camera backend cannot unload while HAL instances remain alive";
                return CAMSTREAM_CAMERA_STATUS_INVALID_STATE;
            }
            if (value.module_handle == nullptr) {
                value.client_count = 0U;
                value.backend = nullptr;
                value.last_error = "loaded camera backend has no owned module handle";
                value.state = LoadState::Faulted;
                value.state_changed.notify_all();
                return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
            }

            handle = value.module_handle;
            backend_path = value.backend_path;
            value.client_count = 0U;
            value.state = LoadState::Unloading;
        }

        dlerror();
        const int close_result = dlclose(handle);
        const char* const close_error = close_result == 0 ? nullptr : dlerror();
        {
            std::lock_guard<std::mutex> lock(value.mutex);
            if (close_result == 0) {
                reset_loaded_fields(value);
                value.last_error.clear();
                value.state = LoadState::Unloaded;
            } else {
                value.backend = nullptr;
                value.registration_status = CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
                value.registration_attempted = false;
                value.state = LoadState::Faulted;
                try {
                    value.last_error = "dlclose failed for camera backend '" + backend_path + "': " +
                                       (close_error != nullptr ? close_error : "unknown error");
                } catch (...) {
                }
            }
            value.state_changed.notify_all();
        }
        return close_result == 0 ? CAMSTREAM_CAMERA_STATUS_OK : CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    } catch (...) {
        try {
            BackendRuntime& value = runtime();
            std::lock_guard<std::mutex> lock(value.mutex);
            if (value.state == LoadState::Unloading) {
                value.backend = nullptr;
                value.registration_status = CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
                value.registration_attempted = false;
                value.client_count = 0U;
                value.live_instance_count = 0U;
                value.state = LoadState::Faulted;
                value.state_changed.notify_all();
            }
        } catch (...) {
        }
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

camstream_camera_status_t copy_runtime_error(char* buffer, std::uint32_t buffer_size) noexcept {
    if (buffer == nullptr || buffer_size == 0U) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        BackendRuntime& value = runtime();
        std::lock_guard<std::mutex> lock(value.mutex);
        const std::size_t maximum = static_cast<std::size_t>(buffer_size - 1U);
        const std::size_t length = value.last_error.size() < maximum ? value.last_error.size() : maximum;
        value.last_error.copy(buffer, length);
        buffer[length] = '\0';
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        buffer[0] = '\0';
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

void set_runtime_error(const char* error) noexcept {
    try {
        BackendRuntime& value = runtime();
        std::lock_guard<std::mutex> lock(value.mutex);
        value.last_error = error != nullptr ? error : "unknown Camera HAL runtime error";
    } catch (...) {
    }
}

const camstream_camera_backend_v1* active_backend() noexcept {
    BackendRuntime& value = runtime();
    std::lock_guard<std::mutex> lock(value.mutex);
    return value.state == LoadState::Loaded && value.client_count > 0U ? value.backend : nullptr;
}

void record_instance_created() noexcept {
    BackendRuntime& value = runtime();
    std::lock_guard<std::mutex> lock(value.mutex);
    ++value.live_instance_count;
}

void record_instance_destroyed() noexcept {
    BackendRuntime& value = runtime();
    std::lock_guard<std::mutex> lock(value.mutex);
    if (value.live_instance_count > 0U) {
        --value.live_instance_count;
    }
}

camstream_camera_status_t register_backend(const camstream_camera_backend_v1* backend) noexcept {
    try {
        BackendRuntime& value = runtime();
        std::lock_guard<std::mutex> lock(value.mutex);
        if (value.state == LoadState::Faulted) {
            return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
        }
        if (value.state != LoadState::Loading) {
            value.last_error = "backend registration is valid only during HAL loading";
            return CAMSTREAM_CAMERA_STATUS_INVALID_STATE;
        }
        if (value.registration_attempted) {
            value.registration_status = CAMSTREAM_CAMERA_STATUS_INVALID_STATE;
            value.last_error = "backend constructor registered more than once during one load";
            return value.registration_status;
        }
        value.registration_attempted = true;

        std::string validation_error;
        const camstream_camera_status_t validation_status = validate_backend(backend, validation_error);
        if (validation_status != CAMSTREAM_CAMERA_STATUS_OK) {
            value.registration_status = validation_status;
            value.last_error = validation_error;
            return value.registration_status;
        }

        const std::size_t name_length = bounded_string_length(backend->backend_name, kMaximumBackendNameLength + 1U);
        value.backend_name.assign(backend->backend_name, name_length);
        value.backend = backend;
        value.registration_status = CAMSTREAM_CAMERA_STATUS_OK;
        value.last_error.clear();
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

} // namespace camstream::camera::detail

extern "C" camstream_camera_status_t
camstream_camera_hal_register_backend_v1(const camstream_camera_backend_v1* backend) {
    return camstream::camera::detail::register_backend(backend);
}
