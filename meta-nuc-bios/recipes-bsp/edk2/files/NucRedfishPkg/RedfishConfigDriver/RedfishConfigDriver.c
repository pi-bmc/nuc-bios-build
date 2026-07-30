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
// Static AMI Setup <-> Redfish attribute map.
//
// SOURCE: Decoded from the NUC5i7RYH BIOS (Intel RYBDWi35.86A / "0386"
//   generation) AMI Aptio Setup HII database. The Setup driver FFS module
//   (GUID 12270524-D586-42DE-A1D0-D88007EDAFA9) carries a single HII package
//   list: an EFI_HII_PACKAGE_FORMS block (formset IFR) immediately followed by
//   an EFI_HII_PACKAGE_STRINGS block (en-US, UCS-2). Each offset below is the
//   EFI_IFR_QUESTION_HEADER.VarStoreInfo.VarOffset of a question whose
//   VarStoreId resolves to varstore #0x0001:
//
//     VarStore  : EFI_IFR_VARSTORE_OP, VarStoreId = 0x0001
//     Name      : L"Setup"          (== AMI_SETUP_VARIABLE_NAME)
//     GUID      : EC87D643-EBA4-4BB5-A1E5-3F3E36B20DA9  (== gAmiSetupFormsetGuid)
//     Size      : 566 bytes
//
//   Width comes from the opcode: CHECKBOX = 1 byte boolean; ONE_OF/NUMERIC size
//   from the flags byte low 2 bits (0->1, 1->2, 2->4, 3->8). DefaultValue is the
//   decoded standard default (EFI_IFR_DEFAULT id 0, or the CHECKBOX default bit /
//   ONE_OF default option). ONE_OF questions are exposed as integer indices into
//   their option list (enumeration); the option value->text mapping is noted in
//   each row's comment.
//
//   ResetRequired is TRUE for every row: AMI Setup values are consumed at the
//   next POST, so any change only takes effect after a reboot. (The per-question
//   EFI_IFR_FLAG_RESET_REQUIRED bit was not relied upon.)
//
// WARNING: These byte offsets are SPECIFIC TO THIS FIRMWARE VERSION. AMI relays
//   Setup fields across BIOS releases; re-decode the IFR (do not assume) if the
//   BIOS is updated. A wrong offset writes the wrong BIOS setting.
// -------------------------------------------------------------------------
//
STATIC AMI_SETUP_MAP_ENTRY  mAmiSetupMap[] = {
  {
    // IFR: CHECKBOX  prompt=0x0212  VarOffset=0x005B  width=1  default=0(off)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/AllowUefi3rdPartyDriver",
    "AllowUefi3rdPartyDriver",
    "Allow UEFI 3rd party driver loaded",
    "Allow UEFI 3rd party drivers to be executed / loaded during the BDS stage.",
    "/Bios",
    0x005B,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    0
  },
  {
    // IFR: CHECKBOX  prompt=0x01B0  VarOffset=0x0014  width=1  default=0(off)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/FastBoot",
    "FastBoot",
    "Fast Boot",
    "If enabled, boot from Network/Optical/Removable devices and RAID config is disabled to speed POST.",
    "/Bios",
    0x0014,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    0
  },
  {
    // IFR: CHECKBOX  prompt=0x01C5  VarOffset=0x0017  width=1  default=0(off)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/BootUsbDevicesFirst",
    "BootUsbDevicesFirst",
    "Boot USB Devices First",
    "If enabled, the BIOS attempts to boot supported USB devices before any other device.",
    "/Bios",
    0x0017,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    0
  },
  {
    // IFR: CHECKBOX  prompt=0x0227  VarOffset=0x0018  width=1  default=1(on)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/BootNetworkDevicesLast",
    "BootNetworkDevicesLast",
    "Boot Network Devices Last",
    "If enabled, network devices are always placed after non-network devices in the boot priority.",
    "/Bios",
    0x0018,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    1
  },
  {
    // IFR: CHECKBOX  prompt=0x01C2  VarOffset=0x001C  width=1  default=0(off)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/InternalUefiShell",
    "InternalUefiShell",
    "Internal UEFI Shell",
    "Enables or disables the built-in UEFI Shell as a boot option.",
    "/Bios",
    0x001C,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    0
  },
  {
    // IFR: ONE_OF  prompt=0x01AB  VarOffset=0x001F  width=1  default=1
    //   options: 0=Disable  1=Legacy PXE  3=UEFI PXE & iSCSI
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/NetworkBoot",
    "NetworkBoot",
    "Network Boot",
    "Boot from network: 0=Disable, 1=Legacy PXE, 3=UEFI PXE & iSCSI.",
    "/Bios",
    0x001F,
    1,
    RedfishValueTypeInteger,
    RedfishAttributeTypeEnumeration,
    TRUE,
    1
  },
  {
    // IFR: CHECKBOX  prompt=0x01D5  VarOffset=0x0022  width=1  default=1(on)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/LegacyBoot",
    "LegacyBoot",
    "Legacy Boot",
    "If enabled, the BIOS can boot via the legacy (non-UEFI) boot sequence.",
    "/Bios",
    0x0022,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    1
  },
  {
    // IFR: CHECKBOX  prompt=0x01D2  VarOffset=0x0033  width=1  default=1(on)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/UefiBoot",
    "UefiBoot",
    "UEFI Boot",
    "If enabled, the BIOS attempts to boot via UEFI before the legacy boot sequence; required for >2 TB boot drives.",
    "/Bios",
    0x0033,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    1
  },
  {
    // IFR: CHECKBOX  prompt=0x0214  VarOffset=0x004E  width=1  default=0(off)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/SecureBoot",
    "SecureBoot",
    "Secure Boot",
    "If enabled, the BIOS only boots trusted OS images. Supported only under UEFI Boot.",
    "/Bios",
    0x004E,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    0
  },
  {
    // IFR: ONE_OF  prompt=0x0118  VarOffset=0x01AB  width=1  default=1
    //   options: 0=No Access  1=View Only  2=Limited  3=Full Access
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/UserAccessLevel",
    "UserAccessLevel",
    "User Access Level",
    "Setup access granted to a user password holder: 0=No Access, 1=View Only, 2=Limited, 3=Full Access.",
    "/Bios",
    0x01AB,
    1,
    RedfishValueTypeInteger,
    RedfishAttributeTypeEnumeration,
    TRUE,
    1
  },
  {
    // IFR: ONE_OF  prompt=0x011E  VarOffset=0x01AE  width=1  default=0
    //   options: 0=Always Prompt  1=Lock  2=Temporarily Skip Prompt  3=Never Prompt
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/UnattendedBiosConfiguration",
    "UnattendedBiosConfiguration",
    "Unattended BIOS Configuration",
    "Physical-presence prompt policy for BIOS config via Intel ITK: 0=Always Prompt, 1=Lock, 2=Temporarily Skip Prompt, 3=Never Prompt.",
    "/Bios",
    0x01AE,
    1,
    RedfishValueTypeInteger,
    RedfishAttributeTypeEnumeration,
    TRUE,
    0
  },
  {
    // IFR: CHECKBOX  prompt=0x0124  VarOffset=0x01B0  width=1  default=1(on)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/ExecuteDisableBit",
    "ExecuteDisableBit",
    "Execute Disable Bit",
    "Enables the CPU Execute Disable Bit (NX), helping prevent certain buffer-overflow attacks.",
    "/Bios",
    0x01B0,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    1
  },
  {
    // IFR: CHECKBOX  prompt=0x0126  VarOffset=0x01B1  width=1  default=1(on)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/IntelVirtualizationTechnology",
    "IntelVirtualizationTechnology",
    "Intel Virtualization Technology",
    "Enables hardware support for CPU virtualization (VT-x).",
    "/Bios",
    0x01B1,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    1
  },
  {
    // IFR: CHECKBOX  prompt=0x0128  VarOffset=0x01B3  width=1  default=1(on)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/IntelVtForDirectedIo",
    "IntelVtForDirectedIo",
    "Intel VT for Directed I/O (VT-d)",
    "Enables Intel VT-d hardware support for I/O virtualization; BIOS publishes a DMAR table.",
    "/Bios",
    0x01B3,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    1
  },
  {
    // IFR: CHECKBOX  prompt=0x0356  VarOffset=0x01CB  width=1  default=1(on)
    NUC_REDFISH_BIOS_SCHEMA,
    L"/Bios/Attributes/IntelHyperThreadingTechnology",
    "IntelHyperThreadingTechnology",
    "Intel Hyper-Threading Technology",
    "When disabled, only one thread per active core is available.",
    "/Bios",
    0x01CB,
    1,
    RedfishValueTypeBoolean,
    RedfishAttributeTypeBoolean,
    TRUE,
    1
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
// HII fallback (EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL).
//
// For any (Schema, ConfigureLang) NOT present in mAmiSetupMap[] we fall back to
// the HII database via the Keyword Handler protocol. Each x-UEFI-redfish HII
// question (e.g. those published by HiiToRedfishBootDxe) registers a string in
// the "x-UEFI-redfish-<Schema>.<Version>" language whose text IS the config
// language (starting with '/'). The Keyword Handler treats that config-language
// string as the keyword, so we can Get/Set a question's value without walking
// IFR ourselves.
//
// This is deliberately SIMPLER than RedfishPkg/RedfishPlatformConfigDxe (which
// walks IFR to recover full type/attribute metadata). See the per-member TODOs
// for the semantic edges this scaffold does not cover.
// -------------------------------------------------------------------------
//

STATIC EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL  *mKeywordHandler = NULL;
STATIC EFI_HII_DATABASE_PROTOCOL            *mHiiDatabase    = NULL;

/**
  Locate (and cache) the platform Keyword Handler protocol. Returns NULL if it
  is not present, in which case the caller degrades to AMI-only behaviour.
**/
STATIC
EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL *
LocateKeywordHandler (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mKeywordHandler == NULL) {
    Status = gBS->LocateProtocol (
                    &gEfiConfigKeywordHandlerProtocolGuid,
                    NULL,
                    (VOID **)&mKeywordHandler
                    );
    if (EFI_ERROR (Status)) {
      mKeywordHandler = NULL;
    }
  }

  return mKeywordHandler;
}

/**
  Locate (and cache) the HII Database protocol. Returns NULL if absent.
**/
STATIC
EFI_HII_DATABASE_PROTOCOL *
LocateHiiDatabase (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mHiiDatabase == NULL) {
    Status = gBS->LocateProtocol (
                    &gEfiHiiDatabaseProtocolGuid,
                    NULL,
                    (VOID **)&mHiiDatabase
                    );
    if (EFI_ERROR (Status)) {
      mHiiDatabase = NULL;
    }
  }

  return mHiiDatabase;
}

