SUMMARY = "coreboot ROM carried inside the flasher live image"
DESCRIPTION = "Stages coreboot-nuc5i7ryh.rom (built by the default \
multiconfig's coreboot recipe) into the flasher live image at \
/opt/coreboot, so flashrom can write it to the NUC's SPI flash from the \
booted USB/ISO. Pulled across the multiconfig boundary the same way \
nanokvm-build's pibmc-firmware-seed carries the Pi image."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Opaque data blob, machine-independent.
inherit allarch

do_configure[noexec] = "1"
do_compile[noexec] = "1"

# The ROM is produced by the default multiconfig (its TMPDIR is ${TOPDIR}/tmp;
# TOPDIR is shared across multiconfigs). Keep in sync with the coreboot
# machine (nuc5i7ryh).
COREBOOT_DEPLOY = "${TOPDIR}/tmp/deploy/images/nuc5i7ryh"

do_install() {
    src="${COREBOOT_DEPLOY}/coreboot-nuc5i7ryh.rom"
    if [ ! -e "$src" ]; then
        bbfatal "nuc-coreboot-rom: no ROM at ${COREBOOT_DEPLOY} -- build the default mc's coreboot first"
    fi
    install -d ${D}/opt/coreboot
    install -m 0444 "$src" ${D}/opt/coreboot/coreboot-nuc5i7ryh.rom
    # Carry the not-bootable marker through if the ROM was a blob-less
    # compile check, so the flasher can refuse to write it.
    if [ -e "${COREBOOT_DEPLOY}/coreboot-nuc5i7ryh.rom.NOT-BOOTABLE" ]; then
        install -m 0444 "${COREBOOT_DEPLOY}/coreboot-nuc5i7ryh.rom.NOT-BOOTABLE" \
            ${D}/opt/coreboot/coreboot-nuc5i7ryh.rom.NOT-BOOTABLE
    fi
}

FILES:${PN} = "/opt/coreboot"
