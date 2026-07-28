# Flashing coreboot with an external SOIC-8 programmer

The stock Intel BIOS locks the SPI controller, so the first coreboot flash has
to bypass the PCH entirely with a clip-on programmer. This is a **one-time
bootstrap**: coreboot does not set the lock bits, so every subsequent update
goes back to `flashrom -p internal` (or the flasher live ISO, `kas-flasher.yml`).

## Why the internal path does not work

Measured on the live unit (board NUC5i7RYB, stock BIOS `RYBDWi35.86A.0386`):

| Bit       | State | Effect                              |
|-----------|-------|-------------------------------------|
| `BLE`     | set   | write-enable traps to SMM           |
| `SMM_BWP` | set   | writes only allowed from inside SMM |
| `FLOCKDN` | set   | descriptor/FREG overrides latched   |

`BIOS_CNTL` reads `0x2a`. `flashrom -p internal --ifd -i bios -w` fails with
`Transaction error at 0x1a0000` and writes nothing. Moving the BIOS-security
jumper only grants the descriptor override (FDO — all FREG regions become
r/w); it does **not** clear `SMM_BWP`, so the write still fails. Intel BIOS
Guard is also present, and the `.bio` update path is Intel-signed.

Note: the vendored board port's own documentation (and the upstream Gerrit
change) claims internal flashing works. That was not true for this unit's
BIOS revision. The README's flashing section is written for the post-coreboot
state.

## The hardware

