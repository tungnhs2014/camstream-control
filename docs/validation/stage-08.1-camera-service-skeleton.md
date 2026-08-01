# Stage 8.1 — Camera Service Skeleton

## Checkpoint status

- Host/Buildroot validation date: 2026-07-30
- BBB evidence: accepted from the project owner; runtime date was not supplied
- Branch: `stage/08.1-camera-service-skeleton`
- Base commit: `cdabf2b` (`stage 8: migrate userspace builds to CMake`)
- Stage 8.1 host validation: **PASS**
- Stage 8.1 Buildroot validation: **PASS**
- Stage 8.1 BBB service lifecycle smoke test: **PASS**
- Stage 8.1: **COMPLETE**
- Stage 8.2: **NEXT — Camera Service owns and controls GStreamer pipeline**

`STAGE7-USB-01` remains **OPEN / DEFERRED**. This service checkpoint performs
no camera or USB operation and does not establish stable production 30-fps,
long-duration USB reliability, or production-ready camera behavior.

## Purpose and scope

Stage 8.1 adds a minimal foreground Linux service whose only responsibility is
to demonstrate an explicit lifecycle and clean synchronous SIGINT/SIGTERM
shutdown. It creates no camera, multimedia, IPC, network, recording,
configuration, daemon, init-system, or production-recovery behavior.

The implemented source layout is:

```text
apps/camstream-service/
├── CMakeLists.txt
├── include/camstream/camera_service.hpp
└── src/
    ├── camera_service.cpp
    └── main.cpp
```

The root CMake project adds `CAMSTREAM_BUILD_SERVICE`, defaulting to `ON` for a
normal developer build. The `camstream-service` C++17 executable uses the
existing `-Wall -Wextra -Wpedantic` policy, target-scoped include paths and
compile/link requirements, `GNUInstallDirs`, and `Threads::Threads`. It
installs as `/usr/bin/camstream-service`.

## Lifecycle and ownership

The non-copyable, non-movable `CameraService` follows:

```text
Created
  -> Initialized
  -> Running
  -> StopRequested
  -> Stopped
```

| Operation | Contract |
| --- | --- |
| `initialize()` | Valid only from Created; blocks SIGINT/SIGTERM and creates the owned `signalfd`; repeated initialization fails |
| `run()` | Valid only from Initialized; enters the blocking event loop; repeated or premature calls fail |
| `request_stop()` | Moves Running to StopRequested and is idempotent after a request or completed stop |
| `shutdown()` | Ignores later termination requests, drains/closes the signal descriptor, restores the prior signal mask, enters Stopped, and is idempotent |
| Destructor | Invokes the same cleanup as a failure fallback and never throws |

The object owns one `signalfd` descriptor and the temporary signal-mask
change. Partial initialization restores the previous mask. Explicit shutdown
first terminally ignores SIGINT/SIGTERM, then closes the descriptor exactly
once before restoring the mask. This prevents repeated termination requests
from applying their default action during cleanup; repeated cleanup does not
double-close the descriptor. The class is intentionally single-threaded and
process-lifetime scoped. All lifecycle operations and destruction must use the
thread that initialized it, and the caller must not independently change that
thread's mask or the termination-signal dispositions. Stopped is terminal and
the process exits without restoring the prior signal dispositions.

## Signal and foreground-process design

The service blocks SIGINT and SIGTERM with `pthread_sigmask`, then receives
them synchronously from a nonblocking, close-on-exec `signalfd`. The event loop
waits with blocking `poll()` and retries only an interrupted poll/read. It has
no asynchronous signal handler and no busy-poll loop. This keeps the loop
suitable for adding a separate IPC descriptor in a later checkpoint without
adding IPC now.

Normal execution logs these observable checkpoints:

```text
Camera service starting
Camera service initialized
Camera service running
SIGINT received
Camera service stopping
Camera service stopped
```

The signal line reports `SIGTERM received` for a SIGTERM shutdown. Output is
flushed at each lifecycle checkpoint so a supervising test can observe
Running before it sends the signal. The process remains in the foreground; it
does not fork, daemonize, create a PID file, or redirect standard streams.

