#include "camstream/capture_config.hpp"
#include "camstream/v4l2_device.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <linux/videodev2.h>
#include <string>

namespace {

constexpr const char* kDefaultDevice = "/dev/video0";
constexpr std::uint32_t kDefaultWidth = 640;
constexpr std::uint32_t kDefaultHeight = 480;
constexpr std::uint32_t kDefaultSkipFrameCount = 0;
constexpr std::uint32_t kDefaultFrameCount = 10;

void print_usage(const char* program)
{
    std::cout << "Usage: " << program
              << " [--device <path>] [--format <MJPG|YUYV>]\n"
              << "       [--width <pixels>] [--height <pixels>] [--fps <rate>]\n"
              << "       [--skip <frames>] [--count <frames>] [--output <path>]\n"
              << "       [--help]\n"
              << "\n"
              << "Query and validate a V4L2 video-capture device.\n"
              << "\n"
              << "Options:\n"
              << "  --device <path>  V4L2 device node (default: "
              << kDefaultDevice << ")\n"
              << "  --format <name>  Negotiate MJPG or YUYV capture format\n"
              << "  --width <pixels> Requested width with --format (default: "
              << kDefaultWidth << ")\n"
              << "  --height <pixels> Requested height with --format (default: "
              << kDefaultHeight << ")\n"
              << "  --fps <rate>      Requested positive integer frame rate\n"
              << "  --skip <frames>   Valid frames to skip with --format "
              << "(default: " << kDefaultSkipFrameCount << ")\n"
              << "  --count <frames>  Frames to validate with --format "
              << "(default: " << kDefaultFrameCount << ")\n"
              << "  --output <path>   Save the final valid MJPG or YUYV frame\n"
              << "  --help           Show this help text\n";
}

bool parse_positive_integer(const std::string& text, std::uint32_t& value)
{
    if (text.empty()) {
        return false;
    }

    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc {} && result.ptr == end && value != 0;
}

bool parse_non_negative_integer(const std::string& text,
                                std::uint32_t& value)
{
    if (text.empty()) {
        return false;
    }

    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc {} && result.ptr == end;
}

int run_capture(const camstream::CaptureConfig& config, bool has_format)
{
    camstream::V4l2Device device(config.device);
    bool operation_succeeded = device.is_open();

    if (operation_succeeded) {
        operation_succeeded = device.query_and_validate_capabilities();
    }

    if (operation_succeeded && !has_format) {
        operation_succeeded = device.enumerate_capture_formats();
    }

    bool buffer_setup_attempted = false;
    if (operation_succeeded && has_format) {
        bool stream_started = false;
        operation_succeeded = device.negotiate_capture_format(config);
        if (operation_succeeded) {
            buffer_setup_attempted = true;
            operation_succeeded = device.prepare_mmap_buffers();
        }
        if (operation_succeeded) {
            operation_succeeded = device.queue_all_buffers();
        }
        if (operation_succeeded) {
            stream_started = device.start_streaming();
            operation_succeeded = stream_started;
        }
        if (operation_succeeded) {
            operation_succeeded = device.capture_frames(config.frame_count,
                                                        config.skip_frames,
                                                        config.output_path);
        }
        if (stream_started) {
            const bool stop_succeeded = device.stop_streaming();
            operation_succeeded = operation_succeeded && stop_succeeded;
        }
    }

    if (buffer_setup_attempted) {
        const bool cleanup_succeeded = device.release_buffers();
        operation_succeeded = operation_succeeded && cleanup_succeeded;
    }

    const bool close_succeeded = device.close();
    return operation_succeeded && close_succeeded ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    camstream::CaptureConfig config {
        kDefaultDevice,
        0,
        kDefaultWidth,
        kDefaultHeight,
        0,
        kDefaultSkipFrameCount,
        kDefaultFrameCount,
        {},
    };
    bool has_format = false;
    bool has_width = false;
    bool has_height = false;
    bool has_fps = false;
    bool has_skip = false;
    bool has_count = false;
    bool has_output = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--help") {
            print_usage(argv[0]);
            return 0;
        }

