/** @file
  NucRedfishSyncDxe - the host-side client of the Redfish Host Interface.

  RedfishPkg gets the host as far as *discovering* the BMC: RedfishHostInterfaceDxe
  publishes the SMBIOS type 42 record, RedfishDiscoverDxe correlates it with the
  CDC-ECM NIC and configures REST EX, and RedfishConfigHandlerDriver signals that
  a service is available. Nothing then talks to it. The drivers that would --
  edk2-redfish-client's feature layer -- are not buildable against this tree (see
  the note above SRC_URI in edk2-uefipayload_2605.bb), so on a stock RedfishPkg build
  the whole chain completes and BDS boots the OS without a single HTTP request.
  Confirmed on hardware 2026-07-29: cbmem showed "Redfish service ... is
  discovered!" immediately followed by "[Bds]BdsWait", and the BMC logged nothing.

  This driver is that missing consumer. It produces
  EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL, which RedfishConfigHandlerDriver invokes
  with the discovered service, and then performs the exchange that makes the host
  interface actually useful for out-of-band management:

    1. GET  /redfish/v1/            -- proves the link and the service are live.
    2. PATCH /redfish/v1/Systems/1  -- reports this host's identity (SMBIOS type
                                       0/1) and boot progress to the BMC, which
                                       otherwise has no in-band view of it.
    3. GET  /redfish/v1/Systems/1   -- reads back the BMC's requested one-time
                                       boot override, acknowledges it, and boots
                                       the matching option in this same boot.

  Everything here is fail-open: an unreachable or unhappy BMC must never stop the
  host from booting, so every failure is logged and returns EFI_SUCCESS.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "NucRedfishSyncDxe.h"

#include <Library/DevicePathLib.h>
#include <Library/JsonLib.h>
#include <Library/UefiBootManagerLib.h>

STATIC EFI_HANDLE  mImageHandle = NULL;
STATIC BOOLEAN     mSyncDone    = FALSE;

/**
  Log an HTTP status code alongside the URI it came from.

  Redfish traffic is invisible from the OS once BDS hands over, so cbmem is the
  only record of what happened. These lines are DEBUG_ERROR rather than
  DEBUG_MANAGEABILITY deliberately: they are the host-side evidence that the
  exchange took place, and must survive whatever PcdDebugPrintErrorLevel is set
  to.

  @param[in] What      Short label for the operation.
  @param[in] Uri       URI operated on.
  @param[in] Status    EFI status of the call.
  @param[in] Response  Response whose status code should be reported, may be NULL.
**/
STATIC
VOID
LogResult (
  IN CONST CHAR8       *What,
  IN EFI_STRING        Uri,
  IN EFI_STATUS        Status,
  IN REDFISH_RESPONSE  *Response
  )
{
  UINTN  Code;

  Code = 0;
  if ((Response != NULL) && (Response->StatusCode != NULL)) {
    Code = (UINTN)*Response->StatusCode;
  }

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSync: %a %s -> %r (HTTP status enum %d)\n",
    What,
    Uri,
    Status,
    Code
    ));
}

