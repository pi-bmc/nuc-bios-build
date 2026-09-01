# NUC EDK2 source-recipe split — design

**Date:** 2026-09-01
**Status:** approved, not yet implemented
**Branch:** `refactor/edk2-source-recipes` (based on `feat/capsule-updates`)

## Goal

Split `edk2-uefipayload_2605.bb` into source-tree recipes that own the trees and
a build recipe that owns the build, so that a change to one tree's patch series
rebuilds the payload without re-unpacking the other trees — the arrangement
`rpi5-uefi-build` already uses. Move the layer's build-input handoffs from
`DEPLOY_DIR_IMAGE` to the sysroot, which is what makes the dependency implicit
rather than hand-declared.

## The pattern being adopted

`rpi5-uefi-build/meta-rpi5-uefi/recipes-bsp` separates *owning a tree* from
*owning the build*.

**Four source-only recipes** — `edk2`, `edk2-platforms`, `edk2-non-osi`,
`edk2-redfish-client`. Each fetches exactly one tree, applies only that tree's
patches, and compiles nothing:

```bitbake
inherit allarch nopackages
do_configure[noexec] = "1"
do_compile[noexec]   = "1"
EDK2_SOURCE_ROOT = "${datadir}/edk2"
do_install() { cp -a ${S}/. ${D}${EDK2_SOURCE_ROOT}/<tree>/ ; ... }
```

**One build recipe** — `rpi5-uefi-firmware`, whose `SRC_URI` holds no git tree
at all, only `file://` config snippets. It declares
`DEPENDS = "edk2 edk2-platforms edk2-non-osi edk2-redfish-client"` and reads
`EDK2_SOURCE_ROOT = "${STAGING_DATADIR}/edk2"`. In the sysroot that one
directory *is* a complete multi-tree EDK2 workspace, laid out exactly as
upstream's `build.sh` assembles one by hand. Trees the build mutates are
reflink-copied into `${WORKDIR}`; trees it only reads stay in the sysroot and go
on `PACKAGES_PATH` as absolute paths.

### How the chain reaches the image

`DEPENDS` makes the consumer's `do_prepare_recipe_sysroot` depend on each
child's `do_populate_sysroot`, so every child taskhash feeds the consumer's.
Editing `edk2-platforms` patch `0034`:

1. That file is in `do_fetch[file-checksums]` (`base.bbclass:142`), so
   `edk2-platforms:do_fetch` rehashes.
2. `do_unpack` → `do_patch` → `do_install` → `do_populate_sysroot` rerun.
3. `rpi5-uefi-firmware`'s `do_prepare_recipe_sysroot` rehashes, and
   `do_configure` → `do_compile` → `do_deploy` rerun.
4. `rpi5-uefi-sdimg`'s `do_stage_bootfiles` reruns via its explicit
   `[depends] += "rpi5-uefi-firmware:do_deploy"`.

The half that matters just as much: `edk2`'s own tasks do **not** rerun. Its
sstate stays valid and its 1.6 GB tree is not re-unpacked.

### The layer's dividing rule

Stated outright in `edk2-non-osi_git.bb`'s header, which exists because
`TFA_BUILD_ARTIFACTS` used to point at `DEPLOY_DIR_IMAGE` and needed a
hand-written `do_compile[depends]` to order it:

> **Source and tool inputs travel through the sysroot via `DEPENDS`. Finished
> deployables travel through `DEPLOY_DIR_IMAGE` with an explicit
> `do_X[depends]`.**

`rpi5-uefi-sdimg` and `rpi5-capsule-image` consume finished artifacts and so
still read `DEPLOY_DIR_IMAGE`. That is the rule working, not an exception to it.

## Current state in nuc-bios-build

`edk2-uefipayload_2605.bb` is 693 lines owning three git trees, 36 patches,
`NucRedfishPkg`, the capsule signing identity, two GUID-drift guards, the
FmpDxe certificate swap, and the build.

**Measured cost.** `do_fetch[file-checksums]` covers all 36 patch files, so
editing *one* — say `0100`, which touches only `edk2-redfish-client` —
rehashes `do_fetch` and re-unpacks all three trees, the 2.4 GB gitsm `edk2`
included, before anything recompiles. There is no child recipe for the change
to be localized to.

**Tree composition** (measured in `build/tmp/work/.../edk2-uefipayload/2605+git/git`):

