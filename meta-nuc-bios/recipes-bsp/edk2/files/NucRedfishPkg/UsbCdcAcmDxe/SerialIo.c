/** @file
  EFI_SERIAL_IO_PROTOCOL over a CDC-ACM bulk endpoint pair.

  SerialIo.Read is a pull interface with no useful way to block: a bulk IN
  transfer with nothing pending NAKs until it times out, so reading straight
  from the wire would stall every caller for the full timeout on an idle link,
  and TerminalDxe polls constantly. A periodic timer therefore drains the
  endpoint into a ring buffer and Read serves from that.

  The timer runs at TPL_CALLBACK, not TPL_NOTIFY: UsbBulkTransfer is
  synchronous and the USB bus driver raises to TPL_CALLBACK internally, which
  would be a lowering — and a fault — from TPL_NOTIFY. Running the callback at
  TPL_CALLBACK also means it cannot preempt a Read at the same level, so the
  ring needs no further guarding.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UsbCdcAcmDxe.h"

#define ACM_DEFAULT_BAUD_RATE   115200
#define ACM_DEFAULT_DATA_BITS   8
#define ACM_DEFAULT_TIMEOUT     1000000  // 1s, in microseconds

/**
  Bytes currently held in the receive ring.

  @param[in]  Dev  Device context.

  @return  Number of buffered bytes.
**/
STATIC
UINTN
RxCount (
  IN USB_CDC_ACM_DEV  *Dev
  )
{
  if (Dev->RxHead >= Dev->RxTail) {
    return Dev->RxHead - Dev->RxTail;
  }

  return USB_CDC_ACM_RX_FIFO_SIZE - Dev->RxTail + Dev->RxHead;
}

/**
  Free space in the receive ring. One byte is always left unused so that a full
  ring is distinguishable from an empty one.

  @param[in]  Dev  Device context.

  @return  Number of bytes that can still be stored.
**/
STATIC
UINTN
RxFree (
  IN USB_CDC_ACM_DEV  *Dev
  )
{
  return USB_CDC_ACM_RX_FIFO_SIZE - 1 - RxCount (Dev);
}

/**
  Append received bytes to the ring, dropping any that do not fit.

  Dropping is deliberate and is the only sane option: this is a console, the
  data has already left the device, and there is nowhere to push back to. A
  full ring means nothing has called Read in 4KB of console output.

  @param[in,out]  Dev     Device context.
  @param[in]      Data    Bytes to store.
  @param[in]      Length  Number of bytes.
**/
STATIC
VOID
RxPut (
  IN OUT USB_CDC_ACM_DEV  *Dev,
  IN     UINT8            *Data,
  IN     UINTN            Length
  )
{
  UINTN  Index;
  UINTN  Room;

  Room = RxFree (Dev);
  if (Length > Room) {
    DEBUG ((
      DEBUG_WARN,
      "UsbCdcAcm: receive ring full, dropping %u byte(s)\n",
      (UINT32)(Length - Room)
      ));
    Length = Room;
  }

  for (Index = 0; Index < Length; Index++) {
    Dev->Rx[Dev->RxHead] = Data[Index];
    Dev->RxHead          = (Dev->RxHead + 1) % USB_CDC_ACM_RX_FIFO_SIZE;
  }
}

/**
  Timer callback: move whatever the gadget has queued into the ring.

  @param[in]  Event    The timer event.
  @param[in]  Context  The device context.
**/
STATIC
VOID
EFIAPI
UsbCdcAcmPoll (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  USB_CDC_ACM_DEV  *Dev;
  EFI_STATUS       Status;
  UINTN            Length;
  UINT32           UsbStatus;

  Dev = (USB_CDC_ACM_DEV *)Context;

  //
  // Only poll when a whole packet can be stored. A short read cannot be
  // resumed, so asking for less than the endpoint's max packet size risks
  // losing the tail of a transfer.
  //
  if (RxFree (Dev) < Dev->BulkInMaxPacket) {
    return;
  }

  Length = Dev->BulkInMaxPacket;
  Status = Dev->DataUsbIo->UsbBulkTransfer (
                             Dev->DataUsbIo,
                             Dev->BulkIn,
                             Dev->RxScratch,
                             &Length,
                             USB_CDC_ACM_XFER_TIMEOUT,
                             &UsbStatus
                             );
  if (EFI_ERROR (Status) || (Length == 0)) {
    //
    // EFI_TIMEOUT is the idle case and by far the common one; anything else is
    // logged but not fatal, because a console that gives up on one failed
    // transfer is worse than one that retries next tick.
    //
    if (EFI_ERROR (Status) && (Status != EFI_TIMEOUT)) {
      DEBUG ((DEBUG_VERBOSE, "UsbCdcAcm: bulk in %r (usb 0x%x)\n", Status, UsbStatus));
    }

    return;
  }

  RxPut (Dev, Dev->RxScratch, Length);
}

