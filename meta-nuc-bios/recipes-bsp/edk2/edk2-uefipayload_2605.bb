SUMMARY = "EDK2 UefiPayloadPkg (MrChromebox fork) — UEFI payload for coreboot"
DESCRIPTION = "Builds UEFIPAYLOAD.fd, the UEFI environment coreboot jumps \
into on the NUC5i7RYH, with the Redfish host-interface client stack compiled \
in. Uses the MrChromebox edk2 fork -- the exact tree coreboot's own \
payloads/external/edk2 machinery defaults to (EDK2_REPO_MRCHROMEBOX, branch \
uefipayload_2605): unlike upstream tianocore it carries the coreboot \
integration patches that matter here -- the CFR-driven SetupMenu (surfaces \
the board port's fan-profile / power-on-after-AC / SATA / fTPM options), the \
SMMSTORE variable driver wired as the EFI variable store, and the cbmem \
console. \
\
Building the payload here rather than inside coreboot's payloads/external \
machinery is what makes NucRedfishPkg possible: its sources are staged into \
the tree by do_configure, instead of having to patch a tree that coreboot \
clones halfway through its own do_compile."
HOMEPAGE = "https://github.com/mrchromebox/edk2"
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"

inherit deploy

# Three source trees, all placed on PACKAGES_PATH:
#
#   edk2 (gitsm)          the payload itself. edk2 vendors its deps as
#                         submodules (openssl for Secure Boot, brotli,
#                         oniguruma, ...) -- same fetcher approach as
#                         oe-core's ovmf.
#   edk2-platforms        Features/Intel/**; coreboot puts nine of its
#                         subdirectories on PACKAGES_PATH (mirrored below).
# edk2-redfish-client (RedfishClientPkg) is intentionally absent.
#
# It is the BIOS *attribute-sync* feature layer -- BiosDxe, BootOptionDxe and
# the JSON converters -- which sits on top of a working host interface. It is
# not needed for initial sync (discovery + RestEx + the type 42 record), and
# neither NucRedfishHostInterfaceLib nor RedfishConfigDriver depends on it:
# both list only RedfishPkg in their [Packages].
#
# It also does not build against this tree. The MrChromebox fork tracks
# UefiPayloadPkg, not RedfishPkg, so its RedfishPkg lags upstream edk2 badly
# regardless of the fork's HEAD date (2026-07-12):
#
#   RedfishClientPkg/Library/RedfishEventLib/RedfishEventLib.inf(39):
#     error 4000: Value of Guid [gEdkIIRedfisEventRedfishInterfaceDisconnectionGuid]
#     is not found under [Guids] section
#
# That GUID is declared by upstream RedfishPkg.dec; this fork's copy has no
# RedfishEvent GUIDs at all. Re-adding the client layer therefore needs a
# matched pair -- either a newer RedfishPkg (rebase the fork, or overlay
# upstream RedfishPkg onto it) or an edk2-redfish-client revision old enough to
# match. Pick the pair deliberately rather than by AUTOREV.
SRC_URI = "gitsm://github.com/mrchromebox/edk2.git;protocol=https;branch=uefipayload_2605;name=edk2;destsuffix=git \
           git://github.com/tianocore/edk2-platforms.git;protocol=https;branch=master;name=platforms;destsuffix=edk2-platforms \
           file://NucRedfishPkg \
           file://bootsplash.bmp \
           "

# The Redfish wiring is two ordinary patches rather than the scripted anchored
# insertions this recipe used to run at do_configure. Both trees are pinned by
# SRCREV, so context diffs are stable, and `patch` already refuses to apply a
# hunk it cannot place -- which was the one property the script was written for.
# NucRedfishPkg itself is still staged by do_configure: it is a whole package
# copied into the tree, not a modification of one.
SRC_URI += "${@' '.join([ \
    'file://0001-UefiPayloadPkg-wire-in-the-Redfish-host-interface-sta.patch', \
    'file://0002-UsbNetwork-assume-media-on-a-point-to-point-gadget.patch', \
    ]) if d.getVar('EDK2_REDFISH') == '1' else ''}"

# Branch head as of 2026-07-13. coreboot master defaults to this branch
# (payloads/external/edk2/Kconfig: EDK2_TAG_OR_REV "origin/uefipayload_2605").
SRCREV_edk2 = "2939f4969466bfe71722494e4cea5cbaa029c709"
# Pinned, not AUTOREV: a floating revision makes the build non-reproducible
# and silently changes what lands in the ROM. 2026-07-28 head.
SRCREV_platforms = "75efd079fed9723db8ce02365233c03b2fdc3b92"
SRCREV_FORMAT = "edk2_platforms"

PV = "2605+git${SRCPV}"

S = "${WORKDIR}/git"
EDK2_PLATFORMS_PATH = "${WORKDIR}/edk2-platforms"

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
EDK2_IPXE ??= "1"
EDK2_IPXE_OPTION_NAME ??= "iPXE"
EDK2_REDFISH ??= "1"
EDK2_CUSTOM_BUILD_PARAMS ??= ""

