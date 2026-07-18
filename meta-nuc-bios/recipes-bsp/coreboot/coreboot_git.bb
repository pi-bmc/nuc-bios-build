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
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=751419260aa954499f7abaabaa882bbe"

inherit deploy

# coreboot master, pinned 2026-07-16. The vendored board patch is Gerrit
# 94032's current patchset with its two cosmetic hunks (mainboard docs
# index, MAINTAINERS) stripped, leaving only new-file additions -- upstream
# stacks it on an unrelated unmerged change, so it is rebased here onto real
# master. Refresh the patch from
#   https://review.coreboot.org/changes/coreboot~94032/revisions/current/patch
# when a new patchset lands (and drop it entirely once the port merges).
# Fetched from the GitHub mirror: full clones off review.coreboot.org
# routinely stall/drop (SIGPIPE mid-clone); the mirror serves the same
# history, and the org also mirrors the submodule repos (coreboot's
# .gitmodules URLs are origin-relative). do_extract_blobs still pins the
# vboot submodule URL explicitly so it never depends on what bitbake set
# origin to.
SRC_URI = "git://github.com/coreboot/coreboot.git;protocol=https;branch=main \
           file://0001-mb-intel-nuc5i5ryb-Add-Intel-Broadwell-U-NUC-mainboard.patch \
           file://nuc5i7ryh.config \
           file://payload-edk2.config \
           file://payload-linuxboot.config \
           "
SRCREV = "149d1494fa4db2b08e7a7a6f7bbf7c7d2e8e18ad"

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

# Kconfig host tools; libuuid for cbfstool's vboot lib. (The edk2 payload
# builds in its own recipe now -- see edk2-uefipayload.)
DEPENDS = "bison-native flex-native python3-native util-linux-native"

# Network stays on for the whole compile: coreboot's build fetches its own
# submodules (vboot, libgfxinit, intel-microcode, ...) and bootstraps the
# i386 crossgcc toolchain from upstream tarballs. Same precedent as
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
        cat >> ${B}/.config <<'EOF'
CONFIG_USE_BLOBS=y
CONFIG_HAVE_MRC=y
CONFIG_HAVE_REFCODE_BLOB=y
EOF
    else
        bbwarn "coreboot: no blobs (COREBOOT_USE_DONOR_BLOBS=0, COREBOOT_BLOBS_DIR unset) -- building the CI-style blob-less ROM; it will NOT boot the NUC."
    fi
}

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
    # The i386 cross toolchain firmware stages are built with. Downloads the
    # gcc/binutils source tarballs on first build (cached in the workdir).
    # libgfxinit (native Broadwell graphics init, the port's tested video
    # path) additionally needs a host Ada compiler -- install your distro's
    # gcc-ada/gnat package or crossgcc skips GNAT and the build fails.
    oe_runmake crossgcc-i386 CPUS=${@oe.utils.cpu_count()}

    oe_runmake olddefconfig
    oe_runmake
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
}

# Firmware is not target userspace: the ROM embeds its own everything.
INHIBIT_DEFAULT_DEPS = "1"
