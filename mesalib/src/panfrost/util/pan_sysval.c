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
#include "compiler/nir/nir_builder.h"

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

static int
panfrost_sysval_for_image_size(nir_intrinsic_instr *instr)
{
        nir_src index = instr->src[0];
        assert(nir_src_is_const(index));

        bool is_array = nir_intrinsic_image_array(instr);
        uint32_t uindex = nir_src_as_uint(index);
        unsigned dim = nir_intrinsic_dest_components(instr) - is_array;

        return PAN_SYSVAL(IMAGE_SIZE, PAN_TXS_SYSVAL_ID(uindex, dim, is_array));
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
        case nir_intrinsic_load_local_group_size:
                return PAN_SYSVAL_LOCAL_GROUP_SIZE;
        case nir_intrinsic_load_work_dim:
                return PAN_SYSVAL_WORK_DIM;
        case nir_intrinsic_load_sample_positions_pan:
                return PAN_SYSVAL_SAMPLE_POSITIONS;
        case nir_intrinsic_load_ssbo_address: 
        case nir_intrinsic_get_ssbo_size: 
                return panfrost_sysval_for_ssbo(instr);
        case nir_intrinsic_load_sampler_lod_parameters_pan:
                return panfrost_sysval_for_sampler(instr);
        case nir_intrinsic_image_size:
                return panfrost_sysval_for_image_size(instr);
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

unsigned
pan_lookup_sysval(struct hash_table_u64 *sysval_to_id,
                  struct panfrost_sysvals *sysvals,
                  int sysval)
{
        /* Try to lookup */

        void *cached = _mesa_hash_table_u64_search(sysval_to_id, sysval);

        if (cached)
                return ((uintptr_t) cached) - 1;

        /* Else assign */

        unsigned id = sysvals->sysval_count++;
        assert(id < MAX_SYSVAL_COUNT);
        _mesa_hash_table_u64_insert(sysval_to_id, sysval, (void *) ((uintptr_t) id + 1));
        sysvals->sysvals[id] = sysval;

        return id;
}

struct hash_table_u64 *
panfrost_init_sysvals(struct panfrost_sysvals *sysvals, void *memctx)
{
        sysvals->sysval_count = 0;
        return _mesa_hash_table_u64_create(memctx);
}
