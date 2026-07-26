# Stage 6C — Synthetic V4L2 Capture Driver

## Checkpoint status

- Validation date: 2026-07-26
- Branch: `stage/06c-synthetic-v4l2-driver`
- Stage 6C.1: **COMPLETE**
- Stage 6C.2 source and build validation: **PASS**
- Stage 6C.2 BeagleBone Black runtime: **PENDING — NOT TESTED**
- Stage 6C: **IN PROGRESS**

This checkpoint introduces only the registration skeleton. It does not claim
format negotiation, buffer allocation, streaming, frame generation, or
compatibility with `camstream-capture` beyond future architectural intent.

## Implementation boundary

The project-owned `camstream_video` module registers one dynamically numbered
V4L2 capture node. It supports open, close, ioctl dispatch, and
`VIDIOC_QUERYCAP`. The driver advertises `V4L2_CAP_VIDEO_CAPTURE`; it does not
advertise `V4L2_CAP_STREAMING`.

The private device state owns the `v4l2_device`, allocated `video_device`, and
mutex. Initialization registers those resources in forward order. Failure and
module-exit paths unwind them in reverse order.

Buildroot integrates the source as the local `camstream-video` package using
the standard `kernel-module` infrastructure. The package is enabled in the
project BeagleBone defconfig. No upstream Linux source was modified and no
kernel configuration fragment change was required: the current Linux 6.18.1
build already enables module and V4L2 core support.

## Build evidence

The module was built from the Stage 6C.1 working tree with the Buildroot
`camstream-video` package target against the project's Linux 6.18.1 output.
The unstripped build artifact is:

```text
camstream-workspace/output/stage06-camera-v4l2/build/
  camstream-video-1.0/camstream_video.ko
```

The Buildroot cross-compiler was GCC 14.3.0. `file`, `readelf`, and `modinfo`
identified the result as a 32-bit little-endian ARM EABI5 relocatable module
with `vermagic` for Linux 6.18.1. `modinfo` reports a dependency on `videodev`.
The `W=1` build completed without a warning attributable to the module. The
package installed the target copy at
`/lib/modules/6.18.1/updates/camstream_video.ko` in the Buildroot target tree.
Kernel `checkpatch.pl` reported zero errors and zero warnings for both the C
source and driver Makefile. `scripts/kernel-doc -none` parsed the source
without an error or warning. Buildroot `check-package` was not run successfully
because the installed host Python environment lacks the `magic` module; no
dependency was installed.

## BeagleBone Black runtime evidence

The `camstream_video` module loaded successfully. Its observed `lsmod` state
was:

```text
camstream_video 12288 ... Live
```

During this test the kernel dynamically assigned `video2` and reported:

```text
camstream-video: registered as video2
```

`video2` records this test instance; it is not a fixed ABI or hard-coded node
number. Userspace identified the assigned node through its driver identity.

`VIDIOC_QUERYCAP` reported:

| Field | Observed value |
| --- | --- |
| Driver name | `camstream-video` |
| Card type | `CamStream synthetic video` |
| Bus info | `platform:camstream-video` |
| Driver version | `6.18.1` |
| Capabilities | Video Capture, Extended Pix Format, Device Capabilities |
| Device capabilities | Video Capture, Extended Pix Format |

`V4L2_CAP_STREAMING` was not reported and remains intentionally absent because
Stage 6C.1 does not implement streaming.

`rmmod camstream_video` returned exit code 0. The kernel reported:

```text
camstream-video: unregistering video2
```

After unloading, `camstream_video` was absent from `lsmod` and `/dev/video2`
was removed. The Logitech C270 remained visible through `v4l2-ctl` with
`/dev/video0`, `/dev/video1`, and `/dev/media0`. No kernel WARNING, Oops, or BUG
was observed during the module lifecycle. This demonstrates Stage 6C.1
coexistence and clean synthetic-node removal; it does not establish broader
USB or system stability.

USB reset events involving the Logitech C270 were also observed during the
runtime session. Similar behavior was already known from Stages 6A and 6B.
This checkpoint does not establish the synthetic V4L2 capture driver as the
cause. The USB-reset root cause remains **DEFERRED**.

## Acceptance matrix

| Requirement | Result |
| --- | --- |
| Source implementation | **PASS** |
| Linux kernel coding style | **PASS** |
| `checkpatch.pl` | **PASS** |
| Kernel-doc validation | **PASS** |
| `W=1` module build | **PASS** |
| Buildroot module integration | **PASS** |
| ARM/Linux 6.18.1 module | **PASS** |
| Module load | **PASS** |
| Dynamic `/dev/videoX` registration | **PASS** |
| `VIDIOC_QUERYCAP` | **PASS** |
| C270 coexistence | **PASS** |
| Module unload | **PASS** |
| Synthetic video-node removal | **PASS** |
| Kernel WARNING/Oops/BUG | **NONE OBSERVED** |

## Reproduction commands

Run on the BeagleBone Black after installing an image containing the package:

```sh
uname -r
find "/lib/modules/$(uname -r)" -name '*camstream*' -print
modprobe camstream_video
dmesg | tail -n 40
ls -l /dev/video*
v4l2-ctl --list-devices
```

Identify the dynamically allocated node by its `camstream-video` identity,
then substitute it below; do not assume a fixed number:

```sh
v4l2-ctl --device /dev/videoX --info
rmmod camstream_video
ls -l /dev/video*
dmesg | tail -n 40
```

