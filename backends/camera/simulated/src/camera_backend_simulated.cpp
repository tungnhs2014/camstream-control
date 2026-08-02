#include <camstream/camera/camera_ppi.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr char kBackendName[] = "simulated";
constexpr char kSourceIdentifier[] = "simulated0";
constexpr std::uint32_t kWidth = 64U;
constexpr std::uint32_t kHeight = 48U;
constexpr std::uint32_t kFrameRateNumerator = 30U;
constexpr std::uint32_t kFrameRateDenominator = 1U;
constexpr std::uint32_t kBytesPerPixel = 2U;
constexpr std::uint32_t kStride = kWidth * kBytesPerPixel;
constexpr std::size_t kFrameSize = static_cast<std::size_t>(kStride) * kHeight;
constexpr std::size_t kBufferCount = 4U;

enum class SimulatedState {
    Created,
    Open,
    Configured,
    Started,
    Stopped,
};

struct SimulatedBuffer {
    std::vector<std::uint8_t> storage;
    std::uint64_t token = 0U;
    bool outstanding = false;
};

struct SimulatedCamera {
    SimulatedCamera() {
        for (SimulatedBuffer& buffer : buffers) {
            buffer.storage.resize(kFrameSize);
        }
    }

    SimulatedState state = SimulatedState::Created;
    std::array<SimulatedBuffer, kBufferCount> buffers;
    std::string last_error;
    std::uint64_t next_sequence = 0U;
    std::uint64_t next_token = 1U;
    std::size_t next_buffer = 0U;
};

SimulatedCamera* camera_from(camstream_camera_instance* instance) noexcept {
    return reinterpret_cast<SimulatedCamera*>(instance);
}

camstream_camera_status_t set_error(SimulatedCamera* camera,
                                    camstream_camera_status_t status,
                                    const char* message) noexcept {
    if (camera != nullptr) {
        try {
            camera->last_error = message != nullptr ? message : "unknown error";
        } catch (...) {
        }
    }
    return status;
}

void clear_error(SimulatedCamera* camera) noexcept {
    if (camera != nullptr) {
        try {
            camera->last_error.clear();
        } catch (...) {
        }
    }
}

camstream_camera_status_t internal_exception(SimulatedCamera* camera, const char* operation) noexcept {
    char message[128]{};
    static_cast<void>(std::snprintf(message,
                                    sizeof(message),
                                    "internal exception in %s",
                                    operation != nullptr ? operation : "backend callback"));
    return set_error(camera, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR, message);
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

void fill_supported_configuration(camstream_camera_stream_config_v1& configuration) noexcept {
    configuration = {};
    configuration.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    configuration.struct_size = sizeof(configuration);
    std::memcpy(configuration.source_identifier, kSourceIdentifier, sizeof(kSourceIdentifier));
    configuration.width = kWidth;
    configuration.height = kHeight;
    configuration.pixel_format = CAMSTREAM_CAMERA_PIXEL_FORMAT_YUYV;
    configuration.frame_rate_numerator = kFrameRateNumerator;
    configuration.frame_rate_denominator = kFrameRateDenominator;
}

bool configuration_is_supported(const camstream_camera_stream_config_v1& configuration) noexcept {
    if (!valid_header(configuration.abi_version, configuration.struct_size, sizeof(configuration))) {
        return false;
    }
    const std::size_t source_length =
        bounded_string_length(configuration.source_identifier, CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE);
    return source_length == sizeof(kSourceIdentifier) - 1U &&
           std::memcmp(configuration.source_identifier, kSourceIdentifier, sizeof(kSourceIdentifier)) == 0 &&
           configuration.width == kWidth && configuration.height == kHeight &&
           configuration.pixel_format == CAMSTREAM_CAMERA_PIXEL_FORMAT_YUYV &&
           configuration.frame_rate_numerator == kFrameRateNumerator &&
           configuration.frame_rate_denominator == kFrameRateDenominator;
}

bool has_outstanding_frame(const SimulatedCamera& camera) noexcept {
    for (const SimulatedBuffer& buffer : camera.buffers) {
        if (buffer.outstanding) {
            return true;
        }
    }
    return false;
}

bool has_available_buffer(const SimulatedCamera& camera) noexcept {
    for (const SimulatedBuffer& buffer : camera.buffers) {
        if (!buffer.outstanding) {
            return true;
        }
    }
    return false;
}

SimulatedBuffer* find_available_buffer(SimulatedCamera& camera) noexcept {
    for (std::size_t offset = 0U; offset < camera.buffers.size(); ++offset) {
        const std::size_t index = (camera.next_buffer + offset) % camera.buffers.size();
        if (!camera.buffers[index].outstanding) {
            camera.next_buffer = (index + 1U) % camera.buffers.size();
            return &camera.buffers[index];
        }
    }
    return nullptr;
}

SimulatedBuffer* find_token(SimulatedCamera& camera, std::uint64_t token) noexcept {
    for (SimulatedBuffer& buffer : camera.buffers) {
        if (buffer.outstanding && buffer.token == token) {
            return &buffer;
        }
    }
    return nullptr;
}

extern "C" camstream_camera_status_t simulated_create(camstream_camera_instance** instance) {
    if (instance == nullptr) {
        return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
    }
    *instance = nullptr;
    try {
        SimulatedCamera* const camera = new SimulatedCamera();
        *instance = reinterpret_cast<camstream_camera_instance*>(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR;
    } catch (...) {
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

extern "C" void simulated_destroy(camstream_camera_instance* instance) {
    try {
        delete camera_from(instance);
    } catch (...) {
    }
}

extern "C" camstream_camera_status_t simulated_open(camstream_camera_instance* instance,
                                                    const char* source_identifier) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr || source_identifier == nullptr || source_identifier[0] == '\0') {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "open requires a source identifier");
        }
        if (camera->state != SimulatedState::Created) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "open requires Created state");
        }
        if (std::strcmp(source_identifier, kSourceIdentifier) != 0) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED,
                             "simulated backend supports only source simulated0");
        }

        camera->state = SimulatedState::Open;
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "open");
    }
}

