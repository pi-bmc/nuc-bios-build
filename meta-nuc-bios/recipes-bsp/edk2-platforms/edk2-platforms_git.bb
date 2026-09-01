SUMMARY = "tianocore edk2-platforms source tree"
DESCRIPTION = "The edk2-platforms tree, staged into ${datadir}/edk2/edk2-platforms. \
               coreboot's own edk2 build puts nine of its subdirectories on \
               PACKAGES_PATH when CONFIG_EDK2_USE_EDK2_PLATFORMS=y, and \
               edk2-uefipayload mirrors that list so the two builds stay \
               comparable. Nothing this board builds currently references a \
               file in it; it is on the path for parity, not for content. \
               Unpatched, and nothing is compiled here."
HOMEPAGE = "https://github.com/tianocore/edk2-platforms"

# Identical BSD-2-Clause-Patent text to edk2's License.txt.
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"

PV = "202607+git${SRCPV}"

SRC_URI = "git://github.com/tianocore/edk2-platforms.git;protocol=https;branch=master;destsuffix=edk2-platforms"

# edk2-platforms head 2026-07-28. Pinned for the same reason as edk2's.
SRCREV = "75efd079fed9723db8ce02365233c03b2fdc3b92"

# UNPACKDIR only exists from styhead (Yocto 5.1) on; scarthgap unpacks straight
# into WORKDIR. Without this shim, S never expands and do_unpack fails its
# unexpanded-variable QA check.
UNPACKDIR ?= "${WORKDIR}"

S = "${UNPACKDIR}/edk2-platforms"

# Source only: nothing is compiled and nothing is packaged, but the tree still
# has to reach edk2-uefipayload's sysroot, so do_populate_sysroot runs.
inherit allarch nopackages

# Where the tree lands in the sysroot. edk2-uefipayload reads exactly this path
# under ${STAGING_DATADIR} -- keep the two in step.
EDK2_SOURCE_ROOT = "${datadir}/edk2"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${EDK2_SOURCE_ROOT}/edk2-platforms
    cp -a ${S}/. ${D}${EDK2_SOURCE_ROOT}/edk2-platforms/

    # Build bookkeeping rather than source: the git fetcher's checkout
    # metadata, and quilt's .pc/ backups plus the "patches" symlink it points
    # at ${WORKDIR}/patches (which would stage as a dangling link).
    rm -rf ${D}${EDK2_SOURCE_ROOT}/edk2-platforms/.git \
           ${D}${EDK2_SOURCE_ROOT}/edk2-platforms/.pc \
           ${D}${EDK2_SOURCE_ROOT}/edk2-platforms/patches
}
