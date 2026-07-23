SUMMARY = "EDK2 UefiPayloadPkg (MrChromebox fork) — UEFI payload for coreboot"
DESCRIPTION = "Builds UEFIPAYLOAD.fd, the UEFI environment coreboot jumps \
into on the NUC5i7RYH. Uses the MrChromebox edk2 fork -- the exact tree \
coreboot's own payloads/external/edk2 machinery defaults to \
(EDK2_REPO_MRCHROMEBOX, branch uefipayload_2605): unlike upstream tianocore \
it carries the coreboot integration patches that matter here -- the \
CFR-driven SetupMenu (surfaces the board port's fan-profile / \
power-on-after-AC / Turbo / SATA / fTPM options), the SMMSTORE variable \
driver wired as the EFI variable store, and the cbmem console. The build \
defines below mirror what coreboot's edk2 Makefile would emit for this \
board's Kconfig, tuned for Broadwell -- see the comments on each."
HOMEPAGE = "https://github.com/mrchromebox/edk2"
LICENSE = "BSD-2-Clause-Patent"
LIC_FILES_CHKSUM = "file://License.txt;md5=2b415520383f7964e96700ae12b4570a"

inherit deploy

# gitsm: edk2 vendors its deps as submodules (openssl for Secure Boot,
# brotli, oniguruma, ...) -- same fetcher approach as oe-core's ovmf.
SRC_URI = "gitsm://github.com/mrchromebox/edk2.git;protocol=https;branch=uefipayload_2605"
# Branch head as of 2026-07-13. coreboot master defaults to this branch
# (payloads/external/edk2/Kconfig: EDK2_TAG_OR_REV "origin/uefipayload_2605").
SRCREV = "2939f4969466bfe71722494e4cea5cbaa029c709"

PV = "2605+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "nuc5i7ryh"

# nasm: MdePkg/CryptoPkg X64 assembly. acpica: iasl for any .asl in the DXE
# set. util-linux: libuuid headers for BaseTools (coreboot's checktools
# verifies exactly these plus nasm).
DEPENDS = "nasm-native acpica-native util-linux-native"

# The payload is firmware, not target userspace; it embeds everything.
INHIBIT_DEFAULT_DEPS = "1"

do_configure[noexec] = "1"

# EDK2 build defines. This mirrors coreboot payloads/external/edk2/Makefile
# for the Kconfig this board would use, with each Broadwell-specific choice
# spelled out:
#   CPU_TIMER_LIB_ENABLE=FALSE  Broadwell has no CPUID leaf 15h crystal
#                               clock -- the TSC-frequency-from-CPUID timer
#                               lib (Skylake+) must stay off; the payload
#                               falls back to the 8254/HPET path (coreboot's
#                               EDK2_CPU_TIMER_LIB is likewise default n).
#   VARIABLE_SUPPORT=SMMSTORE   real, persistent EFI variables in the
#                               SMMSTORE(PRESERVE) 0x80000 FMAP region the
#                               board port lays out; needed for boot entries,
#                               Setup, and Secure Boot keys.
#   SECURE_BOOT_ENABLE=TRUE     coreboot defaults this on for the
#                               MrChromebox fork + SMMSTORE; ships in setup
#                               mode (no PK) until keys are enrolled, so it
#                               does not block unsigned OSes.
#   SERIAL off / CBMEM on       the NCT5577D routes no UART on this board
#                               (NO_UART_ON_SUPERIO); the firmware console
#                               goes to the cbmem ring instead ('cbmem -c'
#                               from the booted OS).
#   TPM_ENABLE=FALSE            the ME PTT fTPM's CRB lives at the
#                               non-standard 0xfed70000 -- edk2's TCG stack
#                               only probes 0xfed40000 and would find
#                               nothing; the OS gets the TPM through the
#                               board port's ACPI TPM2 table instead.
#   PCIE ECAM base/size         Broadwell northbridge: 0xf0000000, 64 buses.
#   PLATFORM_BOOT_TIMEOUT=3     headless box driven over the JetKVM -- give
#                               the operator a beat to hit Esc/F2.
#   BOOT_MANAGER_ESCAPE=TRUE    Esc works alongside F2 for Setup.
#   shell included              no SHELL_TYPE=NONE: keep the EFI shell as a
#                               rescue path (coreboot EDK2_HAVE_EFI_SHELL
#                               default y).
#   full-screen setup           ConOut PCDs at 0 = use the whole libgfxinit
#                               framebuffer (coreboot EDK2_FULL_SCREEN_SETUP
#                               default y).
#   NETWORK_ENABLE=TRUE         pulls in NetworkPkg -- SnpDxe/MnpDxe/ARP/
#                               Ip4/Udp4/Mtftp4/Dhcp4 and UefiPxeBcDxe (the
#                               PXE Base Code) for classic IPv4 TFTP netboot.
#                               SNP_ENABLE builds the generic SNP-over-UNDI
#                               shim so a NIC UNDI/option ROM is bridged to
#                               the SNP the PXE BC consumes. NB: this stack
#                               alone finds NO nic on this board -- the I218-V
#                               (8086:15a1) has no EDK2 driver, so no PXE boot
#                               option appears until a NIC driver is supplied
#                               (iPXE .efirom in coreboot CBFS, tracked
#                               separately). TLS + HTTP_BOOT stay off (they
#                               drag in OpenSSL, ~1 MB+, and PXE needs
#                               neither); flip both on later for HTTPS/HTTP
#                               boot -- that same TLS+HTTP path is what an EDK2
#                               Redfish RestEx client would ride on. IP6 and
#                               iSCSI off to keep the FV lean.
EDK2_BUILD_FLAGS = " \
    -D BOOTLOADER=COREBOOT \
    -D BUILD_ARCH=X64 \
    -D BOOT_MANAGER_ESCAPE=TRUE \
    -D PLATFORM_BOOT_TIMEOUT=3 \
    -D CPU_TIMER_LIB_ENABLE=FALSE \
    -D SERIAL_DRIVER_ENABLE=FALSE \
    -D DISABLE_SERIAL_TERMINAL=TRUE \
    -D USE_CBMEM_FOR_CONSOLE=TRUE \
    -D VARIABLE_SUPPORT=SMMSTORE \
    -D SECURE_BOOT_ENABLE=TRUE \
    -D TPM_ENABLE=FALSE \
    -D SD_MMC_TIMEOUT=10000 \
    -D NETWORK_ENABLE=TRUE \
    -D NETWORK_SNP_ENABLE=TRUE \
    -D NETWORK_IP4_ENABLE=TRUE \
    -D NETWORK_IP6_ENABLE=FALSE \
    -D NETWORK_TLS_ENABLE=FALSE \
    -D NETWORK_HTTP_BOOT_ENABLE=FALSE \
    -D NETWORK_ISCSI_ENABLE=FALSE \
    -D NETWORK_ALLOW_HTTP_CONNECTIONS=TRUE \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdMaxVariableSize=0x8000 \
    --pcd gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseAddress=0xF0000000 \
    --pcd gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseSize=0x4000000 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow=0 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn=0 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow=0 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn=0 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemId=COREv4 \
    --pcd gUefiCpuPkgTokenSpaceGuid.PcdFirstTimeWakeUpAPsBySipi=FALSE \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion=0x0300 \
    --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosDocRev=0x0 \
    "

