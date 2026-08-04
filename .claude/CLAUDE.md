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
   enumeration *is* the proof of link. Patch `0002-UsbNetwork-assume-media-on-a-
   point-to-point-gadget.patch` defaults the initialiser to 1.

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
2026-07-30.

**So the driver stages `BootNext` and then resets immediately**
(`ApplyMatchedOption`). The operator stages a target and issues one reboot; the
firmware reads it, acknowledges it, arms `BootNext`, resets, and the next boot
lands on the target. Verified 2026-08-02: one `systemctl reboot` with `Pxe`
staged produced `[Bds] Expand Fv(...)/FvFile(B68653C7-…)` — iPXE — followed by
a fall-through to the NVMe when iPXE exited.

That reset is only safe because of the FADT fix below. UefiPayloadPkg's
`ResetSystemLib` writes `mAcpiBoardInfo.ResetValue` for both `EfiResetCold` and
`EfiResetWarm`, and that value comes from the FADT — which is now `0x0e`. With
the old `0x06` this path would have hung in raminit exactly like
`systemctl reboot` did.

**Booting the option directly does not work from here**, which is worth knowing
before trying it again. The obvious cheap fix is to skip `BootNext` and call
`EfiBootManagerBoot()` on the matched option — same boot, no reset. But
`StartImage()` requires `TPL_APPLICATION`, and this driver is invoked from
`RedfishConfigHandlerDriver`'s service-discovered *event notification*. Measured
on hardware: `TPL 8` (`TPL_CALLBACK`). Nor can it be deferred to a better
context — UEFI only permits event notify TPLs of `TPL_CALLBACK` or
`TPL_NOTIFY`, so no callback ever runs at `TPL_APPLICATION`. The code keeps the
`EfiBootManagerBoot()` path behind a TPL check anyway, so it becomes live for
free if this ever moves to a caller at application level.

The remaining same-boot option, if the extra reset ever becomes intolerable, is
to run the whole exchange before BdsEntry's snapshot at End-of-DXE — connecting
USB/ECM/SNP/IP4/HTTP/REST EX by hand, which lengthens *every* boot including the
ones with nothing staged.

**Acknowledge before booting, not after.** `HandleBootOverride` clears the
override on the BMC and checks the PATCH succeeded *before* applying it, and
bails if it did not. A successful boot never returns, so an acknowledgement
placed after the boot would never be sent on exactly the runs that worked — the
BMC would still show the override staged and the host would be pinned to that
target on every subsequent boot.

### Warm reboot: the FADT must advertise a *full* CF9 reset

`systemctl reboot` used to leave the host hung at the splash -- powered, USB
enumerated, unresponsive to input -- with only a DC power cycle to recover.
Cold boots were always fine. Root-caused 2026-07-30 by bisecting the reset
method from Linux:

```sh
echo pci > /sys/kernel/reboot/type   # reboots cleanly
echo acpi > /sys/kernel/reboot/type  # hangs (this is the default)
```

Both write CF9. They differ only in the value: Linux's cold-mode `pci` path
writes `0x0e` (`FULL_RST|RST_CPU|SYS_RST`), while the ACPI path uses whatever
the FADT advertises -- and `arch_fill_fadt()` publishes `RST_CPU | SYS_RST`
(`0x06`), a *soft* reset. That leaves the platform partially powered, and
Broadwell's raminit cannot get back through it.

`mainboard_fill_fadt()` in the board port now adds `FULL_RST`, so the ACPI path
does what the working path already did. Verified: `ResetValue = 0x0e` in the
live FADT, and three consecutive `systemctl reboot` cycles each booting in
~40 s. Scoped to the mainboard on purpose -- `0x06` is right on platforms whose
raminit survives a soft reset.

Isolation notes, in case something similar appears again: the hang was **not**
USB. It reproduced with gadget rebinds suppressed, with the CDC-ECM function
disabled entirely, with the UDC fully unbound (no USB device at all), and on
the pre-session firmware restored from backup.

A warm reboot also used to lose the Redfish exchange even when it booted: the
host reset makes `f_ecm` queue a `NETWORK_DISCONNECT`, and the freshly bound
UEFI driver reads it and treats the cable as unplugged for the rest of the boot
(the matching `CONNECTED` having been emitted while nothing was listening).
Patch `0002` therefore makes `CableDetect` sticky as well as defaulting it
to 1 -- see trap 4 above.

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

## Network boot: the LOM needs a UEFI driver of its own

Nothing in stock UefiPayloadPkg publishes `EFI_SIMPLE_NETWORK_PROTOCOL` for the
onboard I218-V. It carries prebuilt Realtek and ASIX UNDI blobs and no Intel
one, and `SnpDxe` only layers SNP over an *existing* UNDI/NII instance. So the
BMC's CDC-ECM gadget was the only network interface the firmware exposed.
Measured on hardware 2026-08-04, before the fix:

```text
NucRedfishSync: 2 SNP handle(s) in the system
NucRedfishSync:   SNP[0] DA:A7:62:23:3E:F5
NucRedfishSync:   SNP[1] DA:A7:62:23:3E:F5
```

Both the gadget; the LOM (`B8:AE:ED:7E:3F:6E`) absent.

