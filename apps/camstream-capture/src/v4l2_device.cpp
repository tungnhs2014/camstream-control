#include "camstream/v4l2_device.hpp"

#include <camstream/logging.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <linux/videodev2.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace camstream {
namespace {

constexpr std::uint32_t kRequestedBufferCount = 4;
constexpr std::uint32_t kMaximumAcceptedBufferCount = 32;
constexpr int kFramePollTimeoutMilliseconds = 2000;
constexpr std::uint32_t kMaximumConsecutiveDequeueRetries = 16;
constexpr std::uint32_t kMaximumConsecutiveErrorFrames = 16;
static_assert(kRequestedBufferCount <= kMaximumAcceptedBufferCount);

/**
 * @brief Executes an ioctl and retries calls interrupted by a signal.
 * @param fd Open V4L2 device descriptor.
 * @param request V4L2 ioctl request.
 * @param argument Structure required by request.
 * @return The ioctl result; -1 preserves errno from the final failed attempt.
 */
int retry_ioctl(int fd, unsigned long request, void* argument) {
    int result;

    do {
        result = ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);

    return result;
}

/**
 * @brief Waits without busy-looping for capture data or priority events.
 * @param fd Open nonblocking V4L2 descriptor with streaming active.
 * @return true when POLLIN or POLLPRI is ready; false on timeout, poll error,
 * or a fatal descriptor event. EINTR restarts the finite poll wait.
 */
bool wait_for_frame_ready(int fd) {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = static_cast<short>(POLLIN | POLLPRI);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kFramePollTimeoutMilliseconds);
    int remaining_timeout = kFramePollTimeoutMilliseconds;
    int poll_result = -1;

    for (;;) {
        descriptor.revents = 0;
        poll_result = poll(&descriptor, static_cast<nfds_t>(1), remaining_timeout);
        if (poll_result != -1 || errno != EINTR) {
            break;
        }

        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
            LOGE("Frame wait timed out after " << kFramePollTimeoutMilliseconds << " ms");
            return false;
        }

        const auto remaining_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        remaining_timeout = remaining_milliseconds > 0 ? static_cast<int>(remaining_milliseconds) : 1;
    }

    if (poll_result == -1) {
        const int error = errno;
        LOGE("poll failed while waiting for a frame: " << std::strerror(error));
        return false;
    }

    if (poll_result == 0) {
        LOGE("Frame wait timed out after " << kFramePollTimeoutMilliseconds << " ms");
        return false;
    }

    const short fatal_events = static_cast<short>(POLLERR | POLLHUP | POLLNVAL);
    if ((descriptor.revents & fatal_events) != 0) {
        LOGE("poll reported fatal device events: 0x"
             << std::hex << static_cast<unsigned int>(static_cast<unsigned short>(descriptor.revents)) << std::dec);
        return false;
    }

    const short ready_events = static_cast<short>(POLLIN | POLLPRI);
    if ((descriptor.revents & ready_events) == 0) {
        LOGE("poll returned without a requested frame event: 0x"
             << std::hex << static_cast<unsigned int>(static_cast<unsigned short>(descriptor.revents)) << std::dec);
        return false;
    }

    return true;
}

/**
 * @brief Writes one validated camera payload without transforming its bytes.
 * @param output_path Destination opened in binary truncate mode.
 * @param payload Non-null userspace-owned payload.
 * @param payload_size Positive number of bytes to write exactly once.
 * @param payload_name Format name used in diagnostics.
 *
 * The caller must keep the dequeued V4L2 buffer in userspace ownership until
 * this function returns. All stream operations, including flush and close,
 * are checked so a short or incomplete output is reported as failure.
 *
 * @return true only when open, exact write, flush, and close all succeed.
 */
bool write_binary_payload(const std::string& output_path, const std::byte* payload, std::size_t payload_size,
                          const char* payload_name) {
    if (output_path.empty() || payload == nullptr || payload_size == 0) {
        LOGE("Invalid " << payload_name << " file-output request");
        return false;
    }

    if (payload_size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        LOGE(payload_name << " payload is too large for file output");
        return false;
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        LOGE("Cannot open " << payload_name << " output '" << output_path << "'");
        return false;
    }

    bool io_succeeded = true;
    output.write(reinterpret_cast<const char*>(payload), static_cast<std::streamsize>(payload_size));
    if (!output) {
        LOGE("Incomplete write to " << payload_name << " output '" << output_path << "'");
        io_succeeded = false;
        output.clear();
    }

    if (io_succeeded) {
        output.flush();
        if (!output) {
            LOGE("Cannot flush " << payload_name << " output '" << output_path << "'");
            io_succeeded = false;
            output.clear();
        }
    }

    output.close();
    const bool close_succeeded = !output.fail();
    if (!close_succeeded) {
        LOGE("Cannot close " << payload_name << " output '" << output_path << "' cleanly");
    }

    return io_succeeded && close_succeeded;
}

