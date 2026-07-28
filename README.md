# CamStream Control

CamStream Control is an embedded Linux camera platform for learning and
demonstrating reproducible board support, Linux video capture, multimedia
pipelines, and a modern C++ control service. The reference target is a
BeagleBone Black running a project-owned Buildroot image with a Logitech C270
USB UVC camera.

## Architecture

Production multimedia path:

```text
Logitech C270
  -> uvcvideo
  -> V4L2
  -> GStreamer
  -> Network Streaming
```

Control path:

```text
IPC Client
  -> Camera Service
  -> controls GStreamer pipeline
```

Diagnostic path:

```text
camstream-video
  -> V4L2
  -> camstream-capture
```

The completed Stage 6C synthetic V4L2 capture driver provides a fixed YUYV
capture source for validating the same native V4L2 interface with
`camstream-capture`. It complements, rather than replaces, the C270 production
path through upstream `uvcvideo`.

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

Stage 6B application functionality, documentation, Buildroot integration,
clean image generation, and packaged BBB/C270 validation are **PASS**. YUYV
640x480 at 30 fps remains unstable under the current USB topology, and
long-term USB stability remains **DEFERRED** without a proven root cause.

Stage 7 is **IN PROGRESS**. Runtime capture-pipeline validation belongs to a
later Stage 7 checkpoint and has not yet been claimed.

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

Guides explain the reproducible project flow. Validation reports distinguish
runtime-tested behavior from enumerated capability and deferred work.

## Repository layout

```text
br2-external/  Project Buildroot external tree, defconfig, fragments, overlay
apps/          Project-owned native userspace applications
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
| Stage 7 — GStreamer integration | **IN PROGRESS** |

Stage 6 is complete. Stage 7 is in progress and must preserve the upstream
UVC/V4L2 baseline while each remaining checkpoint passes its prerequisite
gate.
