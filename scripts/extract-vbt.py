#!/usr/bin/env python3
"""Extract the Video BIOS Table (VBT) from a factory BIOS dump.

The vendored board patch (Gerrit 94032) was fetched from Gerrit's plain
/patch endpoint, which emits "Binary files differ" instead of a GIT binary
patch. data.vbt therefore applies as a ZERO-LENGTH file and coreboot adds an
empty vbt.bin to CBFS. libgfxinit does not need the VBT, but Linux's i915
reads it out of the ACPI opregion to learn the board's port/encoder mapping,
so an empty one is worth fixing.

This recovers the real table from the machine's own factory firmware, which
is strictly better than the upstream blob: it is this exact board's.

  extract-vbt.py stock-bios.rom data.vbt

Layout (struct vbt_header): 20-byte "$VBT<platform>" signature, u16 version,
u16 header_size, u16 vbt_size, u8 checksum.
"""

import argparse
import struct
import sys

SIGNATURE = b"$VBT"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="factory BIOS dump (full chip or BIOS region)")
    ap.add_argument("output", help="where to write data.vbt")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        data = f.read()

    off = data.find(SIGNATURE)
    if off < 0:
        sys.exit("error: no $VBT signature in %s" % args.image)
    if data.find(SIGNATURE, off + 1) >= 0:
        print("warning: more than one $VBT signature; using the first at 0x%x" % off,
              file=sys.stderr)

    sig = data[off:off + 20].rstrip(b"\x00 ").decode("ascii", "replace")
    version, header_size, vbt_size = struct.unpack_from("<HHH", data, off + 20)

    if not 0 < vbt_size <= len(data) - off:
        sys.exit("error: implausible vbt_size %d at 0x%x -- not a VBT" % (vbt_size, off))
    if header_size < 22 or header_size > vbt_size:
        sys.exit("error: implausible header_size %d (vbt_size %d)" % (header_size, vbt_size))

    vbt = data[off:off + vbt_size]
    with open(args.output, "wb") as f:
        f.write(vbt)

    print("signature   : %s" % sig)
    print("version     : %d.%02d" % (version // 100, version % 100))
    print("offset      : 0x%x" % off)
    print("size        : %d bytes -> %s" % (vbt_size, args.output))


if __name__ == "__main__":
    main()
