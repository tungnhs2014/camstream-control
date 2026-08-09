#include <camstream/camera/camera_backend.h>
#include <camstream/logging.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <linux/videodev2.h>
#include <new>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr char kBackendName[] = "v4l2";

struct V4l2StreamConfiguration {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pixel_format;
    std::uint32_t frame_rate_numerator;
    std::uint32_t frame_rate_denominator;
};

struct V4l2Camera {
    int fd = -1;
    std::string source_identifier;
    std::vector<V4l2StreamConfiguration> stream_configurations;
    std::string last_error;
};

V4l2Camera* camera_from(camstream_camera_instance* instance) noexcept {
    return reinterpret_cast<V4l2Camera*>(instance);
}

bool valid_header(std::uint32_t abi_version, std::uint32_t struct_size, std::size_t required_size) noexcept {
    return abi_version == CAMSTREAM_CAMERA_ABI_VERSION_V1 && struct_size >= required_size;
}

std::size_t bounded_string_length(const char* text, std::size_t maximum_length) noexcept {
    std::size_t length = 0U;
    while (length < maximum_length && text[length] != '\0') {
        ++length;
    }
    return length;
}

int retry_ioctl(int fd, unsigned long request, void* argument) noexcept {
    int result;
    do {
        result = ::ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

void close_after_failed_open(int fd, const char* source_identifier) noexcept {
    if (fd >= 0 && ::close(fd) == -1) {
        const int error_number = errno;
        LOGW("Cleanup close failed for V4L2 source '" << (source_identifier != nullptr ? source_identifier : "unknown")
                                                      << "': " << std::strerror(error_number));
    }
}

std::string errno_message(const char* operation, int error_number) {
    std::string message(operation);
    message += " failed: ";
    message += std::strerror(error_number);
    return message;
}

template <typename Message> void retain_error(V4l2Camera* camera, const Message& message) noexcept {
    if (camera != nullptr) {
        try {
            camera->last_error = message;
        } catch (...) {
            LOGE("V4L2 backend could not retain diagnostic text");
        }
    }
}

camstream_camera_status_t set_error(V4l2Camera* camera, camstream_camera_status_t status,
                                    const std::string& message) noexcept {
    retain_error(camera, message);
    return status;
}

camstream_camera_status_t set_error(V4l2Camera* camera, camstream_camera_status_t status,
                                    const char* message) noexcept {
    retain_error(camera, message != nullptr ? message : "unknown V4L2 backend error");
    return status;
}

void clear_error(V4l2Camera* camera) noexcept {
    if (camera != nullptr) {
        camera->last_error.clear();
    }
}

bool configurations_match(const V4l2StreamConfiguration& left, const V4l2StreamConfiguration& right) noexcept {
    return left.width == right.width && left.height == right.height && left.pixel_format == right.pixel_format &&
           left.frame_rate_numerator == right.frame_rate_numerator &&
           left.frame_rate_denominator == right.frame_rate_denominator;
}

camstream_camera_status_t append_discrete_intervals(int fd, std::uint32_t v4l2_pixel_format,
                                                    std::uint32_t hal_pixel_format, std::uint32_t width,
                                                    std::uint32_t height,
                                                    std::vector<V4l2StreamConfiguration>& configurations,
                                                    std::string& error) {
    for (std::uint32_t index = 0U;; ++index) {
        v4l2_frmivalenum interval{};
        interval.index = index;
        interval.pixel_format = v4l2_pixel_format;
        interval.width = width;
        interval.height = height;
        if (retry_ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == -1) {
            if (errno == EINVAL) {
                return CAMSTREAM_CAMERA_STATUS_OK;
            }
            const int error_number = errno;
            error = errno_message("VIDIOC_ENUM_FRAMEINTERVALS", error_number);
            LOGE("VIDIOC_ENUM_FRAMEINTERVALS failed: " << std::strerror(error_number));
            return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
        }

        // HAL v1 exposes finite indexed configurations, so interval ranges remain unadvertised.
        if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE && interval.discrete.numerator != 0U &&
            interval.discrete.denominator != 0U) {
            const V4l2StreamConfiguration configuration{
                width, height, hal_pixel_format, interval.discrete.denominator, interval.discrete.numerator,
            };
            const auto duplicate = std::find_if(
                configurations.begin(), configurations.end(),
                [&configuration](const auto& existing) { return configurations_match(existing, configuration); });
            if (duplicate == configurations.end()) {
                if (configurations.size() == std::numeric_limits<std::uint32_t>::max()) {
                    error = "V4L2 stream-configuration count exceeds Camera HAL v1";
                    return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
                }
                configurations.push_back(configuration);
            }
        }

        if (index == std::numeric_limits<std::uint32_t>::max()) {
            error = "V4L2 frame-interval enumeration index overflow";
            return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
        }
    }
}

camstream_camera_status_t append_discrete_frame_sizes(int fd, std::uint32_t v4l2_pixel_format,
                                                      std::uint32_t hal_pixel_format,
                                                      std::vector<V4l2StreamConfiguration>& configurations,
                                                      std::string& error) {
    for (std::uint32_t index = 0U;; ++index) {
        v4l2_frmsizeenum frame_size{};
        frame_size.index = index;
        frame_size.pixel_format = v4l2_pixel_format;
        if (retry_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) == -1) {
            if (errno == EINVAL) {
                return CAMSTREAM_CAMERA_STATUS_OK;
            }
            const int error_number = errno;
            error = errno_message("VIDIOC_ENUM_FRAMESIZES", error_number);
            LOGE("VIDIOC_ENUM_FRAMESIZES failed: " << std::strerror(error_number));
            return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
        }

        // HAL v1 exposes finite indexed configurations, so frame-size ranges remain unadvertised.
        if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE && frame_size.discrete.width != 0U &&
            frame_size.discrete.height != 0U) {
            const camstream_camera_status_t status = append_discrete_intervals(
                fd, v4l2_pixel_format, hal_pixel_format, frame_size.discrete.width, frame_size.discrete.height,
                configurations, error);
            if (status != CAMSTREAM_CAMERA_STATUS_OK) {
                return status;
            }
        }

        if (index == std::numeric_limits<std::uint32_t>::max()) {
            error = "V4L2 frame-size enumeration index overflow";
            return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
        }
    }
}

