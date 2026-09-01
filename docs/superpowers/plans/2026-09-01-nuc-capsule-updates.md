# NUC Capsule Updates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the NUC payload apply UEFI FMP capsules the BMC stages on its USB mass-storage volume, closing the four-duty host-firmware contract.

**Architecture:** Dasharo's flash path, our delivery path. coreboot's capsule support and MrChromebox's payload half already exist and are switched off; tianocore PR 12861 adds RMAP region selectivity on top. A `ReadyToBoot` scanner walks `\EFI\UpdateCapsule\` and calls `gRT->UpdateCapsule(..., 0)` — flagless — so the capsule is applied in the same boot with no reset, no RAM survival and no coreboot capsule parsing.

**Tech Stack:** EDK2 (UefiPayloadPkg, DXE_DRIVER C), coreboot (Kconfig), Yocto/bitbake, quilt patches, GenerateCapsule.py / AppendRmapManifest.py, gcc for host unit tests.

**Spec:** `docs/superpowers/specs/2026-09-01-nuc-capsule-updates-design.md`

## Global Constraints

- **Repo:** `nuc-bios-build` only, branch `feat/rhi-cdc-eem`.
- **Firmware GUID `d25f89e1-94ec-4533-80b9-7f8855ce0a94`** must be identical in FOUR places: coreboot `CONFIG_DRIVERS_EFI_MAIN_FW_GUID`, payload `-D CAPSULE_MAIN_FW_GUID`, the generated capsule, and the ESRT entry. A mismatch means the capsule silently matches no FMP.
- **RMAP manifest is `COREBOOT` only.** `SMMSTORE`, `RW_MRC_CACHE` and `FMAP` must be excluded. An absent manifest silently falls back to destructive full-flash.
- **`CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y` is required** even though we never use coreboot's capsule parser — it is what unlocks full-flash access in the SMMSTORE SMI handler (`lookup_store_region()` only returns `boot_device_rw()` when it is set).
- **`PcdCapsuleOnDiskSupport` stays FALSE.** A payload has no PEI phase.
- **Capsules are applied FLAGLESS** — `gRT->UpdateCapsule(HeaderArray, 1, 0)`. Reject any capsule carrying `CAPSULE_FLAGS_PERSIST_ACROSS_RESET`.
- **`UpdateCapsule()` success means "processed", never "applied".** Never delete a capsule file on `EFI_SUCCESS` alone; check the FMP descriptor's `LastAttemptStatus`.
- **edk2 patch line endings are MIXED:** LF for commit prose and diff metadata (`diff --git`, `---`, `+++`, `@@`), CRLF for body lines. `git apply` tolerates a mismatch; quilt (what bitbake uses) does not. Never edit a `.patch` with a text editor — bytes-mode Python only.
- **Builds go through kas:** `kas shell -c 'bitbake edk2-uefipayload'`. Never bare bitbake.
- **RELEASE build strips `DEBUG()`.** A `strings` grep is not a build check.
- **Do not commit the `poky` submodule** if kas moves it.

---

## File Structure

| Path | Responsibility |
| --- | --- |
| `meta-nuc-bios/recipes-bsp/edk2/files/0034-UefiPayloadPkg-RMAP-region-manifest-for-SMMSTORE-back.patch` | tianocore PR 12861 verbatim |
| `meta-nuc-bios/recipes-bsp/edk2/files/0035-UefiPayloadPkg-make-FmpDxe-FV-resident.patch` | FDF wiring + scanner NULL-link |
| `meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg/Library/NucCapsuleOnDiskLib/NucCapsuleParse.{c,h}` | Pure capsule/drop-box logic — the only unit-tested part |
| `.../NucCapsuleOnDiskLib/NucCapsuleOnDiskLib.{c,inf}` | Scanner: ReadyToBoot, USB connect, apply, verify, delete, reset |
| `hack/nuccapsule/test/` | Host gcc test harness + EDK2 stub headers |
| `meta-nuc-bios/recipes-bsp/coreboot/files/payload-edk2.config` | coreboot capsule Kconfigs |
| `meta-nuc-bios/recipes-bsp/edk2/edk2-uefipayload_2605.bb` | SRC_URI, `CAPSULE_SUPPORT`, GUID, capsule generation, signing |
| `.../NucRedfishPkg/NucRedfishSyncDxe/NucRedfishInventory.c` | Duty 4: `SoftwareInventory` PATCH |

---

### Task 1: Carry tianocore PR 12861 as patch 0034

**Files:**
- Create: `meta-nuc-bios/recipes-bsp/edk2/files/0034-UefiPayloadPkg-RMAP-region-manifest-for-SMMSTORE-back.patch`
- Modify: `meta-nuc-bios/recipes-bsp/edk2/edk2-uefipayload_2605.bb`

**Interfaces:**
- Consumes: nothing.
- Produces: `FmpDeviceLocateRegionManifest()`, `FmpDeviceFlashRangesOverlap()`, `FmpDeviceShouldDisableVariableServices()`, `FmpDeviceGetFlashRangeStepCount()`, `UefiPayloadPkg/Tools/AppendRmapManifest.py`, and `UefiPayloadPkg/Test/UefiPayloadPkgHostTest.dsc` — all consumed by later tasks.

The upstream diff has ALREADY been verified to apply cleanly to our pinned tree
(`patch -p1 --dry-run`, 13/13 files clean) and already uses this repo's exact
line-ending convention (83 metadata lines LF, 3194 body lines CRLF). Do not
regenerate or reformat it.

- [ ] **Step 1: Fetch the upstream diff**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
curl -sSL --max-time 120 -o /tmp/rmap-capsule.diff \
  "https://github.com/tianocore/edk2/compare/master...StarLabsLtd:edk2:agent/upstream-rmap-capsule.diff"
