/** @file
  Driver binding for the CDC-ACM USB serial console.

  A CDC-ACM function is two interfaces: a communication interface
  (class 0x02, subclass 0x02) carrying the class requests and a notification
  endpoint, and a data interface (class 0x0A) carrying the two bulk endpoints.
  EDK2's USB bus driver publishes one EFI_USB_IO_PROTOCOL handle per interface,
  so this driver binds the communication interface and then finds its sibling
  data interface among the other UsbIo handles on the same USB device.

  Binding the communication interface rather than the data interface is what
  keeps this driver away from the RHI NIC. The BMC composes a network function
  on the same device, and ECM and NCM data interfaces are also class 0x0A —
  binding class 0x0A blind would race UsbCdcEcm/UsbCdcNcm for their data
  interface. Subclass 0x02 belongs to CDC-ACM alone.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UsbCdcAcmDxe.h"

/**
  Test whether a UsbIo handle is a CDC-ACM communication interface.

  @param[in]  UsbIo   The interface to test.

  @retval TRUE   The interface is CDC class, ACM subclass.
  @retval FALSE  It is not, or its descriptor could not be read.
**/
STATIC
BOOLEAN
IsAcmControlInterface (
  IN EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                    Status;
  EFI_USB_INTERFACE_DESCRIPTOR  Desc;

  Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &Desc);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  return (BOOLEAN)((Desc.InterfaceClass == USB_CDC_CLASS) &&
                   (Desc.InterfaceSubClass == USB_CDC_ACM_SUBCLASS));
}

/**
  Return the byte length of a device path up to, but not including, its final
  node. Two interface handles on one USB device share that prefix exactly and
  differ only in the trailing USB() node's interface number, which is what
  makes it a usable sibling test.

  @param[in]  DevicePath  The path to measure.
  @param[out] LastNode    Receives the final (non-end) node, or NULL.

  @return  Size in bytes of the shared prefix; 0 if the path has no nodes.
**/
STATIC
UINTN
DevicePathPrefixSize (
  IN  EFI_DEVICE_PATH_PROTOCOL  *DevicePath,
  OUT EFI_DEVICE_PATH_PROTOCOL  **LastNode
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  EFI_DEVICE_PATH_PROTOCOL  *Last;

  Last = NULL;
  for (Node = DevicePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    Last = Node;
  }

  *LastNode = Last;
  if (Last == NULL) {
    return 0;
  }

  return (UINTN)((UINT8 *)Last - (UINT8 *)DevicePath);
}

