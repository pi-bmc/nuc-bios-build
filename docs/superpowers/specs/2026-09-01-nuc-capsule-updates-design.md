# NUC capsule updates: design

Status: approved for planning · 2026-09-01

Give the NUC payload the ability to apply UEFI FMP capsules the BMC stages on
its USB mass-storage volume, closing the four-duty contract in
`nanokvm-app/.claude/docs/host-firmware-contract.md`.

## Verdict up front

Almost nothing needs inventing. coreboot at our pinned SRCREV (`149d149`)
already carries the Dasharo capsule work, and MrChromebox's UefiPayloadPkg
already carries the payload half. Both are switched off. What is genuinely
missing is a **bridge**: the BMC delivers capsules as *files on a volume*,
while every delivery path Dasharo implemented is an OS calling
`UpdateCapsule()` at runtime. That bridge is this project.

## Upstream status

coreboot's half is upstream at our SRCREV. The **payload half is not**: coreboot's
`Documentation/drivers/efi_capsule_generation.md` tracks it as
tianocore/edk2 PR **#12053**, warning that "older EDK2 trees may be missing
pieces required by this integration".

Our pinned MrChromebox fork already carries it -- `FmpDeviceSmmLib`,
`ParseCapsules()` and `CB_TAG_CAPSULE` are all present and verified. So we
inherit the payload changes without patching them in ourselves, and edk2-tree
edits stay limited to DSC/FDF wiring delivered as quilt patches, keeping
upstream pullable.

## What already exists (verified, not assumed)

**coreboot** (`src/drivers/efi/`):
* `DRIVERS_EFI_UPDATE_CAPSULES` Kconfig, `capsules.c` (24 KB)
* `efi_parse_capsules()` reads `CapsuleUpdateData*` from SMMSTORE, coalesces
  the SG lists, reserves the memory, publishes via CBMEM and coreboot table
  tag `CB_TAG_CAPSULE` (0x0046)
* SMMSTORE full-flash access, gated on that same Kconfig (below)

**Payload** (`UefiPayloadPkg`):
* `UefiPayloadEntry.c:478` `ParseCapsules()` → `EFI_HOB_TYPE_UEFI_CAPSULE`
* `CbParseLib.c:914` reads `CB_TAG_CAPSULE`
* `CapsuleRuntimeDxe`, `DxeCapsuleLibFmp`, `EsrtDxe`, `FmpAuthenticationLibPkcs7`
* `FmpDxe` with `FmpDeviceLib|UefiPayloadPkg/Library/FmpDeviceSmmLib` when
  `BOOTLOADER == COREBOOT` -- the SMI-based flash writer
* `BdsDxe` at `UefiPayloadPkg.dsc:1192` (our NULL-link target)
* `SpiFlashLib` at `UefiPayloadPkg.dsc:611`

All gated behind `DEFINE CAPSULE_SUPPORT = FALSE`.

## Feasibility: the descriptor permits it

`ifdtool -d stock-bios.rom` on this board:

```
FLMSTR1 (Host CPU/BIOS)
  Host CPU/BIOS Region Write Access: enabled
  Intel ME Region Write Access:      disabled
  Flash Descriptor Write Access:     disabled
Flash Region 1 (BIOS): 001a0000 - 007fffff
```

The host CPU may write the BIOS region. Dasharo's HAP/ME-disable requirement
comes from boards whose descriptor denies that, forcing ME to be paused --
and pausing ME needs a cold reboot that clears RAM, which is what destroys
their in-RAM capsules. **That class of problem does not apply here.** The only
remaining write gate is the PCH's `BIOS_CNTL` (BIOSWE/BLE) and protected-range
registers, which is exactly what SMM-based writing exists to lift.

The reported BIOS region matches the FMAP exactly (`BIOS@0x1a0000 0x660000`).

## Architecture

**Dasharo's flash path, our delivery path.**

