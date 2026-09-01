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
bitbake edk2-uefipayload # payload alone; coreboot consumes the sysroot-staged UEFIPAYLOAD.fd
```

The payload is a separate recipe from coreboot on purpose (see the DESCRIPTION in
`edk2-uefipayload_2605.bb`), so `NucRedfishPkg` and the bootsplash are staged into
the tree by `do_configure` rather than patched into a tree coreboot clones
mid-`do_compile`.

## The payload tree is upstream edk2, not the MrChromebox fork

Migrated 2026-08-04. coreboot's `payloads/external/edk2` defaults to
`EDK2_REPO_MRCHROMEBOX` / `origin/uefipayload_2605`; this recipe points at
`tianocore/edk2` master instead, pinned by SRCREV.

The fork turned out to be exactly `edk2-stable202605` **plus 103 commits and
nothing behind it**, so the delta was a patch series rather than a divergent
tree. Eighteen of those commits matter for this board and cherry-pick onto
master without a single conflict; they are `files/0001-0018` and each keeps its
original authorship plus a `(cherry picked from commit ...)` trailer. They split
into coreboot/payload correctness (MTRR programming, root bridges from HOB, the
framebuffer BAR offset, SMMSTORE block alignment, uninitialised memory in the
entry point) and features this board is configured to use (CFR SetupMenu,
`PRIORITIZE_INTERNAL`, the BGRT logo position). `files/0019-0026` are local.

### Editing these patches by hand

Two hazards, both of which fail far from the mistake:

- **edk2 sources are all-CRLF.** Any Python that inserts with `\n`, and any
  `subprocess.run(..., text=True)` capturing `diff` output, silently strips or
  mixes line endings; `patch` then says `different line endings` and rejects
  every hunk. Capture as **bytes**, and assert `LF count == CR count` on the
  result before writing.
- **Hunk headers are not recomputed for you.** Add or drop a `+` line without
  fixing `@@ -a,b +c,d @@` and `patch` reports `malformed patch at line N`
  pointing at the *next* hunk. `patchutils` (`recountdiff`) is not installed
  here; the cheap check is to count the ` `/`-`/`+` lines in each hunk and
  compare against its header before building.

The reliable way to regenerate one patch: bitbake applies the series with
**quilt**, so `build/.../git/.pc/<patch-name>/` holds the pristine copy of every
file that patch touches. Edit the file in the work tree, then
`diff -u .pc/<patch>/<file> <file>` — no arithmetic involved.

The reason to move was edk2-redfish-client: the GUIDs it needs
(`gEdkIIRedfisEventRedfishInterfaceDisconnectionGuid` and friends) are declared
by `RedfishPkg.dec` on master and by **no stable tag through 202605**. The
fork's `RedfishPkg` is byte-identical to upstream's at that tag, so the old note
blaming the fork for this was wrong — it was a stable-tag-vs-master mismatch all
along.

Things that are *not* fork-only, contrary to what this file and the recipe used
to say: SMMSTORE (the whole `SmmStoreLib`/`SmmStoreFvb` stack is upstream), the
cbmem console (`USE_CBMEM_FOR_CONSOLE`), `TIMER_SUPPORT`, `LOAD_OPTION_ROMS` and
`BOOTSPLASH_IMAGE`.

Network flags were renamed by the move. The fork's `NETWORK_PXE_BOOT` was an
aggregator that also set `NETWORK_DRIVER_ENABLE`; upstream has only
`NETWORK_DRIVER_ENABLE` plus NetworkPkg's own knobs (`NETWORK_PXE_BOOT_ENABLE`,
etc.). **`NETWORK_DRIVER_ENABLE` is the load-bearing one**: it is what makes the
DSC `!include NetworkPkg/Network.dsc.inc`, and therefore what the Redfish and
PXE blocks hang off. Drop it and every other `NETWORK_*` flag goes inert
*silently* — no HTTP, no REST EX, no host interface, and no build error.

### Two upstream bugs this exposed

Both are the same shape, and both fail silently. `UefiPayloadPkg` lists two
protocol producers only under `[Components.AARCH64]` and places neither in a
firmware volume on any architecture, so on X64 they are not built at all:

| Producer | Depended on by | Symptom when missing |
| --- | --- | --- |
| `RngDxe` (`EFI_RNG_PROTOCOL`) | `DxeNetLib`, so *every* NetworkPkg driver | The whole stack lands in the DXE FV and is never dispatched. No SNP, no PXE boot option, no REST EX. |
| `Hash2DxeCrypto` (`EFI_HASH2_SERVICE_BINDING_PROTOCOL`) | `TcpDxe` alone | Subtler: SNP/IP4/DHCP4/MTFTP4/HTTP/PXE all come up and netboot works, but `RedfishDiscoverDxe` wants TCP4 **and** REST EX service binding on the same handle, so discovery silently finds nothing. |

`files/0023` adds both, gated on `NETWORK_DRIVER_ENABLE`. If Redfish ever goes
quiet again with a healthy-looking network stack, check `TcpDxe` dispatched
before anything else.

The diagnostic that found this: cross-reference the FV's module list against
the drivers that actually emitted a `Loading driver` line.

```sh
FV=build/tmp/work/*/edk2-uefipayload/*/git/Build/UefiPayloadPkgX64/RELEASE_GCC/FV
grep -oE '[0-9A-Fa-f]{8}-[0-9A-Fa-f-]{27}' $FV/DXEFV.inf | sort -u > /tmp/fv.txt
grep -Ff /tmp/fv.txt $FV/Guid.xref     # GUID -> module name
```

Modules present in `DXEFV.inf` but absent from the log are depex failures.
Ignore `DxeCore`/`PcdDxe`/`DevicePathDxe`/the status-code routers (they run
before the console is up) and `UiApp`/`Shell`/`BootManagerMenuApp` (applications,
loaded on demand).

### The cbmem console had to grow

`CONFIG_CONSOLE_CBMEM_BUFFER_SIZE` is now `0x80000`. The stock 128 KiB does not
hold one boot of this payload, and what it drops is the middle — DXE dispatch
through Redfish discovery — leaving a log that starts at the bootblock and ends
at the OS handoff with a hole where the answer was. `cbmem -c | wc -c` returning
more than the buffer size is the tell.

The `CONSOLE` FMAP region is not an alternative: `board.fmd` puts it at
`0x190000`, below the BIOS region (`0x1a0000`), so the running ME refuses the
read and `flashrom --fmap -i CONSOLE -r` fails with `Transaction error!`. That
one needs the SOIC-8 clip.

### Flashing wipes the EFI variable store

`flashrom --ifd -i bios -w` rewrites the whole BIOS region, and `SMMSTORE` lives
inside it — the `(PRESERVE)` annotation in `board.fmd` means nothing to flashrom.
So every flash loses `BootOrder`, the `debian` entry and any staged `BootNext`,
and the first boot afterwards lands on whatever BDS auto-creates first, which is
the PXE entry. This is expected, not a regression. Steer it back with
`efibootmgr -n 0001 && reboot` from whatever booted.

If PXE has nothing to boot, BDS parks at `Booting from 'PXEv4 (MAC:...)' failed
… Press any key to continue` — send any keypress over the JetKVM and it falls
through to the NVMe. A stall at the splash right after a flash is this, not a
regression.

There is no flasher live ISO. It was removed from the tree deliberately — do not
resurrect `kas-flasher.yml` or a `flasher` multiconfig. Flash either in-band (below)
or with a SOIC-8 clip via `scripts/nuc-spi.sh`.

### Secure Boot needs the variable store to say it can hold auth variables

`SECURE_BOOT_ENABLE=TRUE` has been in the recipe all along and builds the whole
stack — the real `AuthVariableLib` rather than the Null one,
`DxeImageVerificationLib`, `RuntimeCryptLib`, `SecureBootConfigDxe` — and until
patch 0026 none of it did anything. No `PK`, no `KEK`, no `db`, and no
`SetupMode` or `SecureBoot` variable existed at all (`ls
/sys/firmware/efi/efivars/` showed 32 variables, none of them those), so the OS
saw a firmware with no Secure Boot support and Redfish had no state to report.

Nothing errors. The single tell is one line in `cbmem -c`:

```text
Variable driver will work without auth variable support!
```

The chain: `VariableRuntimeDxe` calls `AuthVariableLibInitialize` **only** when
`VariableGlobal.AuthFormat` is set, and `AuthFormat` is nothing more than "does
the variable store's signature GUID equal `gEfiAuthenticatedVariableGuid`"
(`VariableNonVolatile.c:323`). `SmmStoreFvbRuntimeDxe` formatted new stores
with `gEfiVariableGuid` unconditionally, so the initialiser was never reached —
and every authenticated variable is created *by* that initialiser. Its own
comment already knew: *"Caveat: SecureBoot requires
gEfiAuthenticatedVariableGuid type of storage"*.

Patch 0026 makes it a PCD (`PcdSmmStoreAuthenticatedVariables`), defaulted to
the old behaviour and switched on by `SECURE_BOOT_ENABLE`. **It is opt-in for a
real reason**: coreboot's SMMSTORE SMI handler writes what it is handed without
inspecting it, so the signature checks live in the DXE variable driver rather
than behind the SMM boundary. What this buys is a Secure Boot that is functional
and standards-shaped but whose root of trust stops at DXE — it verifies what it
is asked to verify, and does not defend the variable store against an attacker
who already has ring 0. Right for reporting and managing Secure Boot over
Redfish; wrong for a platform claiming SMM-anchored key protection.

The format is chosen once, when a *blank* store is initialised, and the validity
check accepts either GUID — so an existing plain store keeps working and keeps
`AuthFormat` FALSE. Turning it on takes effect on a store that is new or erased,
which every flash produces anyway.

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
cmp bgrt.bmp meta-nuc-bios/recipes-bsp/edk2-uefipayload/files/bootsplash.bmp
ssh root@10.1.40.22 'cat /sys/firmware/acpi/bgrt/status'   # 1 = actually drawn
```

