## @file
# NucRedfish.dsc - platform DSC for building standalone EDK2 Redfish DXE drivers
# (.efi) that are loaded on the NUC's stock BIOS via a Driver#### load option.
#
# Forked from RedfishPkg/RedfishPkg.dsc and extended with:
#   * the NetworkPkg IPv4/HTTP stack (USB CDC-ECM transport, no TLS/SNP),
#   * our platform host-interface library and a replacement platform-config
#     driver (RedfishConfigDriver, backed by the AMI L"Setup" variable),
#   * the RedfishClientPkg BIOS attribute-sync feature layer.
#
# Modules are compiled one at a time with `build -m <inf>`; this DSC only has to
# resolve each module's library/PCD closure.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = NucRedfishPkg
  PLATFORM_GUID                  = 6b1f0d3a-2c4e-4f81-9a2d-7e5c8b0f13a4
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x0001001c
  OUTPUT_DIRECTORY               = Build/NucRedfishPkg
  SUPPORTED_ARCHITECTURES        = IA32|X64|AARCH64|RISCV64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

  #
  # Network feature selection for the USB CDC-ECM Redfish transport.
  #
  DEFINE NETWORK_ENABLE            = TRUE
  DEFINE NETWORK_IP4_ENABLE        = TRUE
  DEFINE NETWORK_IP6_ENABLE        = FALSE
  DEFINE NETWORK_HTTP_ENABLE       = TRUE
  DEFINE NETWORK_HTTP_BOOT_ENABLE  = FALSE
  DEFINE NETWORK_TLS_ENABLE        = FALSE
  DEFINE NETWORK_SNP_ENABLE        = FALSE
  DEFINE NETWORK_PXE_BOOT_ENABLE   = FALSE
  DEFINE NETWORK_ISCSI_ENABLE      = FALSE
  DEFINE NETWORK_VLAN_ENABLE       = FALSE
  DEFINE NETWORK_ALLOW_HTTP_CONNECTIONS = TRUE

  #
  # Redfish feature selection.
  #
  DEFINE REDFISH_ENABLE  = TRUE
  DEFINE REDFISH_CLIENT  = TRUE

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses]
  #
  # Base libraries (union of RedfishPkg.dsc and RedfishClientPkg.dsc).
  #
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  DebugLib|MdePkg/Library/UefiDebugLibStdErr/UefiDebugLibStdErr.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  ReportStatusCodeLib|MdeModulePkg/Library/DxeReportStatusCodeLib/DxeReportStatusCodeLib.inf
  UefiBootManagerLib|MdeModulePkg/Library/UefiBootManagerLib/UefiBootManagerLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
  PerformanceLib|MdePkg/Library/BasePerformanceLibNull/BasePerformanceLibNull.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  DxeServicesLib|MdePkg/Library/DxeServicesLib/DxeServicesLib.inf
  VariablePolicyHelperLib|MdeModulePkg/Library/VariablePolicyHelperLib/VariablePolicyHelperLib.inf

  # For the standalone USB-network transport drivers.
  UefiUsbLib|MdePkg/Library/UefiUsbLib/UefiUsbLib.inf

  # Default SortLib (RedfishRestExDxe/RedfishDiscoverDxe override to BaseSortLib
  # per-component below).
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf

  #
  # Redfish core + client library instances (documented sets).
  #
  !include RedfishPkg/RedfishLibs.dsc.inc
  !include RedfishClientPkg/RedfishClientLibs.dsc.inc

  #
  # ---- OUR OVERRIDES (must come AFTER the includes above: last line wins) ----
  #
  # DxePcdLib (not BasePcdLibNull) so the NetworkPkg DynamicDefault PCDs and the
  # FixedAtBuild pointer PCD PcdRedfishRestExServiceDevicePath resolve.
  #
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf

  # Platform host-interface: our static USB CDC-ECM record.
  RedfishPlatformHostInterfaceLib|NucRedfishPkg/Library/NucRedfishHostInterfaceLib/NucRedfishHostInterfaceLib.inf

  # No BMC bootstrap credential exchange -> Null credential lib (not the IPMI one
  # that RedfishLibs.dsc.inc selects).
  RedfishPlatformCredentialLib|RedfishPkg/Library/PlatformCredentialLibNull/PlatformCredentialLibNull.inf

  # No content-coding, no platform "wanted device" filtering.
  RedfishContentCodingLib|RedfishPkg/Library/RedfishContentCodingLibNull/RedfishContentCodingLibNull.inf
  RedfishPlatformWantedDeviceLib|RedfishPkg/Library/RedfishPlatformWantedDeviceLibNull/RedfishPlatformWantedDeviceLibNull.inf

  # No IPMI transport on this platform.
  IpmiLib|MdeModulePkg/Library/BaseIpmiLibNull/BaseIpmiLibNull.inf
  IpmiCommandLib|MdeModulePkg/Library/BaseIpmiCommandLibNull/BaseIpmiCommandLibNull.inf

