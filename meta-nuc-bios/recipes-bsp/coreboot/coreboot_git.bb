SUMMARY = "coreboot for the Intel NUC5i7RYH (Broadwell-U Rock Canyon)"
DESCRIPTION = "Builds coreboot with the mb/intel/nuc5i5ryb mainboard port \
(coreboot Gerrit change 94032, vendored as a patch until it merges) for the \
NUC5i7RYH. The i7 kit uses the same NUC5iXRYB board as the i5 the port was \
developed on -- same Wildcat Point-LP PCH, NCT5577D SIO, I218-V GbE; only \
the soldered CPU/IGD differ, which coreboot probes at runtime. Payload is \
selectable via NUC_BIOS_PAYLOAD: edk2 (the edk2-uefipayload recipe's \
UEFIPAYLOAD.fd, consumed as a prebuilt FV payload -- CFR setup options and \
the SMMSTORE-backed EFI variable store are enabled explicitly in \
payload-edk2.config) or linuxboot (linux-linuxboot bzImage + u-root \
initramfs). Only the 6 MiB BIOS region of the 8 MiB flash is targeted; the \
factory descriptor/GbE/ME regions stay on the chip."
HOMEPAGE = "https://review.coreboot.org/c/coreboot/+/94032"

# Source pin + license shared with coreboot-toolchain-native.
require coreboot-source.inc

inherit deploy

# The vendored board patch is Gerrit 94032's current patchset with its two
# cosmetic hunks (mainboard docs index, MAINTAINERS) stripped, leaving only
# new-file additions -- upstream stacks it on an unrelated unmerged change,
# so it is rebased here onto real master. Refresh the patch from
#   https://review.coreboot.org/changes/coreboot~94032/revisions/current/patch
# when a new patchset lands (and drop it entirely once the port merges).
# do_extract_blobs pins the vboot submodule URL explicitly so it never
# depends on what bitbake set origin to.
SRC_URI = "${COREBOOT_GIT_URI} \
           file://0001-mb-intel-nuc5i5ryb-Add-Intel-Broadwell-U-NUC-mainboard.patch \
           file://nuc5i7ryh.config \
           file://payload-edk2.config \
           file://payload-linuxboot.config \
           "

# Donor image for the Broadwell memory-init blobs: MrChromebox's public
# coreboot+edk2 build for google/samus (Chromebook Pixel 2015, same
# Broadwell-U/Wildcat Point-LP silicon). mrc.bin and fallback/refcode are
# extracted from its CBFS at build time with the in-tree cbfstool -- verified
# locally: mrc.bin 222876 B (type mrc), refcode 177472 B decompressed. The
# .rom is not an archive; unpack=0 leaves it in WORKDIR.
COREBOOT_DONOR_ROM = "coreboot_edk2-samus-mrchromebox_20260714.rom"
SRC_URI += "https://www.mrchromebox.tech/files/firmware/full_rom/MrChromebox-2606.1/${COREBOOT_DONOR_ROM};name=donor;unpack=0"
SRC_URI[donor.sha256sum] = "7f287b55c0fad06f28d46dcff432a5414045b0fdd4b3df431385e94848c9357d"

S = "${WORKDIR}/git"
B = "${S}"

COMPATIBLE_MACHINE = "nuc5i7ryh"

# Kconfig host tools; libuuid for cbfstool's vboot lib; the i386-elf xgcc
# cross toolchain comes prebuilt (sstate-cached) from
# coreboot-toolchain-native -- editing this recipe no longer re-runs the
# ~30-minute crossgcc bootstrap. (The edk2 payload likewise builds in its
# own recipe -- see edk2-uefipayload.)
DEPENDS = "bison-native flex-native python3-native util-linux-native coreboot-toolchain-native"

# Where the staged toolchain lands (coreboot-toolchain-native installs it
# under ${datadir}); coreboot's Makefile takes it via XGCCPATH (trailing
# slash required -- it is used as a bare prefix).
XGCC = "${STAGING_DATADIR_NATIVE}/coreboot-xgcc/bin/"

# Network stays on for the whole compile: coreboot's build fetches its own
# submodules (vboot, libgfxinit, intel-microcode, ...). Same precedent as
# nanokvm-build's GOTOOLCHAIN=auto recipes.
do_compile[network] = "1"

