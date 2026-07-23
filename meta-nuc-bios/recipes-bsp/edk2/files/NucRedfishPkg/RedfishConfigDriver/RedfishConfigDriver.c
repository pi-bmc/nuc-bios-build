/** @file
  RedfishConfigDriver - EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL producer backed by
  the AMI Aptio L"Setup" NV variable. Replaces RedfishPkg/RedfishPlatformConfigDxe
  on a stock BIOS where walking HII/IFR is not practical.

  This is a SCAFFOLD: the attribute map (mAmiSetupMap) contains example rows with
  placeholder offsets. GetValue/SetValue read/write the mapped byte range of a
  cached copy of L"Setup"; the remaining protocol members are synthesized from
  the same table. See the TODOs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "RedfishConfigDriver.h"

extern EFI_GUID  gAmiSetupFormsetGuid;
extern EFI_GUID  gEdkIIRedfishPlatformConfigProtocolGuid;

//
// -------------------------------------------------------------------------
// Static AMI Setup <-> Redfish attribute map (SCAFFOLD / EXAMPLE ROWS).
//
// TODO: replace Offset/Size/Default with values extracted from this exact
//       NUC5i7RYH firmware's Setup layout. These two rows exist only to give
//       the protocol something to iterate over so the scaffold compiles and
//       links; the offsets are almost certainly wrong for the real firmware.
// -------------------------------------------------------------------------
//
STATIC AMI_SETUP_MAP_ENTRY  mAmiSetupMap[] = {
  {
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/AllowUefi3rdPartyDriver",
    "AllowUefi3rdPartyDriver",
    "Allow UEFI 3rd Party Driver",
    "Enable loading of unsigned third-party UEFI drivers.",
    "/Bios",
    0x0000,   // TODO real offset in L"Setup"
    1,        // 1 byte boolean
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    0
  },
  {
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/BootTimeout",
    "BootTimeout",
    "Boot Timeout (seconds)",
    "Number of seconds the firmware waits before the default boot.",
    "/Bios",
    0x0002,   // TODO real offset in L"Setup"
    2,        // 2 byte integer
    RedfishValueTypeInteger,
    RedfishAttributeTypeInteger,
    FALSE,
    5
  }
};

#define AMI_SETUP_MAP_COUNT  (sizeof (mAmiSetupMap) / sizeof (mAmiSetupMap[0]))

//
// Cached copy of the L"Setup" variable and its size.
//
STATIC UINT8  *mSetupData     = NULL;
STATIC UINTN  mSetupDataSize  = 0;

/**
  (Re)load the AMI L"Setup" variable into the module-global cache.

  @retval EFI_SUCCESS  Cache is populated.
  @retval Others       GetVariable failed.
**/
STATIC
EFI_STATUS
CacheSetupVariable (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Size;

  Size   = 0;
  Status = gRT->GetVariable (AMI_SETUP_VARIABLE_NAME, &gAmiSetupFormsetGuid, NULL, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    DEBUG ((DEBUG_WARN, "%a: L\"Setup\" not found (%r)\n", __func__, Status));
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }

  if (mSetupData != NULL) {
    FreePool (mSetupData);
    mSetupData = NULL;
  }

  mSetupData = AllocateZeroPool (Size);
  if (mSetupData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gRT->GetVariable (AMI_SETUP_VARIABLE_NAME, &gAmiSetupFormsetGuid, NULL, &Size, mSetupData);
  if (EFI_ERROR (Status)) {
    FreePool (mSetupData);
    mSetupData     = NULL;
    mSetupDataSize = 0;
    return Status;
  }

  mSetupDataSize = Size;
  return EFI_SUCCESS;
}

/**
  Find the map entry whose ConfigureLang matches, for the given Schema.

  @param[in]  Schema         Schema string (ASCII) or NULL to ignore.
  @param[in]  ConfigureLang  Configure language (Unicode) to match.

  @return Pointer to the matching entry, or NULL.
**/
STATIC
AMI_SETUP_MAP_ENTRY *
FindEntryByConfigureLang (
  IN CHAR8       *Schema,
  IN EFI_STRING  ConfigureLang
  )
{
  UINTN  Index;

  if (ConfigureLang == NULL) {
    return NULL;
  }

  for (Index = 0; Index < AMI_SETUP_MAP_COUNT; Index++) {
    if ((Schema != NULL) && (AsciiStrCmp (Schema, mAmiSetupMap[Index].Schema) != 0)) {
      continue;
    }

    if (StrCmp ((CHAR16 *)mAmiSetupMap[Index].ConfigureLang, ConfigureLang) == 0) {
      return &mAmiSetupMap[Index];
    }
  }

  return NULL;
}

/**
  Read Entry->Size bytes at Entry->Offset from the cached Setup buffer as an
  unsigned integer (little endian).

  @param[in]   Entry  Map entry.
  @param[out]  Value  Retrieved raw value.

  @retval EFI_SUCCESS  Value read.
  @retval Others       Offset/size out of range or cache empty.
**/
STATIC
EFI_STATUS
ReadSetupValue (
  IN  AMI_SETUP_MAP_ENTRY  *Entry,
  OUT UINT64               *Value
  )
{
  UINT64  Raw;
  UINT32  Index;

  if ((mSetupData == NULL) || (Entry->Size == 0) || (Entry->Size > sizeof (UINT64))) {
    return EFI_INVALID_PARAMETER;
  }

  if (((UINTN)Entry->Offset + Entry->Size) > mSetupDataSize) {
    return EFI_BUFFER_TOO_SMALL;
  }

  Raw = 0;
  for (Index = 0; Index < Entry->Size; Index++) {
    Raw |= ((UINT64)mSetupData[Entry->Offset + Index]) << (8 * Index);
  }

  *Value = Raw;
  return EFI_SUCCESS;
}

/**
  Write Entry->Size bytes at Entry->Offset in the cached Setup buffer and flush
  the whole variable back to NV storage.

  @param[in]  Entry  Map entry.
  @param[in]  Value  Raw value to store (little endian).

  @retval EFI_SUCCESS  Written and persisted.
  @retval Others       Failure.
**/
STATIC
EFI_STATUS
WriteSetupValue (
  IN AMI_SETUP_MAP_ENTRY  *Entry,
  IN UINT64               Value
  )
{
  UINT32      Index;
  UINT32      Attributes;
  EFI_STATUS  Status;
  UINTN       Size;

  if ((mSetupData == NULL) || (Entry->Size == 0) || (Entry->Size > sizeof (UINT64))) {
    return EFI_INVALID_PARAMETER;
  }

  if (((UINTN)Entry->Offset + Entry->Size) > mSetupDataSize) {
    return EFI_BUFFER_TOO_SMALL;
  }

  for (Index = 0; Index < Entry->Size; Index++) {
    mSetupData[Entry->Offset + Index] = (UINT8)((Value >> (8 * Index)) & 0xFF);
  }

  //
  // AMI Setup is NV + BS (+ RT on many builds). Read current attributes so we
  // preserve them on the write-back.
  //
  Attributes = 0;
  Size       = 0;
  Status     = gRT->GetVariable (AMI_SETUP_VARIABLE_NAME, &gAmiSetupFormsetGuid, &Attributes, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    Attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;
  }

  return gRT->SetVariable (
                AMI_SETUP_VARIABLE_NAME,
                &gAmiSetupFormsetGuid,
                Attributes,
                mSetupDataSize,
                mSetupData
                );
}

//
// -------------------------------------------------------------------------
// EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL members
// -------------------------------------------------------------------------
//

/**
  GetValue: return the current value of the mapped Setup Question.
**/
EFI_STATUS
EFIAPI
NucGetValue (
  IN     EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL  *This,
  IN     CHAR8                                   *Schema,
  IN     CHAR8                                   *Version,
  IN     EFI_STRING                              ConfigureLang,
  OUT    EDKII_REDFISH_VALUE                     *Value
  )
{
  AMI_SETUP_MAP_ENTRY  *Entry;
  EFI_STATUS           Status;
  UINT64               Raw;

  if (Value == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Value, sizeof (*Value));
  Value->Type = RedfishValueTypeUnknown;

  Entry = FindEntryByConfigureLang (Schema, ConfigureLang);
  if (Entry == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = CacheSetupVariable ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ReadSetupValue (Entry, &Raw);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Value->Type = Entry->ValueType;
  switch (Entry->ValueType) {
    case RedfishValueTypeBoolean:
      Value->Value.Boolean = (BOOLEAN)(Raw != 0);
      break;
    case RedfishValueTypeInteger:
      Value->Value.Integer = (INT64)Raw;
      break;
    default:
      //
      // TODO: string/array attribute types are not modeled by this scaffold.
      //
      return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

/**
  SetValue: write the value of the mapped Setup Question and persist L"Setup".
**/
EFI_STATUS
EFIAPI
NucSetValue (
  IN     EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL  *This,
  IN     CHAR8                                   *Schema,
  IN     CHAR8                                   *Version,
  IN     EFI_STRING                              ConfigureLang,
  IN     EDKII_REDFISH_VALUE                     *Value
  )
{
  AMI_SETUP_MAP_ENTRY  *Entry;
  EFI_STATUS           Status;
  UINT64               Raw;

  if (Value == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Entry = FindEntryByConfigureLang (Schema, ConfigureLang);
  if (Entry == NULL) {
    return EFI_NOT_FOUND;
  }

  switch (Value->Type) {
    case RedfishValueTypeBoolean:
      Raw = Value->Value.Boolean ? 1 : 0;
      break;
    case RedfishValueTypeInteger:
      Raw = (UINT64)Value->Value.Integer;
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  Status = CacheSetupVariable ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return WriteSetupValue (Entry, Raw);
}

/**
  GetDefaultValue: synthesize the default from the map table.
**/
EFI_STATUS
EFIAPI
NucGetDefaultValue (
  IN     EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL  *This,
  IN     CHAR8                                   *Schema,
  IN     CHAR8                                   *Version,
  IN     EFI_STRING                              ConfigureLang,
  IN     UINT16                                  DefaultClass,
  OUT    EDKII_REDFISH_VALUE                     *Value
  )
{
  AMI_SETUP_MAP_ENTRY  *Entry;

  if (Value == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Value, sizeof (*Value));
  Value->Type = RedfishValueTypeUnknown;

  Entry = FindEntryByConfigureLang (Schema, ConfigureLang);
  if (Entry == NULL) {
    return EFI_NOT_FOUND;
  }

  Value->Type = Entry->ValueType;
  switch (Entry->ValueType) {
    case RedfishValueTypeBoolean:
      Value->Value.Boolean = (BOOLEAN)(Entry->DefaultValue != 0);
      break;
    case RedfishValueTypeInteger:
      Value->Value.Integer = (INT64)Entry->DefaultValue;
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

/**
  GetAttribute: describe the mapped Setup Question as a Redfish attribute.

  Strings returned point into the static map table; the caller does not free
  them in this scaffold. A production implementation should return allocated
  copies per the protocol contract.
**/
EFI_STATUS
EFIAPI
NucGetAttribute (
  IN     EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL  *This,
  IN     CHAR8                                   *Schema,
  IN     CHAR8                                   *Version,
  IN     EFI_STRING                              ConfigureLang,
  OUT    EDKII_REDFISH_ATTRIBUTE                 *AttributeValue
  )
{
  AMI_SETUP_MAP_ENTRY  *Entry;

  if (AttributeValue == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (AttributeValue, sizeof (*AttributeValue));

  Entry = FindEntryByConfigureLang (Schema, ConfigureLang);
  if (Entry == NULL) {
    return EFI_NOT_FOUND;
  }

  AttributeValue->AttributeName = AllocateCopyPool (AsciiStrSize (Entry->AttributeName), Entry->AttributeName);
  AttributeValue->DisplayName   = AllocateCopyPool (AsciiStrSize (Entry->DisplayName), Entry->DisplayName);
  AttributeValue->HelpText      = AllocateCopyPool (AsciiStrSize (Entry->HelpText), Entry->HelpText);
  AttributeValue->MenuPath      = AllocateCopyPool (AsciiStrSize (Entry->MenuPath), Entry->MenuPath);
  AttributeValue->Type          = Entry->AttributeType;
  AttributeValue->ResetRequired = Entry->ResetRequired;
  AttributeValue->ReadOnly      = FALSE;
  AttributeValue->GrayedOut     = FALSE;
  AttributeValue->Suppress      = FALSE;

  //
  // TODO: populate NumMaximum/NumMinimum/NumStep and Values (enumeration list)
  //       from the real Setup Question metadata.
  //
  return EFI_SUCCESS;
}

/**
  GetConfigureLang: return the list of ConfigureLangs for a Schema. RegexPattern
  is ignored in this scaffold (all entries for the schema are returned).
**/
EFI_STATUS
EFIAPI
NucGetConfigureLang (
  IN     EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL  *This,
  IN     CHAR8                                   *Schema,
  IN     CHAR8                                   *Version,
  IN     EFI_STRING                              RegexPattern,
  OUT    EFI_STRING                              **ConfigureLangList,
  OUT    UINTN                                   *Count
  )
{
  UINTN       Index;
  UINTN       Matched;
  EFI_STRING  *List;

  if ((ConfigureLangList == NULL) || (Count == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *ConfigureLangList = NULL;
  *Count             = 0;

  //
  // Count matches first.
  //
  Matched = 0;
  for (Index = 0; Index < AMI_SETUP_MAP_COUNT; Index++) {
    if ((Schema == NULL) || (AsciiStrCmp (Schema, mAmiSetupMap[Index].Schema) == 0)) {
      Matched++;
    }
  }

  if (Matched == 0) {
    return EFI_NOT_FOUND;
  }

  List = AllocateZeroPool (Matched * sizeof (EFI_STRING));
  if (List == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Matched = 0;
  for (Index = 0; Index < AMI_SETUP_MAP_COUNT; Index++) {
    if ((Schema == NULL) || (AsciiStrCmp (Schema, mAmiSetupMap[Index].Schema) == 0)) {
      List[Matched] = AllocateCopyPool (
                        StrSize ((CHAR16 *)mAmiSetupMap[Index].ConfigureLang),
                        mAmiSetupMap[Index].ConfigureLang
                        );
      Matched++;
    }
  }

  *ConfigureLangList = List;
  *Count             = Matched;
  return EFI_SUCCESS;
}

/**
  GetSupportedSchema: return the ';'-separated x-UEFI-redfish schema list.
**/
EFI_STATUS
EFIAPI
NucGetSupportedSchema (
  IN     EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL  *This,
  OUT    CHAR8                                   **SupportedSchema
  )
{
  if (SupportedSchema == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *SupportedSchema = AllocateCopyPool (
                       AsciiStrSize (NUC_REDFISH_SUPPORTED_SCHEMA),
                       NUC_REDFISH_SUPPORTED_SCHEMA
                       );
  if (*SupportedSchema == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

STATIC EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL  mNucRedfishPlatformConfig = {
  REDFISH_PLATFORM_CONFIG_VERSION,
  NucGetValue,
  NucSetValue,
  NucGetDefaultValue,
  NucGetAttribute,
  NucGetConfigureLang,
  NucGetSupportedSchema
};

/**
  Driver entry point. Installs EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL.

  @param[in]  ImageHandle  Image handle.
  @param[in]  SystemTable  System table.

  @retval EFI_SUCCESS  Protocol installed.
  @retval Others       Installation failed.
**/
EFI_STATUS
EFIAPI
NucRedfishConfigDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Handle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gEdkIIRedfishPlatformConfigProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mNucRedfishPlatformConfig
                  );
  DEBUG ((
    DEBUG_INFO,
    "%a: install EdkIIRedfishPlatformConfig protocol - %r (%u map rows)\n",
    __func__,
    Status,
    (UINT32)AMI_SETUP_MAP_COUNT
    ));

  return Status;
}
