#include <camstream/camera/camera_hal.h>
#include <camstream/logging.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::uint32_t kDefaultFrameCount = 3U;
constexpr std::uint32_t kFrameWaitTimeoutMs = 1000U;
constexpr std::size_t kDiagnosticBufferSize = 256U;

struct CommandLine {
    std::string backend_path;
    std::string source_identifier;
    std::uint32_t frame_count = kDefaultFrameCount;
    bool show_help = false;
    bool inspect = false;
};

/** @brief Owns only the diagnostic's HAL instance and module reference cleanup. */
struct DiagnosticResources {
    ~DiagnosticResources() noexcept {
        camstream_camera_destroy(camera);
        if (backend_loaded) {
            const camstream_camera_status_t status = camstream_camera_hal_unload_backend();
            if (status != CAMSTREAM_CAMERA_STATUS_OK) {
                LOGE("Fallback camera backend unload failed with status " << status);
            }
        }
    }

    DiagnosticResources() = default;
    DiagnosticResources(const DiagnosticResources&) = delete;
    DiagnosticResources& operator=(const DiagnosticResources&) = delete;
    DiagnosticResources(DiagnosticResources&&) = delete;
    DiagnosticResources& operator=(DiagnosticResources&&) = delete;

    camstream_camera* camera = nullptr;
    bool backend_loaded = false;
};

void print_usage(std::ostream& output) {
    output << "Usage: camstream-camera-test --backend <path> --source <source-identifier> "
              "[--frames <positive-count>] [--inspect]\n"
              "       camstream-camera-test --help\n";
}

bool parse_positive_count(std::string_view text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }

    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed == 0U ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_command_line(int argc, char* argv[], CommandLine& command_line, std::string& error) {
    bool backend_seen = false;
    bool source_seen = false;
    bool frames_seen = false;
    bool inspect_seen = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            if (argc != 2) {
                error = "--help cannot be combined with other arguments";
                return false;
            }
            command_line.show_help = true;
            return true;
        }
        if (argument == "--backend") {
            if (backend_seen) {
                error = "duplicate --backend option";
                return false;
            }
            if (index + 1 >= argc) {
                error = "--backend requires a path";
                return false;
            }
            command_line.backend_path = argv[++index];
            if (command_line.backend_path.empty()) {
                error = "--backend path must not be empty";
                return false;
            }
            backend_seen = true;
            continue;
        }
        if (argument == "--source") {
            if (source_seen) {
                error = "duplicate --source option";
                return false;
            }
            if (index + 1 >= argc) {
                error = "--source requires an identifier";
                return false;
            }
            command_line.source_identifier = argv[++index];
            if (command_line.source_identifier.empty()) {
                error = "--source identifier must not be empty";
                return false;
            }
            source_seen = true;
            continue;
        }
        if (argument == "--frames") {
            if (frames_seen) {
                error = "duplicate --frames option";
                return false;
            }
            if (index + 1 >= argc || !parse_positive_count(argv[index + 1], command_line.frame_count)) {
                error = "--frames requires a positive integer";
                return false;
            }
            ++index;
            frames_seen = true;
            continue;
        }
        if (argument == "--inspect") {
            if (inspect_seen) {
                error = "duplicate --inspect option";
                return false;
            }
            command_line.inspect = true;
            inspect_seen = true;
            continue;
        }

        error = "unknown option or positional argument: " + std::string(argument);
        return false;
    }

    if (!backend_seen) {
        error = "--backend is required";
        return false;
    }
    if (!source_seen) {
        error = "--source is required";
        return false;
    }
    return true;
}

std::string fourcc_string(std::uint32_t pixel_format) {
    std::string result(4U, ' ');
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        result[index] = static_cast<char>((pixel_format >> (index * 8U)) & UINT32_C(0xff));
    }
    return result;
}

void initialize_configuration(camstream_camera_stream_config_v1& configuration) noexcept {
    configuration = {};
    configuration.abi_version = CAMSTREAM_CAMERA_ABI_VERSION_V1;
    configuration.struct_size = sizeof(configuration);
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
}

void print_configuration(const camstream_camera_stream_config_v1& configuration, std::uint32_t index) {
    std::cout << "Stream configuration[" << index << "]:\n"
              << "  Source: " << configuration.source_identifier << '\n'
              << "  Resolution: " << configuration.width << 'x' << configuration.height << '\n'
              << "  Pixel format: " << fourcc_string(configuration.pixel_format) << '\n'
              << "  Frame rate: " << configuration.frame_rate_numerator << '/' << configuration.frame_rate_denominator
              << '\n';
}

int report_failure(const DiagnosticResources& resources, const char* operation, camstream_camera_status_t status) {
    std::array<char, kDiagnosticBufferSize> diagnostic{};
    if (resources.camera != nullptr) {
        static_cast<void>(camstream_camera_get_last_error(resources.camera, diagnostic.data(),
                                                          static_cast<std::uint32_t>(diagnostic.size())));
    } else {
        static_cast<void>(
            camstream_camera_hal_get_last_error(diagnostic.data(), static_cast<std::uint32_t>(diagnostic.size())));
    }

    if (diagnostic[0] != '\0') {
        LOGE("Camera operation " << operation << " failed with status " << status << ": " << diagnostic.data());
    } else {
        LOGE("Camera operation " << operation << " failed with status " << status);
    }
    return 1;
}

