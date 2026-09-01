SUMMARY = "EDK2 UefiPayloadPkg (upstream tianocore) — UEFI payload for coreboot"
DESCRIPTION = "Builds UEFIPAYLOAD.fd, the UEFI environment coreboot jumps \
into on the NUC5i7RYH, with the Redfish host-interface stack and \
edk2-redfish-client compiled in. \
\
This tracks upstream tianocore/edk2 rather than the MrChromebox fork \
coreboot's payloads/external/edk2 machinery defaults to. The fork is exactly \
edk2-stable202605 plus 103 commits and nothing behind it, so the delta is a \
patch series, not a divergent tree: the eighteen of those commits this board \
actually needs are carried in SRC_URI below and cherry-pick onto master \
without conflict. See the patch headers for what each one is for. \
\
Moving to upstream is what makes RedfishClientPkg buildable. The GUIDs it \
needs (gEdkIIRedfisEventRedfishInterfaceDisconnectionGuid and friends) are \
declared by RedfishPkg.dec on master and by no stable tag through 202605 -- \
including the one the fork sits on, whose RedfishPkg is byte-identical to \
upstream's. It was never a fork problem. \
\
Building the payload here rather than inside coreboot's payloads/external \
machinery is what makes NucRedfishPkg possible: its sources are staged into \
the tree by do_configure, instead of having to patch a tree that coreboot \
clones halfway through its own do_compile."
HOMEPAGE = "https://github.com/tianocore/edk2"
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"

inherit deploy

# Three source trees, all placed on PACKAGES_PATH:
#
#   edk2 (gitsm)          the payload itself. edk2 vendors its deps as
#                         submodules (openssl for Secure Boot, brotli,
#                         oniguruma, jansson for RedfishPkg's JsonLib, ...) --
#                         same fetcher approach as oe-core's ovmf.
#   edk2-platforms        Features/Intel/**; coreboot puts nine of its
#                         subdirectories on PACKAGES_PATH (mirrored below).
#                         Nothing here references it, but keeping it means the
#                         two builds stay comparable.
#   edk2-redfish-client   RedfishClientPkg: the feature layer above the host
#                         interface -- BiosDxe, BootOptionDxe,
#                         ComputerSystemDxe and the JSON converters.
SRC_URI = "gitsm://github.com/tianocore/edk2.git;protocol=https;branch=master;name=edk2;destsuffix=git \
           git://github.com/tianocore/edk2-platforms.git;protocol=https;branch=master;name=platforms;destsuffix=edk2-platforms \
           git://github.com/tianocore/edk2-redfish-client.git;protocol=https;branch=main;name=redfishclient;destsuffix=edk2-redfish-client \
           file://NucRedfishPkg \
           file://bootsplash.bmp \
           "

# 0001-0018: the MrChromebox commits this board needs, cherry-picked onto
# upstream master. Each carries its original authorship and a
# "(cherry picked from commit ...)" trailer. They fall into two groups --
# coreboot/payload correctness (MTRR, root bridges from HOB, the framebuffer
# BAR offset, SMMSTORE block alignment, uninitialised memory in the entry
# point) and features this board is configured to use (CFR SetupMenu,
# PRIORITIZE_INTERNAL, the BGRT logo position).
#
# 0019-0035: local. All are applied unconditionally; what they add is
# gated by DSC defines that default FALSE, so the -D flags below decide what is
# actually built. Making the *patches* conditional instead would be fragile --
# 0021, 0022 and 0023 edit regions 0020 creates or sits beside.
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

# The one patch that applies to edk2-redfish-client rather than edk2. Numbered
# out of the way (0100) so the two series never look like one, and pointed at
# its own tree with patchdir -- do_patch defaults to ${S}, which is edk2.
SRC_URI += "file://0100-RedfishClientPkg-fit-the-client-to-a-Redfish-host-in.patch;patchdir=${EDK2_REDFISH_CLIENT_PATH}"

# All three pinned, not AUTOREV: a floating revision makes the build
# non-reproducible and silently changes what lands in the ROM. The patch series
# is generated against these exact trees, so `patch` refusing a hunk is the
# signal that a bump needs review.
# edk2 master head 2026-08-04.
SRCREV_edk2 = "fa41c179db1f9fc21eb425f44b85a16262c806ca"
# edk2-platforms head 2026-07-28.
SRCREV_platforms = "75efd079fed9723db8ce02365233c03b2fdc3b92"
# edk2-redfish-client head 2026-08-04. It tracks edk2 master, which is the
# whole reason this recipe does too.
SRCREV_redfishclient = "92fabf8572c226cf180c62b1204380385a518db3"
SRCREV_FORMAT = "edk2_platforms_redfishclient"

PV = "2605+git${SRCPV}"

S = "${WORKDIR}/git"
EDK2_PLATFORMS_PATH = "${WORKDIR}/edk2-platforms"
EDK2_REDFISH_CLIENT_PATH = "${WORKDIR}/edk2-redfish-client"

COMPATIBLE_MACHINE = "nuc5i7ryh"

