# mb/intel/nuc5i5ryb — board port sources

These are plain source files copied into `src/mainboard/intel/nuc5i5ryb/` by
the recipe's `do_configure:prepend`, not a patch. The port adds only new files,
so there is nothing upstream to diff against, and shipping sources directly
means edits do not require regenerating a patch.

## Provenance

Upstream base is coreboot Gerrit change 94032 (`mb/intel/nuc5i5ryb`), whose two
cosmetic hunks (mainboard docs index, MAINTAINERS) were dropped. When a new
patchset lands there, diff it against these files rather than re-applying it.
Once the port merges upstream, this directory can be deleted and the recipe's
copy step removed.

## Board-local work on top of 94032

* `bootblock.c` — programs the NCT5577D's AC-loss policy (LDN 0x0a CR 0xe4[6:5])
  early via `mainboard_config_superio()`, and logs `GEN_PMCON_3` from
  `bootblock_mainboard_init()` *before* ramstage's read-modify-write clears the
  write-1-to-clear RTC-well flags. That log line is the only way to tell a
  healthy RTC well from one that lost the coin cell:
  `modprobe memconsole-coreboot; grep GEN_PMCON_3 /sys/firmware/log`
* `smihandler.c` — `mainboard_smi_sleep()`: disables Super I/O keyboard wake,
  sets the CR 0xe6[4] last-power-state latch, and re-asserts `PME_B0_EN`
  (cleared unconditionally by `global_smi_enable()`) on the way into S5.
* `acpi/lan.asl` + `devicetree.cb`'s `gpe0_en_4` — Wake-on-LAN as the remote
  power-on path. `_PRW` depth is 5, not 4: Linux's
  `acpi_enable_wakeup_devices()` skips devices whose depth is below the target
  sleep state, and poweroff targets S5.
* `Kconfig` — `select MAINBOARD_USES_IFD_GBE_REGION` is *not* present; add it if
  `CONFIG_HAVE_GBE_BIN` is ever wanted. That symbol is promptless
  (`def_bool n`), so setting `HAVE_GBE_BIN` in a config fragment silently does
  nothing without the select.

`data.vbt` is deliberately absent here: the recipe installs the real table from
`files/blobs/data.vbt` via `COREBOOT_VBT_FILE`.
