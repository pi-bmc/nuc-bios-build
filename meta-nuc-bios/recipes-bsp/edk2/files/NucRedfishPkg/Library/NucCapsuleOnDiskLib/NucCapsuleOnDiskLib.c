/** @file

  Boot-time scanner for staged UEFI FMP capsules: the firmware half of
  capsule-on-disk, for a platform whose capsules apply synchronously.

  Linked NULL into BdsDxe. At ReadyToBoot -- once per boot, on the first
  boot attempt -- this walks the \EFI\UpdateCapsule drop box (UEFI 2.10
  8.5.5) of every attached FAT volume and applies whatever it finds
  through gRT->UpdateCapsule(). Before scanning it connects USB
  mass-storage interfaces, so the LUN the BMC's gadget exposes is among
  the volumes searched even on a boot that never chose it. An applied
  capsule is deleted (how the BMC tells applied from pending), a failed
  one is left in place with LastAttemptStatus to say why, and any
  success ends in a cold reset into the freshly written firmware.

  Upstream's Capsule-on-Disk machinery (PcdCapsuleOnDiskSupport,
  CoDRelocateCapsule, CapsuleOnDiskLoadPei) is deliberately not used: it
  parks capsules across a reset for PEI to coalesce, and this payload has
  no PEI phase -- coreboot does memory init and hands off straight to
  DxeCore -- and no persist-across-reset support. A flagless capsule
  applied under boot services needs no relocation -- the scan IS the
  processing. PcdCapsuleOnDiskSupport must stay FALSE so BdsEntry's
  relocate-and-reset path never runs; the OsIndicationsSupported bit it
  would have advertised is OR'd back in here instead, after BdsDxe has
  recomputed the variable, so an OS-side deliverer (fwupd's
  capsule-on-disk mode) can discover that a file dropped in
  \EFI\UpdateCapsule will be picked up.

  Why ReadyToBoot, and why the work stays inside the TPL_CALLBACK
  notification: NucRedfishSyncDxe executes BMC boot overrides -- stage
  BootNext, cold reset -- from network callbacks at TPL_CALLBACK, and
  only latches them off at ReadyToBoot. Same-TPL callbacks cannot
  preempt this notification, so no BMC exchange can cold-reset the
  machine while the in-place firmware rewrite is in flight.
  ConnectController from a TPL_CALLBACK notification follows UsbBusDxe's
  own hot-plug enumeration precedent.

  What this deliberately does not do: connect-all. A pending BMC-side
  capsule is reachable through the targeted USB mass-storage connect,
  and a capsule dropped on the boot ESP sits on a volume BDS already
  connected. ConnectAll is avoided so that the common case -- a boot
  with nothing staged -- stays fast; ConnectAll would also revisit every
  USB class driver on the bus, including the BMC gadget's CDC-EEM
  interface, which this scan leaves exactly as the Redfish stack left
  it.

  How an apply is proven, since there is no firmware file to re-read:
  this platform's firmware lives in SPI, and FmpDeviceSmmLib returns
  EFI_UNSUPPORTED from the FMP read-back entry points, so GetImage() is
  never an option here. UpdateCapsule() success only means "processed" --
  DxeCapsuleLibFmp's ProcessFmpCapsuleImage() returns success for any
  capsule it hands to SetImage() and only records the outcome -- so the
  proof has to come from asking the FMP itself: locate the instance whose
  ImageTypeId matches this platform's single firmware image, and read
  back LastAttemptStatus. Only LAST_ATTEMPT_STATUS_SUCCESS counts as
  applied; anything else, including "could not find or read the FMP at
  all", leaves the capsule staged rather than risk reporting an apply
  that never happened.

  Copyright (c) 2026, the pi-bmc contributors.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <Guid/FileInfo.h>
#include <Guid/GlobalVariable.h>
#include <Guid/SystemResourceTable.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FileHandleLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/FirmwareManagement.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/UsbIo.h>

#include <IndustryStandard/Usb.h>

#include "NucCapsuleParse.h"

#define CAPSULE_DIR_NAME  L"\\EFI\\UpdateCapsule"

//
// Bound on one capsule file. Matches the BMC's HttpPushUri cap.
//
#define MAX_CAPSULE_BYTES  SIZE_128MB

//
// Bound on drop-box entries processed per volume in one boot. The BMC
// stages one; the bound only guards the name array.
//
#define MAX_CAPSULES  8

//
// How long a summary stays readable on a console -- before the reset
// that boots the new firmware, or before a failed boot carries on.
//
#define SUMMARY_STALL_US  (5 * 1000 * 1000)

STATIC BOOLEAN  mNucCodScanDone = FALSE;

//
// ImageTypeId of the platform's single FMP instance. Must track the
// literal GUID Task 3 wired into the DSC's CAPSULE_MAIN_FW_GUID (which
// becomes FmpDxe's FILE_GUID/ImageTypeId) and coreboot's own
// CONFIG_DRIVERS_EFI_MAIN_FW_GUID -- all three name the same ROM.
//
STATIC CONST EFI_GUID  mNucCapsuleFmpImageTypeId = {
  0xd25f89e1, 0x94ec, 0x4533, { 0x80, 0xb9, 0x7f, 0x88, 0x55, 0xce, 0x0a, 0x94 }
};

/**
  Advertise EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED.

  BdsDxe recomputes OsIndicationsSupported at BdsEntry from
  PcdCapsuleOnDiskSupport, which this platform keeps FALSE (see the file
  header); ReadyToBoot is after that write, so OR'ing the bit in here
  sticks for the OS session.
**/
STATIC
VOID
AdvertiseFileCapsuleDelivery (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT64      Supported;
  UINTN       Size;

  Supported = 0;
  Size      = sizeof (Supported);
  Status    = gRT->GetVariable (
                     EFI_OS_INDICATIONS_SUPPORT_VARIABLE_NAME,
                     &gEfiGlobalVariableGuid,
                     NULL,
                     &Size,
                     &Supported
                     );
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return;
  }

  if ((Supported & EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED) != 0) {
    return;
  }

  Supported |= EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED;
  gRT->SetVariable (
         EFI_OS_INDICATIONS_SUPPORT_VARIABLE_NAME,
         &gEfiGlobalVariableGuid,
         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
         sizeof (Supported),
         &Supported
         );
}

