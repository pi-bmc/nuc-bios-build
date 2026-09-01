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

SRC_URI = "${COREBOOT_GIT_URI} \
           https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz;name=gmp;unpack=0 \
           https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz;name=mpfr;unpack=0 \
           https://ftp.gnu.org/gnu/mpc/mpc-1.4.1.tar.xz;name=mpc;unpack=0 \
           https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.xz;name=gcc;unpack=0 \
           https://ftp.gnu.org/gnu/binutils/binutils-2.46.1.tar.xz;name=binutils;unpack=0 \
           "

SRC_URI[gmp.sha256sum]      = "a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"
SRC_URI[mpfr.sha256sum]     = "b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01"
SRC_URI[mpc.sha256sum]      = "91204cd32f164bd3b7c992d4a6a8ce6519511aadab30f78b6982d0bf8d73e931"
SRC_URI[gcc.sha256sum]      = "438fd996826b0c82485a29da03a72d71d6e3541a83ec702df4271f6fe025d24e"
SRC_URI[binutils.sha256sum] = "e127a709cba24c76de8936cb7083dd768f28cd37eb010492e2f19b71eb1294e4"

S = "${WORKDIR}/git"

inherit native

# buildgcc's preflight checks demand these on PATH (gcc/g++/make/tar/gzip
# come from HOSTTOOLS; these do not).
DEPENDS = "bison-native flex-native m4-native xz-native"

# The crossgcc-i386 source tarballs come through the Yocto fetcher (SRC_URI
# above: DL_DIR-cached, sha256-checked, MIRRORS-capable) and are staged into
# util/crossgcc/tarballs, where buildgcc verifies them against its own sha1
# sums (util/crossgcc/sum/) and skips its downloads. buildgcc's own fetch
# used to be the mechanism here, and broke the day ftpmirror.gnu.org's
# redirector went flaky mid-download (gmp, 2026-08-31); the versions are
# pinned by the coreboot revision, so keep this list in step with
# util/crossgcc/buildgcc's *_VERSION pins when COREBOOT_GIT_URI moves.
# The network flag stays as a fail-open escape hatch for any archive this
# list misses.
do_compile[network] = "1"

do_configure() {
    install -d ${S}/util/crossgcc/tarballs
    for t in gmp-6.3.0.tar.xz mpfr-4.2.2.tar.xz mpc-1.4.1.tar.xz \
             gcc-15.2.0.tar.xz binutils-2.46.1.tar.xz; do
        install -m 0644 ${WORKDIR}/$t ${S}/util/crossgcc/tarballs/$t
    done
}

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
