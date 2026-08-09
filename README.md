# CamStream Control

CamStream Control is an embedded Linux camera platform for learning and
demonstrating reproducible board support, Linux video capture, multimedia
pipelines, and a modern C++ control service. The reference target is a
BeagleBone Black running a project-owned Buildroot image with a Logitech C270
USB UVC camera.

## Architecture

Planned production multimedia path:

```text
Logitech C270
  -> uvcvideo
  -> V4L2
  -> GStreamer
  -> Network Streaming
```

Planned control path:

```text
Camera Service
  -> owns and controls reusable GStreamer pipeline
  -> future IPC / network streaming
```

Camera portability path:

```text
Application / future camstreamsrc
  -> Camera HAL public C API
  -> Camera HAL core
  -> backend operations table
  -> product backend shared object
```

Stage 8.4 Phase 2 implements explicit-path module loading with ELF-constructor
registration. The shared HAL runtime validates one backend operation table,
and upper layers dispatch only through the public, status-based Camera HAL API.
The active architecture has no `CameraSession` or `CameraError` layer. Stage 8.5
adds the V4L2 product backend Phase 1 discovery path; configuration and streaming
remain pending Phase 2.

Diagnostic path:

```text
C270 or camstream-video
  -> V4L2
  -> camstream-capture or camstream-gst-test
```

The completed Stage 6C synthetic V4L2 capture driver provides a fixed YUYV
capture source for validating the same native V4L2 interface with
`camstream-capture` and the native GStreamer diagnostic component. It
complements, rather than replaces, the C270 production path through upstream
`uvcvideo`.

## Validated baseline

| Area | Project baseline |
| --- | --- |
| Target | BeagleBone Black, MicroSD boot |
| Build system | Buildroot 2026.02.3 with the `CAMSTREAM` `BR2_EXTERNAL` tree |
| Bootloader | U-Boot 2026.01 |
| Kernel | Linux 6.18.1 |
| Camera | Logitech C270, `046d:0825`, upstream `uvcvideo` |
| Network | TP-Link `2357:010c`, `rtl8xxxu`, WPA/DHCP |
| Remote access | Dropbear SSH and SCP |

## Project status

Completed engineering checkpoints and validated functionality are:

- Stage 1: Ubuntu host setup and verification
- Stage 2: known-good BeagleBone Black hardware baseline
- Stages 3–4: reproducible Buildroot external tree, boot image, UART boot, and
  target baseline
- Stage 5: Wi-Fi networking, DHCP, DNS, Dropbear SSH, and SCP
- Stage 6A — C270 with upstream `uvcvideo`: **COMPLETE**
- Stage 6B — native V4L2 capture application: **COMPLETE**, including
  Buildroot packaging and packaged BBB/C270 runtime validation
- Stage 6C — synthetic V4L2 capture driver: **COMPLETE**, including packaged
  rootfs integration, paced YUYV capture, reload, cleanup, and C270 coexistence
- Stage 7.1 — GStreamer Buildroot bring-up: **COMPLETE**, including packaged
  runtime tools and required element discovery on the BBB
- Stage 7.2 — synthetic V4L2 GStreamer pipeline: **COMPLETE**, including three
  successful 300-buffer runs with EOS and clean teardown
- Stage 7.3 — real C270 GStreamer pipelines: **COMPLETE — FUNCTIONAL
  VALIDATION**, covering raw YUY2 and MJPEG decode paths
- Stage 7.4 — reusable C++ GStreamer pipeline component: **COMPLETE —
  FUNCTIONAL VALIDATION**, including packaged BBB execution with the synthetic
  source and both accepted C270 paths
- Stage 7.5 — final Buildroot and artifact acceptance: **COMPLETE — FINAL
  ACCEPTANCE**
- Stage 8.0 — userspace CMake foundation: **COMPLETE**, including host and
  Buildroot validation plus accepted BBB behavior-preservation smoke tests
- Stage 8.1 — camera service skeleton: **COMPLETE**, including host and
  Buildroot validation plus accepted BBB service-lifecycle smoke tests
- Stage 8.2 — GStreamer Service Integration: **COMPLETE**, including service
  ownership of the reusable pipeline and accepted BBB functional validation
- Stage 8.3 — Camera PPI Core: **COMPLETE** within its documented host-validation
  scope, including the corrective cross-session ownership regression
