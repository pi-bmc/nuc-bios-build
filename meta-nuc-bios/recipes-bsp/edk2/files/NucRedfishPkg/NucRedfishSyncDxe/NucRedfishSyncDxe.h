/** @file
  NucRedfishSyncDxe - definitions for the host-side Redfish Host Interface
  client.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef NUC_REDFISH_SYNC_DXE_H_
#define NUC_REDFISH_SYNC_DXE_H_

#include <Uefi.h>

#include <IndustryStandard/SmBios.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/RedfishHttpLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/EdkIIRedfishConfigHandler.h>
#include <Protocol/Smbios.h>

#include <RedfishServiceData.h>

//
// The managed system this firmware reports itself as. The BMC models exactly
// one ComputerSystem ("1"), so the URIs are fixed rather than walked from the
// service root -- one fewer round trip on the boot path, and the BMC is a
// known peer (its UUID is matched during discovery).
//
#define NUC_REDFISH_SERVICE_ROOT_URI  L"/redfish/v1/"
#define NUC_REDFISH_SYSTEM_URI        L"/redfish/v1/Systems/1"

//
// Boot progress state reported at the point the config handler runs: DXE is
// complete and BDS is selecting a boot option. This is the DSP2046
// BootProgressTypes value for that moment.
//
#define NUC_REDFISH_BOOT_PROGRESS  "SystemHardwareInitializationComplete"

//
// Upper bound on the JSON body we build. The payload is a fixed set of short
// SMBIOS-derived strings; 1 KiB leaves generous headroom and keeps the
// allocation off the boot path's critical size budget.
//
#define NUC_REDFISH_JSON_MAX  1024

//
// The first request over the host interface routinely fails with EFI_NO_MEDIA:
// the USB-net stack only learns the link is up by catching a CDC
// NETWORK_CONNECTION notification, and the one the BMC's gadget sends during
// enumeration predates this driver. The BMC re-announces on a 5 s timer, so a
// handful of retries a couple of seconds apart is enough to meet it, while
// bounding the delay this adds to a boot where the BMC never answers.
//
#define NUC_REDFISH_MEDIA_RETRIES      8
#define NUC_REDFISH_MEDIA_RETRY_STALL  2000000   // 2 s, in microseconds

//
// Maximum length kept for any single SMBIOS-sourced string. SMBIOS strings are
// unbounded in principle; truncating defensively keeps a malformed table from
// overflowing the JSON buffer.
//
#define NUC_REDFISH_STR_MAX  64

typedef struct {
  CHAR8    BiosVersion[NUC_REDFISH_STR_MAX];
  CHAR8    Manufacturer[NUC_REDFISH_STR_MAX];
  CHAR8    Model[NUC_REDFISH_STR_MAX];
  CHAR8    SerialNumber[NUC_REDFISH_STR_MAX];
  CHAR8    Uuid[37];                            // 36 chars + NUL
  BOOLEAN  UuidValid;
} NUC_REDFISH_HOST_INVENTORY;

/**
  Collect host inventory (BIOS version, system manufacturer/model/serial/UUID)
  from the SMBIOS tables this firmware published.

  @param[out] Inventory  Receives the collected inventory. Fields that cannot be
                         resolved are left as empty strings.

  @retval EFI_SUCCESS    Inventory was collected (possibly partially).
  @retval EFI_NOT_FOUND  The SMBIOS protocol is not available.
**/
EFI_STATUS
NucRedfishCollectInventory (
  OUT NUC_REDFISH_HOST_INVENTORY  *Inventory
  );

/**
  Build the ComputerSystem PATCH body reporting this host to the BMC.

  @param[in]  Inventory  Host inventory to report.
  @param[out] Json       Receives an allocated ASCII JSON body. Caller frees with
                         FreePool().

  @retval EFI_SUCCESS           Body was built.
  @retval EFI_OUT_OF_RESOURCES  Allocation failed.
**/
EFI_STATUS
NucRedfishBuildSystemPatch (
  IN  NUC_REDFISH_HOST_INVENTORY  *Inventory,
  OUT CHAR8                       **Json
  );

#endif // NUC_REDFISH_SYNC_DXE_H_
