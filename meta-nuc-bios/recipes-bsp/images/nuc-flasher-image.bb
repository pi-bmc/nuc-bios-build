SUMMARY = "Self-flashing BIOS updater disk image for the NUC"
DESCRIPTION = "Assembles nuc-bios-flasher.img with wic (do_image_wic): an \
               MBR disk whose single 60 MiB FAT32 EFI System Partition \
               carries the nuc-flasher-uki UKI at EFI/BOOT/BOOTX64.EFI, the \
               removable-media default boot path. Booted on the NUC (JetKVM \
               virtual media or a USB stick, UEFI/x64) it verifies the BIOS \
               region, flashes it if it differs, and reboots -- see \
               nuc-flasher-uki for the flow and its failure behaviour. \
               Replaces scripts/make-flasher-img.sh. \
\
               An image recipe purely to reuse do_image_wic, for the same \
               reason as the rpi5 image recipes: wic run by hand inside a \
               task deadlocks on the cooker lock ('bitbake -e' from within a \
               task). The OS rootfs this recipe builds is empty and unused; \
               the partition is populated from the flasher-staging tree \
               passed to wic as the default --rootfs-dir."

BUGTRACKER = "https://github.com/pi-bmc/nuc-bios-build/issues"
SECTION = "firmware"
LICENSE = "MIT"
COMPATIBLE_MACHINE = "nuc5i7ryh"

# Raw image: the deliverable is attached as virtual media or dd'd as-is.
IMAGE_FSTYPES = "wic"
IMAGE_INSTALL = ""
IMAGE_FEATURES = ""
IMAGE_LINGUAS = ""
PACKAGE_INSTALL = ""
# Nothing here should end up in a package feed or need one.
NO_RECOMMENDATIONS = "1"

# No kernel goes into this image (its Alpine kernel arrives inside the UKI,
# a deploy artifact) and the machine's virtual/kernel is linux-dummy (see
# nuc5i7ryh.conf); drop image.bbclass's default virtual/kernel dependencies
# (do_rootfs -> do_packagedata, do_build -> do_deploy) rather than build
# even the dummy for nothing.
KERNELDEPMODDEPEND = ""
KERNEL_DEPLOY_DEPEND = ""

# wic's always-added native tools (parted/gptfdisk/dosfstools/mtools) cover
# this kickstart; the x86-64 WKS_FILE_DEPENDS default would additionally
# build syslinux, grub-efi and systemd-boot for bootloader source plugins it
# does not use. (The systemd EFI stub the UKI is wrapped in is a pinned
# artifact nuc-flasher-uki fetches, not a recipe.)
WKS_FILE_DEPENDS = ""

# A .wks edit rebuilds this image with no help from the recipe:
# image_types_wic.bbclass resolves WKS_FILE through WKS_SEARCH_PATH into
# WKS_FULL_PATH and lists it under do_image_wic[file-checksums], so the
# kickstart's content hash is part of the task signature.
WKS_FILE = "nuc-flasher.wks"
WKS_SEARCH_PATH = "${THISDIR}/files/wic"

# The partition content is the flasher-staging dir (passed as the default
# ROOTFS_DIR the .wks references), not the empty OS rootfs.
WIC_CREATE_EXTRA_ARGS = "--rootfs-dir ${WORKDIR}/flasher-staging"

inherit image

FLASHER_STAGING = "${WORKDIR}/flasher-staging"

do_stage_flasher[depends] += "nuc-flasher-uki:do_deploy"
do_stage_flasher[doc] = "Stage the flasher UKI on the removable-media default boot path"
do_stage_flasher () {
    staging="${FLASHER_STAGING}"
    rm -rf "${staging}"
    install -d "${staging}/EFI/BOOT"

    if [ ! -f "${DEPLOY_DIR_IMAGE}/nuc-flasher-uki.efi" ]; then
        bbfatal "No nuc-flasher-uki.efi in ${DEPLOY_DIR_IMAGE}: rebuild nuc-flasher-uki (its do_deploy ships it)."
    fi

    install -m 0644 "${DEPLOY_DIR_IMAGE}/nuc-flasher-uki.efi" \
        "${staging}/EFI/BOOT/BOOTX64.EFI"
}
addtask stage_flasher after do_rootfs before do_image_wic

# Deploy the stable name: nuc-bios-flasher.img.
rename_wic_flasher[doc] = "Rename the wic image to the stable flasher name"
rename_wic_flasher () {
    cp --dereference "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.wic" \
        "${IMGDEPLOYDIR}/nuc-bios-flasher.img"
}

do_image_wic[postfuncs] += "rename_wic_flasher"
