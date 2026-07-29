/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Two jobs, each in the bootblock hook that can actually do it:
 *
 *   mainboard_config_superio()  runs from lynxpoint's
 *       bootblock_early_southbridge_init(), immediately after pch_enable_lpc().
 *       That is the first point at which the NCT5577D at 0x4e is decoded, so
 *       it is where Super I/O pokes belong. The console is not up yet.
 *
 *   bootblock_mainboard_init()  runs after console_init() (see
 *       lib/bootblock.c), so it is the earliest place that can print.
 */

#include <bootblock_common.h>
#include <console/console.h>
#include <device/pci_ops.h>
#include <device/pnp_ops.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <southbridge/intel/lynxpoint/pch_minimal.h>
#include <superio/nuvoton/common/nuvoton.h>
#include <types.h>

/* Devicetree: chip superio/nuvoton/nct6776 at pnp 4e, LDN 0x0a = ACPI. */
#define SIO_ACPI_DEV		PNP_DEV(0x4e, NCT677X_ACPI)

/*
 * NCT5577D LDN 0x0a (ACPI) CR 0xe4[6:5]: what the Super I/O does when VSB
 * returns after AC loss. This is the chip's own policy and it sits upstream of
 * the PCH's AFTERG3_EN -- the direct analogue of what google/jecht does for its
 * ITE in ite_ac_resume_southbridge().
 *
 * The field encoding is the chip's: 0 = stay off, 1 = power on, 3 = restore the
 * state latched in CR 0xe6[4] (maintained by smihandler.c). It lines up with
 * MAINBOARD_POWER_OFF/ON, with KEEP remapped to 3 -- the same mapping
 * superio/nuvoton/common/common.c applies in ramstage.
 */
#define NCT_ACPI_PWR_LOSS_CTL	0xe4
#define  NCT_PWR_LOSS_SHIFT	5
#define  NCT_PWR_LOSS_MASK	(3 << NCT_PWR_LOSS_SHIFT)

/*
 * nuvoton_common_init() programs this again in ramstage, where it can apply the
 * "power_on_after_fail" setup option on top of the Kconfig default. Doing it
 * here as well costs four register accesses and means a boot that dies before
 * ramstage -- bad DIMM, MRC hang -- still leaves a Super I/O that will bring the
 * board back up on the next AC cycle. On a headless box that is the difference
 * between a remote power cycle working and a site visit.
 *
 * Note this runs regardless of whether the devicetree marks LDN 0x0a enabled;
 * the ramstage path does not (nuvoton_common_init() returns early on
 * !dev->enabled).
 */
static void nct5577d_ac_loss_policy(void)
{
	u8 val;

	switch (CONFIG_MAINBOARD_POWER_FAILURE_STATE) {
	case MAINBOARD_POWER_KEEP:
		val = 3;
		break;
	case MAINBOARD_POWER_OFF:
	case MAINBOARD_POWER_ON:
		val = CONFIG_MAINBOARD_POWER_FAILURE_STATE;
		break;
	default:
		return;
	}

	nuvoton_pnp_enter_conf_state(SIO_ACPI_DEV);
	pnp_set_logical_device(SIO_ACPI_DEV);
	pnp_unset_and_set_config(SIO_ACPI_DEV, NCT_ACPI_PWR_LOSS_CTL,
				 NCT_PWR_LOSS_MASK, val << NCT_PWR_LOSS_SHIFT);
	nuvoton_pnp_exit_conf_state(SIO_ACPI_DEV);
}

void mainboard_config_superio(void)
{
	nct5577d_ac_loss_policy();
}

/*
 * GEN_PMCON_3 carries the RTC-well failure flags right next to AFTERG3_EN, and
 * lynxpoint's pch_power_options() does a read-modify-write across the whole
 * register in ramstage. RTC_POWER_FAILED and RTC_BATTERY_DEAD are
 * write-1-to-clear, so that write silently clears whatever they held and
 * nothing downstream -- including reading 0xa4 from the booted OS -- can tell a
 * healthy RTC well from one that lost the coin cell. Sample it here, before
 * anything has touched the register.
 *
 * Also prints AFTERG3_EN as latched by the *previous* boot, which is the value
 * that actually governed this power-on.
 */
static void log_rtc_well_state(void)
{
	const u16 pmcon3 = pci_read_config16(PCH_LPC_DEV, GEN_PMCON_3);

	printk(BIOS_INFO, "GEN_PMCON_3: 0x%04x [AFTERG3_EN=%u]%s%s%s\n",
	       pmcon3, !!(pmcon3 & SLEEP_AFTER_POWER_FAIL),
	       (pmcon3 & RTC_BATTERY_DEAD) ? " RTC_BATTERY_DEAD" : "",
	       (pmcon3 & RTC_POWER_FAILED) ? " RTC_POWER_FAILED" : "",
	       (pmcon3 & SUS_PWR_FLR)      ? " SUS_PWR_FLR"      : "");
}

void bootblock_mainboard_init(void)
{
	log_rtc_well_state();
}
