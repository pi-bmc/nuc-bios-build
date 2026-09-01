SUMMARY = "tianocore edk2-redfish-client (RedfishClientPkg) source tree"
DESCRIPTION = "RedfishClientPkg -- the standard Redfish feature layer that sits \
               on top of edk2's RedfishPkg host-interface core: BiosDxe, \
               BootOptionDxe, ComputerSystemDxe and the JSON converters. \
               Staged into ${datadir}/edk2/edk2-redfish-client as one of \
               edk2-uefipayload's PACKAGES_PATH roots. \
\
               It tracks edk2 MASTER, which is the whole reason the edk2 recipe \
               beside it does too: the GUIDs this tree needs \
               (gEdkIIRedfisEventRedfishInterfaceDisconnectionGuid and friends) \
               are declared by RedfishPkg.dec on master and by no stable tag \
               through 202605. The two are one compatibility pair -- this tree \
               compiles against that edk2's RedfishPkg -- so re-measure the \
               window before moving either SRCREV."
HOMEPAGE = "https://github.com/tianocore/edk2-redfish-client"

# Identical BSD-2-Clause-Patent text to edk2's License.txt, under a different
# filename.
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2b415520383f7964e96700ae12b4570a"

PV = "202608+git${SRCPV}"

# The one patch that applies to this tree rather than to edk2. It kept its
# out-of-the-way 0100 number from when both series shared a recipe and the
# entry needed ";patchdir=" to reach this tree at all; here ${S} IS this tree,
# so it applies like any other patch.
SRC_URI = "git://github.com/tianocore/edk2-redfish-client.git;protocol=https;branch=main;destsuffix=edk2-redfish-client \
           file://0100-RedfishClientPkg-fit-the-client-to-a-Redfish-host-in.patch \
           "

# edk2-redfish-client head 2026-08-04. Pinned for the same reason as edk2's.
SRCREV = "92fabf8572c226cf180c62b1204380385a518db3"

# UNPACKDIR only exists from styhead (Yocto 5.1) on; scarthgap unpacks straight
# into WORKDIR. Without this shim, S never expands and do_unpack fails its
# unexpanded-variable QA check.
UNPACKDIR ?= "${WORKDIR}"

S = "${UNPACKDIR}/edk2-redfish-client"

# Source only: nothing is compiled and nothing is packaged, but the tree still
# has to reach edk2-uefipayload's sysroot, so do_populate_sysroot runs.
inherit allarch nopackages

# Where the tree lands in the sysroot. edk2-uefipayload reads exactly this path
# under ${STAGING_DATADIR} -- keep the two in step.
EDK2_SOURCE_ROOT = "${datadir}/edk2"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client
    cp -a ${S}/. ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client/

    # Build bookkeeping rather than source: the git fetcher's checkout
    # metadata, and quilt's .pc/ backups plus the "patches" symlink it points
    # at ${WORKDIR}/patches (which would stage as a dangling link).
    rm -rf ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client/.git \
           ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client/.pc \
           ${D}${EDK2_SOURCE_ROOT}/edk2-redfish-client/patches
}
