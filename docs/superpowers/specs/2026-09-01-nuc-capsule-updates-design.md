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

## Upstream status and the RMAP patch

coreboot's half is upstream at our SRCREV. The payload's *base* half is already
in our pinned MrChromebox fork -- `FmpDeviceSmmLib`, `ParseCapsules()` and
`CB_TAG_CAPSULE` are all present and verified.

On top of that we carry **tianocore/edk2 PR #12861** (StarLabs branch
`agent/upstream-rmap-capsule`), which is not upstream yet. It is 13 files,
~102 KB, all confined to `UefiPayloadPkg`, and it is what makes this design
safe rather than merely possible:

| file | what it adds |
| --- | --- |
| `FmpDeviceSmmManifest.{c,h}` | RMAP region-manifest parsing |
| `FmpDeviceSmmUpdatePolicy.{c,h}` | overlap detection, variable-service policy |
| `FmpDeviceSmmFlashRetry.{c,h}` | transient flash-error retry |
| `Tools/AppendRmapManifest.py` | builds the manifest trailer |
| `FmpDeviceSmmLibUnitTest.c`, `UefiPayloadPkgHostTest.dsc` | 15 host-runnable unit tests |

It is delivered as a quilt patch in the recipe series, exactly like every other
edk2 change here, so upstream edk2 stays pullable and the patch retires when
the PR merges.

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

## Region preservation -- solved by the RMAP manifest

Inside `BIOS@0x1a0000 0x660000`: `RW_MRC_CACHE` (0x10000), `SMMSTORE(PRESERVE)`
(0x80000), `FMAP` (0x1000), `CONSOLE` (0x20000), `COREBOOT(CBFS)` (remainder).

Without PR #12861, `FmpDeviceSmmLib` writes the **whole BIOS region** and honors
no allowlist -- its header calls region selectivity future work. That is the
behaviour the PR implements.

**RMAP manifest.** A trailer appended to the firmware image inside the capsule:

```c
#define REGION_MANIFEST_SIGNATURE  SIGNATURE_32 ('R','M','A','P')
typedef struct { UINT32 Signature; UINT16 Version; UINT16 EntryCount; }
  REGION_MANIFEST_TRAILER;
typedef struct { CHAR8 RegionName[16]; } REGION_MANIFEST_ENTRY;
```

It lists the FMAP regions the update may program. `AppendRmapManifest.py`
builds it. **If the manifest is absent, the firmware falls back to full-flash**
-- so it is opt-in, and omitting it silently restores the destructive
behaviour.

**Our manifest is `COREBOOT` only.** `SMMSTORE`, `RW_MRC_CACHE` and `FMAP` are
excluded, which buys three things the earlier full-flash design could not have:

1. **The variable store survives.** The library checks whether any write range
   overlaps the live SMMSTORE range; if none does it sets
   `VariableStorePreserved`, and
   `FmpDeviceShouldDisableVariableServices (UseManifest && VariableStorePreserved)`
   then leaves `gRT`'s variable services intact instead of swapping in no-op
   stubs. Boot entries, Secure Boot state and CFR settings survive an update.
2. **`LastAttemptStatus` persists.** With variable services alive, FmpDxe can
   write the final status -- which is what contract duty 4's
   `SoftwareInventory` PATCH needs to report something true.
3. **No ordering constraint on the scanner.** Because variable services are not
   stubbed, the scanner may write variables after a successful `SetImage`. The
   flagless design does not depend on this, but it removes a sharp edge.

`RW_MRC_CACHE` staying out of the manifest also means no memory-retraining boot
after an update.

The library's `IoError` path names the failure causes worth watching: a real
flash fault, a bug in coreboot's SMMSTORE SMI handler, coreboot not lifting
flash protections, or Intel ME not being disabled. Our descriptor check rules
out the ME case for this board.

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

PR #12861 ships `UefiPayloadPkg/Test/UefiPayloadPkgHostTest.dsc` with 15
host-runnable unit tests over exactly the dangerous logic -- manifest parsing
("Absent manifest is reported", "Malformed manifest fails closed"), range
overlap ("Overlapping ranges are detected", "Adjacent ranges do not overlap",
"Malformed ranges fail closed"), the variable-service policy ("Preserved store
keeps variable services", "Unproven store disables variable services"), step
counting, and flash retry exhaustion. These run in CI without hardware and
should be wired into the build.

Our own scanner adds host tests in the same shape as the CDC-EEM framing tests:
capsule header parsing, drop-box file filtering, and `PERSIST_ACROSS_RESET`
rejection.

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
