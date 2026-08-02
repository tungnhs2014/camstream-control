#include "camstream/gstreamer_pipeline.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr int kArgumentErrorExitCode = 2;
constexpr int kRuntimeErrorExitCode = 1;

struct ParsedArguments {
    camstream::GstreamerPipelineConfig config;
    bool show_help = false;
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " --device /dev/videoN"
              << " --format yuy2|mjpeg --width N --height N --fps N --buffers N"
              << " [--sync true|false]\n\n"
              << "Run one finite native-GStreamer V4L2 diagnostic pipeline.\n\n"
              << "Required options:\n"
              << "  --device <path>       Caller-selected V4L2 capture node\n"
              << "  --format yuy2|mjpeg   Raw YUY2 or MJPEG-decode pipeline\n"
              << "  --width <pixels>      Positive capture width\n"
              << "  --height <pixels>     Positive capture height\n"
              << "  --fps <rate>          Positive integral frame rate\n"
              << "  --buffers <count>     Positive finite source-buffer count\n\n"
              << "Optional:\n"
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

bool parse_positive_gint(const std::string& text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }

    std::uint32_t parsed = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    const auto maximum_gint = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (result.ec != std::errc{} || result.ptr != end || parsed == 0U || parsed > maximum_gint) {
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
            parsed.show_help = true;
            return true;
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
        }

        if (seen != nullptr && destination != nullptr) {
            if (!mark_once(*seen, option) || !consume_value(argc, argv, index, option, value)) {
                return false;
            }
            if (!parse_positive_gint(value, *destination)) {
                std::cerr << "Error: invalid " << option << " value '" << value
                          << "'; expected a positive non-overflowing integer\n";
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

        std::cerr << "Error: unknown option '" << option << "'\n";
        return false;
    }

    struct RequiredOption {
        const char* name;
        bool present;
    };
    const RequiredOption required_options[] = {
        {"--device", has_device},
        {"--format", has_format},
        {"--width", has_width},
        {"--height", has_height},
        {"--fps", has_fps},
        {"--buffers", has_buffers},
    };

    for (const RequiredOption& required : required_options) {
        if (!required.present) {
            std::cerr << "Error: missing required option " << required.name << '\n';
            return false;
        }
    }

    return true;
}

const char* format_name(camstream::GstreamerInputFormat format) noexcept {
    return format == camstream::GstreamerInputFormat::Yuy2 ? "yuy2" : "mjpeg";
}

void print_startup_summary(const camstream::GstreamerPipelineConfig& config) {
    std::cout << "CamStream GStreamer diagnostic configuration:\n"
              << "  Device: " << config.device_path << '\n'
              << "  Format: " << format_name(config.input_format) << '\n'
              << "  Resolution: " << config.width << 'x' << config.height << '\n'
              << "  FPS: " << config.fps << '\n'
              << "  Buffers: " << config.buffer_count << '\n'
              << "  Fakesink sync: " << (config.sink_sync ? "true" : "false") << '\n';
}

int run(const ParsedArguments& parsed) {
    print_startup_summary(parsed.config);

    camstream::GstreamerPipeline pipeline(parsed.config);
    if (!pipeline.build()) {
        return kRuntimeErrorExitCode;
    }
    if (!pipeline.start()) {
        (void)pipeline.stop();
        return kRuntimeErrorExitCode;
    }

    const bool wait_succeeded = pipeline.wait();
    const bool stop_succeeded = pipeline.stop();
    return wait_succeeded && stop_succeeded ? 0 : kRuntimeErrorExitCode;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        ParsedArguments parsed;
        if (!parse_arguments(argc, argv, parsed)) {
            print_usage(argv[0]);
            return kArgumentErrorExitCode;
        }
        if (parsed.show_help) {
            print_usage(argv[0]);
            return 0;
        }
        return run(parsed);
    } catch (const std::exception& exception) {
        std::cerr << "Error: unexpected C++ failure: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "Error: unexpected non-standard C++ failure\n";
    }

    return kRuntimeErrorExitCode;
}