camstream_camera_status_t enumerate_stream_configurations(int fd, std::vector<V4l2StreamConfiguration>& configurations,
                                                          std::string& error) {
    for (std::uint32_t index = 0U;; ++index) {
        v4l2_fmtdesc format{};
        format.index = index;
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (retry_ioctl(fd, VIDIOC_ENUM_FMT, &format) == -1) {
            if (errno == EINVAL) {
                break;
            }
            const int error_number = errno;
            error = errno_message("VIDIOC_ENUM_FMT", error_number);
            LOGE("VIDIOC_ENUM_FMT failed: " << std::strerror(error_number));
            return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
        }

        // YUYV is the only V4L2 pixel format represented by the current Camera HAL ABI.
        if (format.pixelformat == V4L2_PIX_FMT_YUYV) {
            const camstream_camera_status_t status = append_discrete_frame_sizes(
                fd, format.pixelformat, CAMSTREAM_CAMERA_PIXEL_FORMAT_YUYV, configurations, error);
            if (status != CAMSTREAM_CAMERA_STATUS_OK) {
                return status;
            }
        }

        if (index == std::numeric_limits<std::uint32_t>::max()) {
            error = "V4L2 format enumeration index overflow";
            return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
        }
    }

    if (configurations.empty()) {
        error = "device exposes no discrete YUYV size and frame-interval combinations representable by Camera HAL v1";
        return CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED;
    }
    return CAMSTREAM_CAMERA_STATUS_OK;
}

