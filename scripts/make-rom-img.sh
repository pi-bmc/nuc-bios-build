#!/usr/bin/env bash
# Build a minimal MBR disk image holding a single FAT32 partition whose only
# content is the coreboot ROM. Intended as JetKVM virtual media (or a USB
# stick) so the ROM can be picked up from a live Linux on the NUC and written
# in-band -- coreboot leaves the SPI flash unlocked (BOOTMEDIA_LOCK_NONE), so
# no SOIC-8 clip is needed once coreboot is running:
#
#     mount /dev/sdX1 /mnt
#     flashrom -p internal --ifd -i bios -w /mnt/coreboot-nuc5i7ryh.rom
#
#   ./scripts/make-rom-img.sh [rom] [out.img]
#
# Fully rootless: mkfs.vfat + mcopy write the filesystem into a plain file and
# sfdisk writes the partition table, so no loop device or mount is involved.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP="$(dirname "${HERE}")"

ROM="${1:-${TOP}/build/tmp/deploy/images/nuc5i7ryh/coreboot-nuc5i7ryh.rom}"
OUT="${2:-${TOP}/coreboot-rom.img}"
LABEL="${LABEL:-COREBOOT}"

# FAT32 needs at least 65525 clusters or it is not legally FAT32. With 512-byte
# clusters (-s 1) that floor is ~33 MiB; 34 MiB leaves a little slack for the
# FATs and root directory while keeping the image small. The partition starts at
# 1 MiB, the conventional alignment.
FS_MIB=34
OFFSET_MIB=1

die() { echo "error: $*" >&2; exit 1; }

[ -f "${ROM}" ] || die "ROM not found: ${ROM} (run: kas build kas.yml)"
[ -f "${ROM}.NOT-BOOTABLE" ] && \
    die "${ROM} was built without mrc.bin/refcode.elf -- compile check only"

rom_bytes=$(stat -c %s "${ROM}")
[ "${rom_bytes}" = 8388608 ] || \
    die "${ROM} is ${rom_bytes} bytes, expected 8388608 -- not a full-chip image"

FS_IMG="$(mktemp -t coreboot-fs.XXXXXX.img)"
trap 'rm -f "${FS_IMG}"' EXIT

echo ">> FAT32 filesystem (${FS_MIB} MiB, label ${LABEL})"
truncate -s "${FS_MIB}M" "${FS_IMG}"
mkfs.vfat -F 32 -s 1 -n "${LABEL}" "${FS_IMG}" >/dev/null

echo ">> copying $(basename "${ROM}") ($(numfmt --to=iec "${rom_bytes}"))"
mcopy -i "${FS_IMG}" "${ROM}" "::/$(basename "${ROM}")"

echo ">> assembling MBR image"
rm -f "${OUT}"
truncate -s "$(( (OFFSET_MIB + FS_MIB) * 1024 * 1024 ))" "${OUT}"
# type 0c = W95 FAT32 (LBA)
printf 'label: dos\nstart=%dMiB, size=%dMiB, type=0c\n' "${OFFSET_MIB}" "${FS_MIB}" \
    | sfdisk --quiet "${OUT}" >/dev/null
dd if="${FS_IMG}" of="${OUT}" bs=1M seek="${OFFSET_MIB}" conv=notrunc status=none

echo
echo "OK: ${OUT} ($(numfmt --to=iec "$(stat -c %s "${OUT}")"))"
sfdisk --list "${OUT}" | sed -n '/Device/,$p' | sed 's/^/  /'
echo "  contents:"
mdir -i "${FS_IMG}" -/ :: | sed 's/^/    /'
echo
echo "sha256: $(sha256sum "${OUT}" | cut -d' ' -f1)"
