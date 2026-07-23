/** @file
  RedfishConfigDriver - HYBRID EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL producer
  for a stock AMI Aptio BIOS. Replaces RedfishPkg/RedfishPlatformConfigDxe.

  This driver serves two sources behind one config protocol:

    1. AMI L"Setup" NV variable (gAmiSetupFormsetGuid): for every ConfigureLang
       present in the static mAmiSetupMap[] table (the "Bios" schema attributes),
       values are read/written by byte offset inside the L"Setup" variable. This
       is the real BIOS source and needs no HII/IFR walking.

    2. HII fallback via EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL: for any (Schema,
       ConfigureLang) NOT in the AMI table -- e.g. HiiToRedfishBootDxe's boot
       form, whose questions carry x-UEFI-redfish config languages -- values are
       served from the HII database using the config language as the keyword in
       the "x-UEFI-redfish-<Schema>.<Version>" namespace.

  The RedfishClientPkg feature drivers consume this protocol to read/write
  firmware settings as Redfish attributes.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef NUC_REDFISH_CONFIG_DRIVER_H_
#define NUC_REDFISH_CONFIG_DRIVER_H_

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/EdkIIRedfishPlatformConfig.h>
#include <Protocol/HiiConfigKeyword.h>
#include <Protocol/HiiDatabase.h>

//
// x-UEFI-redfish config-language namespace prefix (matches
// RedfishPlatformConfigDxe's CONFIGURE_LANGUAGE_PREFIX). The full namespace is
// "x-UEFI-redfish-<Schema>.<Version>", e.g. "x-UEFI-redfish-ComputerSystem.v1_5_0".
//
#define NUC_REDFISH_XUEFI_PREFIX  L"x-UEFI-redfish-"

//
// Name of the AMI Aptio Setup NV variable.
//
#define AMI_SETUP_VARIABLE_NAME  L"Setup"

//
// Schema this platform-config instance answers for. RedfishClientPkg BIOS
// feature queries with schema "Bios".
//
#define NUC_REDFISH_BIOS_SCHEMA  "Bios"

//
// Supported schema list returned by GetSupportedSchema (';'-separated,
// x-UEFI-redfish-<Schema>.<major>_<minor>_<errata>).
//
#define NUC_REDFISH_SUPPORTED_SCHEMA  "x-UEFI-redfish-Bios.v1_0_9"

//
// One row of the AMI Setup <-> Redfish attribute map.
//
// TODO(scaffold): Offset/Size are placeholders. Populate the real byte offsets
//       of each Setup Question from this firmware's SETUP_DATA layout (extract
//       from the AMITSE/Setup HII sources, or diff dmpstore dumps of L"Setup"
//       while toggling a setting) before trusting reads/writes.
//
typedef struct {
  CONST CHAR8                       *Schema;         // e.g. "Bios"
  CONST CHAR16                      *ConfigureLang;  // e.g. L"/Bios/Attributes/AllowUefi3rdPartyDriver"
  CONST CHAR8                       *AttributeName;  // Redfish attribute name
  CONST CHAR8                       *DisplayName;    // human readable
  CONST CHAR8                       *HelpText;
  CONST CHAR8                       *MenuPath;       // Redfish menu path
  UINT32                            Offset;          // byte offset inside L"Setup"
  UINT32                            Size;            // width in bytes (1/2/4/8)
  EDKII_REDFISH_VALUE_TYPES         ValueType;
  EDKII_REDFISH_ATTRIBUTE_TYPES     AttributeType;
  BOOLEAN                           ResetRequired;
  UINT64                            DefaultValue;    // default (for boolean/integer)
} AMI_SETUP_MAP_ENTRY;

EFI_STATUS
EFIAPI
NucRedfishConfigDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );

#endif // NUC_REDFISH_CONFIG_DRIVER_H_
