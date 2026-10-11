/* SPDX-License-Identifier: Apache-2.0 */
#include "primitive_adapter.h"
#include <stdarg.h>
#include <string.h>

struct id { size_t at; uint32_t op, type, input, input_ptr, value_ptr, read_ptr, write_ptr, in_type; unsigned needed; };
struct module { uint32_t *w; size_t n, cap; uint32_t next; const VkAllocationCallbacks *alloc; };
static int reserve(struct module *m, size_t count)
{
    if (count > SIZE_MAX - m->n) return 0;
    if (m->n + count <= m->cap) return 1;
    size_t cap = m->cap ? m->cap : 256;
    while (cap < m->n + count) { if (cap > SIZE_MAX / 8) return 0; cap *= 2; }
    uint32_t *w = hybris_scaled_alloc(m->alloc, cap * 4, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    if (!w) return 0;
    if (m->w) memcpy(w, m->w, m->n * 4);
    hybris_scaled_free(m->alloc, m->w); m->w = w; m->cap = cap;
    return 1;
}
static int words(struct module *m, const uint32_t *p, size_t n)
{ if (!reserve(m, n)) return 0; memcpy(m->w + m->n, p, n * 4); m->n += n; return 1; }
static int ins(struct module *m, unsigned op, unsigned count, ...)
{
    if (!reserve(m, count)) return 0;
    m->w[m->n++] = (count << 16) | op;
    va_list args; va_start(args, count);
    for (unsigned i = 1; i < count; ++i) m->w[m->n++] = va_arg(args, uint32_t);
    va_end(args); return 1;
}
/* Only value types are cloned. Resources, pointers nested in interface values,
 * runtime arrays and specialization-dependent array lengths need separate work. */
static int needed(const uint32_t *w, struct id *ids, uint32_t bound, uint32_t id, unsigned depth)
{
    if (!id || id >= bound || !ids[id].at || depth > 64) return 0;
    if (ids[id].needed) return 1;
    const uint32_t *p = w + ids[id].at;
    unsigned n = p[0] >> 16, op = ids[id].op;
    ids[id].needed = 1;
    switch (op) {
    case 20: return n == 2;
    case 21: return n == 4;
    case 22: return n == 3;
    case 23: case 24: return n == 4 && needed(w, ids, bound, p[2], depth + 1);
    case 28: return n == 4 && p[3] < bound && ids[p[3]].op == 43 &&
        needed(w, ids, bound, p[2], depth + 1) && needed(w, ids, bound, p[3], depth + 1);
    case 30:
        for (unsigned i = 2; i < n; ++i) if (!needed(w, ids, bound, p[i], depth + 1)) return 0;
        return 1;
    case 32: return n == 4 && p[2] == 3 && needed(w, ids, bound, p[3], depth + 1);
    case 43: return n >= 4 && needed(w, ids, bound, p[1], depth + 1);
    default: return 0;
    }
}

/* Some drivers cannot lower whole gl_PerVertex block loads/stores in geometry
 * shaders. Walk aggregates and copy their scalar/vector leaves instead. */
static int pass_value(struct module *m, const uint32_t *w, struct id *ids,
    uint32_t type, uint32_t in, uint32_t out, const uint32_t constants[64],
    uint32_t path[65], unsigned depth)
{
    if (depth >= 64) return 0;
    const uint32_t *p = w + ids[type].at;
    unsigned count = 0;
    if (ids[type].op == 30) count = (p[0] >> 16) - 2;
    if (ids[type].op == 24) count = p[3];
    if (ids[type].op == 28) count = w[ids[p[3]].at + 3];
    if (count) {
        if (count > 64) return 0;
        for (unsigned i = 0; i < count; ++i) {
            path[depth + 1] = constants[i];
            uint32_t child = ids[type].op == 30 ? p[i + 2] : p[2];
            if (!pass_value(m, w, ids, child, in, out, constants, path, depth + 1)) return 0;
        }
        return 1;
    }
    uint32_t ptr = m->next++, value = m->next++, chain[70];
    unsigned n = 5 + depth;
    chain[0] = (n << 16) | 65; chain[1] = ids[type].read_ptr; chain[2] = ptr; chain[3] = in;
    memcpy(chain + 4, path, (depth + 1) * 4);
    if (!words(m, chain, n) || !ins(m, 61, 4, type, value, ptr)) return 0;
    uint32_t target = out;
    if (depth) {
        target = m->next++;
        n = 4 + depth;
        chain[0] = (n << 16) | 65; chain[1] = ids[type].write_ptr; chain[2] = target; chain[3] = out;
        memcpy(chain + 4, path + 1, depth * 4);
        if (!words(m, chain, n)) return 0;
    }
    return ins(m, 62, 3, target, value);
}

VkResult hybris_spirv_primitive_adapter_topology(const uint32_t *v, size_t bytes,
    uint32_t location, uint32_t distances, VkPrimitiveTopology topology, int edges,
    const struct hybris_primitive_faces *faces, const VkAllocationCallbacks *alloc,
    uint32_t **output, size_t *output_bytes, const char **reason)
{
    *output = NULL; *output_bytes = 0;
    *reason = "unsupported primitive-adapter vertex interface";
    uint32_t vertices, input_mode, output_mode;
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
        vertices = 1; input_mode = 19; output_mode = 27; break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
        vertices = 2; input_mode = 20; output_mode = 28; break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
        vertices = 3; input_mode = 22; output_mode = 29; break;
    default: return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (edges && vertices != 3) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (faces && (!edges || faces->cull_mode > VK_CULL_MODE_FRONT_AND_BACK ||
        (faces->front_face != VK_FRONT_FACE_COUNTER_CLOCKWISE &&
         faces->front_face != VK_FRONT_FACE_CLOCKWISE))) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (!v || bytes < 20 || bytes % 4 || v[0] != 0x07230203 || !v[3] ||
        v[3] > 1000000 || (!distances && !edges) || distances > 8) return VK_ERROR_FEATURE_NOT_PRESENT;
    size_t total = bytes / 4;
    uint32_t bound = v[3], outputs[64], count = 0, distance_var = 0;
    uint32_t position_var = 0, position_member = UINT32_MAX;
    struct id *ids = hybris_scaled_alloc(alloc, bound * sizeof(*ids), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    if (!ids) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(ids, 0, bound * sizeof(*ids));
    struct module m = {.next = bound, .alloc = alloc};
    VkResult status = VK_ERROR_FEATURE_NOT_PRESENT;
    unsigned entries = 0;
    for (size_t at = 5; at < total;) {
        const uint32_t *p = v + at;
        unsigned n = p[0] >> 16, op = p[0] & 65535;
        if (!n || n > total - at) goto done;
        if (op == 14 && (n != 3 || p[1] != 0 || p[2] != 1)) goto done;
        if (op == 15 && (n < 4 || p[1] != 0 || ++entries != 1)) goto done;
        if (op == 73 || op == 74 || op == 75 || op == 332) goto done;
        if (op >= 19 && op <= 32) {
            if (n < 2 || !p[1] || p[1] >= bound) goto done;
            ids[p[1]] = (struct id){.at = at, .op = op};
        }
        if (op == 43 || op == 59) {
            if (n < 4 || !p[2] || p[2] >= bound || p[1] >= bound) goto done;
            ids[p[2]] = (struct id){.at = at, .op = op, .type = p[1]};
            if (op == 59 && p[3] == 3) {
                if (n != 4 || count == 64) goto done;
                outputs[count++] = p[2];
            }
        }
        at += n;
    }
    if (!entries || !count) goto done;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t var = outputs[i];
        if (ids[ids[var].type].op != 32 || !needed(v, ids, bound, ids[var].type, 0)) goto done;
        ids[var].needed = 1;
        ids[var].input = m.next++; ids[var].input_ptr = m.next++; ids[var].value_ptr = m.next++;
    }
    for (size_t at = 5; at < total; at += v[at] >> 16) {
        const uint32_t *p = v + at;
        unsigned n = p[0] >> 16, op = p[0] & 65535;
        if (op == 71 && n >= 3 && p[1] < bound && ids[p[1]].needed) {
            /* Reject transform feedback, streams, per-view/per-primitive and
             * other decorations whose primitive-stage meaning is not handled. */
            if (p[2] != 2 && p[2] != 11 && p[2] != 13 && p[2] != 14 && p[2] != 16 &&
                p[2] != 17 && p[2] != 30 && p[2] != 31 && p[2] != 32) goto done;
            if (p[2] == 11 && (n != 4 || p[3] > 3 || p[3] == 2)) goto done;
            if (p[2] == 11 && p[3] == 0 && ids[p[1]].op == 59) {
                if (position_var) goto done;
                position_var = p[1];
            }
            if (distances && p[2] == 30 && n == 4 && p[3] == location && ids[p[1]].op == 59) {
                if (distance_var) goto done;
                distance_var = p[1];
            }
        }
        if (op == 72 && n >= 4 && p[1] < bound && ids[p[1]].needed) {
            if (p[3] != 11 && p[3] != 13 && p[3] != 14 && p[3] != 16 && p[3] != 17 && p[3] != 30 && p[3] != 31) goto done;
            if (p[3] == 11 && (n != 5 || p[4] > 3 || p[4] == 2)) goto done;
            if (p[3] == 11 && p[4] == 0) {
                for (uint32_t i = 0; i < count; ++i)
                    if (v[ids[ids[outputs[i]].type].at + 3] == p[1]) {
                        if (position_var) goto done;
                        position_var = outputs[i]; position_member = p[2];
                    }
            }
        }
    }
    uint32_t float_type = 0;
    if (distances) {
        if (!distance_var) goto done;
        uint32_t array = v[ids[ids[distance_var].type].at + 3];
        if (array >= bound || ids[array].op != 28) goto done;
        const uint32_t *arr = v + ids[array].at;
        uint32_t length = arr[3];
        float_type = arr[2];
        if (float_type >= bound || ids[float_type].op != 22 || v[ids[float_type].at + 2] != 32 ||
            length >= bound || ids[length].op != 43 || (v[ids[length].at] >> 16) != 4 ||
            v[ids[length].at + 3] != distances) goto done;
    } else {
        /* Edge output must also work for ordinary vertex shaders, without
         * manufacturing a CullDistance varying or enabling that feature. */
        for (uint32_t type = 1; type < bound; ++type)
            if (ids[type].needed && ids[type].op == 22 && v[ids[type].at + 2] == 32) {
                float_type = type; break;
            }
        if (!float_type) goto done;
    }
    if (faces && faces->cull_mode && faces->cull_mode != VK_CULL_MODE_FRONT_AND_BACK) {
        if (!position_var) goto done;
        uint32_t type = v[ids[ids[position_var].type].at + 3];
        if (position_member != UINT32_MAX) {
            if (ids[type].op != 30 || position_member >= (v[ids[type].at] >> 16) - 2) goto done;
            type = v[ids[type].at + 2 + position_member];
        }
        if (ids[type].op != 23 || v[ids[type].at + 2] != float_type || v[ids[type].at + 3] != 4) goto done;
    }
    uint32_t void_type = m.next++, bool_type = m.next++, uint_type = m.next++;
    int new_uint = 1;
    for (uint32_t id = 1; id < bound; ++id)
        if (ids[id].needed && ids[id].op == 21 && v[ids[id].at + 2] == 32 && v[ids[id].at + 3] == 0) {
            uint_type = id; new_uint = 0; break;
        }
    uint32_t function_type = m.next++, fn = m.next++, label = m.next++;
    uint32_t constants[64]; for (unsigned i = 0; i < 64; ++i) constants[i] = m.next++;
    uint32_t three = m.next++, zero = m.next++, false_id = m.next++, float_ptr = m.next++;
    uint32_t true_id = m.next++;
    for (uint32_t type = 1; type < bound; ++type) {
        if (!ids[type].needed || ids[type].op < 20 || ids[type].op > 30) continue;
        const uint32_t *p = v + ids[type].at;
        if ((ids[type].op == 30 && (p[0] >> 16) > 66) ||
            (ids[type].op == 28 && v[ids[p[3]].at + 3] > 64)) goto done;
        ids[type].read_ptr = type == float_type ? float_ptr : m.next++;
        ids[type].in_type = (ids[type].op == 24 || ids[type].op == 28 || ids[type].op == 30) ? m.next++ : type;
        for (uint32_t id = 1; id < bound; ++id)
            if (ids[id].op == 32 && v[ids[id].at + 2] == 3 && v[ids[id].at + 3] == type) {
                ids[type].write_ptr = id; ids[id].needed = 1; break;
            }
        if (!ids[type].write_ptr) ids[type].write_ptr = m.next++;
    }
#define EMIT(op, n, ...) do { if (!ins(&m, op, n, __VA_ARGS__)) { status = VK_ERROR_OUT_OF_HOST_MEMORY; goto done; } } while (0)
#define COPY(p, n) do { if (!words(&m, p, n)) { status = VK_ERROR_OUT_OF_HOST_MEMORY; goto done; } } while (0)
    COPY(v, 5);
    for (size_t at = 5; at < total; at += v[at] >> 16)
        if ((v[at] & 65535) == 17 || (v[at] & 65535) == 10) COPY(v + at, v[at] >> 16);
    EMIT(17, 2, 2u); /* Geometry */
    EMIT(14, 3, 0u, 1u); /* Logical GLSL450 */
    uint32_t entry[133] = {(uint32_t)((5 + count * 2) << 16) | 15, 3, fn, 0x6e69616d, 0};
    for (uint32_t i = 0; i < count; ++i) { entry[5 + i * 2] = outputs[i]; entry[6 + i * 2] = ids[outputs[i]].input; }
    COPY(entry, 5 + count * 2);
    EMIT(16, 3, fn, input_mode);
    EMIT(16, 4, fn, 0u, 1u); /* Invocations */
    EMIT(16, 3, fn, edges ? 28u : output_mode);
    EMIT(16, 4, fn, 26u, edges ? 6u : vertices); /* OutputVertices */
    for (size_t at = 5; at < total; at += v[at] >> 16) {
        const uint32_t *p = v + at; unsigned n = p[0] >> 16, op = p[0] & 65535;
        if ((op == 71 || op == 72) && p[1] < bound && ids[p[1]].needed) {
            COPY(p, n);
            if (op == 71 && ids[p[1]].op == 59) {
                size_t start = m.n; COPY(p, n); m.w[start + 1] = ids[p[1]].input;
            }
            if (ids[p[1]].in_type && ids[p[1]].in_type != p[1]) {
                size_t start = m.n; COPY(p, n); m.w[start + 1] = ids[p[1]].in_type;
            }
        }
    }
    for (size_t at = 5; at < total; at += v[at] >> 16) {
        const uint32_t *p = v + at; unsigned n = p[0] >> 16, op = p[0] & 65535;
        if (op >= 19 && op <= 32 && ids[p[1]].needed) COPY(p, n);
        if (op == 43 && ids[p[2]].needed) COPY(p, n);
    }
    EMIT(19, 2, void_type); EMIT(20, 2, bool_type);
    if (new_uint) EMIT(21, 4, uint_type, 32u, 0u);
    EMIT(33, 3, function_type, void_type);
    for (unsigned i = 0; i < 64; ++i) EMIT(43, 4, uint_type, constants[i], i);
    EMIT(43, 4, uint_type, three, vertices); EMIT(43, 4, float_type, zero, 0u);
    EMIT(42, 3, bool_type, false_id); EMIT(32, 4, float_ptr, 1u, float_type);
    EMIT(41, 3, bool_type, true_id);
    for (size_t at = 5; at < total; at += v[at] >> 16) {
        const uint32_t *p = v + at; unsigned n = p[0] >> 16, op = p[0] & 65535;
        if ((op == 24 || op == 28 || op == 30) && ids[p[1]].in_type && ids[p[1]].in_type != p[1]) {
            size_t start = m.n; COPY(p, n); m.w[start + 1] = ids[p[1]].in_type;
            unsigned end = op == 30 ? n : 3;
            for (unsigned i = 2; i < end; ++i) m.w[start + i] = ids[p[i]].in_type;
        }
    }
    for (uint32_t type = 1; type < bound; ++type) {
        if (!ids[type].read_ptr) continue;
        if (type != float_type) EMIT(32, 4, ids[type].read_ptr, 1u, ids[type].in_type);
        if (ids[type].write_ptr >= bound) EMIT(32, 4, ids[type].write_ptr, 3u, type);
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t var = outputs[i], value_type = v[ids[ids[var].type].at + 3], input_array = m.next++;
        EMIT(28, 4, input_array, ids[value_type].in_type, three);
        EMIT(32, 4, ids[var].input_ptr, 1u, input_array);
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t var = outputs[i];
        EMIT(59, 4, ids[var].type, var, 3u);
        EMIT(59, 4, ids[var].input_ptr, ids[var].input, 1u);
    }
    EMIT(54, 5, void_type, fn, 0u, function_type); EMIT(248, 2, label);
    uint32_t discard = false_id;
    if (faces && faces->cull_mode == VK_CULL_MODE_FRONT_AND_BACK) discard = true_id;
    else if (faces && faces->cull_mode) {
        uint32_t xy[3][2];
        for (uint32_t vert = 0; vert < vertices; ++vert) {
            uint32_t components[3];
            for (uint32_t c = 0; c < 3; ++c) {
                uint32_t ptr = m.next++; components[c] = m.next++;
                uint32_t component = c == 2 ? 3 : c;
                if (position_member == UINT32_MAX)
                    EMIT(65, 6, float_ptr, ptr, ids[position_var].input, constants[vert], constants[component]);
                else
                    EMIT(65, 7, float_ptr, ptr, ids[position_var].input, constants[vert], constants[position_member], constants[component]);
                EMIT(61, 4, float_type, components[c], ptr);
            }
            for (uint32_t c = 0; c < 2; ++c) {
                xy[vert][c] = m.next++;
                EMIT(136, 5, float_type, xy[vert][c], components[c], components[2]);
            }
        }
        uint32_t delta[2][2];
        for (uint32_t vert = 0; vert < 2; ++vert) for (uint32_t c = 0; c < 2; ++c) {
            delta[vert][c] = m.next++;
            EMIT(131, 5, float_type, delta[vert][c], xy[vert+1][c], xy[0][c]);
        }
        uint32_t a = m.next++, b = m.next++, area = m.next++, front = m.next++;
        EMIT(133, 5, float_type, a, delta[0][0], delta[1][1]);
        EMIT(133, 5, float_type, b, delta[0][1], delta[1][0]);
        EMIT(131, 5, float_type, area, a, b);
        /* Vulkan's framebuffer-area formula has a negative half factor.
         * Translation/positive scale do not change orientation. Strict
         * comparison makes zero-area triangles back-facing, as specified. */
        int front_positive = (faces->front_face == VK_FRONT_FACE_CLOCKWISE) != !!faces->viewport_inverted;
        EMIT(184, 5, bool_type, front, front_positive ? zero : area, front_positive ? area : zero);
        discard = front;
        if (faces->cull_mode == VK_CULL_MODE_BACK_BIT) {
            discard = m.next++; EMIT(168, 4, bool_type, discard, front);
        }
    }
    for (uint32_t plane = 0; plane < distances; ++plane) {
        uint32_t negative[3];
        for (uint32_t vert = 0; vert < vertices; ++vert) {
            uint32_t ptr = m.next++, value = m.next++; negative[vert] = m.next++;
            EMIT(65, 6, float_ptr, ptr, ids[distance_var].input, constants[vert], constants[plane]);
            EMIT(61, 4, float_type, value, ptr);
            EMIT(184, 5, bool_type, negative[vert], value, zero); /* FOrdLessThan */
        }
        uint32_t all = negative[0];
        for (uint32_t vert = 1; vert < vertices; ++vert) {
            uint32_t both = m.next++;
            EMIT(167, 5, bool_type, both, all, negative[vert]); all = both;
        }
        uint32_t any = m.next++;
        EMIT(166, 5, bool_type, any, discard, all); discard = any;
    }
    uint32_t culled = m.next++, keep = m.next++;
    EMIT(247, 3, keep, 0u); EMIT(250, 4, discard, culled, keep);
    EMIT(248, 2, culled); EMIT(253, 1, 0u); EMIT(248, 2, keep);
    for (uint32_t vert = 0; vert < (edges ? 6u : vertices); ++vert) {
        uint32_t source = edges ? ((vert / 2 + vert % 2) % 3) : vert;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t var = outputs[i], value_type = v[ids[ids[var].type].at + 3];
            uint32_t path[65] = {constants[source]};
            if (!pass_value(&m, v, ids, value_type, ids[var].input, var, constants, path, 0)) {
                status = VK_ERROR_OUT_OF_HOST_MEMORY; goto done;
            }
        }
        EMIT(218, 1, 0u);
        if (edges && vert % 2) EMIT(219, 1, 0u);
    }
    if (!edges) EMIT(219, 1, 0u);
    EMIT(253, 1, 0u); EMIT(56, 1, 0u);
    m.w[3] = m.next;
    *output = m.w; *output_bytes = m.n * 4; m.w = NULL; *reason = NULL; status = VK_SUCCESS;
done:
    hybris_scaled_free(alloc, ids); hybris_scaled_free(alloc, m.w);
    return status;
#undef EMIT
#undef COPY
}

VkResult hybris_spirv_primitive_adapter(const uint32_t *v, size_t bytes,
    uint32_t location, uint32_t distances, int edges,
    const struct hybris_primitive_faces *faces, const VkAllocationCallbacks *alloc,
    uint32_t **output, size_t *output_bytes, const char **reason)
{
    return hybris_spirv_primitive_adapter_topology(v, bytes, location, distances,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, edges, faces, alloc,
        output, output_bytes, reason);
}
