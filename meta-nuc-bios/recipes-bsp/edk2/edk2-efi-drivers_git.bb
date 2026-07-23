SUMMARY = "Standalone EDK2 UEFI drivers (.efi) for the NUC's stock BIOS (Path B)"
DESCRIPTION = "Builds individual UEFI DXE drivers as standalone .efi files, to be \
staged on a USB/FAT volume and loaded on the stock (locked) NUC BIOS via a \
Driver#### load option (efibootmgr --driver) -- NOT flashed into firmware. \
The default set is the EDK2 USB-network stack (NetworkCommon + CDC-ECM/RNDIS/NCM \
transports) PLUS a full EDK2 Redfish client: the NetworkPkg IPv4/HTTP stack, the \
RedfishPkg core drivers, a platform host-interface library and a replacement \
platform-config driver (RedfishConfigDriver, backed by the AMI Setup variable), \
and the RedfishClientPkg BIOS attribute-sync feature drivers. Reuses the same \
MrChromebox edk2 tree the payload recipe fetches, plus tianocore/edk2-redfish-client \
and a local NucRedfishPkg staged from files/. Add your own driver INFs via \
EFI_DRIVER_INFS_append."
HOMEPAGE = "https://github.com/tianocore/edk2/tree/master/RedfishPkg"
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"

inherit deploy

# Same edk2 source (and SRCREV) as edk2-uefipayload -> shared DL_DIR download.
# edk2-redfish-client supplies RedfishClientPkg (not present in the edk2 fork).
# NucRedfishPkg is our local platform glue (DSC + host-interface lib + config driver).
SRC_URI = "gitsm://github.com/mrchromebox/edk2.git;protocol=https;branch=uefipayload_2605;name=edk2 \
           git://github.com/tianocore/edk2-redfish-client.git;protocol=https;branch=main;name=redfishclient;destsuffix=edk2-redfish-client \
           file://NucRedfishPkg \
          "
# edk2 fork rolling HEAD (matches edk2-uefipayload).
# edk2-redfish-client is pinned to a75f45cd (2026-05-15) -- the newest commit
# whose closure the fork's (older) RedfishPkg still satisfies. The next commit
# (b8ffa6e4) adds a Redfish-interface-disconnect event that references
# gEdkIIRedfisEventRedfishInterfaceDisconnectionGuid, a GUID defined only by a
# companion upstream edk2 RedfishPkg change the MrChromebox fork does not carry.
# tianocore does not tag edk2-redfish-client releases.
SRCREV_edk2 = "2939f4969466bfe71722494e4cea5cbaa029c709"
SRCREV_redfishclient = "a75f45cd69c74121fbf58900b9d92735d9a3373c"
SRCREV_FORMAT = "edk2_redfishclient"

PV = "2605+git"
S = "${WORKDIR}/git"

# Staged locations of the extra packages (see PACKAGES_PATH in do_compile).
REDFISH_CLIENT_DIR = "${WORKDIR}/edk2-redfish-client"
NUC_REDFISH_PKG_DIR = "${WORKDIR}"

COMPATIBLE_MACHINE = "nuc5i7ryh"
# nasm: X64 assembly in MdePkg/CryptoPkg lib instances. util-linux: libuuid
# for BaseTools. (No acpica/iasl -- these drivers carry no ACPI .asl.)
DEPENDS = "nasm-native util-linux-native"
INHIBIT_DEFAULT_DEPS = "1"
do_configure[noexec] = "1"

# Our platform DSC carries the library/PCD resolutions for the full driver set.
# Path is resolved via PACKAGES_PATH (NucRedfishPkg is a package root under WORKDIR).
EFI_DRIVERS_DSC ?= "NucRedfishPkg/NucRedfish.dsc"

