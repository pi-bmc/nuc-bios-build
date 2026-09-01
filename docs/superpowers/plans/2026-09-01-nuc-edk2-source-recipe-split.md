# NUC EDK2 Source-Recipe Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `edk2-uefipayload_2605.bb` into three source-tree recipes plus a build-only payload recipe, and move the layer's build-input handoffs from `DEPLOY_DIR_IMAGE` to the sysroot.

**Architecture:** Three new `allarch nopackages` recipes each fetch and patch exactly one tree and stage it under `${datadir}/edk2/<tree>`, compiling nothing. The payload recipe keeps every line of its build, capsule and GUID-guard logic but loses its git `SRC_URI`, gains `DEPENDS` on the three, and builds out of a reflink copy of the staged edk2 tree. Build inputs between recipes travel through the sysroot; finished deployables keep travelling through `DEPLOY_DIR_IMAGE`.

**Tech Stack:** Yocto scarthgap (poky), kas, bitbake, EDK2 BaseTools, coreboot.

**Spec:** `docs/superpowers/specs/2026-09-01-nuc-edk2-source-recipe-split-design.md`

## Global Constraints

- Build command is `kas build`; one recipe or task is `kas shell -c 'bitbake <args>'`. Never invoke bare `bitbake` — it misses `local_conf_header` entirely (`DL_DIR`, `SSTATE_DIR`, `NUC_BIOS_PAYLOAD`, the uninative pin).
- Patch files carry mixed line endings: LF for commit prose and diff metadata, CRLF for body lines. **Never** `Edit`/`Write` a `.patch` under `recipes-bsp/edk2*/files/`. Moving one is `git mv` and nothing else.
- The three source recipes carry no `COMPATIBLE_MACHINE`. They are machine-independent source; the constraint belongs on the build recipe.
- `allarch` sets `INHIBIT_DEFAULT_DEPS` itself — do not add the line to the three source recipes. The payload recipe keeps its explicit one (it is not allarch).
- These `SRCREV`s do not change in this refactor: edk2 `fa41c179db1f9fc21eb425f44b85a16262c806ca`, edk2-platforms `75efd079fed9723db8ce02365233c03b2fdc3b92`, edk2-redfish-client `92fabf8572c226cf180c62b1204380385a518db3`.
- `LIC_FILES_CHKSUM` md5 is `2b415520383f7964e96700ae12b4570a` for all three trees (edk2 `License.txt`, edk2-platforms `License.txt`, edk2-redfish-client `LICENSE`), and `0518d409dae93098cca8dfa932f3ab1b` for `${COMMON_LICENSE_DIR}/BSD-2-Clause-Patent`. All four verified on disk 2026-09-01.
- `NUC_CAPSULE_GUID` stays `d25f89e1-94ec-4533-80b9-7f8855ce0a94`. Six hand-maintained copies exist and two build-time guards check them; neither guard may be weakened.
- No change to capsule behaviour. The open version-gate Critical (`NUC_CAPSULE_VERSION` calibrated to `1 == 1`) is out of scope and stays untouched.
- `.config.yaml` pins poky to `branch: scarthgap` with no commit, so every `kas build` fast-forwards it. A mass rebuild of native recipes means poky moved, not that a task broke something.

---

## File Structure

**Created:**

| File | Responsibility |
|---|---|
| `meta-nuc-bios/recipes-bsp/edk2/edk2_git.bb` | Fetch tianocore/edk2 with submodules, apply patches 0001–0035, prune nested submodule checkouts, stage `${datadir}/edk2/edk2`. |
| `meta-nuc-bios/recipes-bsp/edk2-platforms/edk2-platforms_git.bb` | Fetch tianocore/edk2-platforms unpatched, stage `${datadir}/edk2/edk2-platforms`. |
| `meta-nuc-bios/recipes-bsp/edk2-redfish-client/edk2-redfish-client_git.bb` | Fetch tianocore/edk2-redfish-client, apply patch 0100, stage `${datadir}/edk2/edk2-redfish-client`. |

**Moved (`git mv`, bytes preserved):**

| From | To |
|---|---|
| `recipes-bsp/edk2/files/0100-RedfishClientPkg-fit-the-client-to-a-Redfish-host-in.patch` | `recipes-bsp/edk2-redfish-client/files/` |
| `recipes-bsp/edk2/edk2-uefipayload_2605.bb` | `recipes-bsp/edk2-uefipayload/edk2-uefipayload_2605.bb` |
| `recipes-bsp/edk2/files/NucRedfishPkg/` | `recipes-bsp/edk2-uefipayload/files/NucRedfishPkg/` |
| `recipes-bsp/edk2/files/bootsplash.bmp` | `recipes-bsp/edk2-uefipayload/files/bootsplash.bmp` |

**Staying put:** patches `0001`–`0035` in `recipes-bsp/edk2/files/`. `edk2_git.bb` lands in that directory and inherits the same `FILESPATH`, so no patch file is touched.

**Modified:**

| File | Change |
|---|---|
| `recipes-bsp/edk2-uefipayload/edk2-uefipayload_2605.bb` | Drop git `SRC_URI`, add `DEPENDS`, build from `${EDK2_PATH}`, add `do_install` staging. |
| `recipes-bsp/ipxe/ipxe-efi_git.bb` | Add `do_install` staging `ipxe-intel.efidrv`; keep `do_deploy`. |
| `recipes-bsp/coreboot/coreboot_git.bb` | Read the payload and capsule tools from the sysroot; drop two `[depends]` flags and two `bbfatal` guards. |
| `README.md` | Remove the flasher-ISO paragraph referencing the deleted `nuc-coreboot-rom`. |

**Deleted:** `recipes-bsp/nuc-coreboot-rom/` (orphaned in `d2025de` when the flasher multiconfig was removed).

---

### Task 1: The edk2 and edk2-platforms source recipes

Two new recipes that nothing consumes yet. `edk2-uefipayload` is untouched and keeps building exactly as before, so this task is green on its own.

**Files:**
- Create: `meta-nuc-bios/recipes-bsp/edk2/edk2_git.bb`
- Create: `meta-nuc-bios/recipes-bsp/edk2-platforms/edk2-platforms_git.bb`

**Interfaces:**
- Consumes: nothing.
- Produces: two sysroot trees, `${datadir}/edk2/edk2` and `${datadir}/edk2/edk2-platforms`, both via `EDK2_SOURCE_ROOT = "${datadir}/edk2"`. Task 3 reads them as `${STAGING_DATADIR}/edk2/edk2` and `${STAGING_DATADIR}/edk2/edk2-platforms`.

- [ ] **Step 1: Confirm the recipes do not exist yet**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
kas shell -c 'bitbake -e edk2' 2>&1 | tail -3
```

Expected: FAIL with `ERROR: Nothing PROVIDES 'edk2'`.

- [ ] **Step 2: Write `meta-nuc-bios/recipes-bsp/edk2/edk2_git.bb`**

The patch list is the current `edk2-uefipayload_2605.bb` list with `0100` removed (it applies to a different tree; Task 2 takes it). Order is load-bearing and must not be sorted or reflowed.

```bitbake
SUMMARY = "tianocore edk2 source tree"
DESCRIPTION = "The edk2 core tree, fetched with its submodules, patched, and \
               staged into ${datadir}/edk2/edk2 as the first of the payload \
               build's PACKAGES_PATH roots. Nothing is compiled here -- not \
               even BaseTools, which is host-native and therefore built inside \
               edk2-uefipayload's own copy of this tree. \
\
               This recipe owns the edk2 tree and NOTHING else: no board \
               configuration, no key material, no build. Its two sibling \
               source-tree recipes -- edk2-platforms and edk2-redfish-client -- \
               stage themselves the same way, into the same root, so \
               ${datadir}/edk2 in the sysroot IS the three-tree workspace the \
               payload build needs. edk2-uefipayload is what turns that \
               workspace into UEFIPAYLOAD.fd."