/**
 * @brief Validates and writes one userspace-owned MJPEG payload.
 * @param output_path Destination opened in binary truncate mode.
 * @param mapping Mapping associated with the currently dequeued buffer.
 * @param bytes_used Exact payload length reported by VIDIOC_DQBUF.
 *
 * The caller must retain userspace ownership from DQBUF until this function
 * returns. JPEG SOI/EOI markers are checked before the destination is opened,
 * and exactly bytes_used bytes are written.
 *
 * @return true only when validation, write, flush, and close all succeed.
 */
bool save_mjpeg_frame(const std::string& output_path, const MappedBuffer& mapping, std::uint32_t bytes_used) {
    const std::size_t payload_size = static_cast<std::size_t>(bytes_used);
    const std::byte* const payload = mapping.data();

    if (payload == nullptr || payload_size == 0 || payload_size > mapping.length()) {
        LOGE("MJPEG payload exceeds its mapped buffer");
        return false;
    }

    if (payload_size < 4 || payload[0] != std::byte{0xff} || payload[1] != std::byte{0xd8} ||
        payload[payload_size - 2] != std::byte{0xff} || payload[payload_size - 1] != std::byte{0xd9}) {
        LOGE("Selected MJPEG frame does not contain valid JPEG SOI/EOI markers");
        return false;
    }

    if (!write_binary_payload(output_path, payload, payload_size, "MJPEG")) {
        return false;
    }

    std::cout << "Saved MJPEG frame:\n"
              << "  Path: " << output_path << '\n'
              << "  Bytes: " << payload_size << '\n';
    return true;
}

/**
 * @brief Validates and writes one complete userspace-owned raw YUYV payload.
 * @param output_path Destination opened in binary truncate mode.
 * @param mapping Mapping associated with the currently dequeued buffer.
 * @param bytes_used Exact payload length reported by VIDIOC_DQBUF.
 * @param width Active width returned by VIDIOC_G_FMT.
 * @param height Active height returned by VIDIOC_G_FMT.
 * @param bytes_per_line Active stride returned by VIDIOC_G_FMT.
 * @param size_image Active image size returned by VIDIOC_G_FMT.
 *
 * The payload is written unchanged before QBUF returns its ownership to the
 * driver. A size mismatch is reported, but bytes_used remains authoritative:
 * the payload is neither padded to size_image nor truncated to another size.
 *
 * @return true only when active metadata and payload bounds are valid and the
 * exact bytes_used payload is written, flushed, and closed successfully.
 */
bool save_yuyv_frame(const std::string& output_path, const MappedBuffer& mapping, std::uint32_t bytes_used,
                     std::uint32_t width, std::uint32_t height, std::uint32_t bytes_per_line,
                     std::uint32_t size_image) {
    const std::size_t payload_size = static_cast<std::size_t>(bytes_used);
    const std::byte* const payload = mapping.data();

    std::cout << "YUYV frame metadata:\n"
              << "  Active width: " << width << '\n'
              << "  Active height: " << height << '\n'
              << "  Bytes per line: " << bytes_per_line << '\n'
              << "  Size image: " << size_image << '\n'
              << "  Frame bytes used: " << bytes_used << '\n';

    if (width == 0 || height == 0 || bytes_per_line == 0 || size_image == 0) {
        LOGE("Active YUYV format contains zero-sized metadata");
        return false;
    }

    if (payload == nullptr || payload_size == 0 || payload_size > mapping.length()) {
        LOGE("YUYV payload is empty or exceeds its mapped buffer");
        return false;
    }

    if (bytes_used != size_image) {
        LOGW("YUYV frame bytesused " << bytes_used << " differs from active sizeimage " << size_image
                                     << "; writing exactly bytesused without padding or truncation");
    }

    if (!write_binary_payload(output_path, payload, payload_size, "YUYV")) {
        return false;
    }

    std::cout << "Saved YUYV frame:\n"
              << "  Path: " << output_path << '\n'
              << "  Bytes: " << payload_size << '\n'
              << "  Resolution: " << width << 'x' << height << '\n'
              << "  Pixel format: YUYV\n";
    return true;
}

std::string capability_string(const __u8* value, std::size_t size) {
    const auto* text = reinterpret_cast<const char*>(value);
    return std::string(text, strnlen(text, size));
}

void print_capability_mask(const char* label, std::uint32_t value) {
    std::cout << label << ": 0x" << std::hex << std::setw(8) << std::setfill('0') << value << std::dec
              << std::setfill(' ') << '\n';
}

std::string fourcc_string(std::uint32_t pixel_format) {
    char fourcc[5] = {
        static_cast<char>(pixel_format & 0xff),
        static_cast<char>((pixel_format >> 8) & 0xff),
        static_cast<char>((pixel_format >> 16) & 0xff),
        static_cast<char>((pixel_format >> 24) & 0x7f),
        '\0',
    };

    return fourcc;
}