```
BMC stages \EFI\UpdateCapsule\*.cap on the USB mass-storage LUN
   |
   v
NucCapsuleOnDiskLib  (NULL-linked into BdsDxe, fires at ReadyToBoot)
   |  connect USB mass-storage, walk every FAT volume's drop box
   v
gRT->UpdateCapsule(HeaderArray, 1, 0)        <-- flags = 0, FLAGLESS
   |
   v
CapsuleService.c:145 -- no PERSIST_ACROSS_RESET, so:
ProcessCapsuleImage() under boot services
   |
   v
DxeCapsuleLibFmp -> FmpDxe (FV-resident) -> FmpDeviceSmmLib
   |
   v
SMI -> coreboot SMMSTORE handler, SMMSTORE_CMD_USE_FULL_FLASH -> SPI write
   |
   v
verify -> delete the capsule file -> cold reset
```

Single boot. No RAM survival, no capsule reassembly, no ME pause, no second
reboot, no cross-boot marker.

### Why flagless rather than Dasharo's persist-across-reset

Dasharo's blog is candid that the reset path is their weakest area: capsule
reassembly from fragmented memory is "tedious, error prone and potentially
insecure", and the ME cold-reboot interaction is what forces HAP. Every one of
those costs exists to move capsule *bytes* across a reset. Our bytes are
already on a filesystem the firmware can read, so the entire mechanism is
unnecessary. `CapsuleService.c:145` routes a flagless capsule directly to
`ProcessCapsuleImage()`; the scan IS the processing.

Corollary: `PcdCapsuleOnDiskSupport` stays **FALSE**. Upstream's Capsule-on-Disk
parks capsules across a reset for PEI to coalesce, and a payload has no PEI
phase. The `OsIndicationsSupported` bit it would advertise is OR'd back in by
the scanner so an OS-side deliverer can still discover the drop box.

## The non-obvious coreboot dependency

`CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y` is required **even though we never use
coreboot's capsule parser**. From `src/drivers/smmstore/store.c`:

```c
static enum cb_err lookup_store_region(struct region *region)
{
	if (CONFIG(DRIVERS_EFI_UPDATE_CAPSULES) && smmstore_use_full_flash) {
		const struct region_device *rdev = boot_device_rw();
		...
		*region = *region_device_region(rdev);   /* the WHOLE chip */
```

and `smmstore_preprocess_cmd()` wraps the entire `SMMSTORE_CMD_USE_FULL_FLASH`
handshake in the same `CONFIG()` test. Without the Kconfig the SMI handler is
confined to the 512 KB SMMSTORE FMAP region and `FmpDeviceSmmLib` physically
cannot reach the BIOS region. This is the single easiest thing to get wrong.

## Region preservation -- what actually happens

Inside `BIOS@0x1a0000 0x660000`: `RW_MRC_CACHE` (0x10000), `SMMSTORE(PRESERVE)`
(0x80000), `FMAP` (0x1000), `CONSOLE` (0x20000), `COREBOOT(CBFS)` (remainder).

**`FmpDeviceSmmLib` writes the whole BIOS region and honors no allowlist.** Its
own header says so:

> the BIOS region).  In the future, this can be made more flexible, say, by
> parsing coreboot's FMAP and only updating some regions.

Region selectivity is explicitly unimplemented. `DRIVERS_EFI_CAPSULE_REGIONS`
does not change this -- it is a *capsule-generation* input (an allowlist
manifest embedded in the ROM), consumed when building the capsule, not by the
flasher. Nothing in our payload reads it.

Two consequences, both by design and both handled upstream:

1. **SMMSTORE is overwritten**, so the running firmware's variable store
   disappears mid-update. The library handles this: on success it swaps
   `gRT->GetVariable` / `SetVariable` / `GetNextVariableName` /
   `QueryVariableInfo` for no-op stubs and recomputes `gRT->Hdr.CRC32`, because
   both coreboot's SMI handler and EDK2's variable services would otherwise be
   wrong about the region's location, size and contents -- and touching it
   could corrupt the *new* image.