/**
  Build the "x-UEFI-redfish-<Schema>.<Version>" keyword namespace as a Unicode
  string. Caller frees. Returns NULL on bad input / OOM.
**/
STATIC
EFI_STRING
BuildXUefiNamespace (
  IN CHAR8  *Schema,
  IN CHAR8  *Version
  )
{
  UINTN       Size;
  EFI_STRING  Namespace;

  if ((Schema == NULL) || (Version == NULL)) {
    return NULL;
  }

  //
  // prefix + Schema + '.' + Version + NUL, all as CHAR16.
  //
  Size = (StrLen (NUC_REDFISH_XUEFI_PREFIX) + AsciiStrLen (Schema) + 1 + AsciiStrLen (Version) + 1) * sizeof (CHAR16);

  Namespace = AllocateZeroPool (Size);
  if (Namespace == NULL) {
    return NULL;
  }

  UnicodeSPrint (Namespace, Size, L"%s%a.%a", NUC_REDFISH_XUEFI_PREFIX, Schema, Version);
  return Namespace;
}

STATIC
UINT8
HexCharToNibble (
  IN CHAR16  Char
  )
{
  if ((Char >= L'0') && (Char <= L'9')) {
    return (UINT8)(Char - L'0');
  }

  if ((Char >= L'a') && (Char <= L'f')) {
    return (UINT8)(Char - L'a' + 10);
  }

  if ((Char >= L'A') && (Char <= L'F')) {
    return (UINT8)(Char - L'A' + 10);
  }

  return 0;
}

