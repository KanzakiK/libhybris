#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=${OUT:?Set OUT to an isolated build directory}
mkdir -p "$out"
compat="$repo/hybris/vulkan/compat"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror -ffunction-sections -I"$compat" \
    "$repo/tests/baseline/primitive_adapter_check.c" "$compat/primitive_adapter.c" \
    "$compat/spirv_clip.c" "$compat/spirv_builtins.c" "$compat/spirv_entry.c" \
    "$compat/spirv_decorations.c" -Wl,--gc-sections -o "$out/primitive-adapter-check"
for mode in triangles block point-input point-input-block line-input line-input-block lines lines-block; do
    "$out/primitive-adapter-check" "$mode" "$out/$mode.spv"
    spirv-val --target-env vulkan1.1 "$out/$mode.spv"
done
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror -I"$compat" -I"$compat/../layer" \
    "$repo/tests/baseline/cull-device-features-check.c" "$compat/../layer/device_features.c" \
    -o "$out/cull-device-features-check"
"$out/cull-device-features-check"
