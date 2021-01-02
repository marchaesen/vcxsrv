/*
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright 2018-2019 Alyssa Rosenzweig
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
 */

#include "pan_bo.h"
#include "pan_context.h"
#include "pan_util.h"

#include "util/format/u_format.h"

static void
panfrost_sfbd_format(struct pipe_surface *surf,
                     struct MALI_SINGLE_TARGET_FRAMEBUFFER_PARAMETERS *fb)
{
        /* Explode details on the format */

        const struct util_format_description *desc =
                util_format_description(surf->format);

        /* The swizzle for rendering is inverted from texturing */

        unsigned char swizzle[4];
        panfrost_invert_swizzle(desc->swizzle, swizzle);

        fb->swizzle = panfrost_translate_swizzle_4(swizzle);

        struct pan_blendable_format fmt = panfrost_blend_format(surf->format);

        if (fmt.internal) {
                fb->internal_format = fmt.internal;
                fb->color_writeback_format = fmt.writeback;
        } else {
                unreachable("raw formats not finished for SFBD");
        }
}

static void
panfrost_sfbd_clear(
        struct panfrost_batch *batch,
        struct MALI_SINGLE_TARGET_FRAMEBUFFER_PARAMETERS *sfbd)
{
        if (batch->clear & PIPE_CLEAR_COLOR) {
                sfbd->clear_color_0 = batch->clear_color[0][0];
                sfbd->clear_color_1 = batch->clear_color[0][1];
                sfbd->clear_color_2 = batch->clear_color[0][2];
                sfbd->clear_color_3 = batch->clear_color[0][3];
        }

        if (batch->clear & PIPE_CLEAR_DEPTH)
                sfbd->z_clear = batch->clear_depth;

        if (batch->clear & PIPE_CLEAR_STENCIL)
                sfbd->s_clear = batch->clear_stencil & 0xff;
}

static void
panfrost_sfbd_set_cbuf(
        struct MALI_SINGLE_TARGET_FRAMEBUFFER_PARAMETERS *fb,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        unsigned level = surf->u.tex.level;
        unsigned first_layer = surf->u.tex.first_layer;
        assert(surf->u.tex.last_layer == first_layer);
        signed row_stride = rsrc->slices[level].row_stride;

        mali_ptr base = panfrost_get_texture_address(rsrc, level, first_layer, 0);

        panfrost_sfbd_format(surf, fb);

        fb->color_write_enable = true;
        fb->color_writeback.base = base;
        fb->color_writeback.row_stride = row_stride;

        if (rsrc->modifier == DRM_FORMAT_MOD_LINEAR)
                fb->color_block_format = MALI_BLOCK_FORMAT_LINEAR;
        else if (rsrc->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
                fb->color_block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;
        } else {
                fprintf(stderr, "Invalid render modifier\n");
                assert(0);
        }
}

static void
panfrost_sfbd_set_zsbuf(
        struct MALI_SINGLE_TARGET_FRAMEBUFFER_PARAMETERS *fb,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        unsigned level = surf->u.tex.level;
        assert(surf->u.tex.first_layer == 0);

        fb->zs_writeback.base = rsrc->bo->ptr.gpu + rsrc->slices[level].offset;
        fb->zs_writeback.row_stride = rsrc->slices[level].row_stride;

        if (rsrc->modifier == DRM_FORMAT_MOD_LINEAR)
                fb->zs_block_format = MALI_BLOCK_FORMAT_LINEAR;
        else if (rsrc->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
                fb->zs_block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;
        } else {
                fprintf(stderr, "Invalid render modifier\n");
                assert(0);
        }

        switch (surf->format) {
        case PIPE_FORMAT_Z16_UNORM:
                fb->zs_format = MALI_ZS_FORMAT_D16;
                break;
        case PIPE_FORMAT_Z24_UNORM_S8_UINT:
                fb->zs_format = MALI_ZS_FORMAT_D24S8;
                break;
        case PIPE_FORMAT_Z24X8_UNORM:
                fb->zs_format = MALI_ZS_FORMAT_D24X8;
                break;
        case PIPE_FORMAT_Z32_FLOAT:
                fb->zs_format = MALI_ZS_FORMAT_D32;
                break;
        case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
                fb->zs_format = MALI_ZS_FORMAT_D32_S8X24;
                break;
        default:
                unreachable("Unsupported depth/stencil format.");
        }
}

