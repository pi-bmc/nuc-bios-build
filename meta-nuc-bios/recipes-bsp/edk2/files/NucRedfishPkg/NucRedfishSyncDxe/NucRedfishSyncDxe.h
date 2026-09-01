/** @file
  NucRedfishSyncDxe - definitions for the host-side Redfish Host Interface
  client.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef NUC_REDFISH_SYNC_DXE_H_
#define NUC_REDFISH_SYNC_DXE_H_

#include <Uefi.h>

#include <IndustryStandard/Atapi.h>
#include <IndustryStandard/Nvme.h>
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

#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DiskInfo.h>
#include <Protocol/EdkIIRedfishConfigHandler.h>
#include <Protocol/NvmExpressPassthru.h>
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
#define NUC_REDFISH_MEMORY_URI        L"/redfish/v1/Systems/1/Memory"
#define NUC_REDFISH_PROCESSORS_URI    L"/redfish/v1/Systems/1/Processors"
#define NUC_REDFISH_DRIVES_URI        L"/redfish/v1/Systems/1/Storage/1/Drives"

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

//
// DIMM slots reported. This board has two; the bound is generous so a table
// with more entries is truncated rather than overrunning.
//
#define NUC_REDFISH_MEMORY_MAX  8

//
// One populated memory device, as SMBIOS type 17 describes it, reduced to the
// Redfish Memory v1_7_1 properties the BMC stores.
//
// This exists because edk2-redfish-client's MemoryDxe cannot supply it. That
// driver has no SMBIOS dependency at all -- its only source is
// EDKII_REDFISH_PLATFORM_CONFIG_PROTOCOL, i.e. HII questions in the
// "x-UEFI-redfish-Memory.v1_7_1" namespace -- so on any platform that does not
// publish DIMM inventory as BIOS *setup questions* (which is every platform,
// because Memory is inventory and not configuration) it walks all 38 schema
// properties, misses every one, and POSTs an empty resource:
//
//     RedfishPlatformConfigGetStatementCommon: No match HII statement is found
//       by the given /Memory/{1}/CapacityMiB in schema x-UEFI-redfish-Memory.v1_7_1
//     ... x37 more ...
//     RedfishPostResource: Post URI: /redfish/v1/Systems/1/Memory
//
// The data itself is present and correct -- coreboot's raminit publishes full
// type 17 records, and the OS reads them -- so report them the same way this
// driver already reports type 0 and type 1.
//
typedef struct {
  CHAR8          DeviceLocator[NUC_REDFISH_STR_MAX];
  CHAR8          BankLocator[NUC_REDFISH_STR_MAX];
  CHAR8          Manufacturer[NUC_REDFISH_STR_MAX];
  CHAR8          SerialNumber[NUC_REDFISH_STR_MAX];
  CHAR8          PartNumber[NUC_REDFISH_STR_MAX];
  CONST CHAR8    *MemoryDeviceType;               // "DDR3", "DDR4", ... or NULL
  CONST CHAR8    *BaseModuleType;                 // "SO_DIMM", "UDIMM", ... or NULL
  UINT32         CapacityMiB;
  UINT32         OperatingSpeedMhz;               // configured speed
  UINT32         RatedSpeedMhz;                   // the module's own rating
  UINT16         DataWidthBits;
  UINT16         BusWidthBits;
} NUC_REDFISH_MEMORY_MODULE;

/**
  Collect populated memory devices from the SMBIOS type 17 records this
  firmware published.

  Unpopulated slots (Size == 0) and slots of unknown size are skipped: SMBIOS
  emits a record per socket whether or not it is filled, and reporting an empty
  socket as a Memory resource would claim hardware that is not there.

  @param[out] Modules  Receives the populated modules.
  @param[in]  Max      Capacity of Modules.
  @param[out] Count    Receives the number written.

  @retval EFI_SUCCESS    Zero or more modules were collected.
  @retval EFI_NOT_FOUND  The SMBIOS protocol is not available.
**/
EFI_STATUS
NucRedfishCollectMemory (
  OUT NUC_REDFISH_MEMORY_MODULE  *Modules,
  IN  UINTN                      Max,
  OUT UINTN                      *Count
  );

/**
  Build the Memory POST body for one module.

  @param[in]  Module  Module to describe.
  @param[out] Json    Receives an allocated ASCII JSON body. Caller frees with
                      FreePool().

  @retval EFI_SUCCESS           Body was built.
  @retval EFI_OUT_OF_RESOURCES  Allocation failed.
**/
EFI_STATUS
NucRedfishBuildMemoryPost (
  IN  NUC_REDFISH_MEMORY_MODULE  *Module,
  OUT CHAR8                      **Json
  );

//
// Processor sockets reported. The NUC5i7RYH is single-socket; the bound
// leaves headroom and keeps a malformed table from overrunning.
//
#define NUC_REDFISH_PROCESSOR_MAX  4

