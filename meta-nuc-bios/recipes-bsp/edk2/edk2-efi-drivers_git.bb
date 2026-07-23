SUMMARY = "Standalone EDK2 UEFI drivers (.efi) for the NUC's stock BIOS (Path B)"
DESCRIPTION = "Builds individual UEFI DXE drivers as standalone .efi files, to be \
staged on a USB/FAT volume and loaded on the stock (locked) NUC BIOS via a \
Driver#### load option (efibootmgr --driver) -- NOT flashed into firmware. \
The default set is the EDK2 USB-network stack: NetworkCommon (produces \
EFI_SIMPLE_NETWORK_PROTOCOL) plus the CDC-ECM / RNDIS / NCM transport bindings, \
giving the NUC's UEFI a network link to the BMC over a USB Ethernet gadget. \
Reuses the same MrChromebox edk2 tree the payload recipe fetches (its \
MdeModulePkg lists these drivers as build components), so no second clone. \
Add your own driver INFs via EFI_DRIVER_INFS_append."
HOMEPAGE = "https://github.com/tianocore/edk2/tree/master/MdeModulePkg/Bus/Usb/UsbNetwork"
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"

inherit deploy

# Same source (and SRCREV) as edk2-uefipayload -> shared DL_DIR download.
SRC_URI = "gitsm://github.com/mrchromebox/edk2.git;protocol=https;branch=uefipayload_2605"
SRCREV = "2939f4969466bfe71722494e4cea5cbaa029c709"
PV = "2605+git${SRCPV}"
S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "nuc5i7ryh"
# nasm: X64 assembly in MdePkg/CryptoPkg lib instances. util-linux: libuuid
# for BaseTools. (No acpica/iasl -- these drivers carry no ACPI .asl.)
DEPENDS = "nasm-native util-linux-native"
INHIBIT_DEFAULT_DEPS = "1"
do_configure[noexec] = "1"

# The reference DSC that carries the library/PCD resolutions for these drivers
# (they are listed in its [Components]); -m builds just the named module.
EFI_DRIVERS_DSC ?= "MdeModulePkg/MdeModulePkg.dsc"

# One INF per line. NetworkCommon is the SNP producer; the others are the
# USB-Ethernet class bindings -- ECM is the clean match for the NanoKVM's
# ecm.usb0 gadget, RNDIS/NCM built too so the transport is selectable.
# Append your custom (e.g. Redfish-speaking) driver INFs here.
EFI_DRIVER_INFS ?= " \
    MdeModulePkg/Bus/Usb/UsbNetwork/NetworkCommon/NetworkCommon.inf \
    MdeModulePkg/Bus/Usb/UsbNetwork/UsbCdcEcm/UsbCdcEcm.inf \
    MdeModulePkg/Bus/Usb/UsbNetwork/UsbRndis/UsbRndis.inf \
    MdeModulePkg/Bus/Usb/UsbNetwork/UsbCdcNcm/UsbCdcNcm.inf \
"

do_compile() {
    cd ${S}

    # BaseTools are build-host tools; keep bitbake's cross CC out (same dance
    # as edk2-uefipayload). Name gcc/g++ explicitly.
    unset CC CXX CPP AS AR LD RANLIB STRIP OBJCOPY CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
    oe_runmake -C BaseTools CC=gcc CXX=g++

    export WORKSPACE="${S}"
    export EDK_TOOLS_PATH="${S}/BaseTools"
    export CONF_PATH="${S}/Conf"
    export PYTHON_COMMAND="python3"
    export PATH="${S}/BaseTools/BinWrappers/PosixLike:$PATH"
    mkdir -p ${S}/Conf
    for t in build_rule tools_def target; do
        [ -e "${S}/Conf/$t.txt" ] || cp "${S}/BaseTools/Conf/$t.template" "${S}/Conf/$t.txt"
    done

    # Build each driver standalone against the reference DSC's resolutions.
    for inf in ${EFI_DRIVER_INFS}; do
        bbnote "building EFI driver: $inf"
        build -a X64 -b RELEASE -t GCC \
            -p ${EFI_DRIVERS_DSC} -m "$inf" \
            -n ${@oe.utils.cpu_count()}
    done
}

do_deploy() {
    install -d ${DEPLOYDIR}/efi-drivers
    # Harvest the .efi for each built module by its INF basename.
    for inf in ${EFI_DRIVER_INFS}; do
        name=$(basename "$inf" .inf)
        efi=$(find ${S}/Build -type f -name "${name}.efi" | head -1)
        if [ -n "$efi" ]; then
            install -m 0644 "$efi" ${DEPLOYDIR}/efi-drivers/${name}.efi
            bbplain "deployed ${name}.efi ($(stat -c%s "$efi") bytes)"
        else
            bbfatal "no .efi produced for $inf -- check the build log"
        fi
    done
    # Manifest for the USB staging step.
    ( cd ${DEPLOYDIR}/efi-drivers && for f in *.efi; do
        printf '%s  %s\n' "$(sha256sum "$f" | cut -d" " -f1)" "$f"; done ) \
        > ${DEPLOYDIR}/efi-drivers/SHA256SUMS
}

addtask deploy after do_compile
do_install[noexec] = "1"
