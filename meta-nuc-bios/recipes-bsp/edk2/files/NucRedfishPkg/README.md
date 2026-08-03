# NucRedfishPkg — NUC ⇄ BMC Redfish-over-USB glue

EDK2 Redfish host-interface support for the NUC, talking to the JetKVM BMC's
Redfish service over a USB CDC-ECM link.

**Two ways to load it, and the first is now the real one:**

1. **Built into the payload** (current). `edk2-uefipayload_2605.bb` stages this
   package into the tree and
   `files/0001-UefiPayloadPkg-wire-in-the-Redfish-host-interface-sta.patch`
   adds it to `UefiPayloadPkg`, so the whole stack ships inside
   `coreboot-nuc5i7ryh.rom`. Nothing to register, nothing to load by hand.
2. **Standalone `Driver####` drivers on the stock (locked) AMI BIOS** — the
   original approach, kept because it needs no flashing. See "Loading on the
   NUC" below; `NucRedfish.dsc` is that build.

## What actually runs at boot (option 1)

    RedfishHostInterfaceDxe   publishes SMBIOS type 42 from NucRedfishHostInterfaceLib
    RedfishDiscoverDxe        matches the type 42 MAC to the ECM NIC, configures REST EX
    RedfishConfigHandlerDriver signals "service discovered"
    NucRedfishSyncDxe         <- the part that actually talks to the BMC

`NucRedfishSyncDxe` exists because RedfishPkg alone stops one step short.
Verified on hardware 2026-07-30: discovery completed
(`RedfishServiceDiscoveredCallback: Redfish service 5CC27A14-... is discovered!`)
and BDS then went straight to the OS loader without a single HTTP request,
because nothing in the payload produces `EDKII_REDFISH_CONFIG_HANDLER_PROTOCOL`
— that is edk2-redfish-client's job, and it does not build against this tree
(see the note above `SRC_URI` in `edk2-uefipayload_2605.bb`).

`NucRedfishSyncDxe` produces that protocol and performs the exchange:

| Step | Request | Purpose |
|---|---|---|
| 1 | `GET /redfish/v1/` | proves the type 42 record, ECM link and REST EX line up |
| 2 | `PATCH /redfish/v1/Systems/1` | reports SMBIOS identity + `BootProgress` to the BMC |
| 3 | `GET /redfish/v1/Systems/1` | reads the BMC's one-time boot override, applies it as `BootNext` |

The BMC has no in-band view of the host, so step 2 is the only way its
`ComputerSystem` reflects the real machine rather than placeholders. Step 3 is
the management direction: stage
`Boot.BootSourceOverrideTarget` + `BootSourceOverrideEnabled: "Once"` on the BMC
and the host obeys it at the next boot, then clears it.

Everything is fail-open — a BMC that is down, slow or unhappy never stops the
host from booting.

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
| **ECM gadget MAC** | `da:a7:62:23:3e:f5` | `NUC_REDFISH_ECM_MAC*` **and** `PcdRedfishRestExServiceDevicePath` MAC node **and** the Type 42 USB descriptor | gadget `host_addr` must be **fixed** to this |
| **Service UUID** | `5cc27a14-c9f9-50c6-bdaa-b91b6dc77f98` | `PcdRedfishServiceUuid` | `/redfish/v1` ServiceRoot `UUID` |

### The MAC is derived, not configured (resolved 2026-07-29)
`RedfishDiscoverDxe` rejects the interface unless the Type 42 MAC byte-matches
the actual NIC MAC, so the gadget must present a **fixed** `host_addr`. It now
does: `internal/usbgadget/ethernet.go` derives both `dev_addr` and `host_addr`
from the JetKVM's device ID via SHA-256, setting the locally-administered and
unicast bits (IEEE 802-2014 8.2):

```go
SetEthernetMACSeed(GetDeviceID())   // usb.go, before NewUsbGadget
```

For this unit that yields `dev_addr fa:12:bc:84:da:0a` / `host_addr
da:a7:62:23:3e:f5`, and the NUC enumerates the NIC as `enxdaa762233ef5` --
stable across reboots. Nothing to set by hand. Re-derive with
`deriveGadgetMAC(deviceID, "host")` if the BMC is replaced.

### The UUID is derived too (resolved 2026-07-29)
This README previously targeted `nanokvm-app`, whose `GetServiceRoot` omits
`UUID`. The BMC here is the JetKVM, and its `redfish.go` now publishes one:

```go
"UUID": redfishUUID(),   // sha256("jetkvm/redfish-service-uuid/"+deviceID), RFC 4122 v5
```

Both sides compute it independently from the device ID, so there is no exchange
and no manual step. Verified live over the link:

```
$ curl http://169.254.10.1/redfish/v1/
"UUID": "5cc27a14-c9f9-50c6-bdaa-b91b6dc77f98"
```

Set `PcdRedfishServiceUuid` to all-zero to match any service instead.

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
