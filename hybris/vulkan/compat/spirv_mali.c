/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ASEbrahim and MaliBridge contributors
 *
 * Adapted from MaliBridge server_spvfix.c:
 * https://github.com/ASEbrahim/MaliBridge
 */
#include "spirv_mali.h"

#include <stdlib.h>

#define OP_FUNCTION 54
#define OP_CONSTANT_COMPOSITE 44
#define OP_SPEC_CONSTANT_TRUE 48
#define OP_SPEC_CONSTANT_FALSE 49
#define OP_SPEC_CONSTANT 50
#define OP_SPEC_CONSTANT_COMPOSITE 51
#define OP_SPEC_CONSTANT_OP 52

unsigned hybris_spirv_mali_spec_constants(uint32_t *code, size_t size)
{
    size_t words = size / sizeof(*code);
    if (size % sizeof(*code) || words < 5 || code[0] != 0x07230203u)
        return 0;

    uint32_t bound = code[3];
    if (!bound || bound > (1u << 24))
        return 0;

    uint8_t *constant = calloc(bound, sizeof(*constant));
    if (!constant)
        return 0;

    unsigned changed = 0;
    for (size_t at = 5; at < words;) {
        uint32_t op = code[at] & 0xffffu;
        uint32_t length = code[at] >> 16;
        if (!length || length > words - at)
            break;
        if (op == OP_FUNCTION)
            break; /* Constants and types precede the first function. */

        if ((op == OP_SPEC_CONSTANT_TRUE || op == OP_SPEC_CONSTANT_FALSE ||
             op == OP_SPEC_CONSTANT || op == OP_SPEC_CONSTANT_COMPOSITE ||
             op == OP_SPEC_CONSTANT_OP) &&
            length >= 3 && code[at + 2] < bound) {
            constant[code[at + 2]] = 1;
        } else if (op == OP_CONSTANT_COMPOSITE && length >= 3 &&
                   code[at + 2] < bound) {
            int depends_on_specialization = 0;
            for (uint32_t i = 3; i < length; ++i)
                depends_on_specialization |=
                    code[at + i] < bound && constant[code[at + i]];
            if (depends_on_specialization) {
                code[at] = (length << 16) | OP_SPEC_CONSTANT_COMPOSITE;
                constant[code[at + 2]] = 1;
                ++changed;
            }
        }
        at += length;
    }

    free(constant);
    return changed;
}
