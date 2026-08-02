# Debugging and Validation Guide

This guide selects a small toolset by defect class. Commands are examples for future owner-run validation; their presence
does not mean they were executed for Stage 8.3.

## Quick symptom-to-tool map

| Symptom | Start with |
| --- | --- |
| Build warning | Compiler output |
| Memory corruption | AddressSanitizer |
| Memory or resource leak | Valgrind Memcheck |
| Userspace crash | GDB and a core dump |
| File, `.so`, or ioctl failure | `strace` |
| Kernel or driver error | `dmesg` |
| Kernel memory error | KASAN |
| Deep kernel debugging | KGDB |

## Primary userspace tools

### Compiler warnings

**What it is:** diagnostics emitted while compiling with options such as `-Wall`, `-Wextra`, and `-Wpedantic`.

**Detects:** unused code, suspicious conversions, missing declarations, format mismatches, and other statically visible
problems supported by the compiler.

**When to use:** on every project-owned C or C++ change before runtime testing.

**Example:**

```sh
cmake -S . -B /tmp/camstream-build \
  -DCAMSTREAM_BUILD_CAPTURE=OFF \
  -DCAMSTREAM_BUILD_GST_TEST=OFF \
  -DCAMSTREAM_BUILD_SERVICE=OFF \
  -DCAMSTREAM_BUILD_CAMERA_PPI=ON \
  -DCAMSTREAM_BUILD_CAMERA_BACKEND_SIMULATED=ON \
  -DCAMSTREAM_BUILD_CAMERA_TEST=ON
cmake --build /tmp/camstream-build --parallel
```

**Interpretation:** PASS means the intended targets compile with zero project-owned warnings. A warning or compile error
is FAIL until understood and corrected or explicitly justified by project policy.

### AddressSanitizer and UndefinedBehaviorSanitizer

**What they are:** compiler instrumentation that checks userspace memory accesses and selected undefined behavior while
the instrumented executable runs.

**Detects:** out-of-bounds access, use after free, invalid frees, some leaks, integer or alignment undefined behavior, and
related runtime faults covered by the enabled sanitizer set.

**When to use:** after ownership, buffer, parsing, loader, or lifecycle changes on a host-compatible path.

**Example:**

```sh
cmake -S . -B /tmp/camstream-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCAMSTREAM_BUILD_CAPTURE=OFF \
  -DCAMSTREAM_BUILD_GST_TEST=OFF \
  -DCAMSTREAM_BUILD_SERVICE=OFF \
  -DCAMSTREAM_BUILD_CAMERA_PPI=ON \
  -DCAMSTREAM_BUILD_CAMERA_BACKEND_SIMULATED=ON \
  -DCAMSTREAM_BUILD_CAMERA_TEST=ON
cmake --build /tmp/camstream-asan
/tmp/camstream-asan/apps/camstream-camera-test/camstream-camera-test \
  --backend /tmp/camstream-asan/backends/camera/simulated/libcamstream-camera-backend-simulated.so \
  --frames 8
```

**Interpretation:** PASS requires the expected application result and no sanitizer diagnostic. Any sanitizer report is
FAIL until isolated. ASan and UBSan do not prove that all bugs are absent, and userspace ASan does not instrument the
kernel.

### Valgrind Memcheck

**What it is:** a dynamic userspace instrumentation tool that observes heap use and resource lifetime without requiring
the same compiler instrumentation as ASan.

**Detects:** invalid reads and writes, use of uninitialized data, invalid frees, and heap allocations still reachable or
lost at exit. File descriptors need separate observation or appropriate Valgrind options.

**When to use:** for loader, repeated lifecycle, ownership, and cleanup paths, especially after sanitizer smoke tests.

**Example:**

```sh
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes \
  /tmp/camstream-build/apps/camstream-camera-test/camstream-camera-test \
  --backend /tmp/camstream-build/backends/camera/simulated/libcamstream-camera-backend-simulated.so \
  --frames 8
```

**Interpretation:** PASS requires the expected exit status, no invalid access, no definitely or indirectly lost project
allocation, and no unexpected open descriptor. A report needs ownership-based triage before it is called FAIL or a
third-party suppression. Valgrind does not prove that all bugs are absent.

`vgdb` is a supporting mode: run Valgrind with `--vgdb=yes --vgdb-error=0`, then connect GDB with
`target remote | vgdb`. It connects GDB to a process running under Valgrind; it is not another leak detector.

### GDB and core dumps

**What it is:** an interactive debugger for source-level state, stack frames, variables, threads, and post-mortem core
files.

**Helps investigate:** crashes, assertions, unexpected control flow, invalid state, and the location at which a process
received a fatal signal.

