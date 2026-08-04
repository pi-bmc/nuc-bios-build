SUMMARY = "iPXE EFI application embedded in the edk2 payload firmware volume"
DESCRIPTION = "Builds bin-x86_64-efi-sb/ipxe.efi and deploys it as ipxe.rom, \
mirroring coreboot's payloads/external/iPXE + payloads/external/edk2 pipeline: \
that iPXE Makefile copies whichever target it built to ipxe.rom, and the edk2 \
Makefile's ipxe_rom target then copies ipxe/ipxe.rom to \
UefiPayloadPkg/NetworkDrivers/ipxe.efi, where UefiPayloadPkg.fdf picks it up as \
an FFS in the DXE volume. edk2-uefipayload does the same copy and passes the \
matching -D NETWORK_IPXE=TRUE and PcdiPXEOptionName, which is what makes \
PlatformBootManagerLib register the boot option. \
\
The -sb (secure boot) variant is what coreboot builds for the EFI target. It \
restricts the driver set to DRIVERS_SECBOOT and asserts at build time that every \
included file carries FILE_SECBOOT() -- no signing keys involved. The NUC's \
I218-V is unaffected: drivers/net/intel.c declares FILE_SECBOOT(PERMITTED). It \
suits this payload, which builds with SECURE_BOOT_ENABLE=TRUE. \
\
The LOM exists at all only because the coreboot recipe's refcode GbE-enable \
patch (COREBOOT_REFCODE_GBE_PATCH=1) flips the Broadwell refcode's GbE-disable \
byte -- without it there is no NIC for iPXE to drive."
HOMEPAGE = "https://ipxe.org"

# iPXE is GPL-2.0-or-later with the UBDL (UEFI Binary Distribution License)
# additional-permissions exception. The md5 is the canonical GPLv2 text
# (b234ee4d... is oe-core's common-licenses GPL-2.0-only checksum), which is
# what the pinned SRCREV ships as COPYING.GPLv2.
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://COPYING.GPLv2;md5=b234ee4d69f5fce4486a80fdaf4a4263"

SRC_URI = "git://github.com/ipxe/ipxe.git;protocol=https;branch=master"
# master HEAD as of 2026-07-21.
SRCREV = "ebca9ed2b2e62ee579100142ee61f5ba77a3c712"

PV = "1.21.1+git${SRCPV}"
S = "${WORKDIR}/git"

inherit deploy

# The ROM is firmware; it embeds its own libc/drivers and links nothing from
# the target sysroot.
INHIBIT_DEFAULT_DEPS = "1"

# Host-built like coreboot's cbfstool/blob tooling: ipxe.efi is an x86_64 UEFI
# artifact regardless of the build host, and this build host is
# x86_64, so the BUILD compiler produces it directly. iPXE also needs perl
# and binutils (objcopy/ld) on the build host -- add them to HOSTTOOLS if a
# minimal builder lacks them.
DEPENDS = ""

do_configure[noexec] = "1"

# No PCI expansion ROM is built. It was, once, targeting the onboard NIC --
# confirmed on hardware as 8086:15a3 ("i218v-3"), not the 15a1 this recipe first
# guessed -- but that path never worked and cannot: coreboot's pci_rom_run()
# returns early for anything that is not PCI_CLASS_DISPLAY_VGA, so a LOM's option
# ROM in CBFS is never dispatched, and the I218-V has no physical expansion-ROM
# BAR for PciBusDxe to find either. Confirmed on hardware 2026-07-28: no PXE boot
# option appeared. coreboot's own edk2 path does not build one either; the FV
# route below is the one that produces a working SNP.

