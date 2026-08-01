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
IPC Client
  -> Camera Service
  -> controls GStreamer pipeline
```

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

Stage 8.2 is **NEXT**: the Camera Service will own and control the existing
GStreamer pipeline component. That integration is not implemented yet.

C270 USB resets and inconsistent real throughput remain tracked as
[STAGE7-USB-01](docs/validation/stage-07-gstreamer-integration.md). Stable
production 30-fps operation and long-term USB reliability are not yet claimed.

## Documentation

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
| Stage 8.2 — Camera Service owns and controls GStreamer pipeline | **NEXT** |

Stages 6, 7, 8.0, and 8.1 are complete. Stage 8.2 is next but remains
unimplemented. Future work must preserve the upstream UVC/V4L2 baseline and
the evidence boundaries recorded above.
