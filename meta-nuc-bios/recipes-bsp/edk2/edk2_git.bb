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
    #
    # The prune's only failure mode is silence: a `sed` that fails inside the
    # `$(...)` of a `for` word list does not trip `set -e`, so a renamed or
    # missing .gitmodules yields an empty list, the loop does nothing, and the
    # 1.6 GB tree stages into every consumer sysroot and into sstate with no
    # diagnostic. Assert the file and the outcome instead.
    [ -f "$edk2_root/.gitmodules" ] || \
        bbfatal "edk2: no .gitmodules at the top of the staged tree -- the nested-submodule prune would silently do nothing and stage ~1.6 GB into every consumer sysroot and into sstate"

    pruned=0
    gitmodule_paths='s/^[[:space:]]*path[[:space:]]*=[[:space:]]*//p'
    for sub in $(sed -n "$gitmodule_paths" "$edk2_root/.gitmodules"); do
        [ -f "$edk2_root/$sub/.gitmodules" ] || continue
        for nested in $(sed -n "$gitmodule_paths" "$edk2_root/$sub/.gitmodules"); do
            [ -d "$edk2_root/$sub/$nested" ] || continue
            bbnote "edk2: dropping nested submodule checkout $sub/$nested"
            rm -rf "$edk2_root/$sub/$nested"
            pruned=$((pruned + 1))
        done
    done

    if [ "$pruned" -eq 0 ]; then
        bbfatal "edk2: nested-submodule prune removed nothing -- upstream dropped every depth-2 submodule, or the .gitmodules layout changed and the prune stopped matching. Either way ~1.6 GB is about to stage into every consumer sysroot and into sstate; confirm before relaxing this."
    fi
    bbnote "edk2: pruned $pruned nested submodule checkouts"

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