HOMEPAGE = "https://github.com/tianocore/edk2"

LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"

PV = "202608+git${SRCPV}"

# gitsm, not git: edk2 vendors its dependencies as submodules (openssl for
# Secure Boot, brotli, oniguruma, jansson for RedfishPkg's JsonLib, ...) --
# the same fetcher approach oe-core's ovmf uses.
SRC_URI = "gitsm://github.com/tianocore/edk2.git;protocol=https;branch=master;destsuffix=edk2"

# 0001-0018: the MrChromebox commits this board needs, cherry-picked onto
# upstream master. Each carries its original authorship and a
# "(cherry picked from commit ...)" trailer. They fall into two groups --
# coreboot/payload correctness (MTRR, root bridges from HOB, the framebuffer
# BAR offset, SMMSTORE block alignment, uninitialised memory in the entry
# point) and features this board is configured to use (CFR SetupMenu,
# PRIORITIZE_INTERNAL, the BGRT logo position).
#
# 0019-0035: local. All are applied unconditionally; what they add is gated by
# DSC defines that default FALSE, so edk2-uefipayload's -D flags decide what is
# actually built. Making the *patches* conditional instead would be fragile --
# 0021, 0022 and 0023 edit regions 0020 creates or sits beside.
#
# Order follows SRC_URI order and is load-bearing. The one patch that applies
# to edk2-redfish-client rather than edk2 lives with that tree's recipe.
SRC_URI += "${@' '.join('file://' + p for p in [ \
    '0001-UefiCpuPkg-Disable-MTRR-programming-for-UefiPayloadP.patch', \
    '0002-MdeModulePkg-Don-t-remove-rejected-PCI-devices.patch', \
    '0003-UefiPayloadPkg-GraphicsOutputDxe-Allow-for-framebuff.patch', \
    '0004-MdeModulePkg-UefiBootManagerLib-Add-Pcd-to-prioritiz.patch', \
    '0005-UefiPayloadPkg-Hookup-Prioritize-Internal-build-opti.patch', \
    '0006-MdeModulePkg-UefiBootManagerLib-Honor-PrioritizeInte.patch', \
    '0007-MdeModulePkg-BootLogoLib-Add-option-to-follow-BGRT-s.patch', \
    '0008-MdeModulePkg-Logo-Add-a-PCD-to-control-the-position-.patch', \
    '0009-MdeModulePkg-FaultTolerantWrite-Don-t-check-for-bloc.patch', \
    '0010-UefiPayloadPkg-Set-PcdCpuFeaturesInitOnS3Resume-to-F.patch', \
    '0011-UefiPayloadPkg-Implement-CFR-support.patch', \
    '0012-UefiCpuPkg-CpuDxe-Gate-EFI-Memory-Attribute-Protocol.patch', \
    '0013-UefiPayloadPkg-SmmStoreLib-Support-64-bit-MMIO-store.patch', \
    '0014-UefipayloadPkg-SmmStoreLib-Set-capabilities-for-stor.patch', \
    '0015-UefiPayloadPkg-Library-CbParseLib-Populate-root-brid.patch', \
    '0016-PcRtcEntry-Don-t-assert-if-RTC-init-fails.patch', \
    '0017-UefiPayloadPkg-align-DXE-images-for-page-protections.patch', \
    '0018-UefiPayloadEntry-Fix-use-of-uninitialized-memory.patch', \
    '0019-UsbNetwork-assume-media-on-a-point-to-point-gadget.patch', \
    '0020-UefiPayloadPkg-wire-in-the-Redfish-host-interface-st.patch', \
    '0021-UefiPayloadPkg-give-the-onboard-NIC-a-UNDI-SNP-drive.patch', \
    '0022-UefiPayloadPkg-wire-in-edk2-redfish-client-RedfishCl.patch', \
    '0023-UefiPayloadPkg-give-NetworkPkg-the-protocol-producer.patch', \
    '0024-UefiPayloadPkg-retry-Redfish-HTTP-requests-at-least-.patch', \
    '0025-UefiPayloadPkg-CfrSetupMenuDxe-publish-CFR-options-a.patch', \
    '0026-UefiPayloadPkg-let-SMMSTORE-hold-authenticated-varia.patch', \
    '0027-RedfishConfigHandler-quiesce-the-Redfish-stack-after.patch', \
    '0028-UefiBootManagerLib-do-not-enumerate-USB-NICs-as-boot.patch', \
    '0029-UefiPayloadPkg-wire-in-the-EthernetInterface-feature.patch', \
    '0030-UefiPayloadPkg-build-all-three-USB-CDC-network-class.patch', \
    '0031-UsbCdcNcm-deliver-one-Ethernet-frame-per-NTB-datagra.patch', \
    '0032-UefiPayloadPkg-build-the-CDC-ACM-serial-console-drive.patch', \
    '0033-MdeModulePkg-add-a-USB-CDC-EEM-class-driver.patch', \
    '0034-UefiPayloadPkg-RMAP-region-manifest-for-SMMSTORE-back.patch', \
    '0035-UefiPayloadPkg-make-FmpDxe-FV-resident.patch', \
    ])}"

# Pinned, not AUTOREV: a floating revision makes the build non-reproducible and
# silently changes what lands in the ROM. The patch series is generated against
# this exact tree, so `patch` refusing a hunk is the signal that a bump needs
# review. edk2 master head 2026-08-04.
SRCREV = "fa41c179db1f9fc21eb425f44b85a16262c806ca"

# UNPACKDIR only exists from styhead (Yocto 5.1) on; scarthgap unpacks straight
# into WORKDIR. Without this shim, S = "${UNPACKDIR}/edk2" never expands and
# do_unpack fails its unexpanded-variable QA check.
UNPACKDIR ?= "${WORKDIR}"

S = "${UNPACKDIR}/edk2"

# Source only: nothing is compiled and nothing is packaged, but the tree still
# has to reach edk2-uefipayload's sysroot, so do_populate_sysroot runs.
inherit allarch nopackages

