#!/bin/sh
# make-img.sh -- build a FAT .img holding the NUC Redfish EFI driver set at
# \EFI\BOOT\drivers\, ready to dd to a USB stick or attach as BMC virtual media.
#
# Rootless: uses mkfs.vfat (dosfstools) + mmd/mcopy (mtools). No loop-mount.
#
# Usage:
#   make-img.sh -o nuc-redfish-drivers.img -s <deploy>/efi-drivers
#   make-img.sh -o out.img -s ./ -m 32          # force 32 MiB
#
# Layout produced inside the image:
#   ::/EFI/BOOT/drivers/*.efi
#   ::/EFI/BOOT/drivers/install-drivers.nsh
#
# NOTE this image is a DATA volume (drivers only). To run bcfg you still need a
# UEFI Shell: enable the NUC's Internal UEFI Shell (AMI Setup 0x1C) and pick it
# from the F10 menu, or drop a shell at ::/EFI/BOOT/BOOTX64.EFI yourself.
set -eu
export MTOOLS_SKIP_CHECK=1

SELFDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=""
SRC="$SELFDIR"
SIZE_MB=0            # 0 = auto

while getopts "o:s:m:h" opt; do
    case "$opt" in
        o) OUT=$OPTARG ;;
        s) SRC=$OPTARG ;;
        m) SIZE_MB=$OPTARG ;;
        h) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "bad option; -h for help" >&2; exit 2 ;;
    esac
done

[ -n "$OUT" ] || { echo "error: -o <output.img> is required (-h for help)" >&2; exit 2; }
set -- "$SRC"/*.efi
[ -e "$1" ] || { echo "error: no .efi files in src '$SRC' -- point -s at the deployed efi-drivers dir" >&2; exit 2; }

# Auto-size: 4x the payload, rounded up to a whole MiB, floored at 16 MiB
# (FAT16 wants a comfortable cluster count; the payload is ~2 MiB so 16 is ample).
if [ "$SIZE_MB" -eq 0 ]; then
    BYTES=$(du -bc "$SRC"/*.efi "$SRC"/install-drivers.nsh 2>/dev/null | awk 'END{print $1}')
    SIZE_MB=$(( (BYTES * 4 + 1048575) / 1048576 ))
    [ "$SIZE_MB" -lt 16 ] && SIZE_MB=16
fi

echo "Building $OUT (${SIZE_MB} MiB FAT) from $SRC"
rm -f "$OUT"
dd if=/dev/zero of="$OUT" bs=1M count="$SIZE_MB" status=none
mkfs.vfat -n NUCRFSH "$OUT" >/dev/null

mmd -i "$OUT" ::/EFI ::/EFI/BOOT ::/EFI/BOOT/drivers
mcopy -i "$OUT" "$SRC"/*.efi ::/EFI/BOOT/drivers/
[ -f "$SRC/install-drivers.nsh" ] && mcopy -i "$OUT" "$SRC/install-drivers.nsh" ::/EFI/BOOT/drivers/

N=$(mdir -i "$OUT" -b ::/EFI/BOOT/drivers/ | grep -c '\.efi$' || true)
echo "OK: $OUT  ($(stat -c%s "$OUT") bytes, ${N} .efi at \\EFI\\BOOT\\drivers\\)"
echo "  dd to a stick:   dd if=$OUT of=/dev/sdX bs=4M conv=fsync"
echo "  or attach $OUT as BMC virtual media (USB MSD)."
