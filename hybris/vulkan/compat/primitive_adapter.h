/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_PRIMITIVE_ADAPTER_H
#define HYBRIS_PRIMITIVE_ADAPTER_H
#include "scaled_vertex.h"
struct hybris_primitive_faces {
    VkCullModeFlags cull_mode;
    VkFrontFace front_face;
    int viewport_inverted;
};
/* Generate a triangle geometry stage from a selected, distance-extracted
 * vertex module. All output types/locations and vertex positions are preserved.
 * For edge output, distance_count may be zero: no cull varying is needed.
 * Unsupported interfaces fail closed. Edge output is not polygonMode LINE
 * emulation: dynamic raster state and polygon clipping remain separate work.
 * faces is optional and applies only to edges; viewport_inverted means the
 * viewport transform reverses orientation (negative width*height). */
VkResult hybris_spirv_primitive_adapter(const uint32_t *vertex, size_t bytes,
    uint32_t distance_location, uint32_t distance_count, int edges,
    const struct hybris_primitive_faces *faces,
    const VkAllocationCallbacks *allocator, uint32_t **output, size_t *output_bytes,
    const char **reason);
/* CullDistance removes a whole primitive only when all its vertices have a
 * negative value for the same plane. Native point/line/triangle assembly is
 * retained; this is not fragment clipping or a CPU readback path. */
VkResult hybris_spirv_primitive_adapter_topology(const uint32_t *vertex, size_t bytes,
    uint32_t distance_location, uint32_t distance_count, VkPrimitiveTopology topology,
    int edges, const struct hybris_primitive_faces *faces,
    const VkAllocationCallbacks *allocator, uint32_t **output, size_t *output_bytes,
    const char **reason);
#endif
