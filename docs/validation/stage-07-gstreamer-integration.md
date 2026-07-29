# Stage 7 — GStreamer Integration

## Checkpoint status

- Validation dates: 2026-07-28 through 2026-07-29
- Branch: `stage/07-gstreamer-integration`
- Stage 7.1 — GStreamer Buildroot bring-up: **COMPLETE**
- Stage 7.2 — Synthetic V4L2 GStreamer pipeline: **COMPLETE**
- Stage 7.3 — Real C270 GStreamer pipelines: **COMPLETE — FUNCTIONAL VALIDATION**
- Stage 7.4 — C++ GStreamer pipeline component: **COMPLETE — FUNCTIONAL VALIDATION**
- Stage 7.5 — Final acceptance: **COMPLETE — FINAL ACCEPTANCE**
- Stage 7: **COMPLETE**

## Buildroot configuration

The project BeagleBone defconfig enables the GStreamer runtime and the focused
plugins required by this checkpoint:

```text
BR2_PACKAGE_GSTREAMER1=y
BR2_PACKAGE_GST1_PLUGINS_GOOD=y
BR2_PACKAGE_GST1_PLUGINS_GOOD_JPEG=y
BR2_PACKAGE_GST1_PLUGINS_GOOD_PLUGIN_V4L2=y
```

`BR2_PACKAGE_GSTREAMER1_INSTALL_TOOLS=y` is enabled by the Buildroot 2026.02.3
default for GStreamer and was present in the effective configuration. The
existing CamStream packages remain enabled:

```text
BR2_PACKAGE_CAMSTREAM_CAPTURE=y
BR2_PACKAGE_CAMSTREAM_GST_TEST=y
BR2_PACKAGE_CAMSTREAM_VIDEO=y
```

## BeagleBone Black runtime evidence

The final Buildroot image was booted on the BeagleBone Black. The following
commands completed successfully:

```sh
gst-launch-1.0 --version
gst-inspect-1.0 fakesink
gst-inspect-1.0 v4l2src
gst-inspect-1.0 jpegdec
gst-inspect-1.0 videoconvert
```

These Stage 7.1 checks alone validate only the runtime, command-line tools,
and required element factories. Synthetic pipeline execution is validated
separately in Stage 7.2 below; real-camera execution is validated in Stage
7.3.

## Acceptance matrix

| Check | Result |
| --- | --- |
| GStreamer runtime installed | **PASS** |
| GStreamer command-line tools | **PASS** |
| `fakesink` available | **PASS** |
| `v4l2src` available | **PASS** |
| `jpegdec` available | **PASS** |
| `videoconvert` available | **PASS** |
| `camstream-capture` package preserved | **PASS** |
| `camstream-video` package preserved | **PASS** |

Stage 7.1 is **COMPLETE**. Stage 7 remained **IN PROGRESS** at this checkpoint.

## Stage 7.2 — Synthetic V4L2 GStreamer Pipeline

### BeagleBone Black runtime evidence

The synthetic driver dynamically registered as `video2` during this test; the
node number is session evidence, not a fixed ABI. `v4l2src` opened the
synthetic V4L2 capture node and negotiated:

```text
video/x-raw
format=YUY2
width=640
height=480
framerate=30/1
```

One `GST_DEBUG=2` run processed 300 buffers in approximately 10.11 seconds.
Two additional 300-buffer runs also completed successfully. Every run received
EOS, returned exit status 0, transitioned the pipeline back to NULL, and freed
the pipeline. The module then unloaded and unregistered `video2` cleanly. No
CamStream-related kernel WARNING, Oops, BUG, use-after-free, list corruption,
or workqueue failure was observed.

### Non-blocking observations

- `VIDIOC_CROPCAP` failed because the fixed-format synthetic driver does not
  implement crop capability. Stage 7.2 does not claim crop support.
- GStreamer enabled the V4L2 copy threshold because buffer-count/zero-copy
  certainty was insufficient. This is a performance observation, not a
  functional failure; Stage 7.2 does not claim zero-copy support.

### Acceptance matrix