- Stage 8.4 — Camera HAL Architecture Refactor: **COMPLETE** within its
  documented host-validation scope, with constructor registration,
  process-wide runtime ownership, and direct status-based HAL dispatch
- Stage 8.5 — V4L2 Camera HAL Backend: **IN PROGRESS**; Phase 1 device opening,
  capability checks, and discrete format/size/interval discovery are
  implemented, while Phase 2 configuration and streaming remain pending

Stage 6B application functionality, documentation, Buildroot integration,
clean image generation, and packaged BBB/C270 validation are **PASS**. YUYV
640x480 at 30 fps remains unstable under the current USB topology, and
long-term USB stability remains **DEFERRED** without a proven root cause.

Stage 7 is **COMPLETE**. Synthetic and real-camera functional pipelines, the
reusable C++ component, final incremental Buildroot image generation, and the
focused target-artifact audit passed their acceptance gates.

Stage 8.0 — the userspace CMake foundation — is **COMPLETE**. The migration
introduces a target-based CMake build for `camstream-capture`,
`camstream-gst-test`, and the reusable static `camstream-gstreamer` library.
Source equivalence, host CLI behavior, Buildroot integration, and finite BBB
behavior-preservation smoke tests passed. That foundation is the prerequisite
for the Stage 8.1 service skeleton.

Stage 8.1 adds the minimal foreground `camstream-service` lifecycle with
synchronous SIGINT/SIGTERM handling through `signalfd` and `poll`. Host
lifecycle tests, Buildroot packaging, the incremental image, and focused
target artifacts are validated. BBB runtime dependency resolution, CLI error
handling, SIGINT/SIGTERM shutdown, repeated lifecycle, and process cleanup
passed the accepted finite smoke test. Stage 8.1 is **COMPLETE**. The skeleton
does not yet open a camera or own GStreamer, IPC, networking, recording,
init-system, or recovery behavior.

Stage 8.2 makes `camstream-service` own and control the existing reusable
`camstream::gstreamer` pipeline. The foreground service polls `signalfd` and
the GStreamer bus descriptor together, supports continuous operation and
finite validation runs, handles EOS, errors, warnings, and pipeline state
changes, and performs deterministic signal-driven shutdown. Host, Buildroot,
and accepted BBB functional validation passed. Stage 8.2 is **COMPLETE**.

Stage 8.3 introduces the stable Camera PPI C ABI, explicit backend loading,
the then-current C++17 `CameraSession` ownership wrapper, a simulated backend,
and a finite diagnostic application. Owner-provided host build and lifecycle
evidence, failure paths, sanitizers, Valgrind, and 100 repeated executions passed.
Post-validation review identified and corrected a cross-session frame-ownership
gap. The ownership regression, corrective ASan/UBSan validation, and corrective
Valgrind validation passed without changing the Camera PPI C ABI or backend
ABI. Stage 8.3 is **COMPLETE** within its documented host-validation scope.
Buildroot, BeagleBone Black, and Raspberry Pi validation were not run for this
stage.

Stage 8.4 refactors the completed PPI foundation into the Camera HAL
product-porting architecture. The implementation keeps the platform-independent
HAL contract separate from the backend operation table, replaces the
descriptor-return loader with ELF-constructor registration, and routes
upper layers directly through the status-based public HAL operations. The HAL
core owns lifecycle and frame-token protection, while the process-wide runtime
shares one loaded backend across camera instances and preserves
destroy-before-unload ordering. Phases 1 and 2 are implemented and
owner-validated within their host scope. Stage 8.4 is **COMPLETE** within that
documented host scope; Buildroot integration and target validation were not
claimed by that checkpoint.

Stage 8.5 implements Phase 1 of the V4L2 Camera HAL backend: opening an explicit
device, validating capture and streaming capabilities, and enumerating discrete
YUYV configurations. The active upper-layer model is the direct status-based
Camera HAL API; `CameraSession` and `CameraError` are not part of the current
architecture. V4L2 format application, MMAP, streaming, and frame delivery are
Phase 2 work and are not implemented. The simulated backend is host validated;
Buildroot and board validation for the current HAL/V4L2 backend remain deferred.

C270 USB resets and inconsistent real throughput remain tracked as
[STAGE7-USB-01](docs/validation/stage-07-gstreamer-integration.md). Stable
production 30-fps operation and long-term USB reliability are not yet claimed.