#
# NetworkPkg brings its own [Defines]/[Pcds*]/[LibraryClasses]/[Components.*].
# Our NETWORK_* flags above are set first, so NetworkDefines' !ifndef guards keep
# them. This supplies DpcLib/NetLib/IpIoLib/UdpIoLib/TcpIoLib/HttpLib/HttpIoLib
# and the network driver components (built on demand via `build -m`).
#
!include NetworkPkg/Network.dsc.inc

[PcdsFixedAtBuild]
  # BMC Redfish service is plain HTTP on port 80 over the isolated ECM link.
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishServicePort|80

  # Do not attempt the BMC bootstrap-credential (IPMI) handshake.
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishDisableBootstrapCredentialService|TRUE

  # TODO: set to the BMC's real Redfish ServiceRoot UUID; all-zero = match any.
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishServiceUuid|L"00000000-0000-0000-0000-000000000000"

  #
  # Which NIC is the Redfish host interface: match by MAC address node.
  # TODO: 06:00:00:00:00:01 is a placeholder -- must equal the real usb0 ECM
  #       gadget MAC (and NUC_REDFISH_ECM_MAC in NucRedfishHostInterfaceLib.c).
  #
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishRestExServiceDevicePath.DevicePathMatchMode|DEVICE_PATH_MATCH_MAC_NODE
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishRestExServiceDevicePath.DevicePathNum|1
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishRestExServiceDevicePath.DevicePath|{DEVICE_PATH("MAC(060000000001,0x1)")}

[Components]
  #
  # ---- Existing standalone USB-network transports (kept from the original
  #      recipe; SNP producer + CDC-ECM/RNDIS/NCM class bindings) ----
  #
  MdeModulePkg/Bus/Usb/UsbNetwork/NetworkCommon/NetworkCommon.inf
  MdeModulePkg/Bus/Usb/UsbNetwork/UsbCdcEcm/UsbCdcEcm.inf
  MdeModulePkg/Bus/Usb/UsbNetwork/UsbRndis/UsbRndis.inf
  MdeModulePkg/Bus/Usb/UsbNetwork/UsbCdcNcm/UsbCdcNcm.inf

  #
  # ---- Our platform glue ----
  #
  NucRedfishPkg/Library/NucRedfishHostInterfaceLib/NucRedfishHostInterfaceLib.inf
  NucRedfishPkg/RedfishConfigDriver/RedfishConfigDriver.inf

  #
  # ---- RedfishPkg core drivers (from RedfishComponents.dsc.inc, MINUS
  #      RedfishPlatformConfigDxe which RedfishConfigDriver replaces) ----
  #
  RedfishPkg/RestJsonStructureDxe/RestJsonStructureDxe.inf
  RedfishPkg/RedfishHostInterfaceDxe/RedfishHostInterfaceDxe.inf
  RedfishPkg/RedfishRestExDxe/RedfishRestExDxe.inf {
    <LibraryClasses>
      SortLib|MdeModulePkg/Library/BaseSortLib/BaseSortLib.inf
  }
  RedfishPkg/RedfishCredentialDxe/RedfishCredentialDxe.inf
  RedfishPkg/RedfishDiscoverDxe/RedfishDiscoverDxe.inf {
    <LibraryClasses>
      SortLib|MdeModulePkg/Library/BaseSortLib/BaseSortLib.inf
  }
  RedfishPkg/RedfishConfigHandler/RedfishConfigHandlerDriver.inf
  RedfishPkg/RedfishHttpDxe/RedfishHttpDxe.inf
  MdeModulePkg/Universal/RegularExpressionDxe/RegularExpressionDxe.inf

  #
  # ---- RedfishClientPkg BIOS attribute-sync feature layer ----
  #
  RedfishClientPkg/RedfishFeatureCoreDxe/RedfishFeatureCoreDxe.inf
  RedfishClientPkg/RedfishETagDxe/RedfishETagDxe.inf
  RedfishClientPkg/RedfishConfigLangMapDxe/RedfishConfigLangMapDxe.inf
  RedfishClientPkg/HiiToRedfishBiosDxe/HiiToRedfishBiosDxe.inf
  RedfishClientPkg/Features/Bios/v1_0_9/Dxe/BiosDxe.inf
  RedfishClientPkg/Features/BiosAttributeRegistry/v1_3_6/BiosAttributeRegistryDxe.inf
  RedfishClientPkg/Converter/Bios/v1_0_9/RedfishBios_V1_0_9_Dxe.inf
  RedfishClientPkg/Converter/AttributeRegistry/v1_3_6/RedfishAttributeRegistry_V1_3_6_Dxe.inf