/**
  Consume EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED from
  OsIndications, if an OS set it. The scan runs regardless -- the BMC
  cannot set UEFI variables -- but a set bit left in place would ask
  every subsequent boot to process a delivery that already happened.
**/
STATIC
VOID
ClearFileCapsuleIndication (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT64      OsIndications;
  UINTN       Size;

  Size   = sizeof (OsIndications);
  Status = gRT->GetVariable (
                  EFI_OS_INDICATIONS_VARIABLE_NAME,
                  &gEfiGlobalVariableGuid,
                  NULL,
                  &Size,
                  &OsIndications
                  );
  if (EFI_ERROR (Status) ||
      ((OsIndications & EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED) == 0))
  {
    return;
  }

  OsIndications &= ~EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED;
  gRT->SetVariable (
         EFI_OS_INDICATIONS_VARIABLE_NAME,
         &gEfiGlobalVariableGuid,
         EFI_VARIABLE_NON_VOLATILE |
         EFI_VARIABLE_BOOTSERVICE_ACCESS |
         EFI_VARIABLE_RUNTIME_ACCESS,
         sizeof (OsIndications),
         &OsIndications
         );
}

/**
  Connect USB mass-storage interfaces to their filesystems, and nothing
  else on the bus.

  Two passes. The host-controller pass binds UsbBusDxe wherever a USB
  host controller driver already runs (non-recursive, so enumeration
  creates interface child handles without connecting them); the
  interface pass then recursively connects only interfaces whose class
  is mass storage, producing BlockIo -> partition -> FAT. The BMC
  gadget's CDC-EEM interface stays exactly as the Redfish stack left it.
**/
STATIC
VOID
ConnectUsbMassStorage (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    *Handles;
  UINTN                         Count;
  UINTN                         Index;
  EFI_USB_IO_PROTOCOL           *UsbIo;
  EFI_USB_INTERFACE_DESCRIPTOR  Interface;

  Handles = NULL;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiUsb2HcProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < Count; Index++) {
      gBS->ConnectController (Handles[Index], NULL, NULL, FALSE);
    }

    FreePool (Handles);
  }

  Handles = NULL;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiUsbIoProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiUsbIoProtocolGuid,
                    (VOID **)&UsbIo
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &Interface);
    if (EFI_ERROR (Status) || (Interface.InterfaceClass != USB_MASS_STORE_CLASS)) {
      continue;
    }

    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }

  FreePool (Handles);
}

