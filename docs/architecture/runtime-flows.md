# Camera HAL Runtime Flows

Stage 8.4 routes the finite diagnostic directly through the public Camera HAL and the constructor-registered simulated
backend. Lifecycle and frame-ownership protections live in the HAL core rather than a mandatory C++ wrapper. The
Stage 8.4 architecture is complete within its host-validation scope. Stage 8.5 adds V4L2 backend Phase 1 discovery;
Phase 2 streaming, Buildroot integration, and board validation remain pending.

## Backend load and registration

```mermaid
sequenceDiagram
    actor Client as first HAL client
    participant Runtime as backend runtime
    participant Module as backend shared object
    participant Constructor as ELF constructor

    Client->>Runtime: load_backend(explicit path)
    Runtime->>Runtime: lock, mark LOADING, unlock
    Runtime->>Module: dlopen(RTLD_NOW | RTLD_LOCAL)
    Module->>Constructor: invoke constructor
    Constructor->>Runtime: register_backend_v1(static descriptor)
    Runtime->>Runtime: validate ABI, size, name, callbacks
    Runtime-->>Constructor: registration status
    Module-->>Runtime: module handle
    Runtime->>Runtime: lock, commit LOADED and client count, unlock
    Runtime-->>Client: OK
```

The runtime mutex is not held across `dlopen()`, because registration re-enters the HAL on the same thread. A concurrent
client waits while state is `LOADING` or `UNLOADING`. Once loaded, another request for the same path increments the
client count; it does not require another constructor invocation. A different path is rejected while the current
backend remains active.

## Final backend unload

```mermaid
flowchart LR
    Last["last client releases backend"] --> Live{"live instances == 0?"}
    Live -->|no| Reject["reject unload; remain LOADED"]
    Live -->|yes| Creates{"create reservations == 0?"}
    Creates -->|no| Reject
    Creates -->|yes| Unloading["UNLOADING"]
    Unloading --> Close["dlclose()"]
    Close -->|success| Unloaded["clear runtime state; UNLOADED"]
    Close -->|failure| Faulted["retain module ownership; clear callable ops; FAULTED"]
```

The runtime never calls `dlclose()` while a HAL camera instance remains alive or a create reservation is active. A
failed close does not advertise a reusable unloaded state: it preserves the module handle and diagnostic identity,
removes access to backend callbacks, and rejects later loads and camera operations.

## Successful camera lifecycle

```mermaid
sequenceDiagram
    participant App as "camstream-camera-test"
    participant HAL as "Camera HAL"
    participant Backend as "Simulated Backend"

    App->>HAL: camstream_camera_hal_load_backend(path)

    App->>HAL: camstream_camera_create()
    HAL->>Backend: ops.create()
    Backend-->>HAL: Opaque backend instance

    App->>HAL: camstream_camera_open(simulated0)
    HAL->>Backend: ops.open(instance)

    App->>HAL: camstream_camera_get_capabilities()
    HAL->>Backend: ops.get_capabilities(instance)

    App->>HAL: camstream_camera_get_stream_configuration()
    HAL->>Backend: ops.get_stream_configuration(instance)

    App->>HAL: camstream_camera_configure()
    HAL->>Backend: ops.configure(instance)

    App->>HAL: camstream_camera_start()
    HAL->>Backend: ops.start(instance)

    loop Requested frame count
        App->>HAL: camstream_camera_wait_frame()
        HAL->>Backend: ops.wait_frame()

        App->>HAL: camstream_camera_acquire_frame()
        HAL->>Backend: ops.acquire_frame()
        Backend-->>HAL: Backend token and borrowed planes
        HAL->>HAL: Validate frame
        HAL->>HAL: Map HAL token to backend token
        HAL-->>App: HAL token and borrowed planes

        App->>HAL: camstream_camera_release_frame(HAL token)
        HAL->>HAL: Validate ownership and token
        HAL->>Backend: ops.release_frame(instance, backend token)
        Backend-->>HAL: Release status
        HAL->>HAL: Remove mapping after successful release
    end

    App->>HAL: Stop camera
    HAL->>Backend: ops.stop(instance)

    App->>HAL: Close camera
    HAL->>Backend: ops.close(instance)

    App->>HAL: Destroy camera
    HAL->>Backend: ops.destroy(instance)

    App->>HAL: camstream_camera_hal_unload_backend()
    HAL->>HAL: Mark UNLOADING
    HAL->>HAL: dlclose()
    HAL->>HAL: Clear runtime state on success
```

## Failure and cleanup behavior

- An invalid or unloadable path fails without creating a camera instance.
- A module that does not register, registers more than once, or supplies an incompatible/incomplete descriptor leaves
  the runtime reusable in `UNLOADED` only when cleanup closes its handle successfully. A close failure enters terminal
  `FAULTED`, preserves module ownership for diagnostics, clears callable operations, and rejects later loads.
- A backend operation failure changes HAL state only after successful dispatch and remains available as a bounded
  backend or HAL diagnostic where applicable.
- A request to unload the final runtime reference while a HAL camera or create reservation is active fails, preserving
  callback-and-destroy-before-`dlclose()` ordering.

Release validates camera state and outstanding public-token membership before dispatch. A cross-camera release
therefore leaves both legitimate frames valid. Equal private backend tokens in two backend instances do not alter
ownership because the HAL exposes process-unique ownership tokens.

If frame validation or token tracking fails after backend acquisition, the HAL attempts immediate rollback. If rollback
also fails, it records one pending backend token, blocks further wait/acquire work, and retries during stop or
destruction. Camera destruction releases pending and outstanding tokens before stop, close, and backend-instance
destruction; module release remains an explicit caller operation after camera destruction.