# nasm: MdePkg/CryptoPkg X64 assembly. acpica: iasl for any .asl in the DXE
# set. util-linux: libuuid headers for BaseTools. coreboot's edk2 "checktools"
# target requires the same three, plus imagemagick -- see HOSTTOOLS below.
# openssl-native: generating/converting the capsule signing keypair below.
DEPENDS = "nasm-native acpica-native util-linux-native openssl-native"

# The bootsplash conversion runs ImageMagick's `convert` on the build host,
# exactly as coreboot's edk2 'logo' target does. There is no imagemagick-native
# in poky (it lives in meta-oe), and pulling in a whole layer for one optional
# BMP conversion is not worth it -- so borrow the host binary instead.
# NONFATAL because it is only needed when EDK2_BOOTSPLASH_FILE points at a
# non-BMP image; the layer default is already an uncompressed BMP3 installed
# verbatim, and do_configure fails loudly if a conversion is needed and
# `convert` is missing.
HOSTTOOLS_NONFATAL += "convert"
DEPENDS += "${@'ipxe-efi' if d.getVar('EDK2_IPXE') == '1' else ''}"
# DEPENDS alone only guarantees do_populate_sysroot; ipxe.rom is published by
# do_deploy, so do_configure has to wait for that task specifically.
do_configure[depends] += "${@'ipxe-efi:do_deploy' if d.getVar('EDK2_IPXE') == '1' else ''}"

# The payload is firmware, not target userspace; it embeds everything.
INHIBIT_DEFAULT_DEPS = "1"

# --- knobs mirroring coreboot's payloads/external/edk2/Kconfig ---------------
# Each maps onto a CONFIG_EDK2_* symbol so the two builds stay comparable.
#
# The default bootsplash is the layer's files/bootsplash.bmp (a board-branded
# logo LogoDxe draws per the BGRT spec -- FOLLOW_BGRT_SPEC=TRUE below), shipped
# pre-converted to the only format BmpSupportLib accepts: uncompressed 24-bit
# BMP3 (RLE-compressed BMPs are rejected at draw time, so `convert -compress
# none` alone is not enough -- keep `-type TrueColor`). Regenerate with:
#   magick -size 640x280 xc:black \
#     -fill none -stroke '#3d4451' -strokewidth 2 \
#     -draw "roundrectangle 8,8 631,271 18,18" \
#     -stroke '#00b3a4' -strokewidth 3 -draw "line 170,178 470,178" \
#     -stroke none -font DejaVu-Sans-ExtraLight -pointsize 74 -fill white \
#     -gravity north -annotate +0+62 "NUC5i7RYH" \
#     -font DejaVu-Sans -pointsize 22 -fill '#9aa4b2' \
#     -annotate +0+196 "coreboot  •  EDK2  •  pi-bmc" \
#     -type TrueColor BMP3:bootsplash.bmp
# Override with any image path (converted at configure time) or set to "" to
# fall back to the stock TianoCore logo. Size is a non-issue: the mostly-black
# BMP LZMA-compresses to ~7 KiB when coreboot packs the payload into CBFS.
EDK2_BOOTSPLASH_FILE ??= "${WORKDIR}/bootsplash.bmp"
EDK2_GOP_FILE ??= ""
# EDK2_IPXE builds the iPXE tree and embeds ipxe-intel.efidrv, the UNDI/SNP
# driver for the onboard NIC. That driver is the reason netboot works at all:
# nothing else in this payload publishes EFI_SIMPLE_NETWORK_PROTOCOL for the
# LOM, so without it a chainloaded iPXE snp.efi has no interface to boot from.
#
# EDK2_IPXE_APP additionally embeds the iPXE boot *application* and registers it
# as a boot option. Off, and unavailable: the FDF slot it used was a fork-only
# addition, and BDS already offers the LOM as "PXEv4 (MAC:...)" via the driver
# above, which is what actually chainloads.
EDK2_IPXE ??= "1"
# The Redfish host interface (DSP0270) over the BMC's CDC-ECM gadget.
EDK2_REDFISH ??= "1"
# edk2-redfish-client on top of it. Requires EDK2_REDFISH.
EDK2_REDFISH_CLIENT ??= "1"
EDK2_CUSTOM_BUILD_PARAMS ??= ""

# coreboot's nine-entry PACKAGES_PATH when CONFIG_EDK2_USE_EDK2_PLATFORMS=y,
# plus the Redfish client tree. Order matters: edk2 itself must come first so
# its MdePkg wins over any vendored copy in the other trees.
EDK2_PACKAGES_PATH = "${S}:${EDK2_PLATFORMS_PATH}/Platform/Intel:${EDK2_PLATFORMS_PATH}/Silicon/Intel:${EDK2_PLATFORMS_PATH}/Features/Intel:${EDK2_PLATFORMS_PATH}/Features/Intel/Debugging:${EDK2_PLATFORMS_PATH}/Features/Intel/Network:${EDK2_PLATFORMS_PATH}/Features/Intel/OutOfBandManagement:${EDK2_PLATFORMS_PATH}/Features/Intel/PowerManagement:${EDK2_PLATFORMS_PATH}/Features/Intel/SystemInformation:${EDK2_PLATFORMS_PATH}/Features/Intel/UserInterface:${EDK2_REDFISH_CLIENT_PATH}"

