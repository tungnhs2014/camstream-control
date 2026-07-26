# Stage 6C.1 — Synthetic V4L2 Capture Driver Skeleton

## Checkpoint status

- Validation date: 2026-07-26
- Branch: `stage/06c-synthetic-v4l2-driver`
- Source implementation: **PASS**
- Linux 6.18.1 module build with `W=1`: **PASS**
- BeagleBone Black runtime: **PASS**
- Stage 6C.1: **COMPLETE**
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

Stage 6C.1 satisfies its acceptance criteria and is **COMPLETE**. Stage 6C
remains **IN PROGRESS**; format negotiation, buffer handling, streaming, and
synthetic frame generation belong to later checkpoints.
