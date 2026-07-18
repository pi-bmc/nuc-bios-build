SUMMARY = "coreboot for the Intel NUC5i7RYH (Broadwell-U Rock Canyon)"
DESCRIPTION = "Builds coreboot with the mb/intel/nuc5i5ryb mainboard port \
(coreboot Gerrit change 94032, vendored as a patch until it merges) for the \
NUC5i7RYH. The i7 kit uses the same NUC5iXRYB board as the i5 the port was \
developed on -- same Wildcat Point-LP PCH, NCT5577D SIO, I218-V GbE; only \
the soldered CPU/IGD differ, which coreboot probes at runtime. Payload is \
selectable via NUC_BIOS_PAYLOAD: edk2 (UefiPayloadPkg, built by coreboot's \
own payloads/external machinery -- the port's Kconfig wires CFR setup \
options and SMMSTORE for it) or linuxboot (linux-linuxboot bzImage + u-root \
initramfs from this multiconfig). Only the 6 MiB BIOS region of the 8 MiB \
flash is targeted; the factory descriptor/GbE/ME regions stay on the chip."
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
SRC_URI = "git://review.coreboot.org/coreboot.git;protocol=https;branch=main \
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

# Kconfig host tools; nasm/python3/libuuid for the edk2 payload's BaseTools.
DEPENDS = "bison-native flex-native nasm-native python3-native util-linux-native"

# Network stays on for the whole compile: coreboot's build fetches its own
# submodules (vboot, libgfxinit, intel-microcode, ...), bootstraps the i386
# crossgcc toolchain from upstream tarballs, and clones the edk2 payload
# (MrChromebox fork) when CONFIG_PAYLOAD_EDK2=y. Same precedent as
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
# GbE note: coreboot's Broadwell docs describe a one-byte refcode patch to
# keep the refcode from disabling an Intel GbE MAC (the NUC has an I218-V),
# but the documented offset/value pair applies only to the exact Librem 13 v1
# refcode binary (sha256 8a919ffe...) -- the samus-extracted refcode differs,
# so NO patch is applied here. The nuc5i5ryb port reports I218-V working with
# unpatched blobs; if Ethernet is dead under coreboot, this is the trail.
COREBOOT_BLOBS_DIR ??= ""
COREBOOT_USE_DONOR_BLOBS ??= "1"

BLOBS_DEST = "${S}/3rdparty/blobs/mainboard/intel/nuc5i5ryb"

do_configure() {
    cat ${WORKDIR}/nuc5i7ryh.config > ${B}/.config

    if [ "${NUC_BIOS_PAYLOAD}" = "linuxboot" ]; then
        sed -e "s#@BZIMAGE@#${DEPLOY_DIR_IMAGE}/bzImage#" \
            -e "s#@INITRD@#${DEPLOY_DIR_IMAGE}/initramfs-u-root.cpio#" \
            ${WORKDIR}/payload-linuxboot.config >> ${B}/.config
    else
        cat ${WORKDIR}/payload-edk2.config >> ${B}/.config
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

do_compile() {
    # Donor-blob extraction (mode 2). cbfstool needs the vboot submodule;
    # the fetcher's plain clone does not carry submodules, so init it here
    # (network is on for this task anyway).
    if [ -z "${COREBOOT_BLOBS_DIR}" ] && [ "${COREBOOT_USE_DONOR_BLOBS}" = "1" ]; then
        cd ${S}
        git submodule update --init --checkout 3rdparty/vboot
        oe_runmake -C util/cbfstool
        install -d ${BLOBS_DEST}
        ./util/cbfstool/cbfstool ${WORKDIR}/${COREBOOT_DONOR_ROM} \
            extract -f ${BLOBS_DEST}/mrc.bin -n mrc.bin
        ./util/cbfstool/cbfstool ${WORKDIR}/${COREBOOT_DONOR_ROM} \
            extract -m x86 -f ${BLOBS_DEST}/refcode.elf -n fallback/refcode
        for f in mrc.bin refcode.elf; do
            [ -s "${BLOBS_DEST}/$f" ] || \
                bbfatal "donor blob extraction produced an empty $f -- inspect ${WORKDIR}/${COREBOOT_DONOR_ROM} with util/cbfstool"
        done
    fi

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

# LinuxBoot payload inputs come from this same multiconfig's deploy dir.
python () {
    if d.getVar('NUC_BIOS_PAYLOAD') == 'linuxboot':
        d.appendVarFlag('do_compile', 'depends',
                        ' linux-linuxboot:do_deploy u-root:do_deploy')
}

# Firmware is not target userspace: the ROM embeds its own everything.
INHIBIT_DEFAULT_DEPS = "1"
