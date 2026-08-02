/** @file
  NucRedfishSyncDxe - the host-side client of the Redfish Host Interface.

  RedfishPkg gets the host as far as *discovering* the BMC: RedfishHostInterfaceDxe
  publishes the SMBIOS type 42 record, RedfishDiscoverDxe correlates it with the
  CDC-ECM NIC and configures REST EX, and RedfishConfigHandlerDriver signals that
  a service is available. Nothing then talks to it. The drivers that would --
  edk2-redfish-client's feature layer -- are not buildable against this tree (see
  the note at the top of files/wire-redfish.py), so on a stock RedfishPkg build
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
                                       boot override and applies it as BootNext.

  Everything here is fail-open: an unreachable or unhappy BMC must never stop the
  host from booting, so every failure is logged and returns EFI_SUCCESS.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "NucRedfishSyncDxe.h"

#include <Library/DevicePathLib.h>
#include <Library/JsonLib.h>
#include <Library/PcdLib.h>
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
  Report whether a boot option loads a particular file out of a firmware volume.

  @param[in] Option    Boot option to inspect.
  @param[in] FileGuid  FFS file GUID to match, may be NULL.

  @retval TRUE   The option's device path names this FV file.
  @retval FALSE  It does not, or FileGuid is NULL.
**/
STATIC
BOOLEAN
OptionIsFvFile (
  IN EFI_BOOT_MANAGER_LOAD_OPTION  *Option,
  IN EFI_GUID                      *FileGuid
  )
{
  EFI_DEVICE_PATH_PROTOCOL           *Node;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  *FvFile;

  if ((Option == NULL) || (Option->FilePath == NULL) || (FileGuid == NULL)) {
    return FALSE;
  }

  for (Node = Option->FilePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
        (DevicePathSubType (Node) == MEDIA_PIWG_FW_FILE_DP))
    {
      FvFile = (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)Node;
      if (CompareGuid (&FvFile->FvFileName, FileGuid)) {
        return TRUE;
      }
    }
  }

  return FALSE;
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
  Apply a Redfish BootSourceOverrideTarget as a one-time BootNext.

  BY DESIGN, the override takes effect on the boot *after* the one that fetched
  it -- this is not a bug to be worked around at this layer. BdsEntry caches
  BootNext before it calls any PlatformBootManagerLib API, and says why:

      // Cache the "BootNext" NV variable before calling any PlatformBootManagerLib
      // APIs. This could avoid the "BootNext" set by PlatformBootManagerLib be
      // consumed in this boot.
      -- MdeModulePkg/Universal/BdsDxe/BdsEntry.c

  Anything that runs during BDS -- which includes this driver, because it cannot
  run until discovery has configured REST EX over a connected controller -- is
  on the far side of that snapshot. Verified on hardware 2026-07-30: staging
  BiosSetup/Once left BootNext=Boot0000 set after a boot that went to the OS,
  and the next boot landed in the Boot Manager.

  Same-boot semantics would require the whole exchange to happen before BDS,
  i.e. connecting the USB host controller, the ECM function, SNP, IP4 and REST
  EX by hand at End-of-DXE. That is a much larger change and it lengthens every
  boot, including those where the BMC has nothing staged. Staging a target and
  then power-cycling -- the usual OOB flow -- is unaffected.

  Redfish names boot *classes* ("Pxe", "Hdd", "BiosSetup"); UEFI has numbered
  Boot#### options. The mapping is done by scanning the boot options this
  firmware already built and matching on the attributes BDS itself uses --
  LOAD_OPTION_CATEGORY_APP for the setup UI, and the device path's messaging
  type for network options -- rather than on description text, which is
  localised and unstable.

  @param[in] Target  Redfish BootSourceOverrideTarget value.

  @retval EFI_SUCCESS    BootNext was set.
  @retval EFI_NOT_FOUND  No boot option matched the requested class.
