#include "camera_backend_module.hpp"

#include <camstream/camera/camera_session.hpp>

#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

namespace camstream::camera {
namespace {

constexpr std::size_t kMaximumBackendNameLength = 127U;

using OwnedModuleHandle = std::unique_ptr<void, int (*)(void*)>;

[[noreturn]] void throw_loader_error(const std::string& backend_path, const std::string& detail) {
    std::ostringstream message;
    message << "Camera backend loader failure for '" << backend_path << "': " << detail;
    throw CameraError(message.str(), CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR);
}

bool callbacks_are_complete(const camstream_camera_backend_v1& backend) noexcept {
    return backend.create != nullptr && backend.destroy != nullptr && backend.open != nullptr &&
           backend.close != nullptr && backend.get_capabilities != nullptr &&
           backend.get_stream_configuration != nullptr && backend.configure != nullptr && backend.start != nullptr &&
           backend.wait_frame != nullptr && backend.acquire_frame != nullptr && backend.release_frame != nullptr &&
           backend.stop != nullptr && backend.get_last_error != nullptr;
}

std::size_t bounded_string_length(const char* text, std::size_t maximum_length) noexcept {
    std::size_t length = 0U;
    while (length < maximum_length && text[length] != '\0') {
        ++length;
    }
    return length;
}

} // namespace

CameraBackendModule::CameraBackendModule(void* loaded_handle,
                                         const camstream_camera_backend_v1* validated_descriptor,
                                         std::string backend_identity,
                                         std::string module_path)
    : module_handle(loaded_handle), backend_descriptor(validated_descriptor),
      validated_backend_name(std::move(backend_identity)), loaded_backend_path(std::move(module_path)) {}

std::unique_ptr<CameraBackendModule> CameraBackendModule::load(const std::string& backend_path) {
    if (backend_path.empty()) {
        throw CameraError("Camera backend path must not be empty", CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }

    dlerror();
    OwnedModuleHandle handle(dlopen(backend_path.c_str(), RTLD_NOW | RTLD_LOCAL), dlclose);
    if (handle == nullptr) {
        const char* const error = dlerror();
        throw_loader_error(backend_path, error != nullptr ? error : "dlopen failed");
    }

    dlerror();
    void* const symbol = dlsym(handle.get(), CAMSTREAM_CAMERA_BACKEND_ENTRYPOINT_V1);
    const char* const symbol_error = dlerror();
    if (symbol_error != nullptr || symbol == nullptr) {
        std::ostringstream detail;
        detail << "missing symbol " << CAMSTREAM_CAMERA_BACKEND_ENTRYPOINT_V1;
        if (symbol_error != nullptr) {
            detail << ": " << symbol_error;
        }
        throw_loader_error(backend_path, detail.str());
    }

    camstream_camera_get_backend_v1_fn entrypoint = nullptr;
    static_assert(sizeof(entrypoint) == sizeof(symbol), "POSIX function and data pointers must be equally sized");
    std::memcpy(&entrypoint, &symbol, sizeof(entrypoint));

    const camstream_camera_backend_v1* backend = nullptr;
    try {
        backend = entrypoint();
    } catch (...) {
        throw_loader_error(backend_path,
                           "backend entrypoint crossed the C ABI with an "
                           "exception");
    }

    if (backend == nullptr) {
        throw_loader_error(backend_path, "entrypoint returned null descriptor");
    }
    if (backend->abi_version != CAMSTREAM_CAMERA_ABI_VERSION_V1) {
        std::ostringstream detail;
        detail << "ABI mismatch: expected " << CAMSTREAM_CAMERA_ABI_VERSION_V1 << ", received " << backend->abi_version;
        throw_loader_error(backend_path, detail.str());
    }
    if (backend->struct_size < sizeof(camstream_camera_backend_v1)) {
        throw_loader_error(backend_path, "backend descriptor is smaller than ABI v1");
    }
    if (backend->backend_name == nullptr) {
        throw_loader_error(backend_path, "backend name is null");
    }

    const std::size_t backend_name_length =
        bounded_string_length(backend->backend_name, kMaximumBackendNameLength + 1U);
    if (backend_name_length == 0U || backend_name_length > kMaximumBackendNameLength) {
        throw_loader_error(backend_path, "backend name is empty or not bounded");
    }
    if (!callbacks_are_complete(*backend)) {
        throw_loader_error(backend_path, "backend descriptor has a missing mandatory callback");
    }

    auto module = std::unique_ptr<CameraBackendModule>(new CameraBackendModule(
        handle.get(), backend, std::string(backend->backend_name, backend_name_length), backend_path));
    static_cast<void>(handle.release());
    return module;
}

CameraBackendModule::~CameraBackendModule() noexcept {
    backend_descriptor = nullptr;
    if (module_handle != nullptr) {
        if (dlclose(module_handle) != 0) {
            const char* const error = dlerror();
            std::cerr << "Error: dlclose failed for camera backend '" << loaded_backend_path
                      << "': " << (error != nullptr ? error : "unknown error") << '\n';
        }
        module_handle = nullptr;
    }
}

const camstream_camera_backend_v1& CameraBackendModule::descriptor() const noexcept {
    return *backend_descriptor;
}

const std::string& CameraBackendModule::backend_name() const noexcept {
    return validated_backend_name;
}

const std::string& CameraBackendModule::path() const noexcept {
    return loaded_backend_path;
}

} // namespace camstream::camera
