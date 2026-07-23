/** @file
  One-shot UEFI application that recursively connects every controller, forcing
  the just-loaded USB-Ethernet -> NetworkPkg -> RedfishPkg driver stack to bind.

  Why this exists: the NUC's stock (locked) BIOS runs its BDS connect-all BEFORE
  our Redfish drivers are loaded via Driver####. UEFI driver-model drivers
  (RedfishRestExDxe, RedfishDiscoverDxe, RedfishConfigHandlerDriver, and the
  whole SNP->MNP->IP4->TCP->HTTP chain) only execute their Start() when something
  calls ConnectController on the relevant handles. So after the drivers are
  registered we run this app once (as a BootNext boot option, or from the UEFI
  shell) to drive that binding. It is an APPLICATION, not a driver.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>

/**
  Recursively ConnectController() every handle in the system. Children produced
  by a pass become connectable in the next, so we iterate until the connected
  count stops growing (bounded), which lets a deep stack (USB NIC -> SNP -> MNP
  -> IP4 -> TCP -> HTTP -> RestEx -> Redfish) come up in one invocation.
**/
STATIC
UINTN
ConnectAllRecursive (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;
  UINTN       Connected;

  Handles     = NULL;
  HandleCount = 0;
  Connected   = 0;

  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    //
    // Recursive = TRUE: also connect the child controllers this produces.
    // Ignore per-handle errors (many handles have no drivers to bind).
    //
    Status = gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    if (!EFI_ERROR (Status)) {
      Connected++;
    }
  }

  FreePool (Handles);
  return Connected;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN  Pass;
  UINTN  Connected;
  UINTN  Total;

  Total = 0;

  //
  // Several passes: each pass connects handles that appeared as children of the
  // previous pass. Three passes is ample for the Redfish stack depth; it settles
  // early once nothing new binds.
  //
  for (Pass = 0; Pass < 3; Pass++) {
    Connected = ConnectAllRecursive ();
    Total    += Connected;
    Print (L"ConnectRedfish: pass %u connected %u controller(s)\n", (UINT32)(Pass + 1), (UINT32)Connected);
    if (Connected == 0) {
      break;
    }
  }

  Print (L"ConnectRedfish: done (%u successful ConnectController calls).\n", (UINT32)Total);
  return EFI_SUCCESS;
}