/**
  Report whether a boot option's device path contains a node of the given type.

  @param[in] Option   Boot option to inspect.
  @param[in] Type     Device path type to look for.
  @param[in] SubType  Device path sub-type to look for.

  @retval TRUE   A matching node is present.
  @retval FALSE  No matching node, or the option has no device path.
**/
STATIC
BOOLEAN
OptionHasNode (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *Option,
  IN UINT8                         Type,
  IN UINT8                         SubType
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  if ((Option == NULL) || (Option->FilePath == NULL)) {
    return FALSE;
  }

  for (Node = Option->FilePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == Type) && (DevicePathSubType (Node) == SubType)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Report whether a boot option boots from a real network interface.

  Requires a MAC node, which is what makes it a network option, and rejects
  anything reached through USB. On this board the only USB NIC is the BMC's
  CDC-ECM host interface -- a DSP0270 management link with no DHCP server on it,
  which must never be selected as a boot target. PlatformBootManagerLib prunes
  its auto-created option for the same reason, so this is belt and braces
  against an option that arrived some other way.

  @param[in] Option  Boot option to inspect.

  @retval TRUE   The option boots from a non-USB network interface.
  @retval FALSE  It does not.
**/
STATIC
BOOLEAN
OptionIsNetworkBoot (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *Option
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  BOOLEAN                   HasMac;

  if ((Option == NULL) || (Option->FilePath == NULL)) {
    return FALSE;
  }

  HasMac = FALSE;

  for (Node = Option->FilePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if (DevicePathType (Node) != MESSAGING_DEVICE_PATH) {
      continue;
    }

    if (DevicePathSubType (Node) == MSG_USB_DP) {
      return FALSE;
    }

    if (DevicePathSubType (Node) == MSG_MAC_ADDR_DP) {
      HasMac = TRUE;
    }
  }

  return HasMac;
}

/**
  Report whether a boot option boots from local block storage.

  "Hdd" cannot simply look for an HD() partition node. The boot options visible
  at the point this driver runs are not the ones `efibootmgr` shows from the OS:
  this is during BDS connect, before EfiBootManagerRefreshAllBootOption() and
  before the OS-installed entries are merged in. Dumped on hardware 2026-08-02
  while a Hdd override was staged:

      Boot0001 "NVMe: PM951 NVMe SAMSUNG 256GB " attr=0x1
      Boot0000 "Enter Setup"                     attr=0x109 [FvFile]
      Boot0002 "UEFI Shell"                      attr=0x1   [FvFile]
      Boot0003 "iPXE"                            attr=0x1   [FvFile]

  The OS's own HD()-anchored entry (Boot0004 "debian") is absent, and the disk
  candidate that *is* present is auto-created and points at the NVMe namespace --
  PciRoot()/Pci()/Pci()/NVMe() -- with no partition node, because BDS expands it
  to find the ESP later.

  So match either shape: an HD() node when the option is partition-anchored, or
  a storage messaging node when it names the device. Deliberately no bare
  MSG_USB_DP -- that would also match the BMC's own CDC-ECM NIC; real USB mass
  storage carries HD() once enumerated.

  @param[in] Option  Boot option to inspect.

  @retval TRUE   The option boots from local block storage.
  @retval FALSE  It does not.
**/
STATIC
BOOLEAN
OptionIsDiskBoot (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *Option
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  if ((Option == NULL) || (Option->FilePath == NULL)) {
    return FALSE;
  }

  if (OptionHasNode (Option, MEDIA_DEVICE_PATH, MEDIA_HARDDRIVE_DP)) {
    return TRUE;
  }

  for (Node = Option->FilePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if (DevicePathType (Node) != MESSAGING_DEVICE_PATH) {
      continue;
    }

    switch (DevicePathSubType (Node)) {
      case MSG_NVME_NAMESPACE_DP:
      case MSG_SATA_DP:
      case MSG_ATAPI_DP:
      case MSG_SCSI_DP:
      case MSG_EMMC_DP:
      case MSG_SD_DP:
      case MSG_UFS_DP:
        return TRUE;
      default:
        break;
    }
  }

  return FALSE;
}

/**
  Find the boot option matching a Redfish BootSourceOverrideTarget.

  Redfish names boot *classes* ("Pxe", "Hdd", "BiosSetup"); UEFI has numbered
  Boot#### options. The mapping is done by scanning the boot options this
  firmware already built and matching on the attributes BDS itself uses --
  LOAD_OPTION_CATEGORY_APP for the setup UI, the iPXE FFS GUID for network boot,
  and storage device path nodes for local disks -- rather than on description
  text, which is localised and unstable.

  This only matches; it has no side effects, so the caller can acknowledge the
  request to the BMC before doing anything that might not return. See
  ApplyMatchedOption().

  On success the caller owns *Options and must release it with
  EfiBootManagerFreeLoadOptions().

  @param[in]  Target       Redfish BootSourceOverrideTarget value.
  @param[out] Options      Boot option array the match indexes into.
  @param[out] OptionCount  Number of entries in *Options.
  @param[out] Match        Index of the matching option.

  @retval EFI_SUCCESS    A boot option matched.
  @retval EFI_NOT_FOUND  No boot option matched the requested class.
**/
STATIC
EFI_STATUS
FindBootOverrideOption (
  IN  CONST CHAR8                    *Target,
  OUT EFI_BOOT_MANAGER_LOAD_OPTION   **Options,
  OUT UINTN                          *OptionCount,
  OUT UINTN                          *Match
  )
{
  UINTN    Index;
  BOOLEAN  Found;

  *Options     = NULL;
  *OptionCount = 0;
  *Match       = 0;

  if ((Target == NULL) || (AsciiStrCmp (Target, "None") == 0)) {
    return EFI_NOT_FOUND;
  }

  *Options = EfiBootManagerGetLoadOptions (OptionCount, LoadOptionTypeBoot);
  if ((*Options == NULL) || (*OptionCount == 0)) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: no boot options to override\n"));
    return EFI_NOT_FOUND;
  }

  Found = FALSE;

  //
  // Dump the candidates. An override that does not apply is otherwise
  // indistinguishable from one that was never staged, and the boot option set
  // this early in BDS is not what `efibootmgr` shows from the OS -- it predates
  // EfiBootManagerRefreshAllBootOption(), so auto-created disk entries may not
  // exist yet. Cheap: this only runs when the BMC has actually staged a target.
  //
  for (Index = 0; Index < *OptionCount; Index++) {
    DEBUG ((
      DEBUG_ERROR,
      "NucRedfishSync:   Boot%04x \"%s\" attr=0x%x%a%a\n",
      (*Options)[Index].OptionNumber,
      ((*Options)[Index].Description != NULL) ? (*Options)[Index].Description : L"",
      (*Options)[Index].Attributes,
      OptionHasNode (&(*Options)[Index], MEDIA_DEVICE_PATH, MEDIA_HARDDRIVE_DP) ? " [HD]" : "",
      OptionHasNode (&(*Options)[Index], MEDIA_DEVICE_PATH, MEDIA_PIWG_FW_FILE_DP) ? " [FvFile]" : ""
      ));
  }

  for (Index = 0; Index < *OptionCount; Index++) {
    if (AsciiStrCmp (Target, "BiosSetup") == 0) {
      //
      // The setup UI (UiApp) is the boot option BDS tags as an application.
      //
      Found = (((*Options)[Index].Attributes & LOAD_OPTION_CATEGORY) == LOAD_OPTION_CATEGORY_APP);
    } else if (AsciiStrCmp (Target, "Pxe") == 0) {
      //
      // Network boot is BDS's own "PXEv4 (MAC:...)" option for the onboard NIC,
      // auto-created once ipxe-intel.efidrv publishes SNP for it. Match the
      // device path rather than the description, which is localised.
      //
      // This used to match the iPXE boot application by its FFS GUID. That
      // application is no longer embedded (EDK2_IPXE_APP defaults off): it did
      // not work on this board, and the PXEv4 option is what actually
      // chainloads. See OptionIsNetworkBoot().
      //
      Found = OptionIsNetworkBoot (&(*Options)[Index]);
    } else if (AsciiStrCmp (Target, "Hdd") == 0) {
      //
      // Local block storage, in whichever shape BDS has it at this point.
      // See OptionIsDiskBoot().
      //
      Found = OptionIsDiskBoot (&(*Options)[Index]);
    }

    if (Found) {
      *Match = Index;
      break;
    }
  }

  if (!Found) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: no boot option matches target '%a'\n", Target));
    EfiBootManagerFreeLoadOptions (*Options, *OptionCount);
    *Options     = NULL;
    *OptionCount = 0;
    return EFI_NOT_FOUND;
  }

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSync: boot override '%a' -> Boot%04x \"%s\"\n",
    Target,
    (*Options)[*Match].OptionNumber,
    (*Options)[*Match].Description != NULL ? (*Options)[*Match].Description : L"(no description)"
    ));

  return EFI_SUCCESS;
}