        if (argument == "--device") {
            if (index + 1 >= argc) {
                std::cerr << "Error: --device requires a path\n";
                print_usage(argv[0]);
                return 2;
            }

            config.device = argv[++index];
            if (config.device.empty()) {
                std::cerr << "Error: device path must not be empty\n";
                return 2;
            }
            continue;
        }

        if (argument == "--format") {
            if (index + 1 >= argc) {
                std::cerr << "Error: --format requires MJPG or YUYV\n";
                return 2;
            }

            const std::string format = argv[++index];
            if (format == "MJPG") {
                config.pixel_format = V4L2_PIX_FMT_MJPEG;
            } else if (format == "YUYV") {
                config.pixel_format = V4L2_PIX_FMT_YUYV;
            } else {
                std::cerr << "Error: unsupported format '" << format
                          << "'; expected MJPG or YUYV\n";
                return 2;
            }
            has_format = true;
            continue;
        }

        if (argument == "--width" || argument == "--height") {
            if (index + 1 >= argc) {
                std::cerr << "Error: " << argument
                          << " requires a positive pixel count\n";
                return 2;
            }

            const std::string value = argv[++index];
            std::uint32_t parsed_value = 0;
            if (!parse_positive_integer(value, parsed_value)) {
                std::cerr << "Error: invalid " << argument << " value '"
                          << value << "'; expected a positive integer\n";
                return 2;
            }

            if (argument == "--width") {
                config.width = parsed_value;
                has_width = true;
            } else {
                config.height = parsed_value;
                has_height = true;
            }
            continue;
        }

        if (argument == "--fps") {
            if (index + 1 >= argc) {
                std::cerr << "Error: --fps requires a positive integer\n";
                return 2;
            }

            const std::string value = argv[++index];
            std::uint32_t parsed_value = 0;
            if (!parse_positive_integer(value, parsed_value)) {
                std::cerr << "Error: invalid --fps value '" << value
                          << "'; expected a positive integer\n";
                return 2;
            }

            config.fps = parsed_value;
            has_fps = true;
            continue;
        }

        if (argument == "--count") {
            if (index + 1 >= argc) {
                std::cerr << "Error: --count requires a positive integer\n";
                return 2;
            }

            const std::string value = argv[++index];
            std::uint32_t parsed_value = 0;
            if (!parse_positive_integer(value, parsed_value)) {
                std::cerr << "Error: invalid --count value '" << value
                          << "'; expected a positive integer\n";
                return 2;
            }

            config.frame_count = parsed_value;
            has_count = true;
            continue;
        }

        if (argument == "--skip") {
            if (index + 1 >= argc) {
                std::cerr << "Error: --skip requires a non-negative integer\n";
                return 2;
            }

            const std::string value = argv[++index];
            std::uint32_t parsed_value = 0;
            if (!parse_non_negative_integer(value, parsed_value)) {
                std::cerr << "Error: invalid --skip value '" << value
                          << "'; expected a non-negative integer\n";
                return 2;
            }

            config.skip_frames = parsed_value;
            has_skip = true;
            continue;
        }

        if (argument == "--output") {
            if (index + 1 >= argc) {
                std::cerr << "Error: --output requires a path\n";
                return 2;
            }

            config.output_path = argv[++index];
            if (config.output_path.empty()) {
                std::cerr << "Error: output path must not be empty\n";
                return 2;
            }
            has_output = true;
            continue;
        }

        std::cerr << "Error: unknown option '" << argument << "'\n";
        print_usage(argv[0]);
        return 2;
    }

    if (!has_format && has_fps) {
        std::cerr << "Error: --fps requires --format\n";
        return 2;
    }

    if (!has_format && (has_width || has_height)) {
        std::cerr << "Error: --width and --height require --format\n";
        return 2;
    }

    if (!has_format && has_count) {
        std::cerr << "Error: --count requires --format\n";
        return 2;
    }

    if (!has_format && has_skip) {
        std::cerr << "Error: --skip requires --format\n";
        return 2;
    }

    if (!has_format && has_output) {
        std::cerr << "Error: --output requires --format\n";
        return 2;
    }

    return run_capture(config, has_format);
}