extern "C" camstream_camera_status_t simulated_close(camstream_camera_instance* instance) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr) {
            return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
        }
        if (camera->state == SimulatedState::Created) {
            clear_error(camera);
            return CAMSTREAM_CAMERA_STATUS_OK;
        }
        if (camera->state == SimulatedState::Started) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "close requires stop first");
        }
        if (has_outstanding_frame(*camera)) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                             "close requires every frame to be released");
        }

        camera->state = SimulatedState::Created;
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "close");
    }
}

extern "C" camstream_camera_status_t simulated_get_capabilities(camstream_camera_instance* instance,
                                                                camstream_camera_capabilities_v1* capabilities) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr || capabilities == nullptr ||
            !valid_header(capabilities->abi_version, capabilities->struct_size, sizeof(*capabilities))) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                             "get_capabilities received invalid output storage");
        }
        if (camera->state != SimulatedState::Open && camera->state != SimulatedState::Configured) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "get_capabilities requires an open source");
        }

        *capabilities = {};
        capabilities->abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
        capabilities->struct_size = sizeof(*capabilities);
        capabilities->stream_config_count = 1U;
        capabilities->maximum_plane_count = 1U;
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "get_capabilities");
    }
}

extern "C" camstream_camera_status_t simulated_get_stream_configuration(
    camstream_camera_instance* instance,
    std::uint32_t index,
    camstream_camera_stream_config_v1* configuration) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr || configuration == nullptr ||
            !valid_header(configuration->abi_version, configuration->struct_size, sizeof(*configuration))) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                             "get_stream_configuration received invalid output storage");
        }
        if (camera->state != SimulatedState::Open) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_INVALID_STATE,
                             "get_stream_configuration requires Open state");
        }
        if (index != 0U) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                             "stream-configuration index is out of range");
        }

        fill_supported_configuration(*configuration);
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "get_stream_configuration");
    }
}

extern "C" camstream_camera_status_t simulated_configure(camstream_camera_instance* instance,
                                                         const camstream_camera_stream_config_v1* requested,
                                                         camstream_camera_stream_config_v1* active) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr || requested == nullptr || active == nullptr ||
            !valid_header(active->abi_version, active->struct_size, sizeof(*active))) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT, "configure received invalid arguments");
        }
        if (camera->state != SimulatedState::Open) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "configure requires Open state");
        }
        if (!configuration_is_supported(*requested)) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED,
                             "simulated backend supports only simulated0 64x48 YUYV at "
                             "30/1");
        }

        fill_supported_configuration(*active);
        camera->state = SimulatedState::Configured;
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "configure");
    }
}

extern "C" camstream_camera_status_t simulated_start(camstream_camera_instance* instance) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr) {
            return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
        }
        if (camera->state != SimulatedState::Configured) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "start requires Configured state");
        }

        camera->next_sequence = 0U;
        camera->next_token = 1U;
        camera->next_buffer = 0U;
        for (SimulatedBuffer& buffer : camera->buffers) {
            buffer.token = 0U;
            buffer.outstanding = false;
        }
        camera->state = SimulatedState::Started;
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "start");
    }
}

extern "C" camstream_camera_status_t simulated_wait_frame(camstream_camera_instance* instance,
                                                          std::uint32_t timeout_ms) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        static_cast<void>(timeout_ms);
        if (camera == nullptr) {
            return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
        }
        if (camera->state != SimulatedState::Started) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "wait_frame requires Started state");
        }
        if (!has_available_buffer(*camera)) {
            return CAMSTREAM_CAMERA_STATUS_TIMEOUT;
        }

        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "wait_frame");
    }
}

