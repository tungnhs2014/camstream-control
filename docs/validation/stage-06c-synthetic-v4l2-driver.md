# Stage 6C — Synthetic V4L2 Capture Driver

## Checkpoint status

- Validation date: 2026-07-26
- Branch: `stage/06c-synthetic-v4l2-driver`
- Stage 6C.1: **COMPLETE**
- Stage 6C.2 source and build validation: **PASS**
- Stage 6C.2 BeagleBone Black runtime: **PASS**
- Stage 6C.2: **COMPLETE**
- Stage 6C.3 source and build validation: **PASS**
- Stage 6C.3 BeagleBone Black runtime validation: **PASS**
- Stage 6C.3: **COMPLETE**
- Stage 6C.4 source and build validation: **PASS**
- Stage 6C.4 BeagleBone Black functional runtime validation: **PASS**
- Stage 6C.4 pacing validation: **PASS**
- Stage 6C.4: **COMPLETE**
- Stage 6C.5 final Buildroot/runtime integration: **PASS**
- Stage 6C.5: **COMPLETE**
- Stage 6C: **COMPLETE**

The completed checkpoints provide device registration, a deterministic
single-planar format-negotiation contract, and a validated VB2 MMAP streaming
lifecycle. The Stage 6C.4 working tree adds synthetic frame production, but
later Stage 6C checkpoints remain outside this validated boundary.

## Implementation boundary

The project-owned `camstream_video` module registers one dynamically numbered
V4L2 capture node. It supports open, close, ioctl dispatch, `VIDIOC_QUERYCAP`,
fixed-format enumeration and negotiation, and fixed capture-parameter
negotiation. It also supports an MMAP-backed VB2 queue and start/stop
lifecycle, and the Stage 6C.4 working tree can produce synthetic YUYV buffers.
It advertises `V4L2_CAP_VIDEO_CAPTURE` and `V4L2_CAP_STREAMING`; functional
target frame delivery, visual output, corrected pacing, and unload cleanup have
been validated.

The private device state owns the `v4l2_device`, allocated `video_device`, and
mutex, plus the VB2 queue and protected driver-owned buffer list.
The Stage 6C.4 state also owns one delayed-work producer, its stream state, the
next sequence number, and an absolute next-frame deadline. Initialization
registers resources in forward order. Failure and module-exit paths unwind only
initialized resources through the corresponding VB2/V4L2 and synchronous
work-cancellation paths.

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
- BeagleBone Black runtime: **PASS**
- Stage 6C.2: **COMPLETE**

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

### BeagleBone Black runtime evidence

The synthetic node was dynamically assigned `/dev/video2` during this test.
That node number is observed evidence for this session, not a fixed ABI.

`VIDIOC_QUERYCAP` passed with `V4L2_CAP_VIDEO_CAPTURE` present and
`V4L2_CAP_STREAMING` absent, matching the Stage 6C.2 implementation boundary.
The runtime format results were:

| Operation | Observed result |
| --- | --- |
| `VIDIOC_ENUM_FMT` | YUYV |
| `VIDIOC_ENUM_FRAMESIZES` | Discrete 640x480 |
| `VIDIOC_ENUM_FRAMEINTERVALS` | Discrete 30 fps |
| `VIDIOC_G_FMT` width/height | 640x480 |
| `VIDIOC_G_FMT` pixel format | YUYV |
| `VIDIOC_G_FMT` field | None |
| `VIDIOC_G_FMT` bytes per line | 1280 |
| `VIDIOC_G_FMT` image size | 614400 bytes |
| `VIDIOC_G_FMT` colorspace | sRGB |
| `VIDIOC_S_FMT` 640x480 YUYV | **PASS** |
| `VIDIOC_G_PARM` | 30 fps |

A `VIDIOC_TRY_FMT` request for 1920x1080 YUYV was normalized to 640x480
YUYV. A subsequent `VIDIOC_G_FMT` confirmed that TRY_FMT did not alter the
active format. A `VIDIOC_S_PARM` request for 15 fps was normalized to the only
supported rate, 30 fps.

