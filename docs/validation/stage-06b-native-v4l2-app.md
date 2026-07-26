# Stage 6B native V4L2 application validation

## 1. Objective

Validate the project-owned `camstream-capture` application against the real
Logitech C270 V4L2 path on the BeagleBone Black, including negotiation, MMAP
streaming, buffer ownership, robust frame accounting, and MJPEG/YUYV output.

## 2. Scope and status boundary

This report covers the Stage 6B application, its initial manual deployment,
Buildroot package integration, and final packaged BBB/C270 execution. It does
not cover GStreamer, image decoding or conversion, a service, or the Stage 6C
custom driver.

Current status is deliberately split:

| Scope | Status |
| --- | --- |
| Stage 6B application functionality | **PASS** |
| Stage 6B public documentation | **PASS** after final documentation cleanup |
| Buildroot package integration | **PASS** |
| `beaglebone_defconfig` integration | **PASS** |
| Clean Buildroot rebuild and image generation | **PASS** — manually validated by project owner |
| Final packaged `/usr/bin/camstream-capture` BBB validation | **PASS** |
| Stage 6B overall | **COMPLETE** |

Stage 6B is complete. Stage 6C remains a separate, pending stage.

## 3. Provenance

| Field | Recorded provenance |
| --- | --- |
| Documentation and evidence review date | 2026-07-26 |
| Target execution date | Not retained in the supplied evidence |
| Branch | `stage/06b-native-v4l2-app` |
| Base commit | `654b80d8e3f80aec71ce33bb3c1d075bcca0e2c2` (`stage 6a: bring up USB UVC camera with V4L2`) |
| Tested implementation state | Stage 6B working tree under `apps/camstream-capture/`; not represented by the base commit alone |
| Deployment method | Initial ARM binary copied to `/tmp`; final binary installed as `/usr/bin/camstream-capture` by the Buildroot package |
| Runtime evidence source | Recorded Stage 6B target results, project-owner package closure validation, and local visual inspection of captured files |
| Captured images | Private local evidence; not committed |
| Exact deployed-binary checksum | Not retained; no checksum identity is inferred after testing |

The cross-built executable inspected during this checkpoint identified as a
32-bit little-endian ARM EABI5 hard-float executable built with the Buildroot
toolchain. That checkpoint observation confirms the documented build method,
but it is not used as a substitute for a contemporaneously retained checksum
of the deployed test binary.

## 4. Hardware and software baseline

| Component | Validated baseline |
| --- | --- |
| Target | BeagleBone Black |
| Linux kernel | 6.18.1 |
| Camera | Logitech C270 HD Webcam |
| Camera driver | Upstream `uvcvideo` |
| Image node | `/dev/video0` |
| Metadata node | `/dev/video1` |
| Application | `camstream-capture`; final baseline packaged at `/usr/bin/camstream-capture` |

USB instability observations apply to the current BBB multi-device USB
topology. They do not establish that a particular device, hub, controller,
cable, power source, or traffic class caused the behavior.

## 5. Application build and deployment

During final documentation cleanup, the application was rebuilt as C++17 with
`-Wall -Wextra -Wpedantic`; the build completed with zero warnings. A second
build adding `-Wconversion`, `-Wsign-conversion`, `-Wshadow`, and `-Wformat=2`
also completed with zero warnings. `clang-tidy` 14.0.0 completed with no
diagnostics. `cppcheck` 2.7 completed with no findings after suppressing only
missing system-header diagnostics.

The initial checkpoints used an ARM executable copied to `/tmp`. Final closure
used the project package under
`br2-external/package/camstream-capture/`, with
`BR2_PACKAGE_CAMSTREAM_CAPTURE=y` retained in
`br2-external/configs/beaglebone_defconfig`. The package builds repository
source with `TARGET_CXX` and installs `/usr/bin/camstream-capture`.

