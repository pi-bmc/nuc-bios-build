#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${HERE}/../../../meta-nuc-bios/recipes-bsp/edk2/files/NucRedfishPkg/Library/NucCapsuleOnDiskLib"
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I"${HERE}/stubs" -I"${SRC}" \
    -o "${OUT}/test-parse" "${HERE}/test-parse.c" "${SRC}/NucCapsuleParse.c"
"${OUT}/test-parse"
