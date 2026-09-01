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
# 0001-0003 are patches because they modify existing upstream files, which
# files/mainboard/ cannot express. They back the CFR "Processor" form (VT-x,
# VT-d, Hyper-Threading toggles; see files/mainboard/cfr.c): 0001 routes
# haswell's set_vmx_and_lock() through the "vmx" option, 0002 gates the DMAR
# table and the Broadwell VT-d BARs on "vtd", 0003 ports model_206ax's
# SOFT_RESET_DATA SMT strap to the haswell bootblock as "hyper_threading".
#
# do_extract_blobs pins the vboot submodule URL explicitly so it never
# depends on what bitbake set origin to.
SRC_URI = "${COREBOOT_GIT_URI} \
           file://0001-cpu-intel-haswell-honor-a-runtime-vmx-option.patch \
           file://0002-nb-intel-haswell-broadwell-honor-a-runtime-vtd-optio.patch \
           file://0003-cpu-intel-haswell-add-a-runtime-hyper_threading-opti.patch \
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
# own recipe.) openssl-native: GenerateCapsule.py shells out to openssl to
# sign the capsule built in do_deploy below.
DEPENDS = "bison-native flex-native python3-native util-linux-native nasm-native acpica-native coreboot-toolchain-native openssl-native"

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
        # The payload is built by the edk2-uefipayload recipe and deployed as
        # UEFIPAYLOAD.fd; substitute its absolute path (see the comment block
        # in payload-edk2.config for why PAYLOAD_EDK2 stays on).
        [ -e "${DEPLOY_DIR_IMAGE}/UEFIPAYLOAD.fd" ] || \
            bbfatal "no UEFIPAYLOAD.fd in ${DEPLOY_DIR_IMAGE} -- build edk2-uefipayload first"
        sed -e "s#@UEFIPAYLOAD@#${DEPLOY_DIR_IMAGE}/UEFIPAYLOAD.fd#" \
            ${WORKDIR}/payload-edk2.config >> ${B}/.config

        # --- firmware GUID drift guard ----------------------------------
        # This recipe holds two of the six hand-maintained copies of the
        # firmware image GUID: NUC_CAPSULE_GUID (which do_deploy passes to
        # GenerateCapsule as --guid) and CONFIG_DRIVERS_EFI_MAIN_FW_GUID in
        # payload-edk2.config (which coreboot writes into the ESRT). Neither
        # fails loudly when wrong -- the capsule simply matches no FMP at
        # runtime, or the ESRT advertises a device nothing updates. Check
        # them against each other, and against the finished payload: the
        # FmpDxe FFS this GUID names is in UEFIPAYLOAD.fd as 16 raw bytes,
        # so this also pins edk2-uefipayload's own NUC_CAPSULE_GUID and
        # patch 0035's INF FILE_GUID line end to end.
        python3 - "${NUC_CAPSULE_GUID}" "${WORKDIR}/payload-edk2.config" \
            "${DEPLOY_DIR_IMAGE}/UEFIPAYLOAD.fd" <<'GUIDCHECK'
import re, sys, uuid
guid, cfg, fd = sys.argv[1], sys.argv[2], sys.argv[3]
g = uuid.UUID(guid)
m = re.search(r'^CONFIG_DRIVERS_EFI_MAIN_FW_GUID="([^"]*)"',
              open(cfg, encoding="utf-8").read(), re.M)
if m is None:
    sys.exit("%s sets no CONFIG_DRIVERS_EFI_MAIN_FW_GUID -- coreboot would "
             "publish no ESRT entry for the firmware NUC_CAPSULE_GUID names." % cfg)
if m.group(1).lower() != str(g):
    sys.exit("NUC_CAPSULE_GUID drift: NUC_CAPSULE_GUID is %s but %s says %s."
             % (g, cfg, m.group(1)))
if open(fd, "rb").read().count(g.bytes_le) == 0:
    sys.exit("NUC_CAPSULE_GUID drift: %s carries no FMP image with GUID %s. "
             "edk2-uefipayload's own NUC_CAPSULE_GUID and the INF FILE_GUID "
             "line in 0035-UefiPayloadPkg-make-FmpDxe-FV-resident.patch must "
             "name the same value as this recipe's." % (fd, g))
GUIDCHECK
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