# --- EDK2 build defines -----------------------------------------------------
# Mirrors coreboot payloads/external/edk2/Makefile for this board's Kconfig.
# Broadwell-specific choices, each spelled out:
#   BUILD_ARCH=X64              not a feature switch: it is interpolated into
#                               OUTPUT_DIRECTORY (Build/UefiPayloadPkg$(...)),
#                               which do_compile and do_deploy both read.
#   CPU_TIMER_LIB_ENABLE=FALSE  Broadwell has no CPUID leaf 15h crystal clock;
#                               the TSC-from-CPUID timer lib (Skylake+) must
#                               stay off -- getting this wrong hangs the
#                               payload.
#   VARIABLE_SUPPORT=SMMSTORE   persistent EFI variables in the
#                               SMMSTORE(PRESERVE) 0x80000 FMAP region the
#                               board port lays out. Upstream carries the whole
#                               SmmStoreLib/SmmStoreFvb stack; only the block
#                               alignment and store-capability fixes are
#                               patched in (0009, 0013, 0014).
#   SERIAL off / CBMEM on       no UART is routed (NO_UART_ON_SUPERIO); read
#                               the firmware console with `cbmem -c`.
#   NETWORK_DRIVER_ENABLE=TRUE  the gate for the entire network stack: it is
#                               what makes UefiPayloadPkg.dsc pull in
#                               NetworkPkg/Network.dsc.inc, and therefore what
#                               the Redfish and PXE blocks hang off. Drop it and
#                               every other NETWORK_* flag below goes inert
#                               *silently* -- no HTTP, no REST EX, no host
#                               interface, and no build error.
#   NETWORK_PXE_BOOT_ENABLE     edk2's own PXE stack (UefiPxeBcDxe plus
#                               Mnp/Arp/Dhcp4/Mtftp4). Defaults TRUE inside
#                               NetworkPkg; spelled out because this board
#                               depends on it. iPXE is only the UNDI/SNP
#                               provider for the LOM (ipxe-intel.efidrv, built
#                               with EFI_DOWNGRADE_UX so it offers no boot
#                               method of its own), and edk2 owns the whole
#                               boot path above it.
#   NETWORK_HTTP_ENABLE=TRUE    RedfishRestExDxe rides HttpDxe. TLS stays off:
#                               the host interface is a point-to-point USB
#                               link, and OpenSSL would cost ~1 MB of FV.
# TPM_ENABLE is gone: upstream UefiPayloadPkg has no TPM support to disable
# (the knob and the whole Tcg stack were fork-only). The OS still gets the
# ME PTT fTPM via the board port's ACPI TPM2 table, which is how it worked
# anyway -- edk2's TCG stack only probes 0xfed40000 and this CRB is at the
# non-standard 0xfed70000.
#
# NUC_CAPSULE_GUID must be identical in FOUR places: coreboot's
# CONFIG_DRIVERS_EFI_MAIN_FW_GUID (payload-edk2.config), the -D
# CAPSULE_MAIN_FW_GUID below, the capsule coreboot_git.bb generates, and the
# ESRT entry those two Kconfigs produce. coreboot_git.bb defines the same
# default for the capsule-generation side; a mismatch there means a capsule
# that silently matches no FMP at runtime.
NUC_CAPSULE_GUID ??= "d25f89e1-94ec-4533-80b9-7f8855ce0a94"
EDK2_BUILD_FLAGS = " \
    -D BOOTLOADER=COREBOOT \
    -D BUILD_ARCH=X64 \
    -D BOOT_MANAGER_ESCAPE=TRUE \
    -D PLATFORM_BOOT_TIMEOUT=3 \
    -D CPU_TIMER_LIB_ENABLE=FALSE \
    -D SERIAL_DRIVER_ENABLE=FALSE \
    -D DISABLE_SERIAL_TERMINAL=TRUE \
    -D USE_CBMEM_FOR_CONSOLE=TRUE \
    -D VARIABLE_SUPPORT=SMMSTORE \
    -D CAPSULE_SUPPORT=TRUE \
    -D CAPSULE_MAIN_FW_GUID=${NUC_CAPSULE_GUID} \
    -D SECURE_BOOT_ENABLE=TRUE \
    -D SD_MMC_TIMEOUT=10000 \
    -D PS2_KEYBOARD_ENABLE=TRUE \
    -D SIO_BUS_ENABLE=TRUE \
    -D PRIORITIZE_INTERNAL=TRUE \
    -D FOLLOW_BGRT_SPEC=TRUE \
    -D TIMER_SUPPORT=LAPIC \
    -D LOAD_OPTION_ROMS=TRUE \
    -D NETWORK_DRIVER_ENABLE=TRUE \
    -D NETWORK_ENABLE=TRUE \
    -D NETWORK_SNP_ENABLE=TRUE \
    -D NETWORK_PXE_BOOT_ENABLE=TRUE \
    -D NETWORK_IP4_ENABLE=TRUE \
    -D NETWORK_IP6_ENABLE=FALSE \
    -D NETWORK_VLAN_ENABLE=FALSE \
    -D NETWORK_HTTP_ENABLE=TRUE \
    -D NETWORK_TLS_ENABLE=FALSE \
    -D NETWORK_HTTP_BOOT_ENABLE=FALSE \
    -D NETWORK_ISCSI_ENABLE=FALSE \
    -D NETWORK_ALLOW_HTTP_CONNECTIONS=TRUE \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdMaxVariableSize=0x8000 \
    --pcd gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseAddress=0xF0000000 \
    --pcd gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseSize=0x4000000 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow=0 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn=0 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow=0 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn=0 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemId=COREv4 \
    --pcd gUefiCpuPkgTokenSpaceGuid.PcdFirstTimeWakeUpAPsBySipi=FALSE \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion=0x0300 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosDocRev=0x0 \
    "

