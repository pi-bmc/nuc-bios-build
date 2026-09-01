/** @file
  CDC-ACM USB serial driver: EFI_SERIAL_IO_PROTOCOL over the BMC's acm.GS0
  gadget function.

  The NanoKVM BMC composes a CDC-ACM serial function whose BMC side is a
  /dev/ttyGS* feeding the web terminal and IPMI SOL. A booted Linux binds it
  with cdc_acm and needs nothing from us; firmware does not, because EDK2 has
  no in-tree CDC-ACM SerialIo driver (UsbSerialDxe in edk2-platforms is
  FTDI-specific). This driver is that missing piece, so the NUC's pre-boot
  console can be redirected over the same port the OS console uses.

  See .claude/docs/host-firmware-contract.md in the nanokvm-app repo for the
  endpoint budget that decided CDC-ACM over the bulk-only f_serial it replaced.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef USB_CDC_ACM_DXE_H_
#define USB_CDC_ACM_DXE_H_

#include <Uefi.h>
#include <Protocol/UsbIo.h>
#include <Protocol/SerialIo.h>
#include <Protocol/DevicePath.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <IndustryStandard/Usb.h>

//
// USB CDC class codes. CDC-ACM is the only CDC subclass that uses 0x02, which
// is what makes the control interface a safe thing to bind on: an ECM, NCM or
// EEM function never presents it, so this driver can never steal the RHI NIC's
// interfaces out from under UsbCdcEcm/UsbCdcNcm.
//
#define USB_CDC_CLASS               0x02
#define USB_CDC_ACM_SUBCLASS        0x02
#define USB_CDC_DATA_CLASS          0x0A

//
// CDC class requests (USB CDC 1.2 Table 19). Both are best-effort here: the
// gadget's u_serial core has no real UART behind it, so it records line coding
// and control-line state and changes no behaviour. They are still sent, because
// a host that never asserts DTR looks hung to anything watching modem state.
//
#define USB_CDC_SET_LINE_CODING         0x20
#define USB_CDC_SET_CONTROL_LINE_STATE  0x22

#define USB_CDC_ACM_CONTROL_DTR  BIT0
#define USB_CDC_ACM_CONTROL_RTS  BIT1

//
// Receive FIFO. EFI_SERIAL_IO_PROTOCOL.Read is a pull interface with no way to
// block usefully, and a bulk IN transfer with nothing pending NAKs until it
// times out — so a Read that went straight to the wire would stall the caller
// for the whole timeout on every idle poll. Instead a periodic timer drains the
// endpoint into this ring and Read serves from it.
//
#define USB_CDC_ACM_RX_FIFO_SIZE  4096

//
// Timer period (100ns units) and the per-transfer timeout in milliseconds.
// The bulk transfer inside the timer callback is synchronous, so the timeout is
// also the worst-case time this driver holds TPL_NOTIFY. 1ms is short enough to
// be invisible and long enough that a full-speed packet completes.
//
#define USB_CDC_ACM_POLL_INTERVAL  (10 * 1000)  // 1 ms
#define USB_CDC_ACM_XFER_TIMEOUT   1            // 1 ms

#define USB_CDC_ACM_DEV_SIGNATURE  SIGNATURE_32 ('U', 'a', 'c', 'm')

typedef struct {
  UINT32                      Signature;

  EFI_HANDLE                  ControllerHandle;  // CDC-ACM control interface
  EFI_HANDLE                  DataHandle;        // sibling CDC data interface
  EFI_HANDLE                  ChildHandle;       // where SerialIo is installed
  EFI_HANDLE                  DriverBindingHandle;

  EFI_USB_IO_PROTOCOL         *CtrlUsbIo;
  EFI_USB_IO_PROTOCOL         *DataUsbIo;

  EFI_SERIAL_IO_PROTOCOL      SerialIo;
  EFI_SERIAL_IO_MODE          SerialMode;
  EFI_DEVICE_PATH_PROTOCOL    *DevicePath;

  UINT8                       ControlInterfaceNumber;
  UINT8                       BulkIn;
  UINT8                       BulkOut;
  UINTN                       BulkInMaxPacket;
  UINTN                       BulkOutMaxPacket;

  EFI_EVENT                   PollEvent;
  UINT8                       *RxScratch;
  UINT8                       Rx[USB_CDC_ACM_RX_FIFO_SIZE];
  UINTN                       RxHead;   // next write
  UINTN                       RxTail;   // next read

  UINT32                      ControlBits;
} USB_CDC_ACM_DEV;

#define USB_CDC_ACM_DEV_FROM_SERIAL_IO(a) \
  CR (a, USB_CDC_ACM_DEV, SerialIo, USB_CDC_ACM_DEV_SIGNATURE)

extern EFI_DRIVER_BINDING_PROTOCOL   gUsbCdcAcmDriverBinding;
extern EFI_COMPONENT_NAME2_PROTOCOL  gUsbCdcAcmComponentName2;

//
// SerialIo.c
//

/**
  Install EFI_SERIAL_IO_PROTOCOL on a child handle of the ACM controller and
  start the receive poll timer.

  @param[in]  Dev   The device context, with both UsbIo handles already open
                    and the bulk endpoints resolved.

  @retval EFI_SUCCESS  The protocol is installed and the timer is running.
  @retval other        Nothing was installed; the caller must tear down.
**/
EFI_STATUS
UsbCdcAcmInstallSerialIo (
  IN USB_CDC_ACM_DEV  *Dev
  );

/**
  Stop the poll timer and uninstall EFI_SERIAL_IO_PROTOCOL.

  @param[in]  Dev   The device context.

  @retval EFI_SUCCESS  Torn down.
  @retval other        The protocol is still in use and stays installed.
**/
EFI_STATUS
UsbCdcAcmUninstallSerialIo (
  IN USB_CDC_ACM_DEV  *Dev
  );

#endif // USB_CDC_ACM_DXE_H_
