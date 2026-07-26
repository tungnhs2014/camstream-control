# V4L2 USB camera bring-up

## 1. Purpose and scope

This guide reproduces the CamStream Control Stage 6A baseline: a Logitech C270
USB camera using the upstream Linux `uvcvideo` driver, V4L2 core and
videobuf2, followed by userspace inspection and one real frame capture on a
BeagleBone Black.

Stage 6A proves the standard UVC/V4L2 reference path before any custom capture
driver or native capture application is developed. It does not implement the
later native application or custom V4L2 driver, select a production format,
validate GStreamer, or claim long-term USB stability.

## 2. Hardware and software prerequisites

The validated baseline uses:

- BeagleBone Black booting from MicroSD
- Logitech C270 HD Webcam, USB ID `046d:0825`
- Buildroot 2026.02.3 and Linux 6.18.1
- an Ubuntu development host with the Stage 1 tools
- UART or SSH target access
- enough MicroSD capacity for the generated image

The validated target also used a TP-Link TL-WN722N v2/v3 Wi-Fi adapter through
`rtl8xxxu` and Dropbear SSH. Record the actual hubs, power source, camera,
Wi-Fi adapter, other USB devices, and cabling because topology is relevant to
failures. Stage 6A did not establish a hub or power recommendation.

## 3. Starting Buildroot baseline

Start from the project-controlled BeagleBone defconfig and BR2_EXTERNAL tree,
not an unrecorded interactive configuration. Use absolute paths so Buildroot's
out-of-tree output remains separate from the repository:

```sh
export REPO="$HOME/TungNHS/camstream-control"
export BUILDROOT="$HOME/TungNHS/camstream-workspace/sources/buildroot-2026.02.3"
export OUTPUT="$HOME/TungNHS/camstream-workspace/output/stage06-camera-v4l2"

make -C "$BUILDROOT" O="$OUTPUT" \
  BR2_EXTERNAL="$REPO/br2-external" beaglebone_defconfig
```

Confirm that the resulting Buildroot configuration names the two kernel
fragments and enables the required userspace package options:

```sh
grep -E '^(BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES|BR2_PACKAGE_LIBV4L|BR2_PACKAGE_LIBV4L_UTILS)=' \
  "$OUTPUT/.config"
```

## 4. Why UVC and V4L2 support is required

The C270 follows USB Video Class (UVC). Linux `uvcvideo` handles the USB camera,
V4L2 exposes capture and metadata interfaces to userspace, and videobuf2
provides the streaming-buffer framework used by the driver. The media
controller exposes the device topology through `/dev/mediaX`.

This upstream path is the reference against which later custom-driver and
userspace work can be compared:

```text
Logitech C270 -> USB -> uvcvideo -> V4L2/videobuf2 -> /dev/videoX -> v4l2-ctl
```

## 5. Buildroot libv4l and v4l-utils configuration

The BeagleBone defconfig enables:

```text
BR2_PACKAGE_LIBV4L=y
BR2_PACKAGE_LIBV4L_UTILS=y
```

`BR2_PACKAGE_LIBV4L` enables the Buildroot package and its V4L userspace
libraries. `BR2_PACKAGE_LIBV4L_UTILS` is nested under it and builds tools used
for target inspection, including `v4l2-ctl`, `media-ctl`, and
`v4l2-compliance`. These tools prove device capabilities without requiring a
project-specific application.

## 6. Linux media, V4L2 and UVC configuration

The project camera fragment contains:

```text
CONFIG_MEDIA_SUPPORT=m
CONFIG_MEDIA_SUPPORT_FILTER=y
CONFIG_MEDIA_CAMERA_SUPPORT=y
CONFIG_MEDIA_CONTROLLER=y
CONFIG_VIDEO_DEV=m
CONFIG_MEDIA_USB_SUPPORT=y
CONFIG_USB_VIDEO_CLASS=m
```

