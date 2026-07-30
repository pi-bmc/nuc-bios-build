#!/usr/bin/env python3
"""Wire the Redfish host-interface client stack into UefiPayloadPkg.

UefiPayloadPkg has no Redfish support of its own, but it already carries
``!include NetworkPkg/*`` lines at exactly the right point in every section --
[Defines], [LibraryClasses], [Components] in the DSC and the DXE FV in the FDF.
RedfishPkg ships matching ``.dsc.inc``/``.fdf.inc`` files, so each Redfish
include goes directly after its Network counterpart.

Every insertion is anchored on a literal line that must appear exactly once. A
missing or ambiguous anchor is a hard error rather than a guess -- the same
philosophy as the coreboot recipe's refcode GbE patch, and the reason this is a
script rather than a .patch: it survives upstream churn that would break
context diffs, and fails loudly when it genuinely cannot place a hunk.

Idempotent: re-running on an already-wired tree is a no-op, so do_configure can
run repeatedly without stacking duplicates.
"""

import sys

MARKER = "# --- NucRedfish: wired by wire-redfish.py ---"

# RedfishClientPkg (edk2-redfish-client) is deliberately NOT wired in.
#
# It is the BIOS *attribute-sync* feature layer -- BiosDxe, BootOptionDxe and
# the JSON converters -- which sits on top of a working host interface. It is
# not needed for initial sync (discovery + RestEx + the type 42 record), and
# neither NucRedfishHostInterfaceLib nor RedfishConfigDriver depends on it:
# both list only RedfishPkg in their [Packages].
#
# It also does not currently build against this tree. The MrChromebox fork
# tracks UefiPayloadPkg, not RedfishPkg, so its RedfishPkg lags upstream edk2
# badly regardless of the fork's HEAD date (2026-07-12):
#
#   RedfishClientPkg/Library/RedfishEventLib/RedfishEventLib.inf(39):
#     error 4000: Value of Guid [gEdkIIRedfisEventRedfishInterfaceDisconnectionGuid]
#     is not found under [Guids] section
#
# That GUID is declared by upstream RedfishPkg.dec; this fork's copy has no
# RedfishEvent GUIDs at all. Re-adding the client layer therefore needs a
# matched pair -- either a newer RedfishPkg (rebase the fork, or overlay
# upstream RedfishPkg onto it) or an edk2-redfish-client revision old enough to
# match. Pick the pair deliberately rather than by AUTOREV.

# ---------------------------------------------------------------------------
# DSC
# ---------------------------------------------------------------------------

DSC_DEFINES = """
!include RedfishPkg/RedfishDefines.dsc.inc
"""

# RedfishLibs.dsc.inc resolves RedfishPlatformCredentialLib to
# RedfishPlatformCredentialIpmiLib, which fetches bootstrap credentials over
# IPMI. This board has no IPMI transport at all (no LPC KCS, no SSIF), so that
# instance can never succeed -- swap in the Null one. The BMC instead accepts
# unauthenticated requests that arrive on the USB host-interface subnet, which
# DSP0270 permits.
#
# RedfishPlatformHostInterfaceLib is not set by RedfishLibs.dsc.inc at all --
# it is the platform's job. The upstream BmcUsbNic instance is likewise
# IPMI-driven (IpmiGetChannelInfo/IpmiGetLanConfigurationParameters with no
# fallback), hence NucRedfishHostInterfaceLib.
#
# Both lines come *after* the include so they win: EDK2's DSC parser keeps the
# last definition of a library class within a section.
DSC_LIBS = """
!include RedfishPkg/RedfishLibs.dsc.inc
  RedfishPlatformCredentialLib|NucRedfishPkg/Library/NucRedfishCredentialLib/NucRedfishCredentialLib.inf
  RedfishPlatformHostInterfaceLib|NucRedfishPkg/Library/NucRedfishHostInterfaceLib/NucRedfishHostInterfaceLib.inf
  RedfishPlatformWantedDeviceLib|RedfishPkg/Library/RedfishPlatformWantedDeviceLibNull/RedfishPlatformWantedDeviceLibNull.inf
  RedfishContentCodingLib|RedfishPkg/Library/RedfishContentCodingLibNull/RedfishContentCodingLibNull.inf
"""

# UsbCdcEcm + NetworkCommon are what turn the BMC's CDC-ECM gadget into an
# EFI_SIMPLE_NETWORK_PROTOCOL. UefiPayloadPkg ships only prebuilt vendor USB
# NIC blobs (Realtek/ASIX) and never references the in-tree drivers.
DSC_COMPONENTS = """
!include RedfishPkg/RedfishComponents.dsc.inc
  MdeModulePkg/Bus/Usb/UsbNetwork/NetworkCommon/NetworkCommon.inf
  MdeModulePkg/Bus/Usb/UsbNetwork/UsbCdcEcm/UsbCdcEcm.inf
  NucRedfishPkg/RedfishConfigDriver/RedfishConfigDriver.inf
  NucRedfishPkg/NucRedfishSyncDxe/NucRedfishSyncDxe.inf
"""

