#include <camstream/camera/camera_session.hpp>

#include "camera_backend_module.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace camstream::camera {

namespace detail {
class CameraSessionIdentity final {};
} // namespace detail

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
    explicit Impl(std::unique_ptr<CameraBackendModule> backend_module)
        : module(std::move(backend_module)), owner_identity(std::make_shared<const detail::CameraSessionIdentity>()) {}

    ~Impl() noexcept {
        cleanup();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    const camstream_camera_backend_v1& backend() const noexcept {
        return module->descriptor();
    }

    std::string backend_diagnostic() const noexcept {
        if (instance == nullptr) {
            return {};
        }

        std::array<char, kDiagnosticBufferSize> buffer{};
        try {
            const camstream_camera_status_t status =
                backend().get_last_error(instance, buffer.data(), static_cast<std::uint32_t>(buffer.size()));
            if (status == CAMSTREAM_CAMERA_STATUS_OK) {
                buffer.back() = '\0';
                return std::string(buffer.data());
            }
        } catch (...) {
        }
        return {};
    }

    std::string backend_failure_message(const std::string& operation, camstream_camera_status_t status) const {
        std::ostringstream message;
        message << "Camera backend '" << module->backend_name() << "' (" << module->path() << ") operation "
                << operation << " failed with status " << status;
        const std::string diagnostic = backend_diagnostic();
        if (!diagnostic.empty()) {
            message << ": " << diagnostic;
        }
        return message.str();
    }

    [[noreturn]] void throw_backend_failure(const std::string& operation, camstream_camera_status_t status) const {
        throw CameraError(backend_failure_message(operation, status), status);
    }

    void require_state(SessionState required, const std::string& operation) const {
        if (state != required) {
            std::ostringstream message;
            message << "Camera operation " << operation << " requires " << state_name(required)
                    << " state; current state is " << state_name(state);
            throw CameraError(message.str(), CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
        }
    }

    void require_no_pending_cleanup(const std::string& operation) const {
        if (pending_cleanup_token.has_value()) {
            throw CameraError("Camera operation " + operation + " is blocked by an unresolved acquired-frame rollback",
                              CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
        }
    }

    camstream_camera_status_t release_token_noexcept(std::uint64_t token, bool& callback_threw) noexcept {
        callback_threw = false;
        try {
            return backend().release_frame(instance, token);
        } catch (...) {
            callback_threw = true;
            return CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
        }
    }

    void rollback_acquired_token(std::uint64_t token, const char* original_failure) {
        bool callback_threw = false;
        const camstream_camera_status_t rollback_status = release_token_noexcept(token, callback_threw);
        if (rollback_status == CAMSTREAM_CAMERA_STATUS_OK) {
            return;
        }

        pending_cleanup_token = token;
        std::ostringstream message;
        message << "Camera acquire_frame failed after backend acquisition: " << original_failure
                << "; rollback release_frame failed for token " << token;
        if (callback_threw) {
            message << " because the backend callback crossed the C ABI with an exception";
        } else {
            message << "; " << backend_failure_message("acquire_frame rollback release_frame", rollback_status);
        }
        throw CameraError(message.str(), rollback_status);
    }

    void retry_pending_cleanup_or_throw(const std::string& operation) {
        if (!pending_cleanup_token.has_value()) {
            return;
        }

        bool callback_threw = false;
        const camstream_camera_status_t status = release_token_noexcept(*pending_cleanup_token, callback_threw);
        if (status != CAMSTREAM_CAMERA_STATUS_OK) {
            if (callback_threw) {
                throw CameraError("Camera operation " + operation +
                                      " could not resolve pending frame cleanup because release_frame crossed the "
                                      "C ABI with an exception",
                                  CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR);
            }
            throw_backend_failure(operation + " pending cleanup release_frame", status);
        }
        pending_cleanup_token.reset();
    }

    void cleanup() noexcept {
        if (module == nullptr || instance == nullptr) {
            return;
        }

        if (pending_cleanup_token.has_value()) {
            bool callback_threw = false;
            const camstream_camera_status_t status = release_token_noexcept(*pending_cleanup_token, callback_threw);
            if (status == CAMSTREAM_CAMERA_STATUS_OK) {
                pending_cleanup_token.reset();
            } else {
                std::cerr << "Error: camera pending frame cleanup failed with status " << status;
                if (callback_threw) {
                    std::cerr << " after release_frame crossed the C ABI with an exception";
                }
                std::cerr << '\n';
            }
        }

        for (auto token = outstanding_tokens.rbegin(); token != outstanding_tokens.rend(); ++token) {
            try {
                static_cast<void>(backend().release_frame(instance, *token));
            } catch (...) {
            }
        }
        outstanding_tokens.clear();

        if (state == SessionState::Started) {
            try {
                static_cast<void>(backend().stop(instance));
            } catch (...) {
            }
            state = SessionState::Stopped;
        }
        if (state == SessionState::Configured || state == SessionState::Open || state == SessionState::Stopped) {
            try {
                static_cast<void>(backend().close(instance));
            } catch (...) {
            }
            state = SessionState::Created;
        }

        try {
            backend().destroy(instance);
        } catch (...) {
            std::cerr << "Error: camera backend destroy crossed the C ABI with "
                         "an exception\n";
        }
        instance = nullptr;
    }

    std::unique_ptr<CameraBackendModule> module;
    std::shared_ptr<const detail::CameraSessionIdentity> owner_identity;
    camstream_camera_instance* instance = nullptr;
    SessionState state = SessionState::Created;
    std::vector<std::uint64_t> outstanding_tokens;
    std::optional<std::uint64_t> pending_cleanup_token;
};

CameraError::CameraError(std::string message, camstream_camera_status_t originating_status)
    : std::runtime_error(std::move(message)), error_status(originating_status) {}

camstream_camera_status_t CameraError::status() const noexcept {
    return error_status;
}

CameraFrame::CameraFrame(const camstream_camera_frame_v1& frame,
                         std::shared_ptr<const detail::CameraSessionIdentity> session_identity) noexcept
    : owner_identity(std::move(session_identity)), frame_token(frame.frame_token),
      frame_sequence_number(frame.sequence_number), capture_timestamp_ns(frame.monotonic_timestamp_ns),
      frame_width(frame.width), frame_height(frame.height), frame_pixel_format(frame.pixel_format),
      frame_plane_count(frame.plane_count) {
    for (std::uint32_t index = 0; index < frame_plane_count; ++index) {
        plane_views[index] = {
            frame.planes[index].data,
            frame.planes[index].allocation_size,
            frame.planes[index].bytes_used,
            frame.planes[index].stride,
        };
    }
}

bool CameraFrame::valid() const noexcept {
    return frame_token != 0U;
}

std::uint64_t CameraFrame::sequence_number() const noexcept {
    return frame_sequence_number;
}

std::uint64_t CameraFrame::monotonic_timestamp_ns() const noexcept {
    return capture_timestamp_ns;
}

std::uint32_t CameraFrame::width() const noexcept {
    return frame_width;
}

std::uint32_t CameraFrame::height() const noexcept {
    return frame_height;
}

std::uint32_t CameraFrame::pixel_format() const noexcept {
    return frame_pixel_format;
}

std::uint32_t CameraFrame::plane_count() const noexcept {
    return frame_plane_count;
}

const CameraPlane& CameraFrame::plane(std::size_t index) const {
    if (!valid() || index >= frame_plane_count) {
        throw std::out_of_range("Camera frame plane index is invalid");
    }
    return plane_views[index];
}

void CameraFrame::invalidate() noexcept {
    owner_identity.reset();
    frame_token = 0U;
    frame_plane_count = 0U;
    for (CameraPlane& plane_view : plane_views) {
        plane_view = {};
    }
}

CameraSession::CameraSession(std::unique_ptr<Impl> session_implementation) noexcept
    : implementation(std::move(session_implementation)) {}

CameraSession CameraSession::load(const std::string& backend_path) {
    auto session_implementation = std::make_unique<Impl>(CameraBackendModule::load(backend_path));

    camstream_camera_instance* instance = nullptr;
    camstream_camera_status_t status = CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR;
    try {
        status = session_implementation->backend().create(&instance);
    } catch (...) {
        throw CameraError("Camera backend create crossed the C ABI with an "
                          "exception: " +
                              backend_path,
                          CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR);
    }

    session_implementation->instance = instance;
    if (status != CAMSTREAM_CAMERA_STATUS_OK || instance == nullptr) {
        session_implementation->throw_backend_failure(
            "create", status != CAMSTREAM_CAMERA_STATUS_OK ? status : CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR);
    }
    return CameraSession(std::move(session_implementation));
}

CameraSession::~CameraSession() noexcept = default;

const std::string& CameraSession::backend_name() const noexcept {
    return implementation->module->backend_name();
}

std::uint32_t CameraSession::backend_abi_version() const noexcept {
    return implementation->backend().abi_version;
}

void CameraSession::open(const std::string& source_identifier) {
    implementation->require_state(SessionState::Created, "open");
    if (source_identifier.empty() || source_identifier.size() >= CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE) {
        throw CameraError("Camera source identifier is empty or too long", CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }

    const camstream_camera_status_t status =
        implementation->backend().open(implementation->instance, source_identifier.c_str());
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("open", status);
    }
    implementation->state = SessionState::Open;
}

CameraCapabilities CameraSession::capabilities() const {
    if (implementation->state != SessionState::Open && implementation->state != SessionState::Configured) {
        implementation->require_state(SessionState::Open, "capabilities");
    }

    camstream_camera_capabilities_v1 capabilities{};
    initialize_capabilities(capabilities);
    const camstream_camera_status_t status =
        implementation->backend().get_capabilities(implementation->instance, &capabilities);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("get_capabilities", status);
    }
    if (capabilities.abi_version != CAMSTREAM_CAMERA_ABI_VERSION_V1 ||
        capabilities.struct_size < sizeof(capabilities) || capabilities.stream_config_count == 0U ||
        capabilities.maximum_plane_count == 0U || capabilities.maximum_plane_count > CAMSTREAM_CAMERA_MAX_PLANES) {
        throw_contract_error("get_capabilities", "backend returned invalid capabilities");
    }

    return {capabilities.stream_config_count, capabilities.maximum_plane_count};
}

CameraStreamConfig CameraSession::stream_configuration(std::uint32_t index) const {
    implementation->require_state(SessionState::Open, "stream_configuration");
    camstream_camera_stream_config_v1 configuration{};
    initialize_config(configuration);
    const camstream_camera_status_t status =
        implementation->backend().get_stream_configuration(implementation->instance, index, &configuration);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("get_stream_configuration", status);
    }
    return from_ppi_config(configuration, "get_stream_configuration");
}

