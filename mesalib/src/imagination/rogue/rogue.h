/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ROGUE_H
#define ROGUE_H

#include <stddef.h>
#include <stdint.h>

#include "compiler/shader_enums.h"
#include "nir/nir.h"

/* All registers are 32-bit in size. */
#define ROGUE_REG_SIZE_BYTES 4
#define ROGUE_REG_UNUSED UINT32_MAX

struct nir_spirv_specialization;
struct rogue_build_ctx;
struct rogue_shader;

enum rogue_msaa_mode {
   ROGUE_MSAA_MODE_UNDEF = 0, /* explicitly treat 0 as undefined */
   /* One task for all samples. */
   ROGUE_MSAA_MODE_PIXEL,
   /* For on-edge pixels only: separate tasks for each sample. */
   ROGUE_MSAA_MODE_SELECTIVE,
   /* For all pixels: separate tasks for each sample. */
   ROGUE_MSAA_MODE_FULL,
};

/**
 * \brief Shader binary.
 */
struct rogue_shader_binary {
   size_t size;
   uint8_t data[];
};

PUBLIC
nir_shader *rogue_spirv_to_nir(struct rogue_build_ctx *ctx,
                               gl_shader_stage stage,
                               const char *entry,
                               size_t spirv_size,
                               const uint32_t *spirv_data,
                               unsigned num_spec,
                               struct nir_spirv_specialization *spec);

PUBLIC
struct rogue_shader_binary *rogue_to_binary(struct rogue_build_ctx *ctx,
                                            const struct rogue_shader *shader);

PUBLIC
struct rogue_shader *rogue_nir_to_rogue(struct rogue_build_ctx *ctx,
                                        const nir_shader *nir);
#endif /* ROGUE_H */
