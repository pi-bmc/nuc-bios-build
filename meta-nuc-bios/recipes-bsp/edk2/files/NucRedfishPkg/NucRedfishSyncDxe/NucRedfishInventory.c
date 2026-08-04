/** @file
  SMBIOS-sourced host inventory for the Redfish Host Interface client.

  The BMC has no in-band view of the managed host: over the USB link it can see
  a NIC and nothing else. Everything it reports about the system therefore has
  to come from the host itself, and SMBIOS is where this firmware already
  publishes it -- type 0 (BIOS) and type 1 (System) are populated by
  UefiPayloadPkg from the coreboot tables.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "NucRedfishSyncDxe.h"

/**
  Return the Nth string of an SMBIOS structure.

  SMBIOS keeps a structure's strings in a NUL-separated list that starts at the
  end of its formatted area and ends with a double NUL; fields reference them by
  1-based index, with 0 meaning "no string".

  @param[in] Header  SMBIOS structure header.
  @param[in] Index   1-based string number. 0 returns NULL.

  @retval Pointer to the string, or NULL when the index is 0 or out of range.
**/
STATIC
CHAR8 *
SmbiosGetString (
  IN EFI_SMBIOS_TABLE_HEADER  *Header,
  IN SMBIOS_TABLE_STRING      Index
  )
{
  CHAR8  *Walker;
  UINT8  Current;

  if ((Header == NULL) || (Index == 0)) {
    return NULL;
  }

  Walker  = (CHAR8 *)Header + Header->Length;
  Current = 1;

  //
  // A zero-length string at the head of the list is the terminating double NUL,
  // i.e. the structure has fewer strings than requested.
  //
  while (*Walker != '\0') {
    if (Current == Index) {
      return Walker;
    }

    Walker += AsciiStrLen (Walker) + 1;
    Current++;
  }

  return NULL;
}

/**
  Copy an SMBIOS string into a fixed-size field, truncating if necessary and
  always NUL-terminating.

  @param[out] Dest      Destination buffer.
  @param[in]  DestSize  Size of Dest in bytes.
  @param[in]  Source    Source string, may be NULL.
**/
STATIC
VOID
CopyInventoryString (
  OUT CHAR8        *Dest,
  IN  UINTN        DestSize,
  IN  CONST CHAR8  *Source
  )
{
  UINTN  Length;

  if ((Dest == NULL) || (DestSize == 0)) {
    return;
  }

  Dest[0] = '\0';
  if (Source == NULL) {
    return;
  }

  Length = AsciiStrLen (Source);
  if (Length >= DestSize) {
    Length = DestSize - 1;
  }

  CopyMem (Dest, Source, Length);
  Dest[Length] = '\0';
}

/**
  Render an SMBIOS type 1 UUID field as a Redfish-style UUID string.

  SMBIOS 2.6 and later store the first three fields little-endian (the same
  layout as EFI_GUID), so they are emitted in reverse byte order to produce the
  canonical text form.

  @param[in]  Uuid  16-byte SMBIOS UUID field.
  @param[out] Text  Receives 37 bytes (36 characters plus NUL).

  @retval TRUE   A usable UUID was rendered.
  @retval FALSE  The field was all-zero or all-0xFF, i.e. "not present".
**/
STATIC
BOOLEAN
RenderSmbiosUuid (
  IN  UINT8  *Uuid,
  OUT CHAR8  *Text
  )
{
  UINTN  Index;
  UINT8  Or;
  UINT8  And;

  Or  = 0x00;
  And = 0xFF;
  for (Index = 0; Index < 16; Index++) {
    Or  |= Uuid[Index];
    And &= Uuid[Index];
  }

  if ((Or == 0x00) || (And == 0xFF)) {
    return FALSE;
  }

  AsciiSPrint (
    Text,
    37,
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    Uuid[3],  Uuid[2],  Uuid[1],  Uuid[0],
    Uuid[5],  Uuid[4],
    Uuid[7],  Uuid[6],
    Uuid[8],  Uuid[9],
    Uuid[10], Uuid[11], Uuid[12], Uuid[13], Uuid[14], Uuid[15]
    );

  return TRUE;
}