REQBUFS and MMAP streaming remained unsupported, as expected for Stage 6C.2.
No streaming behavior or frame delivery is claimed.

`rmmod camstream_video` succeeded and the synthetic node disappeared. The C270
nodes `/dev/video0` and `/dev/video1` remained present. No kernel WARNING,
Oops, or BUG was observed.

USB reset events remain a known, deferred observation. This validation does
not establish the synthetic V4L2 capture driver as their cause.

### Stage 6C.2 acceptance

| Requirement | Result |
| --- | --- |
| Source and build validation | **PASS** |
| `VIDIOC_QUERYCAP` capability boundary | **PASS** |
| Format, size, and interval enumeration | **PASS** |
| `TRY_FMT`, `S_FMT`, and `G_FMT` | **PASS** |
| `G_PARM` and `S_PARM` | **PASS** |
| Clean unload and node removal | **PASS** |
| C270 coexistence | **PASS** |
| Kernel WARNING/Oops/BUG | **NONE OBSERVED** |

Stage 6C.2 is **COMPLETE**. At that checkpoint boundary, Stage 6C remained
**IN PROGRESS**.

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
    --try-fmt-video=width=1920,height=1080,pixelformat=YUYV
v4l2-ctl -d "$CAMSTREAM_NODE" --get-fmt-video

v4l2-ctl -d "$CAMSTREAM_NODE" --get-parm
v4l2-ctl -d "$CAMSTREAM_NODE" --set-parm=15
v4l2-ctl -d "$CAMSTREAM_NODE" --get-parm