# "edk2" or "linuxboot" -- set in conf/multiconfig/nuc-bios.conf.
NUC_BIOS_PAYLOAD ??= "edk2"

# Memory init blobs: Broadwell has no native raminit, so a bootable ROM
# needs mrc.bin + refcode.elf. Three modes:
#   1. COREBOOT_BLOBS_DIR set        -- use the user-supplied pair (e.g.
#      extracted from a different donor; see files/blobs/README.md).
#   2. COREBOOT_USE_DONOR_BLOBS = 1  -- (default) extract both from the
#      pinned MrChromebox samus image above with the in-tree cbfstool.
#   3. COREBOOT_USE_DONOR_BLOBS = 0  -- blob-less CI-style compile check;
#      the ROM links but DOES NOT BOOT and is marked .NOT-BOOTABLE.
#
# GbE: the Broadwell refcode hardcodes its internal GbE-enable field to 0
# (movb $0x0,0x37e(%ebx)); without intervention it disables the PCH GbE MAC
# and the OS never sees the I218-V (Documentation/soc/intel/broadwell/
# blobs.md, and nothing in coreboot's own Broadwell code re-enables it).
# The docs' fix is a one-byte patch, but their file offset (131253) is only
# valid for the exact Librem 13 v1 refcode binary -- the samus-extracted one
# is a different build (same instruction sequence, shifted ~0x1cc). So the
# patch here locates the documented instruction by byte pattern, requires it
# to be unique, and flips the immediate to 1. Verified against the pinned
# donor: one hit, byte 0x00 at file offset 0x1fff1. Setting enable=1 is what
# a GbE-equipped board wants regardless (the Gerrit 94032 port reports the
# I218-V working, with unstated blob provenance -- if that was ever true
# unpatched, enabling is still correct, merely redundant).
COREBOOT_BLOBS_DIR ??= ""
COREBOOT_USE_DONOR_BLOBS ??= "1"
COREBOOT_REFCODE_GBE_PATCH ??= "1"

# UEFI PXE for the onboard I218-V: add iPXE's UEFI option ROM (built by the
# ipxe-efirom recipe) to CBFS as pci<vid>,<did>.rom via CONFIG_PXE_ROM, so
# UefiPayloadPkg dispatches it and the payload's NetworkPkg stack
# (NETWORK_ENABLE in edk2-uefipayload) gets an SNP to drive. Set
# COREBOOT_ENABLE_PXE=0 to omit it. The PCI id must match the .efirom the
# ipxe-efirom recipe built (IPXE_VID/IPXE_DID there) -- confirm with lspci on
# the NUC. NB: only meaningful with the refcode GbE-enable patch on (above),
# else there is no NIC. Whether UefiPayloadPkg actually dispatches a CBFS
# option ROM for this non-VGA LOM is the one thing to confirm on hardware; if
# not, the fallback is bundling the driver as an FFS in the payload FV.
COREBOOT_ENABLE_PXE ??= "1"
COREBOOT_PXE_ROM_ID ??= "8086,15a1"
COREBOOT_PXE_EFIROM ??= "808615a1.efirom"

BLOBS_DEST = "${S}/3rdparty/blobs/mainboard/intel/nuc5i5ryb"