/**
  Act on a matched boot override: stage BootNext and reset.

  Always the two-step, never EfiBootManagerBoot() from here. The RPi5 port of
  this driver shipped the same-boot path first and learned the hard way that
  booting from the config-handler callback use-after-frees in-flight discovery
  state -- the boot tears the network stack down underneath
  RedfishDiscoverDxe. One extra reset is the price of a boot that cannot
  corrupt the stack that requested it; the BMC's override was cleared and
  acknowledged before this point, so the loop is safe (the next boot finds
  nothing staged and consumes BootNext normally).

  BdsEntry caches BootNext before PlatformBootManagerLib runs, specifically so
  a BootNext set during BDS is not consumed in the same boot -- hence the
  reset rather than a plain return.

  @param[in] Option  The matched boot option. Not freed here.

  @retval EFI_SUCCESS  BootNext was staged (the reset does not return).
**/
STATIC
EFI_STATUS
ApplyMatchedOption (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *Option
  )
{
  EFI_STATUS  Status;
  UINT16      BootNext;

  //
  // Always stage BootNext and reset; never EfiBootManagerBoot() from here.
  // The RPi5 port of this driver learned that same-boot booting from the
  // config-handler callback use-after-frees in-flight discovery state (the
  // boot tears down the network stack under RedfishDiscoverDxe), and in
  // practice this callback never runs at TPL_APPLICATION anyway (measured
  // TPL_CALLBACK = 8 on this platform).
  //
  BootNext = Option->OptionNumber;
  Status   = gRT->SetVariable (
                    L"BootNext",
                    &gEfiGlobalVariableGuid,
                    EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                    sizeof (BootNext),
                    &BootNext
                    );

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSync: staged BootNext=Boot%04x - %r\n",
    BootNext,
    Status
    ));

  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Reset so the request is honoured now rather than sitting until whenever the
  // host next reboots. BdsEntry has already cached BootNext for this boot, so
  // without this the host would carry on to its default target and the operator
  // would have to power-cycle it themselves.
  //
  // Safe to loop on: the BMC's override was cleared and acknowledged before this
  // point (HandleBootOverride refuses to get here otherwise), so the next boot
  // finds nothing staged and consumes BootNext normally.
  //
  // EfiResetCold, not Warm, though on this platform it makes no difference:
  // UefiPayloadPkg's ResetSystemLib writes mAcpiBoardInfo.ResetValue for both,
  // and that comes from the FADT -- which mainboard_fill_fadt() sets to
  // FULL_RST|RST_CPU|SYS_RST (0x0e). A soft reset (0x06) leaves the platform
  // partially powered and Broadwell raminit cannot get back through it; that was
  // the warm-reboot hang fixed on 2026-07-30. This path depends on that fix.
  //
  DEBUG ((DEBUG_ERROR, "NucRedfishSync: resetting to consume BootNext\n"));
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

  //
  // Not reached.
  //
  CpuDeadLoop ();
  return EFI_SUCCESS;
}