/**
  Send a CDC class request on the control interface. Failures are the caller's
  to ignore: the gadget's u_serial core records line coding and control-line
  state without acting on them, so a rejected request costs nothing.

  @param[in]  Dev      Device context.
  @param[in]  Request  CDC request code.
  @param[in]  Value    Request value field.
  @param[in]  Data     Payload, or NULL.
  @param[in]  Length   Payload length.

  @retval EFI_SUCCESS  The request completed.
  @retval other        The transfer failed.
**/
STATIC
EFI_STATUS
AcmClassRequest (
  IN USB_CDC_ACM_DEV  *Dev,
  IN UINT8            Request,
  IN UINT16           Value,
  IN VOID             *Data,
  IN UINTN            Length
  )
{
  EFI_USB_DEVICE_REQUEST  DevReq;
  UINT32                  UsbStatus;

  ZeroMem (&DevReq, sizeof (DevReq));
  DevReq.RequestType = USB_REQ_TYPE_CLASS | USB_TARGET_INTERFACE;
  DevReq.Request     = Request;
  DevReq.Value       = Value;
  DevReq.Index       = Dev->ControlInterfaceNumber;
  DevReq.Length      = (UINT16)Length;

  return Dev->CtrlUsbIo->UsbControlTransfer (
                           Dev->CtrlUsbIo,
                           &DevReq,
                           (Length == 0) ? EfiUsbNoData : EfiUsbDataOut,
                           ACM_DEFAULT_TIMEOUT / 1000,
                           Data,
                           Length,
                           &UsbStatus
                           );
}