Stage 6C.1 satisfies its acceptance criteria and is **COMPLETE**. At that
checkpoint boundary, format negotiation, buffer handling, streaming, and
synthetic frame generation remained assigned to later checkpoints.

## Stage 6C.2 — Fixed Format Negotiation

### Implementation status

- Source implementation: **PASS**
- Linux kernel coding style: **PASS**
- `checkpatch.pl`: **PASS**
- Kernel-doc validation: **PASS**
- Linux 6.18.1 module build with `W=1`: **PASS**
- Buildroot package rebuild: **PASS**
- BeagleBone Black runtime: **PENDING — NOT TESTED**
- Stage 6C.2: **IN PROGRESS**

Stage 6C.2 adds only a deterministic single-planar format and frame-interval
contract. It does not add a queue, exchange image data, or pace frames.

### Effective capture contract

| Property | Effective value |
| --- | --- |
| Buffer type | `V4L2_BUF_TYPE_VIDEO_CAPTURE` |
| Pixel format | `V4L2_PIX_FMT_YUYV` |
| Resolution | 640x480 |
| Field | `V4L2_FIELD_NONE` |
| Bytes per line | 1280 |
| Image size | 614400 bytes |
| Frame interval | 1/30 second |
| Effective frame rate | 30 fps |
| Colorspace | `V4L2_COLORSPACE_SRGB` |

The format helper normalizes every supported request to this contract and
sets default YCbCr encoding, quantization, and transfer-function metadata.
`VIDIOC_TRY_FMT` returns the effective format without modifying active state.
`VIDIOC_S_FMT` stores the normalized result, and `VIDIOC_G_FMT` returns that
active format.

Format enumeration exposes one `YUYV 4:2:2` entry, one discrete 640x480 frame
size, and one discrete 1/30-second frame interval. Unsupported indices,
formats, sizes, intervals, and buffer types return `-EINVAL`.

`VIDIOC_G_PARM` and `VIDIOC_S_PARM` expose `V4L2_CAP_TIMEPERFRAME` and normalize
the frame interval to 1/30 second. This is negotiation metadata only; actual
30-fps frame production remains outside Stage 6C.2.

The ioctl table contains only `VIDIOC_QUERYCAP`, format/size/interval
enumeration, `TRY_FMT`, `G_FMT`, `S_FMT`, `G_PARM`, and `S_PARM`. The device
still advertises `V4L2_CAP_VIDEO_CAPTURE` without `V4L2_CAP_STREAMING` or
`V4L2_CAP_READWRITE`.

### Build evidence

The existing Buildroot `camstream-video` package was rebuilt from a clean
package directory against Linux 6.18.1 with `W=1`. No warning attributable to
`camstream_video` was emitted. Kernel `checkpatch.pl` reported zero errors and
zero warnings, and `scripts/kernel-doc -none` parsed the source without an
error or warning.

The rebuilt `camstream_video.ko` remains a 32-bit little-endian ARM EABI5
module with Linux 6.18.1 `vermagic` and a `videodev` dependency. Buildroot
installed it under `/lib/modules/6.18.1/updates/` in the target tree. No full
image rebuild was performed.

### BeagleBone Black validation commands

Confirm the target `v4l2-ctl` syntax, load the module, and identify the node by
driver identity rather than by a fixed video number:

```sh
v4l2-ctl --help-vidcap 2>/dev/null || v4l2-ctl --help
uname -r
find "/lib/modules/$(uname -r)" -name '*camstream*' -print
modprobe camstream_video

CAMSTREAM_NODE=
for node in /dev/video*; do
    if v4l2-ctl -d "$node" --info 2>/dev/null |
       grep -q 'Driver name.*camstream-video'; then
        CAMSTREAM_NODE="$node"
        break
    fi
done
test -n "$CAMSTREAM_NODE" || {
    echo "CamStream video node not found"
    exit 1
}
echo "CamStream node: $CAMSTREAM_NODE"
```

Exercise enumeration, active-format reporting, normalization, and fixed frame
parameters:

```sh
v4l2-ctl -d "$CAMSTREAM_NODE" --info
v4l2-ctl -d "$CAMSTREAM_NODE" --list-formats-ext
v4l2-ctl -d "$CAMSTREAM_NODE" --get-fmt-video

v4l2-ctl -d "$CAMSTREAM_NODE" \
    --set-fmt-video=width=640,height=480,pixelformat=YUYV
v4l2-ctl -d "$CAMSTREAM_NODE" --get-fmt-video

v4l2-ctl -d "$CAMSTREAM_NODE" \
    --try-fmt-video=width=1920,height=1080,pixelformat=MJPG
v4l2-ctl -d "$CAMSTREAM_NODE" --get-fmt-video

v4l2-ctl -d "$CAMSTREAM_NODE" --get-parm
v4l2-ctl -d "$CAMSTREAM_NODE" --set-parm=15
v4l2-ctl -d "$CAMSTREAM_NODE" --get-parm

dmesg | tail -n 50
rmmod camstream_video
dmesg | tail -n 50
```

Expected effective results are YUYV, 640x480, field none, 1280 bytes per line,
614400-byte images, and 30 fps even after unsupported format or frame-rate
requests. Runtime results remain **PENDING** until this is executed on the BBB.

The Stage 6B `camstream-capture` application currently requires
`V4L2_CAP_STREAMING` at its capability gate. It is therefore not a Stage 6C.2
acceptance command and is expected to reject this non-streaming node before
buffer setup. The application is unchanged; integration with its streaming
path belongs to later Stage 6C checkpoints.