/**
  Read the BMC's requested boot override from a ComputerSystem payload and apply
  it, then tell the BMC it has been consumed.

  Only "Once" overrides are honoured. A "Continuous" override would re-apply on
  every boot with no way for the host to clear it, which is a boot loop waiting
  to happen on a link whose only operator is the BMC.

  @param[in] Service   Redfish service to acknowledge through.
  @param[in] Response  Response holding the ComputerSystem payload.
**/
STATIC
VOID
HandleBootOverride (
  IN REDFISH_SERVICE   Service,
  IN REDFISH_RESPONSE  *Response
  )
{
  EDKII_JSON_VALUE              Root;
  EDKII_JSON_VALUE              Boot;
  EDKII_JSON_VALUE              Value;
  CONST CHAR8                   *Target;
  CONST CHAR8                   *Enabled;
  EFI_STATUS                    Status;
  REDFISH_RESPONSE              AckResponse;
  EFI_BOOT_MANAGER_LOAD_OPTION  *Options;
  UINTN                         OptionCount;
  UINTN                         Match;

  if ((Response == NULL) || (Response->Payload == NULL)) {
    return;
  }

  Root = RedfishJsonInPayload (Response->Payload);
  if ((Root == NULL) || !JsonValueIsObject (Root)) {
    return;
  }

  Boot = JsonObjectGetValue (JsonValueGetObject (Root), "Boot");
  if ((Boot == NULL) || !JsonValueIsObject (Boot)) {
    return;
  }

  Value  = JsonObjectGetValue (JsonValueGetObject (Boot), "BootSourceOverrideTarget");
  Target = (Value == NULL) ? NULL : JsonValueGetAsciiString (Value);

  Value   = JsonObjectGetValue (JsonValueGetObject (Boot), "BootSourceOverrideEnabled");
  Enabled = (Value == NULL) ? NULL : JsonValueGetAsciiString (Value);

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSync: BMC boot override target='%a' enabled='%a'\n",
    (Target == NULL) ? "(unset)" : Target,
    (Enabled == NULL) ? "(unset)" : Enabled
    ));

  if ((Enabled == NULL) || (AsciiStrCmp (Enabled, "Once") != 0)) {
    return;
  }

  if (EFI_ERROR (FindBootOverrideOption (Target, &Options, &OptionCount, &Match))) {
    return;
  }

  //
  // Acknowledge BEFORE booting, not after. ApplyMatchedOption() boots the target
  // in this boot where it can, and a successful boot never returns -- so an
  // acknowledgement placed after it would never be sent on exactly the runs that
  // worked. The BMC would still show the override staged, the next boot would
  // apply it again, and the host would be pinned to that target forever.
  //
  // Ordering it this way also keeps the override genuinely one-shot if the host
  // resets between here and the boot attempt.
  //
  ZeroMem (&AckResponse, sizeof (AckResponse));
  Status = RedfishHttpPatchResource (
             Service,
             NUC_REDFISH_SYSTEM_URI,
             "{\"Boot\":{\"BootSourceOverrideEnabled\":\"Disabled\"}}",
             &AckResponse
             );
  LogResult ("PATCH(clear-override)", NUC_REDFISH_SYSTEM_URI, Status, &AckResponse);
  RedfishHttpFreeResponse (&AckResponse);

  if (EFI_ERROR (Status)) {
    //
    // Refuse to boot a request we could not acknowledge: the BMC still has it
    // staged, so honouring it now would repeat on every subsequent boot.
    //
    DEBUG ((
      DEBUG_ERROR,
      "NucRedfishSync: not applying override, BMC did not accept the clear - %r\n",
      Status
      ));
    EfiBootManagerFreeLoadOptions (Options, OptionCount);
    return;
  }

  ApplyMatchedOption (&Options[Match]);

  EfiBootManagerFreeLoadOptions (Options, OptionCount);
}

