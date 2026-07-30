# Stage 8.0 — CamStream Userspace CMake Foundation

## Checkpoint status

- Validation date: 2026-07-30
- Branch: `stage/08-userspace-cmake-foundation`
- Base commit: `e916cbf` (`stage 7: complete GStreamer integration`)
- Stage 8.0 host validation: **PASS**
- Stage 8.0 Buildroot validation: **PASS**
- Stage 8.0 BBB behavior-preservation smoke test: **PASS**
- Stage 8.0: **COMPLETE**
- Stage 8.1: **NEXT**

Stage 8.0 is intended as a behavior-preserving build-system migration. Source
equivalence, host-tested CLI behavior, Buildroot integration, and the accepted
BBB behavior-preservation smoke test passed. The migration does not
intentionally add or change camera-service, IPC, networking, recording, V4L2,
GStreamer pipeline, CLI, or runtime functionality.

`STAGE7-USB-01` remains **OPEN / DEFERRED**. This checkpoint does not establish
stable production 30-fps operation, long-duration USB reliability, or
production-ready camera reliability.

## Legacy Makefile baseline

Before replacement, both userspace applications were built with their
existing Makefiles. The default host command used `-O2 -std=c++17 -Wall
-Wextra -Wpedantic` and produced no compiler warnings. It established:

| Baseline property | Result |
| --- | --- |
| `camstream-capture` host build | **PASS** |
| `camstream-gst-test` host build | **PASS** |
| Both `--help` commands | **PASS**, exit 0 |
| Invalid format, missing value, malformed number | Preserved controlled rejection, exit 2 |
| Nonexistent device | Preserved controlled runtime failure, exit 1 |
| Installed binary names | `camstream-capture`, `camstream-gst-test` |

The migrated sources and public GStreamer header were compared with their
pre-migration versions and remained byte-identical. Only the reusable source
and header locations changed. Existing Doxygen API contracts were preserved.
The prior Buildroot packaging contract installed the applications as
`/usr/bin/camstream-capture` and `/usr/bin/camstream-gst-test`; the legacy host
Makefiles themselves did not provide install rules.

## CMake target architecture

The root project uses an out-of-source, target-based CMake structure:

```text
CMakeLists.txt
apps/
├── camstream-capture/
│   ├── CMakeLists.txt
│   └── src/
└── camstream-gst-test/
    ├── CMakeLists.txt
    └── src/
libs/
└── camstream-gstreamer/
    ├── CMakeLists.txt
    ├── include/camstream/gstreamer_pipeline.hpp
    └── src/gstreamer_pipeline.cpp
```

| CMake target | Type and responsibility |
| --- | --- |
| `camstream-capture` | Native V4L2 diagnostic executable |
| `camstream-gstreamer` | Reusable static GStreamer pipeline library |
| `camstream::gstreamer` | Alias for the reusable library |
| `camstream-gst-test` | Diagnostic executable linked to `camstream::gstreamer` |

The build uses CMake 3.16, C++17 without compiler extensions,
`GNUInstallDirs`, target-scoped include directories, compile features,
warning options, and link dependencies. GStreamer is resolved through the
`PkgConfig::GSTREAMER` imported target. The static library introduces no new
CamStream runtime shared-library dependency.

The project target definitions preserve the required language and warning
policy but do not globally hardcode optimization flags. Host developers select
the desired CMake build configuration, while Buildroot supplies the target
optimization and toolchain flags through its generated CMake toolchain file.

The two build-selection options default to `ON` for a normal developer build:

- `CAMSTREAM_BUILD_CAPTURE`
- `CAMSTREAM_BUILD_GST_TEST`

## Local developer build

Ninja was available and used for validation. A normal out-of-source build is:

```sh
cmake -S . -B build-cmake -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-cmake
DESTDIR=/tmp/camstream-install cmake --install build-cmake
```

The repository ignores the specific root build directory `build-cmake/`.
Developers may build only one package-owned target when needed:

```sh
cmake -S . -B build-cmake -G Ninja \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCAMSTREAM_BUILD_CAPTURE=ON \
    -DCAMSTREAM_BUILD_GST_TEST=OFF

cmake -S . -B build-cmake -G Ninja \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCAMSTREAM_BUILD_CAPTURE=OFF \
    -DCAMSTREAM_BUILD_GST_TEST=ON
```

Use separate build directories when switching configurations.

## Host validation evidence

The host build, install, and comparison used temporary directories outside the
repository.

| Check | Result |
| --- | --- |
| CMake configure with Ninja | **PASS** |
| Full CMake build | **PASS**, zero compiler warnings |
| Both executables produced | **PASS** |
| Static `camstream-gstreamer` library produced and linked | **PASS** |
| Temporary `DESTDIR` install | **PASS** |
| Installed paths limited to the two `/usr/bin` applications | **PASS** |
| Capture-only configure/build/install | **PASS**; no GStreamer target built |
| GStreamer-test-only configure/build/install | **PASS** |
| Source-tree build artifacts | **NONE OBSERVED** |

The CMake applications matched the legacy baseline for help output, binary
names, tested CLI syntax, exit status, controlled error output, linked host
libraries, and warning policy. Host checks covered `--help`, invalid format,
missing required value, malformed numeric input, and nonexistent-device
failures. These checks are host behavior evidence only; they are not camera or
BBB runtime validation.

`clang-tidy` completed for all five C++ translation units with no findings.
`cppcheck` reported one existing style suggestion in
`apps/camstream-gst-test/src/main.cpp` about using an STL algorithm; it did not
identify a correctness defect and no behavior-changing cleanup was made.
Doxygen report generation was **NOT RUN** because `doxygen` was unavailable.
A manual public-API coverage review confirmed that the moved public header and
its existing Doxygen contracts were preserved without source changes.

## Buildroot `cmake-package` integration

The `camstream-capture` and `camstream-gst-test` packages now use Buildroot's
`cmake-package` infrastructure and out-of-source package builds. Both packages
use the repository root as their local source and rely on Buildroot's generated
CMake toolchain integration.

- `camstream-capture` enables only `CAMSTREAM_BUILD_CAPTURE`.
- `camstream-gst-test` enables only `CAMSTREAM_BUILD_GST_TEST` and preserves
  the existing GStreamer package dependencies.
- Package symbols and BeagleBone defconfig behavior are unchanged.
- `camstream-video` remains a separate Kbuild kernel-module package.

The pinned Buildroot 2026.02.3 check processed both changed `.mk` files in
BR2_EXTERNAL mode:

```text
32 lines processed
0 warnings generated
exit status 0
```

Targeted `dirclean` and package rebuilds passed for both userspace packages.
No full output-directory clean was performed. The subsequent incremental
final Buildroot image generation passed and produced `rootfs.ext2`,
`rootfs.tar`, and `sdcard.img`.

The reused output tree's informational `.config` provenance string remained
`-g1fec8cb-dirty`, while regenerated BR2_EXTERNAL metadata and the reviewed
Stage 8 working tree identify `e916cbf-dirty`. The package source copies were
compared with the current CMake/application/library sources and matched
byte-for-byte, so no functional source mismatch was observed. Nevertheless,
this incremental result does not independently prove a clean commit-based
reproduction. Regenerating the configuration from a committed project
defconfig remains a future reproducibility check; it does not invalidate the
package, image, artifact, or accepted BBB behavior evidence recorded here.

## Focused target and root-filesystem artifact audit

The final target tree and `rootfs.tar` contained:

- `/usr/bin/camstream-capture`
- `/usr/bin/camstream-gst-test`
- `/lib/modules/6.18.1/updates/camstream_video.ko`
- the required GStreamer runtime libraries and the core, V4L2, JPEG, and video
  conversion plugins

Both applications were inspected as 32-bit little-endian ARM EABI5
executables using the hard-float ABI. Neither application contains RPATH or
RUNPATH entries. A focused scan of the two applications, the CamStream module,
and the required GStreamer libraries/plugins found no embedded CamStream
repository path, external workspace path, Buildroot source path, or Buildroot
output path introduced by this migration. This is a focused project-artifact
result, not a whole-root-filesystem sanitization claim.

