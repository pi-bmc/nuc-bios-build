/** @file
  NucRedfishHostInterfaceLib

  RedfishPlatformHostInterfaceLib instance for the NUC <-> BMC USB CDC-ECM link.
  Reports a single static "Redfish over IP" protocol record plus a USB V2 device
  descriptor (SMBIOS Type 42h) describing the BMC Redfish service reachable over
  the USB Ethernet gadget. Everything is compiled in, so no NV variables and no
  platform HII are required -- suitable for loading on a stock/locked BIOS.

  Based on RedfishPkg/Library/PlatformHostInterfaceLibNull and the reference
  EmulatorPkg RedfishPlatformHostInterfaceLib.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/RedfishHostInterfaceLib.h>
#include <Library/UefiLib.h>

//
// ---------------------------------------------------------------------------
// Static host-interface parameters. These are the fixed decisions for the
// NUC <-> BMC USB CDC-ECM isolated link.
// ---------------------------------------------------------------------------
//

//
// TODO: NUC_REDFISH_ECM_MAC must equal the real usb0 ECM gadget MAC presented
//       by the BMC, and must match the MAC encoded in the DSC PCD
//       gEfiRedfishPkgTokenSpaceGuid.PcdRedfishRestExServiceDevicePath.
//       06:00:00:00:00:01 is a documented locally-administered placeholder.
//
#define NUC_REDFISH_ECM_MAC_0  0x06
#define NUC_REDFISH_ECM_MAC_1  0x00
#define NUC_REDFISH_ECM_MAC_2  0x00
#define NUC_REDFISH_ECM_MAC_3  0x00
#define NUC_REDFISH_ECM_MAC_4  0x00
#define NUC_REDFISH_ECM_MAC_5  0x01

//
// USB idVendor/idProduct of the gadget. Linux g_ether/CDC-ECM defaults are
// 0x1d6b (Linux Foundation) / 0x0104 (Multifunction Composite Gadget); adjust
// to whatever the BMC gadget advertises.
//
#define NUC_REDFISH_ECM_ID_VENDOR   0x1D6B
#define NUC_REDFISH_ECM_ID_PRODUCT  0x0104

//
// IPv4 addressing on the isolated ECM link (see usb0 RHI isolation decisions).
// Host (NUC/UEFI) 169.254.10.2/16 ; Redfish service (BMC) 169.254.10.1/16.
//
#define NUC_REDFISH_HOST_IP_0  0xA9   // 169
#define NUC_REDFISH_HOST_IP_1  0xFE   // 254
#define NUC_REDFISH_HOST_IP_2  0x0A   //  10
#define NUC_REDFISH_HOST_IP_3  0x02   //   2

#define NUC_REDFISH_SVC_IP_0  0xA9    // 169
#define NUC_REDFISH_SVC_IP_1  0xFE    // 254
#define NUC_REDFISH_SVC_IP_2  0x0A    //  10
#define NUC_REDFISH_SVC_IP_3  0x01    //   1

//
// /16 subnet mask 255.255.0.0.
//
#define NUC_REDFISH_IP_MASK_0  0xFF
#define NUC_REDFISH_IP_MASK_1  0xFF
#define NUC_REDFISH_IP_MASK_2  0x00
#define NUC_REDFISH_IP_MASK_3  0x00

#define NUC_REDFISH_SVC_PORT  80

//
// Hostname advertised in the Type 42h record. Kept identical to the service IP
// so an HTTP client that skips DNS still gets a usable Host: header.
//
#define NUC_REDFISH_SVC_HOSTNAME  "169.254.10.1"

//
// TODO: PcdRedfishServiceUuid in the DSC and this UUID both default to the
//       all-zero UUID. Set them to the BMC's real Redfish ServiceRoot UUID when
//       known; the all-zero value tells RedfishDiscoverDxe "match any".
//
STATIC EFI_GUID  mNucRedfishServiceUuid = {
  0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

/**
  Get platform Redfish host interface device descriptor.

  Returns a USB Network Interface V2 (device type 0x04) descriptor for the ECM
  gadget.

  @param[out] DeviceType        Pointer to retrieve device type.
  @param[out] DeviceDescriptor  Pointer to retrieve REDFISH_INTERFACE_DATA. Caller
                                frees with FreePool().

  @retval EFI_SUCCESS           Descriptor returned.
  @retval EFI_OUT_OF_RESOURCES  Allocation failed.
**/
EFI_STATUS
RedfishPlatformHostInterfaceDeviceDescriptor (
  OUT UINT8                   *DeviceType,
  OUT REDFISH_INTERFACE_DATA  **DeviceDescriptor
  )
{
  REDFISH_INTERFACE_DATA             *InterfaceData;
  USB_INTERFACE_DEVICE_DESCRIPTOR_V2 *Usb;

  if ((DeviceType == NULL) || (DeviceDescriptor == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  InterfaceData = AllocateZeroPool (sizeof (REDFISH_INTERFACE_DATA));
  if (InterfaceData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  InterfaceData->DeviceType = REDFISH_HOST_INTERFACE_DEVICE_TYPE_USB_V2;

  Usb                = &InterfaceData->DeviceDescriptor.UsbDeviceV2;
  Usb->Length        = USB_INTERFACE_DEVICE_DESCRIPTOR_V2_SIZE_1_3;  // 0x11
  Usb->IdVendor      = NUC_REDFISH_ECM_ID_VENDOR;
  Usb->IdProduct     = NUC_REDFISH_ECM_ID_PRODUCT;
  Usb->SerialNumberStr = 0;   // no serial-number string
  Usb->MacAddress[0] = NUC_REDFISH_ECM_MAC_0;
  Usb->MacAddress[1] = NUC_REDFISH_ECM_MAC_1;
  Usb->MacAddress[2] = NUC_REDFISH_ECM_MAC_2;
  Usb->MacAddress[3] = NUC_REDFISH_ECM_MAC_3;
  Usb->MacAddress[4] = NUC_REDFISH_ECM_MAC_4;
  Usb->MacAddress[5] = NUC_REDFISH_ECM_MAC_5;
  Usb->Characteristics               = 0;
  Usb->CredentialBootstrappingHandle = 0;

  *DeviceType       = REDFISH_HOST_INTERFACE_DEVICE_TYPE_USB_V2;
  *DeviceDescriptor = InterfaceData;

  return EFI_SUCCESS;
}

/**
  Get platform Redfish host interface protocol data.

  Produces exactly one "Redfish over IP" protocol record (index 0). Any other
  index returns EFI_NOT_FOUND to terminate the caller's enumeration.

  @param[in,out] ProtocolRecord     Pointer to retrieve the protocol record.
                                     Caller frees with FreePool().
  @param[in]     IndexOfProtocolData Index of the protocol data to return.

  @retval EFI_SUCCESS           Record returned.
  @retval EFI_NOT_FOUND         No more records.
  @retval EFI_OUT_OF_RESOURCES  Allocation failed.
**/
EFI_STATUS
RedfishPlatformHostInterfaceProtocolData (
  IN OUT MC_HOST_INTERFACE_PROTOCOL_RECORD  **ProtocolRecord,
  IN UINT8                                  IndexOfProtocolData
  )
{
  MC_HOST_INTERFACE_PROTOCOL_RECORD  *Record;
  REDFISH_OVER_IP_PROTOCOL_DATA      *Data;
  UINT8                              HostNameSize;
  UINT8                              ProtocolDataSize;
  UINTN                              RecordSize;

  if (ProtocolRecord == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (IndexOfProtocolData != 0) {
    return EFI_NOT_FOUND;
  }

  //
  // Include the terminating NUL in the hostname length, matching the reference
  // implementations.
  //
  HostNameSize     = (UINT8)(AsciiStrLen (NUC_REDFISH_SVC_HOSTNAME) + 1);
  ProtocolDataSize = (UINT8)(sizeof (REDFISH_OVER_IP_PROTOCOL_DATA) - 1 + HostNameSize);

  RecordSize = sizeof (MC_HOST_INTERFACE_PROTOCOL_RECORD) - 1 + ProtocolDataSize;
  Record     = AllocateZeroPool (RecordSize);
  if (Record == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Record->ProtocolType        = MCHostInterfaceProtocolTypeRedfishOverIP;
  Record->ProtocolTypeDataLen = ProtocolDataSize;

  Data = (REDFISH_OVER_IP_PROTOCOL_DATA *)Record->ProtocolTypeData;

  CopyGuid (&Data->ServiceUuid, &mNucRedfishServiceUuid);

  Data->HostIpAssignmentType = REDFISH_HOST_INTERFACE_HOST_IP_ASSIGNMENT_TYPE_STATIC; // 0x01
  Data->HostIpAddressFormat  = REDFISH_HOST_INTERFACE_HOST_IP_ADDRESS_FORMAT_IP4;     // 0x01
  Data->HostIpAddress[0]     = NUC_REDFISH_HOST_IP_0;
  Data->HostIpAddress[1]     = NUC_REDFISH_HOST_IP_1;
  Data->HostIpAddress[2]     = NUC_REDFISH_HOST_IP_2;
  Data->HostIpAddress[3]     = NUC_REDFISH_HOST_IP_3;
  Data->HostIpMask[0]        = NUC_REDFISH_IP_MASK_0;
  Data->HostIpMask[1]        = NUC_REDFISH_IP_MASK_1;
  Data->HostIpMask[2]        = NUC_REDFISH_IP_MASK_2;
  Data->HostIpMask[3]        = NUC_REDFISH_IP_MASK_3;

  Data->RedfishServiceIpDiscoveryType = REDFISH_HOST_INTERFACE_HOST_IP_ASSIGNMENT_TYPE_STATIC; // 0x01
  Data->RedfishServiceIpAddressFormat = REDFISH_HOST_INTERFACE_HOST_IP_ADDRESS_FORMAT_IP4;     // 0x01
  Data->RedfishServiceIpAddress[0]    = NUC_REDFISH_SVC_IP_0;
  Data->RedfishServiceIpAddress[1]    = NUC_REDFISH_SVC_IP_1;
  Data->RedfishServiceIpAddress[2]    = NUC_REDFISH_SVC_IP_2;
  Data->RedfishServiceIpAddress[3]    = NUC_REDFISH_SVC_IP_3;
  Data->RedfishServiceIpMask[0]       = NUC_REDFISH_IP_MASK_0;
  Data->RedfishServiceIpMask[1]       = NUC_REDFISH_IP_MASK_1;
  Data->RedfishServiceIpMask[2]       = NUC_REDFISH_IP_MASK_2;
  Data->RedfishServiceIpMask[3]       = NUC_REDFISH_IP_MASK_3;

  Data->RedfishServiceIpPort         = NUC_REDFISH_SVC_PORT;
  Data->RedfishServiceVlanId         = 0xFFFFFFFF;
  Data->RedfishServiceHostnameLength = HostNameSize;
  AsciiStrCpyS (
    (CHAR8 *)Data->RedfishServiceHostname,
    HostNameSize,
    NUC_REDFISH_SVC_HOSTNAME
    );

  *ProtocolRecord = Record;
  return EFI_SUCCESS;
}

/**
  Notification GUID is not used: the SMBIOS Type 42h record can be built
  immediately because all data is static.

  @param[out] InformationReadinessGuid  Unused.

  @retval EFI_UNSUPPORTED  Notification not required.
**/
EFI_STATUS
RedfishPlatformHostInterfaceNotification (
  OUT EFI_GUID  **InformationReadinessGuid
  )
{
  return EFI_UNSUPPORTED;
}

/**
  No USB serial-number string is exposed.

  @param[out] SerialNumber  Unused.

  @retval EFI_UNSUPPORTED  Serial number not available.
**/
EFI_STATUS
RedfishPlatformHostInterfaceSerialNumber (
  OUT CHAR8  **SerialNumber
  )
{
  return EFI_UNSUPPORTED;
}
