/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * _ADR=0 endpoint companions so a driver _DSM (e.g. iwlwifi on the 7265 at
 * RP04) resolves instead of logging AE_BAD_PARAMETER. Board-local, mirrors stock.
 */

Scope (\_SB.PCI0.RP01) { Device (PXSX) { Name (_ADR, Zero) } }	/* 1c.0 */
Scope (\_SB.PCI0.RP04) { Device (PXSX) { Name (_ADR, Zero) } }	/* 1c.3 WLAN */
Scope (\_SB.PCI0.RP05) { Device (PXSX) { Name (_ADR, Zero) } }	/* 1c.4 NVMe */
