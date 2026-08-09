#ifndef CAMSTREAM_CAMERA_BACKEND_RUNTIME_HPP
#define CAMSTREAM_CAMERA_BACKEND_RUNTIME_HPP

#include <camstream/camera/camera_backend.h>

#include <cstdint>

namespace camstream::camera::detail {

camstream_camera_status_t load_backend(const char* backend_path) noexcept;
camstream_camera_status_t unload_backend() noexcept;
camstream_camera_status_t copy_runtime_error(char* buffer, std::uint32_t buffer_size) noexcept;
void set_runtime_error(const char* error) noexcept;
camstream_camera_status_t register_backend(const camstream_camera_backend_v1* backend) noexcept;
const camstream_camera_backend_v1* active_backend() noexcept;
void record_instance_created() noexcept;
void record_instance_destroyed() noexcept;

} // namespace camstream::camera::detail

#endif