## CLI and exit-code contract

```text
camstream-service [--help]
```

| Condition | Exit status |
| --- | ---: |
| `--help` | 0 |
| Clean SIGINT or SIGTERM shutdown | 0 |
| Initialization, event-loop, or cleanup failure | 1 |
| Unknown, duplicate, or positional argument | 2 |

No device, format, dimension, frame-rate, GStreamer, or configuration-file
option exists in this checkpoint.

## Host validation

The host validation used an out-of-source Ninja build with only
`CAMSTREAM_BUILD_SERVICE=ON`. Configure, compile, and temporary DESTDIR install
passed. The compile commands contained C++17 and `-Wall -Wextra -Wpedantic`,
and emitted no warnings. The installed file was:

```text
usr/bin/camstream-service
```

A separate default-options developer configure, build, and temporary install
also passed with zero warnings and installed all three existing userspace
executables: `camstream-capture`, `camstream-gst-test`, and
`camstream-service`.

| Host check | Result |
| --- | --- |
| CMake configure | **PASS** |
| CMake build | **PASS — ZERO WARNINGS** |
| Temporary DESTDIR install | **PASS** |
| Default all-target build/install | **PASS — ZERO WARNINGS**, all three userspace executables installed |
| `--help` | **PASS**, exit 0 |
| Unknown option | **PASS**, exit 2 |
| Duplicate `--help` | **PASS**, exit 2 |
| Positional argument | **PASS**, exit 2 |
| SIGINT lifecycle | **PASS**, Running observed, signal/stopping/stopped logged, exit 0 |
| SIGTERM lifecycle, run 1 | **PASS**, exit 0 |
| SIGTERM lifecycle, run 2 | **PASS**, exit 0 |
| SIGTERM lifecycle, run 3 | **PASS**, exit 0 |
| Repeated-SIGTERM stress | **PASS**, 40/40 runs logged signal/stopping/stopped and exited 0 |
| Invalid lifecycle transitions | **PASS**, run-before-initialize, double initialize, and double run rejected |
| Ownership/lifecycle guards | **PASS**, wrong-thread run rejected and repeated shutdown succeeded |
| Remaining service process | **NONE OBSERVED** |

Every background lifecycle test used a five-second watchdog to prevent a
failed service from hanging the validation.

`cppcheck` and `clang-tidy` completed for both service translation units with
no findings. Doxygen report generation was **NOT RUN — TOOL UNAVAILABLE**. A
manual public-API coverage review confirmed lifecycle, ownership,
preconditions, failure, cleanup, repeatability, and thread-safety contracts on
the public class, enum, constructor, destructor, and methods.

## Buildroot package and image validation

The project adds the `BR2_PACKAGE_CAMSTREAM_SERVICE` package and enables it in
`beaglebone_defconfig`. It requires a C++ runtime and thread-capable toolchain,
and uses Buildroot's out-of-source `cmake-package` integration. Package target
ownership is explicit:

```text
CAMSTREAM_BUILD_CAPTURE=OFF
CAMSTREAM_BUILD_GST_TEST=OFF
CAMSTREAM_BUILD_SERVICE=ON
```

The capture and GStreamer diagnostic packages now explicitly set
`CAMSTREAM_BUILD_SERVICE=OFF`, preventing duplicate service builds or installs.
No GStreamer dependency or init script was added to the service package.

The pinned Buildroot 2026.02.3 `utils/check-package -b` run covered every
changed `Config.in` and `.mk` file. Its final result was:

```text
64 lines processed
0 warnings generated
exit status 0
```

The effective configuration was regenerated from the project BeagleBone
defconfig and contained all four package selections:

```text
BR2_PACKAGE_CAMSTREAM_CAPTURE=y
BR2_PACKAGE_CAMSTREAM_GST_TEST=y
BR2_PACKAGE_CAMSTREAM_SERVICE=y
BR2_PACKAGE_CAMSTREAM_VIDEO=y
```

The targeted `camstream-service-dirclean` and `camstream-service` build passed,
followed by a successful incremental final image build. No full Buildroot
output clean was performed.

