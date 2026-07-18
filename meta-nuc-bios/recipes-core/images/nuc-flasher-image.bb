SUMMARY = "Minimal bootable live Linux to flash coreboot onto the NUC5i7RYH"
DESCRIPTION = "An intel-corei7-64 live ISO carrying flashrom and the coreboot \
ROM. Attach it to the NUC as JetKVM virtual media (CD), boot it, and run \
backup-bios then flash-coreboot. Root has no password; sshd + DHCP come up \
so the stock backup can be scp'd off (the rootfs is RAM-only)."
LICENSE = "MIT"

inherit core-image

# Live EFI ISO. The live class builds an initramfs
# (core-image-minimal-initramfs) that finds and mounts the CD, then pivots to
# this rootfs held in RAM. EFI_PROVIDER = systemd-boot matches the
# intel-corei7-64 machine default (its systemd-bootx64.efi is already built
# and deployed); the NUC5i7RYH boots UEFI, so an EFI-only El Torito ISO is
# what JetKVM virtual media needs. (grub-efi would give a BIOS+EFI hybrid but
# is not built for this machine and would add a bootloader build.)
IMAGE_FSTYPES = "iso"
EFI_PROVIDER = "systemd-boot"
do_bootimg[depends] += "systemd-boot:do_deploy"

# Root shell with no password (this is a throwaway flashing environment) plus
# an ssh server for pulling the stock backup off over the NUC's own NIC.
IMAGE_FEATURES += "debug-tweaks ssh-server-dropbear"

# --- Flashing payload ---
#   flashrom      : the internal-programmer flash tool
#   pciutils      : lspci, and libpci flashrom's internal programmer needs
#   nuc-flasher   : the backup/flash helper scripts + motd + auto-DHCP
#   nuc-coreboot-rom : /opt/coreboot/coreboot-nuc5i7ryh.rom (from the default mc)
IMAGE_INSTALL:append = " \
    flashrom \
    pciutils \
    nuc-flasher \
    nuc-coreboot-rom \
    "

# --- Interactive/rescue tooling on the console ---
IMAGE_INSTALL:append = " \
    util-linux \
    e2fsprogs \
    dosfstools \
    openssh-sftp-server \
    busybox-udhcpc \
    kmod \
    pciutils-ids \
    "

# Keep it lean-ish; no package management on a throwaway image.
IMAGE_FEATURES:remove = "package-management"

# --- Size: strip linux-firmware (1.2 GB of a 1.4 GB rootfs) ---
# The intel-corei7-64 machine RRECOMMENDS the full linux-firmware. The
# flasher needs none of it: flashrom drives the SPI directly, and the NUC's
# SATA/USB/e1000e work without blobs. Drop just that recommend (NOT all
# recommends -- kernel-modules is a recommend too and the live ISO must keep
# it), and pull only the small Broadwell iGPU blob for the console. This
# takes the ISO from ~2.2 GB to well under 200 MB -- critical for mounting
# over JetKVM virtual media.
BAD_RECOMMENDATIONS += "linux-firmware"
# kernel-modules (72 MiB) stays for boot/hardware coverage (xhci/usb-storage
# to see the JetKVM virtual media, e1000e for scp). No linux-firmware at all:
# the NUC's console comes up on the EFI framebuffer (efifb/simpledrm) without
# the i915 DMC blob, and nothing else here needs firmware.
IMAGE_INSTALL:append = " kernel-modules"

# The live rootfs is mounted read-only with a tmpfs overlay, so it needs no
# growth room -- runtime writes (the stock backup) land in RAM, not here.
# Trim the default 1.3x overhead to a slim 1.15x (1.0x leaves no room for
# ext4 metadata and mkfs fails to allocate blocks); no extra space. Takes
# the rootfs.ext4 from ~445 MiB to ~205 MiB.
IMAGE_OVERHEAD_FACTOR = "1.15"
IMAGE_ROOTFS_EXTRA_SPACE = "0"
