/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * The I218-V's MAC is the PCH's own GbE controller at 00:19.0. Unlike the xHCI
 * and EHCI devices, southbridge/intel/lynxpoint/acpi/pch.asl declares no ACPI
 * device for it, so the OS has no wake object to arm and Wake-on-LAN cannot be
 * enabled through ACPI no matter what ethtool is told.
 *
 * _PRW names the same GPE those controllers use -- 0x6d (109) is PME_B0 in the
 * standard-event block, which is lynxpoint's DEFAULT_PRW_VALUE on LP silicon.
 * The second element is the deepest sleep state the device may wake from. It
 * must be 5: Linux's acpi_enable_wakeup_devices() skips devices whose depth is
 * below the target sleep state, and at poweroff the target is S5 -- a depth-4
 * entry is ignored at exactly the moment Wake-on-LAN needs arming.
 */

Scope (\_SB.PCI0)
{
	Device (GLAN)
	{
		Name (_ADR, 0x00190000)
		Name (_PRW, Package () { 0x6d, 5 })
	}
}
