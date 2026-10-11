/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_PRIMITIVE_ADAPTER_H
#define HYBRIS_PRIMITIVE_ADAPTER_H
#include "scaled_vertex.h"
/* Generate a triangle geometry stage from a selected, distance-extracted
 * vertex module. All output types/locations and vertex positions are preserved.
 * Unsupported interfaces fail closed. Edge output is not polygonMode LINE
 * emulation: face culling and polygon clipping equivalence are separate work. */
VkResult hybris_spirv_primitive_adapter(const uint32_t *vertex, size_t bytes,
    uint32_t distance_location, uint32_t distance_count, int edges,
    const VkAllocationCallbacks *allocator, uint32_t **output, size_t *output_bytes,
    const char **reason);
#endif
