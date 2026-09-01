#!/usr/bin/env bash
# Build a self-flashing UEFI disk image: a stock Alpine kernel + a bundled
# static busybox + flashrom + THIS build's coreboot ROM, wrapped as a single
# EFI unified kernel image (UKI). Booted on the NUC (nanokvm virtual media or
# a USB stick, UEFI/x64), its init verifies the BIOS region and:
#
#   * already this ROM      -> reboots (so leaving it attached never loops)
#   * differs               -> flashes the BIOS region only, then reboots
#   * write fails           -> drops to a shell and does NOT reboot
#
# Internal flashing needs no SOIC clip because the running coreboot leaves the
# SPI unlocked (BOOTMEDIA_LOCK_NONE). A machine still on the stock Intel BIOS
# (SMM_BWP set) cannot self-flash -- use scripts/nuc-spi.sh with the clip once.
#
#   ./scripts/make-flasher-img.sh [rom] [out.img]
#
# We download a stock kernel/initramfs + apks rather than build a kernel: the
# flasher needs a generic x86_64 UEFI kernel with USB/NVMe/AHCI, which Alpine's
# -lts already is. Pinned versions below; bump them together. Host tools:
# curl, objcopy (binutils), mkfs.vfat/mcopy/mdel/mmd (dosfstools+mtools),
# sfdisk (util-linux), cpio, gzip, and systemd-boot's linuxx64.efi.stub.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP="$(dirname "${HERE}")"

ROM="${1:-${TOP}/build/tmp/deploy/images/nuc5i7ryh/coreboot-nuc5i7ryh.rom}"
OUT="${2:-${TOP}/nuc-bios-flasher.img}"

ALPINE_REL="${ALPINE_REL:-v3.22}"
CDN="${CDN:-https://dl-cdn.alpinelinux.org/alpine}"
STUB="${STUB:-/usr/lib/systemd/boot/efi/linuxx64.efi.stub}"
# Pinned apks (x86_64, ${ALPINE_REL}); the flashrom dependency closure is all
# musl, so no other libs are needed. Bump as a set when ALPINE_REL moves.
KVER="lts"
APKS_MAIN="musl-1.2.5-r12 pciutils-libs-3.13.0-r1 libusb-1.0.28-r0 busybox-static-1.37.0-r20"
APKS_COMMUNITY="flashrom-1.5.1-r0 libftdi1-1.5-r4"

# The systemd stub's PE image base is ~0x14df90000 with sections up to
# ~0x14dfb4000; the payload sections must sit ABOVE that or objcopy places
# them "below image base" and the firmware refuses to load the UKI.
VMA_CMDLINE="0x14dfb5000"
VMA_LINUX="0x14e000000"
VMA_INITRD="0x150000000"

die() {
  echo "error: $*" >&2
  exit 1
}

[ -f "${ROM}" ] || die "ROM not found: ${ROM} (run: kas build kas.yml)"
[ -f "${ROM}.NOT-BOOTABLE" ] && die "${ROM} is a compile-check build (no mrc.bin/refcode.elf)"
[ "$(stat -c %s "${ROM}")" = 8388608 ] || die "${ROM} is not a full 8 MiB image"
[ -f "${STUB}" ] || die "systemd EFI stub not found at ${STUB} (install systemd-boot / set STUB=)"
for t in curl objcopy mkfs.vfat mcopy mdel mmd sfdisk cpio gzip; do
  command -v "$t" >/dev/null || die "missing host tool: $t"
done

WORK="$(mktemp -d -t nuc-flasher.XXXXXX)"
trap 'rm -rf "${WORK}"' EXIT
DL="${WORK}/dl"
RD="${WORK}/rootfs"
mkdir -p "${DL}" "${RD}"

echo ">> fetching Alpine ${ALPINE_REL} kernel + apks"
fetch() { curl -fsSL -o "${DL}/$2" "$1" || die "download failed: $1"; }
fetch "${CDN}/${ALPINE_REL}/releases/x86_64/netboot/vmlinuz-${KVER}" "vmlinuz"
fetch "${CDN}/${ALPINE_REL}/releases/x86_64/netboot/initramfs-${KVER}" "initramfs"
for a in ${APKS_MAIN}; do fetch "${CDN}/${ALPINE_REL}/main/x86_64/${a}.apk" "${a}.apk"; done
for a in ${APKS_COMMUNITY}; do fetch "${CDN}/${ALPINE_REL}/community/x86_64/${a}.apk" "${a}.apk"; done

