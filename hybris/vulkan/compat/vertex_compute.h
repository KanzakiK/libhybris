/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_VERTEX_COMPUTE_H
#define HYBRIS_VERTEX_COMPUTE_H
#include "scaled_vertex.h"
struct hybris_vertex_compute_field {
    uint32_t location, component, components, kind, offset; /* kind: float=0, uint=1, int=2 */
};
struct hybris_vertex_compute_layout {
    uint32_t input_count, output_count, input_stride, output_stride;
    struct hybris_vertex_compute_field inputs[64], outputs[65];
};
/* Feasibility lowering for a single-instance vertex producer. Scalar and
 * vector (2..4 component) 32-bit inputs/varyings are packed into 16-byte slots.
 * Output slot zero is Position; location inputs/varyings are sorted by Location.
 * Resources and
 * push constants are retained, but descriptor set 'set' must be unused.
 * The reserved set has binding 0 = input records, binding 1 = output records.
 * Returned layout is valid only on success. Dispatch X = vertex count,
 * Y = Z = 1. Unsupported interfaces return FEATURE_NOT_PRESENT, not a partial
 * module. Not a complete fillModeNonSolid implementation. */
VkResult hybris_spirv_vertex_compute(const uint32_t *code, size_t size, uint32_t set,
    struct hybris_vertex_compute_layout *layout,
    const VkAllocationCallbacks *allocator, uint32_t **output, size_t *output_size,
    const char **reason);
/* Fetch actual vertex buffers on the GPU, rather than host-expanded records.
 * Binding 2+i in the reserved descriptor set is a raw storage-buffer view of
 * the vertex binding used by layout.inputs[i]; the descriptor offset includes
 * vkCmdBindVertexBuffers' offset. Binding 1 is the output records buffer.
 * Binding 0 contains six uint32 draw parameters: firstVertex, firstIndex,
 * vertexOffset (signed bits), firstInstance, verticesPerInstance, indexWidth
 * (0 = non-indexed, 2 = uint16, 4 = uint32). Binding 2+input_count is the
 * index buffer, including vkCmdBindIndexBuffer's offset. Non-indexed draws
 * still require a valid dummy descriptor, but never load that buffer.
 * Dispatch X = verticesPerInstance * instanceCount, Y=Z=1. The caller must
 * validate nonzero counts, indexWidth, record capacity and address bounds.
 * Initially supports aligned R32 scalar/vector float/int attributes with
 * vertex or instance rates (divisor 1). Unsupported formats/extensions return
 * FEATURE_NOT_PRESENT. VertexIndex/InstanceIndex/BaseVertex/BaseInstance are
 * preserved. Output record indices are invocation ordinals, not source
 * vertex indices; polygon assembly must use the expanded ordinal stream.
 * Resources and push constants are retained. */
VkResult hybris_spirv_vertex_fetch_compute(const uint32_t *code, size_t size, uint32_t set,
    const VkPipelineVertexInputStateCreateInfo *vertex_input,
    struct hybris_vertex_compute_layout *layout,
    const VkAllocationCallbacks *allocator, uint32_t **output, size_t *output_size,
    const char **reason);
/* Generate the matching vertex replay stage. Preserve the application's
 * output variables/decorations while loading fields from binding 1 records.
 * Polygon points use a fixed point size of 1.0, not application PointSize.
 * The layout must match the compute producer; original vertex logic is not
 * executed a second time. Same interface limitations as the producer. */
VkResult hybris_spirv_vertex_replay(const uint32_t *code, size_t size, uint32_t set,
    struct hybris_vertex_compute_layout *layout,
    const VkAllocationCallbacks *allocator, uint32_t **output, size_t *output_size,
    const char **reason);
#endif
