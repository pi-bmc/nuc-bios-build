/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Intel PTT fTPM 2.0. No CRB at the standard 0xFED40000; the ME's TCG-PTP
 * control area is at 0xFED70000 (offset 0, data buffer at +0x80), published
 * as the TPM2 control area with Start Method 2 (see acpi/tpm.asl).
 */

#include <acpi/acpi.h>
#include <bootstate.h>
#include <console/console.h>
#include <delay.h>
#include <device/device.h>
#include <device/mmio.h>
#include <option.h>
#include <stdint.h>
#include <string.h>
#include <timer.h>
#include <version.h>

#include "nuc5i5ryb.h"

/* ME PTT CRB control area + in-window data buffer. */
#define PTT_CRB_BASE 0xFED70000
#define PTT_CRB_DATA (PTT_CRB_BASE + 0x80)
#define PTT_CRB_DATA_SIZE 0xF80 /* 0x80..0xFFF of the 4K window */

/* CRB control-area offsets (TCG PTP "Control Area"; cf. Linux crb_regs_tail).
 */
#define CRB_CTRL_START 0x0c
#define CRB_CTRL_CMD_SIZE 0x18
#define CRB_CTRL_CMD_PA_LO 0x1c
#define CRB_CTRL_CMD_PA_HI 0x20
#define CRB_CTRL_RSP_SIZE 0x24
#define CRB_CTRL_RSP_PA_LO 0x28
#define CRB_CTRL_RSP_PA_HI 0x2c
#define CRB_HCMD 0x40 /* Intel doorbell (RE'd, not in TCG PTP) */
#define CRB_HSTS 0x44 /* Intel status, 0x03 = ready (RE'd) */

/* Stage cmd, re-arm (HCMD=0), execute, poll; return TPM RC or 0xffffffff. */
static uint32_t ptt_run(const uint8_t *cmd, size_t len) {
  for (size_t i = 0; i < len; i++)
    write8((void *)(PTT_CRB_DATA + i), cmd[i]);

  write32((void *)(PTT_CRB_BASE + CRB_HCMD), 0);       /* re-arm */
  write32((void *)(PTT_CRB_BASE + CRB_CTRL_START), 1); /* execute */

  struct stopwatch sw;
  stopwatch_init_msecs_expire(&sw, 2000);
  while (read32((void *)(PTT_CRB_BASE + CRB_CTRL_START)) & 1) {
    if (stopwatch_expired(&sw))
      return 0xffffffff;
    udelay(50);
  }

  /* response code is a big-endian u32 at buffer offset 6 */
  return (read8((void *)(PTT_CRB_DATA + 6)) << 24) |
         (read8((void *)(PTT_CRB_DATA + 7)) << 16) |
         (read8((void *)(PTT_CRB_DATA + 8)) << 8) |
         read8((void *)(PTT_CRB_DATA + 9));
}

/* TPM2_Startup(TPM_SU_CLEAR): per-boot startup, keeps keys (TPM 2.0 Part 3, CC
 * 0x144). */
static void ptt_tpm_startup(void) {
  static const uint8_t startup_clear[] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0c,
                                          0x00, 0x00, 0x01, 0x44, 0x00, 0x00};
  uint32_t rc = ptt_run(startup_clear, sizeof(startup_clear));

  /* 0x100 = TPM_RC_INITIALIZE = already started; both are fine. */
  if (rc == 0 || rc == 0x100)
    printk(BIOS_DEBUG, "PTT: TPM2_Startup ok (rc=0x%x)\n", rc);
  else
    printk(BIOS_WARNING, "PTT: TPM2_Startup failed (rc=0x%x)\n", rc);
}

/* TPM2_Clear on the platform hierarchy (empty-password session); works only
   from firmware while the auth is still default. TPM 2.0 Part 3, CC 0x126. */
