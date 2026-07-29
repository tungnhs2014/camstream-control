#ifndef CAMSTREAM_GSTREAMER_PIPELINE_HPP
#define CAMSTREAM_GSTREAMER_PIPELINE_HPP

#include <gst/gst.h>

#include <cstdint>
#include <string>

namespace camstream {

/**
 * @brief Selects the input caps and element topology for a camera pipeline.
 */
enum class GstreamerInputFormat {
    Yuy2,
    Mjpeg,
};

/**
 * @brief Complete immutable input contract for one finite pipeline run.
 *
 * The caller must provide a nonempty V4L2 device path and positive numeric
 * values representable by GStreamer integer properties. The configuration is
 * copied into GstreamerPipeline and remains unchanged for its lifetime.
 */
struct GstreamerPipelineConfig {
    /** @brief Caller-selected V4L2 capture node; no discovery is performed. */
    std::string device_path;
    /** @brief Raw YUY2 or MJPEG-decode pipeline topology. */
    GstreamerInputFormat input_format = GstreamerInputFormat::Yuy2;
    /** @brief Requested capture width in pixels. */
    std::uint32_t width = 0;
    /** @brief Requested capture height in pixels. */
    std::uint32_t height = 0;
    /** @brief Requested integral frame rate. */
    std::uint32_t fps = 0;
    /** @brief Finite number of source buffers to process before EOS. */
    std::uint32_t buffer_count = 0;
    /** @brief Whether fakesink synchronizes buffer delivery to the clock. */
    bool sink_sync = false;
};

/**
 * @brief Owns one finite native-GStreamer camera pipeline and its bus.
 *
 * The class is not thread-safe; one caller must serialize build(), start(),
 * wait(), and stop(). Child elements are owned by GstPipeline after successful
 * bin insertion. This object owns the pipeline and bus references, transitions
 * the pipeline to GST_STATE_NULL before releasing them, and supports repeated
 * stop calls. Destruction performs the same idempotent cleanup fallback.
 */
class GstreamerPipeline final {
public:
    /**
     * @brief Stores the pipeline configuration without creating resources.
     * @param config Validated or untrusted configuration checked by build().
     */
    explicit GstreamerPipeline(GstreamerPipelineConfig config);

    /**
     * @brief Stops and releases any resources still owned by this object.
     *
     * Cleanup is idempotent and does not throw. A cleanup failure is reported
     * to standard error but cannot be returned from the destructor.
     */
    ~GstreamerPipeline() noexcept;

    GstreamerPipeline(const GstreamerPipeline&) = delete;
    GstreamerPipeline& operator=(const GstreamerPipeline&) = delete;
    GstreamerPipeline(GstreamerPipeline&&) = delete;
    GstreamerPipeline& operator=(GstreamerPipeline&&) = delete;

    /**
     * @brief Constructs, configures, adds, and links all required elements.
     * @return true when the complete pipeline and owned bus are ready.
     *
     * Calling build() again after success is harmless. Any partial failure is
     * cleaned before false is returned.
     */
    bool build();

    /**
     * @brief Requests and validates the transition to GST_STATE_PLAYING.
     * @return true when the pipeline reaches PLAYING; false otherwise.
     *
     * Requires a successful build(). Repeated calls before wait() consumes a
     * terminal message are idempotent; a completed run must be stopped and
     * rebuilt before another run.
     */
    bool start();

    /**
     * @brief Waits for successful EOS or a terminal pipeline error.
     * @return true only when EOS is received; false on misuse or ERROR.
     *
     * Requires a successful start(). The wait allows the nominal finite-run
     * duration plus a bounded stall allowance, so a stalled source returns
     * failure and can be stopped cleanly. Owned bus messages and parsed error
     * data are released exactly once.
     */
    bool wait();

    /**
     * @brief Transitions to NULL and releases the owned bus and pipeline.
     * @return true when no resources are owned or the NULL transition succeeds.
     *
     * This operation is deterministic, idempotent, and safe after partial
     * construction or a runtime failure. It is not safe to race with wait().
     */
    bool stop() noexcept;

private:
    bool validate_config() const;
    GstElement* create_and_add_element(const char* factory_name,
                                       const char* element_name);
    bool configure_source(GstElement* source) const;
    bool configure_sink(GstElement* sink) const;
    bool configure_capsfilter(GstElement* capsfilter) const;

    GstreamerPipelineConfig config_;
    GstElement* pipeline_ = nullptr;
    GstBus* bus_ = nullptr;
    bool built_ = false;
    bool started_ = false;
    bool run_finished_ = false;
};

} // namespace camstream

#endif
