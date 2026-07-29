SUMMARY = "coreboot for the Intel NUC5i7RYH (Broadwell-U Rock Canyon)"
DESCRIPTION = "Builds coreboot with the mb/intel/nuc5i5ryb mainboard port \
(coreboot Gerrit change 94032, vendored as a patch until it merges) for the \
NUC5i7RYH. The i7 kit uses the same NUC5iXRYB board as the i5 the port was \
developed on -- same Wildcat Point-LP PCH, NCT5577D SIO, I218-V GbE; only \
the soldered CPU/IGD differ, which coreboot probes at runtime. Payload is \
selectable via NUC_BIOS_PAYLOAD: edk2 (built by coreboot's own \
payloads/external/edk2 machinery, with iPXE embedded in the FV via \
EDK2_ENABLE_IPXE -- all tuned through payloads/external/edk2/Kconfig symbols \
set in payload-edk2.config) or linuxboot (linux-linuxboot bzImage + u-root \
initramfs). The image embeds this unit's factory descriptor/GbE/ME regions \
(HAVE_IFD_BIN et al.), so build/coreboot.rom is a complete 8 MiB image valid \
for blank-chip/clip recovery; in-band flashing still writes only the BIOS \
region -- the factory regions on the chip are never touched from the host."
HOMEPAGE = "https://review.coreboot.org/c/coreboot/+/94032"

# Source pin + license shared with coreboot-toolchain-native.
require coreboot-source.inc

inherit deploy

# The mainboard port is carried as plain source files in files/mainboard/,
# copied into the coreboot tree by do_configure:prepend rather than applied as
# a patch. The port creates only new files (nothing upstream is modified), so
# there is nothing for a patch to diff against, and shipping the sources
# directly makes them editable without patch surgery.
#
# Provenance: coreboot Gerrit 94032 (mb/intel/nuc5i5ryb), plus board-local work
# on top -- bootblock.c (early NCT5577D AC-loss policy and GEN_PMCON_3 logging
# before ramstage's read-modify-write clears the RTC-well flags), smihandler.c
# (Super I/O S5 entry handling, GPE re-assert) and acpi/lan.asl + the
# devicetree gpe0_en_4 setting (Wake-on-LAN as the remote power-on path; unlike
# AFTERG3_EN it lives in the suspend well, so it survives an AC cycle without
# depending on RTC-well state). See files/mainboard/README.md.
#
# 0004 stays a patch because it modifies an existing upstream file
# (southbridge/intel/lynxpoint/acpi/xhci.asl), which files/mainboard/ cannot
# express.
#
# do_extract_blobs pins the vboot submodule URL explicitly so it never
# depends on what bitbake set origin to.
SRC_URI = "${COREBOOT_GIT_URI} \
           file://mainboard \
           file://blobs \
           file://nuc5i7ryh.config \
           file://payload-edk2.config \
           file://payload-linuxboot.config \
           "

# Donor image for the Broadwell memory-init blobs: MrChromebox's public
# coreboot+edk2 build for google/tidus (Lenovo ThinkCentre Chromebox).
# mrc.bin and fallback/refcode are extracted from its CBFS at build time with
# the in-tree cbfstool. The .rom is not an archive; unpack=0 leaves it in
# WORKDIR.
#
# WHY tidus and not samus (which this recipe used until 2026-07-28): samus is
# the Chromebook Pixel 2015, whose memory is SOLDERED LPDDR3. This NUC has two
# socketed DDR3L SO-DIMM slots, and tidus is a Broadwell *desktop* Chromebox
# with socketed DDR3L -- the same topology. The blobs are genuinely different
# builds, not just different packaging:
#
#     mrc.bin      samus 222876 B   tidus 223640 B
#     refcode.elf  samus 192440 B   tidus 192624 B
#
# Two independent checks say tidus is the blob pair the Broadwell world
# actually standardises on:
#   1. tidus refcode.elf sha1 e3f985d23199a4bd8ec317beae3dd90ce5dfa3cc is a
#      byte-for-byte match for the refcode Purism ships for the Librem 13 v1
#      (REFCODE_SHA1 in purism-librem-coreboot-updater.sh -- which sources it
#      from a tidus ChromeOS recovery image, not from a Librem ROM).
#   2. Its GbE-disable instruction sits at file offset 131253 (0x200b5) --
#      exactly the offset Documentation/soc/intel/broadwell/blobs.md quotes for
#      the Librem 13 v1 refcode. The samus copy is at 0x1fff1 instead, which is
#      why the patch below matches by byte pattern rather than fixed offset.
COREBOOT_DONOR_ROM = "coreboot_edk2-tidus-mrchromebox_20260714.rom"
SRC_URI += "https://www.mrchromebox.tech/files/firmware/full_rom/MrChromebox-2606.1/${COREBOOT_DONOR_ROM};name=donor;unpack=0"
SRC_URI[donor.sha256sum] = "382bd654e2191369bae75302e70283e50ac2d8d27fe6a55e8c2b365520713eca"

S = "${WORKDIR}/git"
B = "${S}"

COMPATIBLE_MACHINE = "nuc5i7ryh"

