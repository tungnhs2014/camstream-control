# Camera HAL Runtime Flows

Stage 8.4 routes the finite diagnostic through `CameraSession`, the public Camera HAL, and the constructor-registered
simulated backend. The lifecycle and frame-ownership protections are preserved from the completed Stage 8.3 host
scope. Phases 1 and 2 are implemented and owner-validated within the Stage 8.4 host scope; closure is ready for owner
final validation.

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
    Live -->|yes| Unloading["UNLOADING"]
    Unloading --> Close["dlclose()"]
    Close -->|success| Unloaded["clear runtime state; UNLOADED"]
    Close -->|failure| Faulted["retain module ownership; clear callable ops; FAULTED"]
```

The runtime never calls `dlclose()` while a HAL camera instance remains alive. A failed close does not advertise a
reusable unloaded state: it preserves the module handle and diagnostic identity, removes access to backend callbacks,
and rejects later loads and camera operations.

## Successful camera lifecycle

```mermaid
sequenceDiagram
    actor App as camstream-camera-test
    participant Session as CameraSession
    participant HAL as Camera HAL
    participant Backend as simulated backend instance

    App->>Session: load(explicit backend path)
    Session->>HAL: load backend, create camera
    HAL->>Backend: ops.create()
    Backend-->>HAL: opaque backend instance
    App->>Session: open("simulated0")
    Session->>HAL: camstream_camera_open()
    HAL->>Backend: ops.open(instance)
    App->>Session: query and configure
    Session->>HAL: public capability/configuration operations
    HAL->>Backend: matching ops(instance, ...)
    App->>Session: start()
    Session->>HAL: camstream_camera_start()
    HAL->>Backend: ops.start(instance)
    loop finite requested frame count
        App->>Session: wait/acquire
        Session->>HAL: public wait/acquire operations
        HAL->>Backend: ops.wait_frame / ops.acquire_frame
        Backend-->>Session: token and borrowed planes
        Session->>Session: validate frame and record token with session identity
        App->>Session: release_frame(frame)
        Session->>Session: validate owner identity and outstanding token
        Session->>HAL: camstream_camera_release_frame(token)
        HAL->>Backend: ops.release_frame(instance, token)
        Session->>Session: remove token and invalidate view
    end
    App->>Session: stop and close
    Session->>HAL: public stop and close operations
    HAL->>Backend: ops.stop and ops.close
    App->>Session: destroy session
    Session->>HAL: destroy camera
    HAL->>Backend: ops.destroy(instance)
    Session->>HAL: release backend reference
    HAL->>HAL: mark UNLOADING, dlclose, then clear state on success
```

## Failure and cleanup behavior

- An invalid or unloadable path fails without creating a camera instance.
- A module that does not register, registers more than once, or supplies an incompatible/incomplete descriptor leaves
  the runtime reusable in `UNLOADED` only when cleanup closes its handle successfully. A close failure enters terminal
  `FAULTED`, preserves module ownership for diagnostics, clears callable operations, and rejects later loads.
- A backend operation failure changes wrapper state only after successful dispatch and includes bounded backend
  diagnostics when available.
- A request to unload the final runtime reference while a HAL camera is alive fails, preserving
  destroy-before-`dlclose()` ordering.

Release validates session state, frame validity, originating session identity, and outstanding-token membership before
dispatch. A cross-session release therefore leaves both legitimate frames valid. Equal numeric tokens in two backend
instances do not alter ownership.

If wrapper validation or token tracking fails after backend acquisition, `CameraSession` attempts immediate rollback.
If rollback also fails, it records one pending cleanup token, blocks further wait/acquire work, and retries during stop
or destruction. Destructor cleanup releases pending and outstanding tokens before stop, close, camera destruction, and
runtime release.
