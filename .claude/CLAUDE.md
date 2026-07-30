# nuc-bios-build — build, flash, verify

Notes for working on this repo, written from an end-to-end firmware change
(custom EDK2 boot splash, 2026-07-29). The build is documented in
[README.md](../README.md); this file covers the parts that are only obvious
after doing it once on the real hardware.

**No credentials belong in this file.** Access is via SSH key (below); the
account password is deliberately not recorded here.

## The hardware under test

| Role | Address | Notes |
| --- | --- | --- |
| NUC5i7RYH (target) | `10.1.40.22` | Debian 13, hostname `talos-10-1-40-22` |
| JetKVM controlling it | `10.0.107.71` | video, HID, virtual media, DC power |

`10.0.107.72` is a *different* JetKVM and not this board — easy to mix up.

SSH to the NUC is key-based for both `root` and `appkins`; `appkins` has
NOPASSWD sudo. `PermitRootLogin without-password` means root **password** auth is
refused, so a password failure for root does not imply a wrong password.

## Build

```sh
kas build kas.yml        # -> build/tmp/deploy/images/nuc5i7ryh/coreboot-nuc5i7ryh.rom
bitbake edk2-uefipayload # payload alone; coreboot consumes its deployed UEFIPAYLOAD.fd
```

The payload is a separate recipe from coreboot on purpose (see the DESCRIPTION in
`edk2-uefipayload_2605.bb`), so `NucRedfishPkg` and the bootsplash are staged into
the tree by `do_configure` rather than patched into a tree coreboot clones
mid-`do_compile`.

There is no flasher live ISO. It was removed from the tree deliberately — do not
resurrect `kas-flasher.yml` or a `flasher` multiconfig. Flash either in-band (below)
or with a SOIC-8 clip via `scripts/nuc-spi.sh`.

## Flash in-band

coreboot leaves the flash unlocked, so once coreboot is installed no clip is
needed. Confirm first — `setpci -s 00:1f.0 DC.B` should report `09`
(BIOSWE set, BLE and SMM_BWP clear):

```sh
cat coreboot-nuc5i7ryh.rom | ssh root@10.1.40.22 'cat > /root/coreboot.rom'
ssh root@10.1.40.22 'flashrom -p internal -c MX25L6405 --ifd -i bios -r backup.rom'
ssh root@10.1.40.22 'flashrom -p internal -c MX25L6405 --ifd -i bios -w /root/coreboot.rom --noverify-all'
```

Three flags that are all mandatory, each for a non-obvious reason:

- `-c MX25L6405` — six Macronix definitions share this JEDEC ID and flashrom
  refuses to guess between them.
- `--ifd -i bios` — a **full-chip** read or write fails outright (`Transaction
  error!`), because the running ME locks its region and flashrom aborts the whole
  operation rather than skipping it. Only the BIOS region (`0x1a0000-0x7fffff`) is
  writable in-band anyway.
- `--noverify-all` — verification must be scoped to the written region for the
  same reason.

Take the backup **and copy it off the machine** before writing; it is the only
in-band rollback, and recovering without it needs the clip. `flashrom` lives in
`/usr/sbin`, so `which flashrom` as a normal user wrongly reports it missing.

## Verify a firmware change

Read the BGRT ACPI table on the booted host rather than trying to photograph the
screen:

```sh
ssh root@10.1.40.22 'cat /sys/firmware/acpi/bgrt/image' > bgrt.bmp
cmp bgrt.bmp meta-nuc-bios/recipes-bsp/edk2/files/bootsplash.bmp
ssh root@10.1.40.22 'cat /sys/firmware/acpi/bgrt/status'   # 1 = actually drawn
```

This is byte-exact proof of what the payload displayed, and works because the
payload builds with `FOLLOW_BGRT_SPEC=TRUE`.

**Do not try to verify a splash by screenshotting the KVM.** The logo is only up
for `PLATFORM_BOOT_TIMEOUT` (3 s) and the JetKVM's HDMI capture cannot re-lock
fast enough across the firmware's video mode changes. A 100 ms-interval frame
capture across a full boot caught nothing until the Linux console ~33 s in, on a
boot where BGRT proved the splash *was* drawn. A black KVM screen during the
firmware phase means the capture missed it, not that the change failed.

Other useful post-flash checks: `dmidecode -s bios-vendor` (should say `coreboot`)
and `cbmem -c` for the firmware console — no UART is routed on this board, so
`cbmem` is the only way to read coreboot's log.