std::string interval_string(const v4l2_fract& interval) {
    std::ostringstream output;
    output << interval.numerator << '/' << interval.denominator << " s";

    if (interval.numerator != 0 && interval.denominator != 0) {
        const double fps = static_cast<double>(interval.denominator) / static_cast<double>(interval.numerator);
        output << " (" << std::setprecision(6) << fps << " fps)";
    }

    return output.str();
}

struct FrameMode {
    std::uint32_t pixel_format;
    std::uint32_t width;
    std::uint32_t height;
};

bool enumerate_frame_intervals(int fd, const FrameMode& mode, const char* indent) {
    bool found_interval = false;

    for (std::uint32_t index = 0;; ++index) {
        v4l2_frmivalenum interval{};
        interval.index = index;
        interval.pixel_format = mode.pixel_format;
        interval.width = mode.width;
        interval.height = mode.height;

        if (retry_ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == -1) {
            const int error = errno;
            if (error == EINVAL) {
                break;
            }

            LOGE("VIDIOC_ENUM_FRAMEINTERVALS failed for " << fourcc_string(mode.pixel_format) << ' ' << mode.width
                                                          << 'x' << mode.height << " at index " << index << ": "
                                                          << std::strerror(error));
            return false;
        }

        found_interval = true;
        std::cout << indent << "Interval[" << index << "]";

        switch (interval.type) {
        case V4L2_FRMIVAL_TYPE_DISCRETE:
            std::cout << ": " << interval_string(interval.discrete) << '\n';
            break;
        case V4L2_FRMIVAL_TYPE_STEPWISE:
            std::cout << " (stepwise): " << interval_string(interval.stepwise.min) << " to "
                      << interval_string(interval.stepwise.max) << ", step " << interval_string(interval.stepwise.step)
                      << '\n';
            break;
        case V4L2_FRMIVAL_TYPE_CONTINUOUS:
            std::cout << " (continuous): " << interval_string(interval.stepwise.min) << " to "
                      << interval_string(interval.stepwise.max) << '\n';
            break;
        default:
            LOGE("Unknown frame-interval type " << interval.type << " for " << fourcc_string(mode.pixel_format) << ' '
                                                << mode.width << 'x' << mode.height);
            return false;
        }
    }

    if (!found_interval) {
        std::cout << indent << "No frame intervals enumerated\n";
    }

    return true;
}

bool enumerate_frame_sizes(int fd, std::uint32_t pixel_format) {
    bool found_size = false;

    for (std::uint32_t index = 0;; ++index) {
        v4l2_frmsizeenum size{};
        size.index = index;
        size.pixel_format = pixel_format;

        if (retry_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == -1) {
            const int error = errno;
            if (error == EINVAL) {
                break;
            }

            LOGE("VIDIOC_ENUM_FRAMESIZES failed for " << fourcc_string(pixel_format) << " at index " << index << ": "
                                                      << std::strerror(error));
            return false;
        }

        found_size = true;

        if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            std::cout << "  Size[" << index << "]: " << size.discrete.width << 'x' << size.discrete.height << '\n';
            const FrameMode mode{pixel_format, size.discrete.width, size.discrete.height};
            if (!enumerate_frame_intervals(fd, mode, "    ")) {
                return false;
            }
            continue;
        }

        if (size.type != V4L2_FRMSIZE_TYPE_STEPWISE && size.type != V4L2_FRMSIZE_TYPE_CONTINUOUS) {
            LOGE("Unknown frame-size type " << size.type << " for " << fourcc_string(pixel_format));
            return false;
        }

        const auto& range = size.stepwise;
        const bool is_stepwise = size.type == V4L2_FRMSIZE_TYPE_STEPWISE;
        std::cout << "  Size[" << index << "] (" << (is_stepwise ? "stepwise" : "continuous")
                  << "): " << range.min_width << 'x' << range.min_height << " to " << range.max_width << 'x'
                  << range.max_height;
        if (is_stepwise) {
            std::cout << ", step " << range.step_width << 'x' << range.step_height;
        }
        std::cout << '\n';

        std::cout << "    Intervals at minimum size " << range.min_width << 'x' << range.min_height << ":\n";
        const FrameMode minimum_mode{pixel_format, range.min_width, range.min_height};
        if (!enumerate_frame_intervals(fd, minimum_mode, "      ")) {
            return false;
        }

        if (range.min_width != range.max_width || range.min_height != range.max_height) {
            std::cout << "    Intervals at maximum size " << range.max_width << 'x' << range.max_height << ":\n";
            const FrameMode maximum_mode{pixel_format, range.max_width, range.max_height};
            if (!enumerate_frame_intervals(fd, maximum_mode, "      ")) {
                return false;
            }
        }
    }

    if (!found_size) {
        std::cout << "  No frame sizes enumerated\n";
    }

    return true;
}