do_configure() {
    cat ${WORKDIR}/nuc5i7ryh.config > ${B}/.config

    if [ "${NUC_BIOS_PAYLOAD}" = "linuxboot" ]; then
        sed -e "s#@BZIMAGE@#${DEPLOY_DIR_IMAGE}/bzImage#" \
            -e "s#@INITRD@#${DEPLOY_DIR_IMAGE}/initramfs-u-root.cpio#" \
            ${WORKDIR}/payload-linuxboot.config >> ${B}/.config
    else
        sed -e "s#@UEFIPAYLOAD@#${DEPLOY_DIR_IMAGE}/UEFIPAYLOAD.fd#" \
            ${WORKDIR}/payload-edk2.config >> ${B}/.config
    fi

    if [ -n "${COREBOOT_BLOBS_DIR}" ]; then
        for f in mrc.bin refcode.elf; do
            [ -e "${COREBOOT_BLOBS_DIR}/$f" ] || \
                bbfatal "COREBOOT_BLOBS_DIR is set but ${COREBOOT_BLOBS_DIR}/$f is missing"
        done
        # The port expects the blobs under 3rdparty/blobs/mainboard/<board>/
        # (the HAVE_MRC/HAVE_REFCODE_BLOB default paths).
        install -d ${BLOBS_DEST}
        install -m 0644 ${COREBOOT_BLOBS_DIR}/mrc.bin ${BLOBS_DEST}/mrc.bin
        install -m 0644 ${COREBOOT_BLOBS_DIR}/refcode.elf ${BLOBS_DEST}/refcode.elf
    fi

    if [ -n "${COREBOOT_BLOBS_DIR}" ] || [ "${COREBOOT_USE_DONOR_BLOBS}" = "1" ]; then
        # The Broadwell Kconfig defaults for the blob paths are bare
        # "mrc.bin"/"refcode.elf" (relative to the source top, where nothing
        # is); point them at the extracted/user-supplied copies.
        cat >> ${B}/.config <<'EOF'
CONFIG_USE_BLOBS=y
CONFIG_HAVE_MRC=y
CONFIG_MRC_FILE="3rdparty/blobs/mainboard/intel/nuc5i5ryb/mrc.bin"
CONFIG_HAVE_REFCODE_BLOB=y
CONFIG_REFCODE_BLOB_FILE="3rdparty/blobs/mainboard/intel/nuc5i5ryb/refcode.elf"
EOF
    else
        bbwarn "coreboot: no blobs (COREBOOT_USE_DONOR_BLOBS=0, COREBOOT_BLOBS_DIR unset) -- building the CI-style blob-less ROM; it will NOT boot the NUC."
    fi

    # iPXE UEFI option ROM -> CBFS as pci<id>.rom. CONFIG_PXE + CONFIG_PXE_ROM
    # (use an existing image, not BUILD_IPXE) tells coreboot to add the
    # prebuilt .efirom the ipxe-efirom recipe deployed. The absolute
    # DEPLOY_DIR_IMAGE path is what payloads/external/iPXE/Makefile copies in.
    if [ "${COREBOOT_ENABLE_PXE}" = "1" ]; then
        if [ ! -s "${DEPLOY_DIR_IMAGE}/${COREBOOT_PXE_EFIROM}" ]; then
            bbfatal "COREBOOT_ENABLE_PXE=1 but ${DEPLOY_DIR_IMAGE}/${COREBOOT_PXE_EFIROM} is missing -- build ipxe-efirom first (do_configure[depends])."
        fi
        cat >> ${B}/.config <<EOF
CONFIG_PXE=y
CONFIG_PXE_ROM=y
CONFIG_PXE_ROM_ID="${COREBOOT_PXE_ROM_ID}"
CONFIG_PXE_ROM_FILE="${DEPLOY_DIR_IMAGE}/${COREBOOT_PXE_EFIROM}"
EOF
    fi
}
do_configure[vardeps] += "COREBOOT_ENABLE_PXE COREBOOT_PXE_ROM_ID COREBOOT_PXE_EFIROM"