/**
  Report the host's populated DIMMs to the BMC's Memory collection.

  One POST per module. The BMC keys each member on DeviceLocator, so re-running
  this on every boot updates the existing members rather than accumulating
  duplicates -- which matters because the collection is not persisted across a
  BMC restart and has to be able to rebuild itself from whatever boots next.

  Failures are logged and otherwise ignored: memory inventory is useful, but it
  is not worth abandoning the boot-override exchange that follows over.

  @param[in] Service  The Redfish service to report to.
**/
/**
  Report the host's processor sockets to the BMC's Processors collection.

  One POST per populated socket, keyed on the SMBIOS socket designation so a
  later boot updates the existing member rather than accumulating duplicates.
  The POST never carries SpeedLimitMHz/SpeedLocked: those are operator-managed
  properties the BMC preserves across the re-POST (the RPi5 shared-member
  contract), and this platform has no clock knob behind them anyway.
  Fail-open by design, like every other report here.

  @param[in] Service  The Redfish service to report to.
**/
STATIC
VOID
ReportProcessors (
  IN REDFISH_SERVICE  Service
  )
{
  NUC_REDFISH_PROCESSOR  Processors[NUC_REDFISH_PROCESSOR_MAX];
  REDFISH_RESPONSE       Response;
  EFI_STATUS             Status;
  UINTN                  Count;
  UINTN                  Index;
  CHAR8                  *Body;

  Status = NucRedfishCollectProcessors (Processors, NUC_REDFISH_PROCESSOR_MAX, &Count);
  if (EFI_ERROR (Status) || (Count == 0)) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: no processors to report - %r\n", Status));
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Body   = NULL;
    Status = NucRedfishBuildProcessorPost (&Processors[Index], &Body);
    if (EFI_ERROR (Status) || (Body == NULL)) {
      continue;
    }

    ZeroMem (&Response, sizeof (Response));
    Status = RedfishHttpPostResource (Service, NUC_REDFISH_PROCESSORS_URI, Body, &Response);
    LogResult ("POST", NUC_REDFISH_PROCESSORS_URI, Status, &Response);
    RedfishHttpFreeResponse (&Response);

    FreePool (Body);
  }

  DEBUG ((DEBUG_ERROR, "NucRedfishSync: reported %d processor(s)\n", Count));
}

STATIC
VOID
ReportMemory (
  IN REDFISH_SERVICE  Service
  )
{
  NUC_REDFISH_MEMORY_MODULE  Modules[NUC_REDFISH_MEMORY_MAX];
  REDFISH_RESPONSE           Response;
  EFI_STATUS                 Status;
  UINTN                      Count;
  UINTN                      Index;
  CHAR8                      *Body;

  Status = NucRedfishCollectMemory (Modules, NUC_REDFISH_MEMORY_MAX, &Count);
  if (EFI_ERROR (Status) || (Count == 0)) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: no memory devices to report - %r\n", Status));
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Body   = NULL;
    Status = NucRedfishBuildMemoryPost (&Modules[Index], &Body);
    if (EFI_ERROR (Status) || (Body == NULL)) {
      continue;
    }

    ZeroMem (&Response, sizeof (Response));
    Status = RedfishHttpPostResource (Service, NUC_REDFISH_MEMORY_URI, Body, &Response);
    LogResult ("POST", NUC_REDFISH_MEMORY_URI, Status, &Response);
    RedfishHttpFreeResponse (&Response);

    FreePool (Body);
  }

  DEBUG ((DEBUG_ERROR, "NucRedfishSync: reported %d memory device(s)\n", Count));
}

