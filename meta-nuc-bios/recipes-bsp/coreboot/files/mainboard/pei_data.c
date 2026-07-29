/* SPDX-License-Identifier: GPL-2.0-only */

#include <soc/pei_data.h>
#include <soc/pei_wrapper.h>

/*
 * NUC5i5RYB: two DDR3L SO-DIMM slots, one per memory channel, each carrying a
 * real SPD EEPROM on the PCH SMBus. The MRC reads them itself -- the PCH's
 * early init has already brought the controller up (lynxpoint/early_pch.c
 * enable_smbus()) and broadwell_fill_pei_data() hands over smbusbar. Nothing
 * is memory-down, so there is deliberately no spd.bin in CBFS and no
 * HAVE_SPD_IN_CBFS; copy_spd() never runs.
 *
 * addresses[] is [channel][slot] flattened:
 *   [0] ch0 slot0   [1] ch0 slot1
 *   [2] ch1 slot0   [3] ch1 slot1
 * The two populated slots are therefore [0] and [2], not [0] and [1] --
 * writing [1] would put both modules on channel 0 and silently lose
 * dual-channel.
 *
 * Leaving [1] and [3] at zero is load-bearing, not an omission: it is what
 * make_channel_disabled_mask() reads to tell the MRC that the second slot on
 * each channel is unimplemented (mask 2 = "disable dimm 1 on channel"). Do
 * not fill them in.
 *
 * These are 7-bit addresses; broadwell/raminit.c left-shifts them for the MRC
 * (0x50 -> 0xa0, 0x52 -> 0xa4).
 */
void mb_get_spd_map(struct spd_info *spdi)
{
	spdi->addresses[0] = 0x50;	/* channel 0, slot 0 */
	spdi->addresses[2] = 0x52;	/* channel 1, slot 0 */
}

/*
 * First pass: all ports on, OC skipped until the real per-port routing is known.
 * TODO: real OC pins, per-port enable, USB2 port lengths.
 */
const struct usb2_port_setting mainboard_usb2_ports[MAX_USB2_PORTS] = {
	{ 0x0040, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL }, /* P1 */
	{ 0x0040, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL }, /* P2 */
	{ 0x0040, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL }, /* P3 */
	{ 0x0040, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL }, /* P4 */
	{ 0x0040, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL }, /* P5 */
	{ 0x0040, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL }, /* P6 */
	{ 0x0040, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL }, /* P7 */
	{ 0x0040, 1, USB_OC_PIN_SKIP, USB_PORT_BACK_PANEL }, /* P8 */
};

const struct usb3_port_setting mainboard_usb3_ports[MAX_USB3_PORTS] = {
	{ 1, USB_OC_PIN_SKIP, 0 }, /* P1 */
	{ 1, USB_OC_PIN_SKIP, 0 }, /* P2 */
	{ 1, USB_OC_PIN_SKIP, 0 }, /* P3 */
	{ 1, USB_OC_PIN_SKIP, 0 }, /* P4 */
};
