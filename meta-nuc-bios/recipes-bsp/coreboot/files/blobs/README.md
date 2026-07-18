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

## I218-V GbE refcode patch (applied by default)

The Broadwell `pei_data` has no `gbe_enable` field, and the refcode
hardcodes its internal GbE-enable to 0 — `movb $0x0,0x37e(%ebx)` in its
settings-struct init — after which it disables the PCH GbE MAC and the OS
never sees the I218-V (`Documentation/soc/intel/broadwell/blobs.md`;
nothing in coreboot's own Broadwell code re-enables it).

The coreboot docs fix this with a one-byte patch at file offset 131253, but
that offset is only valid for the exact Librem 13 v1 refcode binary they
analyzed (sha256 `8a919ffe…`). The samus-extracted refcode is a *different
build* of the same code: the documented 6-instruction sequence appears
byte-for-byte, exactly once, but shifted (the disable byte lives at
`0x1fff1`, and `0x200b5` holds unrelated code — blind offset-patching would
corrupt it).

So `do_extract_blobs` patches by **pattern**: it finds
`c6 83 7e 03 00 00 00`, requires exactly one occurrence, and flips the
immediate to `0x01`. It is idempotent and refuses to touch a refcode where
the pattern is missing or ambiguous. Disable with
`COREBOOT_REFCODE_GBE_PATCH = "0"` (the Gerrit 94032 port reports the
I218-V working with blobs of unstated provenance, so unpatched *may* work —
but enable=1 is what a GbE-equipped board wants either way).

## ME region

The factory flash descriptor, GbE and ME regions are never touched: flashing
uses `--ifd -i bios` only. Keep the BIOS-security jumper in normal mode —
the port's PTT fTPM needs the ME running. me_cleaner does support Wildcat
Point-LP ME 10 if you prefer a neutered ME over the fTPM.