dmesg | tail -n 50
rmmod camstream_video
dmesg | tail -n 50
```

The observed effective results were YUYV, 640x480, field none, 1280 bytes per
line, 614400-byte images, and 30 fps after unsupported size or frame-rate
requests.

The Stage 6B `camstream-capture` application currently requires
`V4L2_CAP_STREAMING` at its capability gate. It is therefore not a Stage 6C.2
acceptance command and is expected to reject this non-streaming node before
buffer setup. The application is unchanged; integration with its streaming
path belongs to later Stage 6C checkpoints.

## Stage 6C.3 — VB2 MMAP Streaming Lifecycle

### Implementation status

- Source implementation: **PASS**
- Linux kernel coding style: **PASS**
- `checkpatch.pl`: **PASS**
- Kernel-doc validation: **PASS**
- Linux 6.18.1 module build with `W=1`: **PASS**
- Buildroot package rebuild: **PASS**
- BeagleBone Black runtime validation: **PASS**
- Stage 6C.3: **COMPLETE**
- Stage 6C after 6C.3: **IN PROGRESS**

Stage 6C.3 adds buffer allocation, mapping, queue ownership, and start/stop
lifecycle only. It deliberately produces no payload and never completes a
buffer successfully. A finite userspace poll timeout is therefore the expected
checkpoint boundary, not a frame-capture failure.

### VB2 capture contract

The driver initializes one single-planar `V4L2_BUF_TYPE_VIDEO_CAPTURE` queue
with `VB2_MMAP` as its only I/O mode and `vb2_vmalloc_memops` as the allocation
backend. Each VB2 allocation embeds the project buffer wrapper; no separate
bookkeeping allocation is required. The existing device mutex is the queue and
ioctl lock, while a spinlock protects only the driver-owned queued-buffer list.

`queue_setup` derives its one-plane allocation size from the active format's
`sizeimage` value. For the fixed Stage 6C.2 contract this is 614400 bytes. An
existing requested layout is accepted only when it has exactly one plane large
enough for that payload. `buf_prepare` enforces the same bound and sets a zero
payload because no frame is produced in this checkpoint.

After `QBUF`, a buffer remains on the protected driver list until stream
teardown. `STREAMON` changes queue state but starts no timer, thread, workqueue,
or hardware. `STREAMOFF` removes each driver-owned buffer from the list before
returning it to VB2 with `VB2_BUF_STATE_ERROR`; the driver does not access it
after ownership is relinquished. This also provides deterministic close and
unload cleanup through the standard VB2 release path.

The node now truthfully advertises `V4L2_CAP_VIDEO_CAPTURE` and
`V4L2_CAP_STREAMING`, without `V4L2_CAP_READWRITE`. Standard VB2 helpers handle
`REQBUFS`, `QUERYBUF`, `QBUF`, `DQBUF`, `STREAMON`, `STREAMOFF`, `poll`, `mmap`,
and release. `S_FMT` returns `-EBUSY` while VB2 buffers exist so the active
`sizeimage` cannot change underneath an allocated queue.

### Source and build evidence

The driver was split into a small registration/ioctl translation unit, a VB2
translation unit, and a shared private header. Kbuild still emits the existing
`camstream_video.ko` module. The Linux 6.18.1 external-module build completed
with `W=1` and no driver warning. Kernel `checkpatch.pl` reported zero errors
and zero warnings for each changed driver source/Makefile, and
`scripts/kernel-doc -none` completed without an error or warning.

The existing Buildroot `camstream-video` package was rebuilt using
`camstream-video-dirclean` followed by `camstream-video`; no package redesign
or full image rebuild was performed. The unstripped module is a 32-bit
little-endian ARM EABI5 object with Linux 6.18.1 `vermagic`. Buildroot staged
the module under `/lib/modules/6.18.1/updates/camstream_video.ko`.

### BeagleBone Black runtime evidence

The Stage 6C.3 implementation from commit `509ea60` was exercised on the
BeagleBone Black. The synthetic node was dynamically assigned `/dev/video2`
during this test. That node number is observed evidence for this session only;
it is not a fixed device number or ABI.

`VIDIOC_QUERYCAP` reported Video Capture, Streaming, and Device Capabilities.
The Stage 6C.2 format contract also passed regression testing:

| Property | Observed value |
| --- | --- |
| Pixel format | YUYV |
| Resolution | 640x480 |
| Frame rate negotiation | 30 fps |
| Bytes per line | 1280 |
| Image size | 614400 bytes |

The Stage 6B `camstream-capture` application requested four buffers and the
driver granted four. `QUERYBUF` reported a 614400-byte length for every
buffer. All four buffers mapped successfully, all four `QBUF` operations
passed, and `STREAMON` passed.

Because Stage 6C.3 intentionally has no frame producer, `poll` timed out after
2000 ms. The timeout and application exit status 1 are expected checkpoint
results; no successful `DQBUF` is claimed. `STREAMOFF` and MMAP cleanup passed.

A second complete lifecycle run again reached `STREAMON` and the expected poll
timeout. Cleanup passed and no stale queue or driver-list state was observed.
This repeated result validates queue reuse and teardown at the deliberate
no-frame boundary; it does not validate frame production.

`rmmod camstream_video` returned 0 and the synthetic node disappeared. The C270
nodes `/dev/video0` and `/dev/video1` remained, and `/dev/media0` remained
available. No kernel WARNING, Oops, BUG, list corruption, or use-after-free was
observed.

USB reset events involving the C270 were observed and remain
**KNOWN / DEFERRED**. This evidence does not attribute them to
`camstream_video`; no root-cause claim is made.

### Acceptance boundary

| Requirement | Result |
| --- | --- |
| Source implementation | **PASS** |
| `checkpatch.pl` and kernel-doc | **PASS** |
| Linux 6.18.1 `W=1` build | **PASS** |
| Buildroot package rebuild | **PASS** |
| Streaming capability | **PASS** |
| Stage 6C.2 format regression | **PASS** |
| `REQBUFS` | **PASS** |
| `QUERYBUF` | **PASS** |
| MMAP | **PASS** |
| `QBUF` | **PASS** |
| `STREAMON` | **PASS** |
| Poll timeout | **EXPECTED** |
| Successful DQBUF | **NOT IMPLEMENTED — STAGE 6C.4** |
| `STREAMOFF` | **PASS** |
| MMAP cleanup | **PASS** |
| Repeat lifecycle | **PASS** |
| Expected application exit status 1 | **PASS** |
| Module unload | **PASS** |
| Synthetic-node removal | **PASS** |
| C270 coexistence | **PASS** |
| Kernel WARNING/Oops/BUG | **NONE OBSERVED** |

Stage 6C.3 is **COMPLETE**. At that checkpoint boundary, Stage 6C remained
**IN PROGRESS**.

### BeagleBone Black validation commands

Copy only the rebuilt module from the development host, replacing the target
address with the current BBB address:

```sh
MODULE="$HOME/TungNHS/camstream-workspace/output/stage06-camera-v4l2/build/camstream-video-1.0/camstream_video.ko"
BBB_IP='replace-with-bbb-ip'
scp "$MODULE" "root@${BBB_IP}:/tmp/camstream_video.ko"
```

On the target, load the module and discover the node by driver identity rather
than assuming `/dev/video2`:

```sh
uname -r
insmod /tmp/camstream_video.ko

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