/**
  Report the host's local drives to the BMC's Storage subsystem.

  Same shape as ReportMemory -- one POST per drive, keyed on SerialNumber so a
  later boot updates rather than duplicates -- but sourced from the
  boot-services stack (DiskInfo / NVMe pass-thru) instead of SMBIOS, which has
  no structure type for a disk.

  @param[in] Service  The Redfish service to report to.
**/
STATIC
VOID
ReportDrives (
  IN REDFISH_SERVICE  Service
  )
{
  NUC_REDFISH_DRIVE  Drives[NUC_REDFISH_DRIVE_MAX];
  REDFISH_RESPONSE   Response;
  EFI_STATUS         Status;
  UINTN              Count;
  UINTN              Index;
  CHAR8              *Body;

  Status = NucRedfishCollectDrives (Drives, NUC_REDFISH_DRIVE_MAX, &Count);
  if (EFI_ERROR (Status) || (Count == 0)) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: no drives to report - %r\n", Status));
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Body   = NULL;
    Status = NucRedfishBuildDrivePost (&Drives[Index], &Body);
    if (EFI_ERROR (Status) || (Body == NULL)) {
      continue;
    }

    ZeroMem (&Response, sizeof (Response));
    Status = RedfishHttpPostResource (Service, NUC_REDFISH_DRIVES_URI, Body, &Response);
    LogResult ("POST", NUC_REDFISH_DRIVES_URI, Status, &Response);
    RedfishHttpFreeResponse (&Response);

    FreePool (Body);
  }

  DEBUG ((DEBUG_ERROR, "NucRedfishSync: reported %d drive(s)\n", Count));
}

/**
  Report what firmware this board runs, as a Redfish SoftwareInventory.

  This is duty 4 of the BMC's host-firmware contract: report Version and
  LastAttempt* each boot, so the BMC can tell whether a staged capsule was
  ever applied and how it went. LastAttemptStatus survives to be reported at
  all only because the RMAP manifest (Task 6) keeps SMMSTORE out of every
  capsule write range, so FmpDeviceSmmLib never swaps gRT's variable services
  for no-op stubs and FmpDxe can persist the final status across the reset a
  successful apply causes.

  PATCH rather than POST: the resource is fixed and per-node, so
  re-reporting the same node updates it rather than accumulating duplicates.
  Fail-open like everything else here -- a BMC with no UpdateService just
  404s.

  @param[in] Service  The Redfish service to report to.
**/
STATIC
VOID
ReportFirmwareInventory (
  IN REDFISH_SERVICE  Service
  )
{
  NUC_REDFISH_FIRMWARE_STATUS  FirmwareStatus;
  REDFISH_RESPONSE             Response;
  EFI_STATUS                   Status;
  CHAR8                        *Body;

  Status = NucRedfishCollectFirmwareStatus (&FirmwareStatus);
  if (EFI_ERROR (Status)) {
    //
    // Expected on a build without the capsule FMP wired up: there is no
    // matching Firmware Management Protocol instance to ask, so there is
    // nothing to report.
    //
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: no firmware inventory to report - %r\n", Status));
    return;
  }

  Body   = NULL;
  Status = NucRedfishBuildFirmwareInventoryPatch (&FirmwareStatus, &Body);
  if (EFI_ERROR (Status) || (Body == NULL)) {
    return;
  }

  ZeroMem (&Response, sizeof (Response));
  Status = RedfishHttpPatchResource (Service, NUC_REDFISH_FIRMWARE_INVENTORY_URI, Body, &Response);
  LogResult ("PATCH", NUC_REDFISH_FIRMWARE_INVENTORY_URI, Status, &Response);
  RedfishHttpFreeResponse (&Response);

  FreePool (Body);

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSync: reported firmware '%a' version %a (%u), updateable %a, attempt=%a\n",
    FirmwareStatus.Name,
    FirmwareStatus.Version,
    FirmwareStatus.VersionNumber,
    FirmwareStatus.Updateable ? "yes" : "no",
    FirmwareStatus.HasLastAttempt ? "yes" : "no"
    ));
}

