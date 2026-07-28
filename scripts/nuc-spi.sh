#!/usr/bin/env bash
# External SPI flashing for the NUC5i7RYH's Macronix MX25L6405, via a CH341A
# USB programmer and a SOIC-8 clip. This is the bootstrap path: the stock
# Intel BIOS sets SMM_BWP, so `flashrom -p internal` cannot write the chip.
# Once coreboot is running, internal flashing works and the clip is no longer
# needed. See docs/external-flashing.md for the full procedure and the
# hardware caveats (the CH341A 3.3 V mod in particular).
#
#   ./scripts/nuc-spi.sh probe                 identify the chip
#   ./scripts/nuc-spi.sh dump  [out.rom]       two verified reads -> backup
#   ./scripts/nuc-spi.sh info  <rom>           decode the flash descriptor
#   ./scripts/nuc-spi.sh merge <stock> [out]   full 8 MiB golden image
#   ./scripts/nuc-spi.sh flash [stock]         write the BIOS region only
#   ./scripts/nuc-spi.sh console [out.txt]     read coreboot's boot log
#   ./scripts/nuc-spi.sh restore <stock>       write the whole chip back
#
# Env: PROGRAMMER (default ch341a_spi), CHIP (passed to flashrom -c if set),
#      FLASHROM, ROM (path to coreboot-nuc5i7ryh.rom).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP="$(dirname "${HERE}")"

FLASHROM="${FLASHROM:-flashrom}"
PROGRAMMER="${PROGRAMMER:-ch341a_spi}"
ROM="${ROM:-${TOP}/build/tmp/deploy/images/nuc5i7ryh/coreboot-nuc5i7ryh.rom}"
IFD_LAYOUT="${HERE}/ifd-layout.py"

# The 8 MiB MX25L6405 and the BIOS region coreboot's board.fmd is built
# around. A descriptor that disagrees means the ROM must not be flashed.
CHIP_SIZE=8388608
EXPECT_BIOS="0x1a0000:0x7fffff"

# Scratch layout file, removed on exit. Deliberately script-scope rather than
# a local in cmd_flash: the EXIT trap fires after that function has returned,
# so a local would already be gone and `set -u` would abort the trap with
# "layout: unbound variable" -- which made a completed, VERIFIED flash exit
# non-zero and look like a failure.
LAYOUT_TMP=""
cleanup() {
    [ -n "${LAYOUT_TMP}" ] && rm -f "${LAYOUT_TMP}"
    return 0
}
trap cleanup EXIT

die() { echo "error: $*" >&2; exit 1; }

fr() {
    local args=("-p" "${PROGRAMMER}")
    [ -n "${CHIP:-}" ] && args+=("-c" "${CHIP}")
    echo "+ ${FLASHROM} ${args[*]} $*" >&2
    "${FLASHROM}" "${args[@]}" "$@"
}

require_size() {
    local f="$1" want="$2" actual
    actual=$(stat -c %s "$f")
    [ "${actual}" = "${want}" ] || \
        die "$f is ${actual} bytes, expected ${want} -- not a full-chip image"
}

# The coreboot build embeds no descriptor/ME/GbE (no CONFIG_HAVE_IFD_BIN), so
# everything below the BIOS region must still be erased padding. If it is not,
# someone changed the image layout and the region-limited write below is no
# longer the right operation.
require_bios_region_only() {
    local f="$1" base="$2"
    python3 - "$f" "$base" <<'PY'
import sys
path, base = sys.argv[1], int(sys.argv[2], 0)
with open(path, 'rb') as fh:
    head = fh.read(base)
if head.strip(b'\xff'):
    sys.exit("error: %s has non-0xff data below 0x%06x -- it is not a "
             "BIOS-region-only image. Refusing." % (path, base))
PY
}

bios_bounds() {  # echoes "base limit" from a dump's own descriptor
    python3 "${IFD_LAYOUT}" --expect-bios "${EXPECT_BIOS}" --layout "$1" \
        | awk '$2 == "bios" { split($1, a, ":"); print "0x" a[1], "0x" a[2] }'
}

cmd_probe() {
    fr --flash-name
}

cmd_dump() {
    local out="${1:-${TOP}/stock-bios.rom}"
    # Once coreboot is on the chip a re-dump captures coreboot, not the
    # factory image -- and the default filename is the one recovery depends
    # on. Never clobber it silently.
    if [ -e "${out}" ] && [ "${FORCE:-0}" != "1" ]; then
        die "${out} already exists. A dump taken after flashing contains coreboot, not the factory firmware -- overwriting would destroy the only recovery image. Pass a different filename, or FORCE=1 if you really mean it."
    fi
    echo ">> read 1/2 -> ${out}.1"
    fr --progress -r "${out}.1"
    echo ">> read 2/2 -> ${out}.2"
    fr --progress -r "${out}.2"

    if ! cmp -s "${out}.1" "${out}.2"; then
        echo >&2
        echo "The two reads differ: the SPI bus is unstable." >&2
        echo "In-circuit reads on a live board are the usual cause -- the PCH" >&2
        echo "loads the bus. Unplug everything, pull the CMOS cell, drain with" >&2
        echo "the power button, reseat the clip, and retry. If it never" >&2
        echo "settles, desolder the chip. Do NOT write anything." >&2
        exit 1
    fi

    mv "${out}.1" "${out}"
    rm -f "${out}.2"
    require_size "${out}" "${CHIP_SIZE}"
    echo
    echo "OK: two identical reads -> ${out}"
    python3 "${IFD_LAYOUT}" --expect-bios "${EXPECT_BIOS}" "${out}"
    echo
    echo "sha256: $(sha256sum "${out}" | cut -d' ' -f1)"
    echo "Keep this off-device. It is the only recovery path."
}

