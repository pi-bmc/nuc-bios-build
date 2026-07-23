# NUC UEFI-driver build

A Yocto/OpenEmbedded build that produces **standalone UEFI `.efi` drivers** for
the **Intel NUC5i7RYH** (Broadwell-U, Wildcat Point-LP). The drivers are staged
onto a USB/FAT volume and loaded on the NUC's **stock (locked) BIOS** via a
`Driver####` load option — they are **not** flashed into firmware.

Why not flash: this NUC's stock Intel BIOS locks the SPI (`SMM_BWP` + BIOS
Guard), so the coreboot route needs an external SPI programmer. Loading
drivers from USB sidesteps that entirely and works headlessly over SSH.

## Building

```sh
pip3 install kas
kas build kas.yml
```

Output: `build/tmp/deploy/images/nuc5i7ryh/efi-drivers/*.efi` (+ `SHA256SUMS`).

Default driver set (EDK2 USB-network stack):

| Driver | Role |
|---|---|
| `NetworkCommon.efi` | produces `EFI_SIMPLE_NETWORK_PROTOCOL` (the SNP the stack binds on) |
| `UsbCdcEcm.efi` | CDC-ECM transport (matches the BMC's `ecm.usb0` gadget) |
| `UsbRndis.efi` | RNDIS transport |
| `UsbCdcNcm.efi` | NCM transport |

All are x64 `EFI Boot Service Driver` images. The set is defined by
`EFI_DRIVER_INFS` in
[`edk2-efi-drivers_git.bb`](meta-nuc-bios/recipes-bsp/edk2/edk2-efi-drivers_git.bb) —
append your own driver INFs there. (A custom driver pulling HTTP/RestEx/JSON
library classes will need its own small DSC rather than `MdeModulePkg.dsc`.)

## Deploying to the NUC (over SSH, no flashing)

1. Copy the drivers onto a FAT volume (a USB stick, or the NUC's spare ESP)
   under `\EFI\drivers\` — e.g. `NetworkCommon.efi` + `UsbCdcEcm.efi` for the
   ECM path.
2. Ensure the "Allow UEFI 3rd party driver loaded" BIOS option is enabled
   (Secure Boot is off, so unsigned drivers load).
3. Register each as a driver load option:
   ```sh
   efibootmgr --driver --create --disk /dev/sdX --part 1 \
       --loader '\EFI\drivers\UsbCdcEcm.efi' --label "usb-ecm" --reconnect
   ```
4. Reboot — the firmware loads them during BDS every boot; the USB Ethernet
   gadget appears as an SNP that the stock network stack binds on top of.

## Layout

- `meta-nuc-bios/conf/machine/nuc5i7ryh.conf` — a minimal x86-64 target machine
  (the drivers are built by EDK2's own X64 toolchain; the machine just gives
  bitbake a valid target).
- `meta-nuc-bios/recipes-bsp/edk2/edk2-efi-drivers_git.bb` — the driver build.
  Reuses the pinned MrChromebox edk2 tree (its `MdeModulePkg` lists these
  drivers as build components), builds each INF standalone with
  `build -p MdeModulePkg.dsc -m <inf>`, and deploys the `.efi` files.