EFI_STATUS
NucRedfishCollectInventory (
  OUT NUC_REDFISH_HOST_INVENTORY  *Inventory
  )
{
  EFI_STATUS               Status;
  EFI_SMBIOS_PROTOCOL      *Smbios;
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  SMBIOS_TABLE_TYPE0       *Type0;
  SMBIOS_TABLE_TYPE1       *Type1;

  if (Inventory == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Inventory, sizeof (*Inventory));

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: SMBIOS protocol not found - %r\n", Status));
    return EFI_NOT_FOUND;
  }

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  while (!EFI_ERROR (Smbios->GetNext (Smbios, &Handle, NULL, &Record, NULL))) {
    switch (Record->Type) {
      case EFI_SMBIOS_TYPE_BIOS_INFORMATION:
        Type0 = (SMBIOS_TABLE_TYPE0 *)Record;
        CopyInventoryString (
          Inventory->BiosVersion,
          sizeof (Inventory->BiosVersion),
          SmbiosGetString (Record, Type0->BiosVersion)
          );
        break;

      case EFI_SMBIOS_TYPE_SYSTEM_INFORMATION:
        Type1 = (SMBIOS_TABLE_TYPE1 *)Record;
        CopyInventoryString (
          Inventory->Manufacturer,
          sizeof (Inventory->Manufacturer),
          SmbiosGetString (Record, Type1->Manufacturer)
          );
        CopyInventoryString (
          Inventory->Model,
          sizeof (Inventory->Model),
          SmbiosGetString (Record, Type1->ProductName)
          );
        CopyInventoryString (
          Inventory->SerialNumber,
          sizeof (Inventory->SerialNumber),
          SmbiosGetString (Record, Type1->SerialNumber)
          );
        Inventory->UuidValid = RenderSmbiosUuid ((UINT8 *)&Type1->Uuid, Inventory->Uuid);
        break;

      default:
        break;
    }
  }

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSync: inventory bios='%a' mfr='%a' model='%a' sn='%a' uuid='%a'\n",
    Inventory->BiosVersion,
    Inventory->Manufacturer,
    Inventory->Model,
    Inventory->SerialNumber,
    Inventory->UuidValid ? Inventory->Uuid : "(none)"
    ));

  return EFI_SUCCESS;
}

/**
  Map an SMBIOS type 17 MemoryType to the Redfish MemoryDeviceType enumeration.

  Returns NULL for values with no Redfish equivalent, which the caller omits
  rather than guessing -- a wrong enum is worse than an absent property.
**/
STATIC
CONST CHAR8 *
RedfishMemoryDeviceType (
  IN UINT8  SmbiosMemoryType
  )
{
  switch (SmbiosMemoryType) {
    case MemoryTypeSdram:    return "SDRAM";
    case MemoryTypeDdr:      return "DDR";
    case MemoryTypeDdr2:     return "DDR2";
    case MemoryTypeDdr3:     return "DDR3";
    case MemoryTypeDdr4:     return "DDR4";
    case MemoryTypeLpddr:    return "LPDDR_SDRAM";
    case MemoryTypeLpddr2:   return "LPDDR2_SDRAM";
    case MemoryTypeLpddr3:   return "LPDDR3_SDRAM";
    case MemoryTypeLpddr4:   return "LPDDR4_SDRAM";
    default:                 return NULL;
  }
}

/**
  Map an SMBIOS type 17 FormFactor to the Redfish BaseModuleType enumeration.
**/
STATIC
CONST CHAR8 *
RedfishBaseModuleType (
  IN UINT8  FormFactor
  )
{
  switch (FormFactor) {
    case MemoryFormFactorSodimm: return "SO_DIMM";
    case MemoryFormFactorDimm:   return "UDIMM";
    case MemoryFormFactorRimm:   return "RDIMM";
    case MemoryFormFactorFbDimm: return "LRDIMM";
    default:                     return NULL;
  }
}