| Component | Size |
|---|---|
| Total workdir tree | 2.4 GB |
| `Build/` (build output) | 819 MB |
| Nested submodule checkouts (depth ≥ 2) | ~1.08 GB |
| Source that the build actually references | ~520 MB |

The nested checkouts are submodules-of-submodules that gitsm fetches
recursively: `SecurityPkg/.../libspdm/os_stub/openssllib/openssl` (580 MB),
openssl's `cloudflare-quiche` (162 MB), `pyca-cryptography` (82 MB),
`fuzz/corpora` (82 MB), and eight more. No `.inf`, `.dsc`, `.dec`, `.fdf` or
`.inc` in edk2 references a file inside any of them.

**Other divergences from the pattern:**

- `PACKAGE_ARCH` is `corei7-64` on a recipe that compiles only host binaries.
- Three build inputs travel through `DEPLOY_DIR_IMAGE` with hand-written
  `[depends]` plus `bbfatal` existence guards: `ipxe-intel.efidrv`
  (ipxe-efi → payload), `UEFIPAYLOAD.fd` and `edk2-capsule-tools/`
  (payload → coreboot).
- `nuc-coreboot-rom.bb` is an orphan. The flasher multiconfig it served
  (`conf/multiconfig/flasher.conf`, `nuc-flasher-image.bb`, `kas-flasher.yml`)
  was removed on this branch in `d2025de`, replaced by
  `scripts/make-flasher-img.sh`; the recipe's `do_install[mcdepends]` line was
  correctly removed with it. Nothing on this branch builds the recipe. This is
  dead code, not a missing dependency.

## Design

### Recipe inventory

| Path | Role | PACKAGE_ARCH |
|---|---|---|
| `recipes-bsp/edk2/edk2_git.bb` | gitsm tianocore/edk2 + patches 0001–0035, nested-submodule prune | allarch |
| `recipes-bsp/edk2-platforms/edk2-platforms_git.bb` | the tree, unpatched | allarch |
| `recipes-bsp/edk2-redfish-client/edk2-redfish-client_git.bb` | the tree + patch 0100 | allarch |
| `recipes-bsp/edk2-uefipayload/edk2-uefipayload_2605.bb` | the build | MACHINE_ARCH |

### File moves

Patches `0001`–`0035` **stay where they are** in `recipes-bsp/edk2/files/`,
because `edk2_git.bb` lands in that same directory and inherits the same
`FILESPATH`. Nothing rewrites a patch file.

Two `git mv` operations:

- `recipes-bsp/edk2/files/0100-RedfishClientPkg-*.patch`
  → `recipes-bsp/edk2-redfish-client/files/`
- `recipes-bsp/edk2/edk2-uefipayload_2605.bb`,
  `recipes-bsp/edk2/files/NucRedfishPkg/`,
  `recipes-bsp/edk2/files/bootsplash.bmp`
  → `recipes-bsp/edk2-uefipayload/`

`git mv` preserves bytes, which is required: these patches carry mixed line
endings (LF metadata, CRLF bodies) that quilt rejects if disturbed.

### The sysroot contract

Each source recipe installs into `${D}${datadir}/edk2/<tree>`; the payload
recipe reads `${STAGING_DATADIR}/edk2`. Both sides spell
`EDK2_SOURCE_ROOT = "${datadir}/edk2"` so the pairing is visible in each file.

```text
${STAGING_DATADIR}/edk2/
    edk2/                   # core tree, patched, submodule-pruned
    edk2-platforms/         # Features/Intel/** for PACKAGES_PATH
    edk2-redfish-client/    # RedfishClientPkg
```

### Per-recipe specifications

**`edk2_git.bb`** — `SRC_URI` is the gitsm fetch with `destsuffix=edk2` plus
patches 0001–0035 in their current order; `SRCREV = fa41c179db1f9fc21eb425f44b85a16262c806ca`;
`LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"`.
`do_install` copies the tree, then drops nested submodule checkouts by reading
the `.gitmodules` files rather than hardcoding a list, then removes `.pc/`,
`patches` and every `.git`. Verbatim port of rpi5's `edk2_git.bb` `do_install`,
which is the load-bearing part: without the prune this recipe stages 1.6 GB
into every consumer's recipe-sysroot and into sstate.

**`edk2-platforms_git.bb`** — plain fetch, no patches,
`SRCREV = 75efd079fed9723db8ce02365233c03b2fdc3b92`,
`LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"`.
`do_install` copies and drops `.git`.

