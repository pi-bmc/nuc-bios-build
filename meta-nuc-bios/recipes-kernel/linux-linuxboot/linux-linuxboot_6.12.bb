SUMMARY = "Minimal x86-64 kernel for the LinuxBoot coreboot payload"
SECTION = "kernel"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

inherit kernel

LINUX_VERSION = "6.12.96"
LINUX_VERSION_EXTENSION = "-linuxboot"
PV = "${LINUX_VERSION}+git${SRCPV}"

# 6.12 LTS, pinned to the stable point release current as of 2026-07-18.
SRC_URI = "git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git;branch=linux-6.12.y;protocol=https \
           file://linuxboot.cfg"
SRCREV = "96432dc0b5bcec3f2cd5428bac2082175b6143e8"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "nuc5i7ryh"

KBUILD_DEFCONFIG = "x86_64_defconfig"

# Plain kernel.bbclass neither applies KBUILD_DEFCONFIG nor merges .cfg
# fragments (both are kernel-yocto features); do both by hand.
do_configure:prepend() {
    oe_runmake -C ${S} O=${B} ${KBUILD_DEFCONFIG}
}

do_configure:append() {
    ${S}/scripts/kconfig/merge_config.sh -m -O ${B} ${B}/.config ${WORKDIR}/linuxboot.cfg
    oe_runmake -C ${S} O=${B} olddefconfig
}