int close_destroy_and_unload(DiagnosticResources& resources) {
    const camstream_camera_status_t close_status = camstream_camera_close(resources.camera);
    if (close_status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "close", close_status);
    }

    camstream_camera_destroy(resources.camera);
    resources.camera = nullptr;
    const camstream_camera_status_t unload_status = camstream_camera_hal_unload_backend();
    if (unload_status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "unload_backend", unload_status);
    }
    resources.backend_loaded = false;
    return 0;
}

int run_diagnostic(const CommandLine& command_line) {
    DiagnosticResources resources;
    camstream_camera_status_t status = camstream_camera_hal_load_backend(command_line.backend_path.c_str());
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "load_backend", status);
    }
    resources.backend_loaded = true;

    status = camstream_camera_create(&resources.camera);
    if (status != CAMSTREAM_CAMERA_STATUS_OK || resources.camera == nullptr) {
        return report_failure(resources, "create",
                              status != CAMSTREAM_CAMERA_STATUS_OK ? status : CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR);
    }

    std::array<char, 128U> backend_name{};
    std::uint32_t backend_abi_version = 0U;
    status = camstream_camera_get_backend_name(resources.camera, backend_name.data(),
                                               static_cast<std::uint32_t>(backend_name.size()));
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "get_backend_name", status);
    }
    status = camstream_camera_get_backend_abi_version(resources.camera, &backend_abi_version);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "get_backend_abi_version", status);
    }
    std::cout << "Backend:\n"
              << "  Name: " << backend_name.data() << '\n'
              << "  ABI version: " << backend_abi_version << '\n'
              << "  Source: " << command_line.source_identifier << '\n';

    status = camstream_camera_open(resources.camera, command_line.source_identifier.c_str());
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "open", status);
    }

    camstream_camera_capabilities_v1 capabilities{};
    initialize_capabilities(capabilities);
    status = camstream_camera_get_capabilities(resources.camera, &capabilities);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "get_capabilities", status);
    }
    std::cout << "Capabilities:\n"
              << "  Stream configurations: " << capabilities.stream_config_count << '\n'
              << "  Maximum planes: " << capabilities.maximum_plane_count << '\n';

    if (command_line.inspect) {
        for (std::uint32_t index = 0U; index < capabilities.stream_config_count; ++index) {
            camstream_camera_stream_config_v1 configuration{};
            initialize_configuration(configuration);
            status = camstream_camera_get_stream_configuration(resources.camera, index, &configuration);
            if (status != CAMSTREAM_CAMERA_STATUS_OK) {
                return report_failure(resources, "get_stream_configuration", status);
            }
            print_configuration(configuration, index);
        }

        const int cleanup_result = close_destroy_and_unload(resources);
        if (cleanup_result != 0) {
            return cleanup_result;
        }
        std::cout << "Camera HAL inspection: complete\n";
        return 0;
    }

    camstream_camera_stream_config_v1 supported{};
    initialize_configuration(supported);
    status = camstream_camera_get_stream_configuration(resources.camera, 0U, &supported);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "get_stream_configuration", status);
    }
    camstream_camera_stream_config_v1 active{};
    initialize_configuration(active);
    status = camstream_camera_configure(resources.camera, &supported, &active);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "configure", status);
    }
    print_configuration(active, 0U);

    status = camstream_camera_start(resources.camera);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "start", status);
    }

    for (std::uint32_t index = 0U; index < command_line.frame_count; ++index) {
        status = camstream_camera_wait_frame(resources.camera, kFrameWaitTimeoutMs);
        if (status != CAMSTREAM_CAMERA_STATUS_OK) {
            return report_failure(resources, "wait_frame", status);
        }

        camstream_camera_frame_v1 frame{};
        initialize_frame(frame);
        status = camstream_camera_acquire_frame(resources.camera, &frame);
        if (status != CAMSTREAM_CAMERA_STATUS_OK) {
            return report_failure(resources, "acquire_frame", status);
        }

        std::cout << "Frame[" << index << "]:\n"
                  << "  Sequence: " << frame.sequence_number << '\n'
                  << "  Timestamp ns: " << frame.monotonic_timestamp_ns << '\n'
                  << "  Resolution: " << frame.width << 'x' << frame.height << '\n'
                  << "  Pixel format: " << fourcc_string(frame.pixel_format) << '\n'
                  << "  Planes: " << frame.plane_count << '\n';
        for (std::uint32_t plane_index = 0U; plane_index < frame.plane_count; ++plane_index) {
            const camstream_camera_plane_v1& plane = frame.planes[plane_index];
            std::cout << "    Plane[" << plane_index << "] bytes used: " << plane.bytes_used
                      << ", allocation: " << plane.allocation_size << ", stride: " << plane.stride << '\n';
        }

        status = camstream_camera_release_frame(resources.camera, frame.frame_token);
        if (status != CAMSTREAM_CAMERA_STATUS_OK) {
            return report_failure(resources, "release_frame", status);
        }
    }

    status = camstream_camera_stop(resources.camera);
    if (status != CAMSTREAM_CAMERA_STATUS_OK) {
        return report_failure(resources, "stop", status);
    }
    const int cleanup_result = close_destroy_and_unload(resources);
    if (cleanup_result != 0) {
        return cleanup_result;
    }

    std::cout << "Camera HAL lifecycle: complete\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    CommandLine command_line;
    std::string error;
    if (!parse_command_line(argc, argv, command_line, error)) {
        LOGE(error);
        print_usage(std::cerr);
        return 2;
    }
    if (command_line.show_help) {
        print_usage(std::cout);
        return 0;
    }

    return run_diagnostic(command_line);
}
