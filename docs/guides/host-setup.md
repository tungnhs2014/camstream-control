# Host setup and verification

## Purpose and scope

Stage 1 establishes a repeatable Ubuntu development-host baseline for CamStream Control. It documents manual package installation and verifies host tools only. It does not download, configure, or build Buildroot, and it does not verify a BeagleBone Black, a camera, boot flow, or target behavior.

The verified host baseline is Ubuntu 22.04.5 LTS on `x86_64`. Other Ubuntu releases may use different package versions or names and must be checked before use. This guide does not assert compatibility with an unselected Buildroot, Linux kernel, U-Boot, or toolchain version.

## Resource recommendations

For this project, plan for at least 8 GiB of RAM and 50 GiB of free workspace storage. More memory and disk space are helpful for later source trees, build outputs, debug symbols, and test artifacts. These are CamStream Control project recommendations, not universal Buildroot requirements.

## Tool categories

- **Required:** needed to run the Stage 1 verification or planned baseline workflow. A missing required tool makes `check-environment.sh` fail.
- **Recommended:** useful for implementation, debugging, or quality work. A missing recommended tool is reported but does not fail Stage 1.
- **Optional:** convenience tooling with an existing alternative or no Stage 1 dependency. Missing optional tools do not fail Stage 1.

`picocom` is optional because `minicom` covers the current serial-console need. `ltrace` is optional because `gdb`, `strace`, and `valgrind` cover the current debugging baseline. Membership in `dialout` is deferred to Stage 2: verify it only after a USB-to-UART adapter and a serial device node are available.

## Manual package installation

Review each group before running it. Commands below are intentionally manual; there is no automatic setup script.

### Basic development and C/C++ build tools

```sh
sudo apt update
sudo apt install build-essential git cmake ninja-build python3 perl pkg-config
git --version && gcc --version && g++ --version && make --version
cmake --version && ninja --version && python3 --version && perl -v
pkg-config --version
```

### Buildroot host utilities

```sh
sudo apt install patch gzip bzip2 xz-utils tar cpio unzip rsync file bc \
  findutils gawk wget sed diffutils
for tool in patch gzip bzip2 xz tar cpio unzip rsync file bc find awk wget sed diff; do
  command -v "$tool" || exit 1
done
```

### Kernel, U-Boot, menuconfig, and Device Tree tools

```sh
sudo apt install flex bison libncurses-dev libssl-dev libelf-dev \
  device-tree-compiler
flex --version && bison --version && dtc --version
pkg-config --modversion ncursesw openssl libelf
```

`libncurses-dev` supports the planned menuconfig workflow; it is not a universal requirement for every Buildroot build. `libssl-dev` is planned for later kernel/U-Boot development work. Exact downstream consumers and compatible versions remain decisions for later stages.

### UART, USB, SSH, and SCP tools

```sh
sudo apt install minicom usbutils openssh-client
minicom --version && lsusb --version && ssh -V
command -v scp
```

Do not change user groups during Stage 1. When Stage 2 hardware inspection has a USB-to-UART adapter attached, check the serial node and then arrange `dialout` access through the approved local administration process.

### PC-side V4L2 and GStreamer tools

```sh
sudo apt install v4l-utils gstreamer1.0-tools gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good
v4l2-ctl --version && media-ctl --version
gst-launch-1.0 --version && gst-inspect-1.0 --version
for element in v4l2src udpsrc rtpjpegdepay; do
  gst-inspect-1.0 "$element" >/dev/null || exit 1
done
```

This verifies host-side tool availability only. It does not verify a USB camera or any target GStreamer pipeline.

### Debug and static-analysis tools

```sh
sudo apt install shellcheck gdb strace valgrind cppcheck clang-format clang-tidy
shellcheck --version && gdb --version && strace --version && valgrind --version
cppcheck --version && clang-format --version && clang-tidy --version
```

`shellcheck` is required before Stage 1 shell scripts are merged. The remaining tools in this group are recommended project tools. Optional additions are:

```sh
sudo apt install picocom ltrace
picocom --version && ltrace --version
```

## Final environment verification

From the repository root (for example, `$HOME/workspace/camstream-control`):

```sh
bash -n scripts/host/check-environment.sh
bash -n scripts/host/capture-tool-versions.sh
shellcheck scripts/host/check-environment.sh scripts/host/capture-tool-versions.sh
./scripts/host/check-environment.sh
./scripts/host/capture-tool-versions.sh
```

The environment check exits `0` only when required Stage 1 checks pass. It reports recommended and optional gaps without failing. To save the version output deliberately, redirect it yourself; the script never creates a report:

```sh
./scripts/host/capture-tool-versions.sh > host-tool-versions.txt
```

## Troubleshooting

- **Command is missing:** install the package group that contains it, then run the verification command for that group again.
- **Development capability is missing:** verify both the package and `pkg-config` result, for example `pkg-config --modversion ncursesw`.
- **GStreamer element is missing:** inspect the installed plugin packages with `gst-inspect-1.0 <element>` before adding a suitable distribution package.
- **Serial access is denied:** do not treat this as a Stage 1 failure. In Stage 2, confirm the USB-to-UART adapter and serial node first, then follow the approved process for `dialout` access and start a new login session.
- **Package names differ:** use the distribution's package metadata to locate the provider; do not assume this Ubuntu 22.04 package list applies unchanged.