## Focused artifact audit

The target tree and regenerated `rootfs.tar` contain:

- `/usr/bin/camstream-capture`
- `/usr/bin/camstream-gst-test`
- `/usr/bin/camstream-service`
- `/lib/modules/6.18.1/updates/camstream_video.ko`

`camstream-service` was inspected as a stripped, 32-bit little-endian ARM
EABI5 PIE using the hard-float ABI and `/lib/ld-linux-armhf.so.3`. Its required
`libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`, and loader were
present in the target filesystem. It contains neither RPATH nor RUNPATH. A
focused strings scan found no CamStream repository path, external workspace
path, or Buildroot source/output path in the service executable. This is not a
whole-root-filesystem sanitization claim.

The retained artifact hashes for this checkpoint are:

| Artifact | SHA-256 |
| --- | --- |
| `target/usr/bin/camstream-service` | `594651c761e9acae9c2b368fcafad37f61a4a66d22ca359c6554cbd54268916a` |
| `images/rootfs.tar` | `dbf923a69ba539f6d033d1e5d76cfe7c3ec44c6b12118907f45b01b54e5f72db` |
| `images/sdcard.img` | `5310b5399d553c510327bfd8925c8e951858b3cd10751826bf22e4490886fb93` |

## BBB service lifecycle smoke test

The project owner supplied and accepted real BBB runtime evidence for the
cross-built executable copied to `/tmp/camstream-service`. No BBB test was
rerun during documentation closure.

### Runtime dependencies and CLI

`ldd /tmp/camstream-service` resolved every runtime library, contained no
`not found` entry, and reported the ARM hard-float loader
`/lib/ld-linux-armhf.so.3`. The BBB rootfs did not contain the optional `file`
diagnostic command; this is not a service or artifact failure. The 32-bit ARM
EABI5 hard-float identity was already verified from the Buildroot host
artifacts.

| BBB command or behavior | Accepted result |
| --- | --- |
| `/tmp/camstream-service --help` | Usage printed; exit 0 |
| `/tmp/camstream-service --invalid-option` | Controlled argument error and usage; exit 2 |
| `/tmp/camstream-service unexpected` | Controlled positional-argument error and usage; exit 2 |

### Signal shutdown and repeatability

The accepted SIGINT run produced the complete lifecycle:

```text
Camera service starting
Camera service initialized
Camera service running
SIGINT received
Camera service stopping
Camera service stopped
```

It exited 0, and no `camstream-service` process remained. The accepted SIGTERM
run produced the equivalent lifecycle with `SIGTERM received`, exited 0, and
completed normal deterministic shutdown.

Three additional bounded SIGTERM lifecycle runs each started successfully,
reached Running, received SIGTERM, entered Stopping, reached Stopped, and
exited 0. No service process remained after the three runs. BusyBox shell
messages such as `[1]+ Done`, `[2]+ Done`, and `[3]+ Done` were job-control
notifications, not service errors.

### Acceptance matrix

| Stage 8.1 acceptance check | Result |
| --- | --- |
| BBB runtime dependency resolution | **PASS** |
| Help behavior | **PASS** |
| Invalid option and positional argument handling | **PASS** |
| SIGINT shutdown | **PASS** |
| SIGTERM shutdown | **PASS** |
| Three-run repeated start/stop lifecycle | **PASS** |
| Process cleanup after shutdown | **PASS** |
| Application crash or hang | **NONE OBSERVED** in the bounded smoke tests |

This evidence closes only the Stage 8.1 foreground service lifecycle. It does
not demonstrate memory-leak freedom, kernel Oops/BUG absence, production
readiness, long-duration reliability, init-system integration, camera access,
V4L2 capture, GStreamer ownership, IPC, network streaming, or automatic
recovery.

Stage 8.1 is **COMPLETE**. Stage 8.2 is **NEXT — Camera Service owns and
controls GStreamer pipeline**; that behavior remains unimplemented.

## Explicitly out of scope

- Camera opening or V4L2 capture
- GStreamer pipeline ownership
- IPC or network streaming
- Recording
- Init-system integration or daemonization
- Automatic restart or recovery