# Extra flags hook (mirrors coreboot's EDK2_CUSTOM_BUILD_PARAMS).
EDK2_CUSTOM_BUILD_PARAMS ??= ""

do_compile() {
    cd ${S}

    # BaseTools are build-host tools; bitbake's exported cross CC must not
    # leak in (coreboot's Makefile does the same 'unset CC' dance). The
    # fallback 'cc' is not in bitbake's HOSTTOOLS, so name gcc/g++
    # explicitly (command-line vars also beat any CC= inside the makefiles).
    unset CC CXX CPP AS AR LD RANLIB STRIP OBJCOPY CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
    oe_runmake -C BaseTools CC=gcc CXX=g++

    # What edksetup.sh does, without needing to source bash into this task:
    # workspace env + the Conf/*.txt copied from the BaseTools templates.
    export WORKSPACE="${S}"
    export EDK_TOOLS_PATH="${S}/BaseTools"
    export CONF_PATH="${S}/Conf"
    export PYTHON_COMMAND="python3"
    export PATH="${S}/BaseTools/BinWrappers/PosixLike:$PATH"
    mkdir -p ${S}/Conf
    for t in build_rule tools_def target; do
        [ -e "${S}/Conf/$t.txt" ] || cp "${S}/BaseTools/Conf/$t.template" "${S}/Conf/$t.txt"
    done

    # Same invocation as coreboot's UefiPayloadPkg target: the -t GCC
    # toolchain resolves plain 'gcc' from PATH (the build host compiler).
    build -a IA32 -a X64 -b RELEASE -t GCC \
        -p UefiPayloadPkg/UefiPayloadPkg.dsc \
        -n ${@oe.utils.cpu_count()} \
        ${EDK2_BUILD_FLAGS} ${EDK2_CUSTOM_BUILD_PARAMS} \
        -y ${B}/UEFIPAYLOAD.report.txt

    [ -f ${S}/Build/UefiPayloadPkgX64/RELEASE_GCC/FV/UEFIPAYLOAD.fd ] || \
        bbfatal "edk2 build produced no UEFIPAYLOAD.fd -- see ${B}/UEFIPAYLOAD.report.txt"
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${S}/Build/UefiPayloadPkgX64/RELEASE_GCC/FV/UEFIPAYLOAD.fd \
        ${DEPLOYDIR}/UEFIPAYLOAD.fd
}

addtask deploy after do_compile

do_install[noexec] = "1"