extern "C" camstream_camera_status_t simulated_acquire_frame(camstream_camera_instance* instance,
                                                             camstream_camera_frame_v1* frame) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr || frame == nullptr ||
            !valid_header(frame->abi_version, frame->struct_size, sizeof(*frame))) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                             "acquire_frame received invalid output storage");
        }
        if (camera->state != SimulatedState::Started) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "acquire_frame requires Started state");
        }

        SimulatedBuffer* const buffer = find_available_buffer(*camera);
        if (buffer == nullptr) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR,
                             "all simulated frame buffers are outstanding");
        }

        const std::uint64_t sequence = camera->next_sequence++;
        for (std::size_t index = 0U; index < buffer->storage.size(); ++index) {
            buffer->storage[index] =
                static_cast<std::uint8_t>((sequence + static_cast<std::uint64_t>(index)) & UINT64_C(0xff));
        }

        buffer->token = camera->next_token++;
        buffer->outstanding = true;
        const auto timestamp =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());

        *frame = {};
        frame->abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
        frame->struct_size = sizeof(*frame);
        frame->frame_token = buffer->token;
        frame->sequence_number = sequence;
        frame->monotonic_timestamp_ns = static_cast<std::uint64_t>(timestamp.count());
        frame->width = kWidth;
        frame->height = kHeight;
        frame->pixel_format = CAMSTREAM_CAMERA_PIXEL_FORMAT_YUYV;
        frame->plane_count = 1U;
        frame->planes[0].abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
        frame->planes[0].struct_size = sizeof(frame->planes[0]);
        frame->planes[0].data = buffer->storage.data();
        frame->planes[0].allocation_size = buffer->storage.size();
        frame->planes[0].bytes_used = buffer->storage.size();
        frame->planes[0].stride = kStride;
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "acquire_frame");
    }
}

extern "C" camstream_camera_status_t simulated_release_frame(camstream_camera_instance* instance,
                                                             std::uint64_t frame_token) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr || frame_token == 0U) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                             "release_frame requires a nonzero token");
        }
        if (camera->state != SimulatedState::Started) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "release_frame requires Started state");
        }

        SimulatedBuffer* const buffer = find_token(*camera, frame_token);
        if (buffer == nullptr) {
            return set_error(camera,
                             CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT,
                             "frame token is unknown or already released");
        }
        buffer->outstanding = false;
        buffer->token = 0U;
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "release_frame");
    }
}

extern "C" camstream_camera_status_t simulated_stop(camstream_camera_instance* instance) {
    SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr) {
            return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
        }
        if (camera->state == SimulatedState::Stopped) {
            clear_error(camera);
            return CAMSTREAM_CAMERA_STATUS_OK;
        }
        if (camera->state != SimulatedState::Started) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "stop requires Started state");
        }
        if (has_outstanding_frame(*camera)) {
            return set_error(camera, CAMSTREAM_CAMERA_STATUS_INVALID_STATE, "stop requires every frame to be released");
        }

        camera->state = SimulatedState::Stopped;
        clear_error(camera);
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        return internal_exception(camera, "stop");
    }
}

extern "C" camstream_camera_status_t simulated_get_last_error(camstream_camera_instance* instance,
                                                              char* buffer,
                                                              std::uint32_t buffer_size) {
    const SimulatedCamera* const camera = camera_from(instance);
    try {
        if (camera == nullptr || buffer == nullptr || buffer_size == 0U) {
            return CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT;
        }
        const int result = std::snprintf(buffer, buffer_size, "%s", camera->last_error.c_str());
        if (result < 0) {
            buffer[0] = '\0';
            return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
        }
        return CAMSTREAM_CAMERA_STATUS_OK;
    } catch (...) {
        if (buffer != nullptr && buffer_size > 0U) {
            buffer[0] = '\0';
        }
        return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    }
}

const camstream_camera_backend_v1 kBackendDescriptor{
    CAMSTREAM_CAMERA_ABI_VERSION_V1,
    sizeof(camstream_camera_backend_v1),
    kBackendName,
    simulated_create,
    simulated_destroy,
    simulated_open,
    simulated_close,
    simulated_get_capabilities,
    simulated_get_stream_configuration,
    simulated_configure,
    simulated_start,
    simulated_wait_frame,
    simulated_acquire_frame,
    simulated_release_frame,
    simulated_stop,
    simulated_get_last_error,
    {},
};

} // namespace

#if defined(__GNUC__)
#define CAMSTREAM_CAMERA_BACKEND_EXPORT __attribute__((visibility("default")))
#else
#define CAMSTREAM_CAMERA_BACKEND_EXPORT
#endif

extern "C" CAMSTREAM_CAMERA_BACKEND_EXPORT const camstream_camera_backend_v1* camstream_camera_get_backend_v1(void) {
    return &kBackendDescriptor;
}