v4l2-ctl -d "$CAMSTREAM_NODE" --info
v4l2-ctl -d "$CAMSTREAM_NODE" --list-formats-ext
v4l2-ctl -d "$CAMSTREAM_NODE" --get-fmt-video
v4l2-ctl -d "$CAMSTREAM_NODE" --get-parm
```

Run the existing Stage 6B boundary test twice. Each invocation must reach
MMAP/QBUF/STREAMON, time out waiting for a completed frame, then exit non-zero
without hanging while still performing STREAMOFF, unmap, `REQBUFS(count=0)`,
and close cleanup:

```sh
run=1
while [ "$run" -le 2 ]; do
    /usr/bin/camstream-capture \
        --device "$CAMSTREAM_NODE" \
        --format YUYV \
        --width 640 \
        --height 480 \
        --fps 30 \
        --count 1
    status=$?
    echo "Boundary run $run exit status: $status"
    test "$status" -ne 0 || {
        echo "Unexpected completed frame in Stage 6C.3"
        exit 1
    }
    run=$((run + 1))
done
```

Finally inspect kernel health and coexistence, unload the module, and verify
that only the dynamically identified synthetic node disappears:

```sh
v4l2-ctl --list-devices
dmesg | tail -n 100
rmmod camstream_video
test ! -e "$CAMSTREAM_NODE"
v4l2-ctl --list-devices
dmesg | tail -n 100
```

Expected Stage 6C.3 behavior is: capability, enumeration, format/parameter
negotiation, REQBUFS, QUERYBUF, MMAP, QBUF, and STREAMON pass; `poll` times out;
STREAMOFF and all subsequent cleanup pass. DQBUF success, YUYV payload,
sequence/timestamp completion, and paced frame production were assigned to
Stage 6C.4. The recorded BBB evidence above directly validated this Stage 6C.3
boundary twice before those later behaviors were implemented.

## Stage 6C.4 — Synthetic Frame Generation

### Implementation status

- Source implementation: **PASS**
- Linux kernel coding style: **PASS**
- `checkpatch.pl`: **PASS**
- Kernel-doc validation: **PASS**
- Linux 6.18.1 module build with `W=1`: **PASS**
- Buildroot package rebuild: **PASS**
- BeagleBone Black functional runtime validation: **PASS**
- BeagleBone Black pacing validation: **PASS**
- Stage 6C.4: **COMPLETE**
- Stage 6C after 6C.4: **IN PROGRESS**

Stage 6C.4 adds one delayed-work producer. It uses the existing driver-owned
buffer list and does not allocate per-frame storage. `STREAMON` resets the
sequence to zero, marks the producer active, and schedules asynchronous work.
The callback executes in process context and handles at most one buffer per
invocation.

The advertised V4L2 interval remains 1/30 second. The target kernel uses
`CONFIG_HZ=100`, so one jiffy is 10 ms and a 33.333-ms interval cannot be
represented by one fixed integer-jiffy delay. The corrected producer keeps an
absolute nanosecond deadline, rounds each remaining delay up to the next
jiffy, and compensates against the following deadline. Its expected steady
schedule therefore uses quantized 3/4-jiffy timing as a best-effort
approximation; it is not a real-time guarantee or an exact 30.000-fps claim.

The first implementation re-armed a full four-jiffy delay only after filling
and completing each frame. This made each period equal to frame-generation
execution time plus at least 40 ms. It also recalculated the same luma gradient
independently for all 480 identical rows. Supplied BBB timestamps measured
only about 10–11 fps. The correction generates the 1280-byte first scanline
once, replicates it with `memcpy()`, and advances an absolute deadline after
each invocation. If work misses a deadline, arithmetic advances directly to
a future period; it neither busy-loops nor emits catch-up frames.

### Pattern and completion contract

The producer validates that the active format is packed YUYV with an even
width and that `bytesperline * height` equals `sizeimage`. It also rechecks the
mapped plane size and virtual address before writing.

Each 640x480 frame is a deterministic moving horizontal-luma gradient rendered
as vertical grayscale structure. Every YUYV pair contains `Y0, U, Y1, V` with
neutral U and V values of 128. The gradient shifts by eight pixels per
successful sequence, so consecutive frames are visibly different without
floating-point code or color conversion. The first scanline is generated
directly in the VB2 plane and copied to the remaining rows. This covers exactly
the active 614400-byte payload and performs no dynamic allocation.

For a valid frame, the driver sets plane payload to `sizeimage`, field to
`V4L2_FIELD_NONE`, the next zero-based sequence number, and a monotonic
`ktime_get_ns()` timestamp before returning the buffer with
`VB2_BUF_STATE_DONE`. Sequence advances only for a committed successful frame
and resets on every new `STREAMON`. An inaccessible or invalid plane is
returned once with zero payload and `VB2_BUF_STATE_ERROR`.

### Ownership and teardown

The worker takes at most one buffer from the protected list and removes it
before releasing the spinlock. It fills the 614400-byte payload outside the
spinlock. Once `vb2_buffer_done()` returns ownership to VB2, the driver never
accesses that buffer again. A later userspace DQBUF/QBUF cycle transfers the
same buffer back to the driver for reuse.

Every producer scheduling decision is made while holding the same spinlock
that protects `streaming`. `STREAMOFF` first marks streaming inactive under
that lock, then calls `cancel_delayed_work_sync()` without holding the lock.
This prevents re-arming after shutdown and waits for any worker that already
removed a buffer. If shutdown wins before frame commit, that in-flight buffer
is returned with `VB2_BUF_STATE_ERROR`. After cancellation, the existing VB2
stop path returns all buffers still on the list with the error state.

### Source and build evidence

The focused producer is implemented in `camstream_frame.c`; the existing
registration, format, and VB2 responsibilities remain in their Stage 6C.3
translation units. Kbuild continues to emit `camstream_video.ko`.

Kernel `checkpatch.pl` reported zero errors and zero warnings across every
driver source and Makefile. `scripts/kernel-doc -none` completed without an
error or warning. The Linux 6.18.1 external-module build completed with `W=1`
and no CamStream warning.

The existing Buildroot package was rebuilt with `camstream-video-dirclean`
followed by `camstream-video`; no full image was rebuilt. The resulting module
remains a 32-bit little-endian ARM EABI5 object with Linux 6.18.1 `vermagic`
and is staged under `/lib/modules/6.18.1/updates/camstream_video.ko`.

### BBB functional evidence and pacing finding

During the validated session, the kernel dynamically allocated `/dev/video2`
to `camstream-video`; that node number is evidence from one boot, not a fixed
ABI. The node reported Video Capture, Streaming, Extended Pix Format, and
Device Capabilities. Its Stage 6C.2 contract remained YUYV 640x480 at an
advertised 30 fps, with `bytesperline=1280` and `sizeimage=614400`.

The complete VB2 path passed on the BBB: REQBUFS, QUERYBUF, MMAP, QBUF,
STREAMON, poll wake, DQBUF, re-QBUF, STREAMOFF, and MMAP cleanup.

Before the pacing correction, two target runs each captured 30 valid frames
with zero error frames and exit status 0. Both exercised REQBUFS, MMAP, QBUF,
STREAMON, poll, DQBUF, buffer reuse in the order 0, 1, 2, 3, increasing
sequences 0 through 29, STREAMOFF, and MMAP cleanup. Every payload was 614400
bytes for YUYV 640x480 with `bytesperline=1280` and `sizeimage=614400`.

The saved 614400-byte YUYV frame passed visual validation: it showed the full
dark-to-light grayscale gradient expected from sequence-shifted luma with
U=V=128, with no visible truncation or corruption.

Timing did not pass. The retained first/last completion timestamps were:

| Run | Sequence 0 | Sequence 29 | Result |
| --- | --- | --- | --- |
| 1 | 7145.808820 | 7148.457623 | approximately 10–11 fps |
| 2 | 7152.488194 | 7155.312733 | approximately 10–11 fps |

This proved the original producer did not deliver its advertised rate, so
Stage 6C.4 remained **IN PROGRESS** until the corrected implementation was
revalidated.

### Corrected pacing evidence

Two corrected-build captures each delivered 60 continuous frames with
sequences 0 through 59, giving 59 measured intervals per run:

| Run | Elapsed | Average interval | Measured rate | Result |
| --- | ---: | ---: | ---: | --- |
| 1 | 1.971701 s | 33.419 ms/frame | 29.923 fps | **PASS** |
| 2 | 1.971070 s | 33.408 ms/frame | 29.933 fps | **PASS** |

The corrected producer therefore measured approximately 29.93 fps against
the advertised 30-fps contract on `CONFIG_HZ=100`. This is a pacing **PASS**,
not a claim of exact 30.000-fps scheduling.

### Cleanup and kernel-log evidence

`rmmod camstream_video` returned status 0. The dynamically allocated synthetic
`/dev/video2` node disappeared, while the C270 nodes `/dev/video0`,
`/dev/video1`, and `/dev/media0` remained available. The kernel logged
`camstream-video: unregistering video2`; no CamStream-related WARNING, Oops,
BUG, use-after-free, list corruption, or workqueue warning was observed.

The messages `hw-breakpoint: debug architecture 0x4 unsupported`, `mtdoops:
mtd device (...) must be supplied`, and `debugfs: '49000000.dma' already
exists in 'dmaengine'` are unrelated platform messages and are not classified
as CamStream failures.

USB reset events involving the C270 remain **KNOWN / DEFERRED**. This evidence
does not attribute them to `camstream_video`, and no hardware, power, topology,
bandwidth, or host-controller cause is claimed.

### Stage 6C.4 acceptance matrix

| Requirement | Result |
| --- | --- |
| Source/build validation | **PASS** |
| Buildroot package rebuild | **PASS** |
| Video Capture capability | **PASS** |
| Streaming capability | **PASS** |
| Stage 6C.2 format regression | **PASS** |
| REQBUFS | **PASS** |
| QUERYBUF | **PASS** |
| MMAP | **PASS** |
| QBUF | **PASS** |
| STREAMON | **PASS** |
| Poll wake | **PASS** |
| DQBUF | **PASS** |
| Re-QBUF | **PASS** |
| 614400-byte payload | **PASS** |
| Sequence progression | **PASS** |
| Monotonic timestamp metadata | **PASS** |
| 30-frame run 1 | **PASS** |
| 30-frame run 2 | **PASS** |
| 60-frame pacing run 1 | **PASS** |
| 60-frame pacing run 2 | **PASS** |
| Measured pacing, approximately 29.93 fps | **PASS** |
| Visual synthetic frame | **PASS** |
| Frame-size validation | **PASS** |
| STREAMOFF | **PASS** |
| MMAP cleanup | **PASS** |
| Repeated capture | **PASS** |
| Module unload | **PASS** |
| Synthetic-node removal | **PASS** |
| C270 coexistence | **PASS** |
| CamStream WARNING/Oops/BUG | **NONE OBSERVED** |

Stage 6C.4 is **COMPLETE**. At that checkpoint boundary, Stage 6C remained
**IN PROGRESS** pending final Buildroot-image integration validation.

### BeagleBone Black validation commands

Copy the rebuilt module from the development host, replacing the target
address before running `scp`:

```sh
MODULE="$HOME/TungNHS/camstream-workspace/output/stage06-camera-v4l2/build/camstream-video-1.0/camstream_video.ko"
BBB_IP='replace-with-bbb-ip'
scp "$MODULE" "root@${BBB_IP}:/tmp/camstream_video.ko"
```

On the target, load the module and discover its dynamic node by driver
identity:

```sh
insmod /tmp/camstream_video.ko

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

