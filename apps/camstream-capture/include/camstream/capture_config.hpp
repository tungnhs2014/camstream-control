#ifndef CAMSTREAM_CAPTURE_CONFIG_HPP
#define CAMSTREAM_CAPTURE_CONFIG_HPP

#include <cstdint>
#include <string>

namespace camstream {

/**
 * @brief User-requested device and optional capture-mode configuration.
 *
 * A zero pixel format selects capability-enumeration mode. A nonzero pixel
 * format enables negotiation and the MMAP frame-validation lifecycle. The CLI
 * guarantees positive dimensions and frame_count whenever capture is enabled.
 * skip_frames may be zero and counts valid frames discarded before capture;
 * an empty output_path disables frame-file output.
 */
struct CaptureConfig {
    std::string device;
    std::uint32_t pixel_format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 0;
    std::uint32_t skip_frames = 0;
    std::uint32_t frame_count = 0;
    std::string output_path;
};

} // namespace camstream

#endif