STATIC
CHAR16
NibbleToHexChar (
  IN UINT8  Nibble
  )
{
  Nibble &= 0xF;
  return (CHAR16)((Nibble < 10) ? (L'0' + Nibble) : (L'A' + (Nibble - 10)));
}

/**
  Decode a UEFI config-string <Value> (hex) into a UINT64.

  Config-string values encode the storage bytes low-address-first, two hex
  digits per byte. So "01" -> 1, "3412" -> 0x1234.

  TODO(scaffold): the multi-byte little-endian assumption is the documented
        config-string memory order but should be re-verified against real HII
        keyword output; single-byte questions (the common boot-form case) are
        unambiguous.
**/
STATIC
UINT64
ConfigHexToUint64 (
  IN CHAR16  *Hex,
  IN UINTN   HexLen
  )
{
  UINT64  Value;
  UINTN   ByteCount;
  UINTN   Index;
  UINT8   Byte;

  Value     = 0;
  ByteCount = HexLen / 2;
  if (ByteCount > sizeof (UINT64)) {
    ByteCount = sizeof (UINT64);
  }

  for (Index = 0; Index < ByteCount; Index++) {
    Byte   = (UINT8)((HexCharToNibble (Hex[Index * 2]) << 4) | HexCharToNibble (Hex[Index * 2 + 1]));
    Value |= ((UINT64)Byte) << (8 * Index);
  }

  return Value;
}

