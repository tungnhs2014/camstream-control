# BeagleBone Black Buildroot bring-up

## Purpose and boundary

This guide records the CamStream Control platform flow that produced the
validated BeagleBone Black baseline. It connects the project-controlled
Buildroot configuration to a booted target; it is not a general Buildroot
tutorial and does not cover Wi-Fi or camera configuration.

The pinned flow is:

```text
Buildroot 2026.02.3
  -> BR2_EXTERNAL tree CAMSTREAM
  -> beaglebone_defconfig
  -> U-Boot 2026.01
  -> Linux 6.18.1
  -> rootfs + sdcard.img
  -> MicroSD flash
  -> UART
  -> U-Boot + kernel + rootfs
  -> target baseline validation
```

## Project-owned baseline

| Item | Selection | Why it is recorded here |
| --- | --- | --- |
| Buildroot | 2026.02.3 | Top-level build and dependency integration |
| External tree | `br2-external`, name `CAMSTREAM` | Keeps project policy outside Buildroot source |
| Defconfig | `beaglebone_defconfig` | Reproducible project entry point |
| CPU | 32-bit ARM, Cortex-A8 | Matches `BR2_arm` and `BR2_cortex_a8` |
| Floating point | VFPv3 | Matches `BR2_ARM_FPU_VFPV3` |
| Toolchain | Bootlin external ARMv7 EABIHF glibc stable | Pinned external-toolchain family |
| Device management | Dynamic `/dev` through `mdev` | Matches `BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV` |
| Bootloader | U-Boot 2026.01, `am335x_evm_defconfig` | Produces the BeagleBone SPL and U-Boot image |
| Kernel | Linux 6.18.1, `omap2plus_defconfig` | Project kernel baseline |
| Root filesystem | Buildroot-generated ext4 | Matches `BR2_TARGET_ROOTFS_EXT2_4` |
| Disk image | `images/sdcard.img` | Flashable whole-card artifact |

Do not reproduce this baseline by changing Buildroot configuration
interactively and leaving the result only in an output directory. Persistent
project choices belong in `br2-external/configs/beaglebone_defconfig`, kernel
fragments, or the rootfs overlay as appropriate.

## Load the external defconfig

Keep Buildroot source, output, and the project repository separate. The paths
below match the project workspace convention; choose a distinct output name
when preserving another stage's artifacts.

```sh
export REPO="$HOME/TungNHS/camstream-control"
export BUILDROOT="$HOME/TungNHS/camstream-workspace/sources/buildroot-2026.02.3"
export OUTPUT="$HOME/TungNHS/camstream-workspace/output/platform-baseline"

make -C "$BUILDROOT" O="$OUTPUT" \
  BR2_EXTERNAL="$REPO/br2-external" beaglebone_defconfig
```

Confirm the Buildroot release, then check that the generated configuration
still selects the pinned Linux and U-Boot components:

```sh
make -s -C "$BUILDROOT" O="$OUTPUT" \
  BR2_EXTERNAL="$REPO/br2-external" printvars VARS='BR2_VERSION_FULL'

grep -E '^(BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE|BR2_LINUX_KERNEL_DEFCONFIG|BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE|BR2_TARGET_UBOOT_BOARD_DEFCONFIG)=' \
  "$OUTPUT/.config"
```

The expected Buildroot value is `2026.02.3`; the Linux defconfig is
`omap2plus`, and the component version/defconfig values must match the baseline
table above. `BR2_EXTERNAL` is the command-line interface, `CAMSTREAM` is the
name declared by `external.desc`, and Buildroot derives
`BR2_EXTERNAL_CAMSTREAM_PATH` for paths inside project configuration files.

The external tree also owns later Wi-Fi and camera additions. Those additions
must not silently change the platform versions or remove the BeagleBone boot
artifacts.

## Build and inspect the image set

Build the complete image rather than treating a successful component rebuild
as a bootable result:

```sh
make -C "$BUILDROOT" O="$OUTPUT"
```

The expected image set includes:

- `MLO` for the AM335x ROM/SPL stage
- `u-boot.img` for U-Boot
- `zImage` and BeagleBone device trees for Linux
- the generated root filesystem
- `sdcard.img`, assembled as the whole-card image

Check the artifacts and record a local checksum before flashing:

```sh
ls -lh "$OUTPUT/images"
test -s "$OUTPUT/images/sdcard.img"
sha256sum "$OUTPUT/images/sdcard.img"
```

Keep Buildroot output and checksums that identify local test artifacts outside
Git. A later rebuild can be considered equivalent only when its controlled
inputs and resulting evidence support that conclusion.

## Identify and flash the MicroSD safely

Run `lsblk` before inserting the card and again afterward. Identify the card by
path, size, model, transport, removable flag, and partitions—not merely by
device ordering:

```sh
lsblk -o NAME,PATH,SIZE,MODEL,SERIAL,TRAN,RM,FSTYPE,MOUNTPOINTS
```

The following is deliberately a template. `/dev/sdX` must be replaced only
after positive identification of the whole MicroSD device:

```sh
export CARD=/dev/sdX
lsblk -o NAME,PATH,SIZE,MODEL,TRAN,RM,FSTYPE,MOUNTPOINTS "$CARD"
sudo umount /dev/sdX1  # repeat for every mounted partition shown by lsblk
sudo dd if="$OUTPUT/images/sdcard.img" of="$CARD" \
  bs=4M status=progress conv=fsync
sync
```

Writing the wrong device destroys data. Recheck both `if` and `of` immediately
before `dd`, write to the whole card rather than a partition, and do not leave
any card partition mounted.

## Connect the UART safely

Use a 3.3 V TTL USB-to-UART adapter at 115200 baud, 8 data bits, no parity, one
stop bit, and no flow control.

- connect adapter GND to BeagleBone GND
- connect adapter TX to BeagleBone UART RX
- connect adapter RX to BeagleBone UART TX
- do **not** connect the adapter VCC pin to the board
- verify the adapter voltage and header pinout before applying power

Start the terminal before powering the target so that the complete boot log is
captured. UART is the primary evidence path when networking is not yet ready.

## Boot from MicroSD

Insert the flashed card and use the established BeagleBone Black MicroSD boot
procedure, including the board's boot-selection button when required. A
successful baseline proceeds through these distinct checkpoints:

1. AM335x ROM loads `MLO`/SPL from the card.
2. SPL loads U-Boot 2026.01.
3. U-Boot selects the kernel, device tree, and rootfs arguments.
4. Linux 6.18.1 starts and mounts the generated root filesystem.
5. Buildroot init completes and presents a usable target login.

Preserve the first failure and its surrounding UART lines before changing
anything. A login prompt alone is insufficient if the earlier boot source and
version evidence were not observed.

## Validate the target baseline

After login, capture the minimum platform identity and storage evidence:

```sh
cat /etc/os-release
uname -a
tr -d '\000' </proc/device-tree/model; echo
cat /proc/cmdline
mount
df -h
```

The validated project checkpoint established:

| Checkpoint | Result |
| --- | --- |
| Buildroot 2026.02.3 rootfs identity | PASS |
| U-Boot 2026.01 reached through MicroSD boot | PASS |
| Linux 6.18.1 boot and rootfs mount | PASS |
| UART console input and target login | PASS |
| BeagleBone Black target identity | PASS |

These results prove the platform baseline used by later stages. They do not
prove networking, USB camera capture, or application behavior; those require
their own runtime evidence.

## Troubleshooting in boot order

- **No UART characters:** recheck 3.3 V TTL level, GND, crossed TX/RX, serial
  node, 115200 8N1 settings, and whether the terminal was open before power-up.
- **No SPL/MLO banner:** confirm MicroSD selection, card contents, the flashed
  whole-device path, and the first partition layout.
- **SPL starts but U-Boot does not:** verify `u-boot.img`, the generated image,
  and the exact U-Boot version/configuration rather than changing Linux.
- **U-Boot starts but cannot load Linux:** inspect MMC discovery, filenames,
  device tree selection, and U-Boot environment/boot command.
- **Kernel starts but rootfs fails:** preserve the kernel command line and mount
  error; check image partitioning and root device arguments.
- **Login appears but input is corrupted:** isolate terminal settings, adapter,
  wiring, and software baseline. An older project baseline exhibited UART RX
  corruption; the pinned U-Boot 2026.01/Linux 6.18.1 baseline resolved the
  practical console-input problem and should not be silently regressed.

Change one layer at a time and repeat from the earliest failed checkpoint. Do
not debug a later service until ROM, SPL, U-Boot, kernel, rootfs, and login are
individually accounted for.
