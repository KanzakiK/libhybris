#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=${OUT:?Set OUT to an isolated build directory}
mkdir -p "$out"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/command_state_check.c" "$repo/hybris/vulkan/compat/bc_command_state.c" \
    -pthread -o "$out/command-state-check"
"$out/command-state-check"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/polygon_layouts_check.c" "$repo/hybris/vulkan/compat/polygon_layouts.c" \
    -pthread -o "$out/polygon-layouts-check"
"$out/polygon-layouts-check"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/command_resources_check.c" "$repo/hybris/vulkan/layer/commands.c" \
    "$repo/hybris/vulkan/compat/rendering_segments.c" -pthread -o "$out/command-resources-check"
"$out/command-resources-check"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/storage_buffer_check.c" "$repo/hybris/vulkan/compat/memory_visibility.c" \
    -pthread -o "$out/storage-buffer-check"
"$out/storage-buffer-check"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/polygon_indices_check.c" "$repo/hybris/vulkan/compat/polygon_indices.c" \
    -o "$out/polygon-indices-check"
"$out/polygon-indices-check"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/polygon_raster_check.c" "$repo/hybris/vulkan/compat/polygon_raster.c" \
    -o "$out/polygon-raster-check"
"$out/polygon-raster-check"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/polygon_buffers_check.c" "$repo/hybris/vulkan/compat/polygon_buffers.c" \
    -o "$out/polygon-buffers-check"
"$out/polygon-buffers-check"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror -ffunction-sections -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/polygon_pipeline_lifetime_check.c" "$repo/hybris/vulkan/compat/polygon_pipeline.c" \
    -Wl,--gc-sections -pthread -o "$out/polygon-pipeline-lifetime-check"
"$out/polygon-pipeline-lifetime-check"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/rendering_scope_check.c" "$repo/hybris/vulkan/compat/rendering_segments.c" -o "$out/rendering-scope-check"
"$out/rendering-scope-check"
glslangValidator -V --target-env vulkan1.1 "$repo/hybris/vulkan/compat/shaders/polygon_indices.comp" -o "$out/polygon-indices.spv"
spirv-val --target-env vulkan1.1 "$out/polygon-indices.spv"
for stage in vert frag; do
    glslangValidator -V --target-env vulkan1.1 "$repo/tests/baseline/shaders/polygon-replay.$stage" -o "$out/polygon-replay.$stage.spv"
    spirv-val --target-env vulkan1.1 "$out/polygon-replay.$stage.spv"
done
glslangValidator -V --target-env vulkan1.1 -DRESOURCE_PRESENT=1 "$repo/tests/baseline/shaders/vertex-compute.vert" -o "$out/vertex-compute.vert.spv"
spirv-val --target-env vulkan1.1 "$out/vertex-compute.vert.spv"
"${HOST_CC:-cc}" -O2 -Wall -Wextra -I"$repo/hybris/vulkan/compat" \
    "$repo/tests/baseline/vertex_compute_check.c" "$repo/hybris/vulkan/compat/vertex_compute.c" \
    "$repo/hybris/vulkan/compat/spirv_entry.c" "$repo/hybris/vulkan/compat/spirv_decorations.c" \
    "$repo/hybris/vulkan/compat/spirv_builtins.c" -o "$out/vertex-compute-check"
"$out/vertex-compute-check" "$out/vertex-compute.vert.spv" "$out/vertex-compute.comp.spv"
spirv-val --target-env vulkan1.1 "$out/vertex-compute.comp.spv"
"$out/vertex-compute-check" "$out/vertex-compute.vert.spv" "$out/vertex-replay.vert.spv" 0 --replay
spirv-val --target-env vulkan1.1 "$out/vertex-replay.vert.spv"
glslangValidator -V --target-env vulkan1.3 "$repo/tests/baseline/shaders/vertex-compute.vert" -o "$out/vertex-modern.vert.spv"
"$out/vertex-compute-check" "$out/vertex-modern.vert.spv" "$out/vertex-modern.comp.spv"
spirv-val --target-env vulkan1.3 "$out/vertex-modern.comp.spv"
"$out/vertex-compute-check" "$out/vertex-modern.vert.spv" "$out/vertex-modern-replay.vert.spv" 0 --replay
spirv-val --target-env vulkan1.3 "$out/vertex-modern-replay.vert.spv"
glslangValidator -V --target-env vulkan1.3 -DPHYSICAL_MEMORY=1 "$repo/tests/baseline/shaders/vertex-compute.vert" -o "$out/vertex-physical.vert.spv"
"$out/vertex-compute-check" "$out/vertex-physical.vert.spv" "$out/vertex-physical.comp.spv"
spirv-val --target-env vulkan1.3 "$out/vertex-physical.comp.spv"
"$out/vertex-compute-check" "$out/vertex-physical.vert.spv" "$out/vertex-physical-replay.vert.spv" 0 --replay
spirv-val --target-env vulkan1.3 "$out/vertex-physical-replay.vert.spv"
for source in vertex-compute vertex-modern vertex-physical; do
    "$out/vertex-compute-check" "$out/$source.vert.spv" "$out/$source.fetch.spv" 0 --fetch
    spirv-val --target-env vulkan1.3 "$out/$source.fetch.spv"