cmd_info() {
    [ $# -ge 1 ] || die "usage: nuc-spi.sh info <rom>"
    python3 "${IFD_LAYOUT}" "$1"
}

# Stock descriptor/GbE/ME + coreboot's BIOS region = the full-chip image that
# is actually on the flash after a successful write. Archive it; it is also
# what you would write with a plain full-chip -w if you ever want to skip the
# region machinery.
cmd_merge() {
    [ $# -ge 1 ] || die "usage: nuc-spi.sh merge <stock.rom> [out.rom]"
    local stock="$1" out="${2:-${TOP}/nuc5i7ryh-full.rom}"
    [ -f "${ROM}" ] || die "coreboot ROM not found at ${ROM} (run: kas build kas.yml)"
    require_size "${stock}" "${CHIP_SIZE}"
    require_size "${ROM}" "${CHIP_SIZE}"

    local base limit
    read -r base limit < <(bios_bounds "${stock}")
    require_bios_region_only "${ROM}" "${base}"

    python3 - "${stock}" "${ROM}" "${out}" "${base}" "${limit}" <<'PY'
import sys
stock, cb, out, base, limit = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4], 0), int(sys.argv[5], 0)
s = bytearray(open(stock, 'rb').read())
c = open(cb, 'rb').read()
s[base:limit + 1] = c[base:limit + 1]
open(out, 'wb').write(s)
print("merged coreboot 0x%06x-0x%06x into %s -> %s" % (base, limit, stock, out))
PY
    echo "sha256: $(sha256sum "${out}" | cut -d' ' -f1)"
}

cmd_flash() {
    local stock="${1:-${TOP}/stock-bios.rom}"
    [ -f "${ROM}" ] || die "coreboot ROM not found at ${ROM} (run: kas build kas.yml)"
    [ -f "${ROM}.NOT-BOOTABLE" ] && \
        die "${ROM} was built without mrc.bin/refcode.elf -- compile check only"
    [ -f "${stock}" ] || die "no stock backup at ${stock} -- run 'nuc-spi.sh dump' first"

    require_size "${ROM}" "${CHIP_SIZE}"
    require_size "${stock}" "${CHIP_SIZE}"

    local base limit
    read -r base limit < <(bios_bounds "${stock}")
    require_bios_region_only "${ROM}" "${base}"

    LAYOUT_TMP="$(mktemp -t nuc5i7ryh.XXXXXX.layout)"
    python3 "${IFD_LAYOUT}" --layout "${stock}" > "${LAYOUT_TMP}"

    echo
    echo "programmer : ${PROGRAMMER}${CHIP:+ (chip ${CHIP})}"
    echo "writing    : ${ROM}"
    echo "region     : bios ${base}-${limit} only"
    echo "backup     : ${stock} present"
    echo "layout     :"
    sed 's/^/             /' "${LAYOUT_TMP}"
    echo
    echo "The descriptor, ME and GbE regions are not touched -- the ME must stay"
    echo "in Normal mode or the port's PTT fTPM will not come up."
    read -r -p "Type YES to flash: " ans
    [ "${ans}" = "YES" ] || die "aborted"

    fr --progress -l "${LAYOUT_TMP}" -i bios -w "${ROM}"

    echo
    echo "OK: BIOS region written and verified. Remove the clip, reassemble,"
    echo "and power-cycle from a cold G3 state."
    echo "If it does not POST: ./scripts/nuc-spi.sh restore ${stock}"
}

# Recover coreboot's boot log from the CONSOLE FMAP region (needs
# CONFIG_CONSOLE_SPI_FLASH, set in nuc5i7ryh.config). This board routes no
# UART and cbmem -c needs a booted OS, so on a board that does not POST this
# is the only way to see how far coreboot got. Reads 128 KiB, not the whole
# chip, so it is quick.
cmd_console() {
    local out="${1:-${TOP}/coreboot-console.txt}"
    local raw="${out}.bin"
    fr --fmap -i "CONSOLE:${raw}" -r
    python3 - "${raw}" "${out}" <<'PY'
import sys
raw, out = sys.argv[1], sys.argv[2]
data = open(raw, 'rb').read().rstrip(b'\xff').rstrip(b'\x00')
text = data.decode('utf-8', 'replace')
open(out, 'w').write(text)
if not text.strip():
    sys.exit("CONSOLE region is empty -- either this ROM predates "
             "CONFIG_CONSOLE_SPI_FLASH, or coreboot never reached the point "
             "where it flushes the log.")
print(text)
PY
    echo
    echo "log also written to ${out}"
}

cmd_restore() {
    [ $# -ge 1 ] || die "usage: nuc-spi.sh restore <stock.rom>"
    local stock="$1"
    require_size "${stock}" "${CHIP_SIZE}"
    python3 "${IFD_LAYOUT}" --expect-bios "${EXPECT_BIOS}" "${stock}" >/dev/null

    echo "This writes the ENTIRE ${CHIP_SIZE}-byte chip from ${stock}."
    read -r -p "Type YES to restore: " ans
    [ "${ans}" = "YES" ] || die "aborted"

    fr --progress -w "${stock}"
    echo "OK: factory firmware restored."
}

case "${1:-}" in
    probe)   shift; cmd_probe "$@" ;;
    dump)    shift; cmd_dump "$@" ;;
    info)    shift; cmd_info "$@" ;;
    merge)   shift; cmd_merge "$@" ;;
    flash)   shift; cmd_flash "$@" ;;
    console) shift; cmd_console "$@" ;;
    restore) shift; cmd_restore "$@" ;;
    *)       sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 1 ;;
esac