The project owner manually validated a clean Buildroot rebuild, generated
rootfs/image, flashed-image boot, and packaged BBB/C270 runtime. The final
packaged checks passed for MJPEG, YUYV 640x480 at 15 fps, metadata-only
`/dev/video1` rejection, and output visual validation. Exact packaged-test
frame counts were not retained and are not inferred here.

Repository audit also found `BR2_PACKAGE_CAMSTREAM_CAPTURE=y` in the existing
generated `.config` and identified
`target/usr/bin/camstream-capture` as a stripped 32-bit ARM EABI5 executable.
This output inspection corroborates package state but is not presented as a
second clean rebuild. The generated output directory name is not a canonical
project path.

## 6. Device capability and enumeration results

`VIDIOC_QUERYCAP` passed on `/dev/video0`. Driver, card, bus, version,
capability, device-capability, Video Capture, and Streaming checks completed.
Pixel-format, frame-size, and frame-interval enumeration also passed.

Running the application against `/dev/video1` produced the intended
metadata-only rejection rather than treating that node as an image source.

| Checkpoint | Result |
| --- | --- |
| QUERYCAP on `/dev/video0` | PASS |
| Format enumeration | PASS |
| Frame-size enumeration | PASS |
| Frame-interval enumeration | PASS |
| `/dev/video1` metadata-only rejection | PASS |

Enumeration is capability evidence only. Runtime mode results are recorded
separately below.

## 7. Negotiation and streaming lifecycle

The following application-controlled sequence was exercised on the target:

```text
TRY_FMT -> S_FMT -> G_FMT
[when --fps is supplied] G_PARM -> S_PARM -> G_PARM
REQBUFS -> QUERYBUF -> mmap
QBUF -> STREAMON -> poll -> DQBUF -> validate -> QBUF
STREAMOFF -> munmap -> REQBUFS(count=0) -> close
```

The driver-granted MMAP count and each buffer's returned index and length were
validated before use. Dequeued indexes and `bytesused` were checked against
the owned mappings. Buffers were returned to the driver after both normal and
recoverable error-frame processing.

## 8. Runtime-tested camera modes

| Mode | Runtime result | Evidence boundary |
| --- | --- | --- |
| MJPG 640x480 at 30 fps | PASS | Configuration, capture, JPEG output, and host visual validation passed |
| YUYV 640x480 at 15 fps | PASS WITH OBSERVED RECOVERABLE ERROR | Functional capture/output and recovery passed; one recoverable error buffer occurred in the final regression |
| YUYV 320x240 at 30 fps | PASS | Real frame capture passed |
| YUYV 640x480 at 30 fps | UNSTABLE — KNOWN ISSUE | Valid frames were captured, but frequent error-flagged/partial buffers were observed under the current USB topology; the mode is not accepted as stable |

These results are functional checks, not measured throughput, latency,
repeatability, long-duration stability, or a production-format decision.

### 8.1 Final MJPEG regression

The final 6B.9 regression used:

```sh
/tmp/camstream-capture \
  --device /dev/video0 --format MJPG \
  --width 640 --height 480 --fps 30 \
  --skip 30 --count 10 --output /tmp/final.jpg
```

| Observation | Recorded result |
| --- | --- |
| Saved payload | 25658 bytes |
| Dequeued frames | 40 |
| Skipped valid frames | 30 |
| Captured valid frames | 10 |
| Error frames | 0 |
| STREAMOFF | Successful |
| MMAP cleanup | Successful |
| Exit code | 0 |
| Host visual validation | PASS |

### 8.2 Final YUYV regression

The final 6B.9 regression used:

```sh
/tmp/camstream-capture \
  --device /dev/video0 --format YUYV \
  --width 640 --height 480 --fps 15 \
  --skip 30 --count 10 --output /tmp/final.yuyv
```