/**
  Decode the type 17 size fields into MiB.

  SMBIOS overloads one 16-bit field: bit 15 selects KiB rather than MiB, 0
  means the socket is empty, 0xFFFF means unknown, and 0x7FFF means "too large
  to express here, see ExtendedSize". Returns 0 for empty or unknown, which the
  caller treats as "do not report this socket".
**/
STATIC
UINT32
RedfishMemoryCapacityMiB (
  IN SMBIOS_TABLE_TYPE17  *Type17
  )
{
  if ((Type17->Size == 0) || (Type17->Size == 0xFFFF)) {
    return 0;
  }

  if (Type17->Size == 0x7FFF) {
    //
    // ExtendedSize arrived in SMBIOS 2.7; only read it if the record is long
    // enough to contain it.
    //
    if (Type17->Hdr.Length < OFFSET_OF (SMBIOS_TABLE_TYPE17, ExtendedSize) + sizeof (Type17->ExtendedSize)) {
      return 0;
    }

    return Type17->ExtendedSize & 0x7FFFFFFF;
  }

  if ((Type17->Size & BIT15) != 0) {
    //
    // Value is in KiB. Round down; a sub-MiB DIMM is not a thing this needs to
    // represent precisely.
    //
    return (UINT32)(Type17->Size & 0x7FFF) / 1024;
  }

  return Type17->Size;
}