**When to use:** when an application crashes or a reproducible state transition needs inspection.

**Example:**

```sh
ulimit -c unlimited
gdb /tmp/camstream-build/apps/camstream-camera-test/camstream-camera-test core
```

Useful initial commands are `thread apply all backtrace full`, `frame`, `info locals`, and `print expression`.

**Interpretation:** there is no generic GDB PASS. A debugging session succeeds when it identifies or rules out a concrete
hypothesis using a reproducible stack and state. GDB is not a leak detector.

`gdbserver` is a supporting remote mode for target userspace debugging. Start it on the target with
`gdbserver :2345 /usr/bin/application ...`, then connect the matching cross-GDB from the host. Keep symbols for the exact
target binary.

### strace

**What it is:** a tracer for userspace system calls, return values, signals, file paths, and descriptor activity.

**Helps investigate:** `dlopen()` path failures, missing `.so` files, permissions, device opens, ioctl errors, poll
timeouts, signals, and unexpected descriptor closure.

**When to use:** when an application reports a loader, file, device, or syscall-layer error but does not crash.

**Example:**

```sh
strace -f -e trace=openat,close,mmap,munmap,poll,read,write \
  /tmp/camstream-build/apps/camstream-camera-test/camstream-camera-test \
  --backend /tmp/camstream-build/backends/camera/simulated/libcamstream-camera-backend-simulated.so \
  --frames 2
```

**Interpretation:** PASS means the traced calls match the expected lifecycle and return successfully. Negative errno
results or descriptor imbalance are evidence to investigate. `strace` observes syscalls and signals; it is not a heap
leak detector.

## Primary kernel tools

### dmesg

**What it is:** access to the kernel ring buffer containing driver, USB, V4L2, warning, and fault messages.

**Helps investigate:** enumeration failures, disconnects, driver probe errors, kernel warnings, Oops, BUG reports, and
resource failures.

**When to use:** before and after a bounded target test involving hardware, kernel modules, or device nodes.

**Example:**

```sh
dmesg | grep -Ei 'camstream|uvcvideo|usb|WARNING|Oops|BUG|use-after-free|list corruption'
```

**Interpretation:** PASS for a bounded test requires no new relevant fatal kernel message caused by that test. Historical
messages must be separated by timestamps or before/after capture. An observed USB reset is evidence, not automatic proof
of a GStreamer, application, or driver root cause.

### KASAN

**What it is:** kernel compiler instrumentation for detecting invalid kernel memory accesses.

**Detects:** kernel use after free, out-of-bounds accesses, and related memory-safety violations covered by the selected
KASAN mode.

**When to use:** for risky kernel memory, videobuf2 ownership, workqueue, locking, or teardown changes in a dedicated
debug kernel—not for ordinary userspace Camera PPI code.

**Example:**

```sh
grep -E 'CONFIG_KASAN(=|_)' /path/to/kernel-build/.config
dmesg | grep -Ei 'KASAN|BUG: KASAN|use-after-free|out-of-bounds'
```

**Interpretation:** PASS requires KASAN to be enabled in the tested kernel and no relevant report during the bounded test.
A KASAN report is FAIL and must retain its complete stack evidence. Userspace ASan does not instrument the kernel.

### KGDB

**What it is:** a source-level kernel debugger using a configured kernel and a host GDB connection over a supported I/O
transport.

**Helps investigate:** deep kernel control flow, breakpoints, data structures, locks, and hangs that logs alone cannot
resolve.

**When to use:** only in a controlled kernel-debug session with matching unstripped `vmlinux`, symbols, KGDB kernel
configuration, and a dedicated transport. KGDB is for kernel debugging, not userspace Camera PPI code.

**Example:**

```sh
${CROSS_COMPILE}gdb /path/to/kernel-build/vmlinux
(gdb) target remote /dev/ttyUSB0
```

Entering KGDB may stop the target and can disrupt its console or service behavior; follow the board-specific debug plan.

**Interpretation:** KGDB has no generic PASS. It is successful when a controlled session establishes a breakpoint or
captures the kernel state needed to prove or reject the current hypothesis.

## Risk-based validation workflow

Per change:

- format the affected files;
- require zero project-owned compiler warnings;
- run a focused functional smoke test.

Per stage:

- exercise relevant successful functional paths;
- exercise relevant failure paths;
- use sanitizers where memory or ownership is involved;
- use Valgrind where lifecycle or resources are involved;
- perform target validation when behavior is platform-dependent.

When diagnosing an actual failure, select GDB, `vgdb`, `strace`, or KGDB according to the failing layer. Not every tool
must run for every change; validation depth follows risk and the claims being accepted.
