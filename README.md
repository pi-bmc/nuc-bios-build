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
from a pinned public donor image (MrChromebox's samus/Chromebook-Pixel-2015
build — same Broadwell-U silicon) using the in-tree cbfstool, and applies a
pattern-guarded one-byte refcode patch so the refcode doesn't disable the
NUC's I218-V GbE (the blob hardcodes GbE-enable to 0). The blobs can be
produced and inspected without the multi-hour coreboot compile:

```sh
kas shell kas.yml -c 'bitbake coreboot -c extract_blobs'
```

Overrides: `COREBOOT_BLOBS_DIR` to supply your own pair,
`COREBOOT_USE_DONOR_BLOBS = "0"` for a blob-less compile check marked
`.NOT-BOOTABLE`, `COREBOOT_REFCODE_GBE_PATCH = "0"` to skip the GbE patch.
Full story:
[the blob README](meta-nuc-bios/recipes-bsp/coreboot/files/blobs/README.md).

## Flashing

The Rock Canyon SPI controller is unlocked; from Linux on the NUC:

```sh
flashrom  -p internal -r stock.rom                    # backup first!
flashprog -p internal --ifd -i bios -w coreboot-nuc5i7ryh.rom
```

Only the 6 MiB BIOS region is written; the factory descriptor/GbE/ME regions
stay on the 8 MiB flash (a Macronix MX25L6405, SOIC-8, not socketed). Keep
the stock backup off-device; recovery without it needs an external SOIC-8
clip. Keep the BIOS-security jumper in normal mode — the port's PTT fTPM
needs the ME running.

## Flasher live ISO (flash remotely over the JetKVM)

To flash without a local shell, build a self-contained bootable ISO that
carries `flashrom`, the coreboot ROM, and helper scripts — attach it to the
NUC as JetKVM virtual media and boot it:

```sh
kas build kas-flasher.yml
# -> build/tmp-flasher/deploy/images/intel-corei7-64/nuc-flasher-image-*.iso
```

It's an `intel-corei7-64` live image (hybrid BIOS+UEFI El Torito, ~360 MiB —
`linux-firmware` stripped) built under the `flasher` multiconfig;
`nuc-coreboot-rom` pulls the ROM in from the default multiconfig, so the ISO
always carries the current coreboot output. On boot: root logs in with no
password, sshd + DHCP come up, and the login banner lists the steps:

1. `backup-bios` — reads + verifies the factory firmware to `/root`
2. `scp` it off the machine (the rootfs is RAM-only; `ip addr` for the IP)
3. `flash-coreboot` — writes the BIOS region only, then verifies
4. `poweroff`, then power-cycle

Recovery if it won't POST: boot the ISO again and
`flashrom -p internal --ifd -i bios -w /root/stock-bios.rom`.
