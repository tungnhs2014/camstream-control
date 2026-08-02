# CamStream Control Documentation

This index links the public architecture, development, bring-up, and validation material. Detailed evidence remains in
the validation reports rather than being repeated here.

## Architecture

- [System overview](architecture/system-overview.md)
- [Camera PPI design](architecture/camera-ppi-design.md)
- [Stage 8.3 runtime flows](architecture/runtime-flows.md)

## Development practices

- [Coding standard](development/coding-standard.md)
- [Debugging and validation guide](development/debugging-and-validation-guide.md)

## Bring-up guides

- [Ubuntu host setup](guides/host-setup.md)
- [BeagleBone Buildroot bring-up](guides/beaglebone-buildroot-bringup.md)
- [Network and remote-access bring-up](guides/network-remote-access-bringup.md)
- [V4L2 USB camera bring-up](guides/v4l2-camera-bringup.md)
- [Native V4L2 capture application](guides/native-v4l2-capture.md)

## Validation reports

- [Stage 6A — V4L2 camera bring-up](validation/stage-06a-v4l2-camera-bringup.md)
- [Stage 6B — native V4L2 application](validation/stage-06b-native-v4l2-app.md)
- [Stage 6C — synthetic V4L2 capture driver](validation/stage-06c-synthetic-v4l2-driver.md)
- [Stage 7 — GStreamer integration](validation/stage-07-gstreamer-integration.md)
- [Stage 8.0 — userspace CMake foundation](validation/stage-08-userspace-cmake-foundation.md)
- [Stage 8.1 — Camera Service skeleton](validation/stage-08.1-camera-service-skeleton.md)
- [Stage 8.2 — GStreamer Service integration](validation/stage-08.2-gstreamer-service-integration.md)
- [Stage 8.3 — Camera PPI Core](validation/stage-08.3-camera-ppi.md)