static void
panfrost_init_sfbd_params(struct panfrost_batch *batch,
                          struct MALI_SINGLE_TARGET_FRAMEBUFFER_PARAMETERS *sfbd)
{
        sfbd->bound_max_x = batch->key.width - 1;
        sfbd->bound_max_y = batch->key.height - 1;
        sfbd->dithering_enable = true;
        sfbd->clean_pixel_write_enable = true;
        sfbd->tie_break_rule = MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT;
}

static void
panfrost_emit_sfdb_local_storage(struct panfrost_batch *batch, void *sfbd,
                                 unsigned vertex_count)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        /* TODO: Why do we need to make the stack bigger than other platforms? */
        unsigned shift = panfrost_get_stack_shift(MAX2(batch->stack_size, 512));

        pan_section_pack(sfbd, SINGLE_TARGET_FRAMEBUFFER, LOCAL_STORAGE, ls) {
                ls.tls_size = shift;
                ls.wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM;
                ls.tls_base_pointer =
                        panfrost_batch_get_scratchpad(batch,
                                                      shift,
                                                      dev->thread_tls_alloc,
                                                      dev->core_count)->ptr.gpu;
        }
}

static void
panfrost_emit_sfdb_tiler(struct panfrost_batch *batch, void *sfbd,
                         unsigned vertex_count)
{
        void *tiler = pan_section_ptr(sfbd, SINGLE_TARGET_FRAMEBUFFER, TILER);

        panfrost_emit_midg_tiler(batch, tiler, vertex_count);

        /* All weights set to 0, nothing to do here */
        pan_section_pack(sfbd, SINGLE_TARGET_FRAMEBUFFER, PADDING_1, padding) {}
        pan_section_pack(sfbd, SINGLE_TARGET_FRAMEBUFFER, TILER_WEIGHTS, w) {}
}

void
panfrost_attach_sfbd(struct panfrost_batch *batch, unsigned vertex_count)
{
        void *sfbd = batch->framebuffer.cpu;

        panfrost_emit_sfdb_local_storage(batch, sfbd, vertex_count);
        pan_section_pack(sfbd, SINGLE_TARGET_FRAMEBUFFER, PARAMETERS, params) {
                panfrost_init_sfbd_params(batch, &params);
        }
        panfrost_emit_sfdb_tiler(batch, sfbd, vertex_count);
        pan_section_pack(sfbd, SINGLE_TARGET_FRAMEBUFFER, PADDING_2, padding) {}
}

/* Creates an SFBD for the FRAGMENT section of the bound framebuffer */

mali_ptr
panfrost_sfbd_fragment(struct panfrost_batch *batch, bool has_draws)
{
        struct panfrost_ptr t =
                panfrost_pool_alloc_aligned(&batch->pool,
                                            MALI_SINGLE_TARGET_FRAMEBUFFER_LENGTH,
                                            64);
        void *sfbd = t.cpu;

        panfrost_emit_sfdb_local_storage(batch, sfbd, has_draws);
        pan_section_pack(sfbd, SINGLE_TARGET_FRAMEBUFFER, PARAMETERS, params) {
                panfrost_init_sfbd_params(batch, &params);
                panfrost_sfbd_clear(batch, &params);

                /* SFBD does not support MRT natively; sanity check */
                assert(batch->key.nr_cbufs <= 1);
                if (batch->key.nr_cbufs) {
                        struct pipe_surface *surf = batch->key.cbufs[0];
                        struct panfrost_resource *rsrc = pan_resource(surf->texture);
                        struct panfrost_bo *bo = rsrc->bo;

                        panfrost_sfbd_set_cbuf(&params, surf);

                        if (rsrc->checksummed) {
                                unsigned level = surf->u.tex.level;
                                struct panfrost_slice *slice = &rsrc->slices[level];

                                params.crc_buffer.row_stride = slice->checksum_stride;
                                params.crc_buffer.base = bo->ptr.gpu + slice->checksum_offset;
                        }
                }

                if (batch->key.zsbuf)
                        panfrost_sfbd_set_zsbuf(&params, batch->key.zsbuf);

                if (batch->requirements & PAN_REQ_MSAA) {
                        /* Only 4x MSAA supported right now. */
                        params.sample_count = 4;
                        params.msaa = MALI_MSAA_MULTIPLE;
                }
        }
        panfrost_emit_sfdb_tiler(batch, sfbd, has_draws);
        pan_section_pack(sfbd, SINGLE_TARGET_FRAMEBUFFER, PADDING_2, padding) {}

        return t.gpu;
}
