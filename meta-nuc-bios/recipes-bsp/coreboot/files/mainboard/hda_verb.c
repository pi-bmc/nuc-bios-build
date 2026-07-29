/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/azalia_device.h>

/*
 * Realtek ALC283 pin configuration, read from the stock running system
 * (/sys/class/sound/hwC0D0/init_pin_configs). Front headphone (0x21) + mic
 * (0x19) combo jack; everything else disabled (AZALIA_PIN_CFG_NC(0)). The HDMI/DP audio
 * (Intel codec on the iGPU) is programmed by the graphics driver at runtime,
 * so it is not listed here.
 */
const u32 cim_verb_data[] = {
	0x10ec0283,	/* Codec Vendor / Device ID: Realtek ALC283 */
	0x80862057,	/* Subsystem ID */
	11,		/* Number of 4 dword sets */

	AZALIA_SUBVENDOR(0, 0x80862057),
	AZALIA_PIN_CFG(0, 0x12, 0x40000000),
	AZALIA_PIN_CFG(0, 0x14, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, 0x17, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, 0x18, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, 0x19, 0x03a11020),
	AZALIA_PIN_CFG(0, 0x1a, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, 0x1b, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, 0x1d, 0x40500001),
	AZALIA_PIN_CFG(0, 0x1e, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, 0x21, 0x03211010),
};

const u32 pc_beep_verbs[] = {};

AZALIA_ARRAY_SIZES;