2. **The new firmware cannot report the flash result.** The library states
   plainly: "New firmware will not report result of flashing in any way unless
   some kind of communication mechanism is implemented for this purpose."

**Ordering constraint this imposes on the scanner:** after a successful
`SetImage`, no variable write can succeed. The scanner must therefore read
`LastAttemptStatus` from the in-memory FMP descriptor (not a variable), delete
the capsule file (filesystem, still available), and cold reset -- in that
order, with no `SetVariable` in between. Any marker-variable scheme is
impossible here, which independently confirms the flagless design: there is no
second boot to correlate with.

The capsule payload is consequently a **full BIOS-region image**
(0x660000 bytes at 0x1a0000), and a capsule update resets the variable store.
That is a real behavioural consequence -- boot entries, Secure Boot state and
CFR settings do not survive an update -- and it must be stated in operator
docs rather than discovered.

`FmpDeviceSmmLib`'s own `IoError` path names the failure causes worth watching:
an actual flash fault, a bug in coreboot's SMMSTORE SMI handler, coreboot not
lifting flash protections, or Intel ME not being disabled. Our descriptor check
rules out the ME case for this board.

## Components

| Unit | Home | Responsibility |
| --- | --- | --- |
| `NucCapsuleOnDiskLib` | `NucRedfishPkg/Library/` (staged source) | Walk the drop box, apply flagless, verify, delete, reset |
| Capsule payload parsing | same, own file | Header validation, `PERSIST_ACROSS_RESET` rejection -- pure logic, unit tested |
| FDF/DSC wiring | edk2 quilt patch | `FmpDxe` into the FV; NULL-link the scanner into `BdsDxe` |
| Build flags | `edk2-uefipayload_2605.bb` | `CAPSULE_SUPPORT=TRUE`, `CAPSULE_MAIN_FW_GUID` |
| Signing | recipe | Replace `Pkcs7Sign/TestRoot.cer` |
| coreboot config | `nuc5i7ryh.config` | The Kconfigs above |
| `SoftwareInventory` PATCH | `NucRedfishSyncDxe` | Contract duty 4 |

The scanner lives in `NucRedfishPkg` as staged source, matching `UsbCdcAcmDxe`.
Only the DSC/FDF hunks -- edits to files that exist upstream -- go in a quilt
patch, so upstream edk2 stays pullable.

## Port notes from `RpiCapsuleOnDiskLib`

Ports unchanged: the scan loop, the `\EFI\UpdateCapsule` path, USB
mass-storage connect (so the BMC LUN is reachable on a boot that never chose
it), the deliberate refusal to `ConnectAll`, `PERSIST_ACROSS_RESET` rejection,
delete-on-success as the applied-vs-pending signal, `LastAttemptStatus` on
failure, cold reset after success, and the `ReadyToBoot`/TPL_CALLBACK
placement.

Changes:

* **Verification.** rpi5 proves the write by re-reading `RPI_EFI.fd` on the FAT
  volume. There is no such file here and `FmpDeviceSmmLib` returns
  `EFI_UNSUPPORTED` for the FMP read-back entry points, so `GetImage()` is not
  available. v1 uses the FMP descriptor's `LastAttemptStatus`, which
  `DxeCapsuleLibFmp` records from `SetImage()`. A `SpiFlashLib` byte-level
  read-back is possible and is listed as optional hardening, not v1 scope.
* **Dropped:** `InstallSidecarConfig` (config.txt is Pi-only), `ArmPkg` and
  `RaspberryPi.dec`, `PcdNvStorageVariableBase`, `PcdFdBaseAddress`.
* **`FmpDxe` becomes FV-resident.** Today it is built only as a
  capsule-embedded driver. With `SECURE_BOOT_ENABLE=TRUE`, coreboot's Kconfig
  warns an embedded driver must be signed by a trusted key or processing
  fails. FV-residency sidesteps that and matches rpi5.