python () {
    # NETWORK_IPXE_UNDI embeds ipxe-intel.efidrv, the UNDI/SNP driver for the
    # onboard NIC.
    if d.getVar('EDK2_IPXE') == '1':
        d.appendVar('EDK2_BUILD_FLAGS', ' -D NETWORK_IPXE_UNDI=TRUE')
    if d.getVar('EDK2_BOOTSPLASH_FILE'):
        d.appendVar('EDK2_BUILD_FLAGS', ' -D BOOTSPLASH_IMAGE=TRUE')
    if d.getVar('EDK2_GOP_FILE'):
        d.appendVar('EDK2_BUILD_FLAGS', ' -D USE_PLATFORM_GOP=TRUE')
    if d.getVar('EDK2_REDFISH') == '1':
        d.appendVar('EDK2_BUILD_FLAGS', ' -D REDFISH_ENABLE=TRUE')
        if d.getVar('EDK2_REDFISH_CLIENT') == '1':
            d.appendVar('EDK2_BUILD_FLAGS', ' -D REDFISH_CLIENT=TRUE')
    elif d.getVar('EDK2_REDFISH_CLIENT') == '1':
        bb.fatal("EDK2_REDFISH_CLIENT needs EDK2_REDFISH: RedfishClientPkg is "
                 "the feature layer above the host interface, and its DSC/FDF "
                 "block is nested inside the one REDFISH_ENABLE gates")
}

# --- capsule signing identity ------------------------------------------
# UefiPayloadPkg.dsc's FmpDxe component embeds
# PcdFmpDevicePkcs7CertBufferXdr from BaseTools/Source/Python/Pkcs7Sign/
# TestRoot.cer by default -- EDK2's own published test certificate chain.
# Its private keys ship in the same tree, so ANY capsule signed against it
# validates on every board that ever built this payload. do_configure below
# always replaces that PCD with a real certificate before compiling: either
# an operator-supplied identity (NUC_CAPSULE_CERT/NUC_CAPSULE_KEY) or a
# generated keypair, and refuses to build at all if either variable somehow
# still resolves into the Pkcs7Sign/Test tree.
#
# coreboot_git.bb's do_deploy signs the actual capsule against this same
# identity once the finished ROM exists (which it does not, here -- this
# recipe only produces one of the ROM's inputs). Both recipes default
# NUC_CAPSULE_KEYDIR identically, so a from-scratch build needs nothing
# threaded between them: whichever runs do_configure first generates the
# keypair, and the other one just finds it already there.
NUC_CAPSULE_KEYDIR ??= "${TOPDIR}/nuc-capsule-keys"
NUC_CAPSULE_CERT ??= ""
NUC_CAPSULE_KEY ??= ""
NUC_CAPSULE_SUBJECT ??= "/CN=pi-bmc NUC5i7RYH UEFI capsule signing/"
NUC_CAPSULE_CERT_DAYS ??= "7300"

# Shell locals go unbraced throughout these functions, matching the rpi5
# platform's rpi5_fmp_resolve_keys/rpi5_fmp_generate_keys (see
# rpi5-uefi-firmware.bb): bitbake expands ${...} against its own datastore
# before /bin/sh ever sees it, so a braced shell variable is one name
# collision away from being replaced with something else entirely.
nuc_capsule_reject_test_cert() {
    label="$1"; path="$2"
    resolved=$(readlink -f "$path" 2>/dev/null || echo "$path")
    case "$resolved" in
        */Pkcs7Sign/Test*)
            bbfatal "$label '$path' resolves to '$resolved' -- EDK2's own published test certificate chain (BaseTools/Source/Python/Pkcs7Sign). Its private keys ship in every edk2 checkout, so a capsule signed against it validates on any board running this firmware. Configure NUC_CAPSULE_CERT/NUC_CAPSULE_KEY with a real identity, or leave both unset to use the keypair generated under NUC_CAPSULE_KEYDIR."
            ;;
    esac
}

