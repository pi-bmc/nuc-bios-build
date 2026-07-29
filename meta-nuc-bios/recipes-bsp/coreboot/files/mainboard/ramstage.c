/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <device/device.h>
#include <option.h>
#include <superio/nuvoton/common/hwm.h>

/* NCT5577D hardware-monitor I/O base (devicetree: pnp 4e.b io 0x60 = 0xa00). */
#define HWM_IOBASE	0xa00

/* Broadwell-U (i5-5250U) TjMax, degrees Celsius. */
#define CPU_TJMAX	105

/* Per-fan register bank. The NUC's CPU fan is fan2 => CPUFAN. */
#define BANK_CPUFAN	2

/* 0x1c (PECI0_CAL) is rejected by the nct6776 ("Invalid temperature source
   28"); use the CPU diode CPUTIN. */
#define FAN_SOURCE_CPUTIN	0x02

/* Fan profiles selectable via CFR (cfr.c); chip runs SmartFan IV autonomously. */
enum fan_profile {
	FAN_PROFILE_QUIET	= 0,
	FAN_PROFILE_BALANCED	= 1,
	FAN_PROFILE_PERFORMANCE	= 2,
};

#define DUTY(perc)	NUVOTON_PERCENT_TO_DUTY(perc)

static const struct nuvoton_fan_curve cpu_fan_curves[] = {
	[FAN_PROFILE_QUIET] = {
		.name			= "CPUFAN/Quiet",
		.bank			= BANK_CPUFAN,
		.source			= FAN_SOURCE_CPUTIN,
		.temp			= { 45, 60, 75, 90 },
		.duty			= { DUTY(20), DUTY(30), DUTY(45), DUTY(70) },
		.temp_tolerance		= 3,
		.step_up_time		= 4,
		.step_down_time		= 8,
		.duty_per_step_up	= 2,
		.duty_per_step_down	= 1,
		.crit_temp		= 100,
		.crit_duty_en		= 1,
		.crit_duty		= DUTY(100),
		.crit_temp_tolerance	= 2,
	},
	[FAN_PROFILE_BALANCED] = {
		.name			= "CPUFAN/Balanced",
		.bank			= BANK_CPUFAN,
		.source			= FAN_SOURCE_CPUTIN,
		.temp			= { 40, 55, 70, 85 },
		.duty			= { DUTY(25), DUTY(40), DUTY(60), DUTY(85) },
		.temp_tolerance		= 2,
		.step_up_time		= 2,
		.step_down_time		= 4,
		.duty_per_step_up	= 4,
		.duty_per_step_down	= 2,
		.crit_temp		= 100,
		.crit_duty_en		= 1,
		.crit_duty		= DUTY(100),
		.crit_temp_tolerance	= 2,
	},
	[FAN_PROFILE_PERFORMANCE] = {
		.name			= "CPUFAN/Performance",
		.bank			= BANK_CPUFAN,
		.source			= FAN_SOURCE_CPUTIN,
		.temp			= { 35, 50, 65, 80 },
		.duty			= { DUTY(35), DUTY(55), DUTY(80), DUTY(100) },
		.temp_tolerance		= 1,
		.step_up_time		= 1,
		.step_down_time		= 2,
		.duty_per_step_up	= 8,
		.duty_per_step_down	= 4,
		.crit_temp		= 100,
		.crit_duty_en		= 1,
		.crit_duty		= DUTY(100),
		.crit_temp_tolerance	= 2,
	},
};

static void hwm_init(void *unused)
{
	/* Profile is picked in the CFR setup menu (see cfr.c), stored in SMMSTORE. */
	unsigned int profile = get_uint_option("fan_profile", FAN_PROFILE_BALANCED);
	if (profile > FAN_PROFILE_PERFORMANCE)
		profile = FAN_PROFILE_BALANCED;

	/* Route the CPU's PECI temperature into the HWM as the fan source. */
	nuvoton_hwm_enable_peci(HWM_IOBASE, CPU_TJMAX);
	nuvoton_hwm_enable_peci_calibration(HWM_IOBASE);

	nuvoton_hwm_configure_fan(HWM_IOBASE, &cpu_fan_curves[profile]);
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, hwm_init, NULL);

/* SATA (00:1f.2) is off in the devicetree; enable it from "sata_enable"
   before enumeration. */
static void sata_enable_from_option(void *unused)
{
	struct device *sata = pcidev_on_root(0x1f, 2);
	if (sata)
		sata->enabled = get_uint_option("sata_enable", 0);
}
BOOT_STATE_INIT_ENTRY(BS_DEV_ENUMERATE, BS_ON_ENTRY, sata_enable_from_option, NULL);
