# Stage 8.2 — GStreamer Service Integration

## Checkpoint status

- Validation date: 2026-08-01
- Branch: `stage/08.2-gstreamer-service-integration`
- Base commit: `32eeffc` (`stage 8.1: add camera service skeleton`)
- Stage 8.2 host validation: **PASS**
- Stage 8.2 Buildroot validation: **PASS**
- Stage 8.2 BBB runtime validation: **PASS**
- Long-duration reliability: **NOT TESTED — NOT CLAIMED**
- Production readiness: **NOT TESTED — NOT CLAIMED**
- Stage 8.2: **COMPLETE**

`STAGE7-USB-01` remains **OPEN / DEFERRED**. This checkpoint does not establish
stable delivered production 30 fps, long-duration USB reliability, the cause
of historical USB resets, or production-ready camera reliability.

## Scope

Stage 8.2 makes the foreground `camstream-service` process own and control the
existing reusable GStreamer implementation:

```text
camstream-service
  -> CameraService
       -> owns GstreamerPipeline
            -> owns GstPipeline and GstBus
```

`camstream-gst-test` remains the finite diagnostic application and continues
to reuse the same library. No GStreamer source is copied into the service and
no second pipeline implementation is introduced.

Only standard GStreamer elements are used. Custom GStreamer elements,
`libcamstream-v4l2` extraction, IPC, network streaming, recording, init-system
integration, configuration files, daemonization, and automatic USB recovery
are outside this checkpoint.

## Implementation summary

- `CameraService` owns and controls one reusable `GstreamerPipeline`.
- `buffers=0` selects continuous service operation; a positive count selects
  a finite validation run whose expected completion is EOS.
- The service polls its synchronous signal descriptor and the borrowed GstBus
  descriptor in one application event loop.
- Bus messages are drained nonblockingly and classified as continue, EOS, or
  error; warnings continue operation and meaningful pipeline state changes are
  logged.
- SIGINT, SIGTERM, expected finite EOS, initialization failures, and runtime
  failures all follow controlled shutdown paths.
- The existing finite `camstream-gst-test` diagnostic remains a separate
  consumer of the same reusable library.
- CMake and Buildroot preserve exclusive target/package ownership and install
  only the executable owned by each package.

## Architecture and ownership

The public `camstream::GstreamerPipeline` remains non-copyable and non-movable
and continues to support the diagnostic sequence `build()`, `start()`,
`wait()`, `stop()`. Stage 8.2 adds:

- access to the borrowed GStreamer bus poll descriptor after `build()`;
- nonblocking draining of every currently queued bus message through
  GStreamer bus APIs; and
- typed `Continue`, `EndOfStream`, and `Error` outcomes.

The poll descriptor is never read or closed by the service. Readiness causes
the service to drain messages with `gst_bus_pop()` until the queue is empty,
avoiding a busy loop or a message left behind after one notification. The
blocking diagnostic `wait()` and service-side draining are alternative bus
consumption modes and must not run concurrently on one pipeline.

The library owns the `GstPipeline` and `GstBus`, releases every owned
`GstMessage`, frees parsed `GError` and debug strings, transitions the pipeline
to `GST_STATE_NULL`, and then unreferences owned GStreamer objects. `stop()` is
deterministic and idempotent, including after partial build or startup
failure.

The service event sources converge on one application-owned poll loop:

```text
signal fd
    \
     -> CameraService poll loop
    /
GstBus fd
```

Ownership remains explicit:

```text
CameraService
  -> owns GstreamerPipeline
       -> owns GstElement pipeline
       -> owns/accesses GstBus and exposes its borrowed poll descriptor
```

### Pipeline modes and topologies

`GstreamerPipelineConfig::buffer_count` has explicit mode semantics:

- `0`: continuous service mode; no finite `v4l2src num-buffers` limit;
- greater than `0`: finite validation mode; EOS is expected after the
  requested buffer count.

The diagnostic application retains its existing contract: `--buffers` is
required and must be positive. Its finite wait timeout and accepted EOS path
are unchanged.

The reused topologies remain:

