#include "camstream/gstreamer_pipeline.hpp"

#include <camstream/logging.hpp>

#include <limits>
#include <memory>
#include <utility>

namespace camstream {
namespace {

constexpr GstClockTime kStateChangeTimeout = 5 * GST_SECOND;
constexpr GstClockTime kTerminalStallAllowance = 120 * GST_SECOND;

bool validate_property(GObject* object, const char* property_name, GType expected_type) {
    GParamSpec* specification = g_object_class_find_property(G_OBJECT_GET_CLASS(object), property_name);
    if (specification == nullptr) {
        LOGE("Required GStreamer property '" << property_name << "' is unavailable");
        return false;
    }

    const bool readable = (specification->flags & G_PARAM_READABLE) != 0U;
    const bool writable = (specification->flags & G_PARAM_WRITABLE) != 0U;
    if (!readable || !writable || G_PARAM_SPEC_VALUE_TYPE(specification) != expected_type) {
        LOGE("GStreamer property '" << property_name << "' has an incompatible contract");
        return false;
    }

    return true;
}

const char* format_name(GstreamerInputFormat format) noexcept {
    switch (format) {
    case GstreamerInputFormat::Yuy2:
        return "yuy2";
    case GstreamerInputFormat::Mjpeg:
        return "mjpeg";
    }

    return "invalid";
}

} // namespace

GstreamerPipeline::GstreamerPipeline(GstreamerPipelineConfig pipeline_config) : config(std::move(pipeline_config)) {}

GstreamerPipeline::~GstreamerPipeline() noexcept {
    if (!stop()) {
        LOGE("GStreamer cleanup failed during destruction");
    }
}

bool GstreamerPipeline::validate_config() const {
    constexpr auto maximum_gint = static_cast<std::uint32_t>(std::numeric_limits<gint>::max());

    if (config.device_path.empty()) {
        LOGE("V4L2 device path must not be empty");
        return false;
    }

    if (config.width == 0U || config.height == 0U || config.fps == 0U || config.width > maximum_gint ||
        config.height > maximum_gint || config.fps > maximum_gint || config.buffer_count > maximum_gint) {
        LOGE("Width, height, and fps must fit a positive GStreamer integer property; buffers must fit a "
             "non-negative GStreamer integer property");
        return false;
    }

    switch (config.input_format) {
    case GstreamerInputFormat::Yuy2:
    case GstreamerInputFormat::Mjpeg:
        return true;
    }

    LOGE("Unsupported GStreamer input format");
    return false;
}

GstElement* GstreamerPipeline::create_and_add_element(const char* factory_name, const char* element_name) {
    GstElement* element = gst_element_factory_make(factory_name, element_name);
    if (element == nullptr) {
        LOGE("GStreamer element factory '" << factory_name << "' is unavailable");
        return nullptr;
    }

    if (gst_bin_add(GST_BIN(pipeline), element) == FALSE) {
        LOGE("Failed to add element '" << element_name << "' to the pipeline");
        gst_object_unref(element);
        return nullptr;
    }

    // GstBin owns the transferred reference; this pointer is now borrowed.
    return element;
}

bool GstreamerPipeline::configure_source(GstElement* source) const {
    if (!validate_property(G_OBJECT(source), "device", G_TYPE_STRING)) {
        return false;
    }

    const bool finite_run = config.buffer_count > 0U;
    if (finite_run && !validate_property(G_OBJECT(source), "num-buffers", G_TYPE_INT)) {
        return false;
    }

    if (finite_run) {
        const auto requested_buffers = static_cast<gint>(config.buffer_count);
        g_object_set(G_OBJECT(source), "device", config.device_path.c_str(), "num-buffers", requested_buffers, nullptr);
    } else {
        // Preserve the source's unlimited default instead of imposing a count.
        g_object_set(G_OBJECT(source), "device", config.device_path.c_str(), nullptr);
    }

    gchar* active_device = nullptr;
    g_object_get(G_OBJECT(source), "device", &active_device, nullptr);

    bool matches = active_device != nullptr && config.device_path == active_device;
    if (matches && finite_run) {
        gint active_buffers = 0;
        g_object_get(G_OBJECT(source), "num-buffers", &active_buffers, nullptr);
        matches = active_buffers == static_cast<gint>(config.buffer_count);
    }
    if (!matches) {
        LOGE("v4l2src properties were not applied as requested");
    }
    g_free(active_device);
    return matches;
}

bool GstreamerPipeline::configure_sink(GstElement* sink) const {
    if (!validate_property(G_OBJECT(sink), "sync", G_TYPE_BOOLEAN)) {
        return false;
    }

    const gboolean requested_sync = config.sink_sync ? TRUE : FALSE;
    g_object_set(G_OBJECT(sink), "sync", requested_sync, nullptr);

    gboolean active_sync = FALSE;
    g_object_get(G_OBJECT(sink), "sync", &active_sync, nullptr);
    if (active_sync != requested_sync) {
        LOGE("fakesink sync property was not applied");
        return false;
    }

    return true;
}

bool GstreamerPipeline::configure_capsfilter(GstElement* capsfilter) const {
    if (!validate_property(G_OBJECT(capsfilter), "caps", GST_TYPE_CAPS)) {
        return false;
    }

    const auto width = static_cast<gint>(config.width);
    const auto height = static_cast<gint>(config.height);
    const auto fps = static_cast<gint>(config.fps);

    GstCaps* caps = nullptr;
    if (config.input_format == GstreamerInputFormat::Yuy2) {
        caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "YUY2", "width", G_TYPE_INT, width, "height",
                                   G_TYPE_INT, height, "framerate", GST_TYPE_FRACTION, fps, 1, nullptr);
    } else {
        caps = gst_caps_new_simple("image/jpeg", "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, "framerate",
                                   GST_TYPE_FRACTION, fps, 1, nullptr);
    }

