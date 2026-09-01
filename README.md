# NUC BIOS Yocto Build

A Yocto/OpenEmbedded build producing an open-source BIOS for the **Intel
NUC5i7RYH** (Rock Canyon, i7-5557U Broadwell-U, Wildcat Point-LP): **coreboot**
with a selectable payload — **EDK2** UefiPayloadPkg (default) or **LinuxBoot**
(Linux 6.12 + u-root).

The `meta-nuc-bios` layer provides everything: the `nuc5i7ryh` machine,
coreboot with the
[`mb/intel/nuc5i5ryb` board port (Gerrit 94032)](https://review.coreboot.org/c/coreboot/+/94032)
vendored as a patch (the i7 kit uses the same NUC5iXRYB board — coreboot
probes the soldered CPU/IGD at runtime), payload plumbing, `linux-linuxboot`,
and `u-root`.

The BMC firmware for the JetKVM that manages this NUC is a separate build:
[pi-bmc/jetkvm-build](https://github.com/pi-bmc/jetkvm-build). To have the
BMC serve this BIOS to the host, copy the built ROM onto the JetKVM's
`/userdata`.

## Building

```sh
pip3 install kas
kas build kas.yml
```

Output: `build/tmp/deploy/images/nuc5i7ryh/coreboot-nuc5i7ryh.rom`

Host requirements beyond the usual Yocto set:

- **Network during coreboot's do_compile** (crossgcc bootstrap, submodules,
  the edk2 payload clone, the blob donor image).
- **gcc-ada (gnat)** on the host for libgfxinit (native Broadwell graphics
  init). Without it the crossgcc bootstrap fails.

## Architecture

coreboot does silicon init (its Broadwell-U support is mature: Chromebooks,
Purism Librem); the board port is an active upstream review (Gerrit 94032,
vendored here until it merges — boots Debian/Proxmox, libgfxinit lights
HDMI/mDP, I218-V/NVMe/USB3/S3 work, PTT fTPM exposed). Payload options
(`NUC_BIOS_PAYLOAD` in `kas.yml`):

- `edk2` (default): UefiPayloadPkg built as its own Yocto recipe
  (`edk2-uefipayload`, `bitbake edk2-uefipayload` builds just the payload)
  from the pinned MrChromebox edk2 fork — the tree coreboot's own
  external-payload machinery defaults to, carrying the CFR SetupMenu, the
  SMMSTORE variable driver, and the cbmem console. coreboot consumes the
  deployed `UEFIPAYLOAD.fd` as a prebuilt FV payload. Normal UEFI boot for
  stock OSes; the port's CFR setup menu (fan profile, power-on-after-AC,
  Turbo, SATA, fTPM) renders in the EDK2 UI with variables persisted in the
  SMMSTORE flash region. The build defines are tuned for Broadwell — most
  notably `CPU_TIMER_LIB_ENABLE=FALSE` (no CPUID 15h crystal clock on
  Broadwell), SMMSTORE-backed variables, Secure Boot support (setup mode
  until keys are enrolled), serial off/cbmem console on (no UART routed on
  this board), and the payload TCG stack off (the PTT fTPM's CRB lives at a
  non-standard address; the OS gets it via the board's ACPI TPM2 table).
- `linuxboot`: `linux-linuxboot` (6.12 LTS, minimal fragment) + `u-root`
  (core+boot commands) as a kexec-style bootloader.

## Memory-init blobs (automatic by default)

Broadwell has no native raminit; a bootable ROM needs Intel's `mrc.bin` +
`refcode.elf`. By default the coreboot recipe extracts both at build time
from a pinned public donor image (MrChromebox's build for google/tidus, the
Lenovo ThinkCentre Chromebox — Broadwell with socketed DDR3L SO-DIMMs like
this board, and its refcode is byte-identical to the one Purism ships for the
Librem 13 v1) using the in-tree cbfstool, and applies a pattern-guarded
one-byte refcode patch so the refcode doesn't disable the NUC's I218-V GbE
(the blob hardcodes GbE-enable to 0). The blobs can be produced and inspected
without the multi-hour coreboot compile:

```sh
kas shell kas.yml -c 'bitbake coreboot -c extract_blobs'
```

Overrides: `COREBOOT_BLOBS_DIR` to supply your own pair,
`COREBOOT_USE_DONOR_BLOBS = "0"` for a blob-less compile check marked
`.NOT-BOOTABLE`, `COREBOOT_REFCODE_GBE_PATCH = "0"` to skip the GbE patch.
Full story:
[the blob README](meta-nuc-bios/recipes-bsp/coreboot/files/blobs/README.md).

## Flashing

**The first flash needs an external SOIC-8 programmer.** The stock BIOS
(`RYBDWi35.86A.0386`) sets `BLE` + `SMM_BWP` + `FLOCKDN` (`BIOS_CNTL` = `0x2a`),
so `flashrom -p internal` cannot write the chip — and the BIOS-security jumper
only grants the descriptor override, not `SMM_BWP`. Clip-on procedure, hardware
caveats and helper scripts: [docs/external-flashing.md](docs/external-flashing.md).

```sh
./scripts/nuc-spi.sh dump stock-bios.rom   # two verified reads, keep this
./scripts/nuc-spi.sh flash stock-bios.rom  # BIOS region only, via CH341A
```

Once coreboot is running the lock bits are gone and updates are done in-band,
from Linux on the NUC:

```sh
flashrom -p internal -r stock.rom                    # backup first!
flashrom -p internal --ifd -i bios -w coreboot-nuc5i7ryh.rom
```

To get the ROM onto the NUC for that, `nuc-rom-image` builds a small FAT32
disk image carrying it, for attaching as JetKVM virtual media or dd'ing to a
USB stick:

```sh
kas shell .config.yaml -c 'bitbake nuc-rom-image'
# -> build/tmp/deploy/images/nuc5i7ryh/coreboot-rom.img
```

Only the 6 MiB BIOS region is written; the factory descriptor/GbE/ME regions
stay on the 8 MiB flash (a Macronix MX25L6405, SOIC-8, not socketed). Keep
the stock backup off-device; recovery without it needs an external SOIC-8
clip. Keep the BIOS-security jumper in normal mode — the port's PTT fTPM
needs the ME running.

## Self-flashing image (flash remotely over the JetKVM)

Useful for updates once coreboot is in — it uses the internal programmer, so it
cannot perform the initial flash past the stock BIOS's `SMM_BWP`.

To flash without a local shell, build a self-contained bootable image that
carries `flashrom`, this build's coreboot ROM, and an init that does the whole
job unattended — attach it to the NUC as JetKVM virtual media, or dd it to a
USB stick, and boot it:

```sh
kas shell .config.yaml -c 'bitbake nuc-flasher-image'
# -> build/tmp/deploy/images/nuc5i7ryh/nuc-bios-flasher.img
```

The disk carries a single EFI unified kernel image (a stock Alpine -lts
kernel plus a bundled static busybox, flashrom and the ROM — the
`nuc-flasher-uki` recipe, which pins and fetches the Alpine artifacts), so it
needs no multiconfig and no second Yocto build. On boot its init verifies the
BIOS region and:

- **already this ROM** — reboots, so leaving it attached never loops
- **differs** — flashes the BIOS region only, then reboots
- **write fails** — drops to a shell and does *not* reboot

Internal flashing needs no SOIC clip because the running coreboot leaves the
SPI unlocked (`BOOTMEDIA_LOCK_NONE`). A machine still on the stock Intel BIOS
has `SMM_BWP` set and cannot self-flash — use `scripts/nuc-spi.sh` with the
clip once to get coreboot on in the first place.

Recovery if it won't POST: flash the factory backup back with the clip,
`flashrom -p <programmer> --ifd -i bios -w stock-bios.rom`.
