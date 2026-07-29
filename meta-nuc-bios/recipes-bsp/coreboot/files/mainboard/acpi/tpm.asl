/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Intel PTT fTPM 2.0 via ACPI CRB "Start Method 2", replicating the stock
 * \_SB.TPM. No fixed CRB at 0xFED40000; the ME CRB is at 0xFED70000 (see
 * ptt.c). The OS stages a command and calls _DSM fn 1 to ring the ME doorbell.
 */

Scope (\_SB)
{
	Device (TPM)
	{
		Name (_HID, "MSFT0101")
		Name (_CID, "MSFT0101")
		Name (_UID, One)

		Method (_STA, 0, NotSerialized)
		{
			Return (0x0F)
		}

		Name (_CRS, ResourceTemplate ()
		{
			Memory32Fixed (ReadWrite,
				0xFED70000,	// ME command doorbell window
				0x00001000)
		})

		/* Control area: ctrl_start (+0x0C) executes, HCMD (+0x40) is the Intel
		   doorbell (write 0 to re-arm first, else it hangs), HSTS (+0x44) = status. */
		OperationRegion (CRBD, SystemMemory, 0xFED70000, 0x48)
		Field (CRBD, AnyAcc, NoLock, Preserve)
		{
			Offset (0x0C),
			CSTR,   32,	// ctrl_start
			Offset (0x40),
			HCMD,   32,	// Intel host-command doorbell
			HSTS,   32	// Intel host status
		}

		/* CRB "Start" (_DSM fn 1): re-arm, then execute the staged command. */
		Method (STRT, 3, Serialized)
		{
			If ((ToInteger (Arg1) == One))
			{
				If (((HSTS & 0x03) == 0x03))
				{
					HCMD = Zero
					CSTR = One
				}
			}

			Return (Zero)
		}

		Method (_DSM, 4, Serialized)
		{
			/* TCG "Start Method" CRB interface. */
			If ((Arg0 == ToUUID ("6bbf6cab-5463-4714-b7cd-f0203c0368d4")))
			{
				/* Function 0: supported-function bitmap (fn 0 + fn 1). */
				If ((ToInteger (Arg2) == Zero))
				{
					Return (Buffer (One)
					{
						0x03
					})
				}

				Return (STRT (Arg1, Arg2, Arg3))
			}

			Return (Buffer (One)
			{
				0x00
			})
		}
	}
}