| Check | Result |
| --- | --- |
| Synthetic V4L2 node opened by `v4l2src` | **PASS** |
| YUY2 640x480 at 30/1 negotiated | **PASS** |
| `GST_DEBUG=2` 300-buffer run | **PASS** |
| Two additional 300-buffer runs | **PASS** |
| EOS for every run | **PASS** |
| Exit status 0 for every run | **PASS** |
| Pipeline returned to NULL and was freed | **PASS** |
| Module unload and node removal | **PASS** |
| CamStream-related kernel WARNING/Oops/BUG | **NONE OBSERVED** |
| Crop support | **NOT CLAIMED** |
| Zero-copy support | **NOT CLAIMED** |

Stage 7.2 is **COMPLETE**. Stage 7 remained **IN PROGRESS** at this checkpoint.

## Stage 7.3 — Real C270 GStreamer Pipelines

### Camera identification

The real camera was identified dynamically rather than by a fixed video-node
number:

| Property | Observed value |
| --- | --- |
| Driver | `uvcvideo` |
| Device | Logitech C270 HD WEBCAM |
| Selected device capabilities | Video Capture, Streaming |

No fixed `/dev/videoN` ABI is claimed.

### Raw YUYV pipeline

The accepted raw pipeline was:

```text
v4l2src
  -> video/x-raw,format=YUY2,width=320,height=240,framerate=30/1
  -> fakesink
```

Caps negotiation succeeded for YUY2 320x240 at 30/1. Repeated accepted runs
reached EOS, returned exit status 0, transitioned to NULL, and freed the
pipeline. One direct-USB GStreamer run completed 300 buffers in approximately
10.32 seconds, or about 29.1 buffers per second. Later direct native V4L2 tests
showed inconsistent throughput around 19–20 buffers per second.

### MJPEG decode pipeline

The accepted decode pipeline was:

```text
v4l2src
  -> image/jpeg,width=640,height=480,framerate=30/1
  -> jpegdec
  -> videoconvert
  -> fakesink
```

The source negotiated image/jpeg 640x480 at 30/1, and `jpegdec` produced
video/x-raw I420. Repeated accepted runs reached EOS, returned exit status 0,
transitioned to NULL, and freed the pipeline. Observed runtime was
approximately 15.3–15.6 seconds for 300 buffers, or about 19–20 buffers per
second.

### MJPEG passthrough isolation

The isolation pipeline removed software decoding and conversion:

```text
v4l2src
  -> image/jpeg,width=640,height=480,framerate=30/1
  -> fakesink
```

It completed with EOS and exit status 0. Processing 300 buffers took
approximately 15.39 seconds, or about 19.5 buffers per second. This result
does not show `jpegdec` or `videoconvert` to be the primary throughput
bottleneck.

### Native V4L2 isolation

Direct `v4l2-ctl` capture reproduced approximately 19–20 fps without
GStreamer:

| Mode | Buffers | Elapsed | Final reported rate | Additional evidence |
| --- | ---: | ---: | ---: | --- |
| MJPEG 640x480 | 300 | approximately 16.25 s | approximately 19.8 fps | One dropped buffer; exit 0 |
| YUYV 320x240 | 300 | approximately 16.14 s | approximately 19.9 fps | Exit 0 |

### Isolation conclusion and functional acceptance

Functional GStreamer integration passed. The inconsistent throughput is not
specific to GStreamer: `fakesink sync=false` produced similar results, so sink
synchronization was excluded; MJPEG passthrough produced similar throughput,
so JPEG software decoding was not shown to be the primary bottleneck; and
native V4L2 capture reproduced the lower rate without GStreamer. The USB hub
and WiFi adapter were not the sole cause because resets also occurred with the
C270 connected directly to the BBB. The exact root cause has not been
established.

| Check | Result |
| --- | --- |
| Dynamic C270 capture-node identification | **PASS** |
| Raw YUY2 320x240 at 30/1 negotiation | **PASS** |
| Raw repeated pipeline lifecycle | **PASS** |
| MJPEG 640x480 at 30/1 negotiation | **PASS** |
| `jpegdec` output as video/x-raw I420 | **PASS** |
| MJPEG repeated pipeline lifecycle | **PASS** |
| EOS and exit status 0 for accepted runs | **PASS** |
| Pipeline NULL transition and cleanup | **PASS** |
| Stable 30-fps throughput | **DEFERRED — STAGE7-USB-01** |
| Long-term USB stability | **DEFERRED — STAGE7-USB-01** |

