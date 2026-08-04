/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <cf9_reset.h>
#include <device/device.h>
#include <smbios.h>
#include <uuid.h>

#include "nuc5i5ryb.h"

/*
 * SMBIOS type 1 system UUID. The weak default leaves the field zeroed, which
 * dmidecode reports as "Not Settable": the stock BIOS kept its UUID in its own
 * NVRAM, and flashing coreboot erased it. A missing UUID is not cosmetic here
 * -- it is why RedfishResourceIdentifyLibComputerSystem could not match this
 * host's ComputerSystem and had to be resolved to the Null instance, and it
 * leaves the Redfish host interface with no UUID to report to the BMC.
 *
 * The value is version-5 (name-based), derived from this board's one burned-in
 * unique identifier, the onboard I218-V's MAC address:
 *
 *   uuidgen --sha1 --namespace @dns --name "b8:ae:ed:7e:3f:6e"
 *
 * A constant rather than a runtime derivation on purpose: at SMBIOS-write time
 * the MAC lives in the GbE flash region, and parsing that from ramstage buys
 * nothing over deriving it once here. Regenerate if this port ever drives a
 * different physical board.
 *
 * parse_uuid() emits the SMBIOS 2.6+ byte order (first three fields
 * little-endian) and leaves the field zeroed -- "not settable", never garbage
 * -- if the string is malformed.
 */
#define SYSTEM_UUID "d97878df-9997-5a65-9e67-8f035e3fd79d"

void smbios_system_set_uuid(u8 *uuid)
{
	parse_uuid(uuid, SYSTEM_UUID);
}

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
