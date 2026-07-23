# Deploying the NUC Redfish EFI drivers (Path B — no flashing)

The build (`bitbake edk2-efi-drivers`) deposits 34 UEFI drivers + one helper
application into
`build/tmp/deploy/images/nuc5i7ryh/efi-drivers/*.efi` with a `SHA256SUMS`
manifest. These are **loose drivers loaded from USB**, not flashed into the
locked NUC BIOS — the stock firmware loads them at boot via persistent
`Driver####` load options.

## The model

| Piece | UEFI mechanism | Registered by |
|---|---|---|
| 34 DXE drivers | `Driver####` + `DriverOrder` (auto-load every boot) | `bcfg driver add` |
| `ConnectRedfishApp.efi` (a UEFI *application*) | `Boot####` fired once via BootNext / F10 | `bcfg boot add` |

`efibootmgr` on Linux **cannot** write `Driver####` entries (it only does
`Boot####`/`BootNext`), so registration is done from a **UEFI Shell** with
`bcfg`, which builds the correct device path from each file automatically.
Order matters: `Driver####` images run their entry point at load time, so
[install-drivers.nsh](install-drivers.nsh) lists them in dependency order
(USB-net → NetworkPkg → RedfishPkg core → config producer → client feature
layer), **not** alphabetically.

## Prerequisites (BIOS/F2, one time)

- **Secure Boot = Disabled** — unsigned drivers must load (already off:
  `SetupMode=1` on the unit).
- **"Allow UEFI 3rd party driver loaded" = Enabled** — AMI `Setup` offset
  `0x5B`. Gates loading of unsigned 3rd-party drivers even with SB off.
- **Internal UEFI Shell = Enabled** — AMI `Setup` offset `0x1C`. Gives you a
  UEFI Shell in the F10 boot menu, so you need **no** shell binary on the USB.
- **Fast Boot = Disabled** — so USB and the CDC-ECM gadget enumerate before BDS.

(All three toggles are in our decoded AMI offset map; if you build the
setup_var enabler app you can flip `0x5B`/`0x1C` headlessly, but the simplest
path is to set them once in the visual BIOS.)

## Steps

1. **Stage the USB** (Linux, USB already mounted as FAT32):
   ```sh
   ./stage-usb.sh -s <deploy>/images/nuc5i7ryh/efi-drivers -d /run/media/you/NUCUSB
   ```
   Copies `*.efi` + `SHA256SUMS` + `install-drivers.nsh` to
   `EFI/BOOT/drivers/` on the stick and verifies checksums.

2. **Register the drivers** (on the NUC, F10 → Internal UEFI Shell):
   ```
   Shell> map -r
   Shell> fs1:                          # whichever FSx is the USB
   FS1:\> cd \EFI\BOOT\drivers
   FS1:\EFI\BOOT\drivers\> install-drivers.nsh
   FS1:\EFI\BOOT\drivers\> reset
   ```

3. **Verify.** After reboot the 34 drivers auto-load. Fire the connect helper
   once (F10 → `NucRfsh:ConnectOnce`, or `ConnectRedfishApp.efi` from the
   shell), then confirm on the BMC that the CDC-ECM link is up (169.254.10.2 ↔
   169.254.10.1:80) and the Redfish service answers `GET /redfish/v1`.

## Notes

- **Keep the USB in the same port.** `bcfg` records the full device path
  including the USB topology; moving ports invalidates the `Driver####` paths.
  For permanence, stage to the NUC's internal ESP instead of a stick.
- **Re-running `install-drivers.nsh` duplicates entries.** To reset:
  `bcfg driver dump`, then `bcfg driver rm <#>` highest-index-first, then re-run.
- **`ConnectRedfishApp` is one-shot per boot.** It re-runs `ConnectController`
  to bind the stack in case the stock BIOS ran connect-all before our drivers
  loaded. If the stack binds reliably on its own (EDK2 BDS loads `Driver####`
  before connect-all), you can skip it after validating.
