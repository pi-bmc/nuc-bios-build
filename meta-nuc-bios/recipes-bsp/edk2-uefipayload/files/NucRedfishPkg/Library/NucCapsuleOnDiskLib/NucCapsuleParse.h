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
  Read the firmware version out of the FMP payload header a signed capsule
  carries ('MSS1', FMP_PAYLOAD_HEADER as GenerateCapsule.py --fw-version
  writes it, sitting after the capsule/FMP/auth headers).

  The scanner uses this as a LOOP BREAKER, not as anti-downgrade policy:
  FmpDxe only ever compares an incoming image against the lowest supported
  version, never against the running one, so it will happily rewrite the
  bytes already in flash. A capsule that cannot then be deleted would be
  re-applied and cold-reset on every boot, forever.

  @param[in]  Buf        The capsule, fully read into memory.
  @param[in]  Len        Its size in bytes.
  @param[out] FwVersion  Receives the declared firmware version.

  @retval TRUE   A plausible payload header was found; FwVersion is set.
  @retval FALSE  None was found -- the caller must not compare versions.
**/
BOOLEAN
NucCapsuleGetPayloadVersion (
  IN  CONST UINT8  *Buf,
  IN  UINTN        Len,
  OUT UINT32       *FwVersion
  );

/**
  Should this directory entry be considered a capsule? Matches a
  case-insensitive ".cap" extension and rejects "." and "..".
**/
BOOLEAN
NucCapsuleIsCandidateFile (
  IN CONST CHAR16  *Name
  );