| Observation | Recorded result |
| --- | --- |
| Saved payload | 614400 bytes |
| Resolution / format | 640x480 / YUYV |
| Dequeued frames | 41 |
| Skipped valid frames | 30 |
| Captured valid frames | 10 |
| Error frames | 1 recoverable error buffer |
| STREAMOFF | Successful |
| MMAP cleanup | Successful |
| Exit code | 0 |
| Raw conversion/visual validation | PASS; valid visible image data |

The error buffer did not satisfy `--skip` or `--count`, was not saved, and was
requeued. Capture recovered, obtained all requested valid frames, and exited
successfully. This is a functional PASS with an observed recoverable error,
not a claim of perfectly clean or sustained streaming.

### 8.3 Metadata-node and CLI regressions

- `/dev/video1` was correctly rejected as metadata-only with exit code 1.
- `--skip 10` without `--format` was correctly rejected.
- `--format MJPG --skip abc` was correctly rejected as malformed input.

### 8.4 Final packaged runtime closure

The project owner manually validated the clean-image
`/usr/bin/camstream-capture` path on the BBB with the Logitech C270:

| Packaged checkpoint | Result |
| --- | --- |
| MJPEG capture | PASS |
| YUYV 640x480 at 15 fps capture | PASS |
| Metadata-only `/dev/video1` rejection | PASS |
| Captured-output visual validation | PASS |

These closure results establish packaged runtime behavior. They do not replace
the detailed 6B.9 counts above, and no unretained packaged-test counts are
invented.

## 9. Frame validation and error handling

MJPG 640x480 at 30 fps produced clean dequeued frames during the recorded
final regression. YUYV 640x480 at 15 fps completed successfully while
recovering from one error-flagged buffer. YUYV 640x480 at 30 fps produced a
complete frame followed by partial/corrupted buffers carrying
`V4L2_BUF_FLAG_ERROR` during the observed unstable run.

The application behavior passed its error-handling checkpoint:

- an error-flagged buffer was counted as an error, not as skipped or captured;
- its payload was not saved;
- the valid buffer metadata path returned it with QBUF;
- streaming continued for recoverable errors.

A bounded consecutive-error limit is implemented and source-reviewed to avoid
an infinite retry loop, but reaching that limit was **NOT TESTED** at runtime.

Occasional recoverable YUYV error frames can still occur. Successful userspace
filtering does not make the underlying transport behavior stable.

## 10. Skip and initial-frame behavior

Early captured frames were observed to be dark or nearly black, while later
frames contained normal visible camera content. Initial dark frames are
consistent with camera/stream-start settling; the specific internal cause was
not proven.

`--skip` passed target validation as a bounded warm-up mechanism. Only valid
frames advanced the skip count; error-flagged buffers did not. After the skip
phase, the application captured exactly the requested number of valid frames,
and `--output` selected the final valid post-skip frame.

## 11. Output validation

For MJPEG, the application validated SOI/EOI markers and wrote exactly the
DQBUF `bytesused` payload. The final 640x480 regression saved 25658 bytes, and
the file was visually validated on the host.

For YUYV, the application writes exactly the DQBUF `bytesused` payload without
conversion, byte reordering, or padding to `sizeimage`. The 640x480 acceptance
check used the expected complete packed-YUYV payload size:

```text
pixel format: YUYV
resolution: 640x480
expected complete payload: 614400 bytes
```

The final regression saved 614400 bytes. Raw conversion and visual validation
passed using the matching `yuyv422` pixel format and 640x480 resolution, and
the result contained valid visible image data. Captured image content remains
private local evidence; raw runtime logs are not reconstructed here.

## 12. Cleanup validation

Normal completion retained the required ownership order:

```text
STREAMOFF -> munmap -> VIDIOC_REQBUFS(count=0) -> close
```

STREAMOFF was issued only after successful STREAMON. Source review confirmed
that partial initialization uses the same explicit cleanup path, with RAII as
a fallback for owned mappings. Deterministic normal cleanup is **PASS** for the
validated functional scope; exhaustive fault injection into every cleanup
failure path was **NOT TESTED**.