/**
  Perform the host-interface exchange against the discovered Redfish service.

  @param[in] ServiceInfo  Discovered Redfish service information.
**/
STATIC
VOID
NucRedfishSync (
  IN REDFISH_CONFIG_SERVICE_INFORMATION  *ServiceInfo
  )
{
  REDFISH_SERVICE             Service;
  REDFISH_RESPONSE            Response;
  EFI_STATUS                  Status;
  NUC_REDFISH_HOST_INVENTORY  Inventory;
  CHAR8                       *Patch;
  UINTN                       Attempt;

  Service = RedfishCreateService (ServiceInfo);
  if (Service == NULL) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: RedfishCreateService failed\n"));
    return;
  }

  //
  // 1. Service root. Reaching this at all is the proof that the type 42 record,
  //    the ECM link and REST EX all line up.
  //
  // Retried, because the first attempt routinely fails with EFI_NO_MEDIA. The
  // USB-net stack starts CableDetect at 0 and only raises it when it catches a
  // CDC NETWORK_CONNECTION notification; Linux's ECM gadget emits those on link
  // state changes, and the one sent during enumeration is long gone by the time
  // this driver runs. The BMC re-announces its link on a timer for exactly this
  // reason (announceHostInterfaceLink() in its usb.go), so retrying here is what
  // lets the two meet: whichever announcement lands first, the next attempt
  // sees media.
  //
  for (Attempt = 0; Attempt < NUC_REDFISH_MEDIA_RETRIES; Attempt++) {
    ZeroMem (&Response, sizeof (Response));
    Status = RedfishHttpGetResource (Service, NUC_REDFISH_SERVICE_ROOT_URI, NULL, &Response, FALSE);
    if ((Status != EFI_NO_MEDIA) || (Attempt == NUC_REDFISH_MEDIA_RETRIES - 1)) {
      LogResult ("GET", NUC_REDFISH_SERVICE_ROOT_URI, Status, &Response);
      RedfishHttpFreeResponse (&Response);
      break;
    }

    RedfishHttpFreeResponse (&Response);
    DEBUG ((
      DEBUG_ERROR,
      "NucRedfishSync: no media on the host interface, retry %d/%d\n",
      Attempt + 1,
      NUC_REDFISH_MEDIA_RETRIES
      ));
    gBS->Stall (NUC_REDFISH_MEDIA_RETRY_STALL);
  }

  if (EFI_ERROR (Status)) {
    //
    // No point attempting the rest: the service is not answering. Leave the
    // service alone regardless -- see the note at the end of this function.
    //
    return;
  }

  //
  // 2. Report who this host is and how far it has booted.
  //
  Patch = NULL;
  NucRedfishCollectInventory (&Inventory);
  Status = NucRedfishBuildSystemPatch (&Inventory, &Patch);
  if (!EFI_ERROR (Status) && (Patch != NULL)) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: PATCH body %a\n", Patch));

    ZeroMem (&Response, sizeof (Response));
    Status = RedfishHttpPatchResource (Service, NUC_REDFISH_SYSTEM_URI, Patch, &Response);
    LogResult ("PATCH", NUC_REDFISH_SYSTEM_URI, Status, &Response);
    RedfishHttpFreeResponse (&Response);

    FreePool (Patch);
  }

  //
  // 2b. Report the DIMMs.
  //
  ReportMemory (Service);

  //
  // 2b2. Report the processor sockets.
  //
  ReportProcessors (Service);

  //
  // 2c. Report the drives.
  //
  ReportDrives (Service);

  //
  // 2d. Report the firmware inventory (contract duty 4).
  //
  ReportFirmwareInventory (Service);

  //
  // 3. Read back the system, including any boot override the BMC wants applied.
  //
  ZeroMem (&Response, sizeof (Response));
  Status = RedfishHttpGetResource (Service, NUC_REDFISH_SYSTEM_URI, NULL, &Response, FALSE);
  LogResult ("GET", NUC_REDFISH_SYSTEM_URI, Status, &Response);
  if (!EFI_ERROR (Status)) {
    HandleBootOverride (Service, &Response);
  }

  RedfishHttpFreeResponse (&Response);

  //
  // Deliberately no RedfishCleanupService (Service) here.
  //
  // This driver does not own the service. RedfishConfigHandlerDriver creates it
  // once from what RedfishDiscoverDxe found and hands the same instance to every
  // registered config handler in turn. Destroying it when this one finishes
  // tears down the REST EX child underneath everyone who has not run yet.
  //
  // That was harmless while this was the only consumer. It is not harmless now
  // that edk2-redfish-client is compiled in: its ten feature drivers each create
  // their own service off the same discovered interface *after* this handler
  // returns, and every one of them then fails on the first send --
  //
  //     RedfishRestExSendReceive(): Perform HTTP Request Method - 0, URL: ...
  //     ResetHttpTslSession: TCP connection is finished...
  //     HttpSendReceive: /redfish SendReceive failure: Not started
  //
  // -- which surfaces far from here, as "no Redfish version" (so every URI the
  // feature drivers build comes out as "v1Systems" rather than
  // "/redfish/v1/Systems"), "CollectionHandler failure: Not started", and
  // "Fail to dispatch Redfish tasks: Device Error". None of it points back at a
  // cleanup call in an unrelated driver.
  //
  DEBUG ((DEBUG_ERROR, "NucRedfishSync: host interface exchange complete\n"));
}