    if (caps == nullptr || gst_caps_is_empty(caps) != FALSE) {
        LOGE("Failed to construct capture caps");
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        return false;
    }

    g_object_set(G_OBJECT(capsfilter), "caps", caps, nullptr);
    GstCaps* active_caps = nullptr;
    g_object_get(G_OBJECT(capsfilter), "caps", &active_caps, nullptr);

    const bool matches = active_caps != nullptr && gst_caps_is_equal(caps, active_caps) != FALSE;
    if (!matches) {
        LOGE("capsfilter did not retain the requested caps");
    }

    if (active_caps != nullptr) {
        gst_caps_unref(active_caps);
    }
    gst_caps_unref(caps);
    return matches;
}

bool GstreamerPipeline::build() {
    if (built) {
        return true;
    }

    if (pipeline != nullptr || bus != nullptr) {
        if (!stop()) {
            return false;
        }
    }

    if (!validate_config()) {
        return false;
    }

    GError* initialization_error = nullptr;
    if (gst_init_check(nullptr, nullptr, &initialization_error) == FALSE) {
        if (initialization_error != nullptr) {
            LOGE("Failed to initialize GStreamer: " << initialization_error->message);
            g_error_free(initialization_error);
        } else {
            LOGE("Failed to initialize GStreamer");
        }
        return false;
    }

    pipeline = gst_pipeline_new("camstream-pipeline");
    if (pipeline == nullptr) {
        LOGE("Failed to create GstPipeline");
        return false;
    }

    GstElement* source = create_and_add_element("v4l2src", "camera-source");
    GstElement* capsfilter = create_and_add_element("capsfilter", "capture-caps");
    GstElement* decoder = nullptr;
    GstElement* converter = nullptr;
    if (config.input_format == GstreamerInputFormat::Mjpeg) {
        decoder = create_and_add_element("jpegdec", "jpeg-decoder");
        converter = create_and_add_element("videoconvert", "video-converter");
    }
    GstElement* sink = create_and_add_element("fakesink", "diagnostic-sink");

    if (source == nullptr || capsfilter == nullptr || sink == nullptr ||
        (config.input_format == GstreamerInputFormat::Mjpeg && (decoder == nullptr || converter == nullptr))) {
        (void)stop();
        return false;
    }

    if (!configure_source(source) || !configure_capsfilter(capsfilter) || !configure_sink(sink)) {
        (void)stop();
        return false;
    }

    gboolean linked = FALSE;
    if (config.input_format == GstreamerInputFormat::Yuy2) {
        linked = gst_element_link_many(source, capsfilter, sink, nullptr);
    } else {
        linked = gst_element_link_many(source, capsfilter, decoder, converter, sink, nullptr);
    }
    if (linked == FALSE) {
        LOGE("Failed to link the " << format_name(config.input_format) << " pipeline");
        (void)stop();
        return false;
    }

    bus = gst_element_get_bus(pipeline);
    if (bus == nullptr) {
        LOGE("Failed to acquire the pipeline bus");
        (void)stop();
        return false;
    }

    built = true;
    run_finished = false;
    return true;
}

