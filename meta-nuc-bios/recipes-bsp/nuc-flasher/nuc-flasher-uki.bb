SUMMARY = "Self-flashing UEFI unified kernel image for the NUC BIOS"
DESCRIPTION = "Builds nuc-flasher-uki.efi: a stock Alpine -lts kernel + a \
               bundled static busybox + flashrom + THIS build's coreboot \
               ROM, wrapped as a single EFI unified kernel image (UKI) with \
               systemd-boot's linuxx64.efi.stub. Booted on the NUC (JetKVM \
               virtual media or a USB stick, UEFI/x64, via \
               nuc-flasher-image), its init verifies the BIOS region and: \
\
                 * already this ROM      -> reboots (so leaving it attached \
                                            never loops) \
                 * differs               -> flashes the BIOS region only, \
                                            then reboots \
                 * write fails           -> drops to a shell and does NOT \
                                            reboot \
\
               Internal flashing needs no SOIC clip because the running \
               coreboot leaves the SPI unlocked (BOOTMEDIA_LOCK_NONE). A \
               machine still on the stock Intel BIOS (SMM_BWP set) cannot \
               self-flash -- use scripts/nuc-spi.sh with the clip once. \
\
               A stock kernel/initramfs + apks are downloaded rather than a \
               kernel built: the flasher needs a generic x86_64 UEFI kernel \
               with USB/NVMe/AHCI, which Alpine's -lts already is. \
               Replaces the fetch/UKI half of scripts/make-flasher-img.sh."

BUGTRACKER = "https://github.com/pi-bmc/nuc-bios-build/issues"
SECTION = "firmware"

# The UKI aggregates Alpine binaries: Linux, busybox, flashrom and pciutils
# (GPL-2.0), musl (MIT), libusb and libftdi1 (LGPL-2.1), plus the systemd
# EFI stub (LGPL-2.1-or-later).
LICENSE = "GPL-2.0-only & LGPL-2.1-only & MIT"
LIC_FILES_CHKSUM = "\
    file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6 \
    file://${COMMON_LICENSE_DIR}/LGPL-2.1-only;md5=1a6d268fd218675ffea8be556788b780 \
    file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302 \
"

# Alpine pin. The netboot-<version> directories are frozen per point release,
# so the kernel/initramfs URLs and checksums stay valid when v3.22 moves on.
# The apk repositories are NOT versioned that way -- a package rebuild
# REPLACES the old .apk on the mirror, so do_fetch 404s on the next Alpine
# package bump. Bump ALPINE_SNAPSHOT, the apk list and all checksums as a
# set.
ALPINE_REL = "v3.22"
ALPINE_SNAPSHOT = "3.22.5"
ALPINE_CDN = "https://dl-cdn.alpinelinux.org/alpine/${ALPINE_REL}"

# Pinned apks (x86_64, ${ALPINE_REL}); the flashrom dependency closure is all
# musl, so no other libs are needed.
FLASHER_APKS = "\
    musl-1.2.5-r12 \
    pciutils-libs-3.13.0-r1 \
    libusb-1.0.28-r0 \
    busybox-static-1.37.0-r20 \
    flashrom-1.5.1-r0 \
    libftdi1-1.5-r4 \
"

# systemd-boot's EFI stub, as a pinned Debian systemd-boot-efi artifact
# rather than the systemd-boot target recipe: building that recipe here is
# impossible -- its util-linux dependency drags a runtime graph naming
# kernel-module-* providers, which nothing on this kernel-less machine can
# RPROVIDE. snapshot.debian.org file URLs are content-addressed (SHA-1) and
# permanent, unlike pool URLs, which are purged at the next point release;
# this one is systemd-boot-efi_257.13-1~deb13u1_amd64.deb. The stub's PE
# image base (0x14df90000) is a fixed systemd linker constant -- the same in
# Debian 257 and Arch 261 -- and the VMA pins below were verified against
# this exact binary; do_compile rechecks them.
STUB_DEB = "systemd-boot-efi_257.13-1~deb13u1_amd64.deb"
STUB_SHA1 = "c3f9e935e382dc5d6ab1db2a61e1b0ab94fed357"

