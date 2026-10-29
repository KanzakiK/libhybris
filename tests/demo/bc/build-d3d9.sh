#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
OUT=${OUT:-"$ROOT/tests/demo/bc/build"}
CC=${CC:-x86_64-w64-mingw32-gcc}

mkdir -p "$OUT"
"$CC" -std=gnu11 -O2 -Wall -Wextra -Werror \
    "$ROOT/tests/demo/bc/d3d9_bc_demo.c" \
    -o "$OUT/d3d9_bc_demo.exe" \
    -ld3d9 -ld3dcompiler_47 -ldxguid
sha256sum "$OUT/d3d9_bc_demo.exe"
