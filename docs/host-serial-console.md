# Host serial console over the BMC's CDC-ACM gadget

How the NUC reaches the JetKVM's USB serial gadget, what name to use for it, and
which boot phases it can and cannot cover.

The BMC side is `acm.usb0` in the JetKVM's USB gadget, brokered by
`serial_broker.go` and exposed to IPMI SOL by `ipmi_sol.go`. This document is
about the *host* side.

## It is `ttyACM0`, not `ttyAMA0`

Worth stating plainly, because the two look alike and only one exists here.

`ttyAMA*` is the ARM PL011 driver (`drivers/tty/serial/amba-pl011.c`). It binds
an on-SoC UART on ARM hardware — a Raspberry Pi's GPIO serial, or the JetKVM's
own console. Nothing on an x86 host ever produces one.

The NUC sees a **USB CDC-ACM peripheral**, driven by `cdc_acm.ko`, which creates
`/dev/ttyACM<N>`. That is the node, and this whole document is about not writing
the `<N>` down anywhere.

## Why `/dev/ttyACM0` is the wrong name to configure

`N` is assigned in enumeration order across every CDC-ACM device on the machine.
It is `ttyACM0` today because the BMC is the only one. Add a UPS, a managed PDU,
an Arduino, a second BMC — or have USB enumerate in a different order after a
firmware change — and the console silently becomes `ttyACM1` while `ttyACM0`
becomes something else entirely. Nothing errors: a getty comes up on the wrong
device, and `console=ttyACM0` sends the kernel log to whatever answered first.

Use a name derived from the device's identity instead.

## The gadget now has a serial number

It did not until now. `defaultUsbConfig.SerialNumber` was `""`, so the
`strings/0x409/serialnumber` descriptor was empty and udev had nothing unique to
build a path from — every JetKVM produced the same `/dev/serial/by-id/` entry.

`initUsbGadget()` in the kvm repo now sets it to `GetDeviceID()`, the RV1106
serial from `/proc/cpuinfo`. That is the same value the CDC-ECM MACs and the
IPMI GUID derive from, so the serial console, the Redfish host interface and the
BMC's own identity all trace back to one unit.

For the JetKVM on this bench that is `31a9d7bc56e8b54d`, giving:

```
/dev/serial/by-id/usb-JetKVM_USB_Emulation_Device_31a9d7bc56e8b54d-if##-port0
```

**Do not use that path either.** The `if##` is the CDC-ACM control interface
number, which is assigned by the order functions were linked into the gadget
config. BMC mode changes the function set (adding CDC-ECM and mass storage), so
`if##` moves when the USB class selection changes — the exact scenario where you
least want the console to disappear.

## Use a udev rule

Matching on the device's identity and the *driver*, rather than on any number:

`/etc/udev/rules.d/70-jetkvm-console.rules` on the NUC:

```udev
# The JetKVM BMC's CDC-ACM serial console.
#
# Matched on the gadget's USB serial number rather than on ttyACM<N> or on the
# by-id interface index: N moves when another CDC-ACM device appears, and the
# interface index moves when BMC mode changes the gadget's function set.
SUBSYSTEM=="tty", SUBSYSTEMS=="usb", DRIVERS=="cdc_acm", \
  ATTRS{idVendor}=="1d6b", ATTRS{idProduct}=="0104", \
  ATTRS{serial}=="31a9d7bc56e8b54d", \
  SYMLINK+="ttyBMC", TAG+="systemd"
```

Then `/dev/ttyBMC` is the name to put in every other config. Install with:

```sh
udevadm control --reload-rules && udevadm trigger --subsystem-match=tty
ls -l /dev/ttyBMC
```

To make the rule portable across units, drop the `ATTRS{serial}` line — the
vendor/product pair plus `DRIVERS=="cdc_acm"` already identifies a JetKVM
uniquely on a host that has only one.

## What each boot phase can actually use

This is the part worth reading before wiring anything to it. A USB serial device
is not a UART, and three of the five phases cannot use one at all.

| Phase | Works? | Why |
| --- | --- | --- |
| coreboot / EDK2 payload | **No** | The payload has no USB CDC-ACM driver. Nothing in UefiPayloadPkg binds a USB serial peripheral, and this board routes no physical UART, so `cbmem -c` remains the only firmware log. |
| GRUB | **No** | GRUB's `serial` command drives 8250-family UARTs and EFI serial. It has no USB serial support. |
| Linux `earlycon` | **No** | `earlycon` runs before driver init, from a device usable with a fixed MMIO/port address. USB requires the whole host-controller stack to be up, so no USB device can ever be an earlycon. |
| Linux `console=` | **Yes, with a catch** | See below. |
| Login shell (getty) | **Yes** | `systemd` handles it. |

### `console=ttyACM0` and the log replay

The catch is worth understanding rather than working around.

The console cannot attach until `cdc_acm` has probed, which is long after the
kernel starts printing. But the kernel does not discard what it printed in the
meantime: a console registered with `CON_PRINTBUFFER` — which a `console=`
device is — gets the entire printk ring buffer replayed to it the moment it
registers. So you **do** get every early message, arriving as one burst when USB
comes up rather than live.

That covers everything except a panic before USB init. For those you still need
`cbmem -c` or a crash dump.

Note the kernel parameter must name the raw device, not the symlink — the
kernel parses `console=` long before udev exists:

```
console=tty0 console=ttyACM0,115200
```

The baud rate is meaningless on CDC-ACM (there is no line to clock) but is
harmless, and some tools expect the field. Listing `tty0` first and `ttyACM0`
last makes the USB console the one that receives `/dev/console` writes, while
still logging to the screen.

`cdc_acm` must be reachable at that point: built in, or included in the initramfs
(`echo cdc_acm >> /etc/initramfs-tools/modules && update-initramfs -u` on
Debian).

### A getty

`systemd` auto-instantiates `serial-getty@` for any device named in `console=`,
so if you set the kernel parameter above you get a login prompt for free. To have
one without making it the kernel console:

```sh
systemctl enable --now serial-getty@ttyACM0.service
```

That unit name has to be the real device, not `ttyBMC` — systemd derives the
device it waits on from the unit name.

## Verifying from the BMC side

The JetKVM's end is `/dev/ttyGS0`. With the host writing to its console:

```sh
ssh root@10.0.107.71 'timeout 5 cat /dev/ttyGS0'
```

Nothing appearing means the host is not writing, not that the link is down —
test the link by typing at it, since the getty will echo:

```sh
ssh root@10.0.107.71 'printf "\r" > /dev/ttyGS0; timeout 3 cat /dev/ttyGS0'
```

Do not open `/dev/ttyGS0` directly while the app is running. `serial_broker.go`
owns it, and a second reader steals bytes from the first — each gets a random
half of the stream with nothing to say why. Use the broker (the web terminal, or
`ipmitool sol activate`) instead.
