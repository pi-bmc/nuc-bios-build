#!/bin/sh
# stage-usb.sh -- copy the built NUC Redfish EFI driver set onto a mounted
# FAT/ESP USB volume and verify it, ready for install-drivers.nsh.
#
# Usage:
#   stage-usb.sh -d /run/media/me/NUCUSB            # src defaults to ./ (this dir)
#   stage-usb.sh -s <deploy>/efi-drivers -d /mnt/usb
#
# It does NOT format or repartition -- point -d at an already-mounted FAT32
# (or the NUC ESP). Lays the files out as:
#   <dest>/EFI/BOOT/drivers/*.efi
#   <dest>/EFI/BOOT/drivers/SHA256SUMS
#   <dest>/EFI/BOOT/drivers/install-drivers.nsh
set -eu

SELFDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SRC="$SELFDIR"          # default: the folder this script lives in
DEST=""

while getopts "s:d:h" opt; do
    case "$opt" in
        s) SRC=$OPTARG ;;
        d) DEST=$OPTARG ;;
        h) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "bad option; -h for help" >&2; exit 2 ;;
    esac
done

[ -n "$DEST" ] || { echo "error: -d <mounted-usb-dir> is required (-h for help)" >&2; exit 2; }
[ -d "$DEST" ] || { echo "error: dest '$DEST' is not a directory / not mounted" >&2; exit 2; }
[ -f "$SRC/SHA256SUMS" ] || { echo "error: no SHA256SUMS in src '$SRC' -- point -s at the deployed efi-drivers dir" >&2; exit 2; }

TGT="$DEST/EFI/BOOT/drivers"
mkdir -p "$TGT"

echo "Copying .efi + manifest from $SRC -> $TGT"
cp -f "$SRC"/*.efi "$TGT"/
cp -f "$SRC/SHA256SUMS" "$TGT"/
# install-drivers.nsh ships next to this script in the recipe; also next to the
# .efi if do_deploy staged it. Take whichever exists.
if [ -f "$SELFDIR/install-drivers.nsh" ]; then
    cp -f "$SELFDIR/install-drivers.nsh" "$TGT"/
elif [ -f "$SRC/install-drivers.nsh" ]; then
    cp -f "$SRC/install-drivers.nsh" "$TGT"/
else
    echo "warn: install-drivers.nsh not found next to script or src; copy it manually" >&2
fi

echo "Verifying checksums on the USB..."
( cd "$TGT" && sha256sum -c SHA256SUMS ) || { echo "CHECKSUM MISMATCH -- do not use this USB" >&2; exit 1; }

sync
N=$(ls -1 "$TGT"/*.efi | wc -l)
echo "OK: staged $N .efi to $TGT and verified."
cat <<EOF

Next, on the NUC (one time):
  BIOS/F2:  Secure Boot = Disabled
            "Allow UEFI 3rd party driver loaded" = Enabled   (AMI Setup 0x5B)
            Internal UEFI Shell = Enabled   (AMI Setup 0x1C; gives you a shell
                                             in the F10 boot menu -- no shell
                                             binary needed on the USB)
            Fast Boot = Disabled
  Reboot -> F10 -> Internal UEFI Shell, then:
      Shell> map -r
      Shell> fsN:                       # the FSx that is this USB
      FSN:\\> cd \\EFI\\BOOT\\drivers
      FSN:\\EFI\\BOOT\\drivers\\> install-drivers.nsh
      FSN:\\EFI\\BOOT\\drivers\\> reset
EOF