test "$(grep -c '^diff --git' /tmp/rmap-capsule.diff)" = 13 || echo "UNEXPECTED FILE COUNT"
```

Expected: 13 files, ~102 KB.

- [ ] **Step 2: Prepend a quilt header, bytes-mode**

```bash
python3 - <<'PY'
from pathlib import Path
body = Path("/tmp/rmap-capsule.diff").read_bytes()
hdr = b"""From: appkins <nbatkins@gmail.com>
Date: Tue, 1 Sep 2026 14:00:00 -0500
Subject: [PATCH] UefiPayloadPkg: RMAP region manifest for SMMSTORE-backed updates

tianocore/edk2 PR #12861 (StarLabs branch agent/upstream-rmap-capsule),
carried verbatim until it merges upstream.

FmpDeviceSmmLib writes the whole BIOS region and honours no allowlist --
its own header calls region selectivity future work. That means a capsule
update overwrites SMMSTORE, destroying the variable store that backs boot
entries, Secure Boot state and CFR settings, and forcing the library to
swap gRT's variable services for no-op stubs mid-update.

This adds an RMAP manifest: a trailer on the firmware image listing the
FMAP regions the update may program. With SMMSTORE excluded, no write
range overlaps the live store, VariableStorePreserved is set, and the
variable services stay intact -- which in turn lets FmpDxe persist the
final LastAttemptStatus.

Also brings 15 host-runnable unit tests over the dangerous logic
(manifest parsing, range overlap, variable-service policy, flash retry).

Upstream-Status: Backport [https://github.com/tianocore/edk2/pull/12861]

---

"""
out = Path("meta-nuc-bios/recipes-bsp/edk2/files/0034-UefiPayloadPkg-RMAP-region-manifest-for-SMMSTORE-back.patch")
out.write_bytes(hdr + body)
print("wrote", out, out.stat().st_size, "bytes")
PY
```

- [ ] **Step 3: Verify line-ending convention survived**

```bash
awk '{cr=(/\r$/)?1:0; l=$0; sub(/\r$/,"",l);
      if(l~/^(@@|diff --git|---|\+\+\+ |index |new file)/){m++; if(cr)mc++}
      else {b++; if(cr)bc++}}
     END{printf "metadata %d (%d CRLF)  body %d (%d CRLF)\n", m, mc, b, bc}' \
  meta-nuc-bios/recipes-bsp/edk2/files/0034-*.patch
```

Expected: metadata CRLF count **0**, body CRLF count equal to body count.
Anything else means the header write corrupted the body — redo Step 2.

- [ ] **Step 4: Add to SRC_URI**

In `meta-nuc-bios/recipes-bsp/edk2/edk2-uefipayload_2605.bb`, after the `0033-`
line inside the `SRC_URI +=` list:

```
    '0034-UefiPayloadPkg-RMAP-region-manifest-for-SMMSTORE-back.patch', \
```

and change the range comment near line 57 from `0019-0033` to `0019-0034`.

- [ ] **Step 5: Verify it applies in a real build**

```bash
kas shell -c 'bitbake -c patch edk2-uefipayload'
ls build/tmp/work/corei7-64-poky-linux/edk2-uefipayload/2605+git/git/UefiPayloadPkg/Library/FmpDeviceSmmLib/
```

Expected: `do_patch` succeeds and the directory now contains
`FmpDeviceSmmManifest.c`, `FmpDeviceSmmUpdatePolicy.c`, `FmpDeviceSmmFlashRetry.c`,
`FmpDeviceSmmLibUnitTest.c` alongside the original `FmpDeviceSmmLib.c`.

`git apply --check` is NOT a sufficient gate here — it tolerates line-ending
mismatches that quilt rejects. The bitbake run is the gate.

- [ ] **Step 6: Establish whether the PR's host tests can run here**

The patch brings `UefiPayloadPkg/Test/UefiPayloadPkgHostTest.dsc` with 15 unit
tests over the dangerous logic. Running them needs EDK2's pytool/stuart
harness, which this Yocto environment may not have.

```bash
python3 -c "import edk2toollib" 2>/dev/null && echo "pytool AVAILABLE" || echo "pytool ABSENT"
```

If AVAILABLE, build and run the host test DSC and record the result. If ABSENT,
do **not** silently skip: record in the report that these tests exist, cover
manifest parsing / range overlap / variable-service policy / flash retry, and
are currently unrun — so the gap is visible rather than assumed covered. Do not
install a toolchain to make it work; that is a separate decision.

- [ ] **Step 7: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/edk2/files/0034-*.patch \
        meta-nuc-bios/recipes-bsp/edk2/edk2-uefipayload_2605.bb
git commit -m "feat(edk2): carry tianocore PR 12861, RMAP region manifest"
```

---

### Task 2: coreboot capsule Kconfigs

**Files:**
- Modify: `meta-nuc-bios/recipes-bsp/coreboot/files/payload-edk2.config`

**Interfaces:**
- Consumes: nothing.
- Produces: `CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y` (unlocks full-flash SMI access), and the ESRT identity the payload's `CAPSULE_MAIN_FW_GUID` must match.

Kconfig names verified against `src/drivers/efi/Kconfig` at our pinned coreboot
SRCREV `149d149`. `DRIVERS_EFI_VARIABLE_STORE` is already selected by
`PAYLOAD_EDK2 && SMMSTORE`, and `CONFIG_SMMSTORE=y` is already set at line 57.

- [ ] **Step 1: Add the Kconfigs**

Append to `payload-edk2.config`:

```
# --- UEFI capsule updates -------------------------------------------
# DRIVERS_EFI_UPDATE_CAPSULES is required even though we never use
# coreboot's CapsuleUpdateData*/CBMEM capsule parser: it is what unlocks
# full-flash access in the SMMSTORE SMI handler. src/drivers/smmstore/store.c
# only returns boot_device_rw() -- the whole chip -- from
# lookup_store_region() when this is set, and wraps the entire
# SMMSTORE_CMD_USE_FULL_FLASH handshake in the same CONFIG() test. Without
# it FmpDeviceSmmLib is confined to the 512K SMMSTORE FMAP region and
# cannot reach the BIOS region at all.
CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y

# ESRT identity. The GUID must equal the payload's CAPSULE_MAIN_FW_GUID,
# the capsule's GUID and the ESRT entry -- four places, one value.
# coreboot's default is the placeholder 00112233-4455-6677-8899-aabbccddeeff.
CONFIG_DRIVERS_EFI_FW_INFO=y
CONFIG_DRIVERS_EFI_MAIN_FW_GUID="d25f89e1-94ec-4533-80b9-7f8855ce0a94"
CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x00000001
CONFIG_DRIVERS_EFI_MAIN_FW_LSV=0x00000001

# Capsule generation stays OFF: DRIVERS_EFI_GENERATE_CAPSULE assumes
# coreboot builds edk2 in payloads/external, and ours consumes a prebuilt
# UEFIPAYLOAD.fd via PAYLOAD_FILE. The capsule is generated in the edk2
# recipe instead. EMBED_FMP_DXE/ACCEPT_EMBEDDED_DRIVERS stay off with it --
# SECURE_BOOT_ENABLE=TRUE would require the embedded driver to be signed by
# a trusted key, and FmpDxe is FV-resident here instead.
# CONFIG_DRIVERS_EFI_GENERATE_CAPSULE is not set
```

- [ ] **Step 2: Verify the options survive into the generated .config**

```bash
kas shell -c 'bitbake -c configure coreboot'
grep -E "DRIVERS_EFI_(UPDATE_CAPSULES|FW_INFO|MAIN_FW_GUID)" \
  build/tmp/work/*/coreboot/*/build/.config
```

Expected: all three present with the values above. A Kconfig whose
dependencies are unmet is silently dropped, so absence here means
`DRIVERS_EFI_VARIABLE_STORE` or `SMMSTORE` did not select as assumed.

- [ ] **Step 3: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/coreboot/files/payload-edk2.config
git commit -m "feat(coreboot): enable UEFI capsule support and ESRT identity"
```

---

### Task 3: Turn on payload capsule support, make FmpDxe FV-resident

**Files:**
- Create: `meta-nuc-bios/recipes-bsp/edk2/files/0035-UefiPayloadPkg-make-FmpDxe-FV-resident.patch`
- Modify: `meta-nuc-bios/recipes-bsp/edk2/edk2-uefipayload_2605.bb`

**Interfaces:**
- Consumes: Task 1's patch (must apply first).
- Produces: `FmpDxe.efi` and `EsrtDxe.efi` inside the payload FV; the FMP instance the scanner's `UpdateCapsule()` will match.

Today `FmpDxe` is built only as a capsule-embedded driver — `UefiPayloadPkg.dsc`
builds it under `CAPSULE_SUPPORT` but `UefiPayloadPkg.fdf` lists only `EsrtDxe`.
With `SECURE_BOOT_ENABLE=TRUE` an embedded driver must be signed by a trusted
key or capsule processing fails, so we make it FV-resident instead.

- [ ] **Step 1: Write the FDF patch, bytes-mode**

The FDF is CRLF. Metadata lines stay LF, body lines get CRLF.

```bash
python3 - <<'PY'
from pathlib import Path
import subprocess, tempfile
T = Path("build/tmp/work/corei7-64-poky-linux/edk2-uefipayload/2605+git/git")
rel = "UefiPayloadPkg/UefiPayloadPkg.fdf"
src = (T/rel).read_bytes()
lines = src.split(b"\n")
anchor = b"INF MdeModulePkg/Universal/EsrtDxe/EsrtDxe.inf"
hits = [i for i,l in enumerate(lines) if anchor in l]
assert len(hits) == 1, f"anchor matched {len(hits)}"
add = b"INF FmpDevicePkg/FmpDxe/FmpDxe.inf\r"
new = lines[:hits[0]] + [add] + lines[hits[0]:]
with tempfile.TemporaryDirectory() as d:
    a=Path(d)/"a"; b=Path(d)/"b"
    a.write_bytes(src); b.write_bytes(b"\n".join(new))
    r=subprocess.run(["diff","-u","--label",f"aa/{rel}","--label",f"bb/{rel}",str(a),str(b)],capture_output=True)
hdr = b"""From: appkins <nbatkins@gmail.com>
Date: Tue, 1 Sep 2026 14:30:00 -0500
Subject: [PATCH] UefiPayloadPkg: make FmpDxe FV-resident

UefiPayloadPkg builds FmpDxe under CAPSULE_SUPPORT but lists only EsrtDxe
in the FDF, because upstream expects FmpDxe to travel inside the capsule
as an embedded driver. This platform builds with SECURE_BOOT_ENABLE=TRUE,
and coreboot's own Kconfig warns that an embedded driver must then be
signed by a key the running firmware trusts or capsule processing fails.

Putting FmpDxe in the firmware volume sidesteps embedded-driver signing
entirely and matches how the RPi5 platform in this fleet does it.

Upstream-Status: Inappropriate [platform policy]

---

"""
out = Path("meta-nuc-bios/recipes-bsp/edk2/files/0035-UefiPayloadPkg-make-FmpDxe-FV-resident.patch")
out.write_bytes(hdr + b"diff --git aa/%s bb/%s\n" % (rel.encode(), rel.encode()) + r.stdout)
print("wrote", out)
PY
```

- [ ] **Step 2: Add the patch and the build flags**

SRC_URI gains `'0035-UefiPayloadPkg-make-FmpDxe-FV-resident.patch', \` after 0034,
and the range comment becomes `0019-0035`.

In `EDK2_BUILD_FLAGS`, after the `-D VARIABLE_SUPPORT=SMMSTORE` line:

```
    -D CAPSULE_SUPPORT=TRUE \
    -D CAPSULE_MAIN_FW_GUID=d25f89e1-94ec-4533-80b9-7f8855ce0a94 \