**/
STATIC
EFI_STATUS
ApplyBootOverride (
  IN CONST CHAR8  *Target
  )
{
  EFI_BOOT_MANAGER_LOAD_OPTION  *Options;
  UINTN                         OptionCount;
  UINTN                         Index;
  UINTN                         Match;
  BOOLEAN                       Found;
  EFI_STATUS                    Status;
  UINT16                        BootNext;

  if ((Target == NULL) || (AsciiStrCmp (Target, "None") == 0)) {
    return EFI_NOT_FOUND;
  }

  Options = EfiBootManagerGetLoadOptions (&OptionCount, LoadOptionTypeBoot);
  if ((Options == NULL) || (OptionCount == 0)) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: no boot options to override\n"));
    return EFI_NOT_FOUND;
  }

  Found = FALSE;
  Match = 0;

  //
  // Dump the candidates. An override that does not apply is otherwise
  // indistinguishable from one that was never staged, and the boot option set
  // this early in BDS is not what `efibootmgr` shows from the OS -- it predates
  // EfiBootManagerRefreshAllBootOption(), so auto-created disk entries may not
  // exist yet. Cheap: this only runs when the BMC has actually staged a target.
  //
  for (Index = 0; Index < OptionCount; Index++) {
    DEBUG ((
      DEBUG_ERROR,
      "NucRedfishSync:   Boot%04x \"%s\" attr=0x%x%a%a\n",
      Options[Index].OptionNumber,
      (Options[Index].Description != NULL) ? Options[Index].Description : L"",
      Options[Index].Attributes,
      OptionHasNode (&Options[Index], MEDIA_DEVICE_PATH, MEDIA_HARDDRIVE_DP) ? " [HD]" : "",
      OptionHasNode (&Options[Index], MEDIA_DEVICE_PATH, MEDIA_PIWG_FW_FILE_DP) ? " [FvFile]" : ""
      ));
  }

  for (Index = 0; Index < OptionCount; Index++) {
    if (AsciiStrCmp (Target, "BiosSetup") == 0) {
      //
      // The setup UI (UiApp) is the boot option BDS tags as an application.
      //
      Found = ((Options[Index].Attributes & LOAD_OPTION_CATEGORY) == LOAD_OPTION_CATEGORY_APP);
    } else if (AsciiStrCmp (Target, "Pxe") == 0) {
      //
      // Network boot on this platform *is* iPXE: it is built into the payload
      // FV (EDK2_ENABLE_IPXE) and registered by PlatformBootManagerLib as an FV
      // file boot option, so its device path ends in a firmware-volume file
      // node carrying PcdiPXEFile -- not in a MAC/IPv4 node the way a NIC's own
      // UEFI PXE option would. Matching the GUID identifies it exactly, without
      // depending on PcdiPXEOptionName, which is a display string.
      //
      Found = OptionIsFvFile (&Options[Index], (EFI_GUID *)PcdGetPtr (PcdiPXEFile));
    } else if (AsciiStrCmp (Target, "Hdd") == 0) {
      //
      // Local block storage, in whichever shape BDS has it at this point.
      // See OptionIsDiskBoot().
      //
      Found = OptionIsDiskBoot (&Options[Index]);
    }

    if (Found) {
      Match = Index;
      break;
    }
  }

  if (!Found) {
    DEBUG ((DEBUG_ERROR, "NucRedfishSync: no boot option matches target '%a'\n", Target));
    EfiBootManagerFreeLoadOptions (Options, OptionCount);
    return EFI_NOT_FOUND;
  }

  BootNext = Options[Match].OptionNumber;

  Status = gRT->SetVariable (
                  L"BootNext",
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  sizeof (BootNext),
                  &BootNext
                  );

  DEBUG ((
    DEBUG_ERROR,
    "NucRedfishSync: boot override '%a' -> Boot%04x \"%s\" - %r\n",
    Target,
    BootNext,
    Options[Match].Description != NULL ? Options[Match].Description : L"(no description)",
    Status
    ));

  EfiBootManagerFreeLoadOptions (Options, OptionCount);
  return Status;
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
  EDKII_JSON_VALUE   Root;
  EDKII_JSON_VALUE   Boot;
  EDKII_JSON_VALUE   Value;
  CONST CHAR8        *Target;
  CONST CHAR8        *Enabled;
  EFI_STATUS         Status;
  REDFISH_RESPONSE   AckResponse;

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

  if (EFI_ERROR (ApplyBootOverride (Target))) {
    return;
  }

  //
  // Clear the override so it is genuinely one-shot even if the host resets
  // before BDS consumes BootNext.
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
    // No point attempting the rest: the service is not answering.
    //
    RedfishCleanupService (Service);
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
  // 3. Read back the system, including any boot override the BMC wants applied.
  //
  ZeroMem (&Response, sizeof (Response));
  Status = RedfishHttpGetResource (Service, NUC_REDFISH_SYSTEM_URI, NULL, &Response, FALSE);
  LogResult ("GET", NUC_REDFISH_SYSTEM_URI, Status, &Response);
  if (!EFI_ERROR (Status)) {
    HandleBootOverride (Service, &Response);
  }

  RedfishHttpFreeResponse (&Response);
  RedfishCleanupService (Service);

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