const char* field_name(std::uint32_t field) {
    switch (field) {
    case V4L2_FIELD_ANY:
        return "ANY";
    case V4L2_FIELD_NONE:
        return "NONE";
    case V4L2_FIELD_TOP:
        return "TOP";
    case V4L2_FIELD_BOTTOM:
        return "BOTTOM";
    case V4L2_FIELD_INTERLACED:
        return "INTERLACED";
    case V4L2_FIELD_SEQ_TB:
        return "SEQ_TB";
    case V4L2_FIELD_SEQ_BT:
        return "SEQ_BT";
    case V4L2_FIELD_ALTERNATE:
        return "ALTERNATE";
    case V4L2_FIELD_INTERLACED_TB:
        return "INTERLACED_TB";
    case V4L2_FIELD_INTERLACED_BT:
        return "INTERLACED_BT";
    default:
        return "UNKNOWN";
    }
}

v4l2_format requested_v4l2_format(const CaptureConfig& config) {
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = config.width;
    format.fmt.pix.height = config.height;
    format.fmt.pix.pixelformat = config.pixel_format;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    return format;
}

void print_format_result(const char* label, const v4l2_format& format) {
    const auto& pixel = format.fmt.pix;
    std::cout << label << ":\n"
              << "  Width: " << pixel.width << '\n'
              << "  Height: " << pixel.height << '\n'
              << "  Pixel format: " << fourcc_string(pixel.pixelformat) << '\n'
              << "  Field: " << field_name(pixel.field) << " (" << pixel.field << ")\n"
              << "  Bytes per line: " << pixel.bytesperline << '\n'
              << "  Size image: " << pixel.sizeimage << '\n';
}

void print_stream_parameters(const char* label, const v4l2_streamparm& parameters) {
    const auto& capture = parameters.parm.capture;
    const auto& time_per_frame = capture.timeperframe;

    std::cout << label << ":\n";
    print_capability_mask("  Capability mask", capture.capability);
    std::cout << "  Time per frame numerator: " << time_per_frame.numerator << '\n'
              << "  Time per frame denominator: " << time_per_frame.denominator << '\n';

    if (time_per_frame.numerator != 0 && time_per_frame.denominator != 0) {
        const double fps = static_cast<double>(time_per_frame.denominator) /
                           static_cast<double>(time_per_frame.numerator);
        std::cout << "  Calculated FPS: " << std::setprecision(6) << fps << '\n';
    } else {
        std::cout << "  Calculated FPS: unavailable\n";
    }
}

} // namespace

V4l2Device::V4l2Device(std::string requested_device_path) : device_path(std::move(requested_device_path)) {
    fd = open(device_path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd == -1) {
        const int error = errno;
        LOGE("Cannot open '" << device_path << "': " << std::strerror(error));
    }
}

V4l2Device::~V4l2Device() {
    (void)close();
}

bool V4l2Device::is_open() const noexcept {
    return fd != -1;
}

