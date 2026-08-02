#include "camstream/gstreamer_pipeline.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace camstream {
namespace {

constexpr GstClockTime kStateChangeTimeout = 5 * GST_SECOND;
constexpr GstClockTime kTerminalStallAllowance = 120 * GST_SECOND;

bool validate_property(GObject* object, const char* property_name,
                       GType expected_type)
{
    GParamSpec* specification =
        g_object_class_find_property(G_OBJECT_GET_CLASS(object), property_name);
    if (specification == nullptr) {
        std::cerr << "Error: required GStreamer property '" << property_name
                  << "' is unavailable\n";
        return false;
    }

    const bool readable = (specification->flags & G_PARAM_READABLE) != 0U;
    const bool writable = (specification->flags & G_PARAM_WRITABLE) != 0U;
    if (!readable || !writable
        || G_PARAM_SPEC_VALUE_TYPE(specification) != expected_type) {
        std::cerr << "Error: GStreamer property '" << property_name
                  << "' has an incompatible contract\n";
        return false;
    }

    return true;
}

const char* format_name(GstreamerInputFormat format) noexcept
{
    switch (format) {
    case GstreamerInputFormat::Yuy2:
        return "yuy2";
    case GstreamerInputFormat::Mjpeg:
        return "mjpeg";
    }

    return "invalid";
}

} // namespace

GstreamerPipeline::GstreamerPipeline(GstreamerPipelineConfig config)
    : config_(std::move(config))
{
}

GstreamerPipeline::~GstreamerPipeline() noexcept
{
    if (!stop()) {
        std::cerr << "Error: GStreamer cleanup failed during destruction\n";
    }
}

bool GstreamerPipeline::validate_config() const
{
    constexpr auto maximum_gint =
        static_cast<std::uint32_t>(std::numeric_limits<gint>::max());

    if (config_.device_path.empty()) {
        std::cerr << "Error: V4L2 device path must not be empty\n";
        return false;
    }

    if (config_.width == 0U || config_.height == 0U || config_.fps == 0U
        || config_.width > maximum_gint || config_.height > maximum_gint
        || config_.fps > maximum_gint
        || config_.buffer_count > maximum_gint) {
        std::cerr << "Error: width, height, and fps must fit a positive "
                     "GStreamer integer property; buffers must fit a "
                     "non-negative GStreamer integer property\n";
        return false;
    }

    switch (config_.input_format) {
    case GstreamerInputFormat::Yuy2:
    case GstreamerInputFormat::Mjpeg:
        return true;
    }

    std::cerr << "Error: unsupported GStreamer input format\n";
    return false;
}

GstElement* GstreamerPipeline::create_and_add_element(
    const char* factory_name, const char* element_name)
{
    GstElement* element = gst_element_factory_make(factory_name, element_name);
    if (element == nullptr) {
        std::cerr << "Error: GStreamer element factory '" << factory_name
                  << "' is unavailable\n";
        return nullptr;
    }

    if (gst_bin_add(GST_BIN(pipeline_), element) == FALSE) {
        std::cerr << "Error: failed to add element '" << element_name
                  << "' to the pipeline\n";
        gst_object_unref(element);
        return nullptr;
    }

    // GstBin owns the transferred reference; this pointer is now borrowed.
    return element;
}

bool GstreamerPipeline::configure_source(GstElement* source) const
{
    if (!validate_property(G_OBJECT(source), "device", G_TYPE_STRING)) {
        return false;
    }

    const bool finite_run = config_.buffer_count > 0U;
    if (finite_run
        && !validate_property(G_OBJECT(source), "num-buffers", G_TYPE_INT)) {
        return false;
    }

    if (finite_run) {
        const auto requested_buffers = static_cast<gint>(config_.buffer_count);
        g_object_set(G_OBJECT(source), "device", config_.device_path.c_str(),
                     "num-buffers", requested_buffers, nullptr);
    } else {
        // Preserve the source's unlimited default instead of imposing a count.
        g_object_set(G_OBJECT(source), "device", config_.device_path.c_str(),
                     nullptr);
    }

    gchar* active_device = nullptr;
    g_object_get(G_OBJECT(source), "device", &active_device, nullptr);

    bool matches =
        active_device != nullptr && config_.device_path == active_device;
    if (matches && finite_run) {
        gint active_buffers = 0;
        g_object_get(G_OBJECT(source), "num-buffers", &active_buffers, nullptr);
        matches = active_buffers == static_cast<gint>(config_.buffer_count);
    }
    if (!matches) {
        std::cerr << "Error: v4l2src properties were not applied as requested\n";
    }
    g_free(active_device);
    return matches;
}

