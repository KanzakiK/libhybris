#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
OUT=${OUT:-"$ROOT/tests/demo/bc/build"}
CC=${CC:-x86_64-w64-mingw32-gcc}

mkdir -p "$OUT"
"$CC" -std=gnu11 -O2 -Wall -Wextra -Werror \
    "$ROOT/tests/demo/bc/d3d11_bc_demo.c" \
    -o "$OUT/d3d11_bc_demo.exe" \
    -ld3d11 -ld3dcompiler_47 -ldxgi
sha256sum "$OUT/d3d11_bc_demo.exe"