bool V4l2Device::query_and_validate_capabilities() {
    v4l2_capability capability{};

    if (retry_ioctl(fd, VIDIOC_QUERYCAP, &capability) == -1) {
        const int error = errno;
        LOGE("VIDIOC_QUERYCAP failed for '" << device_path << "': " << std::strerror(error));
        return false;
    }

    const std::uint32_t capabilities = capability.capabilities;
    const std::uint32_t device_capabilities = capability.device_caps;
    const std::uint32_t effective_capabilities = (capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? device_capabilities
                                                                                            : capabilities;

    std::cout << "Device: " << device_path << '\n'
              << "Driver: " << capability_string(capability.driver, sizeof(capability.driver)) << '\n'
              << "Card: " << capability_string(capability.card, sizeof(capability.card)) << '\n'
              << "Bus info: " << capability_string(capability.bus_info, sizeof(capability.bus_info)) << '\n'
              << "Driver version: " << ((capability.version >> 16) & 0xff) << '.' << ((capability.version >> 8) & 0xff)
              << '.' << (capability.version & 0xff) << '\n';

    print_capability_mask("Capabilities", capabilities);
    print_capability_mask("Device capabilities", device_capabilities);
    print_capability_mask("Effective capabilities", effective_capabilities);

    const bool supports_capture = (effective_capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
    const bool supports_streaming = (effective_capabilities & V4L2_CAP_STREAMING) != 0;

    if (!supports_capture) {
        if ((effective_capabilities & V4L2_CAP_META_CAPTURE) != 0) {
            LOGE("'" << device_path << "' is a metadata-only node; a video-capture node is required");
        } else {
            LOGE("'" << device_path << "' does not support V4L2 video capture");
        }
    }

    if (!supports_streaming) {
        LOGE("'" << device_path << "' does not support V4L2 streaming I/O");
    }

    return supports_capture && supports_streaming;
}

bool V4l2Device::enumerate_capture_formats() {
    bool found_format = false;

    for (std::uint32_t index = 0;; ++index) {
        v4l2_fmtdesc format{};
        format.index = index;
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (retry_ioctl(fd, VIDIOC_ENUM_FMT, &format) == -1) {
            const int error = errno;
            if (error == EINVAL) {
                break;
            }

            LOGE("VIDIOC_ENUM_FMT failed at index " << index << ": " << std::strerror(error));
            return false;
        }

        found_format = true;
        std::cout << "\nFormat[" << index << "]: " << fourcc_string(format.pixelformat) << " - "
                  << capability_string(format.description, sizeof(format.description)) << '\n';

        if (!enumerate_frame_sizes(fd, format.pixelformat)) {
            return false;
        }
    }

    if (!found_format) {
        std::cout << "\nNo capture formats enumerated\n";
    }

    return true;
}

bool V4l2Device::negotiate_frame_rate(std::uint32_t requested_fps) {
    std::cout << "Requested frame rate:\n"
              << "  Time per frame numerator: 1\n"
              << "  Time per frame denominator: " << requested_fps << '\n'
              << "  Calculated FPS: " << requested_fps << '\n';

    v4l2_streamparm initial_parameters{};
    initial_parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (retry_ioctl(fd, VIDIOC_G_PARM, &initial_parameters) == -1) {
        const int error = errno;
        LOGE("Initial VIDIOC_G_PARM failed: " << std::strerror(error));
        return false;
    }
    print_stream_parameters("Initial G_PARM", initial_parameters);

    if ((initial_parameters.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) == 0) {
        LOGE("Device does not support V4L2_CAP_TIMEPERFRAME; cannot negotiate an explicitly requested frame rate");
        return false;
    }

    v4l2_streamparm set_parameters = initial_parameters;
    set_parameters.parm.capture.timeperframe.numerator = 1;
    set_parameters.parm.capture.timeperframe.denominator = requested_fps;
    if (retry_ioctl(fd, VIDIOC_S_PARM, &set_parameters) == -1) {
        const int error = errno;
        LOGE("VIDIOC_S_PARM failed: " << std::strerror(error));
        return false;
    }
    print_stream_parameters("S_PARM result", set_parameters);

    v4l2_streamparm final_parameters{};
    final_parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (retry_ioctl(fd, VIDIOC_G_PARM, &final_parameters) == -1) {
        const int error = errno;
        LOGE("Final VIDIOC_G_PARM failed: " << std::strerror(error));
        return false;
    }
    print_stream_parameters("Final G_PARM", final_parameters);

    return true;
}

bool V4l2Device::negotiate_capture_format(const CaptureConfig& config) {
    std::cout << "\nRequested format:\n"
              << "  Width: " << config.width << '\n'
              << "  Height: " << config.height << '\n'
              << "  Pixel format: " << fourcc_string(config.pixel_format) << '\n'
              << "  Field: ANY (" << V4L2_FIELD_ANY << ")\n";

    v4l2_format tried_format = requested_v4l2_format(config);
    if (retry_ioctl(fd, VIDIOC_TRY_FMT, &tried_format) == -1) {
        const int error = errno;
        LOGE("VIDIOC_TRY_FMT failed: " << std::strerror(error));
        return false;
    }
    print_format_result("TRY_FMT result", tried_format);

    v4l2_format set_format = requested_v4l2_format(config);
    if (retry_ioctl(fd, VIDIOC_S_FMT, &set_format) == -1) {
        const int error = errno;
        LOGE("VIDIOC_S_FMT failed: " << std::strerror(error));
        return false;
    }
    print_format_result("S_FMT result", set_format);

    v4l2_format active_format{};
    active_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (retry_ioctl(fd, VIDIOC_G_FMT, &active_format) == -1) {
        const int error = errno;
        LOGE("VIDIOC_G_FMT failed: " << std::strerror(error));
        return false;
    }
    print_format_result("G_FMT active format", active_format);
    const auto& active_pixel = active_format.fmt.pix;
    active_pixel_format = active_pixel.pixelformat;
    active_width = active_pixel.width;
    active_height = active_pixel.height;
    active_bytes_per_line = active_pixel.bytesperline;
    active_size_image = active_pixel.sizeimage;

    if (config.fps != 0 && !negotiate_frame_rate(config.fps)) {
        return false;
    }

    return true;
}

bool V4l2Device::prepare_mmap_buffers() {
    if (driver_buffers_allocated || !mappings.empty() || streaming) {
        LOGE("MMAP buffers are already active");
        return false;
    }

    v4l2_requestbuffers request{};
    request.count = kRequestedBufferCount;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;

    std::cout << "\nRequested buffers: " << kRequestedBufferCount << '\n';
    if (retry_ioctl(fd, VIDIOC_REQBUFS, &request) == -1) {
        const int error = errno;
        LOGE("VIDIOC_REQBUFS failed: " << std::strerror(error));
        return false;
    }

    driver_buffers_allocated = true;
    granted_buffer_count = request.count;
    std::cout << "Granted buffers: " << request.count << "\n\n";

    if (request.type != V4L2_BUF_TYPE_VIDEO_CAPTURE || request.memory != V4L2_MEMORY_MMAP) {
        LOGE("Driver returned incompatible buffer type or memory model");
        return false;
    }

    if (request.count == 0) {
        LOGE("Driver granted zero MMAP buffers");
        return false;
    }

    if (request.count > kMaximumAcceptedBufferCount) {
        LOGE("Driver granted " << request.count << " buffers, exceeding the project sanity limit of "
                               << kMaximumAcceptedBufferCount);
        return false;
    }

    if (static_cast<std::uintmax_t>(request.count) > static_cast<std::uintmax_t>(mappings.max_size())) {
        LOGE("Granted buffer count exceeds container capacity");
        return false;
    }

    try {
        mappings.reserve(request.count);
    } catch (const std::exception& error) {
        LOGE("Cannot reserve mapped-buffer storage: " << error.what());
        return false;
    }

    for (std::uint32_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;

        if (retry_ioctl(fd, VIDIOC_QUERYBUF, &buffer) == -1) {
            const int error = errno;
            LOGE("VIDIOC_QUERYBUF failed for buffer " << index << ": " << std::strerror(error));
            return false;
        }

        if (buffer.index != index || buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE || buffer.memory != V4L2_MEMORY_MMAP) {
            LOGE("Buffer " << index << " returned inconsistent QUERYBUF metadata");
            return false;
        }

        if (buffer.length == 0) {
            LOGE("Buffer " << index << " has zero length");
            return false;
        }

        if (static_cast<std::uintmax_t>(buffer.m.offset) >
            static_cast<std::uintmax_t>(std::numeric_limits<off_t>::max())) {
            LOGE("Buffer " << index << " offset cannot be represented by mmap()");
            return false;
        }

        void* const address = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                                   static_cast<off_t>(buffer.m.offset));
        if (address == MAP_FAILED) {
            const int error = errno;
            LOGE("mmap failed for buffer " << index << ": " << std::strerror(error));
            return false;
        }

        mappings.emplace_back(index, address, buffer.length);

        std::cout << "Buffer[" << index << "]:\n"
                  << "  Length: " << buffer.length << '\n'
                  << "  Offset: " << buffer.m.offset << '\n'
                  << "  Mapped: yes\n";
    }

    return true;
}

bool V4l2Device::queue_all_buffers() {
    if (!driver_buffers_allocated || mappings.size() != static_cast<std::size_t>(granted_buffer_count)) {
        LOGE("Cannot queue an incomplete MMAP buffer set");
        return false;
    }

    std::uint32_t queued_count = 0;

    for (std::size_t position = 0; position < mappings.size(); ++position) {
        const std::uint32_t index = mappings[position].index();
        if (static_cast<std::size_t>(index) != position || index >= granted_buffer_count) {
            LOGE("Invalid mapped-buffer index " << index << " at position " << position);
            return false;
        }

        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;

        if (retry_ioctl(fd, VIDIOC_QBUF, &buffer) == -1) {
            const int error = errno;
            LOGE("VIDIOC_QBUF failed for buffer " << index << ": errno " << error << " (" << std::strerror(error)
                                                  << ')');
            return false;
        }

        if (buffer.index != index || buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE || buffer.memory != V4L2_MEMORY_MMAP) {
            LOGE("Buffer " << index << " returned inconsistent QBUF metadata");
            return false;
        }

        ++queued_count;
        std::cout << "Queued buffer[" << index << "]\n";
    }

    std::cout << "Queued buffers: " << queued_count << '\n';
    buffers_queued = queued_count == granted_buffer_count;
    return buffers_queued;
}

bool V4l2Device::start_streaming() {
    if (streaming) {
        LOGE("Streaming is already active");
        return false;
    }

    if (!buffers_queued) {
        LOGE("Cannot start streaming before all buffers are queued");
        return false;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (retry_ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        const int error = errno;
        LOGE("VIDIOC_STREAMON failed: " << std::strerror(error));
        return false;
    }

    streaming = true;
    std::cout << "Streaming: started\n";
    return true;
}

bool V4l2Device::capture_frames(std::uint32_t frame_count, std::uint32_t skip_frames, const std::string& output_path) {
    if (!streaming) {
        LOGE("Cannot capture frames while streaming is inactive");
        return false;
    }

    if (frame_count == 0) {
        LOGE("Frame count must be positive");
        return false;
    }

    if (!output_path.empty() && active_pixel_format != V4L2_PIX_FMT_MJPEG && active_pixel_format != V4L2_PIX_FMT_YUYV) {
        LOGE("File output requires the active format to be MJPG or YUYV");
        return false;
    }

    std::uint64_t dequeued_frames = 0;
    std::uint32_t skipped_valid_frames = 0;
    std::uint32_t captured_valid_frames = 0;
    std::uint64_t error_frames = 0;
    std::uint32_t consecutive_dequeue_retries = 0;
    std::uint32_t consecutive_error_frames = 0;

    while (captured_valid_frames < frame_count) {
        if (!wait_for_frame_ready(fd)) {
            return false;
        }

        v4l2_buffer dequeued_buffer{};
        dequeued_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dequeued_buffer.memory = V4L2_MEMORY_MMAP;

        if (retry_ioctl(fd, VIDIOC_DQBUF, &dequeued_buffer) == -1) {
            const int error = errno;
            if (error == EAGAIN) {
                ++consecutive_dequeue_retries;
                if (consecutive_dequeue_retries > kMaximumConsecutiveDequeueRetries) {
                    LOGE("VIDIOC_DQBUF repeatedly returned EAGAIN after poll readiness");
                    return false;
                }
                continue;
            }

            LOGE("VIDIOC_DQBUF failed: errno " << error << " (" << std::strerror(error) << ')');
            return false;
        }

        ++dequeued_frames;
        consecutive_dequeue_retries = 0;
        buffers_queued = false;

        if (dequeued_buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE || dequeued_buffer.memory != V4L2_MEMORY_MMAP) {
            LOGE("Dequeued buffer returned incompatible type or memory model");
            return false;
        }

        if (static_cast<std::size_t>(dequeued_buffer.index) >= mappings.size()) {
            LOGE("Dequeued buffer index " << dequeued_buffer.index << " exceeds mapped buffer count "
                                          << mappings.size());
            return false;
        }

        const MappedBuffer& mapping = mappings[dequeued_buffer.index];
        if (mapping.index() != dequeued_buffer.index) {
            LOGE("Dequeued buffer index " << dequeued_buffer.index
                                          << " does not match mapped-buffer ownership metadata");
            return false;
        }

        const std::size_t mapped_length = mapping.length();
        if (mapped_length == 0 || static_cast<std::size_t>(dequeued_buffer.bytesused) > mapped_length) {
            LOGE("Dequeued buffer " << dequeued_buffer.index << " reports " << dequeued_buffer.bytesused
                                    << " bytes used for a mapping of " << mapped_length << " bytes");
            return false;
        }

        if (dequeued_buffer.length == 0 || static_cast<std::size_t>(dequeued_buffer.length) > mapped_length ||
            dequeued_buffer.bytesused > dequeued_buffer.length) {
            LOGE("Dequeued buffer " << dequeued_buffer.index << " returned inconsistent length metadata");
            return false;
        }

        if (dequeued_buffer.timestamp.tv_usec < 0 || dequeued_buffer.timestamp.tv_usec >= 1000000) {
            LOGE("Dequeued buffer " << dequeued_buffer.index << " returned an invalid timestamp");
            return false;
        }

        const std::uint32_t buffer_index = dequeued_buffer.index;
        const bool has_buffer_error = (dequeued_buffer.flags & V4L2_BUF_FLAG_ERROR) != 0U;
        const bool is_skipped_valid_frame = !has_buffer_error && skipped_valid_frames < skip_frames;

        if (has_buffer_error) {
            ++error_frames;
            ++consecutive_error_frames;
            LOGW("Dequeued frame[" << dequeued_frames << "] has V4L2_BUF_FLAG_ERROR: buffer=" << buffer_index
                                   << ", sequence=" << dequeued_buffer.sequence
                                   << ", bytesused=" << dequeued_buffer.bytesused << ", flags=0x" << std::hex
                                   << dequeued_buffer.flags << std::dec);
        } else {
            const std::uint32_t frame_number = is_skipped_valid_frame ? skipped_valid_frames + 1
                                                                      : captured_valid_frames + 1;
            std::cout << (is_skipped_valid_frame ? "Skipped valid frame[" : "Captured valid frame[") << frame_number
                      << "]:\n"
                      << "  Buffer index: " << buffer_index << '\n'
                      << "  Bytes used: " << dequeued_buffer.bytesused << '\n'
                      << "  Sequence: " << dequeued_buffer.sequence << '\n'
                      << "  Timestamp: " << dequeued_buffer.timestamp.tv_sec << '.' << std::setfill('0') << std::setw(6)
                      << dequeued_buffer.timestamp.tv_usec << std::setfill(' ') << '\n'
                      << "  Flags: 0x" << std::hex << std::setw(8) << std::setfill('0') << dequeued_buffer.flags
                      << std::dec << std::setfill(' ') << '\n';
        }

        bool frame_output_succeeded = true;
        const bool is_final_valid_frame = !has_buffer_error && !is_skipped_valid_frame &&
                                          (captured_valid_frames + 1 == frame_count);
        if (is_final_valid_frame && !output_path.empty()) {
            if (active_pixel_format == V4L2_PIX_FMT_MJPEG) {
                frame_output_succeeded = save_mjpeg_frame(output_path, mapping, dequeued_buffer.bytesused);
            } else {
                frame_output_succeeded = save_yuyv_frame(output_path, mapping, dequeued_buffer.bytesused, active_width,
                                                         active_height, active_bytes_per_line, active_size_image);
            }
        }

        v4l2_buffer requeue_buffer{};
        requeue_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        requeue_buffer.memory = V4L2_MEMORY_MMAP;
        requeue_buffer.index = buffer_index;

        if (retry_ioctl(fd, VIDIOC_QBUF, &requeue_buffer) == -1) {
            const int error = errno;
            LOGE("VIDIOC_QBUF failed while requeueing buffer " << buffer_index << ": errno " << error << " ("
                                                               << std::strerror(error) << ')');
            return false;
        }

        buffers_queued = true;
        if (requeue_buffer.index != buffer_index || requeue_buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
            requeue_buffer.memory != V4L2_MEMORY_MMAP) {
            LOGE("Buffer " << buffer_index << " returned inconsistent re-QBUF metadata");
            return false;
        }

        if (!frame_output_succeeded) {
            return false;
        }

        if (has_buffer_error) {
            if (consecutive_error_frames >= kMaximumConsecutiveErrorFrames) {
                LOGE("Reached the project limit of "
                     << kMaximumConsecutiveErrorFrames
                     << " consecutive V4L2 buffer errors before capturing the requested valid frames; dequeued="
                     << dequeued_frames << ", skipped=" << skipped_valid_frames
                     << ", captured=" << captured_valid_frames << ", errors=" << error_frames);
                return false;
            }
            continue;
        }

        consecutive_error_frames = 0;
        if (is_skipped_valid_frame) {
            ++skipped_valid_frames;
        } else {
            ++captured_valid_frames;
        }
    }

    std::cout << "Dequeued frames: " << dequeued_frames << '\n'
              << "Skipped valid frames: " << skipped_valid_frames << '\n'
              << "Captured valid frames: " << captured_valid_frames << '\n'
              << "Error frames: " << error_frames << '\n';
    return true;
}

bool V4l2Device::stop_streaming() {
    if (!streaming) {
        LOGE("Cannot stop an inactive stream");
        return false;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (retry_ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        const int error = errno;
        LOGE("VIDIOC_STREAMOFF failed: " << std::strerror(error));
        return false;
    }

    streaming = false;
    buffers_queued = false;
    std::cout << "Streaming: stopped\n";
    return true;
}

bool V4l2Device::release_driver_buffers() {
    v4l2_requestbuffers release_request{};
    release_request.count = 0;
    release_request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    release_request.memory = V4L2_MEMORY_MMAP;

    if (retry_ioctl(fd, VIDIOC_REQBUFS, &release_request) == -1) {
        const int error = errno;
        LOGE("VIDIOC_REQBUFS buffer release failed: " << std::strerror(error));
        return false;
    }

    if (release_request.count != 0) {
        LOGE("Driver reported " << release_request.count << " buffers after release request");
        return false;
    }

    driver_buffers_allocated = false;
    granted_buffer_count = 0;
    buffers_queued = false;
    return true;
}

bool V4l2Device::release_buffers() {
    const bool had_resources = streaming || driver_buffers_allocated || !mappings.empty();
    if (!had_resources) {
        return true;
    }

    bool cleanup_succeeded = true;
    bool can_release_driver_buffers = fd != -1;
    bool all_mappings_unmapped = true;

    if (streaming) {
        LOGE("Stream is still active after STREAMOFF failure; closing device before unmapping buffers");
        if (!close_descriptor()) {
            cleanup_succeeded = false;
        }
        streaming = false;
        driver_buffers_allocated = false;
        granted_buffer_count = 0;
        buffers_queued = false;
        can_release_driver_buffers = false;
    }

    for (auto& mapping : mappings) {
        const std::uint32_t index = mapping.index();
        const std::size_t length = mapping.length();
        if (!mapping.unmap()) {
            const int error = errno;
            LOGE("munmap failed for buffer " << index << " (length " << length << "): " << std::strerror(error));
            cleanup_succeeded = false;
            all_mappings_unmapped = false;
        }
    }
    mappings.clear();

    if (can_release_driver_buffers && driver_buffers_allocated) {
        if (!all_mappings_unmapped) {
            LOGE("Skipping explicit driver-buffer release because a mapping could not be unmapped");
            if (!close_descriptor()) {
                cleanup_succeeded = false;
            }
            driver_buffers_allocated = false;
            granted_buffer_count = 0;
            buffers_queued = false;
        } else if (!release_driver_buffers()) {
            cleanup_succeeded = false;
        }
    }

    if (cleanup_succeeded) {
        std::cout << "MMAP cleanup: success\n";
    }

    return cleanup_succeeded;
}

bool V4l2Device::close_descriptor() {
    if (fd == -1) {
        return true;
    }

    const int device_fd = std::exchange(fd, -1);
    if (::close(device_fd) == -1) {
        const int error = errno;
        LOGE("Cannot close '" << device_path << "': " << std::strerror(error));
        return false;
    }

    return true;
}

bool V4l2Device::close() {
    bool close_succeeded = true;

    if (streaming || driver_buffers_allocated || !mappings.empty()) {
        if (!release_buffers()) {
            close_succeeded = false;
        }
    }

    if (!close_descriptor()) {
        close_succeeded = false;
    }

    return close_succeeded;
}

} // namespace camstream