echo ">> unpacking flasher userland into the overlay"
for a in "${DL}"/*.apk; do tar -xzf "$a" -C "${RD}" 2>/dev/null || true; done
rm -f "${RD}"/.PKGINFO "${RD}"/.SIGN.* 2>/dev/null || true
[ -f "${RD}/usr/sbin/flashrom" ] || die "flashrom missing from overlay after apk unpack"
[ -f "${RD}/bin/busybox.static" ] || die "busybox-static missing from overlay after apk unpack"
mkdir -p "${RD}/bbin"
mv "${RD}/bin/busybox.static" "${RD}/bbin/busybox"
chmod 755 "${RD}/bbin/busybox"

echo ">> staging ROM"
mkdir -p "${RD}/rom"
install -m 0644 "${ROM}" "${RD}/rom/coreboot-nuc5i7ryh.rom"
sha256sum "${ROM}" | cut -d' ' -f1 >"${RD}/rom/coreboot-nuc5i7ryh.rom.sha256"

# init: everything runs through the bundled static busybox, so the base
# initramfs applet set is never relied on. See the header for the flow.
cat >"${RD}/init" <<'INIT'
#!/bbin/busybox sh
BB=/bbin/busybox
$BB mkdir -p /proc /sys /dev /tmp /bin
$BB --install -s /bin
export PATH=/usr/sbin:/usr/bin:/sbin:/bin:/bbin
mount -t proc     none /proc 2>/dev/null
mount -t sysfs    none /sys  2>/dev/null
mount -t devtmpfs none /dev  2>/dev/null

# The node exposes no serial; the only console an operator sees is the HDMI
# framebuffer the nanokvm captures (tty0). Route everything there directly --
# no dependence on console= ordering, and flashrom's own output follows.
exec > /dev/tty0 2>&1 < /dev/tty0

# The MX25L6405 answers to several flashrom chip definitions with identical
# JEDEC IDs; flashrom refuses to act until one is named (see scripts/nuc-spi.sh).
CHIP=MX25L6405
ROM=/rom/coreboot-nuc5i7ryh.rom
read WANT < "${ROM}.sha256"

# Two things are required to touch this chip from the running system, and
# flashrom prints both when it hits the locked ME:
#   1. an explicit layout with only the accessible region (bios) -- at
#      runtime the PCH cannot read the locked ME region (FREG2), so flashrom
#      must never address it. --ifd would read the whole descriptor space
#      (from 0x0) and fault on the ME. The bounds are the PCH FREG1 values
#      nuc-spi.sh's ifd-layout.py emits.
#   2. --noverify-all -- otherwise flashrom read-verifies the WHOLE chip
#      after writing, which reads the ME and faults. --noverify-all confines
#      the read-back to the written region (bios); the layout confines the
#      write. (--noverify-all still verifies bios -- only the other regions
#      are skipped.)
# The write is idempotent: flashrom prints "contents identical" and no-ops
# when the region already matches, so a single -w handles both cases.
BIOS_START=0x001a0000
BIOS_END=0x007fffff
printf '%s:%s bios\n' "$BIOS_START" "$BIOS_END" > /tmp/layout

echo
echo "==== nuc-bios-flasher ===="
echo "ROM sha256: ${WANT}"
echo "region:     bios ${BIOS_START}-${BIOS_END} (descriptor/ME/GbE untouched)"
echo
echo "Flashing (idempotent -- no-ops if already current)..."
echo
if flashrom -p internal -c "$CHIP" -l /tmp/layout -i bios --noverify-all -w "$ROM"; then
    echo
    echo "FLASH OK (written and verified, or already current)."
    echo "Rebooting in 5s. Detach the flasher media for a normal boot."
    sleep 5
    reboot -f
    sleep 10
fi

echo
echo "!!!! FLASH FAILED -- DO NOT POWER OFF OR RESET THE MACHINE !!!!"
echo "A partial BIOS write only becomes fatal at the next reset. The old"
echo "firmware still runs until then. Retry from this shell:"
echo "    flashrom -p internal -c $CHIP -l /tmp/layout -i bios --noverify-all -w $ROM"
echo "and only reboot once it prints VERIFIED."
exec sh
INIT
chmod 755 "${RD}/init"

echo ">> building UKI (kernel + overlay-augmented initramfs)"
(cd "${RD}" && find . | cpio -o -H newc --quiet | gzip -9) >"${WORK}/overlay.cpio.gz"
# Concatenated cpios: the kernel extracts each in turn, later entries winning,
# so the overlay's /init replaces Alpine's. Mixed compression is fine.
cat "${DL}/initramfs" "${WORK}/overlay.cpio.gz" >"${WORK}/initrd.img"
# Both consoles: tty0 first (kernel log on the nanokvm HDMI), ttyS0 last so
# /dev/console is the serial port -- init's tee fans to tty0 AND /dev/console,
# so HDMI shows progress on the node while a serial hookup/QEMU sees it too.
printf 'console=tty0 iomem=relaxed' >"${WORK}/cmdline.txt"
objcopy \
  --add-section .cmdline="${WORK}/cmdline.txt" --change-section-vma .cmdline=${VMA_CMDLINE} \
  --add-section .linux="${DL}/vmlinuz" --change-section-vma .linux=${VMA_LINUX} \
  --add-section .initrd="${WORK}/initrd.img" --change-section-vma .initrd=${VMA_INITRD} \
  "${STUB}" "${WORK}/BOOTX64.EFI"

echo ">> assembling ESP disk image"
FS_MIB=60
OFFSET_MIB=1
FS="${WORK}/fs.img"
truncate -s "${FS_MIB}M" "${FS}"
mkfs.vfat -F 32 -n NUCFLASH "${FS}" >/dev/null
mmd -i "${FS}" ::/EFI ::/EFI/BOOT
mcopy -i "${FS}" "${WORK}/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
rm -f "${OUT}"
truncate -s "$(((OFFSET_MIB + FS_MIB) * 1024 * 1024))" "${OUT}"
# type ef = EFI System Partition.
printf 'label: dos\nstart=%dMiB, size=%dMiB, type=ef\n' "${OFFSET_MIB}" "${FS_MIB}" |
  sfdisk --quiet "${OUT}" >/dev/null
dd if="${FS}" of="${OUT}" bs=1M seek="${OFFSET_MIB}" conv=notrunc status=none

echo
echo "OK: ${OUT} ($(numfmt --to=iec "$(stat -c %s "${OUT}")"))"
echo "     ROM inside: $(cat "${RD}/rom/coreboot-nuc5i7ryh.rom.sha256")"
echo "     sha256:     $(sha256sum "${OUT}" | cut -d' ' -f1)"
echo
echo "Boot it on the NUC over UEFI (nanokvm virtual media or a USB stick)."
echo "It self-flashes and reboots. Detach the media after the reboot."
