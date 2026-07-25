# Stage 6A V4L2 camera bring-up validation

## 1. Objective

Validate real USB UVC camera bring-up on the CamStream Control BeagleBone Black
target, from Buildroot integration through V4L2 enumeration and a visually
verified frame capture.

## 2. Scope

This report covers the Stage 6A kernel configuration, Buildroot packages, build
artifacts, runtime USB/V4L2 behavior, camera formats and controls, and one-frame
capture. It does not cover a custom V4L2 driver, GStreamer integration,
production streaming, concurrent service behavior, or long-term USB stability.

## 3. Provenance

| Field | Recorded provenance |
| --- | --- |
| Documentation and evidence review date | 2026-07-25 |
| Target execution date | Not retained in the supplied evidence |
| Branch | `stage/06-camera-v4l2` |
| Base commit | `f26293273c60335912781e3e2cc2437b869871b9` (`stage 5: add network and SSH remote access`) |
| Tested configuration state | Stage 6A working tree with the camera defconfig additions and `linux-camera.fragment` applied |
| Build output | Local out-of-tree Stage 6A Buildroot output; not committed |
| Tested-image checksum | Not contemporaneously retained; no checksum is inferred after the test |
| Runtime evidence source | Manually recorded target observations; private captured frame and local evidence are not committed |

Validation was performed on the Stage 6A working tree, not on the base commit
alone. The implementation changes remained uncommitted at this documentation
checkpoint, so the branch name and base commit do not by themselves identify
the complete tested configuration.

## 4. Hardware and software baseline

| Component | Validated baseline |
| --- | --- |
| Target | BeagleBone Black |
| Build system | Buildroot 2026.02.3 |
| Linux kernel | 6.18.1 |
| Camera | Logitech C270 HD Webcam |
| Camera USB ID | `046d:0825` |
| Network path | TP-Link TL-WN722N v2/v3 using `rtl8xxxu` |
| SSH server | Dropbear |

The concise USB topology recorded during validation was:

```text
BeagleBone Black USB host
  -> USB hub
     -> TP-Link Wi-Fi adapter
     -> Logitech C270
     -> additional hub/device
```

The topology is descriptive evidence, not proof that a particular hub, device,
or power condition caused the intermittent resets.

## 5. Buildroot integration

The BeagleBone defconfig includes both the existing Wi-Fi fragment and the Stage 6A camera fragment:

```text
linux-wifi.fragment linux-camera.fragment
```

`BR2_PACKAGE_LIBV4L` and `BR2_PACKAGE_LIBV4L_UTILS` are enabled. The resulting
target contains `v4l2-ctl`, `media-ctl`, and `v4l2-compliance` for runtime
inspection.

## 6. Linux kernel configuration

The camera fragment enables the Linux media, V4L2, Media Controller,
USB-media, and UVC support required for the C270 capture path. Runtime
components such as the V4L2 core and UVC driver are configured as modules where
applicable.

```text
CONFIG_MEDIA_SUPPORT=m
CONFIG_MEDIA_SUPPORT_FILTER=y
CONFIG_MEDIA_CAMERA_SUPPORT=y
CONFIG_MEDIA_CONTROLLER=y
CONFIG_VIDEO_DEV=m
CONFIG_MEDIA_USB_SUPPORT=y
CONFIG_USB_VIDEO_CLASS=m
```

The generated Linux configuration contains these values, confirming that the fragment was applied.

## 7. Reproducibility validation

The Linux build directory was cleaned and regenerated with:

```text
linux-dirclean -> linux-configure
```

The regenerated Linux `.config` retained every value from
`linux-camera.fragment`. Camera support therefore comes from the
project-controlled BeagleBone defconfig and fragment rather than residual
`linux-menuconfig` state.

## 8. Build artifact validation

The full Buildroot build completed successfully and produced `sdcard.img`. The target filesystem contains:

- `uvcvideo.ko`
- `videodev.ko`
- `videobuf2-common.ko`
- `videobuf2-dma-contig.ko`
- `videobuf2-memops.ko`
- `videobuf2-v4l2.ko`
- `videobuf2-vmalloc.ko`
- `v4l2-ctl`, `media-ctl`, and `v4l2-compliance`

## 9. Runtime USB enumeration

The target detected the Logitech C270 as `046d:0825`, reported UVC 1.00, and
automatically registered `uvcvideo`. Runtime enumeration created two video
nodes and one media-controller node:

```text
USB identity: 046d:0825
uvcvideo: Found UVC 1.00 device C270 HD WEBCAM (046d:0825)
/dev/video0
/dev/video1
/dev/media0
```

The USB bus/address values are not retained because they are not needed to
establish device identity and may change after reconnect or reboot.

## 10. V4L2 device node analysis