Building the media stack and UVC support as modules keeps the kernel
configuration explicit while allowing normal device-driven module loading.
`CONFIG_MEDIA_SUPPORT_FILTER` exposes only the media support classes selected
for this target; camera and USB-media support are selected here.

## 7. Purpose of `linux-camera.fragment`

`br2-external/board/beaglebone/linux-camera.fragment` owns only the camera
kernel requirements. Keeping these settings in a small project-controlled
fragment makes the intent reviewable and prevents Stage 6A from depending on
unrecorded `linux-menuconfig` state.

The existing `linux-wifi.fragment` remains separate because Wi-Fi and camera
support have different responsibilities and validation evidence.

## 8. Adding the camera fragment alongside Wi-Fi

`BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES` in
`br2-external/configs/beaglebone_defconfig` lists both fragments:

```text
BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_CAMSTREAM_PATH)/board/beaglebone/linux-wifi.fragment $(BR2_EXTERNAL_CAMSTREAM_PATH)/board/beaglebone/linux-camera.fragment"
```

Do not replace the Wi-Fi fragment when adding camera support. Buildroot applies
the listed fragments after the base `omap2plus` kernel defconfig.

## 9. Reproducing the kernel configuration

Regenerate the Linux build directory before inspecting `.config`:

```sh
make -C "$BUILDROOT" O="$OUTPUT" linux-dirclean
make -C "$BUILDROOT" O="$OUTPUT" linux-configure

grep -E '^(CONFIG_MEDIA_SUPPORT|CONFIG_MEDIA_SUPPORT_FILTER|CONFIG_MEDIA_CAMERA_SUPPORT|CONFIG_MEDIA_CONTROLLER|CONFIG_VIDEO_DEV|CONFIG_MEDIA_USB_SUPPORT|CONFIG_USB_VIDEO_CLASS)=' \
  "$OUTPUT/build/linux-6.18.1/.config"
```

`linux-dirclean` removes prior Linux build state. `linux-configure` then
recreates the kernel configuration from the selected kernel defconfig and the
two recorded fragments. Seeing the expected values after this sequence proves
configuration reproducibility; merely inspecting an old `.config` would not.

## 10. Building the full image

After configuration validation, build the complete image:

```sh
make -C "$BUILDROOT" O="$OUTPUT"
```

A successful package or kernel-only build is not sufficient. The Stage 6A
baseline requires the full Buildroot build to produce the bootable image and
target filesystem together.

## 11. Pre-flash artifact checks

Check that the image, tools and kernel modules exist before writing removable
media:

```sh
test -s "$OUTPUT/images/sdcard.img"
sha256sum "$OUTPUT/images/sdcard.img"

for tool in v4l2-ctl media-ctl v4l2-compliance; do
  test -x "$OUTPUT/target/usr/bin/$tool" || exit 1
done

find "$OUTPUT/target/lib/modules/6.18.1" -type f \
  \( -name 'uvcvideo.ko*' -o -name 'videodev.ko*' \
     -o -name 'videobuf2-*.ko*' \) -print
```

Keep the image hash with local build evidence. Do not add `sdcard.img`, kernel
modules, captured frames, or the output directory to Git.

## 12. Flashing and boot expectations

Identify the MicroSD device by comparing `lsblk` output before and after card
insertion. Verify its path, model, size, removable flag and mounted partitions:

```sh
lsblk -o NAME,PATH,SIZE,MODEL,TRAN,RM,FSTYPE,MOUNTPOINTS
```

The following is a template, not a device-discovery shortcut. Replace
`/dev/sdX` only after positively identifying the whole MicroSD device, unmount
its mounted partitions, and review both paths before running `dd`:

```sh
export CARD=/dev/sdX
lsblk -o NAME,PATH,SIZE,MODEL,TRAN,RM,FSTYPE,MOUNTPOINTS "$CARD"
sudo umount /dev/sdX1  # repeat for each mounted partition that lsblk shows
sudo dd if="$OUTPUT/images/sdcard.img" of="$CARD" bs=4M status=progress conv=fsync
sync
```