Stage 7.3 is **COMPLETE — FUNCTIONAL VALIDATION**. It does not establish
stable 30-fps throughput, zero-copy operation, crop support, or long-term USB
reliability. Stage 7.4 is validated separately below; Stage 7 remained
**IN PROGRESS** at the Stage 7.3 checkpoint.

## Stage 7.4 — Reusable C++ GStreamer Pipeline Component

### Implementation and build evidence

Stage 7.4 added a reusable C++17 component built on the native GStreamer C
API and the finite-run `camstream-gst-test` diagnostic CLI. It supports:

```text
YUY2:  v4l2src -> capsfilter(video/x-raw, YUY2) -> fakesink
MJPEG: v4l2src -> capsfilter(image/jpeg) -> jpegdec -> videoconvert -> fakesink
```

The component exposes explicit `build()`, `start()`, `wait()`, and `stop()`
lifecycle operations. It owns the pipeline and bus references, relies on bin
ownership for successfully added elements, releases parsed error data and bus
messages exactly once, transitions to `GST_STATE_NULL` before final release,
and makes cleanup after partial construction deterministic and idempotent. The
bus wait is bounded by the nominal finite-buffer duration plus a project
120-second stall allowance.

The focused application Makefile preserves the project C++17 and warning
policy. The Buildroot generic package uses the target compiler, target make
environment, and staging `pkg-config` metadata. A final incremental image
build passed, and the target root filesystem contained:

```text
/usr/bin/camstream-gst-test
```

The installed binary was inspected as a 32-bit little-endian ARM EABI5
hard-float executable.

The `cpp-quality-reviewer`, `gstreamer-reviewer`, and `bsp-reviewer` approved
the final source/build checkpoint after generated-binary protection, explicit
`videoconvert` dependency coverage, and bounded terminal-wait handling were
reviewed. The pinned Buildroot 2026.02.3 `utils/check-package` execution was
subsequently run with BR2_EXTERNAL mode enabled. It processed 34 lines with 0
warnings and returned exit status 0, so the package check is **PASS**.

### BeagleBone Black runtime evidence

The final image exposed the required `v4l2src`, `jpegdec`, `videoconvert`, and
`fakesink` factories. The CLI rejected invalid format, a missing required
option, and a malformed number with exit status 2. A nonexistent device
produced a controlled construction/state failure with exit status 1.

The synthetic node was identified dynamically and exercised with:

```sh
camstream-gst-test \
    --device "$CAMSTREAM_NODE" \
    --format yuy2 \
    --width 640 --height 480 --fps 30 \
    --buffers 300 --sync false
```

The pipeline reached PLAYING, processed the finite run, received EOS, and
returned exit status 0 in approximately 10.12 seconds. Three consecutive
lifecycle runs completed successfully, after which the synthetic module
unloaded and removed its dynamic node cleanly.

Initial real-camera attempts used `/dev/video1`. Subsequent enumeration showed
that this node exposed Metadata Capture rather than image-capture formats, so
those attempts are invalid and are not acceptance evidence. The corrected
discovery enumerated the actual formats of every candidate node and selected
`/dev/video0` during the tested session because it exposed the C270 YUYV and
MJPG capture formats. `/dev/video0` is an observed session value, not a fixed
device-number ABI.

The corrected C270 runs used:

```sh
camstream-gst-test \
    --device "$C270_NODE" \
    --format yuy2 \
    --width 320 --height 240 --fps 30 \
    --buffers 300 --sync false

camstream-gst-test \
    --device "$C270_NODE" \
    --format mjpeg \
    --width 640 --height 480 --fps 30 \
    --buffers 300 --sync false
```