```

- [ ] **Step 3: Build and verify both drivers land in the FV**

```bash
kas shell -c 'bitbake edk2-uefipayload'
B=build/tmp/work/corei7-64-poky-linux/edk2-uefipayload/2605+git/git/Build/UefiPayloadPkgX64/RELEASE_GCC/X64
ls "$B/FmpDxe.efi" "$B/EsrtDxe.efi"
```

Expected: both exist. `FmpDxe.efi` absent means `CAPSULE_SUPPORT` did not
reach the build; `EsrtDxe.efi` absent means the FDF hunk did not apply.

- [ ] **Step 4: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/edk2/files/0035-*.patch \
        meta-nuc-bios/recipes-bsp/edk2/edk2-uefipayload_2605.bb
git commit -m "feat(nuc): enable payload capsule support, FmpDxe FV-resident"
```

---

### Task 4: Pure capsule/drop-box logic, host-tested

**Files:**
- Create: `meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg/Library/NucCapsuleOnDiskLib/NucCapsuleParse.h`
- Create: `.../NucCapsuleParse.c`
- Create: `hack/nuccapsule/test/stubs/Uefi.h`, `hack/nuccapsule/test/test-parse.c`, `hack/nuccapsule/test/run.sh`

**Interfaces:**
- Consumes: nothing (deliberately dependency-free so it is host-testable).
- Produces:
  - `BOOLEAN NucCapsuleIsFmpCapsule (CONST EFI_GUID *Guid)`
  - `EFI_STATUS NucCapsuleValidateHeader (CONST UINT8 *Buf, UINTN Len, UINTN *ImageSize, UINT32 *Flags)`
  - `BOOLEAN NucCapsuleIsCandidateFile (CONST CHAR16 *Name)`

Isolating this is the point: it is the only part of the scanner testable
without a board, and it covers the three ways a scanner silently does the
wrong thing — accepting a capsule that wants a reset, accepting a truncated
header, and picking up files it should ignore.

- [ ] **Step 1: Write the stub header**

`hack/nuccapsule/test/stubs/Uefi.h`:

```c
#ifndef NUC_CAPSULE_TEST_UEFI_H_
#define NUC_CAPSULE_TEST_UEFI_H_
#include <stdint.h>
#include <stddef.h>
#include <uchar.h>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32;
typedef uint64_t UINT64; typedef size_t UINTN; typedef int BOOLEAN;
typedef char16_t CHAR16;   // NOT uint16_t: u"..." literals are char16_t*,
                           // and -Werror rejects the pointer mismatch.
#define CONST const
#define IN
#define OUT
#define VOID void
#define TRUE 1
#define FALSE 0
typedef UINTN EFI_STATUS;
#define EFI_SUCCESS            0
#define EFI_INVALID_PARAMETER  2
#define EFI_UNSUPPORTED        3
#define EFI_BAD_BUFFER_SIZE    4
typedef struct { UINT32 Data1; UINT16 Data2; UINT16 Data3; UINT8 Data4[8]; } EFI_GUID;
#define CAPSULE_FLAGS_PERSIST_ACROSS_RESET  0x00010000
#define CAPSULE_FLAGS_POPULATE_SYSTEM_TABLE 0x00020000
#define CAPSULE_FLAGS_INITIATE_RESET        0x00040000
#endif
```

- [ ] **Step 2: Write the failing test**

`hack/nuccapsule/test/test-parse.c`:

```c
#include <stdio.h>
#include <string.h>
#include "NucCapsuleParse.h"

static int Failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); Failures++; } } while (0)

// EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID
static const EFI_GUID FmpGuid =
  { 0x6dcbd5ed, 0xe82d, 0x4c44, { 0xbd, 0xa1, 0x71, 0x94, 0x19, 0x9a, 0xd9, 0x2a } };
static const EFI_GUID OtherGuid =
  { 0x11111111, 0x2222, 0x3333, { 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77 } };

// A minimal EFI_CAPSULE_HEADER laid out by hand: GUID, HeaderSize,
// Flags, CapsuleImageSize.
static void BuildHeader (UINT8 *Buf, const EFI_GUID *Guid, UINT32 Flags, UINT32 ImageSize)
{
  memset (Buf, 0, 32);
  memcpy (Buf, Guid, sizeof (EFI_GUID));
  UINT32 HeaderSize = 32;
  memcpy (Buf + 16, &HeaderSize, 4);
  memcpy (Buf + 20, &Flags, 4);
  memcpy (Buf + 24, &ImageSize, 4);
}

static void TestRejectsPersistAcrossReset (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0;
  BuildHeader (Buf, &FmpGuid, CAPSULE_FLAGS_PERSIST_ACROSS_RESET, 64);
  // This platform applies synchronously and has no PEI phase to coalesce
  // for. A capsule asking to persist must be refused, not silently applied.
  CHECK (NucCapsuleValidateHeader (Buf, sizeof (Buf), &ImageSize, &Flags) == EFI_UNSUPPORTED,
         "PERSIST_ACROSS_RESET must be rejected");
}

static void TestAcceptsFlaglessFmpCapsule (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0xFF;
  BuildHeader (Buf, &FmpGuid, 0, 64);
  CHECK (NucCapsuleValidateHeader (Buf, sizeof (Buf), &ImageSize, &Flags) == EFI_SUCCESS,
         "a flagless FMP capsule is accepted");
  CHECK (ImageSize == 64, "CapsuleImageSize is returned");
  CHECK (Flags == 0, "Flags are returned");
}

static void TestRejectsTruncatedHeader (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0;
  BuildHeader (Buf, &FmpGuid, 0, 64);
  CHECK (NucCapsuleValidateHeader (Buf, 20, &ImageSize, &Flags) == EFI_BAD_BUFFER_SIZE,
         "a buffer shorter than the header is refused");
}

static void TestRejectsImageSizeBeyondFile (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0;
  BuildHeader (Buf, &FmpGuid, 0, 4096);   // claims more than the file holds
  CHECK (NucCapsuleValidateHeader (Buf, sizeof (Buf), &ImageSize, &Flags) == EFI_BAD_BUFFER_SIZE,
         "CapsuleImageSize past the end of the file is refused");
}

static void TestRejectsHeaderSizePastImageSize (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0;
  BuildHeader (Buf, &FmpGuid, 0, 16);   // ImageSize < HeaderSize (32)
  CHECK (NucCapsuleValidateHeader (Buf, sizeof (Buf), &ImageSize, &Flags) == EFI_BAD_BUFFER_SIZE,
         "CapsuleImageSize smaller than HeaderSize is refused");
}

static void TestIdentifiesFmpGuid (void)
{
  CHECK (NucCapsuleIsFmpCapsule (&FmpGuid) == TRUE, "FMP GUID recognised");
  CHECK (NucCapsuleIsFmpCapsule (&OtherGuid) == FALSE, "unrelated GUID rejected");
}

static void TestFileFiltering (void)
{
  CHECK (NucCapsuleIsCandidateFile (u"fw.cap") == TRUE,  ".cap accepted");
  CHECK (NucCapsuleIsCandidateFile (u"FW.CAP") == TRUE,  "extension match is case-insensitive");
  CHECK (NucCapsuleIsCandidateFile (u"fw.txt") == FALSE, ".txt ignored");
  CHECK (NucCapsuleIsCandidateFile (u".")      == FALSE, "dot entry ignored");
  CHECK (NucCapsuleIsCandidateFile (u"..")     == FALSE, "dotdot entry ignored");
  CHECK (NucCapsuleIsCandidateFile (u"cap")    == FALSE, "a bare name is not an extension match");
}

int main (void)
{
  TestRejectsPersistAcrossReset ();
  TestAcceptsFlaglessFmpCapsule ();
  TestRejectsTruncatedHeader ();
  TestRejectsImageSizeBeyondFile ();
  TestRejectsHeaderSizePastImageSize ();
  TestIdentifiesFmpGuid ();
  TestFileFiltering ();
  if (Failures) { printf ("%d check(s) failed\n", Failures); return 1; }
  printf ("all NUC capsule parse checks passed\n");
  return 0;
}
```

`hack/nuccapsule/test/run.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${HERE}/../../../meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg/Library/NucCapsuleOnDiskLib"
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I"${HERE}/stubs" -I"${SRC}" \
    -o "${OUT}/test-parse" "${HERE}/test-parse.c" "${SRC}/NucCapsuleParse.c"
"${OUT}/test-parse"
```

- [ ] **Step 3: Run to verify it fails**

```bash
chmod +x hack/nuccapsule/test/run.sh && ./hack/nuccapsule/test/run.sh
```

Expected: FAIL — `NucCapsuleParse.h: No such file or directory`.

- [ ] **Step 4: Implement `NucCapsuleParse.h`**

```c
/** @file
  Pure capsule-header and drop-box filename logic, dependency-free so it can
  be unit tested on a build host.

  Copyright (c) 2026, the pi-bmc contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#pragma once

#include <Uefi.h>

/**
  Is this the EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID?
**/
BOOLEAN
NucCapsuleIsFmpCapsule (
  IN CONST EFI_GUID  *Guid
  );

/**
  Validate a capsule header read from a file.

  @retval EFI_SUCCESS            Flagless FMP capsule; ImageSize and Flags set.
  @retval EFI_UNSUPPORTED        Carries CAPSULE_FLAGS_PERSIST_ACROSS_RESET.
                                 This platform applies synchronously and has
                                 no PEI phase to coalesce for.
  @retval EFI_BAD_BUFFER_SIZE    Header truncated, or the declared sizes do
                                 not fit the buffer.
  @retval EFI_INVALID_PARAMETER  NULL argument.
**/
EFI_STATUS
NucCapsuleValidateHeader (
  IN  CONST UINT8  *Buf,
  IN  UINTN        Len,
  OUT UINTN        *ImageSize,
  OUT UINT32       *Flags
  );

/**
  Should this directory entry be considered a capsule? Matches a
  case-insensitive ".cap" extension and rejects "." and "..".
**/
BOOLEAN
NucCapsuleIsCandidateFile (
  IN CONST CHAR16  *Name
  );
```

- [ ] **Step 5: Implement `NucCapsuleParse.c`**

