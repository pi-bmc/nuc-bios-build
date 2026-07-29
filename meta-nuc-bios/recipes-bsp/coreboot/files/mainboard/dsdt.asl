/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>

DefinitionBlock(
	"dsdt.aml",
	"DSDT",
	ACPI_DSDT_REV_2,
	OEM_ID,
	ACPI_TABLE_CREATOR,
	0x20141018	/* OEM revision */
)
{
	#include <acpi/dsdt_top.asl>

	#include <southbridge/intel/common/acpi/platform.asl>
	#include "acpi/platform.asl"
	#include <soc/intel/common/acpi/acpi_wake_source.asl>

	#include <southbridge/intel/wildcatpoint/acpi/globalnvs.asl>
	#include <southbridge/intel/wildcatpoint/acpi/device_nvs.asl>
	#include <cpu/intel/common/acpi/cpu.asl>
	#include <southbridge/intel/common/acpi/sleepstates.asl>

	Device (\_SB.PCI0)
	{
		#include <northbridge/intel/haswell/acpi/hostbridge.asl>
		#include <southbridge/intel/lynxpoint/acpi/pch.asl>
		#include <drivers/intel/gma/acpi/default_brightness_levels.asl>
	}

	#include "acpi/pcie_pxsx.asl"

	/* \_SB.PCI0.GLAN + _PRW, so the OS can arm Wake-on-LAN. */
	#include "acpi/lan.asl"

#if CONFIG(TPM2_PTT_ACPI_START)
	/* Intel PTT firmware-TPM 2.0 (\_SB.TPM, ACPI Start Method 2). */
	#include "acpi/tpm.asl"
#endif
}
