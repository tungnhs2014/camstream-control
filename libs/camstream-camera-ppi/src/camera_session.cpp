#include <camstream/camera/camera_session.hpp>

#include "camera_backend_module.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

namespace camstream::camera {
namespace {

constexpr std::size_t kDiagnosticBufferSize = 256U;

enum class SessionState {
    Created,
    Open,
    Configured,
    Started,
    Stopped,
};

const char* state_name(SessionState state) noexcept {
    switch (state) {
    case SessionState::Created:
        return "Created";
    case SessionState::Open:
        return "Open";
    case SessionState::Configured:
        return "Configured";
    case SessionState::Started:
        return "Started";
    case SessionState::Stopped:
        return "Stopped";
    }
    return "Unknown";
}

void initialize_config(camstream_camera_stream_config_v1& config) noexcept {
    config = {};
    config.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    config.struct_size = sizeof(config);
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

[[noreturn]] void throw_contract_error(const std::string& operation, const std::string& detail) {
    throw CameraError("Camera " + operation + " contract failure: " + detail, CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR);
}

void validate_config_header(const camstream_camera_stream_config_v1& config, const std::string& operation) {
    if (config.abi_version != CAMSTREAM_CAMERA_ABI_VERSION_V1 || config.struct_size < sizeof(config)) {
        throw_contract_error(operation, "invalid stream-configuration ABI header");
    }
}

std::size_t bounded_string_length(const char* text, std::size_t maximum_length) noexcept {
    std::size_t length = 0U;
    while (length < maximum_length && text[length] != '\0') {
        ++length;
    }
    return length;
}

std::string bounded_source_identifier(const camstream_camera_stream_config_v1& config, const std::string& operation) {
    const std::size_t length = bounded_string_length(config.source_identifier, CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE);
    if (length == 0U || length >= CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE) {
        throw_contract_error(operation, "source identifier is empty or not terminated");
    }
    return std::string(config.source_identifier, length);
}

CameraStreamConfig from_ppi_config(const camstream_camera_stream_config_v1& config, const std::string& operation) {
    validate_config_header(config, operation);
    CameraStreamConfig result;
    result.source_identifier = bounded_source_identifier(config, operation);
    result.width = config.width;
    result.height = config.height;
    result.pixel_format = config.pixel_format;
    result.frame_rate_numerator = config.frame_rate_numerator;
    result.frame_rate_denominator = config.frame_rate_denominator;
    if (result.width == 0U || result.height == 0U || result.pixel_format == 0U || result.frame_rate_numerator == 0U ||
        result.frame_rate_denominator == 0U) {
        throw_contract_error(operation, "backend returned an incomplete configuration");
    }
    return result;
}

camstream_camera_stream_config_v1 to_ppi_config(const CameraStreamConfig& config) {
    if (config.source_identifier.empty() || config.source_identifier.size() >= CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE) {
        throw CameraError("Camera configure source identifier is empty or too long",
                          CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }
    if (config.width == 0U || config.height == 0U || config.pixel_format == 0U || config.frame_rate_numerator == 0U ||
        config.frame_rate_denominator == 0U) {
        throw CameraError("Camera configure dimensions, format, and frame rate must be "
                          "valid",
                          CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }

    camstream_camera_stream_config_v1 result{};
    initialize_config(result);
    std::memcpy(result.source_identifier, config.source_identifier.data(), config.source_identifier.size());
    result.source_identifier[config.source_identifier.size()] = '\0';
    result.width = config.width;
    result.height = config.height;
    result.pixel_format = config.pixel_format;
    result.frame_rate_numerator = config.frame_rate_numerator;
    result.frame_rate_denominator = config.frame_rate_denominator;
    return result;
}

void validate_ppi_frame(const camstream_camera_frame_v1& frame) {
    if (frame.abi_version != CAMSTREAM_CAMERA_ABI_VERSION_V1 || frame.struct_size < sizeof(frame)) {
        throw_contract_error("acquire_frame", "invalid frame ABI header");
    }
    if (frame.frame_token == 0U || frame.width == 0U || frame.height == 0U || frame.pixel_format == 0U ||
        frame.plane_count == 0U || frame.plane_count > CAMSTREAM_CAMERA_MAX_PLANES) {
        throw_contract_error("acquire_frame", "invalid frame metadata");
    }

    for (std::uint32_t index = 0; index < frame.plane_count; ++index) {
        const camstream_camera_plane_v1& plane = frame.planes[index];
        if (plane.abi_version != CAMSTREAM_CAMERA_ABI_VERSION_V1 || plane.struct_size < sizeof(plane) ||
            plane.data == nullptr || plane.allocation_size == 0U || plane.bytes_used == 0U ||
            plane.bytes_used > plane.allocation_size || plane.stride == 0U) {
            throw_contract_error("acquire_frame", "invalid plane metadata");
        }
    }
}

} // namespace

class CameraSession::Impl final {
  public:
    explicit Impl(std::unique_ptr<CameraBackendModule> module) : module_(std::move(module)) {}

    ~Impl() noexcept {
        cleanup();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    const camstream_camera_backend_v1& backend() const noexcept {
        return module_->descriptor();
    }

    std::string backend_diagnostic() const noexcept {
        if (instance_ == nullptr) {
            return {};
        }

        std::array<char, kDiagnosticBufferSize> buffer{};
        try {
            const camstream_camera_status_t status =
                backend().get_last_error(instance_, buffer.data(), static_cast<std::uint32_t>(buffer.size()));
            if (status == CAMSTREAM_CAMERA_STATUS_OK) {
                buffer.back() = '\0';
                return std::string(buffer.data());
            }
        } catch (...) {
        }
        return {};
    }

    [[noreturn]] void throw_backend_failure(const std::string& operation, camstream_camera_status_t status) const {
        std::ostringstream message;
        message << "Camera backend '" << module_->backend_name() << "' (" << module_->path() << ") operation "
                << operation << " failed with status " << status;
        const std::string diagnostic = backend_diagnostic();
        if (!diagnostic.empty()) {
            message << ": " << diagnostic;
        }
        throw CameraError(message.str(), status);
    }

    void require_state(SessionState required, const std::string& operation) const {
        if (state_ != required) {
            std::ostringstream message;
            message << "Camera operation " << operation << " requires " << state_name(required)
                    << " state; current state is " << state_name(state_);
            throw CameraError(message.str(), CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
        }
    }

    void cleanup() noexcept {
        if (module_ == nullptr || instance_ == nullptr) {
            return;
        }

        for (auto token = outstanding_tokens_.rbegin(); token != outstanding_tokens_.rend(); ++token) {
            try {
                static_cast<void>(backend().release_frame(instance_, *token));
            } catch (...) {
            }
        }
        outstanding_tokens_.clear();

        if (state_ == SessionState::Started) {
            try {
                static_cast<void>(backend().stop(instance_));
            } catch (...) {
            }
            state_ = SessionState::Stopped;
        }
        if (state_ == SessionState::Configured || state_ == SessionState::Open || state_ == SessionState::Stopped) {
            try {
                static_cast<void>(backend().close(instance_));
            } catch (...) {
            }
            state_ = SessionState::Created;
        }

        try {
            backend().destroy(instance_);
        } catch (...) {
            std::cerr << "Error: camera backend destroy crossed the C ABI with "
                         "an exception\n";
        }
        instance_ = nullptr;
    }

    std::unique_ptr<CameraBackendModule> module_;
    camstream_camera_instance* instance_ = nullptr;
    SessionState state_ = SessionState::Created;
    std::vector<std::uint64_t> outstanding_tokens_;
};

CameraError::CameraError(std::string message, camstream_camera_status_t status)
    : std::runtime_error(std::move(message)), status_(status) {}

camstream_camera_status_t CameraError::status() const noexcept {
    return status_;
}

CameraFrame::CameraFrame(const camstream_camera_frame_v1& frame)
    : frame_token_(frame.frame_token), sequence_number_(frame.sequence_number),
      monotonic_timestamp_ns_(frame.monotonic_timestamp_ns), width_(frame.width), height_(frame.height),
      pixel_format_(frame.pixel_format), plane_count_(frame.plane_count) {
    for (std::uint32_t index = 0; index < plane_count_; ++index) {
        planes_[index] = {
            frame.planes[index].data,
            frame.planes[index].allocation_size,
            frame.planes[index].bytes_used,
            frame.planes[index].stride,
        };
    }
}

bool CameraFrame::valid() const noexcept {
    return frame_token_ != 0U;
}

std::uint64_t CameraFrame::sequence_number() const noexcept {
    return sequence_number_;
}

std::uint64_t CameraFrame::monotonic_timestamp_ns() const noexcept {
    return monotonic_timestamp_ns_;
}

std::uint32_t CameraFrame::width() const noexcept {
    return width_;
}

std::uint32_t CameraFrame::height() const noexcept {
    return height_;
}

std::uint32_t CameraFrame::pixel_format() const noexcept {
    return pixel_format_;
}

std::uint32_t CameraFrame::plane_count() const noexcept {
    return plane_count_;
}

const CameraPlane& CameraFrame::plane(std::size_t index) const {
    if (!valid() || index >= plane_count_) {
        throw std::out_of_range("Camera frame plane index is invalid");
    }
    return planes_[index];
}

void CameraFrame::invalidate() noexcept {
    frame_token_ = 0U;
    plane_count_ = 0U;
    for (CameraPlane& plane_view : planes_) {
        plane_view = {};
    }
}

CameraSession::CameraSession(std::unique_ptr<Impl> implementation) noexcept
    : implementation_(std::move(implementation)) {}

CameraSession CameraSession::load(const std::string& backend_path) {
    auto implementation = std::make_unique<Impl>(CameraBackendModule::load(backend_path));

    camstream_camera_instance* instance = nullptr;
    camstream_camera_status_t status = CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    try {
        status = implementation->backend().create(&instance);
    } catch (...) {
        throw CameraError("Camera backend create crossed the C ABI with an "
                          "exception: " +
                              backend_path,
                          CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR);
    }

    implementation->instance_ = instance;
    if (status != CAMSTREAM_CAMERA_STATUS_OK || instance == nullptr) {
        implementation->throw_backend_failure(
            "create",
            status != CAMSTREAM_CAMERA_STATUS_OK ? status : CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR);
    }
    return CameraSession(std::move(implementation));
}

CameraSession::~CameraSession() noexcept = default;

const std::string& CameraSession::backend_name() const noexcept {
    return implementation_->module_->backend_name();
}

std::uint32_t CameraSession::backend_abi_version() const noexcept {
    return implementation_->backend().abi_version;
}

void CameraSession::open(const std::string& source_identifier) {
    implementation_->require_state(SessionState::Created, "open");
    if (source_identifier.empty() || source_identifier.size() >= CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE) {
        throw CameraError("Camera source identifier is empty or too long", CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }

    const camstream_camera_status_t status =
        implementation_->backend().open(implementation_->instance_, source_identifier.c_str());
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("open", status);
    }
    implementation_->state_ = SessionState::Open;
}

CameraCapabilities CameraSession::capabilities() const {
    if (implementation_->state_ != SessionState::Open && implementation_->state_ != SessionState::Configured) {
        implementation_->require_state(SessionState::Open, "capabilities");
    }

    camstream_camera_capabilities_v1 capabilities{};
    initialize_capabilities(capabilities);
    const camstream_camera_status_t status =
        implementation_->backend().get_capabilities(implementation_->instance_, &capabilities);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("get_capabilities", status);
    }
    if (capabilities.abi_version != CAMSTREAM_CAMERA_ABI_VERSION_V1 ||
        capabilities.struct_size < sizeof(capabilities) || capabilities.stream_config_count == 0U ||
        capabilities.maximum_plane_count == 0U || capabilities.maximum_plane_count > CAMSTREAM_CAMERA_MAX_PLANES) {
        throw_contract_error("get_capabilities", "backend returned invalid capabilities");
    }

    return {capabilities.stream_config_count, capabilities.maximum_plane_count};
}

CameraStreamConfig CameraSession::stream_configuration(std::uint32_t index) const {
    implementation_->require_state(SessionState::Open, "stream_configuration");
    camstream_camera_stream_config_v1 configuration{};
    initialize_config(configuration);
    const camstream_camera_status_t status =
        implementation_->backend().get_stream_configuration(implementation_->instance_, index, &configuration);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("get_stream_configuration", status);
    }
    return from_ppi_config(configuration, "get_stream_configuration");
}

CameraStreamConfig CameraSession::configure(const CameraStreamConfig& requested) {
    implementation_->require_state(SessionState::Open, "configure");
    const camstream_camera_stream_config_v1 request = to_ppi_config(requested);
    camstream_camera_stream_config_v1 active{};
    initialize_config(active);
    const camstream_camera_status_t status =
        implementation_->backend().configure(implementation_->instance_, &request, &active);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("configure", status);
    }
    CameraStreamConfig result = from_ppi_config(active, "configure");
    implementation_->state_ = SessionState::Configured;
    return result;
}

void CameraSession::start() {
    implementation_->require_state(SessionState::Configured, "start");
    const camstream_camera_status_t status = implementation_->backend().start(implementation_->instance_);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("start", status);
    }
    implementation_->state_ = SessionState::Started;
}

bool CameraSession::wait_frame(std::uint32_t timeout_ms) {
    implementation_->require_state(SessionState::Started, "wait_frame");
    const camstream_camera_status_t status =
        implementation_->backend().wait_frame(implementation_->instance_, timeout_ms);
    if (status == CAMSTREAM_CAMERA_STATUS_TIMEOUT) {
        return false;
    }
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("wait_frame", status);
    }
    return true;
}

CameraFrame CameraSession::acquire_frame() {
    implementation_->require_state(SessionState::Started, "acquire_frame");
    camstream_camera_frame_v1 frame{};
    initialize_frame(frame);
    const camstream_camera_status_t status =
        implementation_->backend().acquire_frame(implementation_->instance_, &frame);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("acquire_frame", status);
    }