```c
/** @file
  Pure capsule-header and drop-box filename logic.

  Copyright (c) 2026, the pi-bmc contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "NucCapsuleParse.h"

//
// EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID, UEFI 2.10 23.3.
//
STATIC CONST EFI_GUID  mFmpCapsuleGuid = {
  0x6dcbd5ed, 0xe82d, 0x4c44, { 0xbd, 0xa1, 0x71, 0x94, 0x19, 0x9a, 0xd9, 0x2a }
};

//
// Field offsets within EFI_CAPSULE_HEADER. Spelled out rather than using the
// struct so this file needs no EDK2 headers and can be built on a host.
//
#define CAPSULE_OFF_GUID         0
#define CAPSULE_OFF_HEADER_SIZE  16
#define CAPSULE_OFF_FLAGS        20
#define CAPSULE_OFF_IMAGE_SIZE   24
#define CAPSULE_MIN_HEADER       28

STATIC
UINT32
ReadLe32 (
  IN CONST UINT8  *P
  )
{
  return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

BOOLEAN
NucCapsuleIsFmpCapsule (
  IN CONST EFI_GUID  *Guid
  )
{
  UINTN  Index;

  if (Guid == NULL) {
    return FALSE;
  }

  if ((Guid->Data1 != mFmpCapsuleGuid.Data1) ||
      (Guid->Data2 != mFmpCapsuleGuid.Data2) ||
      (Guid->Data3 != mFmpCapsuleGuid.Data3))
  {
    return FALSE;
  }

  for (Index = 0; Index < 8; Index++) {
    if (Guid->Data4[Index] != mFmpCapsuleGuid.Data4[Index]) {
      return FALSE;
    }
  }

  return TRUE;
}

EFI_STATUS
NucCapsuleValidateHeader (
  IN  CONST UINT8  *Buf,
  IN  UINTN        Len,
  OUT UINTN        *ImageSize,
  OUT UINT32       *Flags
  )
{
  UINT32    HeaderSize;
  UINT32    CapsuleImageSize;
  UINT32    CapsuleFlags;
  EFI_GUID  Guid;
  UINTN     Index;

  if ((Buf == NULL) || (ImageSize == NULL) || (Flags == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Len < CAPSULE_MIN_HEADER) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Guid.Data1 = ReadLe32 (Buf + CAPSULE_OFF_GUID);
  Guid.Data2 = (UINT16)(Buf[4] | (Buf[5] << 8));
  Guid.Data3 = (UINT16)(Buf[6] | (Buf[7] << 8));
  for (Index = 0; Index < 8; Index++) {
    Guid.Data4[Index] = Buf[8 + Index];
  }

  if (!NucCapsuleIsFmpCapsule (&Guid)) {
    return EFI_UNSUPPORTED;
  }

  HeaderSize       = ReadLe32 (Buf + CAPSULE_OFF_HEADER_SIZE);
  CapsuleFlags     = ReadLe32 (Buf + CAPSULE_OFF_FLAGS);
  CapsuleImageSize = ReadLe32 (Buf + CAPSULE_OFF_IMAGE_SIZE);

  //
  // Refuse a capsule that wants to survive a reset. This platform applies
  // synchronously under boot services; there is no PEI phase to coalesce a
  // persisted capsule, and coreboot's CapsuleUpdateData* path is deliberately
  // not enabled. Applying it anyway would appear to work and then do nothing.
  //
  if ((CapsuleFlags & CAPSULE_FLAGS_PERSIST_ACROSS_RESET) != 0) {
    return EFI_UNSUPPORTED;
  }

  if ((HeaderSize < CAPSULE_MIN_HEADER) || (HeaderSize > Len)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if ((CapsuleImageSize < HeaderSize) || (CapsuleImageSize > Len)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *ImageSize = (UINTN)CapsuleImageSize;
  *Flags     = CapsuleFlags;

  return EFI_SUCCESS;
}

BOOLEAN
NucCapsuleIsCandidateFile (
  IN CONST CHAR16  *Name
  )
{
  UINTN   Len;
  CHAR16  C;
  UINTN   Index;
  CONST CHAR16  *Ext = u".cap";

  if (Name == NULL) {
    return FALSE;
  }

  for (Len = 0; Name[Len] != 0; Len++) {
  }

  //
  // "." and ".." are directory entries, never capsules.
  //
  if ((Len == 1) && (Name[0] == u'.')) {
    return FALSE;
  }

  if ((Len == 2) && (Name[0] == u'.') && (Name[1] == u'.')) {
    return FALSE;
  }

  //
  // Require a real ".cap" suffix, not merely the letters: a file called
  // "cap" is not a capsule.
  //
  if (Len < 4) {
    return FALSE;
  }

  for (Index = 0; Index < 4; Index++) {
    C = Name[Len - 4 + Index];
    if ((C >= u'A') && (C <= u'Z')) {
      C = (CHAR16)(C + (u'a' - u'A'));
    }

    if (C != Ext[Index]) {
      return FALSE;
    }
  }

  return TRUE;
}
```

- [ ] **Step 6: Run the tests**

```bash
./hack/nuccapsule/test/run.sh
```

Expected: `all NUC capsule parse checks passed`, exit 0, ASan/UBSan clean.

