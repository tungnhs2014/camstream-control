#ifndef CAMSTREAM_CAMERA_BACKEND_MODULE_HPP
#define CAMSTREAM_CAMERA_BACKEND_MODULE_HPP

#include <camstream/camera/camera_ppi.h>

#include <memory>
#include <string>

namespace camstream::camera {

/** Owns one dlopen handle and its validated module-owned descriptor. */
class CameraBackendModule final {
  public:
    static std::unique_ptr<CameraBackendModule> load(const std::string& backend_path);

    ~CameraBackendModule() noexcept;

    CameraBackendModule(const CameraBackendModule&) = delete;
    CameraBackendModule& operator=(const CameraBackendModule&) = delete;
    CameraBackendModule(CameraBackendModule&&) = delete;
    CameraBackendModule& operator=(CameraBackendModule&&) = delete;

    const camstream_camera_backend_v1& descriptor() const noexcept;
    const std::string& backend_name() const noexcept;
    const std::string& path() const noexcept;

  private:
    CameraBackendModule(void* handle,
                        const camstream_camera_backend_v1* descriptor,
                        std::string backend_name,
                        std::string backend_path);

    void* handle_ = nullptr;
    const camstream_camera_backend_v1* descriptor_ = nullptr;
    std::string backend_name_;
    std::string backend_path_;
};

} // namespace camstream::camera

#endif
