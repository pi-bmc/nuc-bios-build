SUMMARY = "coreboot crossgcc toolchain (i386-elf xgcc) as a cached native dep"
DESCRIPTION = "Builds coreboot's pinned i386-elf cross toolchain (binutils, \
GCC, GNAT for libgfxinit) once, as a normal native recipe: sstate-cached and \
staged into consumers' native sysroot, so editing the coreboot recipe no \
longer re-runs the ~30-minute crossgcc bootstrap. The coreboot recipe points \
its build at the staged copy via XGCCPATH. coreboot's xgcc is relocatable \
(the project ships it as the movable coreboot-sdk; the driver finds cc1/as \
relative to its own path), which is what makes staging it viable."
HOMEPAGE = "https://doc.coreboot.org/tutorial/part1.html"

require coreboot-source.inc

SRC_URI = "${COREBOOT_GIT_URI}"

S = "${WORKDIR}/git"

inherit native

# buildgcc's preflight checks demand these on PATH (gcc/g++/make/tar/gzip
# come from HOSTTOOLS; these do not).
DEPENDS = "bison-native flex-native m4-native xz-native"

# buildgcc downloads the gcc/binutils/gmp/... source tarballs itself into
# util/crossgcc/tarballs (checksummed against util/crossgcc/sum/). Keeping
# that mechanism (instead of mirroring every tarball in SRC_URI) follows the
# upstream-supported path; same network-in-compile precedent as the rest of
# this layer.
do_compile[network] = "1"

do_configure[noexec] = "1"

do_compile() {
    # Host tools: keep bitbake's compiler env out of buildgcc's probes (its
    # zlib/CC checks fail against a recipe sysroot). libgfxinit needs a host
    # Ada compiler -- install your distro's gcc-ada/gnat package.
    unset CC CXX CPP AS AR LD RANLIB STRIP OBJCOPY NM CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
    oe_runmake crossgcc-i386 CPUS=${@oe.utils.cpu_count()}
}

# Install under ${datadir}: OE only stages SYSROOT_DIRS (bindir/libdir/
# datadir/...) into consumers, so a sibling like ${prefix}/coreboot-xgcc
# would build into the image but never reach coreboot's recipe-sysroot-native.
do_install() {
    install -d ${D}${datadir}/coreboot-xgcc
    cp -a ${S}/util/crossgcc/xgcc/. ${D}${datadir}/coreboot-xgcc/
    chmod -R u+w ${D}${datadir}/coreboot-xgcc

    # gmp/mpfr/mpc leave pkg-config/libtool files that embed the build
    # tmpdir; coreboot never pkg-configs the cross toolchain, so drop them
    # (they otherwise trip the pkgconfig/buildpaths sysroot QA).
    find ${D}${datadir}/coreboot-xgcc -name '*.pc' -delete
    find ${D}${datadir}/coreboot-xgcc -name '*.la' -delete
    find ${D}${datadir}/coreboot-xgcc -depth -type d -name pkgconfig -empty -delete 2>/dev/null || true
}

# Freshly built host binaries with their own layout; leave them alone.
# buildpaths: the xgcc's gcc specs/driver embed the build-time prefix as a
# fallback -- the coreboot SDK toolchain is relocatable (finds cc1/as
# relative to argv[0]) so those paths are never used at runtime; the check
# does not apply. The runtime-path (tmpdir) scan is likewise moot here.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} += "already-stripped ldflags file-rdeps staticdev buildpaths"