- [ ] **Step 7: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg/Library/NucCapsuleOnDiskLib hack/nuccapsule
git commit -m "feat(nuccapsule): capsule header and drop-box logic with host tests"
```

---

### Task 5: The scanner driver

**Files:**
- Create: `.../NucCapsuleOnDiskLib/NucCapsuleOnDiskLib.c`
- Create: `.../NucCapsuleOnDiskLib/NucCapsuleOnDiskLib.inf`
- Modify: `meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg/NucRedfish.dsc`
- Modify: `meta-nuc-bios/recipes-bsp/edk2/files/0035-UefiPayloadPkg-make-FmpDxe-FV-resident.patch` (NULL-link into BdsDxe)

**Interfaces:**
- Consumes: Task 4's `NucCapsuleIsFmpCapsule`, `NucCapsuleValidateHeader`, `NucCapsuleIsCandidateFile`; Task 3's FV-resident `FmpDxe`.
- Produces: the ReadyToBoot behaviour; nothing later consumes it in code.

**Port from** `/home/appkins/src/pi-bmc/rpi5-uefi-build/meta-rpi5-uefi/recipes-bsp/edk2-platforms/files/edk2-platforms/Silicon/RaspberryPi/Library/RpiCapsuleOnDiskLib/RpiCapsuleOnDiskLib.c` (1461 lines). Read its file header comment first — it explains every design choice, and all of them still apply except where listed below.

**Keep:** `LIBRARY_CLASS = NULL|DXE_DRIVER` with a constructor; the ReadyToBoot event at TPL_CALLBACK; connecting USB mass-storage before scanning so the BMC LUN is reachable on a boot that never selected it; the deliberate refusal to `ConnectAll`; `CAPSULE_DIR_NAME L"\\EFI\\UpdateCapsule"`; `gRT->UpdateCapsule (HeaderArray, 1, 0)`; delete-on-success as the applied-vs-pending signal; leaving a failed capsule in place; OR-ing the `OsIndicationsSupported` capsule-on-disk bit back in; cold reset after success.

**Drop:** `InstallSidecarConfig` and everything config.txt; `VerifyFirmwareOnDisk` and `mMediaFirmwareCurrent` (no firmware file exists here); the `ArmPkg` and `Platform/RaspberryPi/RaspberryPi.dec` packages; `PcdNvStorageVariableBase`; `PcdFdBaseAddress`; the legacy-bootstrap commentary.

**Replace:** rpi5 proves the write by re-reading the firmware file. Here, after `UpdateCapsule()` returns, locate the FMP whose `ImageTypeId` is `d25f89e1-94ec-4533-80b9-7f8855ce0a94` via `gEfiFirmwareManagementProtocolGuid`, call `GetImageInfo`, and read `LastAttemptStatus` from the returned `EFI_FIRMWARE_IMAGE_DESCRIPTOR`. Delete the file only when it equals `LAST_ATTEMPT_STATUS_SUCCESS`. This is the concrete form of the rule that `UpdateCapsule()` success means "processed", never "applied".

- [ ] **Step 1: Write the INF**

```
## @file
#  Boot-time \EFI\UpdateCapsule scanner for the NUC payload: applies staged
#  FMP capsules at ReadyToBoot, synchronously, from any attached volume --
#  including the USB mass-storage LUN the BMC's gadget exposes. Linked NULL
#  into BdsDxe. The platform's substitute for upstream Capsule-on-Disk,
#  which needs a PEI phase a payload does not have.
#
#  Copyright (c) 2026, the pi-bmc contributors.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  INF_VERSION    = 0x0001001A
  BASE_NAME      = NucCapsuleOnDiskLib
  FILE_GUID      = 9f2c4e77-3ab1-4d58-9c60-1e83d5a24b0f
  MODULE_TYPE    = DXE_DRIVER
  VERSION_STRING = 1.0
  LIBRARY_CLASS  = NULL|DXE_DRIVER
  CONSTRUCTOR    = NucCapsuleOnDiskLibConstructor

[Sources]
  NucCapsuleOnDiskLib.c
  NucCapsuleParse.c
  NucCapsuleParse.h

[Packages]
  MdePkg/MdePkg.dec
  MdeModulePkg/MdeModulePkg.dec
  NucRedfishPkg/NucRedfishPkg.dec

[LibraryClasses]
  BaseLib
  BaseMemoryLib
  DebugLib
  FileHandleLib
  MemoryAllocationLib
  PcdLib
  UefiBootServicesTableLib
  UefiLib
  UefiRuntimeServicesTableLib

[Protocols]
  gEfiFirmwareManagementProtocolGuid  ## CONSUMES
  gEfiSimpleFileSystemProtocolGuid    ## CONSUMES
  gEfiUsbIoProtocolGuid               ## CONSUMES

[Guids]
  gEfiEventReadyToBootGuid  ## CONSUMES ## Event
  gEfiFileInfoGuid          ## CONSUMES
  gEfiFmpCapsuleGuid        ## CONSUMES
  gEfiGlobalVariableGuid    ## SOMETIMES_PRODUCES ## Variable:L"OsIndicationsSupported"
```

- [ ] **Step 2: Port the scanner**

Copy the rpi5 source, apply the Keep/Drop/Replace list above, and rename every
`RpiCapsuleOnDisk` symbol and message prefix to `NucCapsuleOnDisk`. Preserve the
file header comment's explanations, updating only the platform-specific
sentences (the DWC2/NCM connect-stall paragraph becomes a note that
`ConnectAll` is avoided to keep normal boots fast).

- [ ] **Step 3: Add to the DSC and NULL-link into BdsDxe**

In `NucRedfish.dsc` `[Components]`, after the `UsbCdcAcmDxe` entry:

```
  NucRedfishPkg/Library/NucCapsuleOnDiskLib/NucCapsuleOnDiskLib.inf
```

Extend patch 0035 so `BdsDxe` in `UefiPayloadPkg.dsc` gains the NULL library:

```
  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf {
    <LibraryClasses>
      NULL|NucRedfishPkg/Library/NucCapsuleOnDiskLib/NucCapsuleOnDiskLib.inf
  }
```

Regenerate the patch with the same bytes-mode `diff`-against-the-tree method
Task 3 Step 1 uses. Never hand-edit the `.patch`.

- [ ] **Step 4: Verify the host tests still pass and the payload builds**

```bash
./hack/nuccapsule/test/run.sh
kas shell -c 'bitbake edk2-uefipayload'
```

Expected: tests pass; build green. This is the first compile of the scanner —
expect real compile errors and fix them in the source, then rebuild.

- [ ] **Step 5: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg meta-nuc-bios/recipes-bsp/edk2/files/0035-*.patch
git commit -m "feat(nuccapsule): ReadyToBoot scanner applying staged FMP capsules"
```

---

### Task 6: Capsule generation with the RMAP manifest

**Files:**
- Modify: `meta-nuc-bios/recipes-bsp/edk2/edk2-uefipayload_2605.bb` (or `nuc-coreboot-rom.bb` if the ROM is only final there — check which task has the finished ROM)