EFI_STATUS
NucRedfishCollectMemory (
  OUT NUC_REDFISH_MEMORY_MODULE  *Modules,
  IN  UINTN                      Max,
  OUT UINTN                      *Count
  )
{
  EFI_STATUS                 Status;
  EFI_SMBIOS_PROTOCOL        *Smbios;
  EFI_SMBIOS_HANDLE          Handle;
  EFI_SMBIOS_TABLE_HEADER    *Record;
  SMBIOS_TABLE_TYPE17        *Type17;
  NUC_REDFISH_MEMORY_MODULE  *Module;
  UINT32                     Capacity;

  if ((Modules == NULL) || (Count == NULL) || (Max == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  *Count = 0;
  ZeroMem (Modules, Max * sizeof (*Modules));

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  while (!EFI_ERROR (Smbios->GetNext (Smbios, &Handle, NULL, &Record, NULL))) {
    if (Record->Type != EFI_SMBIOS_TYPE_MEMORY_DEVICE) {
      continue;
    }

    if (*Count >= Max) {
      DEBUG ((DEBUG_ERROR, "NucRedfishSync: more than %d memory devices, truncating\n", Max));
      break;
    }

    Type17 = (SMBIOS_TABLE_TYPE17 *)Record;

    //
    // SMBIOS emits a record per socket whether or not it is populated.
    // Reporting an empty one would claim hardware that is not there.
    //
    Capacity = RedfishMemoryCapacityMiB (Type17);
    if (Capacity == 0) {
      continue;
    }

    Module              = &Modules[*Count];
    Module->CapacityMiB = Capacity;

    CopyInventoryString (
      Module->DeviceLocator,
      sizeof (Module->DeviceLocator),
      SmbiosGetString (Record, Type17->DeviceLocator)
      );
    CopyInventoryString (
      Module->BankLocator,
      sizeof (Module->BankLocator),
      SmbiosGetString (Record, Type17->BankLocator)
      );
    CopyInventoryString (
      Module->Manufacturer,
      sizeof (Module->Manufacturer),
      SmbiosGetString (Record, Type17->Manufacturer)
      );
    CopyInventoryString (
      Module->SerialNumber,
      sizeof (Module->SerialNumber),
      SmbiosGetString (Record, Type17->SerialNumber)
      );
    CopyInventoryString (
      Module->PartNumber,
      sizeof (Module->PartNumber),
      SmbiosGetString (Record, Type17->PartNumber)
      );

    Module->MemoryDeviceType = RedfishMemoryDeviceType (Type17->MemoryType);
    Module->BaseModuleType   = RedfishBaseModuleType (Type17->FormFactor);
    Module->DataWidthBits    = Type17->DataWidth;
    Module->BusWidthBits     = Type17->TotalWidth;
    Module->RatedSpeedMhz    = Type17->Speed;

    //
    // The configured speed is what the module is actually running at, and is
    // what Redfish calls OperatingSpeedMhz. It arrived in SMBIOS 2.7; fall back
    // to the rated speed on an older record.
    //
    if (Type17->Hdr.Length >= OFFSET_OF (SMBIOS_TABLE_TYPE17, ConfiguredMemoryClockSpeed) +
        sizeof (Type17->ConfiguredMemoryClockSpeed))
    {
      Module->OperatingSpeedMhz = Type17->ConfiguredMemoryClockSpeed;
    }

    if (Module->OperatingSpeedMhz == 0) {
      Module->OperatingSpeedMhz = Type17->Speed;
    }

    DEBUG ((
      DEBUG_ERROR,
      "NucRedfishSync: memory[%d] '%a' %d MiB %a %d MHz mfr='%a' pn='%a'\n",
      *Count,
      Module->DeviceLocator,
      Module->CapacityMiB,
      Module->MemoryDeviceType != NULL ? Module->MemoryDeviceType : "(type?)",
      Module->OperatingSpeedMhz,
      Module->Manufacturer,
      Module->PartNumber
      ));

    (*Count)++;
  }

  return EFI_SUCCESS;
}

/**
  Append a "Name": "Value" member when Value is non-empty.

  Values come from SMBIOS strings, which may legitimately contain a double quote
  or backslash; those are escaped rather than dropped so the body stays valid
  JSON. Control characters are replaced with spaces for the same reason.

  @param[in,out] Json      Buffer being built.
  @param[in]     JsonSize  Size of Json.
  @param[in]     Name      Member name.
  @param[in]     Value     Member value; skipped when NULL or empty.
**/
STATIC
VOID
AppendJsonString (
  IN OUT CHAR8        *Json,
  IN     UINTN        JsonSize,
  IN     CONST CHAR8  *Name,
  IN     CONST CHAR8  *Value
  )
{
  CHAR8  Escaped[NUC_REDFISH_STR_MAX * 2];
  UINTN  In;
  UINTN  Out;

  if ((Value == NULL) || (Value[0] == '\0')) {
    return;
  }

  for (In = 0, Out = 0; (Value[In] != '\0') && (Out < sizeof (Escaped) - 2); In++) {
    if ((Value[In] == '"') || (Value[In] == '\\')) {
      Escaped[Out++] = '\\';
      Escaped[Out++] = Value[In];
    } else if ((UINT8)Value[In] < 0x20) {
      Escaped[Out++] = ' ';
    } else {
      Escaped[Out++] = Value[In];
    }
  }

  Escaped[Out] = '\0';

  AsciiStrCatS (Json, JsonSize, ",\"");
  AsciiStrCatS (Json, JsonSize, Name);
  AsciiStrCatS (Json, JsonSize, "\":\"");
  AsciiStrCatS (Json, JsonSize, Escaped);
  AsciiStrCatS (Json, JsonSize, "\"");
}

EFI_STATUS
NucRedfishBuildSystemPatch (
  IN  NUC_REDFISH_HOST_INVENTORY  *Inventory,
  OUT CHAR8                       **Json
  )
{
  CHAR8  *Body;

  if ((Inventory == NULL) || (Json == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Body = AllocateZeroPool (NUC_REDFISH_JSON_MAX);
  if (Body == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // BootProgress leads so the object is never empty: every other member is
  // conditional on SMBIOS actually having supplied it. Subsequent members are
  // emitted with a leading comma by AppendJsonString.
  //
  AsciiSPrint (
    Body,
    NUC_REDFISH_JSON_MAX,
    "{\"BootProgress\":{\"LastState\":\"%a\"}",
    NUC_REDFISH_BOOT_PROGRESS
    );

  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "BiosVersion", Inventory->BiosVersion);
  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "Manufacturer", Inventory->Manufacturer);
  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "Model", Inventory->Model);
  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "SerialNumber", Inventory->SerialNumber);
  if (Inventory->UuidValid) {
    AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "UUID", Inventory->Uuid);
  }

  AsciiStrCatS (Body, NUC_REDFISH_JSON_MAX, "}");

  *Json = Body;
  return EFI_SUCCESS;
}