/**
  Find the CDC data interface belonging to the same USB device as the given
  control interface, and open its UsbIo.

  @param[in,out]  Dev   Device context. On success DataHandle and DataUsbIo are
                        filled in and the protocol is open BY_DRIVER.

  @retval EFI_SUCCESS    The data interface was found and opened.
  @retval EFI_NOT_FOUND  The device exposes no class 0x0A sibling.
  @retval other          A UEFI service failed.
**/
STATIC
EFI_STATUS
FindDataInterface (
  IN OUT USB_CDC_ACM_DEV  *Dev
  )
{
  EFI_STATUS                    Status;
  EFI_DEVICE_PATH_PROTOCOL      *CtrlPath;
  EFI_DEVICE_PATH_PROTOCOL      *CtrlLast;
  EFI_DEVICE_PATH_PROTOCOL      *CandPath;
  EFI_DEVICE_PATH_PROTOCOL      *CandLast;
  EFI_USB_IO_PROTOCOL           *UsbIo;
  EFI_USB_INTERFACE_DESCRIPTOR  Desc;
  EFI_HANDLE                    *Handles;
  UINTN                         HandleCount;
  UINTN                         CtrlPrefix;
  UINTN                         Index;

  Status = gBS->HandleProtocol (
                  Dev->ControllerHandle,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **)&CtrlPath
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CtrlPrefix = DevicePathPrefixSize (CtrlPath, &CtrlLast);
  if (CtrlLast == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiUsbIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;

  for (Index = 0; Index < HandleCount; Index++) {
    if (Handles[Index] == Dev->ControllerHandle) {
      continue;
    }

    if (EFI_ERROR (
          gBS->HandleProtocol (
                 Handles[Index],
                 &gEfiDevicePathProtocolGuid,
                 (VOID **)&CandPath
                 )
          ))
    {
      continue;
    }

    //
    // Same USB device: identical path prefix, and both end in a USB() node.
    //
    if (DevicePathPrefixSize (CandPath, &CandLast) != CtrlPrefix) {
      continue;
    }

    if ((CandLast == NULL) || (CompareMem (CandPath, CtrlPath, CtrlPrefix) != 0)) {
      continue;
    }

    if ((DevicePathType (CandLast) != MESSAGING_DEVICE_PATH) ||
        (DevicePathSubType (CandLast) != MSG_USB_DP))
    {
      continue;
    }

    Status = gBS->OpenProtocol (
                    Handles[Index],
                    &gEfiUsbIoProtocolGuid,
                    (VOID **)&UsbIo,
                    Dev->DriverBindingHandle,
                    Handles[Index],
                    EFI_OPEN_PROTOCOL_BY_DRIVER
                    );
    if (EFI_ERROR (Status)) {
      //
      // Already claimed, or in use. Not our data interface.
      //
      Status = EFI_NOT_FOUND;
      continue;
    }

    if (!EFI_ERROR (UsbIo->UsbGetInterfaceDescriptor (UsbIo, &Desc)) &&
        (Desc.InterfaceClass == USB_CDC_DATA_CLASS))
    {
      Dev->DataHandle = Handles[Index];
      Dev->DataUsbIo  = UsbIo;
      Status          = EFI_SUCCESS;
      break;
    }

    gBS->CloseProtocol (
           Handles[Index],
           &gEfiUsbIoProtocolGuid,
           Dev->DriverBindingHandle,
           Handles[Index]
           );
    Status = EFI_NOT_FOUND;
  }

  FreePool (Handles);
  return Status;
}

/**
  Resolve the bulk IN and OUT endpoints on the data interface.

  @param[in,out]  Dev   Device context; BulkIn/BulkOut and their max packet
                        sizes are filled in on success.

  @retval EFI_SUCCESS    Both endpoints were found.
  @retval EFI_NOT_FOUND  The interface does not expose a bulk pair.
**/
STATIC
EFI_STATUS
ResolveBulkEndpoints (
  IN OUT USB_CDC_ACM_DEV  *Dev
  )
{
  EFI_STATUS                    Status;
  EFI_USB_INTERFACE_DESCRIPTOR  Interface;
  EFI_USB_ENDPOINT_DESCRIPTOR   Endpoint;
  UINT8                         Index;

  Status = Dev->DataUsbIo->UsbGetInterfaceDescriptor (Dev->DataUsbIo, &Interface);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < Interface.NumEndpoints; Index++) {
    Status = Dev->DataUsbIo->UsbGetEndpointDescriptor (Dev->DataUsbIo, Index, &Endpoint);
    if (EFI_ERROR (Status)) {
      continue;
    }

    if ((Endpoint.Attributes & USB_ENDPOINT_TYPE_MASK) != USB_ENDPOINT_BULK) {
      continue;
    }

    if ((Endpoint.EndpointAddress & USB_ENDPOINT_DIR_IN) != 0) {
      Dev->BulkIn          = Endpoint.EndpointAddress;
      Dev->BulkInMaxPacket = Endpoint.MaxPacketSize;
    } else {
      Dev->BulkOut          = Endpoint.EndpointAddress;
      Dev->BulkOutMaxPacket = Endpoint.MaxPacketSize;
    }
  }

  if ((Dev->BulkIn == 0) || (Dev->BulkOut == 0)) {
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

/**
  Driver Binding Supported: this is a CDC-ACM communication interface.

  @param[in]  This                 Protocol instance pointer.
  @param[in]  ControllerHandle     Handle of the device to test.
  @param[in]  RemainingDevicePath  Unused.

  @retval EFI_SUCCESS      The interface is CDC-ACM.
  @retval EFI_UNSUPPORTED  It is not.
  @retval other            The UsbIo protocol could not be opened.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
{
  EFI_STATUS           Status;
  EFI_USB_IO_PROTOCOL  *UsbIo;
  BOOLEAN              Supported;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiUsbIoProtocolGuid,
                  (VOID **)&UsbIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Supported = IsAcmControlInterface (UsbIo);

  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiUsbIoProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );

  return Supported ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

/**
  Driver Binding Start: claim the ACM function and publish SerialIo.

  @param[in]  This                 Protocol instance pointer.
  @param[in]  ControllerHandle     The CDC-ACM communication interface.
  @param[in]  RemainingDevicePath  Unused.

  @retval EFI_SUCCESS  SerialIo is published on a child handle.
  @retval other        Nothing was published; all resources were released.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
{
  EFI_STATUS                    Status;
  USB_CDC_ACM_DEV               *Dev;
  EFI_USB_INTERFACE_DESCRIPTOR  Interface;

  Dev = AllocateZeroPool (sizeof (USB_CDC_ACM_DEV));
  if (Dev == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dev->Signature           = USB_CDC_ACM_DEV_SIGNATURE;
  Dev->ControllerHandle    = ControllerHandle;
  Dev->DriverBindingHandle = This->DriverBindingHandle;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiUsbIoProtocolGuid,
                  (VOID **)&Dev->CtrlUsbIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    goto ErrorFreeDev;
  }

  Status = Dev->CtrlUsbIo->UsbGetInterfaceDescriptor (Dev->CtrlUsbIo, &Interface);
  if (EFI_ERROR (Status)) {
    goto ErrorCloseCtrl;
  }

  Dev->ControlInterfaceNumber = Interface.InterfaceNumber;

  Status = FindDataInterface (Dev);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbCdcAcm: no CDC data interface sibling: %r\n", Status));
    goto ErrorCloseCtrl;
  }

  Status = ResolveBulkEndpoints (Dev);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbCdcAcm: data interface has no bulk pair: %r\n", Status));
    goto ErrorCloseData;
  }

  Dev->RxScratch = AllocateZeroPool (Dev->BulkInMaxPacket);
  if (Dev->RxScratch == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorCloseData;
  }

  Status = UsbCdcAcmInstallSerialIo (Dev);
  if (EFI_ERROR (Status)) {
    goto ErrorFreeScratch;
  }

  DEBUG ((
    DEBUG_INFO,
    "UsbCdcAcm: serial console on bulk in 0x%02x / out 0x%02x\n",
    Dev->BulkIn,
    Dev->BulkOut
    ));

  return EFI_SUCCESS;

ErrorFreeScratch:
  FreePool (Dev->RxScratch);

ErrorCloseData:
  gBS->CloseProtocol (
         Dev->DataHandle,
         &gEfiUsbIoProtocolGuid,
         This->DriverBindingHandle,
         Dev->DataHandle
         );

ErrorCloseCtrl:
  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiUsbIoProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );

ErrorFreeDev:
  FreePool (Dev);
  return Status;
}

/**
  Driver Binding Stop: tear the ACM function down.

  @param[in]  This               Protocol instance pointer.
  @param[in]  ControllerHandle   The CDC-ACM communication interface.
  @param[in]  NumberOfChildren   Number of child handles to stop.
  @param[in]  ChildHandleBuffer  The child handles.

  @retval EFI_SUCCESS  The driver released the controller.
  @retval other        SerialIo is still in use.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmDriverStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer
  )
{
  EFI_STATUS              Status;
  EFI_SERIAL_IO_PROTOCOL  *SerialIo;
  USB_CDC_ACM_DEV         *Dev;

  if (NumberOfChildren == 0) {
    //
    // Nothing published under this controller any more; the child teardown
    // below already released everything.
    //
    return gBS->CloseProtocol (
                  ControllerHandle,
                  &gEfiUsbIoProtocolGuid,
                  This->DriverBindingHandle,
                  ControllerHandle
                  );
  }

  Status = gBS->OpenProtocol (
                  ChildHandleBuffer[0],
                  &gEfiSerialIoProtocolGuid,
                  (VOID **)&SerialIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Dev = USB_CDC_ACM_DEV_FROM_SERIAL_IO (SerialIo);

  Status = UsbCdcAcmUninstallSerialIo (Dev);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (
         Dev->DataHandle,
         &gEfiUsbIoProtocolGuid,
         This->DriverBindingHandle,
         Dev->DataHandle
         );

  if (Dev->RxScratch != NULL) {
    FreePool (Dev->RxScratch);
  }

  if (Dev->DevicePath != NULL) {
    FreePool (Dev->DevicePath);
  }

  FreePool (Dev);
  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL  gUsbCdcAcmDriverBinding = {
  UsbCdcAcmDriverSupported,
  UsbCdcAcmDriverStart,
  UsbCdcAcmDriverStop,
  0x10,
  NULL,
  NULL
};

GLOBAL_REMOVE_IF_UNREFERENCED
EFI_UNICODE_STRING_TABLE  mUsbCdcAcmDriverNameTable[] = {
  { "eng;en", L"USB CDC-ACM Serial Console Driver" },
  { NULL,     NULL                                 }
};

/**
  Retrieves a Unicode string that is the user-readable name of the driver.

  @param[in]  This        Protocol instance pointer.
  @param[in]  Language    RFC 4646 language code.
  @param[out] DriverName  The driver name.

  @retval EFI_SUCCESS            The name was returned.
  @retval EFI_INVALID_PARAMETER  Language or DriverName is NULL.
  @retval EFI_UNSUPPORTED        The language is not supported.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME2_PROTOCOL  *This,
  IN  CHAR8                         *Language,
  OUT CHAR16                        **DriverName
  )
{
  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mUsbCdcAcmDriverNameTable,
           DriverName,
           FALSE
           );
}

/**
  Retrieves a Unicode string that is the user-readable name of a controller.
  Not supported: this driver names no controllers.

  @param[in]  This              Protocol instance pointer.
  @param[in]  ControllerHandle  The controller to name.
  @param[in]  ChildHandle       The child to name, if any.
  @param[in]  Language          RFC 4646 language code.
  @param[out] ControllerName    The controller name.

  @retval EFI_UNSUPPORTED  Always.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmComponentNameGetControllerName (
  IN  EFI_COMPONENT_NAME2_PROTOCOL  *This,
  IN  EFI_HANDLE                    ControllerHandle,
  IN  EFI_HANDLE                    ChildHandle        OPTIONAL,
  IN  CHAR8                         *Language,
  OUT CHAR16                        **ControllerName
  )
{
  return EFI_UNSUPPORTED;
}

GLOBAL_REMOVE_IF_UNREFERENCED
EFI_COMPONENT_NAME2_PROTOCOL  gUsbCdcAcmComponentName2 = {
  UsbCdcAcmComponentNameGetDriverName,
  UsbCdcAcmComponentNameGetControllerName,
  "en"
};

/**
  Driver entry point.

  @param[in]  ImageHandle  The image handle.
  @param[in]  SystemTable  The system table.

  @retval EFI_SUCCESS  The driver binding protocol was installed.
  @retval other        Installation failed.
**/
EFI_STATUS
EFIAPI
UsbCdcAcmDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  return EfiLibInstallDriverBindingComponentName2 (
           ImageHandle,
           SystemTable,
           &gUsbCdcAcmDriverBinding,
           ImageHandle,
           NULL,
           &gUsbCdcAcmComponentName2
           );
}