# iPXE source is fully vendored; no submodule fetch, so no network at compile.
do_compile() {
    # Keep bitbake's exported cross CC out of iPXE's host build (same 'unset'
    # dance the coreboot/edk2 recipes use). iPXE resolves plain gcc from PATH;
    # HOST_CC drives its own util/ tools (elf2efi, efirom).
    unset CC CXX CPP AS AR LD RANLIB STRIP OBJCOPY NM CFLAGS CXXFLAGS CPPFLAGS LDFLAGS

    # Build from scratch. Everything this recipe controls is passed as a make
    # *variable* (ASFLAGS, DRIVERS_efi_net below), and a variable change
    # invalidates no file prerequisite -- so an incremental make happily keeps a
    # binary built under the old settings. That is not hypothetical: bitbake
    # re-runs do_compile in place on a recipe edit, and the first attempt at the
    # DRIVERS_efi_net change silently produced the previous build's ipxe.efi,
    # timestamps and all. It also explains how this recipe went so long without
    # ever building from a clean tree (see the ASFLAGS note below).
    rm -rf ${S}/src/bin-x86_64-efi-sb ${S}/src/bin-x86_64-efi

    # EFI_DOWNGRADE_UX stops iPXE installing its own EFI_LOAD_FILE_PROTOCOL.
    #
    # BDS creates a boot option for every handle carrying LoadFile, so with it
    # installed the onboard NIC appeared twice -- once for iPXE's LoadFile and
    # once for the platform's own PXE stack layered on the same handle:
    #
    #   PXEv4 (MAC:B8AEED7E3F6E)        <- iPXE's LoadFile
    #   PXEv4 (MAC:B8AEED7E3F6E) 2      <- edk2 PXE BC, via its Ip4 child
    #
    # Only the second boots. Selecting the first fails at BDS with "failed: Not
    # Found" and, with nothing to fall back to, parks the machine at a "Press
    # any key to continue" prompt.
    #
    # iPXE anticipates exactly this. Its own comment in interface/efi/efi_snp.c
    # notes that the two cannot sensibly coexist because the boot menu labels
    # both entries identically, and offers this switch to suppress its own. That
    # is the right way round here: this build is a UNDI/SNP provider for the
    # platform's PXE stack, not a boot method in its own right.
    install -d ${S}/src/config/local
    printf '#define EFI_DOWNGRADE_UX\n' > ${S}/src/config/local/general.h

    # The same target coreboot's payloads/external/iPXE builds when
    # CONFIG_IPXE_BUILD_EFI is set.
    #
    # coreboot also passes -fno-pic here (PXE_MAKE_OPTS, commented as working
    # around relocation issues). That is NOT carried over: with this iPXE and
    # this host gcc it causes the very problem it is meant to avoid. iPXE
    # compiles with -fpie; appending -fno-pic wins and switches codegen to
    # absolute addressing, so the link emits R_X86_64_32S and elf2efi cannot
    # translate it into a PE relocation:
    #
    #     ./util/elf2efi64 --subsystem=10 ... bin-x86_64-efi-sb/ipxe.efi
    #     Unrecognised relocation type 11
    #
    # Whatever toolchain that flag helps, it is not this one -- coreboot builds
    # iPXE with its own crossgcc.
    # ASFLAGS is overridden rather than extended. iPXE appends --fatal-warnings
    # to it (Makefile.housekeeping), and binutils 2.4x warns on the
    # .note.GNU-stack declarations scattered through arch/x86/prefix/*.S:
    #
    #     arch/x86/prefix/mromprefix.S:44: Warning: ignoring incorrect section type for .note.GNU-stack
    #     {standard input}: Error: 2 warnings, treating warnings as errors
    #
    # Those are 16-bit BIOS prefixes. An EFI-only image links none of them, but
    # iPXE compiles every source under the platform's SRCDIRS, so one warning
    # anywhere fails the build. EXTRA_ASFLAGS cannot help: it is appended before
    # --fatal-warnings, and --no-warn does not survive it. A command-line
    # assignment does, because make lets it win over the makefile's `+=`.
    #
    # The value reproduces what the build otherwise computes (--64 from
    # arch/x86_64, --divide from WORKAROUND_ASFLAGS) minus --fatal-warnings. If
    # iPXE ever adds another workaround flag it would be dropped here, hence the
    # explicit note -- check `as` invocations in the log if assembly misbehaves.
    #
    # coreboot does not hit this because it builds iPXE with its own crossgcc
    # toolchain, which carries an older binutils.
    #
    # Two artifacts out of one tree:
    #
    #   ipxe.efi        the boot *application* PlatformBootManagerLib registers
    #                   as a boot option, driving the LOM with its own PCI
    #                   driver.
    #   intel.efidrv    a UEFI *driver* that binds the LOM and publishes
    #                   EFI_SIMPLE_NETWORK_PROTOCOL for it (iPXE's
    #                   interface/efi/efi_snp.c), dispatched from the DXE FV.
    #
    # The driver exists because without it nothing in this payload gives the
    # onboard NIC an SNP at all. Measured on hardware 2026-08-04:
    #
    #   NucRedfishSync: 2 SNP handle(s) in the system
    #   NucRedfishSync:   SNP[0] DA:A7:62:23:3E:F5
    #   NucRedfishSync:   SNP[1] DA:A7:62:23:3E:F5
    #
    # -- both the BMC's CDC-ECM gadget, none the LOM (B8:AE:ED:7E:3F:6E).
    # UefiPayloadPkg ships prebuilt Realtek and ASIX UNDI blobs and no Intel
    # one, and SnpDxe only layers SNP over an existing UNDI/NII instance.
    #
    # That is what breaks a chainloaded iPXE snp.efi, which Tinkerbell uses:
    # snp.efi has no native drivers and binds SNP handles only, so it saw the
    # management link as its one and only interface and retried DHCP on it until
    # it gave up. This is not fixable by ordering -- there was no second
    # interface to order against.
    #
    # The target name is the driver set: TGT_DRIVERS for "intel" resolves to the
    # intel driver alone (Makefile.housekeeping), so this carries no USB or SNP
    # shims and cannot bind the gadget itself. The DRIVERS_* overrides below
    # apply only to DRIVERS_ipxe and so do not touch it.
    #
    # The driver comes out of the plain bin-x86_64-efi tree, not the -sb one.
    # iPXE's secboot assertion covers the application prefix but not the driver
    # prefix, so the -sb target refuses to link:
    #
    #     The following files are missing a FILE_SECBOOT() declaration:
    #             interface/efi/efidrvprefix.c
    #
    # That is iPXE's own bookkeeping rather than a signing requirement, and it
    # does not apply here in any case: this driver is dispatched from the DXE FV,
    # and FV contents are not subject to UEFI Secure Boot image verification.
    # The application still builds -sb, matching coreboot.
    oe_runmake -C ${S}/src \
        bin-x86_64-efi-sb/ipxe.efi \
        bin-x86_64-efi/intel.efidrv \
        CROSS_COMPILE="" \
        HOST_CC="${BUILD_CC}" \
        ASFLAGS="--64 --divide" \
        V=1

    [ -s "${S}/src/bin-x86_64-efi-sb/ipxe.efi" ] || \
        bbfatal "iPXE produced no bin-x86_64-efi-sb/ipxe.efi -- check the build log"

    [ -s "${S}/src/bin-x86_64-efi/intel.efidrv" ] || \
        bbfatal "iPXE produced no bin-x86_64-efi/intel.efidrv -- check the build log"

    # The application must still drive the LOM natively -- it is the boot option
    # PlatformBootManagerLib registers, and drivers/net/intel.c is what makes it
    # work: PCI_ROM(0x8086, 0x15a3, "i218v-3", ...) matches the 8086:15a3 on this
    # board. Checked against the link map because the driver set comes from
    # target names and generated .rom.defs rules, neither of which fails loudly
    # if upstream renames something.
    map="${S}/src/bin-x86_64-efi-sb/ipxe.efi.tmp.map"
    [ -r "$map" ] || bbfatal "iPXE link map missing at $map -- cannot verify the driver set"

    grep -qF "blib.a(intel.o)" "$map" || \
        bbfatal "iPXE did not link intel.o -- nothing would drive the onboard I218-V"

    # Same check for the driver, whose whole purpose is that NIC.
    drvmap="${S}/src/bin-x86_64-efi/intel.efidrv.tmp.map"
    [ -r "$drvmap" ] || bbfatal "iPXE driver link map missing at $drvmap"

    grep -qF "blib.a(intel.o)" "$drvmap" || \
        bbfatal "intel.efidrv did not link intel.o -- it would publish no SNP for the LOM"
}

do_deploy() {
    install -d ${DEPLOYDIR}
    # Deployed under coreboot's name for this artifact: its iPXE Makefile copies
    # whichever target it built to ipxe.rom, EFI or legacy option ROM alike, and
    # the edk2 Makefile consumes ipxe.rom. The contents here are a PE, and
    # edk2-uefipayload installs it as NetworkDrivers/ipxe.efi accordingly.
    install -m 0644 ${S}/src/bin-x86_64-efi-sb/ipxe.efi ${DEPLOYDIR}/ipxe.rom

    # The UNDI/SNP driver for the LOM. Kept under its own name -- coreboot has
    # no equivalent artifact, so there is no upstream convention to mirror.
    install -m 0644 ${S}/src/bin-x86_64-efi/intel.efidrv ${DEPLOYDIR}/ipxe-intel.efidrv
}

addtask deploy after do_compile

do_install[noexec] = "1"

# Sanity: this build is meaningless off its target machine.
COMPATIBLE_MACHINE = "nuc5i7ryh"
