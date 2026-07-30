##
#  install-drivers.nsh -- register the NUC Redfish EFI driver set as persistent
#  UEFI Driver#### load options on the stock (locked) NUC BIOS. Path B: nothing
#  is flashed; the drivers live on this USB/FAT volume and BDS loads them from
#  here at every boot.
#
#  HOW TO RUN (from a UEFI Shell 2.0 booted off this USB):
#     Shell> map -r                       # enumerate filesystems
#     Shell> fs1:                          # <-- whichever FSx maps to THIS USB
#     FS1:\> cd \EFI\BOOT\drivers
#     FS1:\EFI\BOOT\drivers\> install-drivers.nsh
#     FS1:\EFI\BOOT\drivers\> reset          # reboot; drivers now auto-load
#
#  Filenames below are resolved against the CURRENT directory, so you MUST cd
#  into \EFI\BOOT\drivers on the USB fs before running (or launch it as the USB's
#  \startup.nsh, in which case cwd is already the USB root -- see the cd line).
#
#  PREREQUISITES on the NUC (BIOS/F2, one time):
#     * Secure Boot        = Disabled   (unsigned drivers must load)
#     * "Allow UEFI 3rd party driver loaded" = Enabled  (AMI Setup offset 0x5B)
#     * Fast Boot          = Disabled   (so USB + net enumerate before BDS)
#
#  RE-RUNNING duplicates entries. To start clean:  bcfg driver dump   then
#  bcfg driver rm <#> for each stale entry (highest index first), then re-run.
##
echo -off
cd \EFI\BOOT\drivers

echo "Registering NUC Redfish driver set (Driver#### load options)..."

#  --- USB-Ethernet transport + SNP (must come first) ---
bcfg driver add 00 NetworkCommon.efi                    "NucRfsh:NetworkCommon"
bcfg driver add 01 UsbCdcEcm.efi                        "NucRfsh:UsbCdcEcm"
bcfg driver add 02 UsbRndis.efi                         "NucRfsh:UsbRndis"
bcfg driver add 03 UsbCdcNcm.efi                        "NucRfsh:UsbCdcNcm"

#  --- NetworkPkg IPv4 + HTTP stack ---
bcfg driver add 04 DpcDxe.efi                           "NucRfsh:Dpc"
bcfg driver add 05 MnpDxe.efi                           "NucRfsh:Mnp"
bcfg driver add 06 ArpDxe.efi                           "NucRfsh:Arp"
bcfg driver add 07 Ip4Dxe.efi                           "NucRfsh:Ip4"
bcfg driver add 08 Udp4Dxe.efi                          "NucRfsh:Udp4"
bcfg driver add 09 TcpDxe.efi                           "NucRfsh:Tcp"
bcfg driver add 10 Dhcp4Dxe.efi                         "NucRfsh:Dhcp4"
bcfg driver add 11 DnsDxe.efi                           "NucRfsh:Dns"
bcfg driver add 12 HttpUtilitiesDxe.efi                 "NucRfsh:HttpUtil"
bcfg driver add 13 HttpDxe.efi                          "NucRfsh:Http"
bcfg driver add 14 RegularExpressionDxe.efi             "NucRfsh:Regex"

#  --- RedfishPkg core ---
bcfg driver add 15 RestJsonStructureDxe.efi             "NucRfsh:RestJson"
bcfg driver add 16 RedfishHostInterfaceDxe.efi          "NucRfsh:HostIface"
bcfg driver add 17 RedfishRestExDxe.efi                 "NucRfsh:RestEx"
bcfg driver add 18 RedfishCredentialDxe.efi             "NucRfsh:Credential"
bcfg driver add 19 RedfishDiscoverDxe.efi               "NucRfsh:Discover"
bcfg driver add 20 RedfishHttpDxe.efi                   "NucRfsh:RedfishHttp"
bcfg driver add 21 RedfishConfigHandlerDriver.efi       "NucRfsh:ConfigHandler"

#  --- our platform config producer (AMI Setup-var backed) ---
bcfg driver add 22 RedfishConfigDriver.efi              "NucRfsh:PlatformConfig"

#  --- RedfishClientPkg feature layer (sync engine + domains) ---
bcfg driver add 23 RedfishFeatureCoreDxe.efi            "NucRfsh:FeatureCore"
bcfg driver add 24 RedfishETagDxe.efi                   "NucRfsh:ETag"
bcfg driver add 25 RedfishConfigLangMapDxe.efi          "NucRfsh:ConfigLangMap"
bcfg driver add 26 HiiToRedfishBootDxe.efi              "NucRfsh:HiiToBoot"
bcfg driver add 27 BiosDxe.efi                          "NucRfsh:BiosFeature"
bcfg driver add 28 BiosAttributeRegistryDxe.efi         "NucRfsh:BiosAttrReg"
bcfg driver add 29 BootOptionDxe.efi                    "NucRfsh:BootOption"
bcfg driver add 30 BootOptionCollectionDxe.efi          "NucRfsh:BootOptColl"
bcfg driver add 31 RedfishBios_V1_0_9_Dxe.efi           "NucRfsh:BiosConv"
bcfg driver add 32 RedfishAttributeRegistry_V1_3_6_Dxe.efi "NucRfsh:AttrRegConv"
bcfg driver add 33 RedfishBootOption_V1_0_4_Dxe.efi     "NucRfsh:BootOptConv"

echo "Driver#### set registered. Current DriverOrder:"
bcfg driver dump -v

#  --- ConnectRedfishApp is a UEFI *application*, not a driver, so it can NOT be
#      a Driver#### entry. Register it as a boot option you can fire once (via
#      BootNext / the F10 boot menu) to force a re-connect after the drivers
#      load, in case the stock BIOS ran connect-all before them.
bcfg boot add 00 ConnectRedfishApp.efi                  "NucRfsh:ConnectOnce"
echo "ConnectRedfishApp registered as a boot option (fire it once after reboot)."

echo "Done. 'reset' to reboot -- drivers load automatically from this USB."