CameraStreamConfig CameraSession::configure(const CameraStreamConfig& requested) {
    implementation->require_state(SessionState::Open, "configure");
    const camstream_camera_stream_config_v1 request = to_ppi_config(requested);
    camstream_camera_stream_config_v1 active{};
    initialize_config(active);
    const camstream_camera_status_t status =
        implementation->backend().configure(implementation->instance, &request, &active);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("configure", status);
    }
    CameraStreamConfig result = from_ppi_config(active, "configure");
    implementation->state = SessionState::Configured;
    return result;
}

void CameraSession::start() {
    implementation->require_state(SessionState::Configured, "start");
    const camstream_camera_status_t status = implementation->backend().start(implementation->instance);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("start", status);
    }
    implementation->state = SessionState::Started;
}

bool CameraSession::wait_frame(std::uint32_t timeout_ms) {
    implementation->require_state(SessionState::Started, "wait_frame");
    implementation->require_no_pending_cleanup("wait_frame");
    const camstream_camera_status_t status = implementation->backend().wait_frame(implementation->instance, timeout_ms);
    if (status == CAMSTREAM_CAMERA_STATUS_TIMEOUT) {
        return false;
    }
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("wait_frame", status);
    }
    return true;
}

CameraFrame CameraSession::acquire_frame() {
    implementation->require_state(SessionState::Started, "acquire_frame");
    implementation->require_no_pending_cleanup("acquire_frame");
    camstream_camera_frame_v1 frame{};
    initialize_frame(frame);
    const camstream_camera_status_t status = implementation->backend().acquire_frame(implementation->instance, &frame);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("acquire_frame", status);
    }

    try {
        validate_ppi_frame(frame);
        const auto duplicate = std::find(
            implementation->outstanding_tokens.begin(), implementation->outstanding_tokens.end(), frame.frame_token);
        if (duplicate != implementation->outstanding_tokens.end()) {
            throw_contract_error("acquire_frame", "backend returned a duplicate frame token");
        }
        implementation->outstanding_tokens.push_back(frame.frame_token);
    } catch (const std::exception& exception) {
        if (frame.frame_token != 0U) {
            implementation->rollback_acquired_token(frame.frame_token, exception.what());
        }
        throw;
    } catch (...) {
        if (frame.frame_token != 0U) {
            implementation->rollback_acquired_token(frame.frame_token, "unknown wrapper exception");
        }
        throw;
    }

    return CameraFrame(frame, implementation->owner_identity);
}