# --- UEFI capsule: RMAP manifest + signed .cap --------------------------
#
# This is the ONLY recipe with the finished ROM. edk2-uefipayload's
# do_deploy publishes UEFIPAYLOAD.fd, one of coreboot.rom's inputs; the
# assembled 8 MiB image (mainboard, blobs, the payload embedded via
# PAYLOAD_FILE) exists for the first time right here, in do_deploy below.
# A capsule built from the payload alone would be missing everything
# coreboot itself contributes, so capsule generation belongs in this
# recipe, not edk2-uefipayload_2605.bb.
#
# Without the RMAP manifest, FmpDeviceSmmLib falls back to a full-flash
# write -- its own header calls region selectivity future work -- which
# overwrites SMMSTORE and resets the variable store: boot entries, Secure
# Boot state, CFR settings, gone. AppendRmapManifest.py's own docstring
# states the fallback plainly: "If the manifest is absent, firmware falls
# back to full-flash updates." The manifest built below lists COREBOOT
# only -- SMMSTORE, RW_MRC_CACHE and FMAP stay out of every write range,
# which is what lets FmpDeviceSmmLib set VariableStorePreserved and keep
# the variable services (and therefore FmpDxe's LastAttemptStatus, which
# Task 5's scanner and Task 7's Redfish report both depend on) alive
# through an update. The guard below fails the build rather than let an
# unmanifested capsule out the door silently.
#
# GUID d25f89e1-94ec-4533-80b9-7f8855ce0a94 must stay identical in FOUR
# places: coreboot's own CONFIG_DRIVERS_EFI_MAIN_FW_GUID
# (payload-edk2.config), the payload's CAPSULE_MAIN_FW_GUID
# (edk2-uefipayload_2605.bb's NUC_CAPSULE_GUID), the capsule generated
# below, and the ESRT entry those two Kconfigs produce. A mismatch means
# the capsule silently matches no FMP at runtime.
NUC_CAPSULE_GUID ??= "d25f89e1-94ec-4533-80b9-7f8855ce0a94"
NUC_CAPSULE_VERSION ??= "1"
NUC_CAPSULE_LSV ??= "1"

# Signing identity -- MUST resolve to the same certificate
# edk2-uefipayload_2605.bb embedded into UefiPayloadPkg.dsc's
# PcdFmpDevicePkcs7CertBufferXdr at its do_configure time, or a capsule
# built here authenticates against a certificate FmpDxe does not trust and
# every update is silently refused at runtime (LastAttemptStatus records
# it; nothing at build time can catch a mismatch between two independently
# resolved identities). Defaults identical to edk2-uefipayload_2605.bb's,
# so the common case -- neither variable configured -- has both recipes
# independently arrive at the same generated keypair under
# NUC_CAPSULE_KEYDIR with nothing threaded between them.
NUC_CAPSULE_KEYDIR ??= "${TOPDIR}/nuc-capsule-keys"
NUC_CAPSULE_CERT ??= ""
NUC_CAPSULE_KEY ??= ""

# Where edk2-uefipayload_2605.bb's do_deploy staged AppendRmapManifest.py and
# the BaseTools/Source/Python tree GenerateCapsule.py needs (it is not
# self-contained -- it imports Common.Uefi.Capsule.* siblings).
#
# MUST be DEPLOY_DIR_IMAGE, not DEPLOYDIR: deploy.bbclass makes DEPLOYDIR a
# private per-task staging directory (${WORKDIR}/deploy-${PN}) that the
# class only publishes into the shared, machine-specific DEPLOY_DIR_IMAGE
# after the task finishes -- two sibling recipes' DEPLOYDIR values are two
# different directories that are never the same on disk while either task
# is running. This recipe's own do_configure already reads
# ${DEPLOY_DIR_IMAGE}/UEFIPAYLOAD.fd for exactly that reason, and
# nuc-coreboot-rom.bb reads coreboot-nuc5i7ryh.rom/UEFIPAYLOAD.fd/ipxe.rom
# the same way -- DEPLOY_DIR_IMAGE is this layer's one cross-recipe sharing
# convention, DEPLOYDIR never is. (Writes to ${DEPLOYDIR} elsewhere in this
# recipe's own do_deploy are correct as they stand: that is this recipe's
# own new output, which the class publishes into DEPLOY_DIR_IMAGE for
# everyone else once this task completes -- the same way
# coreboot-nuc5i7ryh.rom already worked before this file had a capsule
# step at all.)
EDK2_CAPSULE_TOOLS = "${DEPLOY_DIR_IMAGE}/edk2-capsule-tools"

