#ifndef CAMSTREAM_CAMERA_CAMERA_BACKEND_H
#define CAMSTREAM_CAMERA_CAMERA_BACKEND_H

/**
 * @file camera_backend.h
 * @brief Versioned SPI implemented by replaceable Camera HAL backends.
 *
 * A backend owns every opaque instance and frame payload it creates. Each
 * instance is independent, and no callback may allow a C++ exception to cross
 * this C boundary.
 */

#include <camstream/camera/camera_hal.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque backend instance owned by the HAL after create(). */
typedef struct camstream_camera_instance camstream_camera_instance;

/** @brief Creates one opaque instance; ownership passes to the HAL. */
typedef camstream_camera_status_t (*camstream_camera_create_v1_fn)(camstream_camera_instance** instance);

/**
 * @brief Destroys an instance after all other callbacks have ceased.
 *
 * The backend must reclaim remaining resources even after partial
 * initialization or a failed best-effort stop/close sequence.
 */
typedef void (*camstream_camera_destroy_v1_fn)(camstream_camera_instance* instance);

/** @brief Opens one backend-specific source on a created instance. */
typedef camstream_camera_status_t (*camstream_camera_open_v1_fn)(camstream_camera_instance* instance,
                                                                 const char* source_identifier);

/** @brief Closes a stopped or non-started source. */
typedef camstream_camera_status_t (*camstream_camera_close_v1_fn)(camstream_camera_instance* instance);

/** @brief Reports bounded capabilities for an open source. */
typedef camstream_camera_status_t (*camstream_camera_get_capabilities_v1_fn)(
    camstream_camera_instance* instance, camstream_camera_capabilities_v1* capabilities);

/** @brief Returns one supported configuration by zero-based index. */
typedef camstream_camera_status_t (*camstream_camera_get_stream_configuration_v1_fn)(
    camstream_camera_instance* instance, uint32_t index, camstream_camera_stream_config_v1* configuration);

/** @brief Applies a request and reports the backend's active configuration. */
typedef camstream_camera_status_t (*camstream_camera_configure_v1_fn)(
    camstream_camera_instance* instance, const camstream_camera_stream_config_v1* requested,
    camstream_camera_stream_config_v1* active);

/** @brief Starts delivery for a configured instance. */
typedef camstream_camera_status_t (*camstream_camera_start_v1_fn)(camstream_camera_instance* instance);

/** @brief Waits for availability and may return the normal TIMEOUT status. */
typedef camstream_camera_status_t (*camstream_camera_wait_frame_v1_fn)(camstream_camera_instance* instance,
                                                                       uint32_t timeout_ms);

/** @brief Transfers temporary access to one backend-owned frame. */
typedef camstream_camera_status_t (*camstream_camera_acquire_frame_v1_fn)(camstream_camera_instance* instance,
                                                                          camstream_camera_frame_v1* frame);

/** @brief Returns an outstanding frame token and invalidates its plane views. */
typedef camstream_camera_status_t (*camstream_camera_release_frame_v1_fn)(camstream_camera_instance* instance,
                                                                          uint64_t frame_token);

/** @brief Stops delivery after every outstanding frame has been released. */
typedef camstream_camera_status_t (*camstream_camera_stop_v1_fn)(camstream_camera_instance* instance);

/** @brief Copies the last diagnostic into caller-owned bounded storage. */
typedef camstream_camera_status_t (*camstream_camera_get_last_error_v1_fn)(camstream_camera_instance* instance,
                                                                           char* buffer, uint32_t buffer_size);

/**
 * @brief Versioned operation table implemented by one Camera HAL backend.
 *
 * The descriptor and backend_name have static lifetime owned by the loaded
 * module. Every callback is mandatory in ABI v1. Runtime resources belong to
 * the instance returned by create(), never to module registration.
 */
typedef struct camstream_camera_backend_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* backend_name;
    camstream_camera_create_v1_fn create;
    camstream_camera_destroy_v1_fn destroy;
    camstream_camera_open_v1_fn open;
    camstream_camera_close_v1_fn close;
    camstream_camera_get_capabilities_v1_fn get_capabilities;
    camstream_camera_get_stream_configuration_v1_fn get_stream_configuration;
    camstream_camera_configure_v1_fn configure;
    camstream_camera_start_v1_fn start;
    camstream_camera_wait_frame_v1_fn wait_frame;
    camstream_camera_acquire_frame_v1_fn acquire_frame;
    camstream_camera_release_frame_v1_fn release_frame;
    camstream_camera_stop_v1_fn stop;
    camstream_camera_get_last_error_v1_fn get_last_error;
    uint32_t reserved[8];
} camstream_camera_backend_v1;

/**
 * @brief Registers one module-owned backend descriptor during dlopen().
 *
 * A backend ELF constructor calls this hook with static metadata and function
 * pointers only. The descriptor remains valid until the HAL destroys every
 * backend instance, clears its runtime state, and calls dlclose(). Registration
 * outside the runtime LOADING state or more than once for one load is rejected.
 */
camstream_camera_status_t camstream_camera_hal_register_backend_v1(const camstream_camera_backend_v1* backend);

#ifdef __cplusplus
}
#endif

#endif
