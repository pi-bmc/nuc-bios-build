SUMMARY = "coreboot ROM delivery volume for JetKVM virtual media"
DESCRIPTION = "Assembles coreboot-rom.img with wic (do_image_wic): an MBR \
               disk whose single 34 MiB FAT32 partition carries this build's \
               coreboot-nuc5i7ryh.rom. Attach it as JetKVM virtual media (or \
               dd it to a USB stick) so the ROM can be picked up from a live \
               Linux on the NUC and written in-band -- \
\
                   mount /dev/sdX1 /mnt \
                   flashrom -p internal --ifd -i bios -w \
                       /mnt/coreboot-nuc5i7ryh.rom \
\
               -- coreboot leaves the SPI flash unlocked \
               (BOOTMEDIA_LOCK_NONE), so no SOIC-8 clip is needed once \
               coreboot is running. Replaces scripts/make-rom-img.sh. \
\
               An image recipe purely to reuse do_image_wic, for the same \
               reason as the rpi5 image recipes: wic run by hand inside a \
               task deadlocks on the cooker lock ('bitbake -e' from within a \
               task). The OS rootfs this recipe builds is empty and unused; \
               the partition is populated from the rom-staging tree passed \
               to wic as the default --rootfs-dir."

BUGTRACKER = "https://github.com/pi-bmc/nuc-bios-build/issues"
SECTION = "firmware"
LICENSE = "MIT"
COMPATIBLE_MACHINE = "nuc5i7ryh"

# Raw image: the deliverable is attached as virtual media or dd'd as-is, and
# at 35 MiB (8 MiB of it the ROM) compression buys little.
IMAGE_FSTYPES = "wic"
IMAGE_INSTALL = ""
IMAGE_FEATURES = ""
IMAGE_LINGUAS = ""
PACKAGE_INSTALL = ""
# Nothing here should end up in a package feed or need one.
NO_RECOMMENDATIONS = "1"

# No kernel goes into this image and the machine's virtual/kernel is
# linux-dummy (see nuc5i7ryh.conf); drop image.bbclass's default
# virtual/kernel dependencies (do_rootfs -> do_packagedata, do_build ->
# do_deploy) rather than build even the dummy for nothing.
KERNELDEPMODDEPEND = ""
KERNEL_DEPLOY_DEPEND = ""

# wic's always-added native tools (parted/gptfdisk/dosfstools/mtools) cover
# this kickstart; the x86-64 WKS_FILE_DEPENDS default would additionally
# build syslinux, grub-efi and systemd-boot for bootloader source plugins it
# does not use.
WKS_FILE_DEPENDS = ""

# A .wks edit rebuilds this image with no help from the recipe:
# image_types_wic.bbclass resolves WKS_FILE through WKS_SEARCH_PATH into
# WKS_FULL_PATH and lists it under do_image_wic[file-checksums], so the
# kickstart's content hash is part of the task signature.
WKS_FILE = "nuc-rom.wks"
WKS_SEARCH_PATH = "${THISDIR}/files/wic"

# The partition content is the rom-staging dir (passed as the default
# ROOTFS_DIR the .wks references), not the empty OS rootfs.
WIC_CREATE_EXTRA_ARGS = "--rootfs-dir ${WORKDIR}/rom-staging"

inherit image

ROM_STAGING = "${WORKDIR}/rom-staging"

do_stage_rom[depends] += "coreboot:do_deploy"
do_stage_rom[doc] = "Stage this build's coreboot ROM into the staging tree"
do_stage_rom () {
    staging="${ROM_STAGING}"
    rm -rf "${staging}"
    install -d "${staging}"

    rom="${DEPLOY_DIR_IMAGE}/coreboot-nuc5i7ryh.rom"
    if [ ! -f "${rom}" ]; then
        bbfatal "No coreboot-nuc5i7ryh.rom in ${DEPLOY_DIR_IMAGE}: rebuild coreboot (its do_deploy ships it)."
    fi

    # A blob-less compile check links a ROM that cannot boot; a delivery
    # volume carrying one must fail here, loudly, not build as a dud disk.
    if [ -f "${rom}.NOT-BOOTABLE" ]; then
        bbfatal "${rom} was built without mrc.bin/refcode.elf -- compile check only, nothing bootable to deliver."
    fi

    rom_bytes=$(stat -c %s "${rom}")
    if [ "${rom_bytes}" != "8388608" ]; then
        bbfatal "${rom} is ${rom_bytes} bytes, expected 8388608 -- not a full-chip image."
    fi

    install -m 0644 "${rom}" "${staging}/coreboot-nuc5i7ryh.rom"
}
addtask stage_rom after do_rootfs before do_image_wic

# Deploy the stable name: coreboot-rom.img.
rename_wic_rom[doc] = "Rename the wic image to the stable ROM-volume name"
rename_wic_rom () {
    cp --dereference "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.wic" \
        "${IMGDEPLOYDIR}/coreboot-rom.img"
}

do_image_wic[postfuncs] += "rename_wic_rom"