static void ptt_tpm_clear(void) {
  static const uint8_t clear_cmd[] = {
      0x80, 0x02,             /* TPM_ST_SESSIONS */
      0x00, 0x00, 0x00, 0x1b, /* commandSize = 27 */
      0x00, 0x00, 0x01, 0x26, /* TPM2_Clear */
      0x40, 0x00, 0x00, 0x0c, /* authHandle = TPM_RH_PLATFORM */
      0x00, 0x00, 0x00, 0x09, /* authorizationSize = 9 */
      0x40, 0x00, 0x00, 0x09, /* sessionHandle = TPM_RS_PW */
      0x00, 0x00,             /* nonceSize = 0 */
      0x00,                   /* sessionAttributes */
      0x00, 0x00              /* hmacSize = 0 */
  };
  uint32_t rc = ptt_run(clear_cmd, sizeof(clear_cmd));

  if (rc == 0)
    printk(BIOS_INFO, "PTT: TPM2_Clear ok\n");
  else
    printk(BIOS_WARNING, "PTT: TPM2_Clear failed (rc=0x%x)\n", rc);
}

/* Set once the fTPM is enabled + the CRB is up; gates the TPM2 ACPI table. */
static bool ptt_enabled;

static void ptt_setup(void) {
  if (!get_uint_option("tpm_enable", 1)) {
    printk(BIOS_INFO, "PTT: fTPM disabled via setup option\n");
    return;
  }

  if (read32((void *)(PTT_CRB_BASE + CRB_HSTS)) == 0xffffffff) {
    printk(BIOS_WARNING, "PTT: CRB window 0x%x not decoded; fTPM off\n",
           PTT_CRB_BASE);
    return;
  }

  /* Point the CRB command/response buffers at the in-window data buffer. */
  write32((void *)(PTT_CRB_BASE + CRB_CTRL_CMD_SIZE), PTT_CRB_DATA_SIZE);
  write32((void *)(PTT_CRB_BASE + CRB_CTRL_CMD_PA_LO), PTT_CRB_DATA);
  write32((void *)(PTT_CRB_BASE + CRB_CTRL_CMD_PA_HI), 0);
  write32((void *)(PTT_CRB_BASE + CRB_CTRL_RSP_SIZE), PTT_CRB_DATA_SIZE);
  write32((void *)(PTT_CRB_BASE + CRB_CTRL_RSP_PA_LO), PTT_CRB_DATA);
  write32((void *)(PTT_CRB_BASE + CRB_CTRL_RSP_PA_HI), 0);

  ptt_enabled = true;
  printk(BIOS_DEBUG, "PTT: CRB @0x%x ready (HSTS 0x%x), data buffer @0x%x\n",
         PTT_CRB_BASE, read32((void *)(PTT_CRB_BASE + CRB_HSTS)), PTT_CRB_DATA);

  /* Start it now so the OS finds it ready (no OS-side self-test/startup). */
  ptt_tpm_startup();

  /* One-shot: consume the request before clearing, so a failure can't loop. */
  if (get_uint_option("tpm_clear", 0)) {
    if (set_uint_option("tpm_clear", 0) == CB_SUCCESS)
      ptt_tpm_clear();
    else
      printk(BIOS_WARNING, "PTT: tpm_clear set but not resettable; skipping to "
                           "avoid a clear loop\n");
  }
}

/* Emit the TPM2 table (Start Method 2) via the write_acpi_tables hook; the
   generic acpi_create_tpm2() no-ops with no coreboot TPM stack. */
unsigned long ptt_write_tpm2_table(const struct device *dev,
                                   unsigned long current,
                                   struct acpi_rsdp *rsdp) {
  if (!ptt_enabled)
    return current;

  current = acpi_align_current(current);
  acpi_tpm2_t *tpm2 = (acpi_tpm2_t *)current;
  acpi_header_t *header = &tpm2->header;

  memset(tpm2, 0, sizeof(*tpm2));
  memcpy(header->signature, "TPM2", 4);
  memcpy(header->oem_id, OEM_ID, 6);
  memcpy(header->oem_table_id, ACPI_TABLE_CREATOR, 8);
  memcpy(header->asl_compiler_id, ASLC, 4);
  header->asl_compiler_revision = asl_revision;
  header->revision = get_acpi_table_revision(TPM2);
  header->length = sizeof(acpi_tpm2_t);

  tpm2->control_area = PTT_CRB_BASE;
  tpm2->start_method = ACPI_TPM2_SM_ACPI_START;

  header->checksum = acpi_checksum((void *)tpm2, header->length);
  acpi_add_table(rsdp, tpm2);
  printk(BIOS_DEBUG, "PTT: TPM2 table @0x%lx, control area 0x%x\n", current,
         PTT_CRB_BASE);

  return current + header->length;
}

static void ptt_bs_setup(void *unused) { ptt_setup(); }
BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, ptt_bs_setup, NULL);
