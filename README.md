# CamStream Control

CamStream Control is an embedded Linux camera platform for learning and
demonstrating reproducible board support, Linux video capture, multimedia
pipelines, and a modern C++ control service. The reference target is a
BeagleBone Black running a project-owned Buildroot image with a Logitech C270
USB UVC camera.

## Architecture

The production-facing reference path is:

```text
Logitech C270 -> uvcvideo -> V4L2 -> native capture -> GStreamer -> service
```

A later learning path will implement a virtual V4L2 capture driver and feed it
through the same userspace layers. The custom driver will complement, not
replace, the upstream `uvcvideo` reference path.

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

Completed checkpoints through Stage 6A are:

- Stage 1: Ubuntu host setup and verification
- Stage 2: known-good BeagleBone Black hardware baseline
- Stages 3–4: reproducible Buildroot external tree, boot image, UART boot, and
  target baseline
- Stage 5: Wi-Fi networking, DHCP, DNS, Dropbear SSH, and SCP
- Stage 6A: upstream UVC/V4L2 integration, capability inspection, and one
  visually verified 640x480 MJPEG frame capture

Stage 6A is the current completed implementation checkpoint. Stage 6B has not
started. Advertised camera modes and controls have not all been exercised, and
long-term stability under the observed multi-device USB topology remains
**DEFERRED**.

## Documentation

- [Ubuntu host setup](docs/guides/host-setup.md)
- [BeagleBone Buildroot bring-up](docs/guides/beaglebone-buildroot-bringup.md)
- [Network and remote-access bring-up](docs/guides/network-remote-access-bringup.md)
- [V4L2 USB camera bring-up](docs/guides/v4l2-camera-bringup.md)
- [Stage 6A V4L2 validation](docs/validation/stage-06a-v4l2-camera-bringup.md)

Guides explain the reproducible project flow. Validation reports distinguish
runtime-tested behavior from enumerated capability and deferred work.

## Repository layout

```text
br2-external/  Project Buildroot external tree, defconfig, fragments, overlay
docs/guides/   Focused engineering bring-up guides
docs/validation/ Public validation summaries
scripts/host/  Host environment inspection helpers
```

Generated Buildroot output, images, local evidence, credentials, and captured
camera frames do not belong in this repository.

## Roadmap

The next implementation work is the Stage 6B virtual V4L2 learning driver,
followed by native capture, GStreamer integration, the control service, and
system-level validation. Each layer must preserve the upstream UVC/V4L2
baseline and pass its prerequisite gate before the next layer begins.
