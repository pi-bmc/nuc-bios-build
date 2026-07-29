# Intel NUC5i5RYB (Rock Canyon)

The NUC5i5RYB is the mainboard used in the Intel NUC5i5RYK / NUC5i5RYH
mini-PCs, built around a soldered Broadwell-U i5-5250U with a Wildcat
Point-LP PCH.

## Technology

| Part        | Value                                             |
|-------------|---------------------------------------------------|
| Northbridge | Broadwell (`northbridge/intel/broadwell`)         |
| Southbridge | Wildcat Point-LP (`lynxpoint`, 8086:9cc3)         |
| CPU         | Intel Core i5-5250U (soldered, BGA)               |
| RAM         | 2 x DDR3L SO-DIMM (socketed)                       |
| Super I/O   | Nuvoton NCT5577D (nct6776-compatible)             |
| Audio       | Intel HD Audio                                    |
| Network     | Intel I218-V Gigabit Ethernet                     |
| Graphics    | Intel HD Graphics 6000, native init (libgfxinit)  |
| Storage     | M.2/M NVMe, M.2/E WLAN, optional SATA header      |

## Status

### Working

- Native graphics init (libgfxinit): HDMI + mini-DisplayPort
- Intel I218-V Gigabit Ethernet
- USB 3.0 / 2.0 (xHCI)
- M.2 NVMe and M.2 WLAN (Intel 7265)
- Firmware TPM 2.0 (Intel PTT) — see below
- NCT5577D hardware monitoring and SmartFan IV fan control
- CFR setup menu (fan profile, power-on-after-AC, Turbo, SATA, fTPM)
- S3 suspend/resume

### Optional / off by default

- Onboard SATA / 2.5" bay: enable via the CFR "SATA controller" option

## Required proprietary blobs

coreboot does not ship these. Dump the factory firmware first
(`flashrom -p internal -r stock.rom`) and keep it: the descriptor, GbE and
ME regions stay on the chip (coreboot only replaces the BIOS region), and
the memory blobs are extracted from that dump.

| Blob          | Purpose               |
|---------------|-----------------------|
| `mrc.bin`     | Memory reference code |
| `refcode.elf` | Reference code blob   |

To build a bootable image, place both under
`3rdparty/blobs/mainboard/intel/nuc5i5ryb/` and enable `HAVE_MRC`
(`MRC_FILE`) and `HAVE_REFCODE_BLOB` (`REFCODE_BLOB_FILE`). The upstream
default omits them so CI can compile the board.

## Flashing coreboot

| Type                | Value                          |
|---------------------|--------------------------------|
| Socketed flash      | no (SOIC-8)                    |
| Model               | Macronix MX25L6405 (8 MiB)     |
| Internal flashing   | yes (SPI controller unlocked)  |

The BIOS region can be flashed from Linux without opening the case:

```console
# flashprog -p internal --ifd -i bios -w coreboot.rom
```

`--ifd -i bios` keeps the descriptor, GbE and ME regions untouched, which
is required for PTT (see below).

## Firmware TPM 2.0 (Intel PTT)

The board has no discrete TPM. The Management Engine provides PTT
(Platform Trust Technology), a firmware TPM 2.0. It is exposed to the OS
with an ACPI TPM2 table using CRB "Start Method 2".

### CRB location

On Skylake and newer the on-die iTPM decodes a fixed-MMIO CRB at
0xFED40000. On this silicon 0xFED40000 is instead the LPC decode for a
discrete TIS TPM the board does not have, so it reads all-1s. The ME's
CRB is at 0xFED70000 with a non-standard layout:

| Offset       | Field                                            |
|--------------|--------------------------------------------------|
| 0x0C         | ctrl_start (TCG PTP control area, offset 0 base) |
| 0x18 / 0x1C  | cmd_size / cmd_pa                                |
| 0x24 / 0x28  | rsp_size / rsp_pa                                |
| 0x40         | HCMD — Intel host-command doorbell (RE'd)        |
| 0x44         | HSTS — Intel status, 0x03 = ready (RE'd)         |
| 0x80..0xFFF  | command/response data buffer                     |

The control area follows the TCG PTP layout starting at offset 0 (not the
+0x40 locality convention). HCMD/HSTS are Intel-specific and not part of
the TCG PTP spec.

### coreboot's role

`ptt.c` points the CRB command/response registers at the in-window buffer
(0xFED70080), runs TPM2_Startup, and emits the TPM2 table via the
mainboard `write_acpi_tables` hook. `acpi/tpm.asl` provides the
`\_SB.TPM` (MSFT0101) device whose `_DSM` "Start" method drives execution.
No generic coreboot TPM stack is used; the OS (Linux `tpm_crb`) drives
the TPM directly.

One CRB quirk: the ME disarms the execute path after roughly one second
of idle, so a bare `ctrl_start = 1` never completes. Writing `HCMD = 0`
first re-arms it. Both `ptt.c` and the ASL `_DSM` do this.

### Prerequisite

PTT must be enabled in the ME and the ME must be in Normal mode with the
firmware descriptor override off (FDO=0). This is the factory state after
a full G3 power cycle.

### Verification

    cbmem -c:
      PTT: CRB @0xfed70000 ready (HSTS 0x3), data buffer @0xfed70080
      PTT: TPM2_Startup ok (rc=0x0)
      ACPI:    * TPM2

    /sys/firmware/acpi/tables/TPM2: length 76,
      control_area 0xfed70000, start_method 2

    /dev/tpm0, /dev/tpmrm0 present

The fTPM can be disabled or cleared at runtime from the CFR "Security"
menu (`tpm_enable`, `tpm_clear`).

### Adopting the PTT fTPM on another Broadwell/Haswell board

The pieces are board-local and self-contained: `ptt.c` (CRB setup + the
TPM2 table via a `write_acpi_tables` hook in `mainboard.c`), `acpi/tpm.asl`
(the `\_SB.TPM` device), and a board-local `config TPM2_PTT_ACPI_START`.
No generic coreboot file is touched. To reuse them on another
Broadwell/Haswell board whose ME provides PTT but that lacks a discrete
TPM:

1. Confirm the CRB decodes: read HSTS at `0xFED70000 + 0x44`; a value
   other than all-ones means the ME exposes the CRB there. The ME must be
   in Normal mode with PTT enabled (FDO off).
2. Copy `ptt.c`, `acpi/tpm.asl`, `mainboard.c` and the
   `ptt_write_tpm2_table` prototype header; include `acpi/tpm.asl` from the
   board `dsdt.asl`; add `config TPM2_PTT_ACPI_START` and the `Makefile.mk`
   entries.
3. Do NOT enable the coreboot TPM stack (`CONFIG_TPM2` / `CRB_TPM`): the
   generic `acpi_create_tpm2()` must stay a no-op (`tlcl_get_family()` ==
   `TPM_UNKNOWN`) so it does not emit a second, conflicting TPM2 table.
4. Verify as above (cbmem, `/sys/firmware/acpi/tables/TPM2`, `/dev/tpm0`).

The CRB base (`0xFED70000`), the control-area offsets and the HCMD re-arm
were reverse-engineered and hardware-verified on Wildcat Point-LP only. On
other PCHs (e.g. Lynx Point) re-verify the window and doorbell offsets
first. A shared, Kconfig-selectable implementation is feasible if there is
interest.

## Hardware monitoring and fan control

The Nuvoton NCT5577D drives the CPU fan autonomously in SmartFan IV mode
off the CPU (PECI) temperature; the curve is selected in the CFR "Cooling"
menu. In Linux the chip is handled by the `nct6775` driver.

## References

- [Intel NUC5i5RYK/RYH product page](https://ark.intel.com/content/www/us/en/ark/products/83257/intel-nuc-kit-nuc5i5ryk.html)
