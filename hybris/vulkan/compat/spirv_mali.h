/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HYBRIS_SPIRV_MALI_H
#define HYBRIS_SPIRV_MALI_H

#include <stddef.h>
#include <stdint.h>

/* Redeclare constant composites that depend on specialization constants as
 * specialization composites. Arm's compiler otherwise folds DXVK's texture
 * guard to the specialization defaults, which can make sampled textures read
 * black. The rewrite is in place and returns the number changed. */
unsigned hybris_spirv_mali_spec_constants(uint32_t *code, size_t size);

#endif