/**
  Open a volume's root directory.

  @param[in]  FsHandle  Handle carrying SimpleFileSystem.
  @param[out] Root      The opened root on success.

  @retval TRUE   Root is open.
  @retval FALSE  It is not.
**/
STATIC
BOOLEAN
OpenVolumeRoot (
  IN  EFI_HANDLE       FsHandle,
  OUT EFI_FILE_HANDLE  *Root
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;

  Status = gBS->HandleProtocol (
                  FsHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Fs
                  );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Status = Fs->OpenVolume (Fs, Root);
  return !EFI_ERROR (Status);
}

/**
  Does this volume's drop box hold at least one non-empty capsule
  candidate?

  @param[in] Root  Open root directory of the volume.

  @retval TRUE   It does.
  @retval FALSE  No drop box, or nothing in it.
**/
STATIC
BOOLEAN
DropBoxHasCapsules (
  IN EFI_FILE_HANDLE  Root
  )
{
  EFI_STATUS       Status;
  EFI_FILE_HANDLE  Dir;
  EFI_FILE_INFO    *Info;
  BOOLEAN          NoFile;
  BOOLEAN          Found;

  Status = Root->Open (Root, &Dir, CAPSULE_DIR_NAME, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Found = FALSE;
  for (Status = FileHandleFindFirstFile (Dir, &Info), NoFile = FALSE;
       !EFI_ERROR (Status) && !NoFile && (Info != NULL) && !Found;
       Status = FileHandleFindNextFile (Dir, Info, &NoFile))
  {
    if (((Info->Attribute & EFI_FILE_DIRECTORY) == 0) &&
        (Info->FileSize > 0) &&
        NucCapsuleIsCandidateFile (Info->FileName))
    {
      Found = TRUE;
    }
  }

  FileHandleClose (Dir);
  return Found;
}

/**
  Decode a LastAttemptStatus into the UEFI spec's name for it.

  @param[in] LastAttemptStatus  The value FmpDxe recorded.

  @return A static string; never NULL.
**/
STATIC
CONST CHAR16 *
LastAttemptStatusName (
  IN UINT32  LastAttemptStatus
  )
{
  switch (LastAttemptStatus) {
    case LAST_ATTEMPT_STATUS_SUCCESS: return L"success";
    case LAST_ATTEMPT_STATUS_ERROR_UNSUCCESSFUL: return L"unsuccessful";
    case LAST_ATTEMPT_STATUS_ERROR_INSUFFICIENT_RESOURCES: return L"insufficient resources";
    case LAST_ATTEMPT_STATUS_ERROR_INCORRECT_VERSION: return L"incorrect version";
    case LAST_ATTEMPT_STATUS_ERROR_INVALID_FORMAT: return L"invalid image format";
    case LAST_ATTEMPT_STATUS_ERROR_AUTH_ERROR: return L"authentication error (capsule not signed with the certificate this firmware trusts)";
    case LAST_ATTEMPT_STATUS_ERROR_PWR_EVT_AC: return L"AC power not connected";
    case LAST_ATTEMPT_STATUS_ERROR_PWR_EVT_BATT: return L"insufficient battery";
    default: return L"vendor/device specific";
  }
}

/**
  Read LastAttemptStatus (and, optionally, LastAttemptVersion and the
  running Version) back from this platform's single FMP instance --
  the one whose ImageTypeId is mNucCapsuleFmpImageTypeId.

  This is the whole proof that gRT->UpdateCapsule() actually wrote the
  SPI flash: FmpDeviceSmmLib's read-back entry points are
  EFI_UNSUPPORTED on this platform, so there is no firmware file to
  re-read, only the FMP's own record of its last SetImage() attempt.

  @param[out] LastStatus   Receives LastAttemptStatus.
  @param[out] LastVersion  Receives LastAttemptVersion; optional.
  @param[out] Version      Receives the running Version; optional.

  @retval TRUE   The FMP was found and its descriptor carries version 3
                 or higher, so LastAttemptStatus is meaningful.
  @retval FALSE  The FMP was not found, or its descriptor predates
                 LastAttemptStatus -- callers must not treat this as
                 proof of anything.
**/
STATIC
BOOLEAN
GetFmpLastAttemptStatus (
  OUT UINT32  *LastStatus,
  OUT UINT32  *LastVersion  OPTIONAL,
  OUT UINT32  *Version      OPTIONAL
  )
{
  EFI_STATUS                        Status;
  EFI_HANDLE                        *Handles;
  UINTN                             HandleCount;
  UINTN                             Index;
  EFI_FIRMWARE_MANAGEMENT_PROTOCOL  *Fmp;
  EFI_FIRMWARE_IMAGE_DESCRIPTOR     *Info;
  UINTN                             InfoSize;
  UINT32                            DescriptorVersion;
  UINT32                            PackageVersion;
  CHAR16                            *PackageVersionName;
  UINT8                             DescriptorCount;
  UINTN                             DescriptorSize;
  BOOLEAN                           Found;

  Found  = FALSE;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiFirmwareManagementProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  for (Index = 0; (Index < HandleCount) && !Found; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiFirmwareManagementProtocolGuid,
                    (VOID **)&Fmp
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    InfoSize = 0;
    if (Fmp->GetImageInfo (Fmp, &InfoSize, NULL, NULL, NULL, NULL, NULL, NULL) != EFI_BUFFER_TOO_SMALL) {
      continue;
    }

    Info = AllocateZeroPool (InfoSize);
    if (Info == NULL) {
      continue;
    }

    PackageVersionName = NULL;
    Status             = Fmp->GetImageInfo (
                                Fmp,
                                &InfoSize,
                                Info,
                                &DescriptorVersion,
                                &DescriptorCount,
                                &DescriptorSize,
                                &PackageVersion,
                                &PackageVersionName
                                );
    if (!EFI_ERROR (Status) &&
        (DescriptorCount > 0) &&
        CompareGuid (&Info->ImageTypeId, &mNucCapsuleFmpImageTypeId) &&
        (DescriptorVersion >= 3))
    {
      *LastStatus = Info->LastAttemptStatus;
      if (LastVersion != NULL) {
        *LastVersion = Info->LastAttemptVersion;
      }

      if (Version != NULL) {
        *Version = Info->Version;
      }

      Found = TRUE;
    }

    if (PackageVersionName != NULL) {
      FreePool (PackageVersionName);
    }

    FreePool (Info);
  }

  FreePool (Handles);
  return Found;
}

/**
  Print the FMP's freshly recorded last-attempt status.
**/
STATIC
VOID
ReportLastAttempt (
  VOID
  )
{
  UINT32  LastStatus;
  UINT32  LastVersion;

  if (GetFmpLastAttemptStatus (&LastStatus, &LastVersion, NULL)) {
    Print (
      L"NucCapsuleOnDisk:   FMP recorded: LastAttemptVersion %u, LastAttemptStatus %u (%s)\n",
      LastVersion,
      LastStatus,
      LastAttemptStatusName (LastStatus)
      );
  } else {
    Print (L"NucCapsuleOnDisk:   could not read back the FMP's LastAttemptStatus\n");
  }
}

/**
  Apply one capsule file from a drop box.

  @param[in] Dir   Open handle on \EFI\UpdateCapsule.
  @param[in] Name  File name within Dir.

  @retval TRUE   The capsule was applied (and the file deleted).
  @retval FALSE  It was not; the file is left in place.
**/
STATIC
BOOLEAN
ApplyOneCapsule (
  IN EFI_FILE_HANDLE  Dir,
  IN CONST CHAR16     *Name
  )
{
  EFI_STATUS          Status;
  EFI_FILE_HANDLE     File;
  BOOLEAN             CanDelete;
  UINT64              FileSize;
  UINTN               ReadSize;
  EFI_CAPSULE_HEADER  *Capsule;
  EFI_CAPSULE_HEADER  *HeaderArray[1];
  UINTN               ImageSize;
  UINT32              CapsuleFlags;
  UINT32              LastStatus;

  //
  // Read-write so a successful apply can delete the file; a physically
  // write-protected volume still gets its capsule applied, it just
  // cannot be marked consumed.
  //
  CanDelete = TRUE;
  Status    = Dir->Open (
                     Dir,
                     &File,
                     (CHAR16 *)Name,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                     0
                     );
  if (EFI_ERROR (Status)) {
    CanDelete = FALSE;
    Status    = Dir->Open (Dir, &File, (CHAR16 *)Name, EFI_FILE_MODE_READ, 0);
  }

  if (EFI_ERROR (Status)) {
    Print (L"NucCapsuleOnDisk: cannot open %s: %r\n", Name, Status);
    return FALSE;
  }

  Status = FileHandleGetSize (File, &FileSize);
  if (EFI_ERROR (Status) || (FileSize == 0) || (FileSize > MAX_CAPSULE_BYTES)) {
    Print (L"NucCapsuleOnDisk: %s has no plausible capsule size (%Lu bytes)\n", Name, FileSize);
    FileHandleClose (File);
    return FALSE;
  }

  Capsule = AllocatePool ((UINTN)FileSize);
  if (Capsule == NULL) {
    Print (L"NucCapsuleOnDisk: out of memory for %s (%Lu bytes)\n", Name, FileSize);
    FileHandleClose (File);
    return FALSE;
  }

  ReadSize = (UINTN)FileSize;
  Status   = FileHandleRead (File, &ReadSize, Capsule);
  if (EFI_ERROR (Status) || (ReadSize != (UINTN)FileSize)) {
    Print (L"NucCapsuleOnDisk: reading %s failed: %r\n", Name, Status);
    FreePool (Capsule);
    FileHandleClose (File);
    return FALSE;
  }

  //
  // Header sanity, FMP-capsule GUID match, and the PersistAcrossReset
  // refusal all live in NucCapsuleParse.c (host-tested); this scanner
  // does not repeat any of it.
  //
  Status = NucCapsuleValidateHeader ((CONST UINT8 *)Capsule, (UINTN)FileSize, &ImageSize, &CapsuleFlags);
  if (EFI_ERROR (Status)) {
    if (Status == EFI_UNSUPPORTED) {
      Print (
        L"NucCapsuleOnDisk: %s is not a supported flagless FMP capsule -- unrecognised capsule GUID, or it requests PersistAcrossReset, which this platform does not support\n",
        Name
        );
    } else {
      Print (L"NucCapsuleOnDisk: %s carries an inconsistent capsule header\n", Name);
    }

    FreePool (Capsule);
    FileHandleClose (File);
    return FALSE;
  }

  Print (
    L"NucCapsuleOnDisk: applying %s (%u bytes, flags 0x%x, %g)...\n",
    Name,
    (UINT32)ImageSize,
    CapsuleFlags,
    &Capsule->CapsuleGuid
    );

  //
  // Flagless capsule: applied inside the call, under boot services --
  // authentication, version gates, progress display and the SPI write
  // all happen before this returns.
  //
  HeaderArray[0] = Capsule;
  Status         = gRT->UpdateCapsule (HeaderArray, 1, 0);
  FreePool (Capsule);

  if (EFI_ERROR (Status)) {
    Print (L"NucCapsuleOnDisk: %s NOT applied: %r\n", Name, Status);
    ReportLastAttempt ();
    FileHandleClose (File);
    return FALSE;
  }

  //
  // UpdateCapsule() success means "processed", never "applied" --
  // upstream records the SetImage status and returns success either
  // way. See the file header for why the FMP's own LastAttemptStatus,
  // not a re-read of a firmware file, is the only proof this platform
  // can get.
  //
  if (!GetFmpLastAttemptStatus (&LastStatus, NULL, NULL)) {
    Print (L"NucCapsuleOnDisk: %s: could not read back the FMP's LastAttemptStatus; leaving the file staged\n", Name);
    FileHandleClose (File);
    return FALSE;
  }

  Print (
    L"NucCapsuleOnDisk:   FMP LastAttemptStatus: %u (%s)\n",
    LastStatus,
    LastAttemptStatusName (LastStatus)
    );

  if (LastStatus != LAST_ATTEMPT_STATUS_SUCCESS) {
    Print (L"NucCapsuleOnDisk: %s NOT applied (see LastAttemptStatus above)\n", Name);
    FileHandleClose (File);
    return FALSE;
  }

  Print (L"NucCapsuleOnDisk: %s applied, confirmed by the FMP's LastAttemptStatus\n", Name);

  if (CanDelete) {
    //
    // Deleting the consumed capsule is the applied-vs-pending signal
    // the BMC reads back from the volume. FileHandleDelete releases the
    // handle whatever it returns.
    //
    Status = FileHandleDelete (File);
    if (EFI_ERROR (Status)) {
      Print (L"NucCapsuleOnDisk: warning: could not delete %s: %r\n", Name, Status);
    }
  } else {
    Print (L"NucCapsuleOnDisk: warning: %s is on read-only media, left in place\n", Name);
    FileHandleClose (File);
  }

  return TRUE;
}

/**
  Apply every capsule in one volume's drop box.

  @param[in]     Root     Open root directory of the volume.
  @param[in,out] Applied  Incremented per capsule applied.
  @param[in,out] Failed   Incremented per capsule left in place.
**/
STATIC
VOID
ProcessDropBox (
  IN     EFI_FILE_HANDLE  Root,
  IN OUT UINTN            *Applied,
  IN OUT UINTN            *Failed
  )
{
  EFI_STATUS       Status;
  EFI_FILE_HANDLE  Dir;
  EFI_FILE_INFO    *Info;
  BOOLEAN          NoFile;
  CHAR16           *Names[MAX_CAPSULES];
  UINTN            NameCount;
  UINTN            Index;

  Status = Root->Open (Root, &Dir, CAPSULE_DIR_NAME, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return;
  }

  //
  // Collect the names first, apply second: deleting entries out of a
  // directory that is being iterated is exactly the kind of FAT
  // behavior not worth depending on.
  //
  NameCount = 0;
  for (Status = FileHandleFindFirstFile (Dir, &Info), NoFile = FALSE;
       !EFI_ERROR (Status) && !NoFile && (Info != NULL);
       Status = FileHandleFindNextFile (Dir, Info, &NoFile))
  {
    if (((Info->Attribute & EFI_FILE_DIRECTORY) == 0) &&
        (Info->FileSize > 0) &&
        NucCapsuleIsCandidateFile (Info->FileName))
    {
      if (NameCount < MAX_CAPSULES) {
        Names[NameCount] = AllocateCopyPool (StrSize (Info->FileName), Info->FileName);
        if (Names[NameCount] != NULL) {
          NameCount++;
        }
      } else {
        Print (L"NucCapsuleOnDisk: more than %d entries, ignoring %s this boot\n", MAX_CAPSULES, Info->FileName);
      }
    }
  }

  for (Index = 0; Index < NameCount; Index++) {
    if (ApplyOneCapsule (Dir, Names[Index])) {
      (*Applied)++;
    } else {
      (*Failed)++;
    }

    FreePool (Names[Index]);
  }

  FileHandleClose (Dir);
}

/**
  ReadyToBoot: scan every attached volume's drop box and apply what is
  staged. Runs once per boot; later boot attempts in the same BDS pass
  find the latch set.

  @param[in] Event    The ReadyToBoot event.
  @param[in] Context  Unused.
**/
STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS       Status;
  EFI_HANDLE       *Handles;
  UINTN            Count;
  UINTN            Index;
  EFI_FILE_HANDLE  Root;
  BOOLEAN          AnyStaged;
  UINTN            Applied;
  UINTN            Failed;

  if (mNucCodScanDone) {
    return;
  }

  mNucCodScanDone = TRUE;

  AdvertiseFileCapsuleDelivery ();
  ConnectUsbMassStorage ();

  Handles = NULL;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimpleFileSystemProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (EFI_ERROR (Status)) {
    ClearFileCapsuleIndication ();
    return;
  }

  //
  // First pass: is anything staged anywhere? The common boot must stay
  // silent and near-free.
  //
  AnyStaged = FALSE;
  for (Index = 0; Index < Count && !AnyStaged; Index++) {
    if (OpenVolumeRoot (Handles[Index], &Root)) {
      AnyStaged = DropBoxHasCapsules (Root);
      Root->Close (Root);
    }
  }

  if (!AnyStaged) {
    ClearFileCapsuleIndication ();
    FreePool (Handles);
    return;
  }

  Print (L"\nNucCapsuleOnDisk: staged firmware capsule(s) found\n");

  Applied = 0;
  Failed  = 0;
  for (Index = 0; Index < Count; Index++) {
    if (OpenVolumeRoot (Handles[Index], &Root)) {
      ProcessDropBox (Root, &Applied, &Failed);
      Root->Close (Root);
    }
  }

  FreePool (Handles);
  ClearFileCapsuleIndication ();

  Print (
    L"NucCapsuleOnDisk: %d of %d capsule(s) applied\n",
    (UINT32)Applied,
    (UINT32)(Applied + Failed)
    );

  if (Applied > 0) {
    //
    // The firmware in SPI has been rewritten; only a fresh cold boot
    // runs it. The stall keeps the summary readable.
    //
    Print (L"NucCapsuleOnDisk: resetting to boot the new firmware...\n");
    gBS->Stall (SUMMARY_STALL_US);
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }

  if (Failed > 0) {
    //
    // Nothing applied: give a console reader a moment, then let BDS
    // carry on. The capsule stays pending and the next firmware
    // inventory report carries the LastAttemptStatus that says why.
    //
    gBS->Stall (SUMMARY_STALL_US);
  }
}

/**
  Register the ReadyToBoot scan. Never fails BdsDxe's load: a firmware
  that cannot scan for capsules must still boot.

  @param[in] ImageHandle  BdsDxe's image handle.
  @param[in] SystemTable  The system table.

  @retval EFI_SUCCESS  Always.
**/
EFI_STATUS
EFIAPI
NucCapsuleOnDiskLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "NucCapsuleOnDisk: ReadyToBoot registration - %r\n", Status));
  }

  return EFI_SUCCESS;
}