bool GstreamerPipeline::configure_sink(GstElement* sink) const
{
    if (!validate_property(G_OBJECT(sink), "sync", G_TYPE_BOOLEAN)) {
        return false;
    }

    const gboolean requested_sync = config_.sink_sync ? TRUE : FALSE;
    g_object_set(G_OBJECT(sink), "sync", requested_sync, nullptr);

    gboolean active_sync = FALSE;
    g_object_get(G_OBJECT(sink), "sync", &active_sync, nullptr);
    if (active_sync != requested_sync) {
        std::cerr << "Error: fakesink sync property was not applied\n";
        return false;
    }

    return true;
}

bool GstreamerPipeline::configure_capsfilter(GstElement* capsfilter) const
{
    if (!validate_property(G_OBJECT(capsfilter), "caps", GST_TYPE_CAPS)) {
        return false;
    }

    const auto width = static_cast<gint>(config_.width);
    const auto height = static_cast<gint>(config_.height);
    const auto fps = static_cast<gint>(config_.fps);

    GstCaps* caps = nullptr;
    if (config_.input_format == GstreamerInputFormat::Yuy2) {
        caps = gst_caps_new_simple(
            "video/x-raw", "format", G_TYPE_STRING, "YUY2", "width",
            G_TYPE_INT, width, "height", G_TYPE_INT, height, "framerate",
            GST_TYPE_FRACTION, fps, 1, nullptr);
    } else {
        caps = gst_caps_new_simple(
            "image/jpeg", "width", G_TYPE_INT, width, "height", G_TYPE_INT,
            height, "framerate", GST_TYPE_FRACTION, fps, 1, nullptr);
    }

    if (caps == nullptr || gst_caps_is_empty(caps) != FALSE) {
        std::cerr << "Error: failed to construct capture caps\n";
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        return false;
    }

    g_object_set(G_OBJECT(capsfilter), "caps", caps, nullptr);
    GstCaps* active_caps = nullptr;
    g_object_get(G_OBJECT(capsfilter), "caps", &active_caps, nullptr);

    const bool matches =
        active_caps != nullptr && gst_caps_is_equal(caps, active_caps) != FALSE;
    if (!matches) {
        std::cerr << "Error: capsfilter did not retain the requested caps\n";
    }

    if (active_caps != nullptr) {
        gst_caps_unref(active_caps);
    }
    gst_caps_unref(caps);
    return matches;
}

bool GstreamerPipeline::build()
{
    if (built_) {
        return true;
    }

    if (pipeline_ != nullptr || bus_ != nullptr) {
        if (!stop()) {
            return false;
        }
    }

    if (!validate_config()) {
        return false;
    }

    GError* initialization_error = nullptr;
    if (gst_init_check(nullptr, nullptr, &initialization_error) == FALSE) {
        std::cerr << "Error: failed to initialize GStreamer";
        if (initialization_error != nullptr) {
            std::cerr << ": " << initialization_error->message;
            g_error_free(initialization_error);
        }
        std::cerr << '\n';
        return false;
    }

    pipeline_ = gst_pipeline_new("camstream-pipeline");
    if (pipeline_ == nullptr) {
        std::cerr << "Error: failed to create GstPipeline\n";
        return false;
    }

    GstElement* source = create_and_add_element("v4l2src", "camera-source");
    GstElement* capsfilter =
        create_and_add_element("capsfilter", "capture-caps");
    GstElement* decoder = nullptr;
    GstElement* converter = nullptr;
    if (config_.input_format == GstreamerInputFormat::Mjpeg) {
        decoder = create_and_add_element("jpegdec", "jpeg-decoder");
        converter = create_and_add_element("videoconvert", "video-converter");
    }
    GstElement* sink = create_and_add_element("fakesink", "diagnostic-sink");

    if (source == nullptr || capsfilter == nullptr || sink == nullptr
        || (config_.input_format == GstreamerInputFormat::Mjpeg
            && (decoder == nullptr || converter == nullptr))) {
        (void)stop();
        return false;
    }

    if (!configure_source(source) || !configure_capsfilter(capsfilter)
        || !configure_sink(sink)) {
        (void)stop();
        return false;
    }

    gboolean linked = FALSE;
    if (config_.input_format == GstreamerInputFormat::Yuy2) {
        linked = gst_element_link_many(source, capsfilter, sink, nullptr);
    } else {
        linked = gst_element_link_many(source, capsfilter, decoder, converter,
                                       sink, nullptr);
    }
    if (linked == FALSE) {
        std::cerr << "Error: failed to link the "
                  << format_name(config_.input_format) << " pipeline\n";
        (void)stop();
        return false;
    }

    bus_ = gst_element_get_bus(pipeline_);
    if (bus_ == nullptr) {
        std::cerr << "Error: failed to acquire the pipeline bus\n";
        (void)stop();
        return false;
    }

    built_ = true;
    run_finished_ = false;
    return true;
}