# coreboot's nine-entry PACKAGES_PATH when CONFIG_EDK2_USE_EDK2_PLATFORMS=y,
# plus the Redfish client tree. Order matters: edk2 itself must come first so
# its MdePkg wins over any vendored copy in the other trees.
EDK2_PACKAGES_PATH = "${S}:${EDK2_PLATFORMS_PATH}/Platform/Intel:${EDK2_PLATFORMS_PATH}/Silicon/Intel:${EDK2_PLATFORMS_PATH}/Features/Intel:${EDK2_PLATFORMS_PATH}/Features/Intel/Debugging:${EDK2_PLATFORMS_PATH}/Features/Intel/Network:${EDK2_PLATFORMS_PATH}/Features/Intel/OutOfBandManagement:${EDK2_PLATFORMS_PATH}/Features/Intel/PowerManagement:${EDK2_PLATFORMS_PATH}/Features/Intel/SystemInformation:${EDK2_PLATFORMS_PATH}/Features/Intel/UserInterface"

# --- EDK2 build defines -----------------------------------------------------
# Mirrors coreboot payloads/external/edk2/Makefile for this board's Kconfig.
# Broadwell-specific choices, each spelled out:
#   CPU_TIMER_LIB_ENABLE=FALSE  Broadwell has no CPUID leaf 15h crystal clock;
#                               the TSC-from-CPUID timer lib (Skylake+) must
#                               stay off -- getting this wrong hangs the
#                               payload.
#   VARIABLE_SUPPORT=SMMSTORE   persistent EFI variables in the
#                               SMMSTORE(PRESERVE) 0x80000 FMAP region the
#                               board port lays out.
#   TPM_ENABLE=FALSE            the ME PTT fTPM's CRB is at the non-standard
#                               0xfed70000; edk2's TCG stack only probes
#                               0xfed40000 and would find nothing. The OS gets
#                               the TPM via the board port's ACPI TPM2 table.
#   SERIAL off / CBMEM on       no UART is routed (NO_UART_ON_SUPERIO); read
#                               the firmware console with `cbmem -c`.
#   NETWORK_HTTP_ENABLE=TRUE    RedfishRestExDxe rides HttpDxe. TLS stays off:
#                               the host interface is a point-to-point USB
#                               link, and OpenSSL would cost ~1 MB of FV.
# Fork-only options (coreboot gates these behind !CONFIG_EDK2_REPO_OFFICIAL):
#   PRIORITIZE_INTERNAL, FOLLOW_BGRT_SPEC, TIMER_SUPPORT, LOAD_OPTION_ROMS,
#   NETWORK_IPXE, USE_PLATFORM_GOP. They are only valid because this is the
#   MrChromebox fork -- passing them to an upstream tianocore tree errors out.
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
    -D SECURE_BOOT_ENABLE=TRUE \
    -D TPM_ENABLE=FALSE \
    -D SD_MMC_TIMEOUT=10000 \
    -D PS2_KEYBOARD_ENABLE=TRUE \
    -D SIO_BUS_ENABLE=TRUE \
    -D PRIORITIZE_INTERNAL=TRUE \
    -D FOLLOW_BGRT_SPEC=TRUE \
    -D TIMER_SUPPORT=LAPIC \
    -D LOAD_OPTION_ROMS=TRUE \
    -D NETWORK_ENABLE=TRUE \
    -D NETWORK_SNP_ENABLE=TRUE \
    -D NETWORK_IP4_ENABLE=TRUE \
    -D NETWORK_IP6_ENABLE=FALSE \
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
    if d.getVar('EDK2_IPXE') == '1':
        d.appendVar('EDK2_BUILD_FLAGS', ' -D NETWORK_IPXE=TRUE')
        # Deliberately a single word: coreboot emits the same PCD and Kconfig
        # keeps the quotes on string values, so a value containing spaces
        # reaches edk2's build.py mangled -- the DSC parser rejects it with
        # "error 3000: Syntax error".
        d.appendVar('EDK2_BUILD_FLAGS',
                    ' --pcd gUefiPayloadPkgTokenSpaceGuid.PcdiPXEOptionName=L"%s"'
                    % d.getVar('EDK2_IPXE_OPTION_NAME'))
    if d.getVar('EDK2_BOOTSPLASH_FILE'):
        d.appendVar('EDK2_BUILD_FLAGS', ' -D BOOTSPLASH_IMAGE=TRUE')
    if d.getVar('EDK2_GOP_FILE'):
        d.appendVar('EDK2_BUILD_FLAGS', ' -D USE_PLATFORM_GOP=TRUE')
    if d.getVar('EDK2_REDFISH') == '1':
        d.appendVar('EDK2_BUILD_FLAGS', ' -D REDFISH_ENABLE=TRUE')
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

    # --- iPXE (coreboot's 'ipxe_rom' target) ---------------------------------
    # Byte-for-byte the same step:
    #
    #   cp $(top)/payloads/external/iPXE/ipxe/ipxe.rom \
    #      $(EDK2_PATH)/UefiPayloadPkg/NetworkDrivers/ipxe.efi
    #
    # The rename is coreboot's, not ours: ipxe.rom is the generic name its iPXE
    # Makefile gives whichever target it built, and with CONFIG_IPXE_BUILD_EFI
    # that target is bin-x86_64-efi-sb/ipxe.efi -- a PE, which is what
    # UefiPayloadPkg.fdf expects at NetworkDrivers/ipxe.efi.
    if [ "${EDK2_IPXE}" = "1" ]; then
        install -d ${S}/UefiPayloadPkg/NetworkDrivers
        install -m 0644 ${DEPLOY_DIR_IMAGE}/ipxe.rom \
            ${S}/UefiPayloadPkg/NetworkDrivers/ipxe.efi
    fi

    # Redfish wiring itself is applied by do_patch -- see the SRC_URI patches.
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