Writing the wrong device destroys data. Never derive `CARD` from ordering
alone, and never use a partition such as `/dev/sdX1` as the `dd` output.

Boot the BeagleBone Black from the MicroSD using the established board boot
procedure. The expected result is U-Boot handoff to Linux, completion of init,
and an accessible login through UART or Dropbear SSH. Capture boot failures
before changing configuration.

## 13. USB camera enumeration

Connect the camera and inspect USB and kernel evidence on the target:

```sh
lsusb
dmesg | grep -Ei 'uvc|046d|0825|usb|video'
```

The validated C270 identifies as `046d:0825`. USB enumeration proves that the
host controller sees the device; it does not by itself prove driver binding or
frame capture. The camera microphone may also register through
`snd-usb-audio`; audio is outside Stage 6A.

## 14. `uvcvideo` and module checks

Check the active module stack and the installed module metadata:

```sh
lsmod | grep -E 'uvcvideo|videodev|videobuf2'
modinfo uvcvideo
dmesg | grep -Ei 'Found UVC|uvcvideo'
```

The expected path includes `uvcvideo`, `videodev`, `videobuf2-v4l2`,
`videobuf2-common`, `videobuf2-vmalloc`, and `videobuf2-memops`. Automatic
`uvcvideo` binding connects the enumerated USB device to V4L2.

## 15. Video and media node enumeration

List all nodes before assigning roles:

```sh
v4l2-ctl --list-devices
ls -l /dev/video* /dev/media*
media-ctl --device /dev/media0 --print-topology
```

The validated C270 baseline creates `/dev/video0`, `/dev/video1`, and
`/dev/media0`. Node numbering can change with probe order, so scripts and
operators must not assume that `/dev/video0` is always the capture node.

## 16. Finding the actual capture node

Query every `/dev/videoX` node and read its Device Caps:

```sh
for node in /dev/video*; do
  printf '\n%s\n' "$node"
  v4l2-ctl --device "$node" --all
done
```

Choose the node reporting `Video Capture` and `Streaming`. In the validated
baseline, `/dev/video0` has those capabilities. `/dev/video1` reports
`Metadata Capture` and is not an image stream. `/dev/media0` represents the
media controller rather than a capture buffer endpoint.

## 17. Format and frame-rate enumeration

After identifying the image node, enumerate its advertised modes:

```sh
v4l2-ctl --device /dev/video0 --list-formats-ext
```

The C270 advertised at least YUYV 640x480 at 30 fps, YUYV 1280x720 at 10 fps,
MJPEG 640x480 at 30 fps, and MJPEG 1280x720 at 30 fps. Enumeration establishes
supported capability only. It does not prove that every listed mode was
successfully streamed on this target.

## 18. YUYV and MJPEG considerations

YUYV is uncompressed packed YUV 4:2:2, so its USB bandwidth grows directly
with resolution and frame rate. MJPEG is compressed by the camera before USB
transfer. This explains why the C270 advertises 1280x720 at up to 10 fps for
YUYV but up to 30 fps for MJPEG.

That capability difference does not select a production format. Later stages
must evaluate decode cost, image quality, latency, buffer ownership and
stability under the intended full-system load.

## 19. Camera control enumeration

Enumerate controls without changing them:

```sh
v4l2-ctl --device /dev/video0 --list-ctrls-menus
```

The validated camera exposed controls including brightness, contrast,
saturation, white balance, gain, sharpness, backlight compensation, automatic
exposure and exposure time. Stage 6A proves enumeration only; it does not prove
that every control was functionally exercised.

## 20. Real 640x480 MJPEG capture

Configure only a mode reported by `--list-formats-ext`, verify the negotiated
format, and capture one frame after discarding initial buffers:

