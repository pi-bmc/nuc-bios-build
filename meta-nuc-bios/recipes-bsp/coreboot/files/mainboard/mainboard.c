/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>

#include "nuc5i5ryb.h"

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