bool GstreamerPipeline::start()
{
    if (started_) {
        return true;
    }
    if (run_finished_) {
        std::cerr << "Error: a completed pipeline run must be stopped and "
                     "rebuilt before restart\n";
        return false;
    }
    if (!built_ || pipeline_ == nullptr || bus_ == nullptr) {
        std::cerr << "Error: pipeline must be built before start()\n";
        return false;
    }

    const GstStateChangeReturn request_result =
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (request_result == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Error: failed to request GST_STATE_PLAYING\n";
        return false;
    }

    GstState current_state = GST_STATE_VOID_PENDING;
    GstState pending_state = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn completion_result = gst_element_get_state(
        pipeline_, &current_state, &pending_state, kStateChangeTimeout);
    if (completion_result == GST_STATE_CHANGE_FAILURE
        || completion_result == GST_STATE_CHANGE_ASYNC
        || current_state != GST_STATE_PLAYING) {
        std::cerr << "Error: pipeline did not reach GST_STATE_PLAYING; current="
                  << gst_element_state_get_name(current_state)
                  << ", pending=" << gst_element_state_get_name(pending_state)
                  << '\n';
        return false;
    }

    started_ = true;
    std::cout << "Pipeline state: PLAYING\n";
    return true;
}

bool GstreamerPipeline::wait()
{
    if (!started_ || pipeline_ == nullptr || bus_ == nullptr) {
        std::cerr << "Error: pipeline must be started before wait()\n";
        return false;
    }
    if (config_.buffer_count == 0U) {
        std::cerr << "Error: wait() requires a positive finite buffer count; "
                     "use bus polling for continuous mode\n";
        return false;
    }

    constexpr auto message_types = static_cast<GstMessageType>(
        GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_WARNING
        | GST_MESSAGE_STATE_CHANGED);
    const auto buffer_count = static_cast<guint64>(config_.buffer_count);
    const auto fps = static_cast<guint64>(config_.fps);
    const GstClockTime nominal_duration =
        ((buffer_count + fps - 1U) / fps) * GST_SECOND;
    const GstClockTime wait_timeout =
        nominal_duration + kTerminalStallAllowance;
    const GstClockTime wait_started = gst_util_get_timestamp();
    const GstClockTime latest_valid_time = GST_CLOCK_TIME_NONE - 1U;
    const GstClockTime deadline = wait_started <= latest_valid_time - wait_timeout
        ? wait_started + wait_timeout
        : latest_valid_time;

    for (;;) {
        const GstClockTime current_time = gst_util_get_timestamp();
        if (current_time >= deadline) {
            std::cerr << "Error: pipeline exceeded its nominal duration plus "
                         "the 120-second stall allowance\n";
            started_ = false;
            run_finished_ = true;
            return false;
        }

        GstMessage* message = gst_bus_timed_pop_filtered(
            bus_, deadline - current_time, message_types);
        if (message == nullptr) {
            std::cerr << "Error: pipeline exceeded its nominal duration plus "
                         "the 120-second stall allowance\n";
            started_ = false;
            run_finished_ = true;
            return false;
        }

        std::unique_ptr<GstMessage, decltype(&gst_message_unref)> owned_message(
            message, gst_message_unref);

        const GstreamerBusOutcome outcome = process_bus_message(message);
        if (outcome == GstreamerBusOutcome::EndOfStream) {
            return true;
        }
        if (outcome == GstreamerBusOutcome::Error) {
            return false;
        }
    }
}

int GstreamerPipeline::bus_poll_fd() const noexcept
{
    if (!built_ || bus_ == nullptr) {
        std::cerr << "Error: pipeline must be built before requesting its bus "
                     "poll descriptor\n";
        return -1;
    }

    GPollFD poll_descriptor {};
    gst_bus_get_pollfd(bus_, &poll_descriptor);
    if (poll_descriptor.fd < 0) {
        std::cerr << "Error: GstBus returned an invalid poll descriptor\n";
        return -1;
    }
    return poll_descriptor.fd;
}