    try {
        validate_ppi_frame(frame);
        const auto duplicate = std::find(implementation_->outstanding_tokens_.begin(),
                                         implementation_->outstanding_tokens_.end(),
                                         frame.frame_token);
        if (duplicate != implementation_->outstanding_tokens_.end()) {
            throw_contract_error("acquire_frame", "backend returned a duplicate frame token");
        }
        implementation_->outstanding_tokens_.push_back(frame.frame_token);
    } catch (...) {
        if (frame.frame_token != 0U) {
            static_cast<void>(implementation_->backend().release_frame(implementation_->instance_, frame.frame_token));
        }
        throw;
    }

    return CameraFrame(frame);
}

void CameraSession::release_frame(CameraFrame& frame) {
    implementation_->require_state(SessionState::Started, "release_frame");
    if (!frame.valid()) {
        throw CameraError("Camera frame is not outstanding", CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }

    const auto token = std::find(implementation_->outstanding_tokens_.begin(),
                                 implementation_->outstanding_tokens_.end(),
                                 frame.frame_token_);
    if (token == implementation_->outstanding_tokens_.end()) {
        throw CameraError("Camera frame does not belong to this session", CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }

    const camstream_camera_status_t status =
        implementation_->backend().release_frame(implementation_->instance_, frame.frame_token_);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("release_frame", status);
    }
    implementation_->outstanding_tokens_.erase(token);
    frame.invalidate();
}

void CameraSession::stop() {
    if (implementation_->state_ == SessionState::Stopped) {
        return;
    }
    implementation_->require_state(SessionState::Started, "stop");
    if (!implementation_->outstanding_tokens_.empty()) {
        throw CameraError("Camera stop requires all frames to be released", CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
    }

    const camstream_camera_status_t status = implementation_->backend().stop(implementation_->instance_);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("stop", status);
    }
    implementation_->state_ = SessionState::Stopped;
}

void CameraSession::close() {
    if (implementation_->state_ == SessionState::Created) {
        return;
    }
    if (implementation_->state_ == SessionState::Started) {
        throw CameraError("Camera close requires stop() first", CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
    }
    if (implementation_->state_ != SessionState::Open && implementation_->state_ != SessionState::Configured &&
        implementation_->state_ != SessionState::Stopped) {
        throw CameraError("Camera close requires an open source", CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
    }
    if (!implementation_->outstanding_tokens_.empty()) {
        throw CameraError("Camera close requires all frames to be released", CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
    }

    const camstream_camera_status_t status = implementation_->backend().close(implementation_->instance_);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation_->throw_backend_failure("close", status);
    }
    implementation_->state_ = SessionState::Created;
}

} // namespace camstream::camera
