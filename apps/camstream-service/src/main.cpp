#include "camstream/camera_service.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr int kRuntimeErrorExitCode = 1;
constexpr int kArgumentErrorExitCode = 2;

struct ParsedArguments {
    camstream::GstreamerPipelineConfig config{
        "/dev/video0",
        camstream::GstreamerInputFormat::Yuy2,
        640U,
        480U,
        30U,
        0U,
        false,
    };
};

void print_usage(std::ostream& output, const char* program) {
    output << "Usage: " << program << " [options]\n\n"
           << "Run the CamStream camera service in the foreground.\n\n"
           << "Options:\n"
           << "  --device <path>       V4L2 capture node (default: /dev/video0)\n"
           << "  --format yuy2|mjpeg   Pipeline format (default: yuy2)\n"
           << "  --width <pixels>      Positive capture width (default: 640)\n"
           << "  --height <pixels>     Positive capture height (default: 480)\n"
           << "  --fps <rate>          Positive integral frame rate (default: 30)\n"
           << "  --buffers <count>     0 for continuous mode; positive for finite"
              " mode (default: 0)\n"
           << "  --sync true|false     fakesink clock sync (default: false)\n"
           << "  --help                Show this help text\n";
}

bool consume_value(int argc, char* argv[], int& index, const std::string& option, std::string& value) {
    if (index + 1 >= argc || std::string_view(argv[index + 1]).rfind("--", 0) == 0U) {
        std::cerr << "Error: " << option << " requires a value\n";
        return false;
    }

    value = argv[++index];
    if (value.empty()) {
        std::cerr << "Error: " << option << " value must not be empty\n";
        return false;
    }
    return true;
}

bool parse_gint(const std::string& text, bool allow_zero, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }

    std::uint32_t parsed = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    const auto maximum_gint = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (result.ec != std::errc{} || result.ptr != end || (!allow_zero && parsed == 0U) || parsed > maximum_gint) {
        return false;
    }

    value = parsed;
    return true;
}

bool mark_once(bool& seen, const std::string& option) {
    if (seen) {
        std::cerr << "Error: duplicate option " << option << '\n';
        return false;
    }
    seen = true;
    return true;
}

bool parse_arguments(int argc, char* argv[], ParsedArguments& parsed) {
    bool has_device = false;
    bool has_format = false;
    bool has_width = false;
    bool has_height = false;
    bool has_fps = false;
    bool has_buffers = false;
    bool has_sync = false;

    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help") {
            std::cerr << "Error: --help must be used alone\n";
            return false;
        }

        std::string value;
        if (option == "--device") {
            if (!mark_once(has_device, option) || !consume_value(argc, argv, index, option, value)) {
                return false;
            }
            parsed.config.device_path = std::move(value);
            continue;
        }

        if (option == "--format") {
            if (!mark_once(has_format, option) || !consume_value(argc, argv, index, option, value)) {
                return false;
            }
            if (value == "yuy2") {
                parsed.config.input_format = camstream::GstreamerInputFormat::Yuy2;
            } else if (value == "mjpeg") {
                parsed.config.input_format = camstream::GstreamerInputFormat::Mjpeg;
            } else {
                std::cerr << "Error: invalid --format value '" << value << "'; expected yuy2 or mjpeg\n";
                return false;
            }
            continue;
        }

        bool* seen = nullptr;
        std::uint32_t* destination = nullptr;
        bool allow_zero = false;
        if (option == "--width") {
            seen = &has_width;
            destination = &parsed.config.width;
        } else if (option == "--height") {
            seen = &has_height;
            destination = &parsed.config.height;
        } else if (option == "--fps") {
            seen = &has_fps;
            destination = &parsed.config.fps;
        } else if (option == "--buffers") {
            seen = &has_buffers;
            destination = &parsed.config.buffer_count;
            allow_zero = true;
        }

        if (seen != nullptr && destination != nullptr) {
            if (!mark_once(*seen, option) || !consume_value(argc, argv, index, option, value)) {
                return false;
            }
            if (!parse_gint(value, allow_zero, *destination)) {
                std::cerr << "Error: invalid " << option << " value '" << value << "'; expected a "
                          << (allow_zero ? "non-negative" : "positive") << " non-overflowing integer\n";
                return false;
            }
            continue;
        }

        if (option == "--sync") {
            if (!mark_once(has_sync, option) || !consume_value(argc, argv, index, option, value)) {
                return false;
            }
            if (value == "true") {
                parsed.config.sink_sync = true;
            } else if (value == "false") {
                parsed.config.sink_sync = false;
            } else {
                std::cerr << "Error: invalid --sync value '" << value << "'; expected true or false\n";
                return false;
            }
            continue;
        }

        if (std::string_view(option).rfind("--", 0) == 0U) {
            std::cerr << "Error: unknown option '" << option << "'\n";
        } else {
            std::cerr << "Error: unexpected positional argument '" << option << "'\n";
        }
        return false;
    }

    return true;
}

const char* format_name(camstream::GstreamerInputFormat format) noexcept {
    return format == camstream::GstreamerInputFormat::Yuy2 ? "yuy2" : "mjpeg";
}

void print_startup_summary(const camstream::GstreamerPipelineConfig& config) {
    std::cout << "CamStream service configuration:\n"
              << "  Device: " << config.device_path << '\n'
              << "  Format: " << format_name(config.input_format) << '\n'
              << "  Resolution: " << config.width << 'x' << config.height << '\n'
              << "  FPS: " << config.fps << '\n'
              << "  Buffers: " << config.buffer_count << " (" << (config.buffer_count == 0U ? "continuous" : "finite")
              << ")\n"
              << "  Fakesink sync: " << (config.sink_sync ? "true" : "false") << '\n';
}

int run(ParsedArguments parsed) {
    print_startup_summary(parsed.config);
    std::cout << "Camera service starting" << std::endl;

    camstream::CameraService service(std::move(parsed.config));
    if (!service.initialize()) {
        static_cast<void>(service.shutdown());
        return kRuntimeErrorExitCode;
    }

    const bool run_succeeded = service.run();
    const bool shutdown_succeeded = service.shutdown();
    return run_succeeded && shutdown_succeeded ? 0 : kRuntimeErrorExitCode;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            print_usage(std::cout, argv[0]);
            return 0;
        }

        ParsedArguments parsed;
        if (!parse_arguments(argc, argv, parsed)) {
            print_usage(std::cerr, argv[0]);
            return kArgumentErrorExitCode;
        }
        return run(std::move(parsed));
    } catch (const std::exception& exception) {
        std::cerr << "Error: unexpected C++ failure: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "Error: unexpected non-standard C++ failure\n";
    }

    return kRuntimeErrorExitCode;
}
