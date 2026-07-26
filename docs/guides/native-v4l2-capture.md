# Native V4L2 capture application

## 1. Purpose and scope

`camstream-capture` is the CamStream Control low-level V4L2 diagnostic and
validation application. It exercises capture-device capabilities, negotiated
formats and frame rates, MMAP streaming, buffer ownership, error-frame
handling, and single-frame file output without hiding those operations behind
a multimedia framework.

The application validates the upstream `uvcvideo` path with the Logitech C270
and is intended to exercise the later Stage 6C synthetic V4L2 driver through
the same userspace interface. It is not the final streaming product: it does
not decode MJPEG, convert YUYV, run a GStreamer pipeline, or provide a daemon
or control protocol.

## 2. Source structure and ownership

The application is under `apps/camstream-capture/`:

| Component | Responsibility |
| --- | --- |
| `src/main.cpp` | CLI parsing, validation, and top-level operation/cleanup sequence |
| `CaptureConfig` | Validated device, format, dimensions, FPS, skip/count, and output request |
| `V4l2Device` | Owns the device descriptor, driver buffer pool, mappings, streaming state, ioctls, capture loop, and explicit cleanup |
| `MappedBuffer` | Move-only RAII owner for one successful `mmap()` mapping |
| `Makefile` | C++17 host or cross build with `-Wall -Wextra -Wpedantic` |

`V4l2Device` is neither copyable nor movable, so one object remains the stable
owner of the descriptor and capture resources. Each `MappedBuffer` unmaps its
owned region exactly once; moving transfers ownership rather than duplicating
it. Explicit cleanup reports failures, while destructors provide a final
fallback for partially initialized paths.

## 3. Operating modes

The program has two distinct paths:

- Without `--format`, it runs `VIDIOC_QUERYCAP` and enumerates advertised pixel
  formats, frame sizes, and frame intervals. Enumeration does not prove that a
  mode streams successfully.
- With `--format`, it validates capabilities, negotiates the requested mode,
  optionally negotiates FPS, allocates and queues MMAP buffers, streams the
  requested valid-frame count, and cleans up.

The application does not enumerate and stream in the same invocation. Inspect
capabilities first, then run a second command with a mode the device advertises.

## 4. Implemented V4L2 workflow

The capture path is:

```text
open(O_RDWR | O_NONBLOCK)
  -> VIDIOC_QUERYCAP
  -> VIDIOC_TRY_FMT
  -> VIDIOC_S_FMT
  -> VIDIOC_G_FMT
  -> optional frame-rate block (--fps only):
       VIDIOC_G_PARM        (read initial parameters)
       VIDIOC_S_PARM        (request timeperframe)
       VIDIOC_G_PARM        (read final active interval)
  -> VIDIOC_REQBUFS         (request four MMAP buffers)
  -> VIDIOC_QUERYBUF
  -> mmap
  -> VIDIOC_QBUF            (every granted buffer)
  -> VIDIOC_STREAMON
  -> poll
  -> VIDIOC_DQBUF
  -> validate / skip / optionally save
  -> VIDIOC_QBUF
  -> repeat until --count valid post-skip frames complete
  -> VIDIOC_STREAMOFF
  -> munmap
  -> VIDIOC_REQBUFS(count=0)
  -> close
```

The driver may normalize the requested format or interval. The application
prints `TRY_FMT`, `S_FMT`, final `G_FMT`, and the initial/set/final streaming
parameters rather than assuming that the request was accepted unchanged.

The program requests four buffers but accepts the count granted by the driver.
A project sanity limit rejects a returned count above 32. Buffer indexes,
lengths, `bytesused` bounds, types, and memory modes are validated before use.
QUERYBUF offsets are checked for `off_t` representability and then passed to
`mmap()` as driver-provided tokens. `V4L2_BUF_FLAG_ERROR` is inspected to
decide payload validity. The timestamp microsecond field is range-checked;
sequence and timestamp values are otherwise reported for diagnostics rather
than semantically or chronologically validated.

## 5. Buffer ownership contract

Streaming ownership follows the V4L2 MMAP contract:

```text
QBUF  -> driver owns the buffer
DQBUF -> userspace owns the buffer
QBUF  -> driver owns the buffer again
```

Payload inspection and file output occur only after a successful DQBUF and
before the matching re-QBUF. The application never reads mapped payload bytes
after returning that buffer to the driver. A dequeued buffer is requeued even
when `V4L2_BUF_FLAG_ERROR` marks its payload unusable.

## 6. CLI reference

The implemented CLI is:

```text
Usage: camstream-capture [--device <path>] [--format <MJPG|YUYV>]
       [--width <pixels>] [--height <pixels>] [--fps <rate>]
       [--skip <frames>] [--count <frames>] [--output <path>]
       [--help]
```

| Option | Behavior |
| --- | --- |
| `--device <path>` | V4L2 node; default `/dev/video0` |
| `--format MJPG\|YUYV` | Enables negotiation and streaming for one supported format |
| `--width <pixels>` | Positive width with `--format`; default 640 |
| `--height <pixels>` | Positive height with `--format`; default 480 |
| `--fps <rate>` | Positive integer FPS request; requires `--format` |
| `--skip <frames>` | Non-negative number of valid warm-up frames; default 0 and requires `--format` |
| `--count <frames>` | Positive number of valid post-skip frames to capture; default 10 and requires `--format` when supplied |
| `--output <path>` | Saves the final valid post-skip MJPEG or YUYV frame; requires `--format` |
| `--help` | Prints usage and exits successfully |

Frames carrying `V4L2_BUF_FLAG_ERROR` count as dequeued/error frames, but do
not satisfy either `--skip` or `--count`. They are requeued and capture
continues. Sixteen consecutive error frames terminate capture instead of
retrying indefinitely. The final summary keeps dequeued, skipped-valid,
captured-valid, and error counts separate.

## 7. MJPEG and raw YUYV output

For MJPEG, the final valid payload must contain JPEG SOI and EOI markers. The
program then writes exactly the `bytesused` bytes reported by DQBUF. It does
not decode or re-encode the image.

For YUYV, the program records the active width, height, `bytesperline`, and
`sizeimage` returned by G_FMT, then writes the original camera payload without
conversion or byte reordering. A difference between `bytesused` and
`sizeimage` is reported; the file still contains exactly `bytesused` bytes and
is never silently padded or truncated. For the tested C270 640x480 YUYV mode,
the expected complete packed-YUYV payload is 614400 bytes.

Captured files can contain private imagery and should remain local evidence.
To inspect a 640x480 raw YUYV frame on the Ubuntu host:

```sh
ffplay -f rawvideo -pixel_format yuyv422 -video_size 640x480 final.yuyv

ffmpeg -f rawvideo -pixel_format yuyv422 -video_size 640x480 \
  -i final.yuyv -frames:v 1 final.png
```

The width, height, and pixel format must match the active G_FMT result. Do not
reuse the example values for a differently negotiated file.

## 8. Host build

Build the native host diagnostic with the default compiler:

```sh
make -C apps/camstream-capture clean
make -C apps/camstream-capture
file apps/camstream-capture/camstream-capture
apps/camstream-capture/camstream-capture --help
```

An x86-64 host build validates compilation and CLI error paths, but it is not
BBB runtime evidence. A host without a suitable `/dev/video*` node cannot
validate real capture.

## 9. Buildroot package integration and development deployment

The final Stage 6B integration path is:

```text
apps/camstream-capture
  -> Buildroot camstream-capture package
  -> TARGET_CXX
  -> target root filesystem
  -> /usr/bin/camstream-capture
  -> BeagleBone Black + Logitech C270
```

The project-owned package definition is:

```text
br2-external/package/camstream-capture/
├── Config.in
└── camstream-capture.mk
```

`Config.in` requires C++ support. `camstream-capture.mk` takes source from
`apps/camstream-capture`, builds it with Buildroot's `TARGET_CXX`, and installs
the result as `/usr/bin/camstream-capture`. The saved project configuration
retains `BR2_PACKAGE_CAMSTREAM_CAPTURE=y` in
`br2-external/configs/beaglebone_defconfig`.

Buildroot source, project integration, and generated build output are distinct:

```sh
export REPO="$HOME/TungNHS/camstream-control"
export WORKSPACE="$HOME/TungNHS/camstream-workspace"
export BUILDROOT="$WORKSPACE/sources/buildroot-2026.02.3"
export BR2_EXTERNAL="$REPO/br2-external"
export OUTPUT="$WORKSPACE/output/stage06-camera-v4l2"

make -C "$BUILDROOT" O="$OUTPUT" BR2_EXTERNAL="$BR2_EXTERNAL" \
  beaglebone_defconfig
grep '^BR2_PACKAGE_CAMSTREAM_CAPTURE=y$' "$OUTPUT/.config"
make -C "$BUILDROOT" O="$OUTPUT" BR2_EXTERNAL="$BR2_EXTERNAL"
file "$OUTPUT/target/usr/bin/camstream-capture"
```