# Deliberately all-or-nothing: a directory holding some of the four files is
# an error rather than something to top up -- the four are one identity, and
# quietly re-deriving a missing piece is how the certificate baked into the
# firmware ends up not matching the key that signs capsules for it.
nuc_capsule_generate_keys() {
    fmp_keydir="${NUC_CAPSULE_KEYDIR}"

    fmp_present=""
    fmp_missing=""
    for f in capsule.key capsule.crt capsule.cer capsule.pem; do
        if [ -e "$fmp_keydir/$f" ]; then
            fmp_present="$fmp_present $f"
        else
            fmp_missing="$fmp_missing $f"
        fi
    done

    if [ -z "$fmp_missing" ]; then
        return 0
    fi

    if [ -n "$fmp_present" ]; then
        bbfatal "$fmp_keydir is a partial capsule signing key directory (has:$fmp_present, missing:$fmp_missing). Refusing to regenerate: the certificate and the key that signs capsules for it must stay one pair, and boards already carrying this certificate would reject capsules signed by a new one. Restore the missing files, or move the directory aside to start a new identity -- and reflash every board that has the old one."
    fi

    mkdir -p "$fmp_keydir"

    # umask, not a later chmod: the private key must never exist, even for
    # an instant, at a mode another user on the build host could read.
    (umask 077 && openssl req -x509 -newkey rsa:2048 -nodes -sha256 \
        -days ${NUC_CAPSULE_CERT_DAYS} -subj "${NUC_CAPSULE_SUBJECT}" \
        -keyout "$fmp_keydir/capsule.key" -out "$fmp_keydir/capsule.crt") \
        || bbfatal "could not generate a capsule signing keypair in $fmp_keydir"

    openssl x509 -in "$fmp_keydir/capsule.crt" -outform DER \
        -out "$fmp_keydir/capsule.cer" \
        || bbfatal "could not convert $fmp_keydir/capsule.crt to DER"
    (umask 077 && cat "$fmp_keydir/capsule.key" "$fmp_keydir/capsule.crt" \
        > "$fmp_keydir/capsule.pem") \
        || bbfatal "could not write $fmp_keydir/capsule.pem"
    chmod 0644 "$fmp_keydir/capsule.crt" "$fmp_keydir/capsule.cer"

    bbwarn "Generated a self-signed capsule signing keypair in $fmp_keydir. This is now the identity every board flashed with this firmware will trust for updates, and it is stored unencrypted in the build directory: back it up, and replace it with managed key material (NUC_CAPSULE_CERT + NUC_CAPSULE_KEY) before shipping. Losing it means no board flashed with this firmware can ever be capsule-updated again."
}

# Resolve the capsule certificate (and, where available, its signer) into
# $fmp_cert / $fmp_signer for the caller, generating a keypair if neither
# NUC_CAPSULE_CERT nor NUC_CAPSULE_KEY was configured, and refusing to
# proceed if the resolved identity is EDK2's own test certificate chain --
# it always yields a real certificate, or it stops the build.
nuc_capsule_resolve_keys() {
    fmp_cert="${NUC_CAPSULE_CERT}"
    fmp_signer="${NUC_CAPSULE_KEY}"

    if [ -z "$fmp_cert" ] && [ -z "$fmp_signer" ]; then
        nuc_capsule_generate_keys
        fmp_cert="${NUC_CAPSULE_KEYDIR}/capsule.cer"
        fmp_signer="${NUC_CAPSULE_KEYDIR}/capsule.pem"
    fi

    if [ -n "$fmp_signer" ] && [ ! -r "$fmp_signer" ]; then
        bbfatal "NUC_CAPSULE_KEY '$fmp_signer' is not readable."
    fi

    if [ -z "$fmp_cert" ]; then
        mkdir -p "${B}"
        fmp_cert="${B}/nuc-capsule-cert.der"
        openssl x509 -in "$fmp_signer" -outform DER -out "$fmp_cert" \
            || bbfatal "NUC_CAPSULE_KEY '$fmp_signer' holds no certificate. GenerateCapsule signs by running openssl smime -sign -signer <file> and passes no -inkey, so this file must contain the signing certificate as well as the private key -- concatenate them, key first."
    fi

    if [ ! -r "$fmp_cert" ]; then
        bbfatal "NUC_CAPSULE_CERT '$fmp_cert' is not readable."
    fi

    nuc_capsule_reject_test_cert "NUC_CAPSULE_CERT" "$fmp_cert"
    if [ -n "$fmp_signer" ]; then
        nuc_capsule_reject_test_cert "NUC_CAPSULE_KEY" "$fmp_signer"
    fi
}

