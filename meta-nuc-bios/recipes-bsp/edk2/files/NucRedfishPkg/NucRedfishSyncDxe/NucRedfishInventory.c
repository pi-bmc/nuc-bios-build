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