The clean project build, generated image, boot, packaged binary, and packaged
BBB/C270 runtime were manually validated for the Stage 6B closure. Generated
output remains outside the repository.

### Development-only cross build and deployment

The earlier manual workflow remains useful for focused development before a
full image rebuild:

```sh
export REPO="$HOME/TungNHS/camstream-control"
export WORKSPACE="$HOME/TungNHS/camstream-workspace"
export BUILDROOT="$WORKSPACE/sources/buildroot-2026.02.3"
export BR2_EXTERNAL="$REPO/br2-external"

test -d "$BUILDROOT"
test -d "$BR2_EXTERNAL"
find "$WORKSPACE" -path '*/host/bin/*-g++' -print
```

Build outputs are generated workspace state and may use different stage/output
directory names. During the final Stage 6B inspection, the compiler actually
used by this work was found under the `stage06-camera-v4l2` output as
`host/bin/arm-linux-g++`; that output name is recorded evidence, not a project
path contract. Select the verified result explicitly rather than assuming it:

```sh
export CAMSTREAM_CXX="/absolute/path/reported/by/find/to/arm-linux-g++"
test -x "$CAMSTREAM_CXX"
"$CAMSTREAM_CXX" --version

make -C "$REPO/apps/camstream-capture" clean
make -C "$REPO/apps/camstream-capture" \
  CXX="$CAMSTREAM_CXX"

file "$REPO/apps/camstream-capture/camstream-capture"
```

The expected identity is a 32-bit little-endian ARM EABI5 executable using the
hard-float ABI. Do not copy a host x86-64 binary to the target.

For a development-only target check, deploy the ARM binary manually:

```sh
scp "$REPO/apps/camstream-capture/camstream-capture" \
  root@TARGET_IP:/tmp/camstream-capture

ssh root@TARGET_IP \
  'chmod 0755 /tmp/camstream-capture && /tmp/camstream-capture --help'
```

Replace `TARGET_IP` with the verified target address. This manual `/tmp`
deployment is historical/development workflow, not the final integration
path. The packaged `/usr/bin/camstream-capture` path is the Stage 6B release
baseline.

## 10. Validated capture examples

MJPEG 640x480 at 30 fps:

```sh
/usr/bin/camstream-capture \
  --device /dev/video0 \
  --format MJPG \
  --width 640 \
  --height 480 \
  --fps 30 \
  --skip 30 \
  --count 10 \
  --output /tmp/final.jpg
```

YUYV 640x480 at 15 fps:

```sh
/usr/bin/camstream-capture \
  --device /dev/video0 \
  --format YUYV \
  --width 640 \
  --height 480 \
  --fps 15 \
  --skip 30 \
  --count 10 \
  --output /tmp/final.yuyv
```

These commands use the final packaged path and the earlier functional
regression parameters. Exact final packaged-test frame counters were not
retained. They are not throughput or long-duration stability measurements.

## 11. Failure isolation

- **Metadata node rejected:** this is expected for `/dev/video1`; select the
  node whose Device Caps include Video Capture and Streaming.
- **Format or FPS normalized:** compare the printed request, TRY/S/G_FMT, and
  initial/set/final G/S_PARM results; never assume exact acceptance.
- **Poll timeout or repeated EAGAIN:** preserve the application error and
  contemporaneous kernel/USB log evidence before changing configuration.
- **Error-flagged or partial YUYV frames:** inspect the reported flags,
  sequence, `bytesused`, active `sizeimage`, and USB events. The application
  drops and requeues recoverable error buffers.
- **Dark initial output:** increase a bounded `--skip` value. Initial dark
  frames are consistent with camera/stream-start settling, but the specific
  internal cause was not proven.

YUYV 640x480 at 30 fps was unstable on the validated BBB USB topology. The
behavior is consistent with a USB transport/platform limitation under the
current topology, but the exact root cause has not been proven and remains
deferred. Do not label Wi-Fi, a hub, power, MUSB, the camera, or bandwidth as
the cause without controlled evidence.

## 12. Current checkpoint status

| Boundary | Status |
| --- | --- |
| Stage 6B application functionality | **PASS** |
| Stage 6B documentation | **PASS** after final documentation cleanup |
| Buildroot package integration | **PASS** |
| Saved `beaglebone_defconfig` package selection | **PASS** |
| Clean Buildroot image generation and BBB boot | **PASS** — manually validated by project owner |
| Final packaged `/usr/bin/camstream-capture` BBB/C270 validation | **PASS** |
| Stage 6B overall | **COMPLETE** |