```sh
v4l2-ctl --device /dev/video0 \
  --set-fmt-video=width=640,height=480,pixelformat=MJPG \
  --set-parm=30

v4l2-ctl --device /dev/video0 --get-fmt-video --get-parm

v4l2-ctl --device /dev/video0 \
  --stream-mmap=3 --stream-skip=30 --stream-count=1 \
  --stream-to=/tmp/c270-640x480.jpg

test -s /tmp/c270-640x480.jpg
```

This procedure validates one real MJPEG frame. It is not an FPS benchmark,
long-duration stream, or validation of the advertised 1280x720 modes.

## 21. Why initial frames may be discarded

The first single-frame capture in Stage 6A was almost black. A later frame was
valid after the stream had run long enough to discard initial buffers. Camera
auto-exposure and general sensor settling are a practical explanation, but the
observation does not prove a kernel or hardware defect—or a definitive root
cause.

Discarding a bounded number of initial frames gives automatic controls time to
converge and makes visual bring-up more representative. Keep the initial-frame
observation in the evidence rather than hiding it.

## 22. Copying the frame to the host

From the Ubuntu host, copy the frame through the verified SSH path:

```sh
scp root@TARGET_IP:/tmp/c270-640x480.jpg ./c270-640x480.jpg
file ./c270-640x480.jpg
```

Replace `TARGET_IP` with the target address and use the configured login. Open
the file with a host image viewer and confirm that it is a valid image from the
camera. Keep captured images as local evidence unless a deliberate publication
decision and privacy review are made.

## 23. Troubleshooting checkpoints

- **No `046d:0825` USB device:** inspect cable, hub, power and physical topology;
  compare `lsusb` and `dmesg` before changing software.
- **USB device but no `uvcvideo`:** check module installation, `modinfo`,
  `lsmod`, modalias handling and UVC messages.
- **No `/dev/videoX`:** check `videodev`, videobuf2 modules, `mdev` operation and
  kernel logs.
- **Multiple video nodes:** use Device Caps; never infer image capture from the
  lowest node number alone.
- **Format rejected:** select the exact pixel format, size and interval
  advertised by `--list-formats-ext`, then inspect the negotiated result.
- **Black initial frame:** capture after skipping initial buffers and inspect
  exposure controls; do not immediately label it a driver failure.
- **Timeout, disconnect or reset:** preserve concise kernel evidence and record
  all connected USB devices, hubs and power sources before isolating one
  variable at a time.

## 24. Known and deferred USB issue

**DEFERRED / KNOWN ISSUE: Intermittent USB reset under the multi-device USB
topology.**

Observed indicators included `musb-hdrc ... ep2 RX three-strikes error`, USB
disconnect and high-speed USB reset messages while multiple devices and hubs
were present. The root cause is not established. The evidence does not prove
that the camera driver or power is the cause, and the issue is not fixed.

Revisit it during system-integration and stability work by recording topology,
isolating devices and hubs, checking power integrity, and running defined
repeatability and soak tests. Long-term USB stability remains deferred.

## 25. Bring-up completion criteria

Stage 6A functional bring-up is complete when all of the following are backed
by the tested image and target evidence:

- the camera fragment survives `linux-dirclean` and `linux-configure`
- the full Buildroot image builds successfully
- `uvcvideo`, `videodev` and required videobuf2 modules are present
- `v4l2-ctl`, `media-ctl` and `v4l2-compliance` are installed
- the C270 enumerates as `046d:0825` and binds automatically to `uvcvideo`
- image, metadata and media-controller nodes are distinguished by capability
- formats, frame rates and controls enumerate successfully
- a real 640x480 MJPEG frame is captured and visually verified on the host
- untested modes, controls and deferred stability work are explicitly labeled

Meeting these criteria establishes the upstream UVC/V4L2 functional baseline.
It does not authorize Stage 6B, prove production streaming, or close the known
multi-device USB stability issue.