camstream_camera_status_t unsupported_operation(V4l2Camera* camera, const char* operation) noexcept {
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    if (camera->fd < 0) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "operation requires an open V4L2 device");
    }
    try {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED,
                         std::string(operation) + " is not implemented in Stage 8.5 Phase 1");
    } catch (...) {
        LOGE("V4L2 backend could not construct an unsupported-operation diagnostic");
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

void reset_open_state(V4l2Camera& camera) noexcept {
    camera.fd = -1;
    camera.source_identifier.clear();
    camera.stream_configurations.clear();
}

extern "C" {

static camstream_camera_status_t v4l2_create(camstream_camera_instance** instance) {
    if (instance == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    *instance = nullptr;
    try {
        V4l2Camera* const camera = new V4l2Camera();
        *instance = reinterpret_cast<camstream_camera_instance*>(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (const std::bad_alloc&) {
        LOGE("V4L2 backend instance allocation failed");
        return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    } catch (...) {
        LOGE("Unexpected exception while creating a V4L2 backend instance");
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

static void v4l2_destroy(camstream_camera_instance* instance) {
    V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr) {
        return;
    }
    if (camera->fd >= 0) {
        close_after_failed_open(camera->fd, camera->source_identifier.c_str());
        camera->fd = -1;
    }
    delete camera;
}

static camstream_camera_status_t v4l2_open(camstream_camera_instance* instance, const char* source_identifier) {
    V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr || source_identifier == nullptr || source_identifier[0] == '\0') {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "open requires a source identifier");
    }
    if (camera->fd >= 0) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "open requires a closed V4L2 instance");
    }
    const std::size_t source_length = bounded_string_length(source_identifier, CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE);
    if (source_length == 0U || source_length >= CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "source identifier is not bounded");
    }

    int fd = -1;
    try {
        fd = ::open(source_identifier, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd == -1) {
            const int error_number = errno;
            LOGE("open failed for '" << source_identifier << "': " << std::strerror(error_number));
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR, errno_message("open", error_number));
        }

        v4l2_capability capability{};
        if (retry_ioctl(fd, VIDIOC_QUERYCAP, &capability) == -1) {
            const int error_number = errno;
            const std::string error = errno_message("VIDIOC_QUERYCAP", error_number);
            LOGE("VIDIOC_QUERYCAP failed for '" << source_identifier << "': " << std::strerror(error_number));
            close_after_failed_open(fd, source_identifier);
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR, error);
        }

        const std::uint32_t device_capabilities = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0U
                                                      ? capability.device_caps
                                                      : capability.capabilities;
        if ((device_capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0U) {
            LOGE("V4L2 source '" << source_identifier << "' does not support single-planar video capture");
            close_after_failed_open(fd, source_identifier);
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED,
                             "device does not support single-planar video capture");
        }
        if ((device_capabilities & V4L2_CAP_STREAMING) == 0U) {
            LOGE("V4L2 source '" << source_identifier << "' does not support streaming I/O");
            close_after_failed_open(fd, source_identifier);
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED, "device does not support streaming I/O");
        }

        std::vector<V4l2StreamConfiguration> configurations;
        std::string enumeration_error;
        const camstream_camera_status_t enumeration_status = enumerate_stream_configurations(fd, configurations,
                                                                                             enumeration_error);
        if (enumeration_status != CAMSTREAM_CAMERA_STATUS_OK) {
            close_after_failed_open(fd, source_identifier);
            return set_error(camera, enumeration_status, enumeration_error);
        }

        camera->source_identifier.assign(source_identifier, source_length);
        camera->stream_configurations = std::move(configurations);
        camera->fd = fd;
        clear_error(camera);
        LOGI("V4L2 camera opened: " << camera->source_identifier);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (const std::bad_alloc&) {
        LOGE("Resource allocation failed while opening V4L2 source '" << source_identifier << "'");
        if (fd >= 0) {
            close_after_failed_open(fd, source_identifier);
        }
        reset_open_state(*camera);
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR, "resource allocation failed during open");
    } catch (...) {
        LOGE("Unexpected exception while opening V4L2 source '" << source_identifier << "'");
        if (fd >= 0) {
            close_after_failed_open(fd, source_identifier);
        }
        reset_open_state(*camera);
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR, "internal exception during open");
    }
}

static camstream_camera_status_t v4l2_close(camstream_camera_instance* instance) {
    V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    try {
        if (camera->fd < 0) {
            reset_open_state(*camera);
            clear_error(camera);
            return CAMSTREAM_CAMERA_STATUS_OK;
        }

        const int fd = camera->fd;
        reset_open_state(*camera);
        if (::close(fd) == -1) {
            const int error_number = errno;
            LOGE("close failed for V4L2 source: " << std::strerror(error_number));
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR, errno_message("close", error_number));
        }
        clear_error(camera);
        LOGI("V4L2 camera closed");
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        LOGE("Unexpected exception while closing a V4L2 backend instance");
        reset_open_state(*camera);
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR, "internal exception during close");
    }
}

static camstream_camera_status_t v4l2_get_capabilities(camstream_camera_instance* instance,
                                                       camstream_camera_capabilities_v1* capabilities) {
    V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr || capabilities == nullptr ||
        !valid_header(capabilities->abi_version, capabilities->struct_size, sizeof(*capabilities))) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "get_capabilities received invalid output");
    }
    if (camera->fd < 0) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "get_capabilities requires an open device");
    }

    *capabilities = {};
    capabilities->abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    capabilities->struct_size = sizeof(*capabilities);
    capabilities->stream_config_count = static_cast<std::uint32_t>(camera->stream_configurations.size());
    capabilities->maximum_plane_count = 1U;
    clear_error(camera);
    return CAMSTREAM_CAMERA_STATUS_OK;
}