**`edk2-redfish-client_git.bb`** — fetch plus `0100-RedfishClientPkg-fit-the-client-to-a-Redfish-host-in.patch`,
which loses its `;patchdir=${EDK2_REDFISH_CLIENT_PATH}` qualifier because `${S}`
is now that tree. `SRCREV = 92fabf8572c226cf180c62b1204380385a518db3`;
`LIC_FILES_CHKSUM = "file://LICENSE;md5=2b415520383f7964e96700ae12b4570a"`.

All three: `inherit allarch nopackages`, `UNPACKDIR ?= "${WORKDIR}"` (scarthgap
shim), `S = "${UNPACKDIR}/<tree>"`, both build tasks `[noexec]`, and
`PV` carrying each tree's own upstream date (`202608+git${SRCPV}` for edk2 and
edk2-redfish-client, `202607+git${SRCPV}` for edk2-platforms) rather than the
payload's `2605`, which was never any of their versions. `allarch` sets
`INHIBIT_DEFAULT_DEPS` itself, so the current explicit line is dropped from
these three. None of them carries `COMPATIBLE_MACHINE`: they are machine-
independent source, and the constraint belongs on the build recipe that
consumes them.

**`edk2-uefipayload_2605.bb`** — every line of the capsule identity, GUID-drift
guard, certificate swap, bootsplash, GOP and build-flag logic is preserved
unchanged in substance. What changes:

- `SRC_URI` loses all three git entries and keeps `file://NucRedfishPkg`,
  `file://bootsplash.bmp`.
- `LIC_FILES_CHKSUM` becomes
  `file://${COMMON_LICENSE_DIR}/BSD-2-Clause-Patent;md5=0518d409dae93098cca8dfa932f3ab1b`
  — the recipe unpacks no tree to check against.
- `DEPENDS += "edk2 edk2-platforms edk2-redfish-client"`.
- `S = "${UNPACKDIR}"`, `B = "${WORKDIR}/build"`.
- New `EDK2_PATH = "${WORKDIR}/edk2"`, a reflink copy made in `do_configure`
  from `${EDK2_SOURCE_ROOT}/edk2`. The copy is required: the recipe stages
  `NucRedfishPkg` into the tree, rewrites `UefiPayloadPkg.dsc`, writes
  `MdeModulePkg/Logo/Logo.bmp`, and builds BaseTools into it.
- `EDK2_PLATFORMS_PATH` and `EDK2_REDFISH_CLIENT_PATH` point at the sysroot and
  are read in place — nothing writes to either.
- Roughly a dozen `${S}/...` references become `${EDK2_PATH}/...`: the
  NucRedfishPkg stage, both GUID-guard arguments, `BinToPcd.py`, the DSC edit,
  `Logo.bmp`, `NetworkDrivers/`, the `Conf/` templates, `Build/` output paths in
  `do_compile` and `do_deploy`, and the `BaseTools/Source/Python` +
  `AppendRmapManifest.py` capsule-tool staging.
- `WORKSPACE` becomes `${EDK2_PATH}`. Unlike rpi5, this is the tree containing
  the DSC (`UefiPayloadPkg/UefiPayloadPkg.dsc` is inside edk2), so no
  parent-directory arrangement is needed.
- `PACKAGE_ARCH = "${MACHINE_ARCH}"`: the recipe stages a built FV for one
  board, matching `edk2-non-osi`'s reasoning on the rpi5 side.

### Handoffs moving to the sysroot

Per the dividing rule, all three are build inputs, not finished deployables.

| Producer → consumer | Was | Becomes |
|---|---|---|
| ipxe-efi → payload | `${DEPLOY_DIR_IMAGE}/ipxe-intel.efidrv` + `do_configure[depends]` | `${STAGING_DATADIR}/ipxe/ipxe-intel.efidrv` + `DEPENDS` |
| payload → coreboot | `${DEPLOY_DIR_IMAGE}/UEFIPAYLOAD.fd` + `do_configure[depends]` | `${STAGING_DATADIR}/edk2-uefipayload/UEFIPAYLOAD.fd` + `DEPENDS` |
| payload → coreboot | `${DEPLOY_DIR_IMAGE}/edk2-capsule-tools/` + `do_deploy[depends]` | `${STAGING_DATADIR}/edk2-uefipayload/capsule-tools/` + same `DEPENDS` |

`ipxe-efi` gains a `do_install` staging `ipxe-intel.efidrv` under
`${datadir}/ipxe` and drops `do_install[noexec]`; it keeps its `do_deploy`,
because `ipxe.rom` is a finished artifact.