GstreamerBusOutcome GstreamerPipeline::drain_bus_messages() noexcept
{
    if (!built_ || bus_ == nullptr) {
        std::cerr << "Error: pipeline must be built before draining its bus\n";
        return GstreamerBusOutcome::Error;
    }

    GstreamerBusOutcome aggregate = GstreamerBusOutcome::Continue;
    while (GstMessage* message = gst_bus_pop(bus_)) {
        std::unique_ptr<GstMessage, decltype(&gst_message_unref)> owned_message(
            message, gst_message_unref);
        const GstreamerBusOutcome outcome = process_bus_message(message);
        if (outcome == GstreamerBusOutcome::Error) {
            aggregate = GstreamerBusOutcome::Error;
        } else if (outcome == GstreamerBusOutcome::EndOfStream
                   && aggregate == GstreamerBusOutcome::Continue) {
            aggregate = GstreamerBusOutcome::EndOfStream;
        }
    }
    return aggregate;
}

GstreamerBusOutcome GstreamerPipeline::process_bus_message(
    GstMessage* message) noexcept
{
    if (message == nullptr) {
        std::cerr << "Error: cannot process a null GstBus message\n";
        started_ = false;
        run_finished_ = true;
        return GstreamerBusOutcome::Error;
    }

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
        std::cout << "Pipeline: EOS\n";
        started_ = false;
        run_finished_ = true;
        return GstreamerBusOutcome::EndOfStream;

    case GST_MESSAGE_ERROR: {
        GError* error = nullptr;
        gchar* debug_details = nullptr;
        gst_message_parse_error(message, &error, &debug_details);

        const GstObject* source = GST_MESSAGE_SRC(message);
        std::cerr << "Pipeline error source: "
                  << (source != nullptr ? GST_OBJECT_NAME(source) : "unknown")
                  << '\n'
                  << "Pipeline error message: "
                  << (error != nullptr ? error->message : "unavailable") << '\n'
                  << "Pipeline error debug: "
                  << (debug_details != nullptr ? debug_details : "unavailable")
                  << '\n';

        if (error != nullptr) {
            g_error_free(error);
        }
        g_free(debug_details);
        started_ = false;
        run_finished_ = true;
        return GstreamerBusOutcome::Error;
    }

    case GST_MESSAGE_WARNING: {
        GError* warning = nullptr;
        gchar* debug_details = nullptr;
        gst_message_parse_warning(message, &warning, &debug_details);

        const GstObject* source = GST_MESSAGE_SRC(message);
        std::cerr << "Pipeline warning source: "
                  << (source != nullptr ? GST_OBJECT_NAME(source) : "unknown")
                  << '\n'
                  << "Pipeline warning message: "
                  << (warning != nullptr ? warning->message : "unavailable")
                  << '\n'
                  << "Pipeline warning debug: "
                  << (debug_details != nullptr ? debug_details : "unavailable")
                  << '\n';

        if (warning != nullptr) {
            g_error_free(warning);
        }
        g_free(debug_details);
        return GstreamerBusOutcome::Continue;
    }

    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline_)) {
            GstState old_state = GST_STATE_VOID_PENDING;
            GstState new_state = GST_STATE_VOID_PENDING;
            GstState pending_state = GST_STATE_VOID_PENDING;
            gst_message_parse_state_changed(message, &old_state, &new_state,
                                            &pending_state);
            if (old_state != new_state) {
                std::cout << "Pipeline state changed: "
                          << gst_element_state_get_name(old_state) << " -> "
                          << gst_element_state_get_name(new_state);
                if (pending_state != GST_STATE_VOID_PENDING) {
                    std::cout << " (pending "
                              << gst_element_state_get_name(pending_state) << ')';
                }
                std::cout << '\n';
            }
        }
        return GstreamerBusOutcome::Continue;

    default:
        return GstreamerBusOutcome::Continue;
    }
}

bool GstreamerPipeline::stop() noexcept
{
    bool succeeded = true;

    if (pipeline_ != nullptr) {
        const GstStateChangeReturn request_result =
            gst_element_set_state(pipeline_, GST_STATE_NULL);
        if (request_result == GST_STATE_CHANGE_FAILURE) {
            std::cerr << "Error: failed to request GST_STATE_NULL\n";
            succeeded = false;
        } else {
            GstState current_state = GST_STATE_VOID_PENDING;
            GstState pending_state = GST_STATE_VOID_PENDING;
            const GstStateChangeReturn completion_result = gst_element_get_state(
                pipeline_, &current_state, &pending_state, kStateChangeTimeout);
            if (completion_result == GST_STATE_CHANGE_FAILURE
                || completion_result == GST_STATE_CHANGE_ASYNC
                || current_state != GST_STATE_NULL) {
                std::cerr << "Error: pipeline did not reach GST_STATE_NULL\n";
                succeeded = false;
            }
        }
    }

    started_ = false;
    built_ = false;
    run_finished_ = false;

    if (bus_ != nullptr) {
        gst_object_unref(bus_);
        bus_ = nullptr;
    }
    if (pipeline_ != nullptr) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    return succeeded;
}

} // namespace camstream