# .apk is not a suffix the fetcher unpacks, so each lands in WORKDIR as a
# plain file; the stub .deb IS unpacked (to usr/lib/systemd/boot/efi/); the
# kernel/initramfs get snapshot-qualified names because the upstream
# basenames (vmlinuz-lts) are pin-ambiguous in a shared DL_DIR.
SRC_URI = "\
    https://snapshot.debian.org/file/${STUB_SHA1};name=stub;downloadfilename=${STUB_DEB} \
    ${ALPINE_CDN}/releases/x86_64/netboot-${ALPINE_SNAPSHOT}/vmlinuz-lts;name=vmlinuz;downloadfilename=alpine-${ALPINE_SNAPSHOT}-vmlinuz-lts \
    ${ALPINE_CDN}/releases/x86_64/netboot-${ALPINE_SNAPSHOT}/initramfs-lts;name=initramfs;downloadfilename=alpine-${ALPINE_SNAPSHOT}-initramfs-lts \
    ${ALPINE_CDN}/main/x86_64/musl-1.2.5-r12.apk;name=musl \
    ${ALPINE_CDN}/main/x86_64/pciutils-libs-3.13.0-r1.apk;name=pciutils-libs \
    ${ALPINE_CDN}/main/x86_64/libusb-1.0.28-r0.apk;name=libusb \
    ${ALPINE_CDN}/main/x86_64/busybox-static-1.37.0-r20.apk;name=busybox-static \
    ${ALPINE_CDN}/community/x86_64/flashrom-1.5.1-r0.apk;name=flashrom \
    ${ALPINE_CDN}/community/x86_64/libftdi1-1.5-r4.apk;name=libftdi1 \
    file://flasher-init.sh \
"
SRC_URI[stub.sha256sum] = "390ecdcef9bbb753f51bb6d8f696bdae2f1dbedde7239c49ccb2512f137a8933"
SRC_URI[vmlinuz.sha256sum] = "8e46d2e89d66850da8067b85266823483c47d73acf0a6b8820cfe2b92385fb7e"
SRC_URI[initramfs.sha256sum] = "35e02daafbdb2a82f9ee106f90c2b6a013f76c3c47dc9b3edf1fd762449883fc"
SRC_URI[musl.sha256sum] = "4990a5e0ba312e478f94cfe431a70efef1538004eb361c8ae424516848be45bb"
SRC_URI[pciutils-libs.sha256sum] = "93268cc527173599275862094aac127cbf49851acfd5cc9ddfae0e959c966250"
SRC_URI[libusb.sha256sum] = "be4856c1f050b093a21da276271a49c4153726c90de1e67ae85ea978e2577288"
SRC_URI[busybox-static.sha256sum] = "488ad6efd04b5a722719e79f8e0dcc2c24afd6758867af3ce41b04839e60c74b"
SRC_URI[flashrom.sha256sum] = "1ab4c710e874744b4bf168bc22f45fa16df28f34ce2061e0c953b08158f1f3b0"
SRC_URI[libftdi1.sha256sum] = "711de5e8c0677d5e72ace639bba9b887a673a6dd6a7d4a7fc89f8d1331a4eab2"

S = "${WORKDIR}"

inherit deploy nopackages

# Nothing here compiles against the target sysroot. The one toolchain use is
# the cross objcopy assembling the UKI's PE sections; BFD's default vector
# list for an x86_64-linux target includes pei-x86-64, so the ordinary cross
# binutils handles the stub.
INHIBIT_DEFAULT_DEPS = "1"
DEPENDS = "virtual/${TARGET_PREFIX}binutils"

do_configure[noexec] = "1"

# The systemd stub's PE image base is ~0x14df90000 with sections up to
# ~0x14dfb4000; the payload sections must sit ABOVE that or objcopy places
# them "below image base" and the firmware refuses to load the UKI.
VMA_CMDLINE = "0x14dfb5000"
VMA_LINUX = "0x14e000000"
VMA_INITRD = "0x150000000"

