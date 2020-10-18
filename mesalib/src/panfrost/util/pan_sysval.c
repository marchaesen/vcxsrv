/*
 * Copyright (C) 2020 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "pan_ir.h"

/* TODO: ssbo_size */
static int
panfrost_sysval_for_ssbo(nir_intrinsic_instr *instr)
{
        nir_src index = instr->src[0];
        assert(nir_src_is_const(index));
        uint32_t uindex = nir_src_as_uint(index);

        return PAN_SYSVAL(SSBO, uindex);
}

static int
panfrost_sysval_for_sampler(nir_intrinsic_instr *instr)
{
        /* TODO: indirect samplers !!! */
        nir_src index = instr->src[0];
        assert(nir_src_is_const(index));
        uint32_t uindex = nir_src_as_uint(index);

        return PAN_SYSVAL(SAMPLER, uindex);
}

static unsigned
panfrost_nir_sysval_for_intrinsic(nir_intrinsic_instr *instr)
{
        switch (instr->intrinsic) {
        case nir_intrinsic_load_viewport_scale:
                return PAN_SYSVAL_VIEWPORT_SCALE;
        case nir_intrinsic_load_viewport_offset:
                return PAN_SYSVAL_VIEWPORT_OFFSET;
        case nir_intrinsic_load_num_work_groups:
                return PAN_SYSVAL_NUM_WORK_GROUPS;
        case nir_intrinsic_load_ssbo_address: 
        case nir_intrinsic_get_ssbo_size: 
                return panfrost_sysval_for_ssbo(instr);
        case nir_intrinsic_load_sampler_lod_parameters_pan:
                return panfrost_sysval_for_sampler(instr);
        default:
                return ~0;
        }
}

int
panfrost_sysval_for_instr(nir_instr *instr, nir_dest *dest)
{
        nir_intrinsic_instr *intr;
        nir_dest *dst = NULL;
        nir_tex_instr *tex;
        unsigned sysval = ~0;

        switch (instr->type) {
        case nir_instr_type_intrinsic:
                intr = nir_instr_as_intrinsic(instr);
                sysval = panfrost_nir_sysval_for_intrinsic(intr);
                dst = &intr->dest;
                break;
        case nir_instr_type_tex:
                tex = nir_instr_as_tex(instr);
                if (tex->op != nir_texop_txs)
                        break;

                sysval = PAN_SYSVAL(TEXTURE_SIZE,
                                    PAN_TXS_SYSVAL_ID(tex->texture_index,
                                                      nir_tex_instr_dest_size(tex) -
                                                      (tex->is_array ? 1 : 0),
                                                      tex->is_array));
                dst  = &tex->dest;
                break;
        default:
                break;
        }

        if (dest && dst)
                *dest = *dst;

        return sysval;
}

static void
panfrost_nir_assign_sysval_body(struct panfrost_sysvals *ctx, nir_instr *instr)
{
        int sysval = panfrost_sysval_for_instr(instr, NULL);
        if (sysval < 0)
                return;

        /* We have a sysval load; check if it's already been assigned */

        if (_mesa_hash_table_u64_search(ctx->sysval_to_id, sysval))
                return;

        /* It hasn't -- so assign it now! */

        unsigned id = ctx->sysval_count++;
        _mesa_hash_table_u64_insert(ctx->sysval_to_id, sysval, (void *) ((uintptr_t) id + 1));
        ctx->sysvals[id] = sysval;
}

void
panfrost_nir_assign_sysvals(struct panfrost_sysvals *ctx, void *memctx, nir_shader *shader)
{
        ctx->sysval_count = 0;
        ctx->sysval_to_id = _mesa_hash_table_u64_create(memctx);

        nir_foreach_function(function, shader) {
                if (!function->impl) continue;

                nir_foreach_block(block, function->impl) {
                        nir_foreach_instr_safe(instr, block) {
                                panfrost_nir_assign_sysval_body(ctx, instr);
                        }
                }
        }
}
