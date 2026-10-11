#!/usr/bin/env bash
set -euo pipefail
checker=${CHECKER:?Set CHECKER to the native vertex-compute-check binary}
source=${SOURCE:?Set SOURCE to gfxrecon-extract output}
out=${OUT:?Set OUT to an isolated result directory}
mkdir -p "$out"
accepted=0
rejected=0
invalid=0
for shader in "$source"/*_1; do
    name=$(basename "$shader")
    if "$checker" "$shader" "$out/$name.comp.spv" 3 --fetch >"$out/$name.lower.log" 2>&1; then
        if "$checker" "$shader" "$out/$name.replay.spv" 3 --replay >"$out/$name.replay.log" 2>&1 &&
           spirv-val --target-env vulkan1.3 "$out/$name.comp.spv" >"$out/$name.validation.log" 2>&1 &&
           spirv-val --target-env vulkan1.3 "$out/$name.replay.spv" >>"$out/$name.validation.log" 2>&1; then
            accepted=$((accepted+1))
        else
            invalid=$((invalid+1))
            printf 'INVALID %s: ' "$name"
            head -2 "$out/$name.validation.log"
            cat "$out/$name.replay.log"
        fi
    else
        rejected=$((rejected+1))
        printf 'REJECT %s: ' "$name"
        cat "$out/$name.lower.log"
    fi
done
printf 'VERTEX_CORPUS accepted=%s rejected=%s invalid=%s\n' "$accepted" "$rejected" "$invalid"
test "$invalid" = 0
