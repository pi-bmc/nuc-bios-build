/** @file
  Pure capsule-header and drop-box filename logic.

  Copyright (c) 2026, the pi-bmc contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "NucCapsuleParse.h"

//
// EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID, UEFI 2.10 23.3.
//
STATIC CONST EFI_GUID  mFmpCapsuleGuid = {
  0x6dcbd5ed, 0xe82d, 0x4c44, { 0xbd, 0xa1, 0x71, 0x94, 0x19, 0x9a, 0xd9, 0x2a }
};

//
// Field offsets within EFI_CAPSULE_HEADER. Spelled out rather than using the
// struct so this file needs no EDK2 headers and can be built on a host.
//
#define CAPSULE_OFF_GUID         0
#define CAPSULE_OFF_HEADER_SIZE  16
#define CAPSULE_OFF_FLAGS        20
#define CAPSULE_OFF_IMAGE_SIZE   24
#define CAPSULE_MIN_HEADER       28

//
// FMP_PAYLOAD_HEADER, as BaseTools' GenerateCapsule.py --fw-version emits it
// and FmpDevicePkg's FmpPayloadHeaderLib reads it back:
//
//   UINT32 Signature; UINT32 HeaderSize; UINT32 FwVersion; UINT32 Lsv;
//
// Signature is SIGNATURE_32 ('M','S','S','1'). Its offset inside the capsule
// depends on the size of the authentication blob in front of it, so it is
// searched for rather than computed -- within a bound, so a stray four bytes
// deep inside an 8 MiB firmware image can never be mistaken for it.
//
#define FMP_PAYLOAD_SIGNATURE     0x3153534DU
#define FMP_PAYLOAD_MIN_HEADER    16
#define FMP_PAYLOAD_MAX_HEADER    4096
#define FMP_PAYLOAD_SEARCH_LIMIT  (64 * 1024)

STATIC
UINT32
ReadLe32 (
  IN CONST UINT8  *P
  )
{
  return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

BOOLEAN
NucCapsuleIsFmpCapsule (
  IN CONST EFI_GUID  *Guid
  )
{
  UINTN  Index;

  if (Guid == NULL) {
    return FALSE;
  }

  if ((Guid->Data1 != mFmpCapsuleGuid.Data1) ||
      (Guid->Data2 != mFmpCapsuleGuid.Data2) ||
      (Guid->Data3 != mFmpCapsuleGuid.Data3))
  {
    return FALSE;
  }

  for (Index = 0; Index < 8; Index++) {
    if (Guid->Data4[Index] != mFmpCapsuleGuid.Data4[Index]) {
      return FALSE;
    }
  }

  return TRUE;
}

EFI_STATUS
NucCapsuleValidateHeader (
  IN  CONST UINT8  *Buf,
  IN  UINTN        Len,
  OUT UINTN        *ImageSize,
  OUT UINT32       *Flags
  )
{
  UINT32    HeaderSize;
  UINT32    CapsuleImageSize;
  UINT32    CapsuleFlags;
  EFI_GUID  Guid;
  UINTN     Index;

  if ((Buf == NULL) || (ImageSize == NULL) || (Flags == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Len < CAPSULE_MIN_HEADER) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Guid.Data1 = ReadLe32 (Buf + CAPSULE_OFF_GUID);
  Guid.Data2 = (UINT16)(Buf[4] | (Buf[5] << 8));
  Guid.Data3 = (UINT16)(Buf[6] | (Buf[7] << 8));
  for (Index = 0; Index < 8; Index++) {
    Guid.Data4[Index] = Buf[8 + Index];
  }

  if (!NucCapsuleIsFmpCapsule (&Guid)) {
    return EFI_UNSUPPORTED;
  }

  HeaderSize       = ReadLe32 (Buf + CAPSULE_OFF_HEADER_SIZE);
  CapsuleFlags     = ReadLe32 (Buf + CAPSULE_OFF_FLAGS);
  CapsuleImageSize = ReadLe32 (Buf + CAPSULE_OFF_IMAGE_SIZE);

  //
  // Refuse a capsule that wants to survive a reset. This platform applies
  // synchronously under boot services; there is no PEI phase to coalesce a
  // persisted capsule, and coreboot's CapsuleUpdateData* path is deliberately
  // not enabled. Applying it anyway would appear to work and then do nothing.
  //
  if ((CapsuleFlags & CAPSULE_FLAGS_PERSIST_ACROSS_RESET) != 0) {
    return EFI_UNSUPPORTED;
  }

  if ((HeaderSize < CAPSULE_MIN_HEADER) || (HeaderSize > Len)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if ((CapsuleImageSize < HeaderSize) || (CapsuleImageSize > Len)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *ImageSize = (UINTN)CapsuleImageSize;
  *Flags     = CapsuleFlags;

  return EFI_SUCCESS;
}

BOOLEAN
NucCapsuleGetPayloadVersion (
  IN  CONST UINT8  *Buf,
  IN  UINTN        Len,
  OUT UINT32       *FwVersion
  )
{
  UINTN   Offset;
  UINTN   Limit;
  UINT32  HeaderSize;

  if ((Buf == NULL) || (FwVersion == NULL) || (Len < FMP_PAYLOAD_MIN_HEADER)) {
    return FALSE;
  }

  Limit = (Len < FMP_PAYLOAD_SEARCH_LIMIT) ? Len : FMP_PAYLOAD_SEARCH_LIMIT;

  for (Offset = 0; Offset + FMP_PAYLOAD_MIN_HEADER <= Limit; Offset++) {
    if (ReadLe32 (Buf + Offset) != FMP_PAYLOAD_SIGNATURE) {
      continue;
    }

    HeaderSize = ReadLe32 (Buf + Offset + 4);
    if ((HeaderSize < FMP_PAYLOAD_MIN_HEADER) ||
        (HeaderSize >= FMP_PAYLOAD_MAX_HEADER) ||
        (Offset + (UINTN)HeaderSize >= Len))
    {
      continue;
    }

    *FwVersion = ReadLe32 (Buf + Offset + 8);
    return TRUE;
  }

  return FALSE;
}

BOOLEAN
NucCapsuleIsCandidateFile (
  IN CONST CHAR16  *Name
  )
{
  UINTN   Len;
  CHAR16  C;
  UINTN   Index;
  CONST CHAR16  *Ext = u".cap";

  if (Name == NULL) {
    return FALSE;
  }

  for (Len = 0; Name[Len] != 0; Len++) {
  }

  //
  // "." and ".." are directory entries, never capsules.
  //
  if ((Len == 1) && (Name[0] == u'.')) {
    return FALSE;
  }

  if ((Len == 2) && (Name[0] == u'.') && (Name[1] == u'.')) {
    return FALSE;
  }

  //
  // Require a real ".cap" suffix, not merely the letters: a file called
  // "cap" is not a capsule.
  //
  if (Len < 4) {
    return FALSE;
  }

  for (Index = 0; Index < 4; Index++) {
    C = Name[Len - 4 + Index];
    if ((C >= u'A') && (C <= u'Z')) {
      C = (CHAR16)(C + (u'a' - u'A'));
    }

    if (C != Ext[Index]) {
      return FALSE;
    }
  }

  return TRUE;
}