```text
YUY2:  v4l2src -> video/x-raw,format=YUY2 capsfilter -> fakesink
MJPEG: v4l2src -> image/jpeg capsfilter -> jpegdec -> videoconvert -> fakesink
```

No custom element is present in either path.

### Camera Service lifecycle and event loop

The service lifecycle is:

```text
Created
  -> Initialized
  -> PipelineReady
  -> Running
  -> StopRequested or Failed
  -> Stopped
```

Initialization configures the synchronous `signalfd`, builds the owned
pipeline, and borrows the bus poll descriptor. `run()` starts the pipeline and
repeatedly blocks in `poll()` over the signal and bus descriptors. The service
creates no application-owned GStreamer worker thread, asynchronous signal
handler, or busy-poll loop; GStreamer may still use its own internal streaming
threads.

| Event | Service behavior |
| --- | --- |
| SIGINT or SIGTERM | Log the signal, request normal stop, stop the pipeline, exit 0 |
| Finite-mode EOS | Expected normal completion, deterministic stop, exit 0 |
| Continuous-mode EOS | Unexpected runtime failure, deterministic stop, exit 1 |
| GStreamer warning | Parse and log source/message/debug details, continue |
| GStreamer error | Parse and log controlled failure details, enter Failed, stop, exit 1 |
| Pipeline state change | Log meaningful pipeline-level transitions only |
| Finite source stalls before EOS | Absolute nominal-duration plus 120-second deadline expires, deterministic stop, exit 1 |

Shutdown stops the pipeline before closing owned service descriptors and is
idempotent. The pipeline cannot remain PLAYING after service shutdown.

The normal shutdown sequence is:

```text
SIGINT/SIGTERM or expected finite EOS
  -> service requests shutdown
  -> pipeline transitions toward GST_STATE_NULL
  -> GStreamer resources are released
  -> service exits deterministically
```

An error follows the same cleanup sequence but returns the runtime-failure
exit status.

### CLI and exit contract

```text
camstream-service
  [--device <path>]
  [--format yuy2|mjpeg]
  [--width <pixels>]
  [--height <pixels>]
  [--fps <rate>]
  [--buffers <count>]
  [--sync true|false]
  [--help]
```

Defaults are `/dev/video0`, `yuy2`, 640x480, 30 fps, zero buffers
(continuous), and `sync=false`. `/dev/video0` is only a convenience default;
device numbering is dynamic and is not a C270 ABI.

Duplicate options, missing values, malformed or overflowing numbers, zero
width/height/fps, unsupported format/sync values, unknown options, and
positional arguments are rejected. `--buffers 0` is valid only because it
selects continuous service mode.

| Condition | Exit status |
| --- | ---: |
| Help, normal signal shutdown, expected finite EOS | 0 |
| Initialization, pipeline, transition, or runtime failure | 1 |
| Invalid CLI arguments | 2 |

## CMake and Buildroot ownership

The target dependency graph is:

```text
camstream-gstreamer
  <- camstream-gst-test
  <- camstream-service
```

The root CMake project builds `camstream-gstreamer` whenever either consumer
is enabled. `camstream-capture` remains independent. The project preserves
C++17, target-scoped warnings and includes, `GNUInstallDirs`, and the public
include form `<camstream/gstreamer_pipeline.hpp>`.

Buildroot target ownership remains exclusive:

| Package | Enabled project target |
| --- | --- |
| `camstream-capture` | capture only |
| `camstream-gst-test` | GStreamer diagnostic only |
| `camstream-service` | Camera Service only |

The service package enables `CAMSTREAM_BUILD_SERVICE=ON` and explicitly
disables capture and diagnostic targets. It depends only on the existing
GStreamer core, base, good, JPEG, and V4L2 packages needed by the reused
topologies. No unrelated plugin was added.

## Host validation

Ninja was available and used for out-of-source builds in temporary directories
outside the repository.

