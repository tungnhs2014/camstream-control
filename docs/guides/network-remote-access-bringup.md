# Network and remote-access bring-up

## Purpose and scope

This guide records the CamStream Control Stage 5 path from a USB Wi-Fi adapter
to remote shell access on the BeagleBone Black Buildroot target. It assumes the
platform image already boots and UART login works. It does not claim general
Wi-Fi compatibility or long-term stability for the full multi-device USB
topology.

The validated path is:

```text
TP-Link 2357:010c
  -> rtl8xxxu
  -> rtlwifi/rtl8188eufw.bin
  -> wlan0
  -> wpa_supplicant
  -> udhcpc
  -> IPv4 + default route + DNS
  -> Dropbear
  -> SSH/SCP
```

## Project integration

### Kernel Wi-Fi fragment

`br2-external/board/beaglebone/linux-wifi.fragment` contains the focused
kernel requirements:

```text
CONFIG_USB=y
CONFIG_CFG80211=m
CONFIG_MAC80211=m
CONFIG_WLAN=y
CONFIG_WLAN_VENDOR_REALTEK=y
CONFIG_RTL8XXXU=m
```

The module choice allows normal USB modalias loading while keeping the wireless
stack visible in the target module set. USB enumeration alone does not prove
that `rtl8xxxu` bound or that firmware loaded; runtime logs must show those
later checkpoints.

### Buildroot packages

The project defconfig enables the required Realtek firmware, module handling,
wireless tools, WPA client, DHCP client, and SSH server. The relevant runtime
pieces are:

- `rtl8188eufw.bin` under `/lib/firmware/rtlwifi/`
- `rtl8xxxu` and its cfg80211/mac80211 dependencies
- `iw`
- `wpa_supplicant`, its CLI, and `wpa_passphrase`
- BusyBox `udhcpc`
- `kmod`
- Dropbear SSH server

Inspect the built target tree before flashing when diagnosing a missing
runtime component. The exact historical Stage 5 output directory is not
retained in the current workspace. Set `OUTPUT` to the out-of-tree directory
that contains the Stage 5 image under the project workspace convention; the
value below is a template and must be replaced before use:

```sh
export OUTPUT="$HOME/TungNHS/camstream-workspace/output/REPLACE_WITH_STAGE5_OUTPUT"

test -f "$OUTPUT/target/lib/firmware/rtlwifi/rtl8188eufw.bin"
find "$OUTPUT/target/lib/modules" -name 'rtl8xxxu.ko*' -print
test -x "$OUTPUT/target/usr/sbin/wpa_supplicant"
test -x "$OUTPUT/target/usr/sbin/wpa_passphrase"
test -x "$OUTPUT/target/usr/sbin/dropbear"
```

### Rootfs overlay and `S40wifi`

The rootfs overlay contributes:

```text
/etc/init.d/S40wifi
/etc/wpa_supplicant.conf.example
```

`S40wifi` waits for `wlan0`, leaves the target safe when no real
`/etc/wpa_supplicant.conf` exists, raises the interface, starts
`wpa_supplicant` with the `nl80211` backend, then starts `udhcpc` in background
mode. Its start message proves only that startup actions were launched. Verify
association, address, route, DNS, and SSH separately.

The tracked example contains placeholders only. The actual provisioning file
is deliberately absent from Git.

## Provision after every reflash

Flashing `sdcard.img` replaces the target root filesystem. Recreate the real
Wi-Fi configuration and the intended target login credentials after each
reflash through the UART console or another already trusted local path.

In a trusted UART session with terminal recording disabled, copy the tracked
template and edit it locally on the target:

```sh
umask 077
cp /etc/wpa_supplicant.conf.example /etc/wpa_supplicant.conf
chmod 600 /etc/wpa_supplicant.conf
vi /etc/wpa_supplicant.conf
```

Replace both placeholders with private values, preferably using a derived PSK
produced through a trusted offline workflow. This target-local edit avoids
putting an SSID or passphrase in shell history; terminal logging must still be
disabled while the file is visible. Do not paste a real SSID, passphrase, or
PSK into tracked source files, command transcripts, screenshots, issue text, or
documentation. Review evidence for credentials, public IP addresses, MAC
addresses, usernames, and local network details before sharing it.

Set the root password by the approved target-local procedure before attempting
remote login. Never commit a real password or password hash.

## Validate each network layer

Start the service after provisioning, or reboot and let init start it:

```sh
/etc/init.d/S40wifi restart
```

Then validate in dependency order.

### USB, driver, and firmware

```sh
lsusb
dmesg | grep -Ei '2357|010c|rtl8|firmware|wlan'
lsmod | grep -E 'rtl8xxxu|mac80211|cfg80211'
modinfo rtl8xxxu
```

The validated adapter identifies as `2357:010c` and uses `rtl8xxxu` with
`rtlwifi/rtl8188eufw.bin`. Seeing the USB ID without a driver/firmware result is
only enumeration, not a working interface.

### Association and DHCP

```sh
iw dev
iw dev wlan0 link
ip -4 address show dev wlan0
ip route
```

Require an associated access point, a leased IPv4 address, and a default route.
An existing `wlan0` alone is not sufficient.

### Gateway, Internet, and DNS

```sh
ip route
ping -c 3 GATEWAY_IP
ping -c 3 1.1.1.1
ping -c 3 example.com
cat /etc/resolv.conf
```

Replace `GATEWAY_IP` from the observed default route. A gateway response proves
local routing; an IP response proves wider connectivity; a hostname response
also exercises DNS. Record them as separate results.

### Dropbear, SSH, and SCP

On the target:

```sh
ps | grep '[d]ropbear'
command -v ss >/dev/null && ss -lnt
```

The process check applies to the validated baseline. The optional `ss` command
can add listener evidence when that utility is present in the selected image;
do not treat its absence as a Dropbear failure.

From the physical Ubuntu host:

```sh
ssh root@TARGET_IP
printf 'CamStream SCP check\n' >/tmp/camstream-scp-check.txt
scp /tmp/camstream-scp-check.txt root@TARGET_IP:/tmp/
ssh root@TARGET_IP 'cat /tmp/camstream-scp-check.txt'
```

Replace `TARGET_IP` with the leased target address. Validate the host key
through the trusted UART/network context before accepting it. Keep hostnames,
addresses, and fingerprints private unless publication is necessary and
reviewed.

## Reboot validation

After the first end-to-end pass, reboot without manually rerunning `S40wifi`.
The Stage 5 baseline is reproducible only if all of these recover:

1. `2357:010c` enumerates and `rtl8xxxu` loads its firmware.
2. `wlan0` associates through `wpa_supplicant`.
3. `udhcpc` installs IPv4 configuration and the default route.
4. DNS lookup succeeds.
5. Dropbear listens and the physical host can reconnect through SSH.

SCP should also be repeated after reboot if it is an acceptance requirement,
not inferred from a prior session.

## Validated checkpoint

| Checkpoint | Result |
| --- | --- |
| TP-Link `2357:010c` USB enumeration | PASS |
| `rtl8xxxu` binding and RTL8188EU firmware load | PASS |
| `wlan0` creation and WPA association | PASS |
| DHCP lease, IPv4 address, and default route | PASS |
| Gateway, Internet IP, and DNS connectivity | PASS |
| Dropbear SSH from the physical host | PASS |
| SCP from the physical host | PASS |

These are functional checkpoints, not a throughput benchmark or USB soak test.

## Troubleshooting order

- **No USB ID:** inspect the adapter, hub, cable, board host port, and power;
  compare `lsusb` and `dmesg` before changing software.
- **USB ID but no `rtl8xxxu`:** check module installation, modalias matching,
  `modinfo`, `lsmod`, and kernel logs.
- **Firmware error:** confirm the exact requested filename and the built/rootfs
  path; do not substitute a similarly named firmware file.
- **No `wlan0`:** resolve driver/firmware initialization before editing WPA or
  DHCP configuration.
- **Not associated:** check the private configuration, access-point visibility,
  regulatory context, and `wpa_supplicant` logs without exposing credentials.
- **Associated but no address:** inspect `udhcpc` and DHCP server responses.
- **Address but no route/DNS:** treat default route and resolver configuration
  as separate failures.
- **Network works but SSH fails:** inspect Dropbear process/listener, target
  credentials, and host-key diagnostics.
- **Works once but not after reboot:** inspect `S40wifi` ordering, interface wait,
  provisioned file persistence, and Dropbear startup.

The Stage 6A camera work later observed intermittent USB resets with multiple
devices and hubs connected. That root cause remains unproven and USB stability
is deferred. Preserve topology and kernel evidence, then isolate one device,
hub, cable, or power condition at a time rather than attributing the failure to
Wi-Fi or camera software without evidence.
