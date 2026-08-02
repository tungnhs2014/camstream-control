#include <camstream/camera/camera_session.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::uint32_t kDefaultFrameCount = 3U;
constexpr std::uint32_t kFrameWaitTimeoutMs = 1000U;
constexpr char kSimulatedSourceIdentifier[] = "simulated0";

struct CommandLine {
    std::string backend_path;
    std::uint32_t frame_count = kDefaultFrameCount;
    bool show_help = false;
};

void print_usage(std::ostream& output) {
    output << "Usage: camstream-camera-test --backend <path> "
              "[--frames <positive-count>]\n"
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
    bool frames_seen = false;

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

        error = "unknown option or positional argument: " + std::string(argument);
        return false;
    }

    if (!backend_seen) {
        error = "--backend is required";
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

void print_configuration(const camstream::camera::CameraStreamConfig& configuration) {
    std::cout << "Stream configuration:\n"
              << "  Source: " << configuration.source_identifier << '\n'
              << "  Resolution: " << configuration.width << 'x' << configuration.height << '\n'
              << "  Pixel format: " << fourcc_string(configuration.pixel_format) << '\n'
              << "  Frame rate: " << configuration.frame_rate_numerator << '/' << configuration.frame_rate_denominator
              << '\n';
}

int run_diagnostic(const CommandLine& command_line) {
    camstream::camera::CameraSession session = camstream::camera::CameraSession::load(command_line.backend_path);

    std::cout << "Backend:\n"
              << "  Name: " << session.backend_name() << '\n'
              << "  ABI version: " << session.backend_abi_version() << '\n';

    session.open(kSimulatedSourceIdentifier);
    const camstream::camera::CameraCapabilities capabilities = session.capabilities();
    std::cout << "Capabilities:\n"
              << "  Stream configurations: " << capabilities.stream_config_count << '\n'
              << "  Maximum planes: " << capabilities.maximum_plane_count << '\n';

    const camstream::camera::CameraStreamConfig supported = session.stream_configuration(0U);
    const camstream::camera::CameraStreamConfig active = session.configure(supported);
    print_configuration(active);
    session.start();

    for (std::uint32_t index = 0U; index < command_line.frame_count; ++index) {
        if (!session.wait_frame(kFrameWaitTimeoutMs)) {
            throw camstream::camera::CameraError("Timed out waiting for simulated frame",
                                                 CAMSTREAM_CAMERA_STATUS_TIMEOUT);
        }

        camstream::camera::CameraFrame frame = session.acquire_frame();
        std::cout << "Frame[" << index << "]:\n"
                  << "  Sequence: " << frame.sequence_number() << '\n'
                  << "  Timestamp ns: " << frame.monotonic_timestamp_ns() << '\n'
                  << "  Resolution: " << frame.width() << 'x' << frame.height() << '\n'
                  << "  Pixel format: " << fourcc_string(frame.pixel_format()) << '\n'
                  << "  Planes: " << frame.plane_count() << '\n';
        for (std::uint32_t plane_index = 0U; plane_index < frame.plane_count(); ++plane_index) {
            const camstream::camera::CameraPlane& plane = frame.plane(plane_index);
            std::cout << "    Plane[" << plane_index << "] bytes used: " << plane.bytes_used
                      << ", allocation: " << plane.allocation_size << ", stride: " << plane.stride << '\n';
        }
        session.release_frame(frame);
    }

    session.stop();
    session.close();
    std::cout << "Camera PPI lifecycle: complete\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    CommandLine command_line;
    std::string error;
    if (!parse_command_line(argc, argv, command_line, error)) {
        std::cerr << "Error: " << error << '\n';
        print_usage(std::cerr);
        return 2;
    }
    if (command_line.show_help) {
        print_usage(std::cout);
        return 0;
    }

    try {
        return run_diagnostic(command_line);
    } catch (const camstream::camera::CameraError& exception) {
        std::cerr << "Camera error: " << exception.what() << '\n';
    } catch (const std::exception& exception) {
        std::cerr << "Runtime error: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "Runtime error: unknown exception\n";
    }
    return 1;
}