do_configure() {
    # --- stage NucRedfishPkg -------------------------------------------------
    # The whole point of building the payload in its own recipe: these are
    # ordinary layer files copied into the tree, not surgery on a clone that
    # only exists partway through someone else's do_compile.
    rm -rf ${S}/NucRedfishPkg
    cp -a ${WORKDIR}/NucRedfishPkg ${S}/NucRedfishPkg

    # --- firmware GUID drift guard -------------------------------------------
    # NUC_CAPSULE_GUID is hand-written into six places and cannot be factored
    # out: FDF INF statements do not expand $(...) macros, so patch 0035's
    # `INF FILE_GUID = ...` line has to spell it. Drift can still be caught.
    # A wrong FDF GUID fails loudly at GenFv, but a wrong GUID in either C
    # source fails SILENTLY -- the scanner and the Redfish inventory would look
    # up an FMP instance that does not exist and read "could not find the FMP"
    # as a verdict on a capsule that was in fact applied. Compare the staged
    # sources and the patched FDF against the one variable the build uses,
    # here, where all three are on disk and it is in scope. (coreboot_git.bb
    # guards its own two copies the same way, against the finished payload.)
    python3 - "${NUC_CAPSULE_GUID}" \
        "${S}/NucRedfishPkg/Library/NucCapsuleOnDiskLib/NucCapsuleOnDiskLib.c" \
        "${S}/NucRedfishPkg/NucRedfishSyncDxe/NucRedfishInventory.c" \
        "${S}/UefiPayloadPkg/UefiPayloadPkg.fdf" <<'GUIDCHECK'
import sys, uuid
g = uuid.UUID(sys.argv[1])
c = ("0x%08x,0x%04x,0x%04x,{%s}" % (g.fields[0], g.fields[1], g.fields[2],
     ",".join("0x%02x" % b for b in g.bytes[8:])))
bad = []
for path in sys.argv[2:]:
    text = "".join(open(path, "rb").read().decode("utf-8", "replace").lower().split())
    needle = (c if path.endswith(".c") else str(g)).replace(" ", "")
    if needle not in text:
        bad.append("%s does not carry %s" % (path, needle))
if bad:
    sys.exit("NUC_CAPSULE_GUID drift: " + "; ".join(bad) + ".\n"
             "Every copy of the firmware image GUID must hold the same value: "
             "NUC_CAPSULE_GUID here and in coreboot_git.bb, "
             "CONFIG_DRIVERS_EFI_MAIN_FW_GUID in payload-edk2.config, the "
             "INF FILE_GUID line in "
             "0035-UefiPayloadPkg-make-FmpDxe-FV-resident.patch, and the two "
             "C literals above.")
GUIDCHECK

    # --- bootsplash (coreboot's 'logo' target) -------------------------------
    # A .bmp is installed verbatim: the layer default is already the
    # uncompressed BMP3 LogoDxe needs, so the default build has no ImageMagick
    # dependency. Anything else goes through `convert`, exactly as coreboot's
    # edk2 'logo' target does.
    if [ -n "${EDK2_BOOTSPLASH_FILE}" ]; then
        case "${EDK2_BOOTSPLASH_FILE}" in
        *.bmp|*.BMP)
            install -m 0644 "${EDK2_BOOTSPLASH_FILE}" \
                ${S}/MdeModulePkg/Logo/Logo.bmp
            ;;
        *)
            command -v convert >/dev/null 2>&1 || \
                bbfatal "EDK2_BOOTSPLASH_FILE is set to a non-BMP image but ImageMagick's 'convert' is not on the build host -- install imagemagick (coreboot's edk2 checktools requires it for the same reason)"
            convert -background None "${EDK2_BOOTSPLASH_FILE}" \
                BMP3:${S}/MdeModulePkg/Logo/Logo.bmp
            ;;
        esac
    fi

    # --- GOP driver + VBT (coreboot's 'gop_driver' target) -------------------
    if [ -n "${EDK2_GOP_FILE}" ]; then
        install -m 0644 "${EDK2_GOP_FILE}" ${S}/UefiPayloadPkg/IntelGopDriver.efi
        install -m 0644 "${COREBOOT_VBT_FILE}" ${S}/UefiPayloadPkg/vbt.bin
    fi

    # --- iPXE ----------------------------------------------------------------
    # The UNDI/SNP driver for the onboard NIC, dispatched from the DXE FV (see
    # patch 0021). Without it nothing in this payload publishes
    # EFI_SIMPLE_NETWORK_PROTOCOL for the LOM -- edk2 has no driver for this
    # NIC at all, and SnpDxe only layers SNP over an existing UNDI/NII
    # instance. A chainloaded iPXE snp.efi binds SNP handles and nothing else,
    # so with only the BMC's gadget publishing one it had no LOM to boot from.
    #
    # Upstream UefiPayloadPkg has no NetworkDrivers directory of its own; the
    # prebuilt Realtek and ASIX UNDI blobs the fork shipped there were inert on
    # this board and are simply gone.
    if [ "${EDK2_IPXE}" = "1" ]; then
        install -d ${S}/UefiPayloadPkg/NetworkDrivers
        install -m 0644 ${DEPLOY_DIR_IMAGE}/ipxe-intel.efidrv \
            ${S}/UefiPayloadPkg/NetworkDrivers/ipxe-intel.efidrv
    fi

    # --- capsule signing certificate ------------------------------------
    # nuc_capsule_resolve_keys always yields a real certificate (generating
    # one under NUC_CAPSULE_KEYDIR if the operator configured neither
    # NUC_CAPSULE_CERT nor NUC_CAPSULE_KEY) or stops the build -- see the
    # block comment above do_configure. BinToPcd renders it as the
    # XDR-encoded PCD FmpDevicePkg expects; --pcd cannot do this, it is a
    # multi-hundred-byte binary blob.
    nuc_capsule_resolve_keys

    cert_pcd="${B}/nuc-fmp-cert.pcd"
    python3 "${S}/BaseTools/Scripts/BinToPcd.py" \
        -i "$fmp_cert" -x -o "${cert_pcd}" \
        -p gFmpDevicePkgTokenSpaceGuid.PcdFmpDevicePkcs7CertBufferXdr

    # Replace the module-scoped !include inside FmpDxe's <PcdsFixedAtBuild>
    # block (UefiPayloadPkg.dsc) in place, rather than appending a
    # platform-wide override at end of file: a module-scoped PCD override
    # wins over a platform [PcdsFixedAtBuild.common] one, so appending would
    # silently leave the test certificate in effect.
    #
    # This used to be a two-command sed (`r` the rendered PCD in, then `d`
    # the !include line) and it corrupted the DSC: BinToPcd's output file
    # carries no trailing newline, so sed's `r` spliced the very next line
    # -- the block's `<LibraryClasses>` header -- onto the end of the
    # inserted PCD text instead of leaving it on its own line. The DSC
    # parser, still inside <PcdsFixedAtBuild> with no section header in
    # sight, read the next real line (`FmpDeviceLib|...`) as a malformed
    # PCD assignment and failed with "No token space GUID or PCD name
    # specified" -- no C code involved, nothing compiled. Doing this in
    # Python on the raw bytes instead makes the substitution touch only the
    # one matched line's own text, and leaves every surrounding \r\n
    # (the DSC's line ending throughout) exactly as it was.
    #
    # The structural check below is unconditional, not just gated on the
    # substitution having just run: a tree already left corrupted by an
    # earlier, buggy version of this same step (test_include already gone,
    # so the substitution silently has nothing to do) must still be caught
    # here, not passed through to GenFv. A silent structural edit to a
    # generated file is exactly how the corruption above got through once
    # already.
    dsc="${S}/UefiPayloadPkg/UefiPayloadPkg.dsc"
    python3 - "$dsc" "$cert_pcd" <<'PY'