# Where the tree lands in the sysroot. edk2-uefipayload reads exactly this path
# under ${STAGING_DATADIR} -- keep the two in step.
EDK2_SOURCE_ROOT = "${datadir}/edk2"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    edk2_root="${D}${EDK2_SOURCE_ROOT}/edk2"

    install -d "$edk2_root"
    cp -a ${S}/. "$edk2_root/"

    # Drop the submodules OF submodules.
    #
    # gitsm checks submodules out recursively, so edk2's bring their own along:
    # openssl's interop and fuzzing dependencies (cloudflare-quiche,
    # pyca-cryptography, krb5, wycheproof, tlsfuzzer, fuzz/corpora ...),
    # libspdm's private copies of openssl and mbedtls, mipisyst's and the TPM
    # reference stack's test deps. Measured on this tree 2026-09-01: 1.08 GB of
    # a 1.6 GB source tree, and not one .inf, .dsc, .dec, .fdf or .inc anywhere
    # in edk2 references a single file inside any of them.
    #
    # libspdm is the case worth stating, because it is the biggest (580 MB) and
    # looks like a counterexample: SpdmDeviceSecretLibNull.inf does compile
    # libspdm/os_stub/spdm_device_secret_lib_null/lib.c. But that file is
    # libspdm's own, sitting beside the nested checkouts rather than inside one
    # -- os_stub/ exists precisely so an integrator can map SpdmCryptLib onto
    # its own crypto, which is what SecurityPkg does with BaseCryptLib. The
    # bundled os_stub/openssllib/openssl and os_stub/mbedtlslib/mbedtls are the
    # backends EDK2 replaces, so nothing builds them.
    #
    # Only the submodule DIRECTORIES go; everything else under a depth-1
    # submodule stays, which is what keeps that lib.c. The list is read out of
    # the .gitmodules files rather than written here, so it stays right when
    # upstream adds or drops one.
    gitmodule_paths='s/^[[:space:]]*path[[:space:]]*=[[:space:]]*//p'
    for sub in $(sed -n "$gitmodule_paths" "$edk2_root/.gitmodules"); do
        [ -f "$edk2_root/$sub/.gitmodules" ] || continue
        for nested in $(sed -n "$gitmodule_paths" "$edk2_root/$sub/.gitmodules"); do
            [ -d "$edk2_root/$sub/$nested" ] || continue
            bbnote "edk2: dropping nested submodule checkout $sub/$nested"
            rm -rf "$edk2_root/$sub/$nested"
        done
    done

    # Build bookkeeping rather than source: quilt's .pc/ backups and the
    # "patches" symlink it points at ${WORKDIR}/patches (which would stage as a
    # dangling link), then every .git in the tree.
    rm -rf "$edk2_root/.pc" "$edk2_root/patches"

    # -prune so find does not try to descend into a directory rm just removed.
    # Recursive rather than just the top level because gitsm leaves one behind
    # in every submodule it checked out. Nothing in the EDK2 build shells out
    # to git.
    find "$edk2_root" -name .git -prune -exec rm -rf {} +
}
```

- [ ] **Step 3: Write `meta-nuc-bios/recipes-bsp/edk2-platforms/edk2-platforms_git.bb`**

```bitbake
SUMMARY = "tianocore edk2-platforms source tree"
DESCRIPTION = "The edk2-platforms tree, staged into ${datadir}/edk2/edk2-platforms. \
               coreboot's own edk2 build puts nine of its subdirectories on \
               PACKAGES_PATH when CONFIG_EDK2_USE_EDK2_PLATFORMS=y, and \
               edk2-uefipayload mirrors that list so the two builds stay \
               comparable. Nothing this board builds currently references a \
               file in it; it is on the path for parity, not for content. \
               Unpatched, and nothing is compiled here."
HOMEPAGE = "https://github.com/tianocore/edk2-platforms"

# Identical BSD-2-Clause-Patent text to edk2's License.txt.
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"

PV = "202607+git${SRCPV}"

SRC_URI = "git://github.com/tianocore/edk2-platforms.git;protocol=https;branch=master;destsuffix=edk2-platforms"

# edk2-platforms head 2026-07-28. Pinned for the same reason as edk2's.
SRCREV = "75efd079fed9723db8ce02365233c03b2fdc3b92"

# UNPACKDIR only exists from styhead (Yocto 5.1) on; scarthgap unpacks straight
# into WORKDIR. Without this shim, S never expands and do_unpack fails its
# unexpanded-variable QA check.
UNPACKDIR ?= "${WORKDIR}"

S = "${UNPACKDIR}/edk2-platforms"

# Source only: nothing is compiled and nothing is packaged, but the tree still
# has to reach edk2-uefipayload's sysroot, so do_populate_sysroot runs.
inherit allarch nopackages

# Where the tree lands in the sysroot. edk2-uefipayload reads exactly this path
# under ${STAGING_DATADIR} -- keep the two in step.
EDK2_SOURCE_ROOT = "${datadir}/edk2"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${EDK2_SOURCE_ROOT}/edk2-platforms
    cp -a ${S}/. ${D}${EDK2_SOURCE_ROOT}/edk2-platforms/

    # Build bookkeeping rather than source: the git fetcher's checkout
    # metadata, and quilt's .pc/ backups plus the "patches" symlink it points
    # at ${WORKDIR}/patches (which would stage as a dangling link).
    rm -rf ${D}${EDK2_SOURCE_ROOT}/edk2-platforms/.git \
           ${D}${EDK2_SOURCE_ROOT}/edk2-platforms/.pc \
           ${D}${EDK2_SOURCE_ROOT}/edk2-platforms/patches
}
```

- [ ] **Step 4: Build both and verify the staged trees**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
kas shell -c 'bitbake edk2 edk2-platforms'
```

Expected: both succeed. Then:

```bash
ls build/tmp/sysroots-components/all/edk2/usr/share/edk2/edk2/UefiPayloadPkg/UefiPayloadPkg.dsc
ls build/tmp/sysroots-components/all/edk2-platforms/usr/share/edk2/edk2-platforms/Features/Intel
```

Expected: both paths exist.

- [ ] **Step 5: Verify the submodule prune actually ran**

```bash
root=build/tmp/sysroots-components/all/edk2/usr/share/edk2/edk2
du -sh "$root"
test ! -d "$root/SecurityPkg/DeviceSecurity/SpdmLib/libspdm/os_stub/openssllib/openssl" \
  && echo "PRUNE OK: nested libspdm openssl gone"
test ! -d "$root/CryptoPkg/Library/OpensslLib/openssl/cloudflare-quiche" \
  && echo "PRUNE OK: nested cloudflare-quiche gone"
test -f "$root/SecurityPkg/DeviceSecurity/SpdmLib/libspdm/os_stub/spdm_device_secret_lib_null/lib.c" \
  && echo "KEEP OK: libspdm's own os_stub source survived"
find "$root" -name .git | head
```

Expected: `du` reports roughly 520 MB (not 1.6 GB); all three `OK` lines print; `find` prints nothing.

If `du` still reports over 1 GB, the prune loop did not match — do not proceed. Read `$root/.gitmodules` and check the `path =` extraction against it.

- [ ] **Step 6: Verify `edk2-uefipayload` is unaffected**

```bash
kas shell -c 'bitbake -e edk2-uefipayload' | grep -E '^(S|SRCREV_edk2)='
```

Expected: still `S="${WORKDIR}/git"` and the same `SRCREV_edk2`. This task changed nothing about the payload.

- [ ] **Step 7: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/edk2/edk2_git.bb \
        meta-nuc-bios/recipes-bsp/edk2-platforms/edk2-platforms_git.bb
git commit -m "feat(nuc): give edk2 and edk2-platforms their own source recipes

Each owns one tree and stages it under \${datadir}/edk2, compiling
nothing, so a change to one tree's patch series stops re-unpacking the
others. edk2's do_install drops gitsm's recursively-fetched depth-2
submodule checkouts -- 1.08 GB of a 1.6 GB tree that no .inf, .dsc,
.dec, .fdf or .inc in edk2 references."
```

---

### Task 2: The edk2-redfish-client source recipe and the payload cutover

The atomic cutover. Patch `0100` moves to its own tree's recipe, the payload recipe moves to its own directory, and the payload starts consuming all three source recipes from the sysroot instead of fetching anything. Nothing here changes what the payload *builds* — only where its inputs come from.

**Files:**
- Create: `meta-nuc-bios/recipes-bsp/edk2-redfish-client/edk2-redfish-client_git.bb`
- Move: `recipes-bsp/edk2/files/0100-RedfishClientPkg-fit-the-client-to-a-Redfish-host-in.patch` → `recipes-bsp/edk2-redfish-client/files/`
- Move: `recipes-bsp/edk2/edk2-uefipayload_2605.bb` → `recipes-bsp/edk2-uefipayload/edk2-uefipayload_2605.bb`
- Move: `recipes-bsp/edk2/files/NucRedfishPkg/` → `recipes-bsp/edk2-uefipayload/files/NucRedfishPkg/`
- Move: `recipes-bsp/edk2/files/bootsplash.bmp` → `recipes-bsp/edk2-uefipayload/files/bootsplash.bmp`
- Modify: `recipes-bsp/edk2-uefipayload/edk2-uefipayload_2605.bb`

**Interfaces:**
- Consumes: `${STAGING_DATADIR}/edk2/edk2` and `${STAGING_DATADIR}/edk2/edk2-platforms` from Task 1.
- Produces: `${EDK2_PATH} = "${WORKDIR}/edk2"`, the writable copy every later `do_configure`/`do_compile` step reads. Task 3 adds `do_install` reading `${EDK2_PATH}/Build/UefiPayloadPkgX64/RELEASE_GCC/FV/UEFIPAYLOAD.fd`.

- [ ] **Step 1: Move the files**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build/meta-nuc-bios/recipes-bsp
mkdir -p edk2-redfish-client/files edk2-uefipayload/files
git mv edk2/files/0100-RedfishClientPkg-fit-the-client-to-a-Redfish-host-in.patch \
       edk2-redfish-client/files/
git mv edk2/edk2-uefipayload_2605.bb        edk2-uefipayload/
git mv edk2/files/NucRedfishPkg             edk2-uefipayload/files/
git mv edk2/files/bootsplash.bmp            edk2-uefipayload/files/
```