| Check | Result |
| --- | --- |
| Full CMake configure/build | **PASS — ZERO COMPILER WARNINGS** |
| Additional `-Wconversion -Wsign-conversion -Wshadow -Wformat=2` syntax check | **PASS — ZERO WARNINGS** |
| Temporary `DESTDIR` install | **PASS**, all three userspace binaries installed under `/usr/bin` |
| Service-only build/install | **PASS**, only `camstream-service` installed |
| Diagnostic-only build/install | **PASS**, only `camstream-gst-test` installed |
| Service links reusable GStreamer library | **PASS** |
| Diagnostic links reusable GStreamer library | **PASS** |
| Service help | **PASS**, exit 0 |
| Invalid format, duplicate option, malformed number | **PASS**, controlled exit 2 |
| Zero width, invalid sync, positional input | **PASS**, controlled exit 2 |
| Nonexistent device in continuous and finite modes | **PASS**, controlled exit 1 without crash or hang |
| Diagnostic help | **PASS**, exit 0 |
| Diagnostic invalid format and zero buffers | **PASS**, existing controlled exit 2 preserved |
| Diagnostic nonexistent device | **PASS**, controlled exit 1 |
| Source-tree build artifacts | **NONE OBSERVED** |

No host camera runtime success is claimed. `clang-tidy` completed on the three
changed translation units with no findings. `cppcheck` completed with no
correctness finding; its only output was an informational note about the
number of preprocessor configurations checked. Doxygen report generation was
**NOT RUN — TOOL UNAVAILABLE**. Manual public-API Doxygen review confirmed
coverage of pipeline ownership, borrowed descriptor lifetime, mutually
exclusive bus-consumption modes, service lifecycle, preconditions, failure
semantics, and deterministic cleanup.

The bounded finite-stall deadline is implemented and source-reviewed. Its
runtime trigger is **NOT TESTED** because no suitable host V4L2 source was
available that could enter PLAYING and then stall before EOS. The current BBB
image also has no deterministic stall-injection fixture, so a reproducible
fault-injection procedure remains to be defined before this path can be
runtime-validated.

## Buildroot and artifact validation

The pinned Buildroot 2026.02.3 checker was run in BR2_EXTERNAL mode for every
modified `Config.in` and `.mk` file:

```text
33 lines processed
0 warnings generated
exit status 0
```

The effective BeagleBone configuration preserved all four CamStream packages
and the required GStreamer packages. Targeted `dirclean` and rebuilds passed
for `camstream-service` and `camstream-gst-test`, followed by a successful
incremental final image build. No full output-tree clean was performed.

The target tree and regenerated `rootfs.tar` contain:

- `/usr/bin/camstream-capture`
- `/usr/bin/camstream-gst-test`
- `/usr/bin/camstream-service`
- `/lib/modules/6.18.1/updates/camstream_video.ko`
- GStreamer core plus the core-elements, V4L2, JPEG, and video-conversion
  plugins required by the two pipeline topologies

The changed `camstream-service` and regression `camstream-gst-test` binaries
are 32-bit little-endian ARM EABI5 PIE executables using hard-float VFP calling
conventions and `/lib/ld-linux-armhf.so.3`. Both declare their GStreamer/GLib
runtime dependencies, contain neither RPATH nor RUNPATH, and passed focused
string scans for CamStream repository paths, the external workspace path,
Buildroot source paths, and the output-tree path. This is focused changed-
artifact evidence, not whole-rootfs sanitization.

| Artifact | SHA-256 |
| --- | --- |
| `target/usr/bin/camstream-service` | `ac85802d634ca780dabff433d04c4763a189e7338793c16a7d89a2fd9cceb6e4` |
| `target/usr/bin/camstream-gst-test` | `ffde666878ed192c971612d6bc78f9270103659cb3e2c3a940e84518fb53712b` |
| `images/rootfs.tar` | `5025d08ac7b34590086443ddbcfaa94f4f7c1e3d5ff83000655bf2d92b1c875a` |
| `images/sdcard.img` | `3bb8a293830969d0413df6f3434aea00b57e5d96369928e1190834bf1ac206f3` |

## BBB runtime validation

The project owner supplied and accepted functional BBB runtime evidence for
the Stage 8.2 image. The runtime date was not supplied. Device nodes were
identified by driver and capture capability during that boot; their numeric
assignments are not treated as stable interfaces.