//
// One populated processor socket, as SMBIOS type 4 describes it, reduced to
// the Redfish Processor v1_14_0 properties the BMC stores. Same member
// contract as the RPi5 implementation this is ported from: Socket doubles as
// the member identity, and the POST never carries the operator-managed
// SpeedLimitMHz/SpeedLocked pair (this platform has no clock knob to back
// them; the BMC preserves whatever an operator staged across the re-POST).
//
typedef struct {
  CHAR8          Socket[NUC_REDFISH_STR_MAX];
  CHAR8          Manufacturer[NUC_REDFISH_STR_MAX];
  CHAR8          Model[NUC_REDFISH_STR_MAX];
  CHAR8          SerialNumber[NUC_REDFISH_STR_MAX];
  CHAR8          PartNumber[NUC_REDFISH_STR_MAX];
  CHAR8          IdRegisters[19];                  // "0x" + 16 hex digits + NUL
  CONST CHAR8    *ProcessorType;                   // "CPU", "GPU", ... or NULL
  UINT32         MaxSpeedMhz;
  UINT32         OperatingSpeedMhz;                // current, not rated
  UINT32         TotalCores;
  UINT32         TotalEnabledCores;
  UINT32         TotalThreads;
  BOOLEAN        Enabled;                          // type 4 CPU Status says enabled
} NUC_REDFISH_PROCESSOR;

/**
  Collect populated processor sockets from the SMBIOS type 4 records coreboot
  published.

  Unpopulated sockets are skipped on the same reasoning as memory: SMBIOS
  emits a record per socket whether or not it is filled, and reporting an
  empty one would claim hardware that is not there.

  @param[out] Processors  Receives the populated sockets.
  @param[in]  Max         Capacity of Processors.
  @param[out] Count       Receives the number written.

  @retval EFI_SUCCESS    Zero or more processors were collected.
  @retval EFI_NOT_FOUND  The SMBIOS protocol is not available.
**/
EFI_STATUS
NucRedfishCollectProcessors (
  OUT NUC_REDFISH_PROCESSOR  *Processors,
  IN  UINTN                  Max,
  OUT UINTN                  *Count
  );

/**
  Build the Processor POST body for one socket.

  @param[in]  Processor  Processor to describe.
  @param[out] Json       Receives an allocated ASCII JSON body. Caller frees
                         with FreePool().

  @retval EFI_SUCCESS           Body was built.
  @retval EFI_OUT_OF_RESOURCES  Allocation failed.
**/
EFI_STATUS
NucRedfishBuildProcessorPost (
  IN  NUC_REDFISH_PROCESSOR  *Processor,
  OUT CHAR8                  **Json
  );

//
// Physical drives reported. One NVMe SSD is the expected population; the bound
// covers a SATA disk in the 2.5" bay as well, with headroom.
//
#define NUC_REDFISH_DRIVE_MAX  8

//
// One physical drive, reduced to the Redfish Drive properties the BMC stores.
//
// Unlike memory, this cannot come from SMBIOS at all: DSP0134 defines no
// structure type for a disk (its whole storage story is type 9 slots and type
// 41 onboard devices), so the boot-services protocol stack is the only
// firmware-native source. It also cannot come from edk2-redfish-client, whose
// Features/ directory has no Storage driver -- and whose only data source
// (HII questions) could never carry drive inventory anyway.
//
typedef struct {
  CHAR8          Model[NUC_REDFISH_STR_MAX];
  CHAR8          SerialNumber[NUC_REDFISH_STR_MAX];
  CHAR8          Revision[NUC_REDFISH_STR_MAX];    // firmware revision
  CONST CHAR8    *Protocol;                        // "NVMe" or "SATA"
  CONST CHAR8    *MediaType;                       // "SSD", "HDD", or NULL when unknown
  UINT64         CapacityBytes;                    // 0 = unknown
} NUC_REDFISH_DRIVE;

/**
  Collect the local drives BDS has connected, via EFI_DISK_INFO_PROTOCOL.

  Only reports what is already connected: this runs at TPL_CALLBACK, where
  ConnectController is not permitted, so a drive BDS has not brought up is
  invisible here. On this board the exchange runs during BdsWait, after
  ConnectAll, so that is every drive.

  @param[out] Drives  Receives the drives.
  @param[in]  Max     Capacity of Drives.
  @param[out] Count   Receives the number written.

  @retval EFI_SUCCESS  Zero or more drives were collected.
**/
EFI_STATUS
NucRedfishCollectDrives (
  OUT NUC_REDFISH_DRIVE  *Drives,
  IN  UINTN              Max,
  OUT UINTN              *Count
  );

/**
  Build the Drive POST body for one drive.

  @param[in]  Drive  Drive to describe.
  @param[out] Json   Receives an allocated ASCII JSON body. Caller frees with
                     FreePool().

  @retval EFI_SUCCESS           Body was built.
  @retval EFI_OUT_OF_RESOURCES  Allocation failed.
**/
EFI_STATUS
NucRedfishBuildDrivePost (
  IN  NUC_REDFISH_DRIVE  *Drive,
  OUT CHAR8              **Json
  );

#endif // NUC_REDFISH_SYNC_DXE_H_
