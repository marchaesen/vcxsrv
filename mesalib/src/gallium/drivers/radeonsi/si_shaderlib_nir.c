/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define AC_SURFACE_INCLUDE_NIR
#include "ac_surface.h"
#include "si_pipe.h"

static void *create_nir_cs(struct si_context *sctx, nir_builder *b)
{
   nir_shader_gather_info(b->shader, nir_shader_get_entrypoint(b->shader));

   struct pipe_compute_state state = {0};
   state.ir_type = PIPE_SHADER_IR_NIR;
   state.prog = b->shader;
   sctx->b.screen->finalize_nir(sctx->b.screen, (void*)state.prog);
   return sctx->b.create_compute_state(&sctx->b, &state);
}

static nir_ssa_def *get_global_ids(nir_builder *b, unsigned num_components)
{
   unsigned mask = BITFIELD_MASK(num_components);

   nir_ssa_def *local_ids = nir_channels(b, nir_load_local_invocation_id(b), mask);
   nir_ssa_def *block_ids = nir_channels(b, nir_load_workgroup_id(b, 32), mask);
   nir_ssa_def *block_size = nir_channels(b, nir_load_workgroup_size(b), mask);
   return nir_iadd(b, nir_imul(b, block_ids, block_size), local_ids);
}

static void unpack_2x16(nir_builder *b, nir_ssa_def *src, nir_ssa_def **x, nir_ssa_def **y)
{
   *x = nir_iand(b, src, nir_imm_int(b, 0xffff));
   *y = nir_ushr(b, src, nir_imm_int(b, 16));
}

void *si_create_dcc_retile_cs(struct si_context *sctx, struct radeon_surf *surf)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "dcc_retile");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 3;
   b.shader->info.num_ssbos = 1;

   /* Get user data SGPRs. */
   nir_ssa_def *user_sgprs = nir_load_user_data_amd(&b);

   /* Relative offset from the displayable DCC to the non-displayable DCC in the same buffer. */
   nir_ssa_def *src_dcc_offset = nir_channel(&b, user_sgprs, 0);

   nir_ssa_def *src_dcc_pitch, *dst_dcc_pitch, *src_dcc_height, *dst_dcc_height;
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 1), &src_dcc_pitch, &src_dcc_height);
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 2), &dst_dcc_pitch, &dst_dcc_height);

   /* Get the 2D coordinates. */
   nir_ssa_def *coord = get_global_ids(&b, 2);
   nir_ssa_def *zero = nir_imm_int(&b, 0);

   /* Multiply the coordinates by the DCC block size (they are DCC block coordinates). */
   coord = nir_imul(&b, coord, nir_imm_ivec2(&b, surf->u.gfx9.color.dcc_block_width,
                                             surf->u.gfx9.color.dcc_block_height));

   nir_ssa_def *src_offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, surf->bpe, &surf->u.gfx9.color.dcc_equation,
                                 src_dcc_pitch, src_dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 zero, zero, zero); /* z, sample, pipe_xor */
   src_offset = nir_iadd(&b, src_offset, src_dcc_offset);
   nir_ssa_def *value = nir_load_ssbo(&b, 1, 8, zero, src_offset, .align_mul=1);

   nir_ssa_def *dst_offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, surf->bpe, &surf->u.gfx9.color.display_dcc_equation,
                                 dst_dcc_pitch, dst_dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 zero, zero, zero); /* z, sample, pipe_xor */
   nir_store_ssbo(&b, value, zero, dst_offset, .write_mask=0x1, .align_mul=1);

   return create_nir_cs(sctx, &b);
}

void *gfx9_create_clear_dcc_msaa_cs(struct si_context *sctx, struct si_texture *tex)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "clear_dcc_msaa");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 2;
   b.shader->info.num_ssbos = 1;

   /* Get user data SGPRs. */
   nir_ssa_def *user_sgprs = nir_load_user_data_amd(&b);
   nir_ssa_def *dcc_pitch, *dcc_height, *clear_value, *pipe_xor;
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 0), &dcc_pitch, &dcc_height);
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 1), &clear_value, &pipe_xor);
   clear_value = nir_u2u16(&b, clear_value);

   /* Get the 2D coordinates. */
   nir_ssa_def *coord = get_global_ids(&b, 3);
   nir_ssa_def *zero = nir_imm_int(&b, 0);

   /* Multiply the coordinates by the DCC block size (they are DCC block coordinates). */
   coord = nir_imul(&b, coord,
                    nir_channels(&b, nir_imm_ivec4(&b, tex->surface.u.gfx9.color.dcc_block_width,
                                                   tex->surface.u.gfx9.color.dcc_block_height,
                                                   tex->surface.u.gfx9.color.dcc_block_depth, 0), 0x7));

   nir_ssa_def *offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, tex->surface.bpe,
                                 &tex->surface.u.gfx9.color.dcc_equation,
                                 dcc_pitch, dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 tex->buffer.b.b.array_size > 1 ? nir_channel(&b, coord, 2) : zero, /* z */
                                 zero, pipe_xor); /* sample, pipe_xor */

   /* The trick here is that DCC elements for an even and the next odd sample are next to each other
    * in memory, so we only need to compute the address for sample 0 and the next DCC byte is always
    * sample 1. That's why the clear value has 2 bytes - we're clearing 2 samples at the same time.
    */
   nir_store_ssbo(&b, clear_value, zero, offset, .write_mask=0x1, .align_mul=2);

   return create_nir_cs(sctx, &b);
}
