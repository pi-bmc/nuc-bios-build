/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <cf9_reset.h>
#include <device/device.h>

#include "nuc5i5ryb.h"

/*
 * Advertise a *full* CF9 reset in the FADT, not the soft one.
 *
 * arch_fill_fadt() publishes reset_value = RST_CPU | SYS_RST (0x06), which
 * toggles CPU and system reset but leaves the platform partially powered. On
 * this board that is not enough to get back through Broadwell's raminit: an
 * OS-initiated ACPI reset -- the default path for `reboot` -- hangs the next
 * boot in the payload, with the machine drawing power, USB enumerated and no
 * response to any input. A DC power cycle is then the only way back.
 *
 * Adding FULL_RST (0x0e) makes the ACPI reset do what the working path already
 * does. Established on hardware 2026-07-30 by bisecting the reset method from
 * Linux: `/sys/kernel/reboot/type=acpi` (FADT, 0x06) hangs, while `type=pci`
 * with the default cold mode -- which writes 0x0e to CF9 -- reboots cleanly
 * every time. The two differ only in FULL_RST.
 *
 * This is deliberately scoped to the mainboard rather than changed in
 * arch/x86: 0x06 is correct on platforms whose raminit survives a soft reset,
 * and only this board has been tested.
 */
void mainboard_fill_fadt(acpi_fadt_t *fadt)
{
	if (CONFIG(HAVE_CF9_RESET))
		fadt->reset_value = RST_CPU | SYS_RST | FULL_RST;
}

static void mainboard_enable(struct device *dev)
{
#if CONFIG(TPM2_PTT_ACPI_START)
	/* PTT publishes the TPM2 table itself (ptt.c); the generic path is idle. */
	dev->ops->write_acpi_tables = ptt_write_tpm2_table;
#endif
}

struct chip_operations mainboard_ops = {
	.enable_dev = mainboard_enable,
};