v4l2-ctl -d "$CAMSTREAM_NODE" --info
v4l2-ctl -d "$CAMSTREAM_NODE" --list-formats-ext
v4l2-ctl -d "$CAMSTREAM_NODE" --get-fmt-video
v4l2-ctl -d "$CAMSTREAM_NODE" --get-parm
```

Run the primary Stage 6B acceptance client and verify the saved payload:

```sh
rm -f /tmp/synthetic.yuyv
/usr/bin/camstream-capture \
    --device "$CAMSTREAM_NODE" \
    --format YUYV \
    --width 640 \
    --height 480 \
    --fps 30 \
    --count 10 \
    --output /tmp/synthetic.yuyv
status=$?
echo "Primary capture exit status: $status"
test "$status" -eq 0
test "$(stat -c %s /tmp/synthetic.yuyv)" -eq 614400
```

Revalidate corrected pacing with two 60-frame runs. Retain each complete client
log outside Git so the first and last kernel timestamps can be calculated:

```sh
run=1
while [ "$run" -le 2 ]; do
	log="/tmp/camstream-pacing-run-${run}.log"
	/usr/bin/camstream-capture \
		--device "$CAMSTREAM_NODE" \
		--format YUYV \
		--width 640 \
		--height 480 \
		--fps 30 \
		--count 60 >"$log" 2>&1
	status=$?
	cat "$log"
	echo "Pacing capture $run exit status: $status"
	test "$status" -eq 0 || exit 1

	awk '
		/^Captured valid frame\[/ { frame = 1; next }
		frame && /^  Sequence:/ {
			sequence = $2
			if (samples == 0)
				first_sequence = sequence
			last_sequence = sequence
			next
		}
		frame && /^  Timestamp:/ {
			timestamp = $2 + 0
			if (samples == 0)
				first_timestamp = timestamp
			last_timestamp = timestamp
			samples++
			frame = 0
		}
		END {
			intervals = last_sequence - first_sequence
			elapsed = last_timestamp - first_timestamp
			if (samples != 60 || intervals <= 0 || elapsed <= 0)
				exit 1
			printf "Intervals: %d\nElapsed: %.6f s\n", \
			       intervals, elapsed
			printf "Average interval: %.6f s\nMeasured FPS: %.3f\n", \
			       elapsed / intervals, intervals / elapsed
		}
	' "$log" || exit 1
	run=$((run + 1))
done
```

Inspect cleanup and coexistence before and after unloading:

```sh
v4l2-ctl --list-devices
dmesg | tail -n 120
rmmod camstream_video
test ! -e "$CAMSTREAM_NODE"
v4l2-ctl --list-devices
dmesg | tail -n 120
```

Pacing acceptance requires 60 valid frames, no error frames, continuous
sequence and buffer reuse, no poll timeout, exit status 0, and clean
STREAMOFF/MMAP/REQBUFS(0)/close behavior in both runs. For each log, elapsed is
the last timestamp minus the first, interval count is the last sequence minus
the first, average interval is elapsed divided by interval count, and measured
FPS is interval count divided by elapsed. The result must be materially
corrected from 10–11 fps and consistent with the 10-ms jiffy resolution.

### Visual-validation commands

On the development host, keep all captured output outside the repository:

```sh
BBB_IP='replace-with-bbb-ip'
scp "root@${BBB_IP}:/tmp/synthetic.yuyv" /tmp/synthetic.yuyv
test "$(stat -c %s /tmp/synthetic.yuyv)" -eq 614400

if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg \
        -f rawvideo \
        -pixel_format yuyv422 \
        -video_size 640x480 \
        -i /tmp/synthetic.yuyv \
        -frames:v 1 \
        /tmp/synthetic.png
else
    echo "ffmpeg unavailable; visual conversion cannot be reproduced here"
fi
```

The recorded visual validation is **PASS**. The output was 614400 bytes, and
the converted 640x480 YUYV422 image showed the complete dark-to-light grayscale
gradient expected with neutral U and V values of 128. No visible truncation or
corruption was observed. The raw frame and converted image remain outside Git.

## Stage 6C.5 — Final Buildroot and Runtime Integration

### Integration status

- Project Buildroot configuration: **PASS**
- Final image/rootfs contents: **PASS**
- Packaged BBB runtime validation: **PASS**
- Reload and coexistence regression: **PASS**
- Final teardown: **PASS**
- Stage 6C.5: **COMPLETE**
- Stage 6C: **COMPLETE**

The project `beaglebone_defconfig` enables both
`BR2_PACKAGE_CAMSTREAM_VIDEO=y` and `BR2_PACKAGE_CAMSTREAM_CAPTURE=y`.
Inspection of the final Buildroot `rootfs.tar` confirmed these packaged
artifacts:

```text
/lib/modules/6.18.1/updates/camstream_video.ko
/usr/bin/camstream-capture
```

Final acceptance used the module and application installed in the image. A
manual module copy through `/tmp` is not part of the Stage 6C.5 acceptance
path.

### Packaged BBB runtime evidence

The packaged module dynamically registered `/dev/video2` in the tested
sessions. That number is runtime evidence only and is not a fixed device ABI.
The first packaged capture used YUYV 640x480 at the advertised 30 fps and
passed REQBUFS, MMAP, QBUF, STREAMON, DQBUF, re-QBUF, STREAMOFF, and MMAP
cleanup. It produced 60 valid frames, zero error frames, 614400 bytes used per
frame, continuous sequences 0 through 59, and exit status 0.

During capture, the CamStream synthetic node and the C270 nodes `/dev/video0`,
`/dev/video1`, and `/dev/media0` remained available together.

For the reload regression, `rmmod camstream_video` returned 0, the module was
loaded again from `/lib/modules/6.18.1/updates/`, and a second 60-frame capture
again produced 60 valid frames, zero error frames, and exit status 0. Final
`rmmod camstream_video` returned 0; the synthetic node disappeared and the
C270 nodes remained available.

No CamStream-related WARNING, Oops, BUG, use-after-free, list corruption, or
workqueue warning was observed. C270 USB reset events remain
**KNOWN / DEFERRED** and are not attributed to `camstream_video`.

### Stage 6C.5 acceptance matrix

| Requirement | Result |
| --- | --- |
| `camstream-video` enabled in project defconfig | **PASS** |
| `camstream-capture` enabled in project defconfig | **PASS** |
| Module present in final rootfs | **PASS** |
| Capture application present in final rootfs | **PASS** |
| Packaged 60-frame capture | **PASS** |
| Valid/error frames | **60 / 0** |
| 614400-byte payload | **PASS** |
| Sequence 0 through 59 | **PASS** |
| STREAMOFF and MMAP cleanup | **PASS** |
| C270 coexistence | **PASS** |
| Rootfs module reload | **PASS** |
| Second packaged 60-frame capture | **PASS** |
| Final module unload | **PASS** |
| Synthetic-node removal | **PASS** |
| C270 availability after teardown | **PASS** |
| CamStream WARNING/Oops/BUG | **NONE OBSERVED** |

Stage 6C.5 is **COMPLETE**. Stages 6C.1 through 6C.5 have passed their defined
gates, so Stage 6C is **COMPLETE**. Stage 7 remains **PLANNED** and has not
started.
