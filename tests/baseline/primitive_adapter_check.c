/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include "clip_distance.h"
#include "primitive_adapter.h"
#include "shaders/primitive.inc"
int main(int argc, char **argv) {
    const int block = argc > 1 && strstr(argv[1], "block");
    const int edges = argc > 1 && strstr(argv[1], "lines");
    uint32_t *vertex = NULL, *geometry = NULL;
    size_t vertex_size = 0, geometry_size = 0;
    unsigned rewritten = 0;
    uint32_t location = 0, count = 0;
    const char *reason = NULL;
    const uint32_t *input = block ? primitive_cull_block_vertex_spv : primitive_cull_vertex_spv;
    size_t input_size = block ? sizeof(primitive_cull_block_vertex_spv) : sizeof(primitive_cull_vertex_spv);
    VkResult result = hybris_spirv_cull_plan(input, input_size, NULL, 0, &location, &count);
    if (result || count != 2) return 1;
    result = hybris_spirv_cull_vertex(input, input_size, location, NULL, &vertex, &vertex_size, &rewritten);
    if (result || !rewritten || !vertex) return 1;
    result = hybris_spirv_primitive_adapter(vertex, vertex_size, location, count, edges,
        NULL, &geometry, &geometry_size, &reason);
    if (result) { fprintf(stderr, "adapter result=%d reason=%s\n", result, reason); return 1; }
    uint32_t *invalid = NULL;
    size_t invalid_size = 0;
    const char *invalid_reason = NULL;
    if (hybris_spirv_primitive_adapter(vertex, vertex_size, 5, 2, edges,
        NULL, &invalid, &invalid_size, &invalid_reason) != VK_ERROR_FEATURE_NOT_PRESENT || invalid || invalid_size)
        return 1;
    if (hybris_spirv_primitive_adapter(vertex, vertex_size, location, 3, edges,
        NULL, &invalid, &invalid_size, &invalid_reason) != VK_ERROR_FEATURE_NOT_PRESENT || invalid || invalid_size)
        return 1;
    FILE *file = fopen(argc > 2 ? argv[2] : "/tmp/arlinux-adapter.spv", "wb");
    if (!file || fwrite(geometry, 1, geometry_size, file) != geometry_size) return 1;
    fclose(file); free(vertex); free(geometry);
    printf("PRIMITIVE_ADAPTER block=%d edges=%d bytes=%zu unsupported=ok\n", block, edges, geometry_size);
    return 0;
}
