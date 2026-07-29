/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <drivers/option/cfr_frontend.h>

/* Must match enum fan_profile in ramstage.c; takes effect at the next boot. */
static const struct sm_enum_value fan_profile_values[] = {
	{ "Quiet",       0 },
	{ "Balanced",    1 },
	{ "Performance", 2 },
	SM_ENUM_VALUE_END,
};

static const struct sm_object fan_profile = SM_DECLARE_ENUM({
	.opt_name	= "fan_profile",
	.ui_name	= "CPU fan profile",
	.ui_helptext	= "SmartFan curve the NCT5577D uses to drive the CPU fan "
			  "off the CPU (PECI) temperature. Quiet keeps it slow "
			  "and ramps late; Performance ramps early and hard.",
	.default_value	= 1,	/* Balanced */
	.values		= fan_profile_values,
});

static struct sm_obj_form cooling = {
	.ui_name = "Cooling",
	.obj_list = (const struct sm_object *[]) {
		&fan_profile,
		NULL
	},
};

/* Applied by southbridge/intel/lynxpoint (reads "power_on_after_fail"). */
static const struct sm_enum_value power_state_values[] = {
	{ "Power off (S5)", 0 },	/* MAINBOARD_POWER_OFF */
	{ "Power on (S0)",  1 },	/* MAINBOARD_POWER_ON  */
	SM_ENUM_VALUE_END,
};

static const struct sm_object power_on_after_fail = SM_DECLARE_ENUM({
	.opt_name	= "power_on_after_fail",
	.ui_name	= "Restore power after AC loss",
	.ui_helptext	= "What the board does when AC power returns after an "
			  "outage. For a headless server, choose Power on so it "
			  "boots back up unattended.",
	.default_value	= CONFIG_MAINBOARD_POWER_FAILURE_STATE,
	.values		= power_state_values,
});

static struct sm_obj_form power = {
	.ui_name = "Power",
	.obj_list = (const struct sm_object *[]) {
		&power_on_after_fail,
		NULL
	},
};

/* Read by ptt.c ("tpm_enable"): gates the fTPM. */
static const struct sm_enum_value tpm_enable_values[] = {
	{ "Disabled", 0 },
	{ "Enabled",  1 },
	SM_ENUM_VALUE_END,
};

static const struct sm_object tpm_enable = SM_DECLARE_ENUM({
	.opt_name	= "tpm_enable",
	.ui_name	= "Firmware TPM (Intel PTT)",
	.ui_helptext	= "Expose the Intel ME firmware-TPM 2.0 to the OS as "
			  "/dev/tpm0. Disable to hide it from the OS entirely.",
	.default_value	= 1,	/* Enabled */
	.values		= tpm_enable_values,
});

/* Read by ptt.c ("tpm_clear"): one-shot, self-resets after clearing. */
static const struct sm_enum_value tpm_clear_values[] = {
	{ "No",              0 },
	{ "Clear next boot", 1 },
	SM_ENUM_VALUE_END,
};

static const struct sm_object tpm_clear = SM_DECLARE_ENUM({
	.opt_name	= "tpm_clear",
	.ui_name	= "Clear TPM (reset ownership)",
	.ui_helptext	= "At the next boot, reset the firmware-TPM: clears the "
			  "owner/endorsement hierarchy authorizations and evicts "
			  "keys (TPM2_Clear) - this RESETS TPM ownership. Reverts "
			  "to No automatically after it runs.",
	.default_value	= 0,	/* No */
	.values		= tpm_clear_values,
});

static struct sm_obj_form security = {
	.ui_name = "Security",
	.obj_list = (const struct sm_object *[]) {
		&tpm_enable,
		&tpm_clear,
		NULL
	},
};

/* Applied by ramstage.c ("sata_enable"): enables the 00:1f.2 SATA controller. */
static const struct sm_enum_value sata_enable_values[] = {
	{ "Disabled", 0 },
	{ "Enabled",  1 },
	SM_ENUM_VALUE_END,
};

static const struct sm_object sata_enable = SM_DECLARE_ENUM({
	.opt_name	= "sata_enable",
	.ui_name	= "SATA controller",
	.ui_helptext	= "Enable the onboard SATA/AHCI controller for the RYH "
			  "2.5\" bay or the onboard SATA header (port 0). Off by "
			  "default since this board boots from NVMe.",
	.default_value	= 0,	/* Disabled */
	.values		= sata_enable_values,
});

static struct sm_obj_form storage = {
	.ui_name = "Storage",
	.obj_list = (const struct sm_object *[]) {
		&sata_enable,
		NULL
	},
};

static struct sm_obj_form *sm_root[] = {
	&security,
	&power,
	&cooling,
	&storage,
	NULL
};

void mb_cfr_setup_menu(struct lb_cfr *cfr_root)
{
	cfr_write_setup_menu(cfr_root, sm_root);
}