# Refuse EDK2's own published test certificate chain
# (BaseTools/Source/Python/Pkcs7Sign) no matter how it got configured. Its
# private keys ship in every edk2 checkout, so a capsule signed against it
# validates on any board running this firmware -- shipping it defeats
# signing entirely.
nuc_capsule_reject_test_cert() {
    label="$1"; path="$2"
    resolved=$(readlink -f "$path" 2>/dev/null || echo "$path")
    case "$resolved" in
        */Pkcs7Sign/Test*)
            bbfatal "$label '$path' resolves to '$resolved' -- EDK2's own published test certificate chain. Configure NUC_CAPSULE_CERT/NUC_CAPSULE_KEY with a real identity, or leave both unset to use the keypair edk2-uefipayload_2605.bb generates under NUC_CAPSULE_KEYDIR."
            ;;
    esac
}

# Resolve $fmp_cert (DER) / $fmp_signer (PEM key+cert) for signing. Does NOT
# generate a keypair -- edk2-uefipayload_2605.bb's do_configure already must
# have run (do_configure[depends] below covers the ordering) and either
# used an operator-supplied identity or generated one under the same
# NUC_CAPSULE_KEYDIR default; if neither is configured and nothing was
# generated, that is a build-ordering problem, not something to paper over
# by generating a second, different keypair here.
nuc_capsule_resolve_keys() {
    fmp_cert="${NUC_CAPSULE_CERT}"
    fmp_signer="${NUC_CAPSULE_KEY}"

    if [ -z "$fmp_cert" ] && [ -z "$fmp_signer" ]; then
        fmp_keydir="${NUC_CAPSULE_KEYDIR}"
        for f in capsule.cer capsule.pem; do
            [ -e "$fmp_keydir/$f" ] || \
                bbfatal "no capsule signing key at $fmp_keydir/$f -- edk2-uefipayload's do_configure generates this keypair when NUC_CAPSULE_CERT/NUC_CAPSULE_KEY are unset. Build edk2-uefipayload before coreboot (the normal order; do_configure already depends on its do_deploy for UEFIPAYLOAD.fd), or set NUC_CAPSULE_CERT/NUC_CAPSULE_KEY here to wherever the signing identity actually lives."
        done
        fmp_cert="$fmp_keydir/capsule.cer"
        fmp_signer="$fmp_keydir/capsule.pem"
    fi

    [ -r "$fmp_cert" ] || bbfatal "NUC_CAPSULE_CERT '$fmp_cert' is not readable."
    if [ -n "$fmp_signer" ] && [ ! -r "$fmp_signer" ]; then
        bbfatal "NUC_CAPSULE_KEY '$fmp_signer' is not readable."
    fi

    nuc_capsule_reject_test_cert "NUC_CAPSULE_CERT" "$fmp_cert"
    if [ -n "$fmp_signer" ]; then
        nuc_capsule_reject_test_cert "NUC_CAPSULE_KEY" "$fmp_signer"
    fi
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/build/coreboot.rom ${DEPLOYDIR}/coreboot-nuc5i7ryh.rom
    rm -f ${DEPLOYDIR}/coreboot-nuc5i7ryh.rom.NOT-BOOTABLE
    if [ -z "${COREBOOT_BLOBS_DIR}" ] && [ "${COREBOOT_USE_DONOR_BLOBS}" != "1" ]; then
        echo "built without mrc.bin/refcode.elf -- compile check only, do not flash" \
            > ${DEPLOYDIR}/coreboot-nuc5i7ryh.rom.NOT-BOOTABLE
        bbnote "coreboot-nuc5i7ryh.rom is a blob-less compile check -- skipping capsule generation, there is nothing bootable to update to"
        return 0
    fi

    # --- Step 1: append the RMAP manifest and generate the capsule -------
    tools="${EDK2_CAPSULE_TOOLS}"
    [ -d "$tools" ] || \
        bbfatal "no capsule tooling at $tools -- build edk2-uefipayload before coreboot; do_configure already depends on its do_deploy for UEFIPAYLOAD.fd, and this needs the same ordering for its Tools/BaseTools staging."

    python3 "$tools/AppendRmapManifest.py" \
        --region COREBOOT \
        -o "${B}/coreboot-rmap.rom" "${DEPLOYDIR}/coreboot-nuc5i7ryh.rom"
    install -m 0644 ${B}/coreboot-rmap.rom ${DEPLOYDIR}/coreboot-nuc5i7ryh-rmap.rom

    # --- Step 2: guard the manifest ---------------------------------------
    # AppendRmapManifest.py silently produces a plain copy if it is ever
    # invoked wrong (or if a future edit here drops the --region argument):
    # its own docstring says the fallback plainly, "If the manifest is
    # absent, firmware falls back to full-flash updates." That must fail
    # the build, not warn.
    python3 - "${B}/coreboot-rmap.rom" <<'PY'
import struct, sys
p = sys.argv[1]
d = open(p, 'rb').read()
sig, ver, n = struct.unpack('<IHH', d[-8:])
assert sig == 0x50414D52, "RMAP signature missing -- capsule would silently full-flash"
assert n >= 1, "RMAP manifest is empty"
print("RMAP ok: version {}, {} region(s)".format(ver, n))
PY

    # --- Step 3: resolve and validate the signer --------------------------
    nuc_capsule_resolve_keys

    if [ -z "$fmp_signer" ]; then
        bbwarn "NUC_CAPSULE_CERT is set but NUC_CAPSULE_KEY is not, so no capsule was built -- the private key is not available to this build. Sign one offline against ${DEPLOYDIR}/coreboot-nuc5i7ryh-rmap.rom with edk2's BaseTools/Source/Python/Capsule/GenerateCapsule.py; the arguments must match this build's --guid ${NUC_CAPSULE_GUID} --fw-version ${NUC_CAPSULE_VERSION} --lsv ${NUC_CAPSULE_LSV}."
        return 0
    fi

    # GenerateCapsule wants PEM for --other-public-cert and
    # --trusted-public-cert, and both are mandatory even for a self-signed
    # certificate that is its own chain and its own anchor.
    other_pub="${B}/nuc-capsule-cert.pub.pem"
    openssl x509 -inform DER -in "$fmp_cert" -out "$other_pub" \
        || bbfatal "could not convert '$fmp_cert' to PEM for GenerateCapsule"

    # Run the script directly with PYTHONPATH set, rather than through the
    # BaseTools/BinWrappers/PosixLike/GenerateCapsule wrapper: the wrapper
    # is generated by edk2-uefipayload's own do_compile (`oe_runmake -C
    # BaseTools`) inside ITS WORKDIR, which this recipe does not reach into
    # (see the EDK2_CAPSULE_TOOLS comment above) -- only the staged
    # Source/Python tree travels through DEPLOYDIR.
    # --signing-tool-path pins openssl to this recipe's own native sysroot
    # (the DEPENDS += "openssl-native" above), the same binary every check
    # above used, instead of whatever openssl happens to be on PATH.
    PYTHONPATH="$tools/BaseTools-Source-Python" python3 \
        "$tools/BaseTools-Source-Python/Capsule/GenerateCapsule.py" \
        --encode --guid ${NUC_CAPSULE_GUID} \
        --fw-version ${NUC_CAPSULE_VERSION} --lsv ${NUC_CAPSULE_LSV} \
        --signer-private-cert "$fmp_signer" \
        --other-public-cert   "$other_pub" \
        --trusted-public-cert "$other_pub" \
        --signing-tool-path "${STAGING_BINDIR_NATIVE}" \
        -o "${DEPLOYDIR}/nuc-firmware.cap" "${B}/coreboot-rmap.rom" \
        || bbfatal "GenerateCapsule failed to build ${DEPLOYDIR}/nuc-firmware.cap"

    chmod 0644 "${DEPLOYDIR}/nuc-firmware.cap"
    bbnote "Built ${DEPLOYDIR}/nuc-firmware.cap: image type ${NUC_CAPSULE_GUID}, version ${NUC_CAPSULE_VERSION}, lsv ${NUC_CAPSULE_LSV}."
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
        # do_configure reads the deployed .fd path, so the dependency is on
        # configure rather than compile.
        d.appendVarFlag('do_configure', 'depends',
                        ' edk2-uefipayload:do_deploy')
        # do_deploy (capsule generation) reads AppendRmapManifest.py and the
        # BaseTools/Source/Python tree edk2-uefipayload's own do_deploy
        # stages into DEPLOY_DIR_IMAGE. That chain is already transitively
        # ordered after edk2-uefipayload:do_deploy via
        # do_configure -> do_compile -> "addtask deploy after do_compile"
        # above, so this line does not change *what* ends up ordered before
        # coreboot's do_deploy runs -- the actual bug that first broke this
        # was EDK2_CAPSULE_TOOLS pointing at DEPLOYDIR (this recipe's own
        # private per-task staging dir, which can never contain another
        # recipe's output) instead of DEPLOY_DIR_IMAGE. This is declared
        # explicitly anyway so the dependency do_deploy actually consumes is
        # not left to be inferred through do_configure's, which could
        # silently stop covering it if the task chain above ever changes.
        d.appendVarFlag('do_deploy', 'depends',
                        ' edk2-uefipayload:do_deploy')
}

# Firmware is not target userspace: the ROM embeds its own everything.
INHIBIT_DEFAULT_DEPS = "1"
