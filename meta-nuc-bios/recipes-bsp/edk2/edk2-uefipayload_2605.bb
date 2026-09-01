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
DEPENDS = "nasm-native acpica-native util-linux-native"

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
    -D CAPSULE_MAIN_FW_GUID=d25f89e1-94ec-4533-80b9-7f8855ce0a94 \
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

do_configure() {
    # --- stage NucRedfishPkg -------------------------------------------------
    # The whole point of building the payload in its own recipe: these are
    # ordinary layer files copied into the tree, not surgery on a clone that
    # only exists partway through someone else's do_compile.
    rm -rf ${S}/NucRedfishPkg
    cp -a ${WORKDIR}/NucRedfishPkg ${S}/NucRedfishPkg

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
}

addtask deploy after do_compile

do_install[noexec] = "1"