## Driving the JetKVM

JSON-RPC at `http://10.0.107.71/jsonrpc`, authenticated with a cookie
`authToken=<local_auth_token from /userdata/kvm_config.json>`:

```sh
curl -s -b "authToken=$TOKEN" -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"setDCPowerState","params":{"enabled":false}}' \
  http://10.0.107.71/jsonrpc
```

Methods worth knowing: `setDCPowerState{enabled}` / `getDCPowerState` (hard power
cycle; the active extension is `dc-power`), `mountWithStorage{filename,mode}` plus
`setUsbDeviceState{device:"massStorage",enabled}` for virtual media served out of
`/userdata/jetkvm/images/`, and `getVirtualMediaState`. Method names and parameters
are defined in `jsonrpc.go` in the jetkvm-community/kvm checkout.

The JetKVM has no `scp` or sftp-server, so copy files to it with
`cat local | ssh 10.0.107.71 'cat > /userdata/...'`.

## Redfish Host Interface (OOB management)

The NUC's firmware and the JetKVM talk Redfish over the USB CDC-ECM link
(DSP0270). Both halves live in this repo's sibling checkouts:

- host: `meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg` (built into the
  payload; see its README)
- BMC: `jetkvm-community/kvm/redfish.go` and `usb.go`

The link is `169.254.10.1` (BMC) ⇄ `169.254.10.2` (host), port 80, no TLS. The
service UUID and both ECM MACs are *derived* from the JetKVM's device ID on both
sides, so there is nothing to configure by hand — see `NucRedfishPkg/README.md`.

### The chain, and where it used to stop

```text
RedfishHostInterfaceDxe    -> SMBIOS type 42   (NucRedfishHostInterfaceLib)
RedfishDiscoverDxe         -> match MAC, configure REST EX
RedfishConfigHandlerDriver -> "service discovered"
NucRedfishSyncDxe          -> the actual HTTP exchange
```

Everything above `NucRedfishSyncDxe` is stock RedfishPkg and works. It stops
there because nothing in the payload *produces*
`EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL` (that is edk2-redfish-client, which does
not build against this tree). `NucRedfishSyncDxe` is that consumer.

**Four non-obvious traps in writing such a consumer**, all hit on hardware
2026-07-30:

1. `Init` is called twice — once when the protocol is *installed* (before
   discovery, `ServiceInfo` all zeroes) and once after discovery. Gate on
   `ServiceInfo->RedfishServiceRestExHandle != NULL`.
2. The pre-discovery call **must return an error** (`EFI_NOT_READY`).
   `RedfishConfigHandlerInitialization()` installs `gEfiCallerIdGuid` on the
   handle of any config handler whose `Init` returned success, and skips marked
   handles on later passes — so returning `EFI_SUCCESS` early permanently
   suppresses the call that actually has a service attached.
3. `RedfishCreateService` fails outright unless a credential lib answers. The
   stock `PlatformCredentialLibNull` returns `EFI_UNSUPPORTED`, which is *not*
   the same as "unauthenticated" — `NucRedfishCredentialLib` answers
   `AuthMethodNone` instead.
4. The first request returns `EFI_NO_MEDIA`. edk2's USB-net stack starts
   `CableDetect = 0` and only raises it on catching a CDC
   `NETWORK_CONNECTION`/`NETWORK_CONNECTED` notification; Linux's `f_ecm` emits
   those only on link-state changes, and the enumeration-time one fires long
   before the UEFI driver binds, so it is effectively never set. That model
   suits a dongle with an RJ45; it does not suit a point-to-point gadget, where
   enumeration *is* the proof of link. `wire-redfish.py` patches the initialiser
   to 1 (a real `NETWORK_DISCONNECT` still clears it).

   An earlier fix had the BMC re-emit the notification by bouncing usb0 on a
   timer. Do not reintroduce it: overlapping announcers ended up flapping the
   link roughly twice every 5 s across the exact phase the host enumerates in.

**Boot override lands one boot late, by design.** `BdsEntry` caches `BootNext`
before calling any PlatformBootManagerLib API, specifically so that a `BootNext`
set during BDS is *not* consumed in the same boot (the comment saying so is in
`MdeModulePkg/Universal/BdsDxe/BdsEntry.c`). Anything running during BDS is on
the far side of that snapshot, and this driver cannot run earlier — it needs
REST EX over a connected controller. So staging
`BootSourceOverrideTarget` + `Once` takes effect on the *next* boot. Verified
2026-07-30. Same-boot semantics would mean connecting USB/ECM/SNP/IP4/REST EX by
hand at End-of-DXE, which lengthens every boot including the ones with nothing
staged; stage-then-power-cycle is unaffected and is the usual OOB flow.

