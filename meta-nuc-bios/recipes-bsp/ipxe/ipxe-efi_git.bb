SUMMARY = "iPXE UEFI PCI option ROM for the onboard Intel I218-V (NUC5i7RYH)"
DESCRIPTION = "Builds bin-x86_64-efi/<vid><did>.efirom -- a UEFI PCI option \
ROM carrying iPXE's 'intel' driver for the NUC5i7RYH's I218-V LOM \
(8086:15a1; iPXE lists it as 'i218v-2'). coreboot adds this to CBFS as \
pci<vid>,<did>.rom via CONFIG_PXE_ROM; UefiPayloadPkg's PciBusDxe dispatches \
it off the device's expansion-ROM path and it installs an \
EFI_SIMPLE_NETWORK_PROTOCOL -- the SNP the payload's NetworkPkg stack \
(NETWORK_ENABLE, set in edk2-uefipayload) binds for UEFI PXE. The LOM is \
only present at all because the coreboot recipe's refcode GbE-enable patch \
(COREBOOT_REFCODE_GBE_PATCH=1) flips the Broadwell refcode's GbE-disable \
byte -- without it there is no NIC for this ROM to drive."
HOMEPAGE = "https://ipxe.org"

# iPXE is GPL-2.0-or-later with the UBDL (UEFI Binary Distribution License)
# additional-permissions exception. The md5 is the canonical GPLv2 text
# (b234ee4d... is oe-core's common-licenses GPL-2.0-only checksum), which is
# what the pinned SRCREV ships as COPYING.GPLv2.
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://COPYING.GPLv2;md5=b234ee4d69f5fce4486a80fdaf4a4263"

SRC_URI = "git://github.com/ipxe/ipxe.git;protocol=https;branch=master"
# master HEAD as of 2026-07-21.
SRCREV = "ebca9ed2b2e62ee579100142ee61f5ba77a3c712"

PV = "1.21.1+git${SRCPV}"
S = "${WORKDIR}/git"

inherit deploy

# The ROM is firmware; it embeds its own libc/drivers and links nothing from
# the target sysroot.
INHIBIT_DEFAULT_DEPS = "1"

# Host-built like coreboot's cbfstool/blob tooling: the .efirom is an
# x86_64 UEFI artifact regardless of the build host, and this build host is
# x86_64, so the BUILD compiler produces it directly. iPXE also needs perl
# and binutils (objcopy/ld) on the build host -- add them to HOSTTOOLS if a
# minimal builder lacks them.
DEPENDS = ""

do_configure[noexec] = "1"

# Target NIC. CONFIRMED ON HARDWARE 2026-07-28: the UEFI shell's `pci` on this
# unit reports 00:19.0 Ethernet controller = 8086:15A3, i.e. iPXE's "i218v-3"
#     PCI_ROM ( 0x8086, 0x15a3, "i218v-3", "I218-V", INTEL_NO_PHY_RST )
# NOT the 15a1 ("i218v-2") this recipe originally guessed. PciBusDxe matches
# option ROMs by PCI id, so a 15a1 ROM is simply never dispatched for a 15a3
# device -- the ROM lands in CBFS, nothing loads it, no SNP is produced and no
# PXE boot option ever appears. COREBOOT_PXE_ROM_ID/COREBOOT_PXE_EFIROM in the
# coreboot recipe must stay in step with this.
IPXE_VID ??= "8086"
IPXE_DID ??= "15a3"
IPXE_ROM_FILE = "${IPXE_VID}${IPXE_DID}.efirom"

# iPXE source is fully vendored; no submodule fetch, so no network at compile.
do_compile() {
    # Keep bitbake's exported cross CC out of iPXE's host build (same 'unset'
    # dance the coreboot/edk2 recipes use). iPXE resolves plain gcc from PATH;
    # HOST_CC drives its own util/ tools (elf2efi, efirom).
    unset CC CXX CPP AS AR LD RANLIB STRIP OBJCOPY NM CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

    # Two artifacts, for two different dispatch paths:
    #
    #   ipxe.efi     a plain UEFI driver, embedded in the payload's DXE
    #                firmware volume as an FFS and dispatched by the DXE
    #                dispatcher. Works regardless of PCI class -- this is the
    #                path that actually produces an SNP on this board.
    #
    #   <vid><did>.efirom  a PCI expansion ROM. Kept because it is cheap, but
    #                NOT the working path: coreboot's pci_rom_run() returns
    #                early for anything that is not PCI_CLASS_DISPLAY_VGA, so a
    #                LOM's option ROM in CBFS is never loaded, and the I218-V
    #                has no physical expansion-ROM BAR for PciBusDxe to find
    #                either. Confirmed on hardware 2026-07-28: no PXE boot
    #                option appeared.
    oe_runmake -C ${S}/src \
        bin-x86_64-efi/ipxe.efi \
        bin-x86_64-efi/${IPXE_ROM_FILE} \
        CROSS_COMPILE="" \
        HOST_CC="${BUILD_CC}" \
        V=1

    [ -s "${S}/src/bin-x86_64-efi/ipxe.efi" ] || \
        bbfatal "iPXE produced no ipxe.efi -- check the build log"
    [ -s "${S}/src/bin-x86_64-efi/${IPXE_ROM_FILE}" ] || \
        bbfatal "iPXE produced no ${IPXE_ROM_FILE} -- check the NIC PCI id (IPXE_DID) and the build log"
}

do_deploy() {
    install -d ${DEPLOYDIR}
    # edk2-uefipayload stages this into UefiPayloadPkg/NetworkDrivers/.
    install -m 0644 ${S}/src/bin-x86_64-efi/ipxe.efi ${DEPLOYDIR}/ipxe.efi
    install -m 0644 ${S}/src/bin-x86_64-efi/${IPXE_ROM_FILE} \
        ${DEPLOYDIR}/${IPXE_ROM_FILE}
}

addtask deploy after do_compile

do_install[noexec] = "1"

# Sanity: this ROM is meaningless off its target machine.
COMPATIBLE_MACHINE = "nuc5i7ryh"