**Interfaces:**
- Consumes: Task 1's `AppendRmapManifest.py`; Task 2's GUID/version.
- Produces: a signed `.cap` the BMC can stage.

The manifest is what keeps this non-destructive. **An absent manifest silently
falls back to full-flash**, which overwrites SMMSTORE and resets the variable
store — so the build must fail loudly rather than emit an unmanifested capsule.

- [ ] **Step 1: Append the manifest and generate the capsule**

Add a `do_deploy`-time step that, given the finished ROM:

```bash
python3 "${EDK2_PATH}/UefiPayloadPkg/Tools/AppendRmapManifest.py" \
    --regions COREBOOT \
    -o "${B}/coreboot-rmap.rom" "${B}/coreboot.rom"

python3 "${EDK2_PATH}/BaseTools/Source/Python/Capsule/GenerateCapsule.py" \
    --encode --guid d25f89e1-94ec-4533-80b9-7f8855ce0a94 \
    --fw-version 1 --lsv 1 \
    --signer-private-cert "${NUC_CAPSULE_SIGNER_CERT}" \
    --other-public-cert   "${NUC_CAPSULE_OTHER_CERT}" \
    --trusted-public-cert "${NUC_CAPSULE_TRUSTED_CERT}" \
    -o "${DEPLOYDIR}/nuc-firmware.cap" "${B}/coreboot-rmap.rom"
```

Confirm `AppendRmapManifest.py`'s actual argument names from the patched tree
before writing this — read
`build/tmp/work/.../git/UefiPayloadPkg/Tools/AppendRmapManifest.py` and match
its `argparse` definitions exactly.

- [ ] **Step 2: Guard the manifest**

```bash
python3 - <<'PY'
import struct, sys
p = "${B}/coreboot-rmap.rom"
d = open(p,'rb').read()
sig, ver, n = struct.unpack('<IHH', d[-8:])
assert sig == 0x50414D52, "RMAP signature missing -- capsule would silently full-flash"
assert n >= 1, "RMAP manifest is empty"
print(f"RMAP ok: version {ver}, {n} region(s)")
PY
```

- [ ] **Step 3: Reject the EDK2 test certificate**

The DSC's default chain is `BaseTools/Source/Python/Pkcs7Sign/TestRoot.cer` —
EDK2's *public test certificate*. Shipping it means any capsule validates. Fail
the build when the configured signer resolves to a path under `Pkcs7Sign/Test`,
mirroring how `rpi5_fmp_resolve_keys` refuses to proceed without a real key.

- [ ] **Step 4: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/edk2/edk2-uefipayload_2605.bb
git commit -m "feat(nuc): generate a signed capsule with an RMAP COREBOOT manifest"
```

---

### Task 7: Contract duty 4 — per-boot SoftwareInventory PATCH

**Files:**
- Modify: `meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg/NucRedfishSyncDxe/NucRedfishInventory.c`

**Interfaces:**
- Consumes: the FMP descriptor's version and `LastAttemptStatus` (available because the RMAP manifest preserved variable services).

The BMC serves whatever member id the host PATCHes
(`api/redfish/update_service.go`), so the id is ours to choose; use
`BiosFirmware` for fleet consistency with the RPi5.

- [ ] **Step 1: PATCH the firmware inventory each boot**

Following the existing `AppendJsonString` idiom in this file, PATCH
`/redfish/v1/UpdateService/FirmwareInventory/BiosFirmware` with `Version` from
the FMP descriptor, plus `LastAttemptVersion` and `LastAttemptStatus`. PATCH
merges per DSP0266, so a boot that omits `LastAttempt*` leaves the previous
attempt visible rather than clearing it — omit them when no attempt was made
this boot.

- [ ] **Step 2: Verify against the BMC**

```bash
ssh root@10.1.168.17 \
  'grep "UpdateService/FirmwareInventory" /var/log/NanoKVM-Server.log | tail -3'
```

Expected: a `PATCH .../FirmwareInventory/BiosFirmware -> 200` from client
`169.254.10.2` on each host boot.

- [ ] **Step 3: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg/NucRedfishSyncDxe/
git commit -m "feat(nuc): report firmware inventory to the BMC each boot"
```

---

### Task 8: Hardware validation

**Files:** none — this is the acceptance gate.

**This task cannot be run by an agent.** It needs physical flashing and a
recoverable board.

- [ ] **Step 1: Flash the first capsule-capable build externally**

The machinery must be in the *running* firmware before any capsule can apply.
Accepted by the operator. Use the image flasher; keep a SOIC-8 clip attached.

- [ ] **Step 2: Confirm the ESRT entry**

Boot Linux and read `/sys/firmware/efi/esrt/entries/*/fw_class` — expect
`d25f89e1-94ec-4533-80b9-7f8855ce0a94`. A missing entry means the GUID does not
match across the four places, or `EsrtDxe` is not in the FV.

- [ ] **Step 3: Stage a capsule from the BMC and reboot**

Expect: the scanner finds it, applies it in that boot, deletes the file, and
cold resets. Then verify **the variable store survived** — boot entries and
Secure Boot state intact. If they were lost, the RMAP manifest did not take
effect and the update full-flashed.

- [ ] **Step 4: Confirm the BMC's view**

The capsule is gone from `\EFI\UpdateCapsule\` and `FirmwareInventory` shows the
new version with `LastAttemptStatus` success.

---

## Notes for the executor

- **Never hand-edit a `.patch`.** Bytes-mode Python only; regenerate hunks by
  diffing against the real patched tree rather than computing hunk headers.
- **`git apply --check` is not a sufficient gate.** It tolerates line-ending
  mismatches quilt rejects. Only a bitbake `do_patch` proves a patch applies.
- **A green build is not a working update.** Only Task 8 can tell you the flash
  path works, and the first attempt can brick a board.