## 13. Acceptance matrix

| Acceptance item | Result |
| --- | --- |
| Host build with `-Wall -Wextra -Wpedantic` | PASS — zero warnings |
| Host build with additional conversion/shadow/format warnings | PASS — zero warnings |
| `clang-tidy` 14.0.0 | PASS — no diagnostics |
| `cppcheck` 2.7 | PASS — no findings; missing system headers only suppressed |
| Buildroot cross-compiler ARM build | PASS |
| Manual `/tmp` deployment and execution | PASS |
| Buildroot package integration | PASS |
| `BR2_PACKAGE_CAMSTREAM_CAPTURE=y` in saved `beaglebone_defconfig` | PASS |
| Clean Buildroot rebuild from project configuration | PASS — manually validated by project owner |
| Rootfs/image generation | PASS — manually validated by project owner |
| Flashed-image BBB boot | PASS — manually validated by project owner |
| Packaged `/usr/bin/camstream-capture` present | PASS |
| Packaged BBB/C270 runtime | PASS |
| Packaged MJPEG capture | PASS |
| Packaged YUYV 640x480 at 15 fps capture | PASS |
| Packaged output visual validation | PASS |
| QUERYCAP and capture/streaming capability validation | PASS |
| Format, size, and interval enumeration | PASS |
| Metadata-only node rejection | PASS |
| TRY_FMT/S_FMT/G_FMT negotiation | PASS |
| G_PARM/S_PARM frame-rate negotiation | PASS |
| REQBUFS/QUERYBUF/MMAP setup | PASS |
| Queue every granted buffer | PASS |
| STREAMON/STREAMOFF | PASS |
| Poll/DQBUF/re-QBUF lifecycle | PASS |
| Buffer index and payload-bound validation | PASS |
| `V4L2_BUF_FLAG_ERROR` filtering and requeue | PASS |
| Bounded persistent-error termination | IMPLEMENTED / SOURCE REVIEWED — runtime trigger NOT TESTED |
| MJPEG save and visual validation | PASS |
| Raw YUYV 640x480 at 15 fps save and visual validation | PASS WITH OBSERVED RECOVERABLE ERROR |
| YUYV 320x240 at 30 fps runtime frame capture | PASS |
| Valid-frame skip/count behavior | PASS |
| Deterministic cleanup | PASS |
| Exhaustive cleanup fault injection | NOT TESTED |
| Stable YUYV 640x480 at 30 fps under current topology | UNSTABLE — KNOWN ISSUE |
| Long-term USB stability | DEFERRED |
| Stage 6B documentation | PASS |
| Stage 6B overall | COMPLETE |

## 14. Known and deferred USB behavior

Observed indicators included `V4L2_BUF_FLAG_ERROR`, partial YUYV `bytesused`,
and USB reset events around some stream starts. Lower-load YUYV modes behaved
significantly better in the recorded functional checks.

The behavior is consistent with a USB transport/platform limitation under the
current topology, but the exact root cause has not been proven and remains
deferred. The evidence does not establish USB bandwidth, Wi-Fi, a hub, power,
MUSB, or the camera as the cause. Controlled topology isolation and sustained
runtime testing belong to later integration and stability work.

## 15. Conclusion

Stage 6B native application functionality is **PASS**. Buildroot package
integration, clean image generation, packaged BBB/C270 runtime, and Stage 6B
documentation are **PASS**. The application
successfully exercised the real C270 `uvcvideo` path through negotiation,
MMAP streaming, defensive dequeue/requeue handling, valid-frame skip/count,
and MJPEG/YUYV output on the BBB.

Stage 6B overall is **COMPLETE**. Stage 6C is the next separate stage and has
not started.
The YUYV 640x480 at 30 fps instability and long-term USB stability remain
known/deferred work; no exact root cause is claimed.
