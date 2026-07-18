# Broadwell memory-init blobs

coreboot has no native raminit for Broadwell-U; a bootable NUC5i7RYH ROM
needs two Intel blobs:

- `mrc.bin` — memory reference code (romstage, ~218 KiB)
- `refcode.elf` — relocatable system-agent/PCH init (ramstage)

## Default: automatic extraction (nothing to do)

The coreboot recipe extracts both at build time from a pinned, public
Broadwell donor image — MrChromebox's coreboot+edk2 build for google/samus
(Chromebook Pixel 2015, same Broadwell-U/Wildcat Point-LP silicon), sha256-
verified — using the in-tree cbfstool:

```sh
cbfstool <donor>.rom extract -f mrc.bin -n mrc.bin
cbfstool <donor>.rom extract -m x86 -f refcode.elf -n fallback/refcode
```

(Verified against `coreboot_edk2-samus-mrchromebox_20260714.rom`: mrc.bin
222876 B, refcode 177472 B decompressed.)

## Override: supply your own pair

Extract from a different Broadwell coreboot image with the same two commands
(see <https://doc.coreboot.org/soc/intel/broadwell/blobs.html>), drop the
files in a directory (this gitignored `files/blobs/` dir works), and set:

```bitbake
COREBOOT_BLOBS_DIR = "/path/to/blobs"
```

Setting `COREBOOT_USE_DONOR_BLOBS = "0"` with no `COREBOOT_BLOBS_DIR` builds
the CI-style blob-less compile check (deployed ROM marked `.NOT-BOOTABLE`).

## I218-V GbE caveat (read if Ethernet dies under coreboot)

The Broadwell `pei_data` has no `gbe_enable` field, and the coreboot docs
describe a one-byte refcode patch to keep the refcode from disabling an
Intel GbE MAC — **for the Librem 13 v1 refcode binary only** (sha256
`8a919ffe…`, file offset 131253: `0x00` → `0x01`). The samus-extracted
refcode is a different binary (that offset holds `0x32`), so the recipe does
NOT apply the patch. The nuc5i5ryb port (Gerrit 94032) reports the I218-V
working with unpatched blobs. If Ethernet turns out dead under coreboot,
this is the first thing to investigate — likely by locating the matching
field in the samus refcode or sourcing the Librem refcode and patching per
the docs.

## ME region

The factory flash descriptor, GbE and ME regions are never touched: flashing
uses `--ifd -i bios` only. Keep the BIOS-security jumper in normal mode —
the port's PTT fTPM needs the ME running. me_cleaner does support Wildcat
Point-LP ME 10 if you prefer a neutered ME over the fTPM.