bool GstreamerPipeline::start() {
    if (started) {
        return true;
    }
    if (run_finished) {
        LOGE("A completed pipeline run must be stopped and rebuilt before restart");
        return false;
    }
    if (!built || pipeline == nullptr || bus == nullptr) {
        LOGE("Pipeline must be built before start()");
        return false;
    }

    const GstStateChangeReturn request_result = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (request_result == GST_STATE_CHANGE_FAILURE) {
        LOGE("Failed to request GST_STATE_PLAYING");
        return false;
    }

    GstState current_state = GST_STATE_VOID_PENDING;
    GstState pending_state = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn completion_result = gst_element_get_state(pipeline, &current_state, &pending_state,
                                                                         kStateChangeTimeout);
    if (completion_result == GST_STATE_CHANGE_FAILURE || completion_result == GST_STATE_CHANGE_ASYNC ||
        current_state != GST_STATE_PLAYING) {
        LOGE("Pipeline did not reach GST_STATE_PLAYING; current="
             << gst_element_state_get_name(current_state) << ", pending=" << gst_element_state_get_name(pending_state));
        return false;
    }

    started = true;
    LOGI("Pipeline state: PLAYING");
    return true;
}

bool GstreamerPipeline::wait() {
    if (!started || pipeline == nullptr || bus == nullptr) {
        LOGE("Pipeline must be started before wait()");
        return false;
    }
    if (config.buffer_count == 0U) {
        LOGE("wait() requires a positive finite buffer count; use bus polling for continuous mode");
        return false;
    }

    constexpr auto message_types = static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR |
                                                               GST_MESSAGE_WARNING | GST_MESSAGE_STATE_CHANGED);
    const auto buffer_count = static_cast<guint64>(config.buffer_count);
    const auto fps = static_cast<guint64>(config.fps);
    const GstClockTime nominal_duration = ((buffer_count + fps - 1U) / fps) * GST_SECOND;
    const GstClockTime wait_timeout = nominal_duration + kTerminalStallAllowance;
    const GstClockTime wait_started = gst_util_get_timestamp();
    const GstClockTime latest_valid_time = GST_CLOCK_TIME_NONE - 1U;
    const GstClockTime deadline = wait_started <= latest_valid_time - wait_timeout ? wait_started + wait_timeout
                                                                                   : latest_valid_time;

    for (;;) {
        const GstClockTime current_time = gst_util_get_timestamp();
        if (current_time >= deadline) {
            LOGE("Pipeline exceeded its nominal duration plus the 120-second stall allowance");
            started = false;
            run_finished = true;
            return false;
        }

        GstMessage* message = gst_bus_timed_pop_filtered(bus, deadline - current_time, message_types);
        if (message == nullptr) {
            LOGE("Pipeline exceeded its nominal duration plus the 120-second stall allowance");
            started = false;
            run_finished = true;
            return false;
        }

        std::unique_ptr<GstMessage, decltype(&gst_message_unref)> owned_message(message, gst_message_unref);

        const GstreamerBusOutcome outcome = process_bus_message(message);
        if (outcome == GstreamerBusOutcome::EndOfStream) {
            return true;
        }
        if (outcome == GstreamerBusOutcome::Error) {
            return false;
        }
    }
}

int GstreamerPipeline::bus_poll_fd() const noexcept {
    if (!built || bus == nullptr) {
        LOGE("Pipeline must be built before requesting its bus poll descriptor");
        return -1;
    }

    GPollFD poll_descriptor{};
    gst_bus_get_pollfd(bus, &poll_descriptor);
    if (poll_descriptor.fd < 0) {
        LOGE("GstBus returned an invalid poll descriptor");
        return -1;
    }
    return poll_descriptor.fd;
}

GstreamerBusOutcome GstreamerPipeline::drain_bus_messages() noexcept {
    if (!built || bus == nullptr) {
        LOGE("Pipeline must be built before draining its bus");
        return GstreamerBusOutcome::Error;
    }

    GstreamerBusOutcome aggregate = GstreamerBusOutcome::Continue;
    while (GstMessage* message = gst_bus_pop(bus)) {
        std::unique_ptr<GstMessage, decltype(&gst_message_unref)> owned_message(message, gst_message_unref);
        const GstreamerBusOutcome outcome = process_bus_message(message);
        if (outcome == GstreamerBusOutcome::Error) {
            aggregate = GstreamerBusOutcome::Error;
        } else if (outcome == GstreamerBusOutcome::EndOfStream && aggregate == GstreamerBusOutcome::Continue) {
            aggregate = GstreamerBusOutcome::EndOfStream;
        }
    }
    return aggregate;
}