### Warm reboot hangs at the BDS wait — pre-existing, not RHI

`systemctl reboot` leaves the host at the splash with "Press ESC for Boot
Options/Settings" indefinitely; cold power cycles are reliable. Isolated
2026-07-30 and **not** caused by the Redfish or USB work:

- still hangs with gadget rebinds suppressed and no link flapping;
- still hangs with the CDC-ECM function disabled entirely
  (`setUsbDeviceState{device:"ethernet",enabled:false}`), so nothing USB-net is
  involved;
- still hangs on the **pre-session firmware** re-flashed from
  `backups/nuc-bios-region-pre-splash.rom`, which predates all of it.

So it belongs to the coreboot board port / payload warm-reset path, not to this
feature. Recover with a DC power cycle.

What is known about the hung state itself (probed 2026-07-30 while hung):

- the screen shows the splash and "Press ESC for Boot Options/Settings";
- **ESC does nothing** — the host does not respond to HID at all, so this is a
  hard hang, not BDS sitting in its hotkey wait;
- the USB gadget reads `configured` and usb0 has carrier, so the host got as far
  as enumerating USB;
- **no Redfish request ever arrives**, though a healthy boot always issues three.
  The exchange runs during BDS connect, so the hang is at or before that point —
  the on-screen prompt is drawn earlier and simply never gets overwritten.

The obstacle to going further is that this board routes no UART, and the
firmware console lives in CBMEM, which a cold recovery clears. `cbmem -1`
(`--oneboot`) exists on the host and would show the previous boot, but only if
the recovery preserves DRAM — a cold power cycle does not. Next step for anyone
picking this up: recover via warm reset instead (if the hang ever permits it),
or enable a CBMEM console that survives, before theorising about causes.

### Verifying it

Host side (`cbmem -c` on the NUC) shows the whole chain, including
`NucRedfishSync:` lines for each request. BMC side:

```sh
ssh root@10.0.107.71 'grep -a "ComputerSystem updated" /userdata/jetkvm/last.log'
```

The BMC's subsystem loggers default to **Error** level, so raise the ones you
need when reproducing — the app is launched from `/oem/usr/bin/RkLunch.sh`:

```sh
. /etc/profile.d/RkEnv.sh          # else the binary cannot find librockit.so
JETKVM_LOG_INFO=redfish,usb setsid /userdata/jetkvm/bin/jetkvm_app > /userdata/jetkvm/last.log 2>&1 &
```

`last.log` is the live log; `app.log` is stale. The device has no
curl/python — to exercise the *unauthenticated host-interface path* from a
workstation, tunnel to the usb0 address (requests then arrive from
`169.254.10.1`, which is inside the host-interface subnet):

```sh
ssh -f -N -L 18080:169.254.10.1:80 root@10.0.107.71
curl -X PATCH -H 'Content-Type: application/json' \
  -d '{"BootProgress":{"LastState":"SystemHardwareInitializationComplete"}}' \
  http://127.0.0.1:18080/redfish/v1/Systems/1
```

### USB enumeration is the fragile part

The host enumerates USB **once**, early in firmware, and takes what it finds. A
gadget rebind landing in that window costs the host its RHI NIC for the whole
boot — or hangs its USB enumeration (observed: a NUC sitting at the boot splash
indefinitely while the BMC logged repeated rebind cycles). `usb.go` therefore
never rebinds while the host is powered off, nor within
`hostEnumerationGrace` (90 s) of power-on; it binds once on the power-on
transition (`ensureHostInterfaceReady`) and then leaves the gadget alone.

## Bootsplash

`EDK2_BOOTSPLASH_FILE` defaults to the layer's `files/bootsplash.bmp`, installed
verbatim; any other path is converted with ImageMagick at configure time. It must
be **uncompressed 24-bit BMP3** — edk2's `BmpSupportLib` rejects RLE-compressed
BMPs, and ImageMagick produces those for palette images, so keep `-type TrueColor`.
The regeneration command is in the recipe. Size is not a concern: a mostly-black
BMP LZMA-compresses to ~7 KiB inside CBFS.
