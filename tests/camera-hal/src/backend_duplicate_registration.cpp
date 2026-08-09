#include <camstream/camera/camera_backend.h>

namespace {

const camstream_camera_backend_v1 kDuplicateTestDescriptor{
    CAMSTREAM_CAMERA_ABI_VERSION_V1 + 1U,
    sizeof(camstream_camera_backend_v1),
    "duplicate-test-backend",
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

__attribute__((constructor)) static void register_backend_twice() noexcept {
    static_cast<void>(camstream_camera_hal_register_backend_v1(&kDuplicateTestDescriptor));
    static_cast<void>(camstream_camera_hal_register_backend_v1(&kDuplicateTestDescriptor));
}

} // namespace