rpi5's hard-won comment carries over verbatim and is the most important line in
the port:

> `UpdateCapsule()` success means "processed", never "applied" -- upstream
> records the `SetImage` status and returns success either way.

Never delete the capsule file on `UpdateCapsule()` returning `EFI_SUCCESS`
alone.

## Configuration

**coreboot** (`nuc5i7ryh.config`), Kconfig names verified against
`src/drivers/efi/Kconfig`:
* `CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y`
* `CONFIG_DRIVERS_EFI_FW_INFO=y`
* `CONFIG_DRIVERS_EFI_MAIN_FW_GUID="d25f89e1-94ec-4533-80b9-7f8855ce0a94"`
  (must equal the payload's `CAPSULE_MAIN_FW_GUID`; coreboot's default is the
  placeholder `00112233-4455-6677-8899-aabbccddeeff`)
* `CONFIG_DRIVERS_EFI_MAIN_FW_VERSION` and `CONFIG_DRIVERS_EFI_MAIN_FW_LSV` for
  ESRT reporting and rollback policy -- both default 0, which is not a policy
* `DRIVERS_EFI_VARIABLE_STORE` and `SMMSTORE` are already on
* `GENERATE_CAPSULE` / `EMBED_FMP_DXE` stay **off**: they assume coreboot
  builds edk2, and ours consumes a prebuilt `UEFIPAYLOAD.fd` via `PAYLOAD_FILE`

**Payload** (`EDK2_BUILD_FLAGS`):
* `-D CAPSULE_SUPPORT=TRUE`
* `-D CAPSULE_MAIN_FW_GUID=d25f89e1-94ec-4533-80b9-7f8855ce0a94` -- `FmpDxe` interprets its `FILE_GUID`
  as the firmware GUID, and it must match the capsule and the ESRT entry
* Keep `DisplayUpdateProgressLibText`. Dasharo hit unpredictable placement with
  the graphics variant tied to boot-logo dimensions, and our DSC already notes
  the graphics lib *aborts the update* when GOP is missing.

**Signing:** the DSC `!include`s `BaseTools/Source/Python/Pkcs7Sign/TestRoot.cer`
-- EDK2's public test certificate. Shipping it means any capsule validates.
Replace it the way rpi5 does with `rpi5_fmp_resolve_keys` and
`PcdFmpDevicePkcs7CertBufferXdr`.

## Error handling

* Capsule fails authentication or version gate: leave the file, record
  `LastAttemptStatus`, continue booting. Never delete.
* `UpdateCapsule()` returns an error: leave the file, report, continue.
* `UpdateCapsule()` returns success but `LastAttemptStatus` says failure: leave
  the file, report. This is the case the rpi5 comment exists to catch.
* Capsule carries `PERSIST_ACROSS_RESET`: reject and report -- this platform
  applies synchronously and has no PEI phase to coalesce for.
* Read-only media: apply, warn, leave the file. The BMC will see it as pending;
  that is honest.
* Success: delete, then cold reset into the new firmware.

## Testing

Unit-testable on the build host, the same harness shape as the CDC-EEM framing
tests: capsule header parsing, the drop-box walk's file filtering, and
`PERSIST_ACROSS_RESET` rejection.

Everything past `UpdateCapsule()` requires the board. Given the brick risk,
first hardware validation happens on a unit with a SOIC-8 clip attached.

Accepted, per the operator: the first capsule-capable build must be flashed
externally, because the machinery has to be in the *running* firmware before a
capsule can be applied. Dasharo hit the same wall ("only future updates can
leverage this method").

## Out of scope

* coreboot's `CapsuleUpdateData*` reset path (we deliberately do not use it)
* OS-side delivery (fwupd, `/dev/efi_capsule_loader`) -- the drop box is
  advertised via `OsIndicationsSupported`, but nothing here depends on it
* `SpiFlashLib` byte-level verification -- optional hardening, post-v1
