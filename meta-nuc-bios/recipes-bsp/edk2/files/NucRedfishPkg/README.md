# NucRedfishPkg — NUC ⇄ BMC Redfish-over-USB glue

Standalone EDK2 Redfish client drivers loaded on the NUC's stock (locked) BIOS
via `Driver####`, talking to the BMC's Redfish service over a USB CDC-ECM link.
No firmware flashing.

## Deployment convention (both sides MUST agree)

Because the NUC drivers are compiled with fixed values and the BMC is
configured at runtime, these are a **contract** — set the BMC to match, or the
NUC won't discover/reach it.

| Thing | Value | NUC side (compiled here) | BMC side (must configure) |
|---|---|---|---|
| Link subnet | `169.254.10.0/16` | host-interface lib IPs/mask | ECM `usb0` address |
| Host (NUC) IP | `169.254.10.2` | `NucRedfishHostInterfaceLib.c` | — (the NUC is the host) |
| Redfish service (BMC) IP | `169.254.10.1` | host-interface record | ECM `usb0` = `169.254.10.1/16` |
| Service port | `80` (HTTP, no TLS) | `PcdRedfishServicePort` | Redfish HTTP listener on `:80` |
| **ECM gadget MAC** | `06:00:00:00:00:01` | `NUC_REDFISH_ECM_MAC*` **and** `PcdRedfishRestExServiceDevicePath` MAC node **and** the Type 42 USB descriptor | gadget `host_addr` must be **fixed** to this |
| **Service UUID** | `c1298dbc-7850-4ffb-85bf-a5e241c28125` | `PcdRedfishServiceUuid` | `/redfish/v1` ServiceRoot `UUID` |

### Why the MAC is a convention, not a queried value
The NanoKVM gadget script (`nanokvm-build .../S03usbdev`) sets the host-end MAC
as `06:$mac_tail`, and `mac_tail` is **kernel-randomized per unit** by default.
Redfish discovery (`RedfishDiscoverDxe`) rejects the interface unless the Type 42
MAC byte-matches the actual NIC MAC — so the gadget must present a **fixed**
`host_addr`. Set it explicitly on the BMC:

```sh
echo "06:00:00:00:00:01" > functions/ecm.usb0/host_addr   # (before UDC bind)
```

Change the MAC in one NUC-side place too if you pick a different value:
`NucRedfishHostInterfaceLib.c` (`NUC_REDFISH_ECM_MAC_*`) **and** the DSC
`PcdRedfishRestExServiceDevicePath.DevicePath` `MAC(...)` node — they must agree.

### Why the UUID needs a BMC change
`nanokvm-app`'s `GetServiceRoot` (server/service/redfish/service_root.go) does
**not** set the `UUID` property (it's `omitempty`). For discovery correlation,
add it:

```go
Resource: Resource{ ... },
UUID:     "c1298dbc-7850-4ffb-85bf-a5e241c28125",
```

If you can't change the BMC, set `PcdRedfishServiceUuid` back to all-zero
(`00000000-0000-0000-0000-000000000000`) which matches any service.

## Loading on the NUC (over SSH, no flashing)

1. Copy every `*.efi` from `build/tmp/deploy/images/nuc5i7ryh/efi-drivers/` to a
   FAT volume under `\EFI\drivers\`.
2. Register the **drivers** with `efibootmgr --driver --create` (load order:
   USB-net → NetworkPkg → RegularExpression/RestJsonStructure → Redfish core →
   `RedfishConfigDriver` → RedfishClient feature drivers). `ConnectRedfishApp.efi`
   is an **application**, not a driver — do not register it as a `Driver####`.
3. Enable "Allow UEFI 3rd party driver loaded" (via the `setup_var`/`BootNext`
   step; Secure Boot is off).
4. After the drivers are registered, run **`ConnectRedfishApp.efi`** once — as a
   one-shot `BootNext` boot option or from the UEFI shell — to force the driver
   stack to bind (the stock BIOS's connect-all ran before our drivers loaded).

## Open TODOs (functional wiring)

- `RedfishConfigDriver.c` — the `AMI_SETUP_MAP_ENTRY` table maps Redfish BIOS
  attributes to `Setup`-variable byte offsets (from the extracted IFR). Verify
  each offset against this exact firmware build (`RYBDWi35.86A.0386`).
- End-to-end only provable against the live BMC over the ECM link.