/**
  EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL.Init.

  Called by RedfishConfigHandlerDriver once a Redfish service has been
  discovered. Runs the exchange inline: the caller is already on the BDS path
  with the network stack up, and the work is a handful of requests against a
  point-to-point link.

  @param[in] This         This protocol instance.
  @param[in] ServiceInfo  Discovered Redfish service information.

  @retval EFI_SUCCESS  Always -- a BMC problem must not block booting.
**/
EFI_STATUS
EFIAPI
NucRedfishConfigHandlerInit (
  IN EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL  *This,
  IN REDFISH_CONFIG_SERVICE_INFORMATION     *ServiceInfo
  )
{
  if (ServiceInfo == NULL) {
    return EFI_SUCCESS;
  }

  //
  // Init is called on two different occasions, and only the second one is
  // usable. RedfishConfigHandlerDriver registers a protocol notify on
  // EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL, so installing ours in the entry point
  // makes it fire immediately -- before any service has been discovered, with
  // gRedfishConfigData.RedfishServiceInfo still zeroed. The call that matters
  // comes later, from RedfishServiceDiscoveredCallback, once that structure has
  // been filled in.
  //
  // So the arrival of a REST EX handle, not the call itself, is the signal that
  // there is something to talk to. Observed on hardware 2026-07-30: gating on
  // the call alone ran the exchange against an empty ServiceInfo ("service at
  // (unknown)", RedfishCreateService failed) and latched the guard, which then
  // suppressed the real invocation.
  //
  // Returning an error here is load-bearing, not just informative.
  // RedfishConfigHandlerInitialization() installs gEfiCallerIdGuid on the
  // handle of every config handler whose Init returned success, and skips
  // handles carrying that GUID on subsequent passes. Answering EFI_SUCCESS to
  // the pre-discovery call therefore marks this driver "already initialized"
  // and the post-discovery pass never calls it again -- which is exactly what
  // happened on hardware 2026-07-30: discovery completed and logged
  // "REST EX is configured" / "is discovered!", and no exchange followed.
  // EFI_NOT_READY takes the `continue` branch instead, leaving the handle
  // unmarked and eligible for the call that has a service attached to it.
  //
  if (ServiceInfo->RedfishServiceRestExHandle == NULL) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: init before discovery (no REST EX yet), waiting\n"));
    return EFI_NOT_READY;
  }

  //
  // Discovery can signal more than once (for example if another interface is
  // acquired later). The inventory report is idempotent, but repeating it would
  // add avoidable delay to every boot.
  //
  if (mSyncDone) {
    return EFI_SUCCESS;
  }

  mSyncDone = TRUE;

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSync: config handler init, service at %s (uuid %s)\n",
    (ServiceInfo->RedfishServiceLocation != NULL) ? ServiceInfo->RedfishServiceLocation : L"(unknown)",
    (ServiceInfo->RedfishServiceUuid != NULL) ? ServiceInfo->RedfishServiceUuid : L"(unknown)"
    ));

  NucRedfishSync (ServiceInfo);

  return EFI_SUCCESS;
}

/**
  EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL.Stop.

  @param[in] This  This protocol instance.

  @retval EFI_SUCCESS  Nothing to tear down; the service handle is released at
                       the end of each exchange.
**/
EFI_STATUS
EFIAPI
NucRedfishConfigHandlerStop (
  IN EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

STATIC EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL  mNucRedfishConfigHandler = {
  NucRedfishConfigHandlerInit,
  NucRedfishConfigHandlerStop
};

/**
  Driver entry point. Installs the config handler protocol; everything else is
  driven by RedfishConfigHandlerDriver when a service is discovered.

  @param[in] ImageHandle  Image handle.
  @param[in] SystemTable  System table.

  @retval EFI_SUCCESS  Protocol installed.
  @retval Others       Installation failed.
**/
EFI_STATUS
EFIAPI
NucRedfishSyncDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  mImageHandle = ImageHandle;

  Status = gBS->InstallProtocolInterface (
                  &mImageHandle,
                  &gEdkIIRedfishConfigHandlerProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mNucRedfishConfigHandler
                  );

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSyncDxe: install EdkIIRedfishConfigHandler protocol - %r\n",
    Status
    ));

  return Status;
}
