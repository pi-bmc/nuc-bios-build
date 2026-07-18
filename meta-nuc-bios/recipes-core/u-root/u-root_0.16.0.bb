SUMMARY = "u-root initramfs for the LinuxBoot payload"
DESCRIPTION = "Builds the u-root ramfs generator from source and runs it to \
produce the LinuxBoot initramfs: the core + boot command sets (incl. \
localboot/fbnetboot/kexec) compiled into one Go busybox. The cpio is embedded \
into the coreboot ROM via CONFIG_LINUX_INITRD when NUC_BIOS_PAYLOAD = \
\"linuxboot\"."
HOMEPAGE = "https://github.com/u-root/u-root"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=bf431bf303eaf01f17bef6624d9f2208"

inherit deploy

# v0.16.0 (Feb 2026). nobranch: release tags are cut off release branches.
SRC_URI = "git://github.com/u-root/u-root.git;protocol=https;nobranch=1"
SRCREV = "a9c0bf61c74128eceaed057ee98f4068b603c5f9"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "nuc5i7ryh"

DEPENDS = "go-native"

# Go module downloads + GOTOOLCHAIN auto-fetch (u-root's go.mod tracks a
# newer Go than scarthgap's 1.22 go-native).
do_compile[network] = "1"

do_configure[noexec] = "1"

do_compile() {
    cd ${S}
    export HOME="${WORKDIR}"
    export GOTOOLCHAIN="auto"
    export GOFLAGS="-mod=mod -modcacherw"

    # Build the generator for the build host, then have it compile the
    # busybox for the target. GOARCH governs the embedded command build;
    # amd64 matches the corei7-64 machine.
    go build -o ${B}/u-root .
    GOARCH=amd64 ${B}/u-root -uroot-source ${S} \
        -o ${B}/initramfs-u-root.cpio core boot

    chmod -R u+w ${HOME}/go 2>/dev/null || true
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/initramfs-u-root.cpio ${DEPLOYDIR}/initramfs-u-root.cpio
}

addtask deploy after do_compile

do_install[noexec] = "1"