void CameraSession::release_frame(CameraFrame& frame) {
    implementation->require_state(SessionState::Started, "release_frame");
    if (!frame.valid()) {
        throw CameraError("Camera frame is not outstanding", CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }
    if (frame.owner_identity != implementation->owner_identity) {
        throw CameraError("Camera frame belongs to a different session", CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }

    const auto token = std::find(
        implementation->outstanding_tokens.begin(), implementation->outstanding_tokens.end(), frame.frame_token);
    if (token == implementation->outstanding_tokens.end()) {
        throw CameraError("Camera frame does not belong to this session", CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT);
    }

    const camstream_camera_status_t status =
        implementation->backend().release_frame(implementation->instance, frame.frame_token);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("release_frame", status);
    }
    implementation->outstanding_tokens.erase(token);
    frame.invalidate();
}

void CameraSession::stop() {
    if (implementation->state == SessionState::Stopped) {
        return;
    }
    implementation->require_state(SessionState::Started, "stop");
    implementation->retry_pending_cleanup_or_throw("stop");
    if (!implementation->outstanding_tokens.empty()) {
        throw CameraError("Camera stop requires all frames to be released", CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
    }

    const camstream_camera_status_t status = implementation->backend().stop(implementation->instance);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("stop", status);
    }
    implementation->state = SessionState::Stopped;
}

void CameraSession::close() {
    if (implementation->state == SessionState::Created) {
        return;
    }
    if (implementation->state == SessionState::Started) {
        throw CameraError("Camera close requires stop() first", CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
    }
    if (implementation->state != SessionState::Open && implementation->state != SessionState::Configured &&
        implementation->state != SessionState::Stopped) {
        throw CameraError("Camera close requires an open source", CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
    }
    if (!implementation->outstanding_tokens.empty()) {
        throw CameraError("Camera close requires all frames to be released", CAMSTREAM_CAMERA_STATUS_INVALID_STATE);
    }

    const camstream_camera_status_t status = implementation->backend().close(implementation->instance);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        implementation->throw_backend_failure("close", status);
    }
    implementation->state = SessionState::Created;
}

} // namespace camstream::camera