# Donor-blob extraction (mode 2), a standalone task so the blobs can be
# produced and inspected without the multi-hour coreboot compile:
#   bitbake coreboot -c extract_blobs
# (also runs automatically before do_compile). cbfstool needs the vboot
# submodule, which the fetcher's plain clone does not carry -- hence the
# network flag on this task.
do_extract_blobs() {
    if [ -n "${COREBOOT_BLOBS_DIR}" ] || [ "${COREBOOT_USE_DONOR_BLOBS}" != "1" ]; then
        bbnote "donor blob extraction skipped (COREBOOT_BLOBS_DIR set or COREBOOT_USE_DONOR_BLOBS != 1)"
        return 0
    fi

    cd ${S}
    git submodule init 3rdparty/vboot
    git config submodule.3rdparty/vboot.url https://github.com/coreboot/vboot.git
    git submodule update --checkout 3rdparty/vboot
    # cbfstool runs on the build host; keep bitbake's exported cross CC (the
    # corei7-64 target compiler) out of its build.
    oe_runmake -C util/cbfstool CC="${BUILD_CC}" LDFLAGS=""

    install -d ${BLOBS_DEST}
    ./util/cbfstool/cbfstool ${WORKDIR}/${COREBOOT_DONOR_ROM} \
        extract -f ${BLOBS_DEST}/mrc.bin -n mrc.bin
    ./util/cbfstool/cbfstool ${WORKDIR}/${COREBOOT_DONOR_ROM} \
        extract -m x86 -f ${BLOBS_DEST}/refcode.elf -n fallback/refcode

    for f in mrc.bin refcode.elf; do
        [ -s "${BLOBS_DEST}/$f" ] || \
            bbfatal "donor blob extraction produced an empty $f -- inspect ${WORKDIR}/${COREBOOT_DONOR_ROM} with util/cbfstool"
    done

    # Keep the refcode from disabling the I218-V (see the GbE comment above):
    # find movb $0x0,0x37e(%ebx) [c6 83 7e 03 00 00 00], require exactly one
    # occurrence, flip the immediate to 1. Idempotent; refuses ambiguity.
    if [ "${COREBOOT_REFCODE_GBE_PATCH}" = "1" ]; then
        python3 - "${BLOBS_DEST}/refcode.elf" <<'PYEOF'
import sys
path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
disable = bytes.fromhex('c6837e03000000')  # movb $0x0,0x37e(%ebx)
enable  = bytes.fromhex('c6837e03000001')  # movb $0x1,0x37e(%ebx)
if data.count(enable) == 1 and data.count(disable) == 0:
    print('refcode: GbE enable already patched')
    sys.exit(0)
n = data.count(disable)
if n != 1:
    sys.exit('refcode: expected exactly one GbE-disable site, found %d -- '
             'donor refcode changed, re-verify before patching' % n)
off = data.index(disable) + 6
data[off] = 0x01
open(path, 'wb').write(data)
print('refcode: enabled Intel GbE (patched byte at file offset 0x%x)' % off)
PYEOF
    fi

    bbplain "extracted Broadwell blobs into ${BLOBS_DEST}:"
    bbplain "$(sha256sum ${BLOBS_DEST}/mrc.bin ${BLOBS_DEST}/refcode.elf)"
}
do_extract_blobs[network] = "1"
addtask extract_blobs after do_patch before do_compile

do_compile() {
    # Host-side tools (cbfstool & friends via HOSTCC) must use the build
    # host's toolchain, not bitbake's cross CC. The firmware stages are
    # compiled by the staged xgcc (XGCCPATH) from coreboot-toolchain-native.
    unset CC CXX CPP AS AR LD RANLIB STRIP OBJCOPY NM CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

    # Drop coreboot's build output so its toolchain probe (build/xcompile) is
    # regenerated against the current XGCCPATH -- a stale cache from an
    # earlier run records "no x86_32 toolchain" and make would reuse it.
    rm -rf ${B}/build ${B}/.xcompile

    oe_runmake olddefconfig XGCCPATH=${XGCC}
    oe_runmake XGCCPATH=${XGCC}
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/build/coreboot.rom ${DEPLOYDIR}/coreboot-nuc5i7ryh.rom
    rm -f ${DEPLOYDIR}/coreboot-nuc5i7ryh.rom.NOT-BOOTABLE
    if [ -z "${COREBOOT_BLOBS_DIR}" ] && [ "${COREBOOT_USE_DONOR_BLOBS}" != "1" ]; then
        echo "built without mrc.bin/refcode.elf -- compile check only, do not flash" \
            > ${DEPLOYDIR}/coreboot-nuc5i7ryh.rom.NOT-BOOTABLE
    fi
}

addtask deploy after do_compile

do_install[noexec] = "1"

# Payload inputs come from sibling recipes' deploy dirs: the edk2
# UefiPayloadPkg FV (default) or the LinuxBoot kernel + u-root initramfs.
python () {
    if d.getVar('NUC_BIOS_PAYLOAD') == 'linuxboot':
        d.appendVarFlag('do_compile', 'depends',
                        ' linux-linuxboot:do_deploy u-root:do_deploy')
    else:
        d.appendVarFlag('do_compile', 'depends',
                        ' edk2-uefipayload:do_deploy')
    # The iPXE .efirom must be deployed before do_configure -- it is both
    # referenced (CONFIG_PXE_ROM_FILE) and existence-checked there.
    if d.getVar('COREBOOT_ENABLE_PXE') == '1':
        d.appendVarFlag('do_configure', 'depends', ' ipxe-efirom:do_deploy')
}

# Firmware is not target userspace: the ROM embeds its own everything.
INHIBIT_DEFAULT_DEPS = "1"
