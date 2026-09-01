/** @file

  EthCfg - the persistent BMC-managed IPv4 policy for the onboard NIC,
  shared between EthConfigDxe (efivarstore Setup page + boot-time apply
  into Ip4Config2) and the Redfish EthernetInterface feature driver
  (RedfishEthernetInterfaceDxe), which consumes standard
  /Systems/1/EthernetInterfaces/{id} PATCHes into these questions.

  This header is included by VFR as well as C: keep it to #defines and the
  varstore struct only.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef NUC_ETH_CONFIG_H_
#define NUC_ETH_CONFIG_H_

//
// Vendor GUID of the EthCfg variable AND the EthConfigDxe formset
// (one GUID for both, the ConfigDxe idiom).
//
#define NUC_ETH_CONFIG_FORMSET_GUID \
  { 0x967e2001, 0x25c5, 0x4d1e, { 0xbf, 0x24, 0xb4, 0x22, 0xd8, 0x81, 0xa1, 0xa9 } }

#define NUC_ETH_CONFIG_VARIABLE_NAME  L"EthCfg"

//
// "255.255.255.255" plus the terminating NUL.
//
#define NUC_ETH_IP4_STR_SIZE  16

//
// "AA:BB:CC:DD:EE:FF" plus the terminating NUL.
//
#define NUC_ETH_MAC_STR_SIZE  18

#pragma pack (1)
typedef struct {
  //
  // Standard EthernetInterface semantics (DHCPv4/DHCPEnabled): TRUE
  // applies the DHCP policy every boot (the platform default anyway);
  // FALSE with a parseable Address+SubnetMask below applies the static
  // configuration; FALSE with an empty address touches nothing, which
  // keeps whatever the NIC's native IPv4 form configured.
  //
  BOOLEAN    DhcpEnabled;
  //
  // The claimed NIC's MAC, seeded from its device path each boot.
  // Report-only (READ_ONLY question): the value behind the Redfish
  // MACAddress property.
  //
  CHAR16     MacAddress[NUC_ETH_MAC_STR_SIZE];
  //
  // Dotted-quad strings, NUL terminated, empty when unset. Address and
  // SubnetMask are required for a static apply (a parse failure keeps
  // the boot on the NIC's existing configuration); Gateway and the two
  // DNS servers are optional.
  //
  CHAR16     Ip4Address[NUC_ETH_IP4_STR_SIZE];
  CHAR16     Ip4SubnetMask[NUC_ETH_IP4_STR_SIZE];
  CHAR16     Ip4Gateway[NUC_ETH_IP4_STR_SIZE];
  CHAR16     Ip4Dns1[NUC_ETH_IP4_STR_SIZE];
  CHAR16     Ip4Dns2[NUC_ETH_IP4_STR_SIZE];
} NUC_ETH_CONFIG;
#pragma pack ()

#endif // NUC_ETH_CONFIG_H_
