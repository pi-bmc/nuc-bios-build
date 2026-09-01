/** @file
  Pure capsule-header and drop-box filename logic, dependency-free so it can
  be unit tested on a build host.

  Copyright (c) 2026, the pi-bmc contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#pragma once

#include <Uefi.h>

/**
  Is this the EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID?
**/
BOOLEAN
NucCapsuleIsFmpCapsule (
  IN CONST EFI_GUID  *Guid
  );

/**
  Validate a capsule header read from a file.

  @retval EFI_SUCCESS            Flagless FMP capsule; ImageSize and Flags set.
  @retval EFI_UNSUPPORTED        Carries CAPSULE_FLAGS_PERSIST_ACROSS_RESET.
                                 This platform applies synchronously and has
                                 no PEI phase to coalesce for.
  @retval EFI_BAD_BUFFER_SIZE    Header truncated, or the declared sizes do
                                 not fit the buffer.
  @retval EFI_INVALID_PARAMETER  NULL argument.
**/
EFI_STATUS
NucCapsuleValidateHeader (
  IN  CONST UINT8  *Buf,
  IN  UINTN        Len,
  OUT UINTN        *ImageSize,
  OUT UINT32       *Flags
  );

/**
  Should this directory entry be considered a capsule? Matches a
  case-insensitive ".cap" extension and rejects "." and "..".
**/
BOOLEAN
NucCapsuleIsCandidateFile (
  IN CONST CHAR16  *Name
  );