This is byte-exact proof of what the payload displayed. It works because
`BootGraphicsResourceTableDxe` is in the DXE FV, which is stock upstream
behaviour gated on `BOOTSPLASH_IMAGE`. `FOLLOW_BGRT_SPEC=TRUE` is a *separate*
thing and only moves the logo to 38.2% from the top — it has no bearing on
whether the BGRT table is published.

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

- host: `meta-nuc-bios/recipes-bsp/edk2-uefipayload/files/NucRedfishPkg` (built into the
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

Everything above `NucRedfishSyncDxe` is stock RedfishPkg. `NucRedfishSyncDxe`
is the consumer of `EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL` that the payload
otherwise lacks. Since the move to upstream edk2 (below), edk2-redfish-client
is compiled in alongside it and produces that protocol too.

### Getting edk2-redfish-client to actually run

Four separate things had to be true, and each failed in a way that pointed
somewhere other than the cause. In dependency order:

**1. Do not clean up a service you were handed.** `NucRedfishSyncDxe` used to
call `RedfishCleanupService()` when its exchange finished. That was harmless
while it was the only consumer, and is not now:
`RedfishConfigHandlerDriver` creates the service once and passes the *same*
instance to every registered config handler, so destroying it tears down the
REST EX child underneath everyone who has not run yet. The ten client feature
drivers each create their own service off that interface afterwards, and every
one of them then failed on its first send with

```text
ResetHttpTslSession: TCP connection is finished...
HttpSendReceive: /redfish SendReceive failure: Not started
```

which surfaces as "no Redfish version" — so every URI the feature drivers build
comes out as `v1Systems` rather than `/redfish/v1/Systems` — plus
"CollectionHandler failure: Not started" and "Fail to dispatch Redfish tasks:
Device Error". None of it points at a cleanup call in another driver.

**2. `PcdHttpGetRetry` and friends default to 0.** RedfishHttpDxe then gives up
on the first failure, and the first failure after an idle gap is guaranteed
rather than exceptional — the BMC closes the TCP connection, `RedfishRestEx`
resets the instance and returns an error, and it is the *next* attempt that
works. Patch 0024 sets all five to 3. The log tell is `failed (1/0)`; with the
fix it reads `retry (1/3)`.

**3. The BMC has to serve the branches each feature driver walks.** They are
separate drivers and each gives up if its link is missing:
`TaskService`, `Registries`, and on the ComputerSystem `Bios`, `SecureBoot`,
`Memory` and `Boot/BootOptions`. Before this the JetKVM had none of them, and
gin fell through to the web UI's `index.html` — which is why
`RedfishTaskServiceDxe` reported `Device Error` rather than a 404. See
`redfish_client.go` in the kvm checkout.

**edk2-redfish-client is a HII bridge, not an inventory agent.** This is the
single most useful thing to know about it, and it is not obvious from the
package name. Every feature driver's only data source is
`EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL` — HII questions carrying a string in
the `x-UEFI-redfish-<schema>.<version>` language. `MemoryDxe`'s INF lists no
`gEfiSmbiosProtocolGuid` and no SMBIOS library at all. So:

- **Anything that is *inventory* has to come from somewhere else.** `MemoryDxe`
  walks all 38 Memory properties, misses every one, and POSTs an empty
  resource. The data was never absent — coreboot publishes complete SMBIOS
  type 17 (two Crucial DDR3-1600 SODIMMs with part numbers) and the OS reads
  it. `NucRedfishSyncDxe` reports the DIMMs directly, the same way it already
  reports type 0 and type 1. The Memory feature drivers are dropped from the
  build (patch 0100) because an empty member is worse than no member: it looks
  like inventory and contains none.

- **Anything that is *configuration* has to be published as HII with the right
  language string.** The four `BiosOption1..4` attributes that show up by
  default are RedfishClientPkg's own sample form
  (`HiiToRedfishBiosDxe/HiiToRedfishBiosVfr.vfr`), not this board's settings.
  Patch 0025 makes `CfrSetupMenuDxe` register
  `/Bios/Attributes/<cfr_option_name>` against each question's prompt string
  token in the `x-UEFI-redfish-Bios.v1_0_9` language, which is exactly the
  lookup `RedfishPlatformConfigDxe` performs to decide whether a question is
  visible to Redfish.

  **The string package for that language has to be created first**, and this is
  the part that is easy to get wrong. `EFI_HII_STRING_PROTOCOL.SetString` walks
  only the string packages a package list already has and returns
  `EFI_NOT_FOUND` for a language it has never carried — it does **not** create
  one. So registering into a fresh language fails outright, and `HiiSetString`
  reports it only by returning string ID 0:

  ```text
  CFR: failed to publish "power_on_after_fail" as a Redfish attribute
  ```

  `NewString` is the call that creates it, and it back-fills a blank block for
  every string ID already in use so the new package's token space stays aligned
  with `en-US` — which is what makes it possible to then `SetString` a prompt
  token minted before the package existed. A driver whose strings come from a
  `.uni` never meets this: the compiler emits one package per language in the
  source. The CFR menu is built at runtime from coreboot's blob and starts with
  `en-US` alone.

`NucRedfishPkg/RedfishConfigDriver` **was removed** (2026-08-04). Its
`mAmiSetupMap[]` described the AMI Aptio `L"Setup"` variable of the *stock
Intel BIOS* (GUID `EC87D643-…`, 566-byte varstore, `FastBoot` at
`VarOffset=0x0014`), and this machine has not run that firmware since coreboot
was flashed, so all 15 rows described a BIOS that is not there. It also produced
`gEdkIIRedfishPlatformConfigProtocolGuid`, the same protocol as stock
`RedfishPlatformConfigDxe` — two producers, and which one a consumer's
`LocateProtocol` found was not defined. `RedfishPlatformConfigDxe` is what
answers now, over the CFR questions above. `gAmiSetupFormsetGuid` went with it.

**4. `RedfishResourceIdentifyLibComputerSystem` cannot work on this board.** It
matches the resource's `UUID` against SMBIOS type 1 to pick this host's system
out of a BMC managing several. coreboot leaves that UUID unset here
(`dmidecode -s system-uuid` says `Not Settable`), so it returns `EFI_NOT_FOUND`
and the member is skipped with `"/redfish/v1/Systems/1" is not handled by us`.
It is also the wrong question on a host interface: one host, one system, nothing
to disambiguate. Patch 0100 — the only patch that applies to the
edk2-redfish-client tree rather than edk2, hence the `patchdir` in SRC_URI —
resolves the library to the Null instance, which accepts the resource.

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
JETKVM_LOG_INFO=redfish,usb nohup setsid /userdata/jetkvm/bin/jetkvm_app \
  > /userdata/jetkvm/redfish-verify.log 2>&1 < /dev/null &
```

Use `nohup setsid` and a log path of your own. A bare `setsid ... &` over ssh
does not always survive the session closing, and when the app is restarted by
something else it truncates `last.log` — so the run you were trying to capture
disappears just as you go to read it. (`disown` is not available on this
BusyBox shell.)

**Everything `redfish_client.go` holds is in memory and is lost on app
restart.** That is deliberate — a stale copy would claim knowledge of a host
that may since have changed — but it means "the collection is empty" can mean
"the app restarted", not "the host never reported". Check for a fresh
`JetKVM Starting Up` before concluding anything.

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

Do **not** clean up such a tunnel with `pkill -f <the port spec>`: the wrapper
shell's own command line contains that string, so pkill kills the shell running
it (exit 144, no tunnel, no error). Use a fresh local port instead.

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
appends. See patch 0021.

**Known open issue: the prune no longer wins.** With edk2-redfish-client
compiled in, `RedfishClientPkg/HiiToRedfishBootDxe`'s `RefreshBootOrderList()`
calls `EfiBootManagerRefreshAllBootOption()` of its own accord, *after* BDS —
by which time the CDC-ECM NIC's PXE stack has finally come up (it does not
during `EfiBootManagerConnectAll()`; the second
`RedfishDiscoverDriverBindingStart` in the log is well after `BdsWait`). So the
host-interface `PXEv4 (MAC:DAA762233EF5)` entry is recreated after the prune
deleted it, and reappears in `efibootmgr`. The prune itself is fine — it simply
runs too early and is no longer the last writer.

The right fix is to stop deleting after the fact and filter at the source:
install `EDKII_PLATFORM_BOOT_MANAGER_PROTOCOL`
(`gEdkiiPlatformBootManagerProtocolGuid`), whose `RefreshAllBootOptions()` hook
`EfiBootManagerRefreshAllBootOption()` calls internally — see
`MdeModulePkg/Library/UefiBootManagerLib/BmBoot.c`. Every caller then goes
through the filter, whatever the ordering. Do not try to fix this by chasing
callers or by re-running the prune from a later event.

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