# The ROM travels through DEPLOY_DIR_IMAGE with an explicit [depends] (the
# repo idiom for finished artifacts, see the coreboot recipe).
do_compile[depends] += "coreboot:do_deploy"
do_compile () {
    rom="${DEPLOY_DIR_IMAGE}/coreboot-nuc5i7ryh.rom"
    stub="${WORKDIR}/usr/lib/systemd/boot/efi/linuxx64.efi.stub"

    if [ ! -f "${rom}" ]; then
        bbfatal "No coreboot-nuc5i7ryh.rom in ${DEPLOY_DIR_IMAGE}: rebuild coreboot (its do_deploy ships it)."
    fi
    # A blob-less compile check links a ROM that cannot boot; a flasher
    # carrying one must fail here, loudly, not brick-by-flashing a dud.
    if [ -f "${rom}.NOT-BOOTABLE" ]; then
        bbfatal "${rom} was built without mrc.bin/refcode.elf -- compile check only, nothing bootable to flash."
    fi
    rom_bytes=$(stat -c %s "${rom}")
    if [ "${rom_bytes}" != "8388608" ]; then
        bbfatal "${rom} is ${rom_bytes} bytes, expected 8388608 -- not a full-chip image."
    fi
    if [ ! -f "${stub}" ]; then
        bbfatal "No linuxx64.efi.stub under ${WORKDIR}/usr -- did ${STUB_DEB} unpack?"
    fi

    # The VMA pins must sit above every stub section or objcopy places the
    # payload sections "below image base" and the firmware refuses to load
    # the UKI. Verified against the pinned stub when it was pinned; recheck
    # so a stub bump cannot silently break the layout. The comparison lives
    # in gawk because bitbake's shell parser rejects $(( )) arithmetic.
    stub_fit=$(${OBJDUMP} -h "${stub}" | \
        gawk -v limit="${VMA_CMDLINE}" '$1 ~ /^[0-9]+$/ { e = strtonum("0x" $4) + strtonum("0x" $3); if (e > m) m = e } END { if (m < strtonum(limit)) print "ok"; else printf "end=0x%x", m }')
    if [ "${stub_fit}" != "ok" ]; then
        bbfatal "Stub sections (${stub_fit}) reach VMA_CMDLINE ${VMA_CMDLINE} -- raise the VMA pins for this stub."
    fi

    # Build from scratch: bitbake re-runs do_compile in place, and a stale
    # overlay would silently ride along.
    work="${B}/flasher"
    overlay="${work}/rootfs"
    rm -rf "${work}"
    install -d "${overlay}"

    # Unpack the flasher userland into the overlay. Each .apk is a set of
    # concatenated gzip streams (signature, control, data); GNU tar extracts
    # the data segment and then complains about the leading streams, hence
    # the tolerated failure -- the explicit file checks below are the real
    # gate.
    for a in ${FLASHER_APKS}; do
        tar -xzf "${WORKDIR}/${a}.apk" -C "${overlay}" 2>/dev/null || true
    done
    rm -f "${overlay}/.PKGINFO" "${overlay}"/.SIGN.* 2>/dev/null || true
    [ -f "${overlay}/usr/sbin/flashrom" ] || \
        bbfatal "flashrom missing from overlay after apk unpack"
    [ -f "${overlay}/bin/busybox.static" ] || \
        bbfatal "busybox-static missing from overlay after apk unpack"

    # /bbin/busybox: the init runs everything through the bundled static
    # busybox, so the base initramfs applet set is never relied on.
    install -d "${overlay}/bbin"
    mv "${overlay}/bin/busybox.static" "${overlay}/bbin/busybox"
    chmod 755 "${overlay}/bbin/busybox"

    # THIS build's ROM plus its checksum, which the init prints on boot.
    install -d "${overlay}/rom"
    install -m 0644 "${rom}" "${overlay}/rom/coreboot-nuc5i7ryh.rom"
    sha256sum "${overlay}/rom/coreboot-nuc5i7ryh.rom" | cut -d' ' -f1 \
        > "${overlay}/rom/coreboot-nuc5i7ryh.rom.sha256"

    install -m 0755 "${WORKDIR}/flasher-init.sh" "${overlay}/init"

    # Concatenated cpios: the kernel extracts each in turn, later entries
    # winning, so the overlay's /init replaces Alpine's. Mixed compression
    # is fine.
    (cd "${overlay}" && find . | LC_ALL=C sort | cpio -o -H newc --quiet | gzip -9) \
        > "${work}/overlay.cpio.gz"
    cat "${WORKDIR}/alpine-${ALPINE_SNAPSHOT}-initramfs-lts" "${work}/overlay.cpio.gz" \
        > "${work}/initrd.img"

    # tty0 is the only console an operator sees (the HDMI framebuffer the
    # JetKVM captures; the node exposes no serial). iomem=relaxed lets
    # flashrom's internal programmer map the SPI controller.
    printf 'console=tty0 iomem=relaxed' > "${work}/cmdline.txt"

    ${OBJCOPY} \
        --add-section .cmdline="${work}/cmdline.txt" --change-section-vma .cmdline=${VMA_CMDLINE} \
        --add-section .linux="${WORKDIR}/alpine-${ALPINE_SNAPSHOT}-vmlinuz-lts" --change-section-vma .linux=${VMA_LINUX} \
        --add-section .initrd="${work}/initrd.img" --change-section-vma .initrd=${VMA_INITRD} \
        "${stub}" "${work}/nuc-flasher-uki.efi"
}

do_deploy () {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/flasher/nuc-flasher-uki.efi ${DEPLOYDIR}/nuc-flasher-uki.efi
}
addtask deploy after do_compile

# Sanity: this build is meaningless off its target machine.
COMPATIBLE_MACHINE = "nuc5i7ryh"
