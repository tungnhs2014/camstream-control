# CamStream Control System Overview

## Purpose

CamStream Control is an Embedded Linux multimedia project for learning and demonstrating reproducible board support,
camera capture, GStreamer pipelines, service lifecycle control, and future network streaming. Correct ownership,
repeatable builds, and evidence from the actual target take priority over unsupported capability claims.

## Current development platform

The current supported reference platform is:

- Ubuntu 22.04 development host;
- BeagleBone Black target booting a project-owned Buildroot 2026.02.3 image;
- U-Boot 2026.01 and Linux 6.18.1;
- Logitech C270 USB UVC camera through the upstream `uvcvideo` driver;
- TP-Link `2357:010c` Wi-Fi adapter through `rtl8xxxu`;
- UART for boot evidence and Ethernet/Wi-Fi for target access.

The BeagleBone Black is the current integration and runtime-validation target. Future CSI-camera work is planned for a
platform with an appropriate CSI receiver and libcamera integration; it is not implemented or validated by Stage 8.3.

## C4-style System Context Diagram

This is a C4-style context view expressed with a Mermaid flowchart. It is not a UML diagram.

```mermaid
flowchart LR
    Developer["Developer<br/>Ubuntu 22.04 host"]
    System["CamStream Control<br/>Embedded Linux camera platform"]
    BBB["BeagleBone Black<br/>Current reference target"]
    Camera["Logitech C270<br/>USB UVC camera"]
    Client["Future streaming/control client"]

    Developer -->|configures, builds, flashes, validates| System
    System -->|runs on| BBB
    Camera -->|USB video frames| BBB
    BBB -. planned media and control traffic .-> Client
```

## Software boundaries

```mermaid
flowchart TB
    subgraph TargetUserspace[Target-validated BeagleBone Black userspace components]
        Service[camstream-service]
        Capture[camstream-capture]
        GstTest[camstream-gst-test]
        GstLib[camstream-gstreamer]
    end

    subgraph HalFoundation[Stage 8.4 Camera HAL runtime]
        HalTest[camstream-camera-test]
        HAL[camstream-camera-hal]
        Runtime[constructor-registration runtime]
        Ops[backend operations table]
        Simulated[simulated camera backend]
    end

    subgraph Planned[Not implemented in Stage 8.4]
        FutureBackends["V4L2 and libcamera camera backends<br/>planned"]
    end

    subgraph Kernel[Linux kernel space]
        Uvc[upstream uvcvideo]
        Synthetic[camstream-video synthetic V4L2 driver]
        Usb[USB host and media support]
    end

    C270[Logitech C270] --> Usb --> Uvc
    Service --> GstLib
    GstTest --> GstLib
    Capture --> Uvc
    Capture --> Synthetic
    HalTest --> HAL --> Ops --> Simulated
    HAL --> Runtime
    Simulated -->|ELF constructor registers static ops| Runtime
    HAL -. future .-> FutureBackends
    FutureBackends -. planned V4L2 path .-> Uvc
```

Kernel drivers own hardware-facing and V4L2 kernel behavior. Userspace applications own policy, diagnostics, pipeline
control, and process lifecycle. The Camera HAL adds a userspace portability boundary; it does not replace the kernel
V4L2 API or the production C270 path. Its foundation, simulated backend, and diagnostic inherit the host-validated
Stage 8.3 behavior. Stage 8.4 adds the public HAL dispatch and constructor-registration runtime while preserving that
behavior. Phases 1 and 2 are implemented and owner-validated within their host scope; Buildroot and board validation
remain outside this checkpoint.

## Current and planned components

| Component | Responsibility | Status |
| --- | --- | --- |
| `camstream-capture` | Native V4L2 diagnostic capture and frame output | Implemented and target validated |
| `camstream-gst-test` | Finite GStreamer diagnostic pipelines | Implemented and target validated |
| `camstream-service` | Foreground service owning the reusable GStreamer pipeline | Implemented through Stage 8.2 |
| `camstream-gstreamer` | Shared GStreamer pipeline implementation | Implemented through Stage 8.2 |
| `camstream-video` | Synthetic kernel V4L2 capture driver | Implemented and target validated |
| `camstream-camera-hal` | Shared Camera HAL contract, constructor-registration runtime, public dispatch, and C++ session wrapper | Implemented and owner-validated in host scope; Stage 8.4 ready for owner final validation |
| simulated camera backend | Hardware-independent Camera HAL backend | Owner-validated in host scope; not target validated |
| `camstream-camera-test` | Finite Camera HAL lifecycle diagnostic | Owner-validated in host scope; not target validated |
| V4L2 camera backend | Camera HAL backend using Linux V4L2 userspace APIs | Stage 8.6 planned, not implemented |
| libcamera camera backend | Camera HAL backend using libcamera | Planned, not implemented |
| IPC and network streaming | External control and production media delivery | Planned |

## Camera paths

The current USB production-oriented path remains:

```text
Logitech C270 -> USB -> uvcvideo -> V4L2 -> GStreamer -> camstream-service
```

The Stage 8.4 hardware-independent diagnostic path is:

```text
camstream-camera-test -> CameraSession -> Camera HAL -> backend operations -> simulated backend
```

A future USB portability path may place the planned V4L2 backend behind the same Camera HAL. A future CSI path may use
the planned libcamera backend. Neither backend exists in Stage 8.4. The simulated backend registers its
static operation table through an ELF constructor; runtime camera resources remain per instance.

## Repository structure

```text
apps/                              Project-owned userspace applications
backends/camera/simulated/         Simulated Camera HAL backend
br2-external/                      Buildroot BR2_EXTERNAL integration
docs/                              Architecture, development, guides, validation
drivers/                           Project-owned Linux kernel drivers
libs/camstream-camera-hal/         Camera HAL contract, backend SPI, runtime, dispatch, and C++ wrapper
libs/camstream-gstreamer/          Reusable GStreamer pipeline library
scripts/                           Host and target helper scripts
```

Generated source trees, Buildroot output, binaries, modules, images, credentials, and private runtime evidence remain
outside Git.