/**
  Reset the port: drop anything buffered and re-assert the line state.

  @param[in]  This  Protocol instance pointer.

  @retval EFI_SUCCESS  The port was reset.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmReset (
  IN EFI_SERIAL_IO_PROTOCOL  *This
  )
{
  USB_CDC_ACM_DEV  *Dev;

  Dev         = USB_CDC_ACM_DEV_FROM_SERIAL_IO (This);
  Dev->RxHead = 0;
  Dev->RxTail = 0;

  return This->SetControl (This, Dev->ControlBits);
}

/**
  Set the port attributes. Line coding is forwarded to the device for the
  benefit of anything watching it, but has no effect on the link: the gadget is
  a USB endpoint pair, not a UART, and the bytes move at USB speed regardless.

  @param[in]  This              Protocol instance pointer.
  @param[in]  BaudRate          Requested baud rate; 0 means keep the default.
  @param[in]  ReceiveFifoDepth  Requested FIFO depth; 0 means keep the default.
  @param[in]  Timeout           Read/write timeout in microseconds.
  @param[in]  Parity            Requested parity.
  @param[in]  DataBits          Requested data bits; 0 means keep the default.
  @param[in]  StopBits          Requested stop bits.

  @retval EFI_SUCCESS  The attributes were recorded.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmSetAttributes (
  IN EFI_SERIAL_IO_PROTOCOL  *This,
  IN UINT64                  BaudRate,
  IN UINT32                  ReceiveFifoDepth,
  IN UINT32                  Timeout,
  IN EFI_PARITY_TYPE         Parity,
  IN UINT8                   DataBits,
  IN EFI_STOP_BITS_TYPE      StopBits
  )
{
  USB_CDC_ACM_DEV  *Dev;
  UINT8            LineCoding[7];

  Dev = USB_CDC_ACM_DEV_FROM_SERIAL_IO (This);

  if (BaudRate == 0) {
    BaudRate = ACM_DEFAULT_BAUD_RATE;
  }

  if (DataBits == 0) {
    DataBits = ACM_DEFAULT_DATA_BITS;
  }

  if (Timeout == 0) {
    Timeout = ACM_DEFAULT_TIMEOUT;
  }

  if (Parity == DefaultParity) {
    Parity = NoParity;
  }

  if (StopBits == DefaultStopBits) {
    StopBits = OneStopBit;
  }

  Dev->SerialMode.BaudRate         = BaudRate;
  Dev->SerialMode.Timeout          = Timeout;
  Dev->SerialMode.Parity           = (UINT32)Parity;
  Dev->SerialMode.DataBits         = DataBits;
  Dev->SerialMode.StopBits         = (UINT32)StopBits;
  Dev->SerialMode.ReceiveFifoDepth = USB_CDC_ACM_RX_FIFO_SIZE;

  //
  // SET_LINE_CODING payload (CDC 1.2 Table 17): dwDTERate, bCharFormat,
  // bParityType, bDataBits.
  //
  WriteUnaligned32 ((UINT32 *)&LineCoding[0], (UINT32)BaudRate);
  LineCoding[4] = (StopBits == TwoStopBits) ? 2 : ((StopBits == OneFiveStopBits) ? 1 : 0);
  LineCoding[5] = (UINT8)((Parity == NoParity) ? 0 :
                          (Parity == OddParity) ? 1 :
                          (Parity == EvenParity) ? 2 :
                          (Parity == MarkParity) ? 3 : 4);
  LineCoding[6] = DataBits;

  AcmClassRequest (Dev, USB_CDC_SET_LINE_CODING, 0, LineCoding, sizeof (LineCoding));

  return EFI_SUCCESS;
}

/**
  Set the control bits. Only DTR and RTS reach the wire; the rest are either
  read-only status or unsupported by CDC-ACM.

  @param[in]  This      Protocol instance pointer.
  @param[in]  Control   The control bits to set.

  @retval EFI_SUCCESS            The bits were applied.
  @retval EFI_UNSUPPORTED        A requested bit cannot be set on this port.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmSetControl (
  IN EFI_SERIAL_IO_PROTOCOL  *This,
  IN UINT32                  Control
  )
{
  USB_CDC_ACM_DEV  *Dev;
  UINT16           LineState;

  //
  // Hardware/software flow control has no CDC-ACM equivalent this driver can
  // honour, and silently accepting it would misreport the port's behaviour.
  //
  if ((Control & (EFI_SERIAL_HARDWARE_LOOPBACK_ENABLE |
                  EFI_SERIAL_SOFTWARE_LOOPBACK_ENABLE |
                  EFI_SERIAL_HARDWARE_FLOW_CONTROL_ENABLE)) != 0)
  {
    return EFI_UNSUPPORTED;
  }

  Dev              = USB_CDC_ACM_DEV_FROM_SERIAL_IO (This);
  Dev->ControlBits = Control & (EFI_SERIAL_DATA_TERMINAL_READY | EFI_SERIAL_REQUEST_TO_SEND);

  LineState = 0;
  if ((Dev->ControlBits & EFI_SERIAL_DATA_TERMINAL_READY) != 0) {
    LineState |= USB_CDC_ACM_CONTROL_DTR;
  }

  if ((Dev->ControlBits & EFI_SERIAL_REQUEST_TO_SEND) != 0) {
    LineState |= USB_CDC_ACM_CONTROL_RTS;
  }

  AcmClassRequest (Dev, USB_CDC_SET_CONTROL_LINE_STATE, LineState, NULL, 0);

  return EFI_SUCCESS;
}

/**
  Report the control bits, including the buffer-state bits the console layer
  uses to decide whether a read would block.

  @param[in]   This     Protocol instance pointer.
  @param[out]  Control  Receives the control bits.

  @retval EFI_SUCCESS            The bits were returned.
  @retval EFI_INVALID_PARAMETER  Control is NULL.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmGetControl (
  IN  EFI_SERIAL_IO_PROTOCOL  *This,
  OUT UINT32                  *Control
  )
{
  USB_CDC_ACM_DEV  *Dev;

  if (Control == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = USB_CDC_ACM_DEV_FROM_SERIAL_IO (This);

  //
  // Writes are synchronous, so the output buffer is always empty by the time
  // anyone can ask.
  //
  *Control = Dev->ControlBits | EFI_SERIAL_OUTPUT_BUFFER_EMPTY;

  if (RxCount (Dev) == 0) {
    *Control |= EFI_SERIAL_INPUT_BUFFER_EMPTY;
  }

  return EFI_SUCCESS;
}

/**
  Write bytes to the port.

  @param[in]      This        Protocol instance pointer.
  @param[in,out]  BufferSize  On input the byte count; on output the count
                              actually written.
  @param[in]      Buffer      The bytes to write.

  @retval EFI_SUCCESS       Everything was written.
  @retval EFI_DEVICE_ERROR  The transfer failed.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmWrite (
  IN     EFI_SERIAL_IO_PROTOCOL  *This,
  IN OUT UINTN                   *BufferSize,
  IN     VOID                    *Buffer
  )
{
  USB_CDC_ACM_DEV  *Dev;
  EFI_STATUS       Status;
  UINTN            Length;
  UINT32           UsbStatus;

  if ((BufferSize == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (*BufferSize == 0) {
    return EFI_SUCCESS;
  }

  Dev    = USB_CDC_ACM_DEV_FROM_SERIAL_IO (This);
  Length = *BufferSize;

  Status = Dev->DataUsbIo->UsbBulkTransfer (
                             Dev->DataUsbIo,
                             Dev->BulkOut,
                             Buffer,
                             &Length,
                             Dev->SerialMode.Timeout / 1000,
                             &UsbStatus
                             );

  *BufferSize = Length;

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbCdcAcm: bulk out %r (usb 0x%x)\n", Status, UsbStatus));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  Read buffered bytes from the port.

  @param[in]      This        Protocol instance pointer.
  @param[in,out]  BufferSize  On input the byte count wanted; on output the
                              count actually returned.
  @param[out]     Buffer      Receives the bytes.

  @retval EFI_SUCCESS  The full request was satisfied.
  @retval EFI_TIMEOUT  Fewer bytes than requested were available.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmRead (
  IN     EFI_SERIAL_IO_PROTOCOL  *This,
  IN OUT UINTN                   *BufferSize,
  OUT    VOID                    *Buffer
  )
{
  USB_CDC_ACM_DEV  *Dev;
  UINT8            *Out;
  UINTN            Wanted;
  UINTN            Index;

  if ((BufferSize == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Dev    = USB_CDC_ACM_DEV_FROM_SERIAL_IO (This);
  Out    = (UINT8 *)Buffer;
  Wanted = *BufferSize;

  for (Index = 0; (Index < Wanted) && (RxCount (Dev) > 0); Index++) {
    Out[Index]  = Dev->Rx[Dev->RxTail];
    Dev->RxTail = (Dev->RxTail + 1) % USB_CDC_ACM_RX_FIFO_SIZE;
  }

  *BufferSize = Index;

  return (Index == Wanted) ? EFI_SUCCESS : EFI_TIMEOUT;
}

EFI_STATUS
UsbCdcAcmInstallSerialIo (
  IN USB_CDC_ACM_DEV  *Dev
  )
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *ParentPath;
  UART_DEVICE_PATH          Uart;

  Dev->SerialIo.Revision      = SERIAL_IO_INTERFACE_REVISION;
  Dev->SerialIo.Reset         = UsbCdcAcmReset;
  Dev->SerialIo.SetAttributes = UsbCdcAcmSetAttributes;
  Dev->SerialIo.SetControl    = UsbCdcAcmSetControl;
  Dev->SerialIo.GetControl    = UsbCdcAcmGetControl;
  Dev->SerialIo.Write         = UsbCdcAcmWrite;
  Dev->SerialIo.Read          = UsbCdcAcmRead;
  Dev->SerialIo.Mode          = &Dev->SerialMode;

  Dev->SerialMode.ControlMask = EFI_SERIAL_INPUT_BUFFER_EMPTY |
                                EFI_SERIAL_OUTPUT_BUFFER_EMPTY |
                                EFI_SERIAL_DATA_TERMINAL_READY |
                                EFI_SERIAL_REQUEST_TO_SEND;
  Dev->SerialMode.BaudRate         = ACM_DEFAULT_BAUD_RATE;
  Dev->SerialMode.ReceiveFifoDepth = USB_CDC_ACM_RX_FIFO_SIZE;
  Dev->SerialMode.Timeout          = ACM_DEFAULT_TIMEOUT;
  Dev->SerialMode.Parity           = NoParity;
  Dev->SerialMode.DataBits         = ACM_DEFAULT_DATA_BITS;
  Dev->SerialMode.StopBits         = OneStopBit;

  //
  // SerialIo goes on a child handle whose path ends in a UART() node, not on
  // the controller itself: that is the shape TerminalDxe expects to bind and
  // the shape a ConIn/ConOut variable can name.
  //
  Status = gBS->HandleProtocol (
                  Dev->ControllerHandle,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **)&ParentPath
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (&Uart, sizeof (Uart));
  Uart.Header.Type    = MESSAGING_DEVICE_PATH;
  Uart.Header.SubType = MSG_UART_DP;
  SetDevicePathNodeLength (&Uart.Header, sizeof (UART_DEVICE_PATH));
  Uart.BaudRate = ACM_DEFAULT_BAUD_RATE;
  Uart.DataBits = ACM_DEFAULT_DATA_BITS;
  Uart.Parity   = (UINT8)NoParity;
  Uart.StopBits = (UINT8)OneStopBit;

  Dev->DevicePath = AppendDevicePathNode (ParentPath, &Uart.Header);
  if (Dev->DevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Dev->ChildHandle,
                  &gEfiDevicePathProtocolGuid,
                  Dev->DevicePath,
                  &gEfiSerialIoProtocolGuid,
                  &Dev->SerialIo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    FreePool (Dev->DevicePath);
    Dev->DevicePath = NULL;
    return Status;
  }

  //
  // Tie the child to the parent controller so the driver model tears the two
  // down together.
  //
  Status = gBS->OpenProtocol (
                  Dev->ControllerHandle,
                  &gEfiUsbIoProtocolGuid,
                  (VOID **)&Dev->CtrlUsbIo,
                  Dev->DriverBindingHandle,
                  Dev->ChildHandle,
                  EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  );
  if (EFI_ERROR (Status)) {
    goto ErrorUninstall;
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UsbCdcAcmPoll,
                  Dev,
                  &Dev->PollEvent
                  );
  if (EFI_ERROR (Status)) {
    goto ErrorCloseChild;
  }

  Status = gBS->SetTimer (Dev->PollEvent, TimerPeriodic, USB_CDC_ACM_POLL_INTERVAL);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Dev->PollEvent);
    Dev->PollEvent = NULL;
    goto ErrorCloseChild;
  }

  //
  // Assert DTR/RTS so anything on the BMC side watching modem state sees an
  // open port rather than a dead one.
  //
  UsbCdcAcmSetControl (
    &Dev->SerialIo,
    EFI_SERIAL_DATA_TERMINAL_READY | EFI_SERIAL_REQUEST_TO_SEND
    );

  return EFI_SUCCESS;

ErrorCloseChild:
  gBS->CloseProtocol (
         Dev->ControllerHandle,
         &gEfiUsbIoProtocolGuid,
         Dev->DriverBindingHandle,
         Dev->ChildHandle
         );

ErrorUninstall:
  gBS->UninstallMultipleProtocolInterfaces (
         Dev->ChildHandle,
         &gEfiDevicePathProtocolGuid,
         Dev->DevicePath,
         &gEfiSerialIoProtocolGuid,
         &Dev->SerialIo,
         NULL
         );
  FreePool (Dev->DevicePath);
  Dev->DevicePath = NULL;
  return Status;
}

EFI_STATUS
UsbCdcAcmUninstallSerialIo (
  IN USB_CDC_ACM_DEV  *Dev
  )
{
  EFI_STATUS  Status;

  if (Dev->PollEvent != NULL) {
    gBS->SetTimer (Dev->PollEvent, TimerCancel, 0);
    gBS->CloseEvent (Dev->PollEvent);
    Dev->PollEvent = NULL;
  }

  gBS->CloseProtocol (
         Dev->ControllerHandle,
         &gEfiUsbIoProtocolGuid,
         Dev->DriverBindingHandle,
         Dev->ChildHandle
         );

  Status = gBS->UninstallMultipleProtocolInterfaces (
                  Dev->ChildHandle,
                  &gEfiDevicePathProtocolGuid,
                  Dev->DevicePath,
                  &gEfiSerialIoProtocolGuid,
                  &Dev->SerialIo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    //
    // Something still holds SerialIo. Put the poll back so the port keeps
    // working rather than going quiet with a consumer attached.
    //
    gBS->OpenProtocol (
           Dev->ControllerHandle,
           &gEfiUsbIoProtocolGuid,
           (VOID **)&Dev->CtrlUsbIo,
           Dev->DriverBindingHandle,
           Dev->ChildHandle,
           EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
           );

    if (!EFI_ERROR (
           gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UsbCdcAcmPoll,
                  Dev,
                  &Dev->PollEvent
                  )
           ))
    {
      gBS->SetTimer (Dev->PollEvent, TimerPeriodic, USB_CDC_ACM_POLL_INTERVAL);
    }

    return Status;
  }

  return EFI_SUCCESS;
}
