#include <stdio.h>
#include <string.h>
#include "NucCapsuleParse.h"

static int Failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); Failures++; } } while (0)

// EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID
static const EFI_GUID FmpGuid =
  { 0x6dcbd5ed, 0xe82d, 0x4c44, { 0xbd, 0xa1, 0x71, 0x94, 0x19, 0x9a, 0xd9, 0x2a } };
static const EFI_GUID OtherGuid =
  { 0x11111111, 0x2222, 0x3333, { 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77 } };

// A minimal EFI_CAPSULE_HEADER laid out by hand: GUID, HeaderSize,
// Flags, CapsuleImageSize.
static void BuildHeader (UINT8 *Buf, const EFI_GUID *Guid, UINT32 Flags, UINT32 ImageSize)
{
  memset (Buf, 0, 32);
  memcpy (Buf, Guid, sizeof (EFI_GUID));
  UINT32 HeaderSize = 32;
  memcpy (Buf + 16, &HeaderSize, 4);
  memcpy (Buf + 20, &Flags, 4);
  memcpy (Buf + 24, &ImageSize, 4);
}

static void TestRejectsPersistAcrossReset (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0;
  BuildHeader (Buf, &FmpGuid, CAPSULE_FLAGS_PERSIST_ACROSS_RESET, 64);
  // This platform applies synchronously and has no PEI phase to coalesce
  // for. A capsule asking to persist must be refused, not silently applied.
  CHECK (NucCapsuleValidateHeader (Buf, sizeof (Buf), &ImageSize, &Flags) == EFI_UNSUPPORTED,
         "PERSIST_ACROSS_RESET must be rejected");
}

static void TestAcceptsFlaglessFmpCapsule (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0xFF;
  BuildHeader (Buf, &FmpGuid, 0, 64);
  CHECK (NucCapsuleValidateHeader (Buf, sizeof (Buf), &ImageSize, &Flags) == EFI_SUCCESS,
         "a flagless FMP capsule is accepted");
  CHECK (ImageSize == 64, "CapsuleImageSize is returned");
  CHECK (Flags == 0, "Flags are returned");
}

static void TestRejectsTruncatedHeader (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0;
  BuildHeader (Buf, &FmpGuid, 0, 64);
  CHECK (NucCapsuleValidateHeader (Buf, 20, &ImageSize, &Flags) == EFI_BAD_BUFFER_SIZE,
         "a buffer shorter than the header is refused");
}

static void TestRejectsImageSizeBeyondFile (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0;
  BuildHeader (Buf, &FmpGuid, 0, 4096);   // claims more than the file holds
  CHECK (NucCapsuleValidateHeader (Buf, sizeof (Buf), &ImageSize, &Flags) == EFI_BAD_BUFFER_SIZE,
         "CapsuleImageSize past the end of the file is refused");
}

static void TestRejectsHeaderSizePastImageSize (void)
{
  UINT8 Buf[64]; UINTN ImageSize = 0; UINT32 Flags = 0;
  BuildHeader (Buf, &FmpGuid, 0, 16);   // ImageSize < HeaderSize (32)
  CHECK (NucCapsuleValidateHeader (Buf, sizeof (Buf), &ImageSize, &Flags) == EFI_BAD_BUFFER_SIZE,
         "CapsuleImageSize smaller than HeaderSize is refused");
}

static void TestIdentifiesFmpGuid (void)
{
  CHECK (NucCapsuleIsFmpCapsule (&FmpGuid) == TRUE, "FMP GUID recognised");
  CHECK (NucCapsuleIsFmpCapsule (&OtherGuid) == FALSE, "unrelated GUID rejected");
}

static void TestFileFiltering (void)
{
  CHECK (NucCapsuleIsCandidateFile (u"fw.cap") == TRUE,  ".cap accepted");
  CHECK (NucCapsuleIsCandidateFile (u"FW.CAP") == TRUE,  "extension match is case-insensitive");
  CHECK (NucCapsuleIsCandidateFile (u"fw.txt") == FALSE, ".txt ignored");
  CHECK (NucCapsuleIsCandidateFile (u".")      == FALSE, "dot entry ignored");
  CHECK (NucCapsuleIsCandidateFile (u"..")     == FALSE, "dotdot entry ignored");
  CHECK (NucCapsuleIsCandidateFile (u"cap")    == FALSE, "a bare name is not an extension match");
}

int main (void)
{
  TestRejectsPersistAcrossReset ();
  TestAcceptsFlaglessFmpCapsule ();
  TestRejectsTruncatedHeader ();
  TestRejectsImageSizeBeyondFile ();
  TestRejectsHeaderSizePastImageSize ();
  TestIdentifiesFmpGuid ();
  TestFileFiltering ();
  if (Failures) { printf ("%d check(s) failed\n", Failures); return 1; }
  printf ("all NUC capsule parse checks passed\n");
  return 0;
}