That is what broke netboot. A chainloaded iPXE **`snp.efi`** — which is what
Tinkerbell boots — contains no native drivers at all: it binds SNP handles and
nothing else. With only the management link publishing one, it retried DHCP over
the RHI until it hit its retry limit, with no second interface to fall through
to.

**The fix has two halves**, and both are needed:

1. **`ipxe-intel.efidrv`** — iPXE built as a UEFI *driver* rather than an
   application (`bin-x86_64-efi/intel.efidrv`, via `interface/efi/efi_snp.c`),
   embedded in the DXE FV next to the Realtek and ASIX blobs. It is the only
   thing that drives this NIC: edk2 has **no** driver for it. Nothing in the
   tree matches `8086:15a3` or any PCI network class, and the one UNDI in
   edk2-platforms (`OptionRomPkg/UndiRuntimeDxe`) targets `8086:1229`, a 1990s
   EtherExpress PRO/100. `SnpDxe`, `UefiPxeBcDxe`, `Ip4Dxe` and friends are all
   consumers — they need a UNDI/NII or SNP from somewhere.

2. **`NETWORK_PXE_BOOT=TRUE`** — edk2's own PXE stack. It was never enabled, so
   the payload had no PXE boot method at all. The `PXEv4 (MAC:...)` entries that
   used to appear were iPXE's own `EFI_LOAD_FILE_PROTOCOL`, not this stack.

iPXE is built with **`EFI_DOWNGRADE_UX`** so it does *not* install its own
`EFI_LOAD_FILE_PROTOCOL`. BDS creates a boot option for every handle carrying
LoadFile, so without this the NIC appeared twice and the entry BDS picked was
iPXE's, which fails:

```text
Booting from 'PXEv4 (MAC:B8AEED7E3F6E)' failed: Not Found
Verify it contains/points to a valid 64-bit UEFI OS.
Press any key to continue
```

With nothing to fall back to that parks the machine at that prompt — recoverable
by sending a keypress over the JetKVM, no power cycle needed. iPXE anticipates
this exactly: its comment in `efi_snp.c` notes the two cannot sensibly coexist
because the boot menu labels both entries identically, and offers the switch to
suppress its own.

So the boot path is entirely edk2's, with iPXE only at the bottom:

```text
edk2 PXE BC -> Ip4 -> MNP -> SNP -> iPXE UNDI -> I218-V
```

The end state is one boot option per real device:

```text
Boot0001* NVMe: PM951 NVMe SAMSUNG 256GB
Boot0002* PXEv4 (MAC:B8AEED7E3F6E)   PciRoot()/Pci(0x19,0)/MAC()/IPv4()
Boot0003* UEFI Shell
Boot0004* debian
```

Verified end to end 2026-08-04: CaptainOS (Tinkerbell) netboots from that entry.

Note the DSC forces the Realtek and ASIX UNDI blobs on with `NETWORK_PXE_BOOT`;
they are inert here but do occupy FV space.

`PlatformBootManagerLib` additionally prunes auto-created network boot options
that traverse a USB node — that is the BMC's host interface, a management link
with no DHCP server on it — and any duplicate MAC, stripping the `" 2"` BDS
appends. See patch 0003.

### Approaches that do not work — do not retry these

All tried on hardware 2026-08-02/04.

**Reordering the interfaces is impossible.** `device pci 14.0` (xHCI) and
`19.0` (GbE) in `devicetree.cb` *describe* fixed Wildcat Point-LP PCH functions.
coreboot enables or disables them; it cannot renumber silicon, so any ascending
PCI scan reaches xHCI — and therefore the gadget — before the LOM. ACPI
(`lan.asl`) describes the device to the OS and has no bearing on UEFI driver
binding or SNP creation either.

**Disconnecting the NIC in firmware does not hide it from a full iPXE.**
`gBS->DisconnectController()` on the host-interface NIC succeeds and tears down
its SNP/IP4/REST EX — which *does* hide it from `snp.efi` — but a full
`ipxe.efi` re-enumerates the USB bus with its own xHCI driver and binds the
gadget regardless:

```text
NucRedfishSync: disconnected host-interface NIC DA:A7:62:23:3E:F5 - Success
... net0mac=da:a7:62:23:3e:f5 net0chip=cdc-ecm     <- iPXE, with snpnet AND ecm linked
```

**Detaching the gadget from the BMC was abandoned.** Removing the ECM function
needs a gadget rebind that races BDS launching the boot option, and it left a
warm-rebooted host with no host interface at all — the re-attach only fires on a
DC power-on transition, which `systemctl reboot` never produces.

**Restricting the payload iPXE's driver set was a dead end too.** Emptying
`DRIVERS_usb_net`/`DRIVERS_efi_net` did make the LOM `net0` for the binary we
build, but it does nothing for a chainloaded one — which is the case that
matters. Removed once the driver landed.

## Bootsplash

`EDK2_BOOTSPLASH_FILE` defaults to the layer's `files/bootsplash.bmp`, installed
verbatim; any other path is converted with ImageMagick at configure time. It must
be **uncompressed 24-bit BMP3** — edk2's `BmpSupportLib` rejects RLE-compressed
BMPs, and ImageMagick produces those for palette images, so keep `-type TrueColor`.
The regeneration command is in the recipe. Size is not a concern: a mostly-black
BMP LZMA-compresses to ~7 KiB inside CBFS.