/**
  Append a "Name": <number> member when Value is non-zero.

  Zero is treated as "SMBIOS did not say" throughout type 17 -- an unknown speed
  or width is encoded as 0 -- so omitting it is more honest than reporting a
  DIMM that runs at 0 MHz.
**/
STATIC
VOID
AppendJsonNumber (
  IN OUT CHAR8        *Json,
  IN     UINTN        JsonSize,
  IN     CONST CHAR8  *Name,
  IN     UINT32       Value
  )
{
  CHAR8  Buffer[32];

  if (Value == 0) {
    return;
  }

  AsciiSPrint (Buffer, sizeof (Buffer), ",\"%a\":%d", Name, Value);
  AsciiStrCatS (Json, JsonSize, Buffer);
}

EFI_STATUS
NucRedfishBuildMemoryPost (
  IN  NUC_REDFISH_MEMORY_MODULE  *Module,
  OUT CHAR8                      **Json
  )
{
  CHAR8  *Body;

  if ((Module == NULL) || (Json == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Body = AllocateZeroPool (NUC_REDFISH_JSON_MAX);
  if (Body == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // @odata.type leads so the object is never empty and the BMC can tell which
  // schema version this is; everything after it is conditional and emitted with
  // a leading comma.
  //
  // DeviceLocator doubles as the member's identity: the BMC uses it as the
  // resource Id, so re-reporting the same socket on a later boot updates that
  // member rather than creating a second one.
  //
  AsciiSPrint (
    Body,
    NUC_REDFISH_JSON_MAX,
    "{\"@odata.type\":\"#Memory.v1_7_1.Memory\""
    ",\"Status\":{\"State\":\"Enabled\",\"Health\":\"OK\"}"
    ",\"MemoryType\":\"DRAM\""
    );

  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "DeviceLocator", Module->DeviceLocator);
  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "Name", Module->DeviceLocator);
  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "Manufacturer", Module->Manufacturer);
  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "SerialNumber", Module->SerialNumber);
  AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "PartNumber", Module->PartNumber);

  if (Module->MemoryDeviceType != NULL) {
    AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "MemoryDeviceType", Module->MemoryDeviceType);
  }

  if (Module->BaseModuleType != NULL) {
    AppendJsonString (Body, NUC_REDFISH_JSON_MAX, "BaseModuleType", Module->BaseModuleType);
  }

  AppendJsonNumber (Body, NUC_REDFISH_JSON_MAX, "CapacityMiB", Module->CapacityMiB);
  AppendJsonNumber (Body, NUC_REDFISH_JSON_MAX, "OperatingSpeedMhz", Module->OperatingSpeedMhz);
  AppendJsonNumber (Body, NUC_REDFISH_JSON_MAX, "DataWidthBits", Module->DataWidthBits);
  AppendJsonNumber (Body, NUC_REDFISH_JSON_MAX, "BusWidthBits", Module->BusWidthBits);

  //
  // AllowedSpeedsMHz is an array of what the module itself supports, as opposed
  // to OperatingSpeedMhz which is what it was configured to.
  //
  if (Module->RatedSpeedMhz != 0) {
    CHAR8  Speeds[48];

    AsciiSPrint (Speeds, sizeof (Speeds), ",\"AllowedSpeedsMHz\":[%d]", Module->RatedSpeedMhz);
    AsciiStrCatS (Body, NUC_REDFISH_JSON_MAX, Speeds);
  }

  AsciiStrCatS (Body, NUC_REDFISH_JSON_MAX, "}");

  *Json = Body;
  return EFI_SUCCESS;
}