- [ ] **Step 2: Verify no patch byte changed**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
git diff --cached --stat -- '*.patch'
```

Expected: the `0100` line shows a pure rename (`0 insertions(+), 0 deletions(-)` for it). If it shows content changes, `git checkout` the file and redo the move — a rewritten patch will be rejected by quilt with "different line endings".

- [ ] **Step 3: Write `meta-nuc-bios/recipes-bsp/edk2-redfish-client/edk2-redfish-client_git.bb`**

```bitbake
SUMMARY = "tianocore edk2-redfish-client (RedfishClientPkg) source tree"
DESCRIPTION = "RedfishClientPkg -- the standard Redfish feature layer that sits \
               on top of edk2's RedfishPkg host-interface core: BiosDxe, \
               BootOptionDxe, ComputerSystemDxe and the JSON converters. \
               Staged into ${datadir}/edk2/edk2-redfish-client as one of \
               edk2-uefipayload's PACKAGES_PATH roots. \
\
               It tracks edk2 MASTER, which is the whole reason the edk2 recipe \
               beside it does too: the GUIDs this tree needs \
               (gEdkIIRedfisEventRedfishInterfaceDisconnectionGuid and friends) \
               are declared by RedfishPkg.dec on master and by no stable tag \
               through 202605. The two are one compatibility pair -- this tree \
               compiles against that edk2's RedfishPkg -- so re-measure the \
               window before moving either SRCREV."
HOMEPAGE = "https://github.com/tianocore/edk2-redfish-client"

# Identical BSD-2-Clause-Patent text to edk2's License.txt, under a different
# filename.
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2b415520383f7964e96700ae12b4570a"

PV = "202608+git${SRCPV}"

# The one patch that applies to this tree rather than to edk2. It kept its
# out-of-the-way 0100 number from when both series shared a recipe and the
# entry needed ";patchdir=" to reach this tree at all; here ${S} IS this tree,
# so it applies like any other patch.
SRC_URI = "git://github.com/tianocore/edk2-redfish-client.git;protocol=https;branch=main;destsuffix=edk2-redfish-client \
           file://0100-RedfishClientPkg-fit-the-client-to-a-Redfish-host-in.patch \
           "

# edk2-redfish-client head 2026-08-04. Pinned for the same reason as edk2's.
SRCREV = "92fabf8572c226cf180c62b1204380385a518db3"

# UNPACKDIR only exists from styhead (Yocto 5.1) on; scarthgap unpacks straight
# into WORKDIR. Without this shim, S never expands and do_unpack fails its
# unexpanded-variable QA check.
UNPACKDIR ?= "${WORKDIR}"

S = "${UNPACKDIR}/edk2-redfish-client"

# Source only: nothing is compiled and nothing is packaged, but the tree still
# has to reach edk2-uefipayload's sysroot, so do_populate_sysroot runs.
inherit allarch nopackages

# Where the tree lands in the sysroot. edk2-uefipayload reads exactly this path
# under ${STAGING_DATADIR} -- keep the two in step.
EDK2_SOURCE_ROOT = "${datadir}/edk2"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client
    cp -a ${S}/. ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client/

    # Build bookkeeping rather than source: the git fetcher's checkout
    # metadata, and quilt's .pc/ backups plus the "patches" symlink it points
    # at ${WORKDIR}/patches (which would stage as a dangling link).
    rm -rf ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client/.git \
           ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client/.pc \
           ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client/patches
}
```

- [ ] **Step 4: Build it and verify the patch applied**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
kas shell -c 'bitbake edk2-redfish-client'
grep -rl 'RedfishClientPkg' build/tmp/sysroots-components/all/edk2-redfish-client/usr/share/edk2/edk2-redfish-client/RedfishClientPkg/RedfishClientPkg.dec
```

Expected: build succeeds and the `.dec` path exists. A quilt failure here means the `git mv` disturbed the patch.

- [ ] **Step 5: Rewrite the payload recipe's source and path block**

In `meta-nuc-bios/recipes-bsp/edk2-uefipayload/edk2-uefipayload_2605.bb`:

Replace the `LIC_FILES_CHKSUM` line (this recipe now unpacks no tree to check against):

```bitbake
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-2-Clause-Patent;md5=0518d409dae93098cca8dfa932f3ab1b"
```

Replace the entire `SRC_URI` block — the three git entries, the `0001`–`0035`
`SRC_URI +=` python expression, and the `0100` line with its `patchdir` — with:

```bitbake
# Everything under files/ is build configuration and platform glue for the
# trees the three source recipes stage, not source this recipe fetches:
# NucRedfishPkg (this board's own EDK2 package, staged into the edk2 copy by
# do_configure) and the bootsplash.
SRC_URI = "file://NucRedfishPkg \
           file://bootsplash.bmp \
           "
```

Delete the four `SRCREV_*` lines and `SRCREV_FORMAT`. Replace the `PV` line: with no git source, `${SRCPV}` no longer resolves to anything meaningful.

```bitbake
# Not an upstream version: this is the payload's own. The trees it builds
# carry their own PVs in their own recipes.
PV = "2605"
```

Add the dependency on the three trees, immediately above the existing `DEPENDS` line:

```bitbake
# The three source trees, each fetched, patched and staged by its own recipe
# under ${STAGING_DATADIR}/edk2 -- see the EDK2_*_PATH block below and
# recipes-bsp/{edk2,edk2-platforms,edk2-redfish-client}.
DEPENDS = "edk2 edk2-platforms edk2-redfish-client"
DEPENDS += "nasm-native acpica-native util-linux-native openssl-native"
```

