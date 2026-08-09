#include <camstream/camera/camera_backend.h>

namespace {

const camstream_camera_backend_v1 kInvalidBackend{
    CAMSTREAM_CAMERA_ABI_VERSION_V1 + 1U,
    sizeof(camstream_camera_backend_v1),
    "invalid-test-backend",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    {},
};

__attribute__((constructor)) static void register_invalid_backend() noexcept {
    static_cast<void>(camstream_camera_hal_register_backend_v1(&kInvalidBackend));
}

} // namespace
