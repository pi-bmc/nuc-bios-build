/* SPDX-License-Identifier: GPL-2.0-only */

/* SMM code needs to be kept simple. */
#define __SIMPLE_DEVICE__

#include <acpi/acpi.h>
#include <cpu/x86/smm.h>
#include <device/pnp_ops.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <superio/nuvoton/common/nuvoton.h>
#include <types.h>

/* Devicetree: chip superio/nuvoton/nct6776 at pnp 4e, LDN 0x0a = ACPI. */
#define SIO_ACPI_DEV		PNP_DEV(0x4e, NCT677X_ACPI)

/* LDN 0x0a (ACPI) registers touched on the way into soft-off. */
#define NCT_ACPI_KBD_WAKE	0xe0
#define  NCT_KBD_WAKEUP_PSOUT	(1 << 6)
#define NCT_ACPI_PWR_STATE	0xe6
#define  NCT_PWR_STATE_OFF	(1 << 4)

/*
 * This mirrors superio/nuvoton/common/smm.c, which is upstream's implementation
 * of exactly this. It is not reused because nothing in src/superio/nuvoton adds
 * that file to smm-y, so nuvoton_smi_sleep() is compiled for no board and
 * cannot be linked against; and its ACPI_DEV is built from
 * SUPERIO_NUVOTON_PNP_BASE, a symbol whose Kconfig help text scopes it to the
 * pre-ram serial driver. Eight lines here beats a mainboard Makefile reaching
 * into another subsystem's sources.
 *
 * Two things have to happen before the board reaches S5:
 *
 *   CR 0xe0[6], keyboard wake via PSOUT. nuvoton_common_init() sets this at
 *   boot, and common.c's own comment states that the keyboard-wake bits survive
 *   into S5 and must be turned back off by SMM or ACPI code before soft-off.
 *   This board has no PS/2 keyboard (devicetree: pnp 4e.5 off), so clearing it
 *   is defensive rather than load-bearing.
 *
 *   CR 0xe6[4], the "last power state" latch. nuvoton_common_init() clears it
 *   in ramstage, meaning "this board came up"; set it here, meaning "this board
 *   was switched off deliberately". The Super I/O consults the latch when
 *   CR 0xe4[6:5] holds 3, which is how the "Previous state" choice of the
 *   power_on_after_fail setup option is implemented. Keeping it accurate is
 *   harmless when the option is Off or On, and required the moment it is not.
 */
void mainboard_smi_sleep(u8 slp_typ)
{
	if (slp_typ != ACPI_S5)
		return;

	nuvoton_pnp_enter_conf_state(SIO_ACPI_DEV);
	pnp_set_logical_device(SIO_ACPI_DEV);
	pnp_unset_and_set_config(SIO_ACPI_DEV, NCT_ACPI_KBD_WAKE,
				 NCT_KBD_WAKEUP_PSOUT, 0);
	pnp_unset_and_set_config(SIO_ACPI_DEV, NCT_ACPI_PWR_STATE,
				 0, NCT_PWR_STATE_OFF);
	nuvoton_pnp_exit_conf_state(SIO_ACPI_DEV);

	/*
	 * Wake-on-LAN. The devicetree asks for PME_B0_EN via gpe0_en_4, but
	 * global_smi_enable() runs after that and does disable_gpe(PME_B0_EN)
	 * unconditionally (lynxpoint/smi.c), so by the time an OS is running the
	 * bit is clear again. Linux normally re-arms it through the GLAN _PRW
	 * when it enables wake at shutdown; assert it here as well so a magic
	 * packet can still bring the board up if the OS did not, or could not.
	 *
	 * This is the last firmware that runs before S5, and GPE0_EN lives in
	 * the suspend well -- it survives S5 for as long as AC is present, which
	 * is exactly the window this needs to cover.
	 */
	enable_gpe(PME_B0_EN);
}

/*
 * No mainboard_smi_gpi() here on purpose: cpu/x86/smm/smm_module_handler.c
 * already provides a __weak empty one, and this board enables no alternate GPI
 * SMI sources (devicetree leaves alt_gp_smi_en at 0).
 */