(That replaces the current single `DEPENDS = "nasm-native acpica-native util-linux-native openssl-native"` line. The `DEPENDS += ipxe-efi` conditional below it stays as it is; Task 3 changes how ipxe's artifact is read, not whether it is depended on.)

Replace the `S` / path block:

```bitbake
# UNPACKDIR only exists from styhead (Yocto 5.1) on; scarthgap unpacks straight
# into WORKDIR.
UNPACKDIR ?= "${WORKDIR}"

# There is no source tree to point S at -- the file:// entries above unpack
# straight into ${WORKDIR}, and the tree that IS built lives in EDK2_PATH.
S = "${UNPACKDIR}"
B = "${WORKDIR}/build"

# Where the three sibling recipes stage their trees; must match the
# EDK2_SOURCE_ROOT they install into. In the sysroot this directory IS a
# complete EDK2 workspace for UefiPayloadPkg.
EDK2_SOURCE_ROOT = "${STAGING_DATADIR}/edk2"

# Read straight out of the sysroot: the build never writes into either.
EDK2_PLATFORMS_PATH = "${EDK2_SOURCE_ROOT}/edk2-platforms"
EDK2_REDFISH_CLIENT_PATH = "${EDK2_SOURCE_ROOT}/edk2-redfish-client"

# edk2 cannot be read in place. do_configure stages NucRedfishPkg into it,
# rewrites UefiPayloadPkg.dsc's FmpDxe certificate PCD, writes
# MdeModulePkg/Logo/Logo.bmp and UefiPayloadPkg/NetworkDrivers/, and do_compile
# builds host-native BaseTools inside it -- so the build gets a private,
# writable copy. Rebuilt from scratch every configure, which is also what makes
# turning a knob back off actually take effect rather than leaving yesterday's
# edits behind.
EDK2_SRC = "${EDK2_SOURCE_ROOT}/edk2"
EDK2_PATH = "${WORKDIR}/edk2"
```

Update `EDK2_BOOTSPLASH_FILE`'s default — it already reads `${WORKDIR}/bootsplash.bmp`, which is still correct, so leave it.

Update `EDK2_PACKAGES_PATH` so its first entry is the copy rather than `${S}`:

```bitbake
EDK2_PACKAGES_PATH = "${EDK2_PATH}:${EDK2_PLATFORMS_PATH}/Platform/Intel:${EDK2_PLATFORMS_PATH}/Silicon/Intel:${EDK2_PLATFORMS_PATH}/Features/Intel:${EDK2_PLATFORMS_PATH}/Features/Intel/Debugging:${EDK2_PLATFORMS_PATH}/Features/Intel/Network:${EDK2_PLATFORMS_PATH}/Features/Intel/OutOfBandManagement:${EDK2_PLATFORMS_PATH}/Features/Intel/PowerManagement:${EDK2_PLATFORMS_PATH}/Features/Intel/SystemInformation:${EDK2_PLATFORMS_PATH}/Features/Intel/UserInterface:${EDK2_REDFISH_CLIENT_PATH}"
```

- [ ] **Step 6: Add the tree copy at the top of `do_configure`**

Insert as the first thing `do_configure` does, before the NucRedfishPkg stage:

```bash
do_configure() {
    # --- private, writable copy of the staged edk2 tree ----------------------
    # The sysroot tree is read-only and shared. Everything below writes into
    # the tree (NucRedfishPkg, the DSC certificate PCD, Logo.bmp,
    # NetworkDrivers/), and do_compile builds BaseTools into it.
    #
    # --reflink=auto because this tree is ~520 MB even after the edk2 recipe
    # drops gitsm's recursively-fetched depth-2 submodule checkouts. On a CoW
    # filesystem the copy is metadata only; anywhere else it falls back to a
    # real one.
    rm -rf "${EDK2_PATH}"
    mkdir -p "${EDK2_PATH}"
    cp -a --reflink=auto "${EDK2_SRC}/." "${EDK2_PATH}/"

    # --- stage NucRedfishPkg -------------------------------------------------
    ...
```

- [ ] **Step 7: Rewrite every remaining `${S}` in the build tasks**

There are 26 of them, all pointing at what is now `${EDK2_PATH}`. Rewrite mechanically, then audit — a missed one silently resolves to `${WORKDIR}` and fails far from its cause.

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build/meta-nuc-bios/recipes-bsp/edk2-uefipayload
python3 - edk2-uefipayload_2605.bb <<'PY'
import sys
p = sys.argv[1]
src = open(p, encoding='utf-8').read()
head, sep, body = src.partition('do_configure() {')
assert sep, "do_configure() { not found -- refusing to rewrite blindly"
n = body.count('${S}')
body = body.replace('${S}', '${EDK2_PATH}')
open(p, 'w', encoding='utf-8').write(head + sep + body)
print("rewrote %d ${S} references below do_configure" % n)
PY
```

Expected: `rewrote 26 ${S} references below do_configure`.

If the count is not 26, stop and inspect — the recipe drifted from what this plan was written against.

- [ ] **Step 8: Verify no stale `${S}` remains in a build task**

```bash
grep -n '\${S}' edk2-uefipayload_2605.bb
```

Expected: **zero** hits (`grep` exits 1). Nothing below `do_configure() {` may reference `${S}`, and as executed nothing above it does either — the `do_patch defaults to ${S}` remark was rewritten out of the SRC_URI comment along with the patches it described, and `S = "${UNPACKDIR}"` is an assignment, not a reference, so `grep '\${S}'` does not match it. Any hit at all means the rewrite was incomplete; fix it by hand.

- [ ] **Step 9: Point `WORKSPACE` and the Build output at the copy**

In `do_compile`, the mechanical rewrite already changed these; confirm they now read:

```bash
    cd ${EDK2_PATH}
    ...
    export WORKSPACE="${EDK2_PATH}"
    export PACKAGES_PATH="${EDK2_PACKAGES_PATH}"
    export EDK_TOOLS_PATH="${EDK2_PATH}/BaseTools"
    export CONF_PATH="${EDK2_PATH}/Conf"
    export PYTHON_COMMAND="python3"
    export PATH="${EDK2_PATH}/BaseTools/BinWrappers/PosixLike:$PATH"
```

`WORKSPACE` is the edk2 copy itself, not its parent: unlike the rpi5 firmware build, this platform's DSC (`UefiPayloadPkg/UefiPayloadPkg.dsc`) lives *inside* edk2, so `-p UefiPayloadPkg/UefiPayloadPkg.dsc` resolves against the tree directly.

- [ ] **Step 10: Build the payload**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
kas shell -c 'bitbake edk2-uefipayload'
```

Expected: succeeds, and `build/tmp/deploy/images/nuc5i7ryh/UEFIPAYLOAD.fd` is refreshed.

- [ ] **Step 11: Verify the payload is byte-comparable to the pre-refactor build**

The FMP GUID must still be present in the FV as 16 raw bytes — that is what patch 0035's `INF FILE_GUID` line and coreboot's ESRT entry both key on.

```bash
python3 - build/tmp/deploy/images/nuc5i7ryh/UEFIPAYLOAD.fd <<'PY'
import sys, uuid
g = uuid.UUID("d25f89e1-94ec-4533-80b9-7f8855ce0a94")
d = open(sys.argv[1], 'rb').read()
print("FmpDxe FFS GUID present:", g.bytes_le in d)
print("size:", len(d))
PY
```

Expected: `FmpDxe FFS GUID present: True`.

- [ ] **Step 12: Verify the edk2 tree recipes were sstate hits**

```bash
grep "Sstate summary" build/tmp/log/cooker/*/console-latest.log | tail -1
```

Expected: the payload rebuilt but `edk2`, `edk2-platforms` and `edk2-redfish-client` did not re-unpack — this is the property the whole refactor exists for, checked properly in Task 5.

- [ ] **Step 13: Commit**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
git add -A meta-nuc-bios/recipes-bsp/edk2 \
           meta-nuc-bios/recipes-bsp/edk2-redfish-client \
           meta-nuc-bios/recipes-bsp/edk2-uefipayload
git commit -m "refactor(nuc): build the payload from staged trees, not its own fetch

edk2-redfish-client gets its own source recipe (and patch 0100, which no
longer needs ';patchdir=' now that \${S} is that tree). edk2-uefipayload
moves to its own directory, drops all three git fetches, and builds from
a reflink copy of the edk2 tree the sysroot carries.

No change to what is built: same SRCREVs, same patch order, same build
flags, same FmpDxe FFS GUID in the FV."
```

---

### Task 3: Move the build-input handoffs to the sysroot

Three artifacts cross recipe boundaries as inputs to a build, and all three currently travel through `DEPLOY_DIR_IMAGE` with a hand-written task dependency and a `bbfatal` existence guard. Source and tool inputs belong in the sysroot; finished deployables stay in `DEPLOY_DIR_IMAGE`.

**Files:**
- Modify: `meta-nuc-bios/recipes-bsp/ipxe/ipxe-efi_git.bb`
- Modify: `meta-nuc-bios/recipes-bsp/edk2-uefipayload/edk2-uefipayload_2605.bb`
- Modify: `meta-nuc-bios/recipes-bsp/coreboot/coreboot_git.bb`

**Interfaces:**
- Consumes: `${EDK2_PATH}` from Task 2.
- Produces: `${STAGING_DATADIR}/ipxe/ipxe-intel.efidrv`,
  `${STAGING_DATADIR}/edk2-uefipayload/UEFIPAYLOAD.fd`,
  `${STAGING_DATADIR}/edk2-uefipayload/capsule-tools/AppendRmapManifest.py`,
  `${STAGING_DATADIR}/edk2-uefipayload/capsule-tools/BaseTools-Source-Python/`.

- [ ] **Step 1: Stage `ipxe-intel.efidrv` from ipxe-efi**

In `ipxe-efi_git.bb`, replace `do_install[noexec] = "1"` with a real install, and add `nopackages` to the inherit line (this is firmware, not target userspace, and without it the new `${datadir}/ipxe` tree trips an installed-but-not-shipped QA error):

```bitbake
inherit deploy nopackages
```

```bitbake
# The UNDI/SNP driver is a build INPUT to edk2-uefipayload, so it travels
# through the sysroot rather than DEPLOY_DIR_IMAGE: a normal DEPENDS then
# carries both the ordering and the signature, with no do_configure[depends]
# and no existence guard on the consuming side.
#
# ipxe.rom stays in do_deploy: it is a finished artifact a human collects,
# not an input to another recipe's build.
do_install() {
    install -d ${D}${datadir}/ipxe
    install -m 0644 ${S}/src/bin-x86_64-efi/intel.efidrv \
        ${D}${datadir}/ipxe/ipxe-intel.efidrv
}
```

Delete the `do_install[noexec] = "1"` line. `do_deploy` is unchanged: it keeps installing both `ipxe.rom` and `ipxe-intel.efidrv`, which remain the artifacts a human collects.

- [ ] **Step 2: Read it from the sysroot in the payload**

In `edk2-uefipayload_2605.bb`, delete this line entirely:

```bitbake
do_configure[depends] += "${@'ipxe-efi:do_deploy' if d.getVar('EDK2_IPXE') == '1' else ''}"
```

and its two-line comment above it (`# DEPENDS alone only guarantees do_populate_sysroot; ...`). In `do_configure`, change the install source:

```bash
    if [ "${EDK2_IPXE}" = "1" ]; then
        install -d ${EDK2_PATH}/UefiPayloadPkg/NetworkDrivers
        install -m 0644 ${STAGING_DATADIR}/ipxe/ipxe-intel.efidrv \
            ${EDK2_PATH}/UefiPayloadPkg/NetworkDrivers/ipxe-intel.efidrv
    fi
```

- [ ] **Step 3: Stage the payload FV and capsule tooling**

In `edk2-uefipayload_2605.bb`, add `nopackages` to the inherit line:

```bitbake
inherit deploy nopackages
```

Delete `do_install[noexec] = "1"` and add:

```bash
# Build inputs for coreboot_git.bb, staged where a plain DEPENDS reaches them.
#
# UEFIPAYLOAD.fd is what coreboot embeds in CBFS. AppendRmapManifest.py and
# GenerateCapsule.py run against the FINISHED ROM, which does not exist here --
# this recipe produces one of its inputs -- so coreboot's do_deploy is where
# the capsule gets built, and it needs these tools to do it. GenerateCapsule.py
# is not self-contained: it imports sibling packages under
# BaseTools/Source/Python (Common.Uefi.Capsule.*, Common.Edk2.Capsule.*), so
# the whole tree travels, not just the one script.
#
# The same UEFIPAYLOAD.fd also goes to DEPLOY_DIR_IMAGE in do_deploy below.
# That is not a duplicate with a different purpose: the sysroot copy is
# coreboot's build input, the deployed copy is the artifact a human collects.
do_install() {
    install -d ${D}${datadir}/edk2-uefipayload
    install -m 0644 ${EDK2_PATH}/Build/UefiPayloadPkgX64/RELEASE_GCC/FV/UEFIPAYLOAD.fd \
        ${D}${datadir}/edk2-uefipayload/UEFIPAYLOAD.fd

    install -d ${D}${datadir}/edk2-uefipayload/capsule-tools
    cp -a ${EDK2_PATH}/BaseTools/Source/Python \
        ${D}${datadir}/edk2-uefipayload/capsule-tools/BaseTools-Source-Python
    install -m 0755 ${EDK2_PATH}/UefiPayloadPkg/Tools/AppendRmapManifest.py \
        ${D}${datadir}/edk2-uefipayload/capsule-tools/AppendRmapManifest.py
}
```

Then trim `do_deploy` to just the two deployables, deleting its whole
`--- capsule tooling, for coreboot_git.bb's do_deploy ---` block:

```bash
do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${EDK2_PATH}/Build/UefiPayloadPkgX64/RELEASE_GCC/FV/UEFIPAYLOAD.fd \
        ${DEPLOYDIR}/UEFIPAYLOAD.fd
    install -m 0644 ${B}/UEFIPAYLOAD.report.txt ${DEPLOYDIR}/UEFIPAYLOAD.report.txt
}
```

- [ ] **Step 4: Set the payload's PACKAGE_ARCH**

The recipe stages a built FV for one board. Add beside `COMPATIBLE_MACHINE`:

```bitbake
# The staged FV is a binary built for one board, so the sysroot component is
# machine-specific -- the same reasoning rpi5-uefi-build's edk2-non-osi applies
# to the TF-A binary it carries. The three source-tree recipes stay allarch:
# they stage source, which is not.
PACKAGE_ARCH = "${MACHINE_ARCH}"
```

- [ ] **Step 5: Consume both from the sysroot in coreboot**

In `coreboot_git.bb`, add the dependency beside the existing `DEPENDS`:

```bitbake
# The payload FV and the capsule tooling, staged under
# ${STAGING_DATADIR}/edk2-uefipayload. Not declared when the LinuxBoot payload
# is selected -- with the knob on linuxboot, the edk2 payload is not in the
# dependency graph at all.
DEPENDS += "${@'edk2-uefipayload' if d.getVar('NUC_BIOS_PAYLOAD') != 'linuxboot' else ''}"
```

Replace the `EDK2_CAPSULE_TOOLS` assignment and its whole `MUST be
DEPLOY_DIR_IMAGE, not DEPLOYDIR` comment block with:

```bitbake
# Build inputs from edk2-uefipayload, read out of this recipe's own sysroot.
#
# The layer's rule: source and tool inputs travel through the sysroot via
# DEPENDS, which carries both the ordering and the signature; finished
# deployables travel through DEPLOY_DIR_IMAGE with an explicit do_X[depends].
# UEFIPAYLOAD.fd and the capsule tooling are inputs to this build, so they come
# from the sysroot and need no hand-written task dependency and no existence
# guard -- a missing file here is a build-system bug, not an operator error.
#
# This recipe's own do_deploy still writes to ${DEPLOYDIR}, which is correct:
# that is its new output, published into DEPLOY_DIR_IMAGE by deploy.bbclass
# once the task completes. ${DEPLOYDIR} is never a path to read another
# recipe's output from -- it is a private per-task staging directory
# (${WORKDIR}/deploy-${PN}), and two sibling recipes' values are never the same
# directory on disk.
EDK2_PAYLOAD_FD = "${STAGING_DATADIR}/edk2-uefipayload/UEFIPAYLOAD.fd"
EDK2_CAPSULE_TOOLS = "${STAGING_DATADIR}/edk2-uefipayload/capsule-tools"
```

In `do_configure`'s edk2 branch, delete the two-line existence guard and use the
new variable in both places:

```bash
    else
        # The payload is built by the edk2-uefipayload recipe and staged into
        # this recipe's sysroot; substitute its absolute path (see the comment
        # block in payload-edk2.config for why PAYLOAD_EDK2 stays on).
        sed -e "s#@UEFIPAYLOAD@#${EDK2_PAYLOAD_FD}#" \
            ${WORKDIR}/payload-edk2.config >> ${B}/.config
```

and in the GUID drift guard's argument list, `"${DEPLOY_DIR_IMAGE}/UEFIPAYLOAD.fd"`
becomes `"${EDK2_PAYLOAD_FD}"`.

In `do_deploy`, delete the tooling existence guard:

```bash
    # --- Step 1: append the RMAP manifest and generate the capsule -------
    tools="${EDK2_CAPSULE_TOOLS}"

    python3 "$tools/AppendRmapManifest.py" \
```

Finally, reduce the `python () { ... }` block to the LinuxBoot branch only:

```bitbake
# The LinuxBoot payload's inputs are finished deployables (a bzImage and a
# u-root initramfs), so they keep the DEPLOY_DIR_IMAGE + explicit [depends]
# arrangement. The edk2 payload's inputs are build inputs and come through the
# sysroot -- see the DEPENDS line above.
python () {
    if d.getVar('NUC_BIOS_PAYLOAD') == 'linuxboot':
        d.appendVarFlag('do_compile', 'depends',
                        ' linux-linuxboot:do_deploy u-root:do_deploy')
}
```

- [ ] **Step 6: Update the two stale `bbfatal` messages that describe the old ordering**

In `coreboot_git.bb`'s `nuc_capsule_resolve_keys` region, the missing-key message still says "do_configure already depends on its do_deploy for UEFIPAYLOAD.fd". Replace that clause:

```bash
                bbfatal "no capsule signing key at $fmp_keydir/$f -- edk2-uefipayload's do_configure generates this keypair when NUC_CAPSULE_CERT/NUC_CAPSULE_KEY are unset. Build edk2-uefipayload before coreboot (the normal order; this recipe DEPENDS on it for the payload FV, which orders its do_configure first), or set NUC_CAPSULE_CERT/NUC_CAPSULE_KEY here to wherever the signing identity actually lives."
```

Also update the comment above `nuc_capsule_resolve_keys` that reads
"(do_configure[depends] below covers the ordering)" to "(the DEPENDS on
edk2-uefipayload covers the ordering)".

- [ ] **Step 7: Verify the wiring parses as designed**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
kas shell -c 'bitbake -e coreboot' | grep -E '^(EDK2_PAYLOAD_FD|EDK2_CAPSULE_TOOLS|PACKAGE_ARCH)='
kas shell -c 'bitbake -e edk2-uefipayload' | grep -E '^(PACKAGE_ARCH|EDK2_PATH|EDK2_SRC)='
```

Expected: the coreboot paths both begin with a `.../recipe-sysroot/usr/share/` prefix, not `.../deploy/images/`; `edk2-uefipayload`'s `PACKAGE_ARCH` is `nuc5i7ryh`.

- [ ] **Step 8: Full build**

```bash
kas build
```

Expected: succeeds, producing `build/tmp/deploy/images/nuc5i7ryh/coreboot-nuc5i7ryh.rom` and `nuc-firmware.cap`.

- [ ] **Step 9: Verify the capsule is unchanged in kind**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
python3 - build/tmp/deploy/images/nuc5i7ryh/coreboot-nuc5i7ryh-rmap.rom <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
sig, ver, n = struct.unpack('<IHH', d[-8:])
assert sig == 0x50414D52, "RMAP signature missing"
print("RMAP ok: version %d, %d region(s)" % (ver, n))
PY
ls -l build/tmp/deploy/images/nuc5i7ryh/nuc-firmware.cap
```

Expected: `RMAP ok: version 1, 1 region(s)` and a `nuc-firmware.cap` on disk. This is the same check `do_deploy` already runs; running it again here confirms the tooling still reached the build after moving to the sysroot.

- [ ] **Step 10: Commit**

```bash
git add meta-nuc-bios/recipes-bsp/ipxe/ipxe-efi_git.bb \
        meta-nuc-bios/recipes-bsp/edk2-uefipayload/edk2-uefipayload_2605.bb \
        meta-nuc-bios/recipes-bsp/coreboot/coreboot_git.bb
git commit -m "refactor(nuc): build inputs travel through the sysroot

ipxe-intel.efidrv, UEFIPAYLOAD.fd and the capsule tooling are inputs to
another recipe's build, so a plain DEPENDS should carry both the
ordering and the signature. Drops three hand-written do_X[depends]
flags and two bbfatal existence guards; DEPLOY_DIR_IMAGE keeps carrying
the finished deployables, which is the half of the rule that was always
right."
```

---

### Task 4: Delete the orphaned nuc-coreboot-rom recipe

`nuc-coreboot-rom.bb` stages the ROM into a flasher live image that no longer exists. The multiconfig it served — `conf/multiconfig/flasher.conf`, `nuc-flasher-image.bb`, `kas-flasher.yml` — was removed in `d2025de` and replaced by `scripts/make-flasher-img.sh`; the recipe's `do_install[mcdepends]` line went with it, correctly. Nothing on this branch builds the recipe.

**Files:**
- Delete: `meta-nuc-bios/recipes-bsp/nuc-coreboot-rom/`
- Modify: `README.md` (the flasher-ISO paragraph at :126)
- Modify: `meta-nuc-bios/recipes-bsp/coreboot/coreboot_git.bb` (stale reference)

**Interfaces:**
- Consumes: nothing.
- Produces: nothing.

- [ ] **Step 1: Confirm it is genuinely orphaned**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
grep -rn 'nuc-coreboot-rom' --include='*.bb' --include='*.bbappend' --include='*.conf' --include='*.yaml' . | grep -v '^./poky'
```

Expected: only the recipe itself and the two prose comments. No `IMAGE_INSTALL`, no `DEPENDS`, no multiconfig. If anything else appears, stop — it is not orphaned and this task does not apply.

- [ ] **Step 2: Delete the recipe**

```bash
git rm -r meta-nuc-bios/recipes-bsp/nuc-coreboot-rom
```

- [ ] **Step 3: Fix the stale reference in coreboot_git.bb**

Task 3 already rewrote the comment block containing it. Confirm nothing remains:

```bash
grep -n 'nuc-coreboot-rom' meta-nuc-bios/recipes-bsp/coreboot/coreboot_git.bb
```

Expected: no output. If a hit remains, delete that clause from the comment.

- [ ] **Step 4: Fix README.md**

Replace the whole `## Flasher live ISO (flash remotely over the JetKVM)` section — `README.md:110` through the end of that section at `:137` — with the text below. It describes `scripts/make-flasher-img.sh`, which is what actually builds the flasher on this branch, and drops every reference to `nuc-coreboot-rom`, `kas-flasher.yml` and `nuc-flasher-image`.

````markdown
## Self-flashing image (flash remotely over the JetKVM)

Useful for updates once coreboot is in — it uses the internal programmer, so it
cannot perform the initial flash past the stock BIOS's `SMM_BWP`.

To flash without a local shell, build a self-contained bootable image that
carries `flashrom`, this build's coreboot ROM, and an init that does the whole
job unattended — attach it to the NUC as JetKVM virtual media, or dd it to a
USB stick, and boot it:

```sh
./scripts/make-flasher-img.sh
# -> nuc-bios-flasher.img   (defaults: the ROM from build/tmp/deploy, repo root)
```

It is a single EFI unified kernel image (a stock Alpine -lts kernel plus a
bundled static busybox, flashrom and the ROM), so it needs no multiconfig and
no second Yocto build. On boot its init verifies the BIOS region and:

- **already this ROM** — reboots, so leaving it attached never loops
- **differs** — flashes the BIOS region only, then reboots
- **write fails** — drops to a shell and does *not* reboot

Internal flashing needs no SOIC clip because the running coreboot leaves the
SPI unlocked (`BOOTMEDIA_LOCK_NONE`). A machine still on the stock Intel BIOS
has `SMM_BWP` set and cannot self-flash — use `scripts/nuc-spi.sh` with the
clip once to get coreboot on in the first place.

Recovery if it won't POST: flash the factory backup back with the clip,
`flashrom -p <programmer> --ifd -i bios -w stock-bios.rom`.
````

- [ ] **Step 5: Verify the layer still parses**

```bash
kas shell -c 'bitbake -p'
```

Expected: parsing completes with no error and no warning naming `nuc-coreboot-rom`.

- [ ] **Step 6: Commit**

```bash
git add -A meta-nuc-bios/recipes-bsp README.md
git commit -m "chore(nuc): drop the orphaned nuc-coreboot-rom recipe

Its consumer -- the flasher multiconfig and its live image -- was
removed in d2025de in favour of scripts/make-flasher-img.sh, and the
recipe's mcdepends went with it. Nothing has built this since. README
still described the removed ISO flow."
```

---

### Task 5: Prove the incremental-rebuild property

The acceptance test for the whole refactor: a change to one tree's patch series must rebuild the payload without re-unpacking the other trees. This task changes no shipped file — it verifies, then restores.

**Files:**
- Test only. No file is left modified.

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: nothing.

- [ ] **Step 1: Establish a clean baseline**

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
kas build
kas shell -c 'bitbake -e edk2 edk2-redfish-client' >/dev/null && echo "baseline built"
```

Expected: a no-op build (everything a sstate hit or already current).

- [ ] **Step 2: Perturb the redfish-client patch and confirm the blast radius**

Touching the file's mtime is not enough — bitbake hashes content. Append a
trailing comment line, which quilt ignores:

```bash
patch=meta-nuc-bios/recipes-bsp/edk2-redfish-client/files/0100-RedfishClientPkg-fit-the-client-to-a-Redfish-host-in.patch
printf '\n# rebuild-property probe, reverted immediately\n' >> "$patch"
kas shell -c 'bitbake -n edk2 edk2-platforms edk2-redfish-client edk2-uefipayload coreboot' \
  | grep -E 'do_(unpack|patch|compile|deploy)' | sort | uniq
```

Expected in the dry-run output:
- `edk2-redfish-client:do_unpack`, `:do_patch` — present.
- `edk2-uefipayload:do_configure`/`:do_compile`, `coreboot:do_compile` — present.
- `edk2:do_unpack` and `edk2-platforms:do_unpack` — **absent**. This is the property. Before the refactor, all three re-unpacked.

- [ ] **Step 3: Restore the patch and confirm the tree is clean**

```bash
git checkout -- "$patch"
git status --short
```

Expected: no output from `git status`.

- [ ] **Step 4: Confirm the reverse direction — a NucRedfishPkg edit touches no tree**

```bash
glue=meta-nuc-bios/recipes-bsp/edk2-uefipayload/files/NucRedfishPkg/NucRedfishPkg.dec
printf '\n# rebuild-property probe, reverted immediately\n' >> "$glue"
kas shell -c 'bitbake -n edk2 edk2-platforms edk2-redfish-client edk2-uefipayload' \
  | grep -E 'do_unpack' | sort | uniq
git checkout -- "$glue"
```

Expected: **no** `do_unpack` line for any of the three source recipes. Only the payload rebuilds.

- [ ] **Step 5: Confirm both GUID drift guards still fire**

The refactor moved the files both guards read. A guard that silently stopped
checking would be invisible until a capsule matched no FMP on hardware, so
prove each one still fails the build when the GUID disagrees.

`bitbake`'s `-R/--postread` takes a file, so write the override to one. Scope
each override to the recipe under test: an unscoped `NUC_CAPSULE_GUID` perturbs
both recipes at once, and since coreboot `DEPENDS` on `edk2-uefipayload`, the
payload's guard fires first and coreboot's `do_configure` never runs — the
coreboot probe would then prove nothing about coreboot's guard.

```bash
cd /home/appkins/src/pi-bmc/nuc-bios-build
echo 'NUC_CAPSULE_GUID:pn-edk2-uefipayload = "00000000-0000-0000-0000-000000000000"' > build/guid-probe-payload.conf
kas shell -c 'bitbake -R build/guid-probe-payload.conf -c configure -f edk2-uefipayload' 2>&1 | tail -20
```

Expected: FAIL with `NUC_CAPSULE_GUID drift:` naming `NucCapsuleOnDiskLib.c`,
`NucRedfishInventory.c` and `UefiPayloadPkg.fdf`. Then the coreboot side, with
the payload left alone so it configures cleanly and coreboot's own guard is
what runs:

```bash
echo 'NUC_CAPSULE_GUID:pn-coreboot = "00000000-0000-0000-0000-000000000000"' > build/guid-probe-coreboot.conf
kas shell -c 'bitbake -R build/guid-probe-coreboot.conf -c configure -f coreboot' 2>&1 | tail -20
```

Expected: FAIL naming `payload-edk2.config` or the payload FV.

Restore both to a clean state before continuing:

```bash
rm -f build/guid-probe-payload.conf build/guid-probe-coreboot.conf
kas shell -c 'bitbake -c configure -f edk2-uefipayload coreboot'
```

Expected: both succeed, printing `UefiPayloadPkg.dsc FmpDxe block OK`.

- [ ] **Step 6: Record the result**

Append a short section to `docs/superpowers/specs/2026-09-01-nuc-edk2-source-recipe-split-design.md` under a new `## Verified` heading, stating the observed blast radius for both probes and the date. Replace the spec's `**Status:** approved, not yet implemented` line with `**Status:** implemented`.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/specs/2026-09-01-nuc-edk2-source-recipe-split-design.md
git commit -m "docs(nuc): record the verified rebuild blast radius

A redfish-client patch edit reruns that tree's unpack and patch and the
payload build, and leaves edk2's and edk2-platforms' unpacks as sstate
hits. A NucRedfishPkg edit touches no tree recipe at all."
```

---

## Notes for the executor

**Hardware validation is not in this plan.** The refactor is a no-op for what
the firmware does: same SRCREVs, same patch order, same build flags. The
`nuc-firmware.cap` and `coreboot-nuc5i7ryh.rom` it produces should be
functionally identical to the pre-refactor build's. Byte-identical is not
expected — build paths are embedded in some EDK2 output — so do not treat a
checksum difference as a failure.

**The open capsule Critical is not yours.** `NUC_CAPSULE_VERSION` is calibrated
to `1 == 1`, which makes the scanner delete a capsule without applying it. It
is tracked on `feat/capsule-updates` and must not be touched here; a fix
landing in this branch would mix two reviews.

**The first build after Task 3 is a full rebuild, and that is expected.**
Renaming the payload recipe's directory does not change its `PN`, but changing
its `PACKAGE_ARCH` from `corei7-64` to `nuc5i7ryh` moves its sysroot component
and invalidates everything downstream of it. Budget for a from-scratch edk2
payload build (tens of minutes) once, not for every task.

**If a build is unexpectedly slow,** check the sstate summary before assuming a
task broke something:

```bash
grep "Sstate summary" build/tmp/log/cooker/*/console-latest.log | tail -1
```

A high `Missed` count with a mass rebuild of native recipes almost always means
poky moved — `.config.yaml` pins it to `branch: scarthgap` with no commit, so
every `kas build` fast-forwards it.