Runtime dependency checks resolved the libraries required by both
`camstream-service` and `camstream-gst-test`. GStreamer element discovery
confirmed `v4l2src`, `fakesink`, `jpegdec`, and `videoconvert` were available.

The C270 Video Capture node appeared as `/dev/video0` during this boot.
`/dev/video1` exposed metadata capture only and was excluded. The synthetic
driver node appeared as `/dev/video2` during the synthetic tests.
**`/dev/video2` is not a stable ABI or persistent device assignment.** The
same dynamic-device rule applies to the observed C270 node number.

### Accepted service and diagnostic runs

| Runtime check | Accepted result |
| --- | --- |
| `camstream-service --help` | **PASS**, exit 0 |
| Invalid, duplicate, and malformed service CLI values | **PASS**, controlled exit 2 |
| Nonexistent device | **PASS**, detailed controlled GStreamer error, exit 1 |
| Existing `camstream-gst-test` regression | **PASS**, EOS received, exit 0 |
| Synthetic finite service pipeline | **PASS**, EOS received, exit 0 |
| Synthetic continuous service pipeline | **PASS**, SIGINT shutdown, exit 0 |
| C270 finite YUY2 service pipeline | **PASS**, EOS received, exit 0 |
| C270 finite MJPEG decode service pipeline | **PASS**, EOS received, exit 0 |
| C270 continuous service pipeline | **PASS**, SIGTERM shutdown, exit 0 |
| Process cleanup after service shutdown | **PASS**, no service process remained |
| Synthetic module unload | **PASS**, clean unload accepted |

The finite and continuous runs demonstrate the service-side nonblocking bus
path, expected finite EOS handling, and signal-driven continuous shutdown.
The retained `camstream-gst-test` result separately demonstrates that its
blocking diagnostic path remains functional.

## Accepted evidence

| Category | Result | Evidence boundary |
| --- | --- | --- |
| Host validation | **PASS** | Existing recorded out-of-source builds, installs, CLI checks, warnings, and static-analysis results |
| Buildroot validation | **PASS** | Existing recorded `check-package`, targeted package rebuilds, incremental image generation, and artifact audit |
| BBB runtime validation | **PASS** | Owner-accepted dependency, CLI, synthetic, C270, EOS, signal-shutdown, cleanup, and module-unload evidence |
| Long-duration reliability | **NOT TESTED — NOT CLAIMED** | No sustained-duration acceptance evidence supplied |
| Production readiness | **NOT TESTED — NOT CLAIMED** | Functional checkpoint evidence only |

## Known limitations and excluded claims

`STAGE7-USB-01` remains **OPEN / DEFERRED**. C270 USB resets and inconsistent
delivered frame throughput have been observed on the BBB USB host path. Stage
8.2 did not perform a root-cause investigation, and this report does not
attribute the resets to GStreamer, the service, or another component.

The checkpoint does not claim:

- stable delivered production 30 fps;
- long-duration C270 or USB reliability;
- production readiness;
- memory-leak freedom;
- absence of kernel Oops/BUG or a general kernel-health guarantee, because no
  bounded kernel-log evidence was supplied for this closure; or
- runtime exercise of the bounded finite-stall fault path, which remains
  source-reviewed but **NOT TESTED**.

Custom plugins, reusable V4L2-library extraction, IPC, network streaming,
recording, init-system integration, configuration files, and automatic USB
recovery remain outside Stage 8.2. Stage 8.3 — Reusable V4L2 Userspace Library
— is **NEXT** and has not started.

## Reviewer result

- Reviewer: `validation-reviewer`
- Blocker: **NONE**
- Major: **NONE**
- Minor: local-only `AGENTS.md` still identifies BBB runtime validation as
  pending; tracked checkpoint acceptance is unaffected, and the file remains
  excluded from Git and outside this documentation-only closure scope.
- Verdict: **APPROVED**

## Final verdict

Stage 8.2 functional acceptance criteria are satisfied by the existing host,
Buildroot, and owner-accepted BBB evidence. The validation review found no
blocker or major issue. **Stage 8.2 is COMPLETE and its closure diff is
APPROVED for owner review.**