# Kconfig host tools; libuuid for cbfstool's vboot lib; the i386-elf xgcc
# cross toolchain comes prebuilt (sstate-cached) from
# coreboot-toolchain-native -- editing this recipe no longer re-runs the
# ~30-minute crossgcc bootstrap. (The edk2 payload likewise builds in its
# own recipe.)
DEPENDS = "bison-native flex-native python3-native util-linux-native nasm-native acpica-native coreboot-toolchain-native"

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
#      pinned MrChromebox tidus image above with the in-tree cbfstool.
#   3. COREBOOT_USE_DONOR_BLOBS = 0  -- blob-less CI-style compile check;
#      the ROM links but DOES NOT BOOT and is marked .NOT-BOOTABLE.
#
# GbE: the Broadwell refcode hardcodes its internal GbE-enable field to 0
# (movb $0x0,0x37e(%ebx)); without intervention it disables the PCH GbE MAC
# and the OS never sees the I218-V (Documentation/soc/intel/broadwell/
# blobs.md, and nothing in coreboot's own Broadwell code re-enables it).
# The docs' fix is a one-byte patch at a fixed file offset (131253), which is
# only valid for one exact refcode build. The patch here instead locates the
# documented instruction by byte pattern, requires it to be unique, and flips
# the immediate to 1 -- so it survives a donor change. Verified against both
# donors: exactly one hit each, byte 0x00 at 0x200b5 (tidus, == the docs'
# 131253) and at 0x1fff1 (samus). Setting enable=1 is what
# a GbE-equipped board wants regardless (the Gerrit 94032 port reports the
# I218-V working, with unstated blob provenance -- if that was ever true
# unpatched, enabling is still correct, merely redundant).
COREBOOT_USE_DONOR_BLOBS ??= "1"
COREBOOT_REFCODE_GBE_PATCH ??= "1"

# NOTE: no PXE/option-ROM plumbing here any more. iPXE is built by
# payloads/external/iPXE and embedded in the edk2 payload FV as an FFS, driven
# by CONFIG_EDK2_ENABLE_IPXE in payload-edk2.config.
# The old approach put an iPXE PCI option ROM in CBFS as pci<vid>,<did>.rom via
# CONFIG_PXE_ROM; that cannot work for a LOM, because coreboot's pci_rom_run()
# returns early for any device that is not PCI_CLASS_DISPLAY_VGA, so the ROM is
# never loaded. Confirmed on hardware 2026-07-28: no PXE boot option appeared.

BLOBS_DIR = "3rdparty/blobs/mainboard/intel/nuc5i5ryb"
BLOBS_DEST = "${S}/${BLOBS_DIR}"

# This unit's factory flash regions, extracted from stock-bios.rom with
# `ifdtool -x` and byte-verified against the dump (2026-07-28). Embedding them
# (the CONFIG_HAVE_*_BIN block appended to .config below) makes
# build/coreboot.rom a complete 8 MiB image that is also valid for
# blank-chip/clip recovery -- scripts/nuc-spi.sh accepts it because everything
# below the BIOS region matches the stock backup byte-for-byte.
MAINBOARD_DIR = "src/mainboard/intel/nuc5i5ryb"

do_configure:prepend() {
    # The board port: plain sources, not a patch (see the SRC_URI comment).
    # file://mainboard unpacks the whole directory to ${WORKDIR}/mainboard.
    install -d "${S}/${MAINBOARD_DIR}"
    cp -a "${WORKDIR}/mainboard/." "${S}/${MAINBOARD_DIR}/"
    # Layer-only files, and docs that belong outside the mainboard directory.
    rm -rf "${S}/${MAINBOARD_DIR}/Documentation" "${S}/${MAINBOARD_DIR}/README.md"
    install -D -m 0644 "${WORKDIR}/mainboard/Documentation/nuc5i5ryb.md" \
        "${S}/Documentation/mainboard/intel/nuc5i5ryb.md"

    # file://blobs unpacks the whole directory to ${WORKDIR}/blobs. Stage all of
    # it: coreboot's *_BIN_PATH / *_FILE settings are paths relative to the
    # source top, so every blob the .config can reference has to live under
    # ${S}. Copying the directory wholesale (rather than naming each file) means
    # dropping a new blob into the layer -- gbe.bin, another vgabios -- needs no
    # recipe edit, only the matching CONFIG_* line.
    install -d "${BLOBS_DEST}"
    cp -a "${WORKDIR}/blobs/." "${BLOBS_DEST}/"
    # Layer-only documentation; not a blob.
    rm -f "${BLOBS_DEST}/README.md"
    chmod 0644 "${BLOBS_DEST}"/*
}

do_configure() {
    cat ${WORKDIR}/nuc5i7ryh.config > ${B}/.config

    if [ "${NUC_BIOS_PAYLOAD}" = "linuxboot" ]; then
        sed -e "s#@BZIMAGE@#${DEPLOY_DIR_IMAGE}/bzImage#" \
            -e "s#@INITRD@#${DEPLOY_DIR_IMAGE}/initramfs-u-root.cpio#" \
            ${WORKDIR}/payload-linuxboot.config >> ${B}/.config
    else
        cat ${WORKDIR}/payload-edk2.config >> ${B}/.config
    fi
    # The port expects the blobs under 3rdparty/blobs/mainboard/<board>/
    # (the HAVE_MRC/HAVE_REFCODE_BLOB default paths).
    install -d ${BLOBS_DEST}
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
# After do_configure, not do_patch: do_configure:prepend stages the layer's
# whole blobs/ directory into ${BLOBS_DEST}, which includes mrc.bin and
# refcode.elf. Both tasks write those two paths, and "after do_patch" left them
# unordered -- bitbake ran them concurrently, so the donor's GbE-patched
# refcode could be overwritten by (or interleaved with) the layer's unpatched
# copy, silently disabling the I218-V. Ordering it here makes the layer copies
# the baseline and the donor pair authoritative whenever donor mode is on.
addtask extract_blobs after do_configure before do_compile

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
}

# Firmware is not target userspace: the ROM embeds its own everything.
INHIBIT_DEFAULT_DEPS = "1"
