#!/usr/bin/env python3
"""Decode the Intel Flash Descriptor at the start of a full SPI flash dump.

Used by scripts/nuc-spi.sh to derive a flashrom layout from the NUC's own
descriptor instead of trusting hardcoded offsets, and to sanity-check a dump
before anything is written back.

  ifd-layout.py stock.rom                     # human-readable region table
  ifd-layout.py --layout stock.rom            # flashrom -l layout file
  ifd-layout.py --expect-bios 0x1a0000:0x7fffff stock.rom

Region registers (FLREG0..FLREG4) live at FRBA, which FLMAP0 (0x14) points
at. Each is base = (reg & 0x7fff) << 12, limit = ((reg >> 16) & 0x7fff) << 12
| 0xfff; base > limit means the region is unused.
"""

import argparse
import struct
import sys

FLVALSIG_OFFSET = 0x10
FLVALSIG = 0x0FF0A55A
# FLREG order is fixed by the descriptor spec; only the first five exist on
# Wildcat Point-LP.
REGION_NAMES = ["fd", "bios", "me", "gbe", "pd"]


def decode(data):
    if len(data) < 0x1000:
        sys.exit("error: %d bytes is too small to hold a flash descriptor" % len(data))

    (sig,) = struct.unpack_from("<I", data, FLVALSIG_OFFSET)
    if sig != FLVALSIG:
        sys.exit(
            "error: no Intel flash descriptor -- FLVALSIG at 0x10 is 0x%08x, "
            "expected 0x%08x.\nThe dump is not a full-chip read, or the read "
            "was garbage (wrong chip clipped / unstable SPI)." % (sig, FLVALSIG)
        )

    (flmap0,) = struct.unpack_from("<I", data, 0x14)
    frba = ((flmap0 >> 16) & 0xFF) << 4

    regions = []
    for i, name in enumerate(REGION_NAMES):
        off = frba + 4 * i
        if off + 4 > len(data):
            break
        (reg,) = struct.unpack_from("<I", data, off)
        base = (reg & 0x7FFF) << 12
        limit = (((reg >> 16) & 0x7FFF) << 12) | 0xFFF
        regions.append((name, base, limit, base <= limit))

    return regions


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="full-chip SPI dump")
    ap.add_argument("--layout", action="store_true",
                    help="emit a flashrom layout file instead of a table")
    ap.add_argument("--expect-bios", metavar="BASE:LIMIT",
                    help="fail unless the BIOS region has exactly these bounds "
                         "(e.g. 0x1a0000:0x7fffff, matching board.fmd)")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        data = f.read()

    regions = decode(data)
    used = [r for r in regions if r[3]]

    if args.expect_bios:
        want_base, want_limit = (int(x, 0) for x in args.expect_bios.split(":"))
        bios = next((r for r in used if r[0] == "bios"), None)
        if bios is None:
            sys.exit("error: the descriptor declares no BIOS region")
        if (bios[1], bios[2]) != (want_base, want_limit):
            sys.exit(
                "error: BIOS region is 0x%06x-0x%06x but 0x%06x-0x%06x was "
                "expected.\ncoreboot's board.fmd hardcodes the expected bounds; "
                "a mismatch means the fmap and the descriptor disagree and the "
                "ROM must not be flashed." % (bios[1], bios[2], want_base, want_limit)
            )

    if args.layout:
        for name, base, limit, _ in sorted(used, key=lambda r: r[1]):
            print("%08x:%08x %s" % (base, limit, name))
        return

    print("image      : %s (%d bytes)" % (args.image, len(data)))
    print("%-6s %-10s %-10s %s" % ("region", "base", "limit", "size"))
    for name, base, limit, valid in regions:
        if not valid:
            print("%-6s %-10s %-10s unused" % (name, "-", "-"))
            continue
        print("%-6s 0x%06x   0x%06x   %d KiB" % (name, base, limit, (limit - base + 1) // 1024))


if __name__ == "__main__":
    main()