# One INF per line. Every INF must also appear in NucRedfish.dsc [Components]
# (or be provided by an !include'd component list, e.g. NetworkPkg/Network.dsc.inc).
# NucRedfishHostInterfaceLib is a LIBRARY linked into RedfishHostInterfaceDxe and
# is intentionally NOT listed here.
EFI_DRIVER_INFS ?= " \
    MdeModulePkg/Bus/Usb/UsbNetwork/NetworkCommon/NetworkCommon.inf \
    MdeModulePkg/Bus/Usb/UsbNetwork/UsbCdcEcm/UsbCdcEcm.inf \
    MdeModulePkg/Bus/Usb/UsbNetwork/UsbRndis/UsbRndis.inf \
    MdeModulePkg/Bus/Usb/UsbNetwork/UsbCdcNcm/UsbCdcNcm.inf \
    NetworkPkg/DpcDxe/DpcDxe.inf \
    NetworkPkg/MnpDxe/MnpDxe.inf \
    NetworkPkg/ArpDxe/ArpDxe.inf \
    NetworkPkg/Ip4Dxe/Ip4Dxe.inf \
    NetworkPkg/Udp4Dxe/Udp4Dxe.inf \
    NetworkPkg/TcpDxe/TcpDxe.inf \
    NetworkPkg/Dhcp4Dxe/Dhcp4Dxe.inf \
    NetworkPkg/DnsDxe/DnsDxe.inf \
    NetworkPkg/HttpUtilitiesDxe/HttpUtilitiesDxe.inf \
    NetworkPkg/HttpDxe/HttpDxe.inf \
    MdeModulePkg/Universal/RegularExpressionDxe/RegularExpressionDxe.inf \
    RedfishPkg/RestJsonStructureDxe/RestJsonStructureDxe.inf \
    RedfishPkg/RedfishHostInterfaceDxe/RedfishHostInterfaceDxe.inf \
    RedfishPkg/RedfishRestExDxe/RedfishRestExDxe.inf \
    RedfishPkg/RedfishCredentialDxe/RedfishCredentialDxe.inf \
    RedfishPkg/RedfishDiscoverDxe/RedfishDiscoverDxe.inf \
    RedfishPkg/RedfishHttpDxe/RedfishHttpDxe.inf \
    RedfishPkg/RedfishConfigHandler/RedfishConfigHandlerDriver.inf \
    NucRedfishPkg/RedfishConfigDriver/RedfishConfigDriver.inf \
    NucRedfishPkg/ConnectRedfishApp/ConnectRedfishApp.inf \
    RedfishClientPkg/RedfishFeatureCoreDxe/RedfishFeatureCoreDxe.inf \
    RedfishClientPkg/RedfishETagDxe/RedfishETagDxe.inf \
    RedfishClientPkg/RedfishConfigLangMapDxe/RedfishConfigLangMapDxe.inf \
    RedfishClientPkg/HiiToRedfishBootDxe/HiiToRedfishBootDxe.inf \
    RedfishClientPkg/Features/Bios/v1_0_9/Dxe/BiosDxe.inf \
    RedfishClientPkg/Features/BiosAttributeRegistry/v1_3_6/BiosAttributeRegistryDxe.inf \
    RedfishClientPkg/Features/BootOption/v1_0_4/Dxe/BootOptionDxe.inf \
    RedfishClientPkg/Features/BootOptionCollection/BootOptionCollectionDxe.inf \
    RedfishClientPkg/Converter/Bios/v1_0_9/RedfishBios_V1_0_9_Dxe.inf \
    RedfishClientPkg/Converter/AttributeRegistry/v1_3_6/RedfishAttributeRegistry_V1_3_6_Dxe.inf \
    RedfishClientPkg/Converter/BootOption/v1_0_4/RedfishBootOption_V1_0_4_Dxe.inf \
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
    # Resolve RedfishClientPkg (edk2-redfish-client) and NucRedfishPkg (local)
    # as additional package roots alongside the edk2 tree.
    export PACKAGES_PATH="${S}:${REDFISH_CLIENT_DIR}:${NUC_REDFISH_PKG_DIR}"

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