The YUY2 run reached PLAYING and EOS, returned status 0, and completed in
approximately 16.63 seconds. The MJPEG-decode run also reached PLAYING and
EOS, returned status 0, and completed in approximately 21.62 seconds. One C270
USB reset occurred inside each bounded real-camera test window. Both pipelines
recovered and completed; no kernel Oops, BUG, use-after-free, list corruption,
or fatal GStreamer error was observed.

### Acceptance and limits

| Check | Result |
| --- | --- |
| Reusable native-API C++17 component | **PASS** |
| YUY2 and MJPEG-decode graph construction | **PASS** |
| RAII cleanup and bounded bus wait | **PASS** |
| Buildroot target package build | **PASS** |
| Final incremental Buildroot image build | **PASS** |
| ARM hard-float binary in `/usr/bin` | **PASS** |
| Required target GStreamer factories | **PASS** |
| CLI negative-path exit behavior | **PASS** |
| Synthetic YUY2 finite run | **PASS** |
| Three repeated synthetic lifecycles | **PASS** |
| Synthetic module unload cleanup | **PASS** |
| Initial `/dev/video1` C270 attempts | **NOT TESTED — invalid metadata node; excluded from acceptance** |
| Corrected C270 YUY2 finite run | **PASS — FUNCTIONAL** |
| Corrected C270 MJPEG-decode finite run | **PASS — FUNCTIONAL** |
| Fatal kernel or GStreamer error | **NONE OBSERVED** |
| Buildroot `utils/check-package` | **PASS — 34 lines, 0 warnings, exit status 0** |
| Stable delivered 30 fps | **DEFERRED — STAGE7-USB-01** |
| Long-duration USB reliability | **DEFERRED — STAGE7-USB-01** |

Stage 7.4 is **COMPLETE — FUNCTIONAL VALIDATION**. Requested or negotiated
30/1 caps and finite EOS completion do not establish stable delivered 30 fps.
The bounded runs do not establish long-duration USB reliability, and the USB
resets are not causally attributed to GStreamer. CMake migration remains
outside Stage 7.4. Stage 7.5 final acceptance is recorded separately below.

## Stage 7.5 — Final Acceptance

### Build and package validation

The pinned Buildroot 2026.02.3 package validator was run in BR2_EXTERNAL mode:

```sh
./utils/check-package -b \
  ~/TungNHS/camstream-control/br2-external/package/camstream-gst-test/Config.in \
  ~/TungNHS/camstream-control/br2-external/package/camstream-gst-test/camstream-gst-test.mk
```

It processed 34 lines, generated 0 warnings, and returned exit status 0. The
`camstream-gst-test` package check is therefore **PASS**.

A final incremental build used Buildroot 2026.02.3, the existing
`stage06-camera-v4l2` output directory, and the project `CAMSTREAM`
BR2_EXTERNAL tree. No `make clean` was used. The build finalized the target,
regenerated the root filesystems, and generated `sdcard.img` successfully.

The first artifact scan found an absolute kernel-build path in the synthetic
module's compiled VB2 diagnostic string. The package build now passes a scoped
`-fmacro-prefix-map` through Kbuild. Pinned `check-package` then processed the
changed `camstream-video.mk` file with 0 warnings, the module package rebuild
passed, and a second incremental image build completed successfully. This
change affects compile-time path provenance, not driver behavior; no new BBB
runtime result is claimed for it.

The pinned validator command for that package file processed 14 lines with 0
warnings and returned exit status 0:

```sh
./utils/check-package -b \
  ~/TungNHS/camstream-control/br2-external/package/camstream-video/camstream-video.mk
```

### Final target and image artifacts

The final target tree and generated `rootfs.tar` contain:

```text
/usr/bin/camstream-capture
/usr/bin/camstream-gst-test
/lib/modules/6.18.1/updates/camstream_video.ko
/usr/lib/gstreamer-1.0/libgstcoreelements.so
/usr/lib/gstreamer-1.0/libgstvideo4linux2.so
/usr/lib/gstreamer-1.0/libgstjpeg.so
/usr/lib/gstreamer-1.0/libgstvideoconvertscale.so
```