| Node | Observed device capabilities | Role |
| --- | --- | --- |
| `/dev/video0` | Video Capture, Streaming, Extended Pix Format | Image capture |
| `/dev/video1` | Metadata Capture, Streaming, Extended Pix Format | UVC metadata |
| `/dev/media0` | Media-controller node | Media Controller topology |

`/dev/video0` is the real image capture node. `/dev/video1` must not be treated as a second image stream.

## 11. Format and frame-rate enumeration

Important enumerated modes were:

| Pixel format | Resolution | Frame rate |
| --- | --- | --- |
| YUYV | 640×480 | 30 fps |
| YUYV | 1280×720 | 10 fps |
| MJPEG | 640×480 | 30 fps |
| MJPEG | 1280×720 | 30 fps |

At 1280×720, the camera advertises MJPEG at 30 fps while YUYV reaches 10 fps.
This is a capability observation, not a streaming-performance measurement.

Only mode advertisement was established for this table. Stage 6A did not
runtime-stream every enumerated mode.

## 12. Camera control enumeration

V4L2 control enumeration succeeded. Observed controls included brightness,
contrast, saturation, white balance, gain, sharpness, backlight compensation,
automatic exposure, and exposure time. Stage 6A verified discoverability of
these controls; it did not functionally exercise all controls or characterize
their full operating ranges and image-quality effects.

## 13. Real frame capture validation

A 640×480 MJPEG frame was captured from `/dev/video0` using `v4l2-ctl`. The
initial one-frame capture was almost black. After skipping initial frames to
allow the stream to progress, a valid image was captured and visually verified
on the Ubuntu host. This is consistent with stream-start or exposure settling,
but Stage 6A did not establish the cause.

**Real MJPEG frame capture: PASS**

The captured frame is validation evidence only and is not added to Git.

**Host-side image visual verification: PASS**

The minimal retained runtime result is:

```text
capture node: /dev/video0
negotiated mode: 640x480 MJPEG
captured frame: non-empty and visually verified on the Ubuntu host
```

This summary does not publish the private camera image or claim a measured
frame rate, latency, or sustained-stream duration.

## 14. Engineering observations

- Explicit node-role inspection prevented the UVC metadata node from being mistaken for an image-capture node.
- Initial-frame skipping is relevant for later capture tooling because exposure may not have converged immediately after stream start.
- MJPEG exposes the higher enumerated 1280×720 frame rate and is a candidate for
  later pipeline evaluation; Stage 6A does not select the production format.
- Successful enumeration and one-frame capture establish functional V4L2 bring-up, but do not establish soak stability.

## 15. Known and deferred issues

**DEFERRED / KNOWN ISSUE: Intermittent USB reset under multi-device USB hub load.**

Observed messages included concise indicators such as
`musb-hdrc ep2 RX three-strikes error`, high-speed USB reset, and USB
disconnect/re-enumeration. No root cause has been proven.

Minimal high-value runtime indicators were:

```text
musb-hdrc ... ep2 RX three-strikes error
USB disconnect
reset high-speed USB device
```

Ellipses omit volatile controller/device details; the statements above preserve
the observed failure class without presenting a reconstructed raw transcript.

The issue is not fixed, and long-term USB stability is not marked as passed. It
must be revisited during system-integration and stability validation under the
intended multi-device topology.

## 16. Acceptance matrix

| Acceptance item | Result |
| --- | --- |
| Buildroot camera integration | PASS |
| Kernel V4L2/UVC configuration | PASS |
| Kernel configuration reproducibility | PASS |
| Full Buildroot image build | PASS |
| `uvcvideo` module present | PASS |
| `videodev` and videobuf2 modules present | PASS |
| V4L2 userspace tools present | PASS |
| C270 USB enumeration | PASS |
| `uvcvideo` automatic binding | PASS |
| `/dev/video0` image capture node | PASS |
| `/dev/video1` metadata node | PASS |
| `/dev/media0` presence | PASS |
| Video Capture capability | PASS |
| `Streaming` Device Caps enumeration | PASS |
| Format enumeration | PASS |
| Control enumeration | PASS |
| 640×480 MJPEG real capture | PASS |
| Host-side image visual verification | PASS |
| 1280×720 MJPEG runtime capture | NOT TESTED |
| 1280×720 YUYV runtime capture | NOT TESTED |
| All camera controls functional | NOT TESTED |
| Long-term USB stability | DEFERRED |

## 17. Conclusion

Stage 6A functional V4L2 bring-up is **COMPLETE**. The Logitech C270 enumerates
automatically through `uvcvideo`; `/dev/video0` is the image capture node,
`/dev/video1` is the metadata node, and `/dev/media0` exposes the media
controller. MJPEG advertises 1280×720 at 30 fps, compared with 10 fps for YUYV
at that resolution. A real 640×480 MJPEG frame capture passed after
initial frames were skipped.

Long-term USB stability remains deferred. This conclusion does not authorize a custom V4L2 driver or claim stable production streaming.