[ACEIRMC B07R5LPTYM](https://www.amazon.com/Organizer-Socket-Adpter-Programmer-CH341A/dp/B07R5LPTYM)
is a **CH341A** "black board" USB programmer plus SOIC-8/SOP-8 test clips. It
enumerates as USB `1a86:5512` and flashrom drives it as `ch341a_spi`. The host
already has what it needs:

```console
$ flashrom --version
flashrom v1.7.0 (git:v1.7.0) on Linux
$ flashrom -p ch341a_spi          # with nothing plugged in
Couldn't open device 1a86:5512.
```

Target chip: **Macronix MX25L6405**, 8 MiB, SOIC-8, 3.3 V, soldered (not
socketed) on the NUC5i7RYB board.

### Do the 3.3 V mod first

The black CH341A board powers the CH341A itself from USB 5 V and only regulates
the *target's* VCC pin to 3.3 V. CS#/CLK/MOSI are therefore driven at **5 V**,
against an MX25L6405 whose absolute-maximum input is VCC+0.5 V ≈ 3.9 V. In
circuit those same lines are wired straight to the PCH's SPI pins.

Fix it before the clip ever touches the board. Either:

- Lift/cut CH341A pin 28 (VCC) off the 5 V rail and tie it to the on-board
  AMS1117 3.3 V output, tying pin 9 (V3) to 3.3 V as well —
  [VoltLog #318](https://www.voltlog.com/ch341a-programmer-3-3v-fix-voltlog-318/),
  or the [no-pin-lifting trace cut](https://wej.k.vu/electronics/ch341a-mini-programmer-fix/).
- Or put a 4-channel level shifter between the programmer and the clip.

People do flash unmodified boards and get away with it. Do not gamble a NUC
board on it.

### Check Boot Guard before writing anything

If Boot Guard were fused in verified-boot mode, an externally flashed coreboot
would be rejected by the ACM and the board would be permanently dead — the one
failure the clip cannot undo. On the running Debian install:

```console
# modprobe msr && rdmsr 0x13a
```

`0` (or the MSR faulting) means Boot Guard is not provisioned and coreboot will
boot. Anything non-zero: stop and decode it — the verified-boot bit is fatal,
measured-only is survivable. Broadwell NUC5 hardware with a working upstream
coreboot port should read 0, but this is a ten-second check against a
non-recoverable outcome.

## What actually gets written

`coreboot-nuc5i7ryh.rom` is an 8 MiB *frame* with only the BIOS region filled
in. The build sets no `CONFIG_HAVE_IFD_BIN`/ME/GbE binaries, and
`board.fmd` declares `FLASH 0x800000 { BIOS@0x1a0000 0x660000 { ... } }`, so:

```text
0x000000 ┬─ descriptor ─┐
0x001000 │  GbE         │  0xff padding in coreboot-nuc5i7ryh.rom
0x003000 │  ME (~1.6 MB)│  ← MUST come from the factory chip
0x1a0000 ┼─ BIOS ───────┤
         │  coreboot +  │  ← the only region we write
         │  UEFIPAYLOAD │
0x7fffff ┴──────────────┘
```

**Writing the whole 8 MiB from `coreboot-nuc5i7ryh.rom` erases the descriptor,
GbE and ME and the board will not POST.** The ME must also stay in Normal mode
for the port's PTT fTPM to come up. So the write is region-limited even on the
external programmer, using a layout derived from the chip's own descriptor.

## Procedure

### 1. Build the ROM

Not built yet in this tree — `build/tmp/deploy/images/nuc5i7ryh/` only has the
`efi-drivers/` output from the abandoned Path-B branch.

```sh
kas build kas.yml
# -> build/tmp/deploy/images/nuc5i7ryh/coreboot-nuc5i7ryh.rom
```

`gcc-ada`/gnat is present on this host, so libgfxinit will build.

### 2. Get at the chip

Power down, unplug the PSU, open the bottom cover, and pull the board out.
Then, so the PCH cannot partially power up through the clip:

- disconnect the CMOS coin cell,
- hold the power button ~30 s to drain the rails.

Intel does not document where the flash sits. Its Technical Product
Specification labels major components on both sides (Figures 1 and 2) and the
SPI flash is not among them, and no public teardown identifies it. Note also
that Broadwell-U is a two-die MCM — the Wildcat Point-LP PCH is *inside* the
CPU package under the thermal solution — so "near the PCH" is not a landmark.

Look for an 8-pin SOIC, 1.27 mm pitch, body ~5 x 4 mm (150 mil) or ~5 x 5.3 mm
(208 mil), marked `25L6405` / `MX25L6405...`. Second sources are possible
(`W25Q64`, `GD25Q64`); coreboot's `board_info.txt` records Macronix for the
NUC5i5RYB. Check the accessible side first — the one exposed by the bottom
cover, carrying the SO-DIMMs, M.2 and the BIOS security jumper — since
reaching the other side means removing the thermal solution.

Do not identify it by looks alone. Clip a candidate and run `probe`: a
matching 8 MiB 25-series RDID plus a `dump` whose byte 0x10 is `5a a5 f0 0f`
(the descriptor signature `ifd-layout.py` checks) is proof. The wrong chip
fails both, and `flash` refuses without a valid dump, so a misidentification
cannot turn into a bad write.

Pin 1 is the dot/notch corner; the clip cable's red stripe is pin 1. On the
CH341A's ZIF socket, 25-series parts go in the positions nearest the lever,
against the "25" silkscreen row.

Pinout, for sanity-checking the clip seating: 1 CS#, 2 DO, 3 WP#, 4 GND,
5 DI, 6 CLK, 7 HOLD#, 8 VCC.

### 3. Probe and back up

```sh
./scripts/nuc-spi.sh probe
./scripts/nuc-spi.sh dump stock-bios.rom
```

`dump` reads the chip **twice** and refuses to continue unless the two reads
are byte-identical, then decodes the descriptor and asserts the BIOS region is
`0x1a0000-0x7fffff` (what `board.fmd` was built against). A valid read starts
with the descriptor signature `5a a5 f0 0f` at offset `0x10`.

If the two reads differ, the bus is unstable — the usual cause of in-circuit
failure. Reseat, re-drain, retry. If it never settles, desolder the chip
(hot air, 8 pins) and program it in the kit's SOP8→DIP8 adapter. **Do not
write a chip you cannot read twice consistently.**

### Observed on this board: the clip back-powers the NUC

Reading `flashrom -p ch341a_spi -V` and the fan tell you which failure you have:

| Symptom | Meaning |
|---------|---------|
| `id1 0xff, id2 0xffff` | MISO floating high — clip not contacting anything |
| `id1 0x00, id2 0x00`, stable | MISO held low — see below |
| mixed `0xff`/`0x00`/garbage | marginal contact — reseat |

On this board, plugging in the programmer **spun the NUC's fan** with the PSU
and coin cell out: the flash's VCC pin is on a rail shared with the board, so
the clip back-feeds it, the Wildcat Point-LP die (inside the CPU package)
comes up far enough to drive SPI, and MISO reads a rock-solid `0x00` — you are
contending with a live master, not reading a silent chip. The CH341A's AMS1117
cannot supply that load either, so the rail sags.

There is no clip-only fix. Either lift pin 8 (VCC) off its pad so the clip
powers the flash alone, or desolder the chip and use the SOP8→DIP8 adapter.
Check first that the PSU really is unplugged and the coin cell really is out —
either one produces the same signature for free.

Copy `stock-bios.rom` somewhere safe. It is the entire recovery plan.

If flashrom cannot pick between MX25L6405 variants, pass the one it names:

```sh
CHIP=MX25L6405 ./scripts/nuc-spi.sh dump stock-bios.rom
```

### 4. Flash

```sh
./scripts/nuc-spi.sh flash stock-bios.rom
```

This refuses to run unless the ROM is 8 MiB, has no `.NOT-BOOTABLE` marker
(built without `mrc.bin`/`refcode.elf`), is all-`0xff` below `0x1a0000`, and a
verified backup exists. It builds a flashrom layout from the *chip's* actual
descriptor and runs:

```sh
flashrom -p ch341a_spi -l <layout> -i bios -w coreboot-nuc5i7ryh.rom
```

flashrom reads the chip, overlays only the BIOS region, writes, and verifies.
Expect a few minutes — the CH341A is slow.

Optionally archive the exact full-chip contents:

```sh
./scripts/nuc-spi.sh merge stock-bios.rom nuc5i7ryh-full.rom
```

### 5. Reassemble and boot

Remove the clip, reconnect the coin cell, reassemble, and power on from a cold
G3 state (the ME needs it, and so does PTT). Leave the BIOS-security jumper in
normal mode.

## Reverting to stock

`stock-bios.rom` from step 3 is the only way back. **Intel's own BIOS recovery
is not available once coreboot is flashed** — the security-jumper `.bio`
recovery path is implemented by a recovery block that lives inside the BIOS
region, which coreboot overwrites. There is nothing left to run it. Likewise
`RYBDWi35.86A.0386.bio` from Intel's download site cannot be applied: it is a
capsule for the running Intel BIOS to consume, not a flashable image.

Because the flash only ever had its BIOS region modified, restoring that one
region returns the chip to bit-identical factory contents.

### If coreboot still boots — no clip needed

coreboot does not set `BLE`/`SMM_BWP`, so the internal programmer works. From
Linux on the NUC:

```sh
flashrom -p internal --ifd -i bios -w stock-bios.rom
```

This is also what the flasher live ISO does if you can't get a local shell —
boot it as JetKVM virtual media, `scp` the backup in, and run the same command.

Then **clear CMOS** (unplug, pull the coin cell for a minute) before booting.
The stock BIOS will otherwise find coreboot-era CMOS contents.

### If it does not POST — clip back on

```sh
./scripts/nuc-spi.sh restore stock-bios.rom
```

That writes the full 8 MiB, but flashrom reads first and only erases/rewrites
blocks that differ — since only the BIOS region changed, it is no slower or
riskier than the region-limited write. Verify explicitly if you want:

```sh
flashrom -p ch341a_spi -v stock-bios.rom
```

### Afterwards

Booting the stock BIOS re-arms `SMM_BWP`, so you are back to needing the clip
for the next coreboot flash. Keep the programmer.

## After the first flash

coreboot leaves the BIOS region writable, so the clip is retired:

```sh
flashrom -p internal --ifd -i bios -w coreboot-nuc5i7ryh.rom
```

which is exactly what the flasher live ISO automates — build it with
`kas build kas-flasher.yml` and attach it as JetKVM virtual media.