import sys

TEST_INCLUDE = b'!include BaseTools/Source/Python/Pkcs7Sign/TestRoot.cer.gFmpDevicePkgTokenSpaceGuid.PcdFmpDevicePkcs7CertBufferXdr.inc'
COMPONENT_START = b'FmpDevicePkg/FmpDxe/FmpDxe.inf {'
LIB_CLASSES = b'<LibraryClasses>'
FMP_DEVICE_LIB = b'FmpDeviceLib|UefiPayloadPkg/Library/FmpDeviceSmmLib/FmpDeviceSmmLib.inf'
PCD_MARKER = b'PcdFmpDevicePkcs7CertBufferXdr|{0x'

def fail(msg):
    sys.stderr.write(msg + "\n")
    sys.exit(1)

dsc_path, cert_pcd_path = sys.argv[1], sys.argv[2]

with open(dsc_path, 'rb') as f:
    raw = f.read()

if b'\r\n' not in raw:
    fail("{}: no CRLF line endings found -- refusing to edit a file whose convention I cannot confirm".format(dsc_path))

lines = raw.split(b'\r\n')

with open(cert_pcd_path, 'rb') as f:
    pcd_line = f.read().strip(b'\r\n')
if b'\n' in pcd_line or b'\r' in pcd_line:
    fail("{}: contains an embedded line break -- BinToPcd's output format changed; this replacement assumes exactly one physical line".format(cert_pcd_path))

# Strict single-line substitution: replace ONLY the line whose stripped
# content is the test-cert !include, preserving its own indentation and
# its neighbours' \r\n untouched either side.
hits = [i for i, l in enumerate(lines) if l.strip() == TEST_INCLUDE]
if len(hits) > 1:
    fail("expected at most one Pkcs7Sign/TestRoot !include line in {}, found {}".format(dsc_path, len(hits)))
if len(hits) == 1:
    idx = hits[0]
    indent = lines[idx][:len(lines[idx]) - len(lines[idx].lstrip())]
    lines[idx] = indent + pcd_line
    raw = b'\r\n'.join(lines)
    with open(dsc_path, 'wb') as f:
        f.write(raw)

# --- structural verification -------------------------------------------
lines = raw.split(b'\r\n')
starts = [i for i, l in enumerate(lines) if COMPONENT_START in l]
if len(starts) != 1:
    fail("expected exactly one '{}' component line, found {}".format(COMPONENT_START.decode(), len(starts)))
start = starts[0]
end = None
for i in range(start + 1, len(lines)):
    if lines[i].strip() == b'}':
        end = i
        break
if end is None:
    fail("could not find the closing '}' for the FmpDxe component block")

block = lines[start:end]

lib_hits = [i for i, l in enumerate(block) if l.strip() == LIB_CLASSES]
if len(lib_hits) != 1:
    fail("expected exactly one <LibraryClasses> line inside the FmpDxe component block, found {} -- the certificate swap may have swallowed or duplicated it".format(len(lib_hits)))