# ---------------------------------------------------------------------------
# FDF
# ---------------------------------------------------------------------------

FDF_DXE = """
!include RedfishPkg/Redfish.fdf.inc
  INF MdeModulePkg/Bus/Usb/UsbNetwork/NetworkCommon/NetworkCommon.inf
  INF MdeModulePkg/Bus/Usb/UsbNetwork/UsbCdcEcm/UsbCdcEcm.inf
  INF NucRedfishPkg/RedfishConfigDriver/RedfishConfigDriver.inf
  INF NucRedfishPkg/NucRedfishSyncDxe/NucRedfishSyncDxe.inf
"""

# Without these the drivers load and do nothing. Observed on hardware
# 2026-07-29: RedfishHostInterfaceDxe published SMBIOS type 42 and
# RedfishDiscoverDxe loaded, but discovery never ran -- the whole boot produced
# exactly two Redfish log lines and BDS went straight to the OS loader.
#
#   PcdRedfishRestExServiceDevicePath  is how RedfishRestExDxe decides WHICH
#       network interface is the Redfish host interface. Left at its default it
#       matches nothing, so no interface is ever acquired. Matched by MAC node
#       against the gadget's pinned host_addr.
#   PcdRedfishDisableBootstrapCredentialService  turns off the DSP0270
#       bootstrap-credential exchange, which is specified over IPMI -- a
#       transport this board does not have. The BMC accepts unauthenticated
#       requests arriving on the host-interface subnet instead.
#   PcdRedfishServiceUuid  must equal the UUID in the type 42 protocol record
#       and the BMC's ServiceRoot.UUID.
#
# These live in files/NucRedfishPkg/NucRedfish.dsc too, which is the standalone
# driver build; keep the two in step.
DSC_PCDS = """
[PcdsFixedAtBuild]
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishServicePort|80
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishDisableBootstrapCredentialService|TRUE
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishServiceUuid|L"5cc27a14-c9f9-50c6-bdaa-b91b6dc77f98"
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishRestExServiceDevicePath.DevicePathMatchMode|DEVICE_PATH_MATCH_MAC_NODE
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishRestExServiceDevicePath.DevicePathNum|1
  gEfiRedfishPkgTokenSpaceGuid.PcdRedfishRestExServiceDevicePath.DevicePath|{DEVICE_PATH("MAC(DAA762233EF5,0x1)")}
"""

# RedfishDiscoverDxe reports almost everything at DEBUG_MANAGEABILITY
# (0x00800000): which network interfaces it found, whether a controller carried
# REST EX, and why it skipped one. UefiPayloadPkg builds with
# PcdDebugPrintErrorLevel = 0x8000004F, which does not include that bit, so all
# of it is discarded -- the driver can fail every discovery step and produce a
# completely silent log. Observed 2026-07-29: RedfishDiscoverDxe dispatched but
# EFI_REDFISH_DISCOVER_PROTOCOL never appeared, with no explanation anywhere in
# 2555 lines of cbmem.
#
# Turn the bit on so the discovery path is debuggable. Costs log volume only.
DEBUG_LEVEL_OLD = "gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8000004F"
DEBUG_LEVEL_NEW = "gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8080004F"


def enable_manageability_debug(text, path):
    count = text.count(DEBUG_LEVEL_OLD)
    if count != 1:
        sys.exit(
            f"wire-redfish: expected exactly one PcdDebugPrintErrorLevel in {path}, "
            f"found {count} -- re-check the debug mask before changing it."
        )
    return text.replace(DEBUG_LEVEL_OLD, DEBUG_LEVEL_NEW, 1)


# We build with NETWORK_IP6_ENABLE=FALSE, so Ip6Dxe is absent and no handle ever
# carries gEfiTcp6ServiceBindingProtocolGuid.  RedfishDiscoverDxe gates the TCP6
# row of mRequiredProtocol[] on PcdIPv6HttpSupport
# (IsRedfishRequiredProtocolIndexActive), and TestForRequiredProtocols is an
# all-of test that returns EFI_UNSUPPORTED on the first missing entry.  Left at
# TRUE, RedfishDiscoverDriverBindingSupported therefore rejects *every*
# controller -- including the one that does have TCP4 + REST EX -- so
# DriverBindingStart never runs and EFI_REDFISH_DISCOVER_PROTOCOL is never
# installed.  Silently: the only diagnostics on that path are
# DEBUG_MANAGEABILITY.  Observed 2026-07-29.
#
# This PCD is DynamicEx here (NetworkPkg.dec declares it in a section that
# permits both), and NetworkDynamicPcds.dsc.inc sets it TRUE.  A [PcdsFixedAtBuild]
# override is silently ignored -- the AutoGen.h for RedfishDiscoverDxe still
# emitted LibPcdGetExBool.  It has to be re-stated in a dynamic section after
# that include.
DSC_IPV6_PCD = """
[PcdsDynamicExDefault]
  gEfiNetworkPkgTokenSpaceGuid.PcdIPv6HttpSupport|FALSE
"""