The required GStreamer, base, and video runtime libraries are also present.
Both CamStream applications are 32-bit little-endian ARM EABI5 executables,
use the hard-float loader, and report VFP register arguments. The module and
inspected GStreamer libraries/plugins are ARM EABI5 objects. Neither
application has an RPATH or RUNPATH entry.

No project host-build path was found in the Stage 7 acceptance artifact set:
the two CamStream applications, `camstream_video.ko`, the required GStreamer
runtime libraries, or the four required plugins. This is a focused Stage 7
artifact check, not a claim that every third-party file in the complete rootfs
has been audited or remediated.

### Acceptance matrix

| Check | Result |
| --- | --- |
| Pinned Buildroot 2026.02.3 identity | **PASS** |
| `camstream-gst-test` `check-package -b` | **PASS — 34 lines, 0 warnings, exit status 0** |
| Final incremental image build without `make clean` | **PASS** |
| Both CamStream applications in target and rootfs image | **PASS** |
| Synthetic module in target and rootfs image | **PASS** |
| Required GStreamer runtime libraries and plugins | **PASS** |
| ARM EABI5 hard-float application architecture | **PASS** |
| Host path absent from focused Stage 7 artifact set | **PASS** |
| Existing accepted BBB runtime evidence retained | **PASS** |
| New BBB runtime test during Stage 7.5 | **NOT TESTED — not required; prior evidence retained** |
| Stable delivered production 30 fps | **DEFERRED — STAGE7-USB-01** |
| Long-duration USB reliability | **DEFERRED — STAGE7-USB-01** |
| USB reset attribution to GStreamer | **DEFERRED — no causal evidence** |
| Production-ready camera reliability | **DEFERRED — STAGE7-USB-01** |

Stage 7.5 is **COMPLETE — FINAL ACCEPTANCE**. Stages 7.1 through 7.5 are
complete, so Stage 7 is **COMPLETE**. The open USB reliability issue does not
invalidate the accepted functional integration, but it blocks production
30-fps and long-duration reliability claims.

## Deferred platform issue — C270 USB stability and throughput

### STAGE7-USB-01

- Status: **OPEN / DEFERRED**
- Stage 7 functional integration impact: **NON-BLOCKING**
- Production-ready 30-fps or long-duration reliability claim: **BLOCKING**

### Confirmed observations

- Repeated messages reported `reset high-speed USB device ... using
  musb-hdrc`.
- Resets occurred with the C270 and WiFi adapter connected through a USB hub
  and with the C270 connected directly to the BBB.
- One earlier `uvcvideo` message reported `Failed to resubmit video URB (-1)`.
- Throughput was inconsistent despite negotiated or configured 30/1 fps and
  was often approximately 19–20 buffers per second.
- One direct raw GStreamer run reached approximately 29.1 buffers per second.

USB resets and inconsistent throughput are confirmed observations. A causal
relationship between every reset and reduced throughput has not been proven.
No kernel Oops, BUG, use-after-free, list corruption, or fatal GStreamer error
was observed during the accepted functional runs; pipelines recovered or
completed with EOS and exit status 0.

### Future investigation checklist

1. Retest with a known-good regulated BBB power supply.
2. Check USB VBUS stability and current under camera load.
3. Retest with a known-good USB cable and another UVC camera.
4. Compare direct USB and externally powered hub topologies.
5. Check C270 exposure controls, including auto-exposure priority, because
   camera exposure may affect delivered frame rate.
6. Capture bounded kernel logs around each test using BEGIN/END markers.
7. Compare native `v4l2-ctl` and GStreamer using identical modes.
8. Investigate `musb-hdrc` and `uvcvideo` behavior, including possible kernel
   version or driver-specific issues.
9. Use USB tracing or usbmon if available in a dedicated diagnostic build.

Do not perform this investigation as part of the Stage 7.3 documentation
closure.

### Closure criteria

STAGE7-USB-01 may be closed only when:

- no USB reset occurs during a representative long-duration test;
- no fatal UVC or URB error occurs;
- repeated 300-buffer runs produce consistent throughput;
- the required production mode sustains the agreed frame rate; and
- results are reproduced across at least two runs.