fmp_hits = [i for i, l in enumerate(block) if FMP_DEVICE_LIB in l]
if len(fmp_hits) != 1:
    fail("expected exactly one FmpDeviceSmmLib FmpDeviceLib assignment inside the FmpDxe component block, found {}".format(len(fmp_hits)))

pcd_hits = [i for i, l in enumerate(block) if PCD_MARKER in l]
if len(pcd_hits) != 1:
    fail("expected exactly one embedded PcdFmpDevicePkcs7CertBufferXdr assignment inside the FmpDxe component block, found {} -- FmpDxe has no key to authenticate against and no capsule could ever be applied".format(len(pcd_hits)))

if not (pcd_hits[0] < lib_hits[0] < fmp_hits[0]):
    fail("FmpDxe component block is out of order: expected cert PCD, then <LibraryClasses>, then FmpDeviceLib (got PCD@{}, LibraryClasses@{}, FmpDeviceLib@{})".format(pcd_hits[0], lib_hits[0], fmp_hits[0]))

print("UefiPayloadPkg.dsc FmpDxe block OK: cert PCD, <LibraryClasses> and FmpDeviceLib present, exactly once each, in order")
PY
}

do_compile() {
    cd ${S}

    # BaseTools are build-host tools; bitbake's exported cross CC must not leak
    # in (coreboot's Makefile does the same 'unset CC' dance). The fallback
    # 'cc' is not in bitbake's HOSTTOOLS, so name gcc/g++ explicitly
    # (command-line vars also beat any CC= inside the makefiles).
    unset CC CXX CPP AS AR LD RANLIB STRIP OBJCOPY CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
    oe_runmake -C BaseTools CC=gcc CXX=g++

    # What edksetup.sh does, without needing to source bash into this task:
    # workspace env + the Conf/*.txt copied from the BaseTools templates.
    export WORKSPACE="${S}"
    export PACKAGES_PATH="${EDK2_PACKAGES_PATH}"
    export EDK_TOOLS_PATH="${S}/BaseTools"
    export CONF_PATH="${S}/Conf"
    export PYTHON_COMMAND="python3"
    export PATH="${S}/BaseTools/BinWrappers/PosixLike:$PATH"
    mkdir -p ${S}/Conf
    for t in build_rule tools_def target; do
        [ -e "${S}/Conf/$t.txt" ] || cp "${S}/BaseTools/Conf/$t.template" "${S}/Conf/$t.txt"
    done

    # Same invocation as coreboot's UefiPayloadPkg target: the -t GCC toolchain
    # resolves plain 'gcc' from PATH (the build host compiler).
    build -a IA32 -a X64 -b RELEASE -t GCC \
        -p UefiPayloadPkg/UefiPayloadPkg.dsc \
        -n ${@oe.utils.cpu_count()} \
        ${EDK2_BUILD_FLAGS} ${EDK2_CUSTOM_BUILD_PARAMS} \
        -y ${B}/UEFIPAYLOAD.report.txt

    [ -f ${S}/Build/UefiPayloadPkgX64/RELEASE_GCC/FV/UEFIPAYLOAD.fd ] || \
        bbfatal "edk2 build produced no UEFIPAYLOAD.fd -- see ${B}/UEFIPAYLOAD.report.txt"
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${S}/Build/UefiPayloadPkgX64/RELEASE_GCC/FV/UEFIPAYLOAD.fd \
        ${DEPLOYDIR}/UEFIPAYLOAD.fd
    install -m 0644 ${B}/UEFIPAYLOAD.report.txt ${DEPLOYDIR}/UEFIPAYLOAD.report.txt

    # --- capsule tooling, for coreboot_git.bb's do_deploy -----------------
    # AppendRmapManifest.py and GenerateCapsule.py run against the FINISHED
    # ROM, which does not exist here -- this recipe only produces one of its
    # inputs (UEFIPAYLOAD.fd). coreboot_git.bb's do_deploy is where
    # coreboot.rom is actually assembled, so that is where the capsule gets
    # built; it needs these tools to do it.
    #
    # GenerateCapsule.py is not self-contained: it imports sibling packages
    # under BaseTools/Source/Python (Common.Uefi.Capsule.*, Common.Edk2.
    # Capsule.*), so the whole tree travels, not just the one script.
    # Carried through DEPLOYDIR -- the only cross-recipe sharing convention
    # this layer uses (see nuc-coreboot-rom.bb) -- rather than coreboot_git.bb
    # reaching into this recipe's private WORKDIR, which is not guaranteed
    # to still exist by the time coreboot's do_deploy runs (e.g. under
    # rm_work) and which no other recipe in this layer does.
    install -d ${DEPLOYDIR}/edk2-capsule-tools
    rm -rf ${DEPLOYDIR}/edk2-capsule-tools/BaseTools-Source-Python
    cp -a ${S}/BaseTools/Source/Python \
        ${DEPLOYDIR}/edk2-capsule-tools/BaseTools-Source-Python
    install -m 0755 ${S}/UefiPayloadPkg/Tools/AppendRmapManifest.py \
        ${DEPLOYDIR}/edk2-capsule-tools/AppendRmapManifest.py
}

addtask deploy after do_compile

do_install[noexec] = "1"