DSC_EDITS = [
    ("!include NetworkPkg/NetworkDefines.dsc.inc", DSC_DEFINES),
    ("!include NetworkPkg/NetworkLibs.dsc.inc", DSC_LIBS),
    ("!include NetworkPkg/NetworkComponents.dsc.inc", DSC_COMPONENTS),
    ("!include NetworkPkg/NetworkFixedPcds.dsc.inc", DSC_PCDS),
    ("!include NetworkPkg/NetworkDynamicPcds.dsc.inc", DSC_IPV6_PCD),
]


def insert_after(text, anchor, payload, path):
    """Insert payload after the single occurrence of anchor."""
    count = text.count(anchor)
    if count == 0:
        sys.exit(f"wire-redfish: anchor not found in {path}:\n  {anchor}")
    if count > 1:
        sys.exit(
            f"wire-redfish: anchor is ambiguous ({count} occurrences) in {path}:\n"
            f"  {anchor}\n"
            "Refusing to guess which one to patch."
        )
    return text.replace(anchor, anchor + "\n" + MARKER + payload + MARKER, 1)


def wire_dsc(path):
    with open(path) as fh:
        text = fh.read()
    if MARKER in text:
        print(f"wire-redfish: {path} already wired, skipping")
        return
    for anchor, payload in DSC_EDITS:
        text = insert_after(text, anchor, payload, path)
    text = enable_manageability_debug(text, path)
    with open(path, "w") as fh:
        fh.write(text)
    print(f"wire-redfish: wired {len(DSC_EDITS)} sections into {path}")


# The RELEASE firmware volume is budgeted at 9 MiB, which the Redfish +
# NetworkPkg + USB-NIC drivers overflow:
#
#   GenFv: ERROR 3000: the required fv image size 0xaa1e20 exceeds the set fv
#   image size 0x900000
#
# This is the *uncompressed* FV budget, not the size of the artifact that ends
# up in CBFS: cbfstool converts UEFIPAYLOAD.fd to a payload ELF and drops the
# FV's empty padding, which is why a 9 MiB FD_SIZE previously yielded a 2.02 MiB
# fallback/payload entry. Raising it costs CBFS only the real added content.
FD_SIZE_OLD = """DEFINE FD_SIZE     = 0x0900000
DEFINE NUM_BLOCKS  = 0x900"""
FD_SIZE_NEW = """DEFINE FD_SIZE     = 0x0C00000
DEFINE NUM_BLOCKS  = 0xC00"""


def resize_release_fv(text, path):
    """Grow the RELEASE FD from 9 MiB to 12 MiB."""
    count = text.count(FD_SIZE_OLD)
    if count != 1:
        sys.exit(
            f"wire-redfish: expected exactly one RELEASE FD_SIZE block in {path}, "
            f"found {count}. The FDF layout changed -- re-check the size budget "
            "against the GenFv requirement before adjusting."
        )
    return text.replace(FD_SIZE_OLD, FD_SIZE_NEW, 1)


def wire_fdf(path):
    with open(path) as fh:
        text = fh.read()
    if MARKER in text:
        print(f"wire-redfish: {path} already wired, skipping")
        return

    text = resize_release_fv(text, path)

    # The FDF includes NetworkPkg/Network.fdf.inc twice -- once for the
    # NETWORKFV and once for the DXE FV. Redfish drivers are ordinary DXE
    # drivers and belong with the second, so anchor on the last occurrence
    # rather than erroring on the ambiguity.
    anchor = "!include NetworkPkg/Network.fdf.inc"
    idx = text.rfind(anchor)
    if idx < 0:
        sys.exit(f"wire-redfish: anchor not found in {path}:\n  {anchor}")
    if text.count(anchor) not in (1, 2):
        sys.exit(
            f"wire-redfish: expected 1 or 2 occurrences of the FDF anchor in "
            f"{path}, found {text.count(anchor)} -- layout changed, re-verify "
            "which FV the Redfish drivers belong in."
        )
    end = idx + len(anchor)
    text = text[:end] + "\n" + MARKER + FDF_DXE + MARKER + text[end:]

    with open(path, "w") as fh:
        fh.write(text)
    print(f"wire-redfish: wired the DXE FV in {path}")


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: wire-redfish.py <UefiPayloadPkg.dsc> <UefiPayloadPkg.fdf>")
    wire_dsc(sys.argv[1])
    wire_fdf(sys.argv[2])


if __name__ == "__main__":
    main()