/**
  Encode a UINT64 into a config-string <Value> hex of exactly HexLen digits
  (HexLen must be even), low-address byte first -- the inverse of
  ConfigHexToUint64 so Get/Set round-trip is self-consistent. Out must hold
  HexLen + 1 CHAR16.
**/
STATIC
VOID
Uint64ToConfigHex (
  IN  UINT64   Value,
  IN  UINTN    HexLen,
  OUT CHAR16   *Out
  )
{
  UINTN  ByteCount;
  UINTN  Index;
  UINT8  Byte;

  ByteCount = HexLen / 2;
  if (ByteCount > sizeof (UINT64)) {
    ByteCount = sizeof (UINT64);
  }

  for (Index = 0; Index < ByteCount; Index++) {
    Byte              = (UINT8)((Value >> (8 * Index)) & 0xFF);
    Out[Index * 2]    = NibbleToHexChar ((UINT8)(Byte >> 4));
    Out[Index * 2 + 1] = NibbleToHexChar ((UINT8)(Byte & 0xF));
  }

  Out[ByteCount * 2] = L'\0';
}

/**
  Return the length (in CHAR16) of the hex <Value> that follows "&VALUE=" in a
  KeywordResp string, and a pointer to its first digit. Returns FALSE if the
  string has no "&VALUE=" element.
**/
STATIC
BOOLEAN
FindKeywordRespValue (
  IN  EFI_STRING  KeywordResp,
  OUT EFI_STRING  *ValueStart,
  OUT UINTN       *ValueLen
  )
{
  EFI_STRING  Ptr;
  UINTN       Len;

  Ptr = StrStr (KeywordResp, L"&VALUE=");
  if (Ptr == NULL) {
    return FALSE;
  }

  Ptr += StrLen (L"&VALUE=");
  for (Len = 0; (Ptr[Len] != L'\0') && (Ptr[Len] != L'&'); Len++) {
  }

  *ValueStart = Ptr;
  *ValueLen   = Len;
  return TRUE;
}