static camstream_camera_status_t v4l2_get_stream_configuration(camstream_camera_instance* instance, std::uint32_t index,
                                                               camstream_camera_stream_config_v1* configuration) {
    V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr || configuration == nullptr ||
        !valid_header(configuration->abi_version, configuration->struct_size, sizeof(*configuration))) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                         "get_stream_configuration received invalid output");
    }
    if (camera->fd < 0) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                         "get_stream_configuration requires an open device");
    }
    if (index >= camera->stream_configurations.size()) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                         "stream-configuration index is out of range");
    }

    const V4l2StreamConfiguration& supported = camera->stream_configurations[index];
    *configuration = {};
    configuration->abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    configuration->struct_size = sizeof(*configuration);
    std::memcpy(configuration->source_identifier, camera->source_identifier.c_str(),
                camera->source_identifier.size() + 1U);
    configuration->width = supported.width;
    configuration->height = supported.height;
    configuration->pixel_format = supported.pixel_format;
    configuration->frame_rate_numerator = supported.frame_rate_numerator;
    configuration->frame_rate_denominator = supported.frame_rate_denominator;
    clear_error(camera);
    return CAMSTREAM_CAMERA_STATUS_OK;
}

static camstream_camera_status_t v4l2_configure(camstream_camera_instance* instance,
                                                const camstream_camera_stream_config_v1* requested,
                                                camstream_camera_stream_config_v1* active) {
    V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr || requested == nullptr || active == nullptr ||
        !valid_header(requested->abi_version, requested->struct_size, sizeof(*requested)) ||
        !valid_header(active->abi_version, active->struct_size, sizeof(*active))) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "configure received invalid arguments");
    }
    return unsupported_operation(camera, "configure");
}

static camstream_camera_status_t v4l2_start(camstream_camera_instance* instance) {
    return unsupported_operation(camera_from(instance), "start");
}

static camstream_camera_status_t v4l2_wait_frame(camstream_camera_instance* instance, std::uint32_t timeout_ms) {
    static_cast<void>(timeout_ms);
    return unsupported_operation(camera_from(instance), "wait_frame");
}

static camstream_camera_status_t v4l2_acquire_frame(camstream_camera_instance* instance,
                                                    camstream_camera_frame_v1* frame) {
    V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr || frame == nullptr ||
        !valid_header(frame->abi_version, frame->struct_size, sizeof(*frame))) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "acquire_frame received invalid output");
    }
    return unsupported_operation(camera, "acquire_frame");
}

static camstream_camera_status_t v4l2_release_frame(camstream_camera_instance* instance, std::uint64_t frame_token) {
    V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr || frame_token == 0U) {
        return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "release_frame requires a nonzero token");
    }
    return unsupported_operation(camera, "release_frame");
}

static camstream_camera_status_t v4l2_stop(camstream_camera_instance* instance) {
    return unsupported_operation(camera_from(instance), "stop");
}

static camstream_camera_status_t v4l2_get_last_error(camstream_camera_instance* instance, char* buffer,
                                                     std::uint32_t buffer_size) {
    const V4l2Camera* const camera = camera_from(instance);
    if (camera == nullptr || buffer == nullptr || buffer_size == 0U) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    const int result = std::snprintf(buffer, buffer_size, "%s", camera->last_error.c_str());
    if (result < 0) {
        buffer[0] = '\0';
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
    return CAMSTREAM_CAMERA_STATUS_OK;
}

} // extern "C"

const camstream_camera_backend_v1 kBackendDescriptor{
    CAMSTREAM_CAMERA_ABI_VERSION_V1,
    sizeof(camstream_camera_backend_v1),
    kBackendName,
    v4l2_create,
    v4l2_destroy,
    v4l2_open,
    v4l2_close,
    v4l2_get_capabilities,
    v4l2_get_stream_configuration,
    v4l2_configure,
    v4l2_start,
    v4l2_wait_frame,
    v4l2_acquire_frame,
    v4l2_release_frame,
    v4l2_stop,
    v4l2_get_last_error,
    {},
};

} // namespace

__attribute__((constructor)) static void register_v4l2_backend() noexcept {
    const camstream_camera_status_t status = camstream_camera_hal_register_backend_v1(&kBackendDescriptor);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        LOGE("V4L2 backend registration failed with status " << status);
    }
}
