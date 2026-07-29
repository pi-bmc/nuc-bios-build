/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_NUC5I5RYB_H
#define MAINBOARD_NUC5I5RYB_H

#include <acpi/acpi.h>
#include <device/device.h>

/* Intel PTT fTPM: emit the TPM2 table (Start Method 2); see ptt.c. */
unsigned long ptt_write_tpm2_table(const struct device *dev, unsigned long current,
				   struct acpi_rsdp *rsdp);

#endif
