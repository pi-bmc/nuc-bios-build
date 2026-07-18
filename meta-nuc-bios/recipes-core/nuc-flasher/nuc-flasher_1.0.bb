SUMMARY = "coreboot flashing helpers + instructions for the live ISO"
DESCRIPTION = "The flash-coreboot / backup-bios helper scripts, a DHCP-on-boot \
init script (so the stock backup can be scp'd off), and an motd with the \
step-by-step for the NUC coreboot flasher live image."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://backup-bios \
           file://flash-coreboot \
           file://nuc-flasher-netup \
           file://motd"

S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "nuc-flasher-netup"
INITSCRIPT_PARAMS = "start 40 2 3 4 5 . stop 60 0 1 6 ."

# flashrom does the actual write; the app expects the ROM at /opt/coreboot
# (nuc-coreboot-rom), pulled into the image separately.
RDEPENDS:${PN} = "flashrom"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/backup-bios ${D}${bindir}/backup-bios
    install -m 0755 ${WORKDIR}/flash-coreboot ${D}${bindir}/flash-coreboot

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/nuc-flasher-netup ${D}${sysconfdir}/init.d/nuc-flasher-netup

    # Print the banner from profile.d rather than owning /etc/motd (base-files
    # already provides that file -- two owners is an rpm/dnf conflict). This
    # fires on both the console root login and ssh sessions.
    install -d ${D}${datadir}/nuc-flasher
    install -m 0644 ${WORKDIR}/motd ${D}${datadir}/nuc-flasher/motd
    install -d ${D}${sysconfdir}/profile.d
    printf '#!/bin/sh\ncat %s/nuc-flasher/motd\n' "${datadir}" \
        > ${D}${sysconfdir}/profile.d/nuc-flasher.sh
    chmod 0644 ${D}${sysconfdir}/profile.d/nuc-flasher.sh
}

FILES:${PN} = " \
    ${bindir}/backup-bios \
    ${bindir}/flash-coreboot \
    ${sysconfdir}/init.d/nuc-flasher-netup \
    ${datadir}/nuc-flasher/motd \
    ${sysconfdir}/profile.d/nuc-flasher.sh \
    "