/**
  HII fallback for GetValue: read the current value of the question whose
  x-UEFI-redfish config language is ConfigureLang, via the Keyword Handler.

  TODO(scaffold): the Keyword Handler does not report the Redfish value TYPE, so
        every HII value is surfaced as RedfishValueTypeInteger (boolean questions
        read back as 0/1; enumerations as the raw index; strings/arrays are not
        representable here). Full typing would require the IFR walk that
        RedfishPlatformConfigDxe performs.
**/
STATIC
EFI_STATUS
HiiGetValueByKeyword (
  IN     CHAR8                *Schema,
  IN     CHAR8                *Version,
  IN     EFI_STRING           ConfigureLang,
  OUT    EDKII_REDFISH_VALUE  *Value
  )
{
  EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL  *Kh;
  EFI_STRING                           Namespace;
  EFI_STRING                           Request;
  EFI_STRING                           Results;
  EFI_STRING                           Progress;
  EFI_STRING                           ValueStr;
  UINT32                               ProgressErr;
  EFI_STATUS                           Status;
  UINTN                                ReqSize;
  UINTN                                HexLen;

  if ((ConfigureLang == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Kh = LocateKeywordHandler ();
  if (Kh == NULL) {
    return EFI_UNSUPPORTED;
  }

  Namespace = BuildXUefiNamespace (Schema, Version);
  if (Namespace == NULL) {
    return EFI_UNSUPPORTED;
  }

  ReqSize = StrSize (ConfigureLang) + StrSize (L"KEYWORD=");
  Request = AllocateZeroPool (ReqSize);
  if (Request == NULL) {
    FreePool (Namespace);
    return EFI_OUT_OF_RESOURCES;
  }

  UnicodeSPrint (Request, ReqSize, L"KEYWORD=%s", ConfigureLang);

  Results     = NULL;
  Progress    = NULL;
  ProgressErr = 0;
  Status      = Kh->GetData (Kh, Namespace, Request, &Progress, &ProgressErr, &Results);
  if (EFI_ERROR (Status) || (Results == NULL)) {
    DEBUG ((DEBUG_INFO, "%a: HII keyword GetData(%s) - %r (err 0x%x)\n", __func__, ConfigureLang, Status, ProgressErr));
    FreePool (Request);
    FreePool (Namespace);
    if (Results != NULL) {
      FreePool (Results);
    }

    return EFI_NOT_FOUND;
  }

  if (!FindKeywordRespValue (Results, &ValueStr, &HexLen)) {
    FreePool (Results);
    FreePool (Request);
    FreePool (Namespace);
    return EFI_NOT_FOUND;
  }

  ZeroMem (Value, sizeof (*Value));
  Value->Type          = RedfishValueTypeInteger;
  Value->Value.Integer = (INT64)ConfigHexToUint64 (ValueStr, HexLen);

  FreePool (Results);
  FreePool (Request);
  FreePool (Namespace);
  return EFI_SUCCESS;
}

/**
  HII fallback for SetValue: write the question whose x-UEFI-redfish config
  language is ConfigureLang, via the Keyword Handler.

  The target byte width is discovered by first reading back the current value
  (so multi-byte questions are re-encoded at the correct width). Only boolean /
  integer inputs are encoded.

  TODO(scaffold): string / array inputs are not encoded (returns EFI_UNSUPPORTED);
        if the current value cannot be read the width defaults to a single byte.
**/
STATIC
EFI_STATUS
HiiSetValueByKeyword (
  IN     CHAR8                *Schema,
  IN     CHAR8                *Version,
  IN     EFI_STRING           ConfigureLang,
  IN     EDKII_REDFISH_VALUE  *Value
  )
{
  EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL  *Kh;
  EFI_STRING                           Namespace;
  EFI_STRING                           GetReq;
  EFI_STRING                           SetReq;
  EFI_STRING                           Results;
  EFI_STRING                           Progress;
  EFI_STRING                           ValueStr;
  UINT32                               ProgressErr;
  EFI_STATUS                           Status;
  UINTN                                ReqSize;
  UINTN                                HexLen;
  UINT64                               Raw;
  CHAR16                               HexBuf[2 * sizeof (UINT64) + 1];

  if ((ConfigureLang == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  switch (Value->Type) {
    case RedfishValueTypeBoolean:
      Raw = Value->Value.Boolean ? 1 : 0;
      break;
    case RedfishValueTypeInteger:
      Raw = (UINT64)Value->Value.Integer;
      break;
    default:
      //
      // TODO(scaffold): string/array set via keyword handler is not modeled.
      //
      return EFI_UNSUPPORTED;
  }

  Kh = LocateKeywordHandler ();
  if (Kh == NULL) {
    return EFI_UNSUPPORTED;
  }

  Namespace = BuildXUefiNamespace (Schema, Version);
  if (Namespace == NULL) {
    return EFI_UNSUPPORTED;
  }

  //
  // Discover the storage width by reading the current value first.
  //
  HexLen  = 2;   // default: single byte
  ReqSize = StrSize (ConfigureLang) + StrSize (L"KEYWORD=");
  GetReq  = AllocateZeroPool (ReqSize);
  if (GetReq != NULL) {
    UnicodeSPrint (GetReq, ReqSize, L"KEYWORD=%s", ConfigureLang);
    Results     = NULL;
    Progress    = NULL;
    ProgressErr = 0;
    Status      = Kh->GetData (Kh, Namespace, GetReq, &Progress, &ProgressErr, &Results);
    if (!EFI_ERROR (Status) && (Results != NULL)) {
      if (FindKeywordRespValue (Results, &ValueStr, &HexLen)) {
        if ((HexLen == 0) || (HexLen > 2 * sizeof (UINT64)) || ((HexLen & 1) != 0)) {
          HexLen = 2;
        }
      } else {
        HexLen = 2;
      }
    }

    if (Results != NULL) {
      FreePool (Results);
    }

    FreePool (GetReq);
  }

  Uint64ToConfigHex (Raw, HexLen, HexBuf);

  //
  // SetData request: "NAMESPACE=<ns>&KEYWORD=<lang>&VALUE=<hex>"
  //
  ReqSize = StrSize (Namespace) + StrSize (ConfigureLang) + StrSize (HexBuf)
            + StrSize (L"NAMESPACE=&KEYWORD=&VALUE=");
  SetReq = AllocateZeroPool (ReqSize);
  if (SetReq == NULL) {
    FreePool (Namespace);
    return EFI_OUT_OF_RESOURCES;
  }

  UnicodeSPrint (SetReq, ReqSize, L"NAMESPACE=%s&KEYWORD=%s&VALUE=%s", Namespace, ConfigureLang, HexBuf);

  Progress    = NULL;
  ProgressErr = 0;
  Status      = Kh->SetData (Kh, SetReq, &Progress, &ProgressErr);
  DEBUG ((DEBUG_INFO, "%a: HII keyword SetData(%s = %s) - %r (err 0x%x)\n", __func__, ConfigureLang, HexBuf, Status, ProgressErr));

  FreePool (SetReq);
  FreePool (Namespace);
  return Status;
}

/**
  Return the raw <MultiKeywordResp> string listing every keyword (config
  language) the HII database exposes in the x-UEFI-redfish-<Schema>.<Version>
  namespace, or NULL. Caller frees.
**/
STATIC
EFI_STRING
HiiGetNamespaceKeywords (
  IN CHAR8  *Schema,
  IN CHAR8  *Version
  )
{
  EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL  *Kh;
  EFI_STRING                           Namespace;
  EFI_STRING                           Results;
  EFI_STRING                           Progress;
  UINT32                               ProgressErr;
  EFI_STATUS                           Status;

  Kh = LocateKeywordHandler ();
  if (Kh == NULL) {
    return NULL;
  }

  Namespace = BuildXUefiNamespace (Schema, Version);
  if (Namespace == NULL) {
    return NULL;
  }

  //
  // A NULL KeywordString asks the handler to return every known keyword in the
  // given namespace.
  //
  Results     = NULL;
  Progress    = NULL;
  ProgressErr = 0;
  Status      = Kh->GetData (Kh, Namespace, NULL, &Progress, &ProgressErr, &Results);
  FreePool (Namespace);

  if (EFI_ERROR (Status)) {
    if (Results != NULL) {
      FreePool (Results);
    }

    return NULL;
  }

  return Results;
}

/**
  Walk the HII database and collect the distinct x-UEFI-redfish-* string-package
  languages currently registered (each is itself a supported-schema token, e.g.
  "x-UEFI-redfish-ComputerSystem.v1_5_0"). Returns a ';'-separated ASCII string
  (caller frees) or NULL when none are present / on error.

  This is the "schemas the HII fallback can serve" half of GetSupportedSchema.
**/
STATIC
CHAR8 *
CollectHiiSupportedSchemas (
  VOID
  )
{
  EFI_HII_DATABASE_PROTOCOL    *HiiDb;
  EFI_HII_PACKAGE_LIST_HEADER  *PkgList;
  UINT8                        *Buffer;
  UINT8                        *ListPtr;
  UINT8                        *BufferEnd;
  UINTN                        BufferSize;
  EFI_STATUS                   Status;
  CHAR8                        *Csv;
  UINTN                        CsvLen;

  HiiDb = LocateHiiDatabase ();
  if (HiiDb == NULL) {
    return NULL;
  }

  //
  // Size query. The implementation returns the required size in BufferSize.
  //
  BufferSize = 0;
  Status     = HiiDb->ExportPackageLists (HiiDb, NULL, &BufferSize, NULL);
  if ((BufferSize == 0) || (Status == EFI_SUCCESS)) {
    return NULL;
  }

  Buffer = AllocateZeroPool (BufferSize);
  if (Buffer == NULL) {
    return NULL;
  }

  Status = HiiDb->ExportPackageLists (HiiDb, NULL, &BufferSize, (EFI_HII_PACKAGE_LIST_HEADER *)Buffer);
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return NULL;
  }

  Csv       = NULL;
  CsvLen    = 0;
  ListPtr   = Buffer;
  BufferEnd = Buffer + BufferSize;

  //
  // The export is a concatenation of package lists. Each starts with an
  // EFI_HII_PACKAGE_LIST_HEADER whose PackageLength spans the whole list.
  //
  while ((ListPtr + sizeof (EFI_HII_PACKAGE_LIST_HEADER)) <= BufferEnd) {
    UINT8  *PkgPtr;
    UINT8  *ListEnd;
    UINT32  PkgListLen;

    PkgList    = (EFI_HII_PACKAGE_LIST_HEADER *)ListPtr;
    PkgListLen = PkgList->PackageLength;
    if ((PkgListLen < sizeof (EFI_HII_PACKAGE_LIST_HEADER)) || ((ListPtr + PkgListLen) > BufferEnd)) {
      break;
    }

    PkgPtr  = ListPtr + sizeof (EFI_HII_PACKAGE_LIST_HEADER);
    ListEnd = ListPtr + PkgListLen;

    while ((PkgPtr + sizeof (EFI_HII_PACKAGE_HEADER)) <= ListEnd) {
      EFI_HII_PACKAGE_HEADER  *PkgHdr;
      UINT32                  PkgLen;
      UINT32                  PkgType;

      //
      // EFI_HII_PACKAGE_HEADER packs Length:24 and Type:8 into one UINT32.
      //
      PkgHdr  = (EFI_HII_PACKAGE_HEADER *)PkgPtr;
      PkgLen  = PkgHdr->Length;
      PkgType = PkgHdr->Type;

      if (PkgLen == 0) {
        break;
      }

      if (PkgType == EFI_HII_PACKAGE_END) {
        break;
      }

      if (PkgType == EFI_HII_PACKAGE_STRINGS) {
        EFI_HII_STRING_PACKAGE_HDR  *StrHdr;
        CHAR8                       *Language;

        StrHdr   = (EFI_HII_STRING_PACKAGE_HDR *)PkgPtr;
        Language = StrHdr->Language;

        if (AsciiStrnCmp (Language, "x-UEFI-redfish-", AsciiStrLen ("x-UEFI-redfish-")) == 0) {
          //
          // Dedup: skip if this exact language is already present.
          //
          if ((Csv == NULL) || (AsciiStrStr (Csv, Language) == NULL)) {
            UINTN  LangLen;
            UINTN  NewSize;
            CHAR8  *NewCsv;

            LangLen = AsciiStrLen (Language);
            NewSize = CsvLen + LangLen + 2;   // optional ';' + NUL
            NewCsv  = AllocateZeroPool (NewSize);
            if (NewCsv != NULL) {
              if ((Csv != NULL) && (CsvLen > 0)) {
                AsciiStrCpyS (NewCsv, NewSize, Csv);
                AsciiStrCatS (NewCsv, NewSize, ";");
              }

              AsciiStrCatS (NewCsv, NewSize, Language);
              if (Csv != NULL) {
                FreePool (Csv);
              }

              Csv    = NewCsv;
              CsvLen = AsciiStrLen (Csv);
            }
          }
        }
      }

      PkgPtr += PkgLen;
    }

    ListPtr += PkgListLen;
  }

  FreePool (Buffer);
  return Csv;
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
    //
    // Not an AMI L"Setup" attribute -> HII keyword-handler fallback (e.g. the
    // HiiToRedfishBootDxe boot form).
    //
    return HiiGetValueByKeyword (Schema, Version, ConfigureLang, Value);
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
    //
    // Not an AMI L"Setup" attribute -> HII keyword-handler fallback.
    //
    return HiiSetValueByKeyword (Schema, Version, ConfigureLang, Value);
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
    //
    // TODO(scaffold): HII fallback rows have no default modeled here. Recovering
    //       an HII question's default (EFI_IFR_DEFAULT) needs the IFR walk that
    //       this keyword-handler-based scaffold intentionally skips.
    //
    return EFI_UNSUPPORTED;
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
    //
    // TODO(scaffold): HII fallback rows have no attribute metadata modeled here
    //       (DisplayName/HelpText/possible-values live in the IFR + string
    //       packages). Report unsupported rather than fabricating metadata.
    //
    return EFI_UNSUPPORTED;
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
  GetConfigureLang: return the list of ConfigureLangs for a Schema as the UNION
  of the AMI L"Setup" table rows and the HII fallback (keyword-handler) config
  languages registered for "x-UEFI-redfish-<Schema>.<Version>".

  TODO(scaffold): RegexPattern is not applied -- every config language for the
        schema is returned. Applying the pattern would use the platform
        RegularExpressionDxe (gEfiRegularExpressionProtocolGuid), which this
        scaffold does not yet wire in.
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
  UINTN       AmiCount;
  UINTN       HiiCount;
  UINTN       Total;
  EFI_STRING  *List;
  EFI_STRING  Results;
  EFI_STRING  Cursor;
  EFI_STRING  Keyword;
  UINTN       KeyLen;

  if ((ConfigureLangList == NULL) || (Count == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *ConfigureLangList = NULL;
  *Count             = 0;

  //
  // 1. Count AMI L"Setup" table matches.
  //
  AmiCount = 0;
  for (Index = 0; Index < AMI_SETUP_MAP_COUNT; Index++) {
    if ((Schema == NULL) || (AsciiStrCmp (Schema, mAmiSetupMap[Index].Schema) == 0)) {
      AmiCount++;
    }
  }

  //
  // 2. Ask the HII keyword handler for every config language registered in this
  //    schema's x-UEFI-redfish namespace (NULL keyword => "all"). Count them.
  //
  Results  = NULL;
  HiiCount = 0;
  if ((Schema != NULL) && (Version != NULL)) {
    Results = HiiGetNamespaceKeywords (Schema, Version);
    if (Results != NULL) {
      Cursor = Results;
      while ((Cursor = StrStr (Cursor, L"KEYWORD=")) != NULL) {
        HiiCount++;
        Cursor += StrLen (L"KEYWORD=");
      }
    }
  }

  if ((AmiCount + HiiCount) == 0) {
    if (Results != NULL) {
      FreePool (Results);
    }

    return EFI_NOT_FOUND;
  }

  List = AllocateZeroPool ((AmiCount + HiiCount) * sizeof (EFI_STRING));
  if (List == NULL) {
    if (Results != NULL) {
      FreePool (Results);
    }

    return EFI_OUT_OF_RESOURCES;
  }

  //
  // 3. Copy the AMI table config languages.
  //
  Total = 0;
  for (Index = 0; Index < AMI_SETUP_MAP_COUNT; Index++) {
    if ((Schema == NULL) || (AsciiStrCmp (Schema, mAmiSetupMap[Index].Schema) == 0)) {
      List[Total] = AllocateCopyPool (
                      StrSize ((CHAR16 *)mAmiSetupMap[Index].ConfigureLang),
                      mAmiSetupMap[Index].ConfigureLang
                      );
      Total++;
    }
  }

  //
  // 4. Copy the HII-derived config languages (each KEYWORD=<lang> element, up to
  //    the next '&' which begins its VALUE).
  //
  if (Results != NULL) {
    Cursor = Results;
    while ((Cursor = StrStr (Cursor, L"KEYWORD=")) != NULL) {
      Keyword = Cursor + StrLen (L"KEYWORD=");
      for (KeyLen = 0; (Keyword[KeyLen] != L'\0') && (Keyword[KeyLen] != L'&'); KeyLen++) {
      }

      List[Total] = AllocateZeroPool ((KeyLen + 1) * sizeof (CHAR16));
      if (List[Total] != NULL) {
        CopyMem (List[Total], Keyword, KeyLen * sizeof (CHAR16));
        List[Total][KeyLen] = L'\0';
        Total++;
      }

      Cursor = Keyword + KeyLen;
    }

    FreePool (Results);
  }

  *ConfigureLangList = List;
  *Count             = Total;
  return EFI_SUCCESS;
}

/**
  GetSupportedSchema: return the ';'-separated x-UEFI-redfish schema list as the
  UNION of the AMI L"Setup" schema (always served) and the schemas the HII
  fallback can serve (discovered by walking the HII database for x-UEFI-redfish-*
  string packages).
**/
EFI_STATUS
EFIAPI
NucGetSupportedSchema (
  IN     EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL  *This,
  OUT    CHAR8                                   **SupportedSchema
  )
{
  CHAR8  *HiiSchemas;
  UINTN  Size;

  if (SupportedSchema == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Schemas served from HII (may be NULL if none / no HII database).
  //
  HiiSchemas = CollectHiiSupportedSchemas ();

  if (HiiSchemas == NULL) {
    //
    // AMI-only.
    //
    *SupportedSchema = AllocateCopyPool (
                         AsciiStrSize (NUC_REDFISH_SUPPORTED_SCHEMA),
                         NUC_REDFISH_SUPPORTED_SCHEMA
                         );
    if (*SupportedSchema == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    return EFI_SUCCESS;
  }

  //
  // AMI schema ';' HII schemas.
  //
  Size = AsciiStrLen (NUC_REDFISH_SUPPORTED_SCHEMA) + 1 + AsciiStrLen (HiiSchemas) + 1;

  *SupportedSchema = AllocateZeroPool (Size);
  if (*SupportedSchema == NULL) {
    FreePool (HiiSchemas);
    return EFI_OUT_OF_RESOURCES;
  }

  AsciiStrCpyS (*SupportedSchema, Size, NUC_REDFISH_SUPPORTED_SCHEMA);
  AsciiStrCatS (*SupportedSchema, Size, ";");
  AsciiStrCatS (*SupportedSchema, Size, HiiSchemas);

  FreePool (HiiSchemas);
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

  //
  // NOTE: there used to be a ReadyToBoot handler here that ConnectController()'d
  // every handle recursively, four passes, to "drive the Redfish stack into
  // binding".  Both halves of that were wrong:
  //
  //   * It was unnecessary.  The real reason discovery never ran was
  //     PcdIPv6HttpSupport defaulting to TRUE while the payload is built with
  //     NETWORK_IP6_ENABLE=FALSE, which made RedfishDiscoverDxe's all-of
  //     required-protocol test reject every controller (see wire-redfish.py).
  //     With that fixed, RedfishDiscoverDriverBindingStart runs during normal
  //     BDS connect, well before ReadyToBoot.
  //
  //   * It was harmful.  Reconnecting the whole handle database at ReadyToBoot
  //     tears down and re-enumerates the USB CDC-ECM device out from under an
  //     in-flight Redfish acquire (RedfishServiceAbortAcquire + undi.shutdown()
  //     in the log), and the passes never converged -- each reported the same 5
  //     controllers, i.e. it was churning the same stack repeatedly.
  //
  // Verified 2026-07-29: the NUC hangs during boot whenever the ECM link is
  // live, and boots in 25s with the gadget's ECM function unlinked.
  //
  return Status;
}