## Documentation

- [Documentation index](docs/README.md)
- [System overview](docs/architecture/system-overview.md)
- [Camera HAL design](docs/architecture/camera-hal-design.md)
- [Project coding standard](docs/development/coding-standard.md)
- [Debugging and validation guide](docs/development/debugging-and-validation-guide.md)
- [Ubuntu host setup](docs/guides/host-setup.md)
- [BeagleBone Buildroot bring-up](docs/guides/beaglebone-buildroot-bringup.md)
- [Network and remote-access bring-up](docs/guides/network-remote-access-bringup.md)
- [V4L2 USB camera bring-up](docs/guides/v4l2-camera-bringup.md)
- [Native V4L2 capture application](docs/guides/native-v4l2-capture.md)
- [Stage 6A V4L2 validation](docs/validation/stage-06a-v4l2-camera-bringup.md)
- [Stage 6B native application validation](docs/validation/stage-06b-native-v4l2-app.md)
- [Stage 6C synthetic driver validation](docs/validation/stage-06c-synthetic-v4l2-driver.md)
- [Stage 7 GStreamer validation](docs/validation/stage-07-gstreamer-integration.md)
- [Stage 8.0 userspace CMake foundation](docs/validation/stage-08-userspace-cmake-foundation.md)
- [Stage 8.1 camera service skeleton](docs/validation/stage-08.1-camera-service-skeleton.md)
- [Stage 8.2 GStreamer service integration](docs/validation/stage-08.2-gstreamer-service-integration.md)
- [Stage 8.3 Camera PPI Core](docs/validation/stage-08.3-camera-ppi.md)

Guides explain the reproducible project flow. Validation reports distinguish
runtime-tested behavior from enumerated capability and deferred work.

## Repository layout

```text
CMakeLists.txt  Root userspace CMake project and build-selection options
br2-external/  Project Buildroot external tree, defconfig, fragments, overlay
apps/          Project-owned native userspace applications
libs/          Reusable project-owned userspace libraries
drivers/       Project-owned kernel drivers
docs/guides/   Focused engineering bring-up guides
docs/validation/ Public validation summaries
scripts/host/  Host environment inspection helpers
```

Generated Buildroot output, images, local evidence, credentials, and captured
camera frames do not belong in this repository.

## Roadmap

| Stage | Status |
| --- | --- |
| Stage 6A — C270 and upstream `uvcvideo` | **COMPLETE** |
| Stage 6B — native V4L2 capture application | **COMPLETE** |
| Stage 6C — synthetic V4L2 capture driver | **COMPLETE** |
| Stage 7.1 — GStreamer Buildroot bring-up | **COMPLETE** |
| Stage 7.2 — synthetic V4L2 pipeline | **COMPLETE** |
| Stage 7.3 — real C270 pipelines | **COMPLETE — FUNCTIONAL VALIDATION** |
| Stage 7.4 — C++ GStreamer pipeline component | **COMPLETE — FUNCTIONAL VALIDATION** |
| Stage 7.5 — final Buildroot and artifact acceptance | **COMPLETE — FINAL ACCEPTANCE** |
| Stage 7 — GStreamer integration | **COMPLETE** |
| Stage 8.0 — userspace CMake foundation | **COMPLETE** |
| Stage 8.1 — camera service skeleton | **COMPLETE** |
| Stage 8.2 — GStreamer Service Integration | **COMPLETE** |
| Stage 8.3 — Camera PPI Core | **COMPLETE** |
| Stage 8.4 — Camera HAL Architecture Refactor | **COMPLETE — HOST SCOPE** |
| Stage 8.5 — V4L2 Camera HAL Backend | **IN PROGRESS — PHASE 1 IMPLEMENTED** |
| Stage 8.6 — GStreamer `camstreamsrc` | **PLANNED** |
| Stage 8.7 — Service Integration | **PLANNED** |
| Stage 9 — network camera streaming | **PLANNED** |

Stages 6, 7, 8.0, 8.1, 8.2, 8.3, and 8.4 are complete within their documented
validation scopes. Stage 8.5 Phase 1 is implemented; Phase 2, Buildroot
integration, and board validation remain pending. Future work must preserve the
upstream UVC/V4L2 baseline and the evidence boundaries recorded above.