The focused artifact hashes retained for this checkpoint are:

| Artifact | SHA-256 |
| --- | --- |
| `images/sdcard.img` | `8b8420a2f41fb9a514791dbab7c086a808a46c6566ec9b1e0530906683d3b656` |
| `images/rootfs.tar` | `8dc58d6f00d413693377a906cd6da1f3a072dd12ff1c1fdc200215f7495aad6e` |
| `target/usr/bin/camstream-capture` | `3d53c851645f48c0cada30a14dc7589954c70bba85b71c858fe1de9f32adf846` |
| `target/usr/bin/camstream-gst-test` | `8fad6aaf5cc85a12c6a82859b580ad7cee2c5d7bc6ae2bf86f9fb9c471b9e663` |

## BBB behavior-preservation smoke-test evidence

The project owner supplied and accepted the following BBB runtime results.
They validate the CMake-migrated userspace behavior at smoke-test scope; they
do not establish sustained throughput, long-duration USB reliability, or
production readiness.

### Runtime dependencies and CLI behavior

`ldd /tmp/camstream-gst-test` resolved every required library with no `not
found` dependency. The executable used the ARM hard-float loader:

```text
/lib/ld-linux-armhf.so.3
```

| CLI check | Result |
| --- | --- |
| `camstream-capture --help` | **PASS**, exit 0 |
| `camstream-gst-test --help` | **PASS**, exit 0 |
| Invalid GStreamer format | **PASS**, controlled error, exit 2 |

### V4L2 node selection

The C270 capture node was identified by driver, capabilities, and formats, not
by assuming a fixed device number:

| Observed node in this boot | Identity and disposition |
| --- | --- |
| `/dev/video0` | `uvcvideo`; Device Caps included Video Capture and Streaming; advertised YUYV and MJPG; selected for C270 runtime checks |
| `/dev/video1` | Metadata Capture only; excluded from runtime acceptance |
| `/dev/video2` | Synthetic `camstream-video` node for this boot only; not a fixed ABI |

### Accepted finite pipeline runs

| Source and path | Requested mode | Buffers and sink | Runtime result |
| --- | --- | --- | --- |
| Synthetic V4L2 capture | YUY2 640x480 at requested 30 fps | 120, `fakesink sync=true` | Reached PLAYING, received EOS, exit 0, elapsed 4.53 s |
| Logitech C270 raw | YUY2 320x240 at requested 30 fps | 120, `fakesink sync=false` | Reached PLAYING, received EOS, exit 0, elapsed 5.04 s |
| Logitech C270 MJPEG decode | MJPEG 640x480 at requested 30 fps through the existing decode pipeline | 120, `fakesink sync=false` | Reached PLAYING, received EOS, exit 0, elapsed 5.10 s |

These elapsed times are recorded test conditions only. They do not establish
stable delivered 30 fps. No application crash or fatal GStreamer error was
observed.

### Cleanup and evidence limits

`rmmod camstream_video` returned exit 0, and the module was absent from
`lsmod` afterward. No bounded kernel-log evidence was supplied, so this
checkpoint makes no claim about the absence of a kernel Oops, BUG, WARNING, or
other kernel failure signature.

The isolated earlier line `synthetic exit=2` was not part of the accepted
synthetic run and is explicitly excluded from Stage 8.0 evidence.

## Final acceptance

| Gate | Result |
| --- | --- |
| Host CMake validation | **PASS** |
| Buildroot package and image validation | **PASS** |
| BBB behavior-preservation smoke test | **PASS** |
| Stable delivered production 30 fps | **DEFERRED — STAGE7-USB-01** |
| Long-duration USB reliability | **DEFERRED — STAGE7-USB-01** |
| Production-ready camera reliability | **DEFERRED — STAGE7-USB-01** |
| Bounded kernel Oops/BUG absence check | **NOT TESTED** |

Stage 8.0 is **COMPLETE**. `STAGE7-USB-01` remains **OPEN / DEFERRED**. Stage
8.1 is **NEXT**.

Doxygen report generation remains **NOT RUN — TOOL UNAVAILABLE**; the manual
public-API coverage review recorded above passed.
