#ifndef CAMSTREAM_CAMERA_CAMERA_PPI_H
#define CAMSTREAM_CAMERA_CAMERA_PPI_H

/**
 * @file camera_ppi.h
 * @brief Stable Camera Platform Porting Interface shared by upper layers and
 * replaceable camera backends.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMSTREAM_CAMERA_ABI_VERSION_V1 UINT32_C(1)
#define CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE UINT32_C(128)
#define CAMSTREAM_CAMERA_MAX_PLANES UINT32_C(4)
#define CAMSTREAM_CAMERA_BACKEND_ENTRYPOINT_V1 "camstream_camera_get_backend_v1"

#define CAMSTREAM_CAMERA_FOURCC(a, b, c, d)                                                                            \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << UINT32_C(8)) | ((uint32_t)(uint8_t)(c) << UINT32_C(16)) |     \
     ((uint32_t)(uint8_t)(d) << UINT32_C(24)))

#define CAMSTREAM_CAMERA_PIXEL_FORMAT_YUYV CAMSTREAM_CAMERA_FOURCC('Y', 'U', 'Y', 'V')

/** @brief C-compatible status returned across the Camera PPI boundary. */
typedef int32_t camstream_camera_status_t;

#define CAMSTREAM_CAMERA_STATUS_OK ((camstream_camera_status_t)0)
#define CAMSTREAM_CAMERA_STATUS_TIMEOUT ((camstream_camera_status_t)1)
#define CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT ((camstream_camera_status_t)-1)
#define CAMSTREAM_CAMERA_STATUS_INVALID_STATE ((camstream_camera_status_t)-2)
#define CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED ((camstream_camera_status_t)-3)
#define CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR ((camstream_camera_status_t)-4)
#define CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR ((camstream_camera_status_t)-5)

/** @brief Opaque backend instance owned by the caller after create(). */
typedef struct camstream_camera_instance camstream_camera_instance;

/**
 * @brief Platform-neutral stream configuration exchanged with a backend.
 *
 * The caller initializes abi_version and struct_size before passing this
 * structure to a backend. source_identifier is a bounded, NUL-terminated
 * backend-specific identifier and is not a platform device structure.
 */
typedef struct camstream_camera_stream_config_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    char source_identifier[CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE];
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t frame_rate_numerator;
    uint32_t frame_rate_denominator;
    uint32_t reserved[4];
} camstream_camera_stream_config_v1;

/**
 * @brief Bounded capability summary for indexed configuration discovery.
 */
typedef struct camstream_camera_capabilities_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t stream_config_count;
    uint32_t maximum_plane_count;
    uint32_t reserved[4];
} camstream_camera_capabilities_v1;

/**
 * @brief One backend-owned image plane borrowed by an acquired frame.
 *
 * data remains valid only until release_frame() succeeds for the enclosing
 * frame token. The caller must not free or modify the pointed-to storage.
 */
typedef struct camstream_camera_plane_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const uint8_t* data;
    uint64_t allocation_size;
    uint64_t bytes_used;
    uint32_t stride;
    uint32_t reserved[3];
} camstream_camera_plane_v1;

/**
 * @brief Temporary multi-plane frame access transferred by acquire_frame().
 *
 * frame_token is opaque and backend-neutral. The backend may have multiple
 * outstanding tokens. All plane storage remains backend-owned and valid until
 * release_frame(instance, frame_token) succeeds.
 */
typedef struct camstream_camera_frame_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t frame_token;
    uint64_t sequence_number;
    uint64_t monotonic_timestamp_ns;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t plane_count;
    camstream_camera_plane_v1 planes[CAMSTREAM_CAMERA_MAX_PLANES];
    uint32_t reserved[4];
} camstream_camera_frame_v1;

/** @brief Creates one opaque instance; ownership passes to the caller. */
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
    camstream_camera_instance* instance,
    const camstream_camera_stream_config_v1* requested,
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
                                                                           char* buffer,
                                                                           uint32_t buffer_size);

/**
 * @brief Versioned backend descriptor returned by the exported entrypoint.
 *
 * The descriptor and backend_name have static lifetime owned by the loaded
 * module. Every callback is mandatory in ABI v1. No callback may allow an
 * exception to cross this C boundary.
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

/** @brief Function type of the single explicit Camera PPI v1 entrypoint. */
typedef const camstream_camera_backend_v1* (*camstream_camera_get_backend_v1_fn)(void);

/**
 * @brief Returns the module-owned Camera PPI v1 backend descriptor.
 *
 * A backend shared object must export this exact symbol. The returned pointer
 * remains valid until the loader calls dlclose() after destroying all backend
 * instances.
 */
const camstream_camera_backend_v1* camstream_camera_get_backend_v1(void);

#ifdef __cplusplus
}
#endif

#endif
