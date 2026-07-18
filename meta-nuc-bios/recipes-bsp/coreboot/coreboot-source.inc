# Shared coreboot source pin for coreboot_git.bb (the ROM build) and
# coreboot-toolchain-native (the crossgcc/xgcc bootstrap). One clone in
# DL_DIR serves both.
#
# Fetched from the GitHub mirror: full clones off review.coreboot.org
# routinely stall/drop (SIGPIPE mid-clone); the mirror serves the same
# history, and the org also mirrors the submodule repos (coreboot's
# .gitmodules URLs are origin-relative).
COREBOOT_GIT_URI = "git://github.com/coreboot/coreboot.git;protocol=https;branch=main"
# coreboot master, pinned 2026-07-16.
SRCREV = "149d1494fa4db2b08e7a7a6f7bbf7c7d2e8e18ad"

LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=751419260aa954499f7abaabaa882bbe"
