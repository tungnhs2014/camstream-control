#ifndef CAMSTREAM_CAMERA_CAMERA_HAL_H
#define CAMSTREAM_CAMERA_CAMERA_HAL_H

/**
 * @file camera_hal.h
 * @brief Platform-independent Camera HAL types and public C operations.
 *
 * This header is the C-compatible boundary presented to upper layers. It
 * deliberately contains no Linux, V4L2, GStreamer, or backend-loader types.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMSTREAM_CAMERA_ABI_VERSION_V1 UINT32_C(1)
#define CAMSTREAM_CAMERA_MAX_SOURCE_ID_SIZE UINT32_C(128)
#define CAMSTREAM_CAMERA_MAX_PLANES UINT32_C(4)

#define CAMSTREAM_CAMERA_FOURCC(a, b, c, d)                                                                            \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << UINT32_C(8)) | ((uint32_t)(uint8_t)(c) << UINT32_C(16)) |     \
     ((uint32_t)(uint8_t)(d) << UINT32_C(24)))

#define CAMSTREAM_CAMERA_PIXEL_FORMAT_YUYV CAMSTREAM_CAMERA_FOURCC('Y', 'U', 'Y', 'V')

/** @brief C-compatible status returned across the Camera HAL boundary. */
typedef int32_t camstream_camera_status_t;

#define CAMSTREAM_CAMERA_STATUS_OK ((camstream_camera_status_t)0)
#define CAMSTREAM_CAMERA_STATUS_TIMEOUT ((camstream_camera_status_t)1)
#define CAMSTREAM_CAMERA_STATUS_INVALID_ARGUMENT ((camstream_camera_status_t)-1)
#define CAMSTREAM_CAMERA_STATUS_INVALID_STATE ((camstream_camera_status_t)-2)
#define CAMSTREAM_CAMERA_STATUS_NOT_SUPPORTED ((camstream_camera_status_t)-3)
#define CAMSTREAM_CAMERA_STATUS_RESOURCE_ERROR ((camstream_camera_status_t)-4)
#define CAMSTREAM_CAMERA_STATUS_INTERNAL_ERROR ((camstream_camera_status_t)-5)

/**
 * @brief Platform-neutral stream configuration exchanged through the HAL.
 *
 * Callers initialize abi_version and struct_size before exchanging this
 * structure through the HAL. source_identifier is a bounded, NUL-terminated
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

/** @brief Bounded capability summary for indexed configuration discovery. */
typedef struct camstream_camera_capabilities_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t stream_config_count;
    uint32_t maximum_plane_count;
    uint32_t reserved[4];
} camstream_camera_capabilities_v1;

/**
 * @brief One implementation-owned image plane borrowed by an acquired frame.
 *
 * data remains valid only until the enclosing frame is successfully released
 * through the HAL. The upper layer must not free or modify the storage.
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
 * frame_token is an opaque, process-unique HAL ownership token. It identifies
 * both the acquired frame and its originating camera, so another camera cannot
 * release it even when two backends use the same private token value. An
 * implementation may have multiple outstanding frames. All plane storage
 * remains implementation-owned and valid until the matching HAL release
 * operation succeeds.
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

/**
 * @brief Opaque Camera HAL instance owned by the caller after create().
 *
 * One instance owns its backend instance, lifecycle state, and every acquired
 * frame token. Calls on one instance must be serialized by the caller.
 */
typedef struct camstream_camera camstream_camera;

/**
 * @brief Acquires one process-wide reference to an explicit backend module.
 *
 * The first reference loads the module and validates its constructor
 * registration. Further references to the same path reuse that module. A
 * runtime fault caused by failed module cleanup rejects later load requests.
 */
camstream_camera_status_t camstream_camera_hal_load_backend(const char* backend_path);

/**
 * @brief Releases one backend-module reference.
 *
 * The last release unloads the module. It fails while any HAL camera instance
 * remains alive, enforcing destroy-before-dlclose ordering. A failed module
 * close leaves the runtime faulted and unavailable rather than falsely unloaded.
 */
camstream_camera_status_t camstream_camera_hal_unload_backend(void);

/** @brief Copies the last process-wide backend-loader diagnostic. */
camstream_camera_status_t camstream_camera_hal_get_last_error(char* buffer, uint32_t buffer_size);

/** @brief Creates one opaque HAL camera through the active backend. */
camstream_camera_status_t camstream_camera_create(camstream_camera** camera);

/**
 * @brief Destroys one HAL camera and its backend instance.
 *
 * Destruction performs best-effort frame release, stop, and close before the
 * backend instance is destroyed. The module reference remains caller-owned
 * and must be released separately after all cameras are destroyed.
 */
void camstream_camera_destroy(camstream_camera* camera);

/** @brief Copies the validated backend identity for one HAL camera. */
camstream_camera_status_t camstream_camera_get_backend_name(camstream_camera* camera, char* buffer,
                                                            uint32_t buffer_size);

/** @brief Returns the validated backend ABI version for one HAL camera. */
camstream_camera_status_t camstream_camera_get_backend_abi_version(camstream_camera* camera, uint32_t* abi_version);

/** @brief Opens one backend-specific source from Created state. */
camstream_camera_status_t camstream_camera_open(camstream_camera* camera, const char* source_identifier);

/** @brief Closes a stopped or non-started source. */
camstream_camera_status_t camstream_camera_close(camstream_camera* camera);

/** @brief Reports bounded capabilities for an open source. */
camstream_camera_status_t camstream_camera_get_capabilities(camstream_camera* camera,
                                                            camstream_camera_capabilities_v1* capabilities);

/** @brief Returns one supported configuration by zero-based index. */
camstream_camera_status_t camstream_camera_get_stream_configuration(camstream_camera* camera, uint32_t index,
                                                                    camstream_camera_stream_config_v1* configuration);

/** @brief Applies a request and reports the backend's active configuration. */
camstream_camera_status_t camstream_camera_configure(camstream_camera* camera,
                                                     const camstream_camera_stream_config_v1* requested,
                                                     camstream_camera_stream_config_v1* active);

/** @brief Starts frame delivery. */
camstream_camera_status_t camstream_camera_start(camstream_camera* camera);

/** @brief Waits for frame availability and may return the normal TIMEOUT status. */
camstream_camera_status_t camstream_camera_wait_frame(camstream_camera* camera, uint32_t timeout_ms);

/**
 * @brief Acquires temporary access to one backend-owned frame.
 *
 * The HAL validates backend metadata, assigns a HAL ownership token, and
 * rolls the backend acquisition back if the frame cannot be safely tracked.
 */
camstream_camera_status_t camstream_camera_acquire_frame(camstream_camera* camera, camstream_camera_frame_v1* frame);

/**
 * @brief Returns an outstanding frame token and invalidates its plane views.
 *
 * Unknown, stale, already-released, and different-camera tokens are rejected.
 */
camstream_camera_status_t camstream_camera_release_frame(camstream_camera* camera, uint64_t frame_token);

/** @brief Stops frame delivery after every outstanding frame is released. */
camstream_camera_status_t camstream_camera_stop(camstream_camera* camera);

/** @brief Copies the backend instance's last diagnostic into bounded storage. */
camstream_camera_status_t camstream_camera_get_last_error(camstream_camera* camera, char* buffer, uint32_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