GstreamerBusOutcome GstreamerPipeline::process_bus_message(GstMessage* message) noexcept {
    if (message == nullptr) {
        LOGE("Cannot process a null GstBus message");
        started = false;
        run_finished = true;
        return GstreamerBusOutcome::Error;
    }

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
        LOGI("Pipeline: EOS");
        started = false;
        run_finished = true;
        return GstreamerBusOutcome::EndOfStream;

    case GST_MESSAGE_ERROR: {
        GError* error = nullptr;
        gchar* debug_details = nullptr;
        gst_message_parse_error(message, &error, &debug_details);

        const GstObject* source = GST_MESSAGE_SRC(message);
        LOGE("Pipeline error source: " << (source != nullptr ? GST_OBJECT_NAME(source) : "unknown"));
        LOGE("Pipeline error message: " << (error != nullptr ? error->message : "unavailable"));
        LOGD("Pipeline error debug: " << (debug_details != nullptr ? debug_details : "unavailable"));

        if (error != nullptr) {
            g_error_free(error);
        }
        g_free(debug_details);
        started = false;
        run_finished = true;
        return GstreamerBusOutcome::Error;
    }

    case GST_MESSAGE_WARNING: {
        GError* warning = nullptr;
        gchar* debug_details = nullptr;
        gst_message_parse_warning(message, &warning, &debug_details);

        const GstObject* source = GST_MESSAGE_SRC(message);
        LOGW("Pipeline warning source: " << (source != nullptr ? GST_OBJECT_NAME(source) : "unknown"));
        LOGW("Pipeline warning message: " << (warning != nullptr ? warning->message : "unavailable"));
        LOGD("Pipeline warning debug: " << (debug_details != nullptr ? debug_details : "unavailable"));

        if (warning != nullptr) {
            g_error_free(warning);
        }
        g_free(debug_details);
        return GstreamerBusOutcome::Continue;
    }

    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline)) {
            GstState old_state = GST_STATE_VOID_PENDING;
            GstState new_state = GST_STATE_VOID_PENDING;
            GstState pending_state = GST_STATE_VOID_PENDING;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
            if (old_state != new_state) {
                if (pending_state != GST_STATE_VOID_PENDING) {
                    LOGI("Pipeline state changed: " << gst_element_state_get_name(old_state) << " -> "
                                                    << gst_element_state_get_name(new_state) << " (pending "
                                                    << gst_element_state_get_name(pending_state) << ')');
                } else {
                    LOGI("Pipeline state changed: " << gst_element_state_get_name(old_state) << " -> "
                                                    << gst_element_state_get_name(new_state));
                }
            }
        }
        return GstreamerBusOutcome::Continue;

    default:
        return GstreamerBusOutcome::Continue;
    }
}

bool GstreamerPipeline::stop() noexcept {
    bool succeeded = true;

    if (pipeline != nullptr) {
        const GstStateChangeReturn request_result = gst_element_set_state(pipeline, GST_STATE_NULL);
        if (request_result == GST_STATE_CHANGE_FAILURE) {
            LOGE("Failed to request GST_STATE_NULL");
            succeeded = false;
        } else {
            GstState current_state = GST_STATE_VOID_PENDING;
            GstState pending_state = GST_STATE_VOID_PENDING;
            const GstStateChangeReturn completion_result = gst_element_get_state(pipeline, &current_state,
                                                                                 &pending_state, kStateChangeTimeout);
            if (completion_result == GST_STATE_CHANGE_FAILURE || completion_result == GST_STATE_CHANGE_ASYNC ||
                current_state != GST_STATE_NULL) {
                LOGE("Pipeline did not reach GST_STATE_NULL");
                succeeded = false;
            }
        }
    }

    started = false;
    built = false;
    run_finished = false;

    if (bus != nullptr) {
        gst_object_unref(bus);
        bus = nullptr;
    }
    if (pipeline != nullptr) {
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }

    return succeeded;
}

} // namespace camstream
