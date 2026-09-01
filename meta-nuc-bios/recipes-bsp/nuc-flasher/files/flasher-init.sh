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