The payload gains a `do_install` staging `UEFIPAYLOAD.fd` and the capsule
tooling under `${datadir}/edk2-uefipayload`; it keeps `do_deploy` for
`UEFIPAYLOAD.fd` and the build report, which are what a human collects.

`coreboot_git.bb` drops the whole `python () { ... appendVarFlag ... }` block for
the edk2 branch, drops both `bbfatal` existence guards (a missing sysroot file
is a build-system bug, not an operator error), and keeps the linuxboot branch's
`[depends]` unchanged — `bzImage` and the u-root initramfs are finished
deployables. The comment block at `coreboot_git.bb:364-375` naming
`DEPLOY_DIR_IMAGE` as "this layer's one cross-recipe sharing convention" is
rewritten to state the rule above.

### Orphan cleanup

Delete `recipes-bsp/nuc-coreboot-rom/`. Its consumer was removed in `d2025de`
and nothing references it. Update `README.md:126`, which still describes the
removed ISO flow, and the two stale prose references to it in
`edk2-uefipayload_2605.bb:679` and `coreboot_git.bb:371`.

## Resulting dependency chain

| Edit | Reruns | Does not rerun |
|---|---|---|
| edk2 patch 0001–0035 | edk2 fetch→sysroot, payload, coreboot | edk2-platforms, edk2-redfish-client unpacks |
| patch 0100 | edk2-redfish-client fetch→sysroot, payload, coreboot | the 2.4 GB edk2 unpack |
| `NucRedfishPkg/**` | payload configure→deploy, coreboot | all three tree unpacks |
| build flag / PCD in payload | payload configure→deploy, coreboot | all three tree unpacks |
| `payload-edk2.config`, mainboard | coreboot only | everything edk2 |
| any `SRCREV` | that tree only, then payload, coreboot | the other two trees |

## Traps

1. **Patch bytes.** The patches carry LF metadata with CRLF bodies. `git mv`
   only — never rewrite one to change a path.
2. **`destsuffix` change.** `git` → `edk2` in the fetch; `S` must follow, and
   the old `S = "${WORKDIR}/git"` must not survive anywhere.
3. **Submodule prune is not optional.** Skipping it stages 1.6 GB per consumer
   sysroot and into sstate.
4. **`${S}` rewrite completeness.** A missed `${S}` in the payload silently
   resolves to `${WORKDIR}` and produces a confusing "file not found" far from
   the cause. Every occurrence must be audited, not grepped-and-hoped.
5. **DSC edit idempotence.** The FmpDxe certificate swap already guards against
   re-running on an already-edited tree. Because `do_configure` now re-copies
   `edk2` fresh each time, it always sees a pristine DSC — the existing
   unconditional structural check stays as the backstop.
6. **sstate invalidation.** `PACKAGE_ARCH` and `PN` changes invalidate
   everything downstream. The first build after this lands is a full rebuild.

## Out of scope

- Any change to the capsule feature's behaviour, including the open version-gate
  Critical (`NUC_CAPSULE_VERSION` calibrated to `1 == 1`). That is a separate
  fix on `feat/capsule-updates`.
- Splitting `NucRedfishPkg` into its own recipe. It has no fetch and no patch
  series, so a separate recipe buys neither of the two things this split is for.
  It stays in the build recipe's `files/`, mirroring how `rpi5-uefi-firmware`
  keeps its DSC/FDF snippets.
- The coreboot tree itself. `coreboot-source.inc` +
  `coreboot-toolchain-native` already implement this same split for coreboot's
  crossgcc bootstrap.
- Restoring the flasher multiconfig.

## Verification

1. `bitbake -e edk2-uefipayload | grep ^EDK2_PATH=` and the sysroot paths
   resolve as designed.
2. `kas build` from clean sstate produces a `coreboot-nuc5i7ryh.rom` and
   `nuc-firmware.cap` byte-comparable to the pre-refactor build for the same
   `SRCREV`s and flags. Byte-identical is not required (build paths change);
   the FMP GUID, the RMAP manifest region list, and the ESRT entry must match.
3. Touch `files/0100-*.patch`; confirm `edk2:do_unpack` is a sstate hit while
   the payload rebuilds.
4. Touch a `NucRedfishPkg` source; confirm all three tree recipes are sstate
   hits while the payload rebuilds.
5. Confirm both GUID drift guards still fire by temporarily perturbing
   `NUC_CAPSULE_GUID`.