done
"$out/vertex-compute-check" "$out/vertex-compute.vert.spv" --expect-unsupported 0 --fetch-unsupported
glslangValidator -V --target-env vulkan1.1 -DINPUT_INSTANCE=1 -DRESOURCE_PRESENT=1 "$repo/tests/baseline/shaders/vertex-compute.vert" -o "$out/vertex-instance.vert.spv"
"$out/vertex-compute-check" "$out/vertex-instance.vert.spv" "$out/vertex-instance.fetch.spv" 0 --fetch
spirv-val --target-env vulkan1.1 "$out/vertex-instance.fetch.spv"
"$out/vertex-compute-check" "$out/vertex-instance.vert.spv" "$out/vertex-instance.replay.spv" 0 --replay
spirv-val --target-env vulkan1.1 "$out/vertex-instance.replay.spv"
glslangValidator -V --target-env vulkan1.1 -DINPUT_VEC3=1 "$repo/tests/baseline/shaders/vertex-compute.vert" -o "$out/vertex-vec3.vert.spv"
"$out/vertex-compute-check" "$out/vertex-vec3.vert.spv" "$out/vertex-vec3.comp.spv"
spirv-val --target-env vulkan1.1 "$out/vertex-vec3.comp.spv"
glslangValidator -V --target-env vulkan1.1 -DFULL_BLOCK=1 "$repo/tests/baseline/shaders/vertex-compute.vert" -o "$out/vertex-full-block.spv"
for mode in compute replay; do
    args=(); [[ "$mode" == replay ]] && args=(0 --replay)
    "$out/vertex-compute-check" "$out/vertex-full-block.spv" "$out/vertex-full-block.$mode.spv" "${args[@]}"
    spirv-val --target-env vulkan1.1 "$out/vertex-full-block.$mode.spv"
done
for variant in INPUT_MATRIX RESOURCE_COLLISION EXTERNAL_STORE EXTERNAL_ATOMIC ACTIVE_CLIP; do
    glslangValidator -V --target-env vulkan1.1 -D"$variant"=1 "$repo/tests/baseline/shaders/vertex-compute.vert" -o "$out/vertex-$variant.spv"
    spirv-val --target-env vulkan1.1 "$out/vertex-$variant.spv"
    "$out/vertex-compute-check" "$out/vertex-$variant.spv" --expect-unsupported
done
python3 - "$out" <<'PY'
from pathlib import Path
import struct,sys
out=Path(sys.argv[1])
parts=[]
for name, path in [('polygon_indices','polygon-indices.spv'),('polygon_replay_vert','polygon-replay.vert.spv'),('polygon_replay_frag','polygon-replay.frag.spv'),('vertex_compute_source','vertex-compute.vert.spv'),('vertex_instance_source','vertex-instance.vert.spv')]:
    data=(out/path).read_bytes()
    words=struct.unpack('<'+'I'*(len(data)//4),data)
    parts.append('static const uint32_t '+name+'_spv[]={\n'+','.join(hex(x) for x in words)+'\n};\n')
(out/'polygon-indices.inc').write_text(''.join(parts))
PY
"${GLIBC_CC:-aarch64-linux-gnu-gcc}" -O2 -Wall -Wextra -ffunction-sections -I"$out" -I"$repo/tests/baseline" -I"$repo/hybris/vulkan/compat" \
  "$repo/tests/baseline/polygon-compute.c" "$repo/tests/baseline/probe_common.c" \
  "$repo/tests/baseline/probe_validation.c" "$repo/hybris/vulkan/compat/vertex_compute.c" \
  "$repo/hybris/vulkan/compat/spirv_entry.c" "$repo/hybris/vulkan/compat/spirv_decorations.c" \
  "$repo/hybris/vulkan/compat/spirv_builtins.c" "$repo/hybris/vulkan/compat/rendering_segments.c" \
  "$repo/hybris/vulkan/compat/vertex_resources.c" "$repo/hybris/vulkan/compat/polygon_pipeline.c" \
  "$repo/hybris/vulkan/compat/bc_command_state.c" "$repo/hybris/vulkan/compat/polygon_indices.c" \
  "$repo/hybris/vulkan/compat/polygon_bindings.c" \
  "$repo/hybris/vulkan/compat/polygon_raster.c" \
  "$repo/hybris/vulkan/compat/polygon_buffers.c" \
  -Wl,--gc-sections -pthread -ldl -o "$out/polygon-compute"
