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
#include "pan_cmdstream.h"
#include "pan_util.h"
#include "panfrost-quirks.h"


static bool
panfrost_mfbd_has_zs_crc_ext(struct panfrost_batch *batch)
{
        if (batch->key.nr_cbufs == 1) {
                struct pipe_surface *surf = batch->key.cbufs[0];

                if (surf->texture && pan_resource(surf->texture)->checksummed)
                        return true;
        }

        if (batch->key.zsbuf &&
            ((batch->clear | batch->draws) & PIPE_CLEAR_DEPTHSTENCIL))
                return true;

        return false;
}

static enum mali_mfbd_color_format
panfrost_mfbd_raw_format(unsigned bits)
{
        switch (bits) {
        case    8: return MALI_MFBD_COLOR_FORMAT_RAW8;
        case   16: return MALI_MFBD_COLOR_FORMAT_RAW16;
        case   24: return MALI_MFBD_COLOR_FORMAT_RAW24;
        case   32: return MALI_MFBD_COLOR_FORMAT_RAW32;
        case   48: return MALI_MFBD_COLOR_FORMAT_RAW48;
        case   64: return MALI_MFBD_COLOR_FORMAT_RAW64;
        case   96: return MALI_MFBD_COLOR_FORMAT_RAW96;
        case  128: return MALI_MFBD_COLOR_FORMAT_RAW128;
        case  192: return MALI_MFBD_COLOR_FORMAT_RAW192;
        case  256: return MALI_MFBD_COLOR_FORMAT_RAW256;
        case  384: return MALI_MFBD_COLOR_FORMAT_RAW384;
        case  512: return MALI_MFBD_COLOR_FORMAT_RAW512;
        case  768: return MALI_MFBD_COLOR_FORMAT_RAW768;
        case 1024: return MALI_MFBD_COLOR_FORMAT_RAW1024;
        case 1536: return MALI_MFBD_COLOR_FORMAT_RAW1536;
        case 2048: return MALI_MFBD_COLOR_FORMAT_RAW2048;
        default: unreachable("invalid raw bpp");
        }
}

static void
panfrost_mfbd_rt_init_format(const struct panfrost_device *dev,
                             struct pipe_surface *surf,
                             struct MALI_RENDER_TARGET *rt)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);
        enum pipe_format format =
                drm_is_afbc(rsrc->layout.modifier) ?
                panfrost_afbc_format_fixup(dev, surf->format) :
                surf->format;

        /* Explode details on the format */

        const struct util_format_description *desc =
                util_format_description(format);

        /* The swizzle for rendering is inverted from texturing */

        unsigned char swizzle[4];
        panfrost_invert_swizzle(desc->swizzle, swizzle);

        rt->swizzle = panfrost_translate_swizzle_4(swizzle);

        /* Fill in accordingly, defaulting to 8-bit UNORM */

        if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
                rt->srgb = true;

        struct pan_blendable_format fmt = panfrost_blend_format(surf->format);

        if (fmt.internal) {
                rt->internal_format = fmt.internal;
                rt->writeback_format = fmt.writeback;
        } else {
                /* Construct RAW internal/writeback, where internal is
                 * specified logarithmically (round to next power-of-two).
                 * Offset specified from RAW8, where 8 = 2^3 */

                unsigned bits = desc->block.bits;
                unsigned offset = util_logbase2_ceil(bits) - 3;
                assert(offset <= 4);

                rt->internal_format =
                        MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW8 + offset;

                rt->writeback_format = panfrost_mfbd_raw_format(bits);
        }
}

static void
panfrost_mfbd_rt_set_buf(struct pipe_surface *surf,
                         struct MALI_RENDER_TARGET *rt)
{
        struct panfrost_device *dev = pan_device(surf->context->screen);
        struct panfrost_resource *rsrc = pan_resource(surf->texture);
        unsigned level = surf->u.tex.level;
        unsigned first_layer = surf->u.tex.first_layer;
        assert(surf->u.tex.last_layer == first_layer);
        int row_stride = rsrc->layout.slices[level].row_stride;

        /* Only set layer_stride for layered MSAA rendering  */

        unsigned nr_samples = surf->texture->nr_samples;
        unsigned layer_stride = (nr_samples > 1) ? rsrc->layout.slices[level].surface_stride : 0;

        if (layer_stride)
                rt->writeback_msaa = MALI_MSAA_LAYERED;
        else if (surf->nr_samples)
                rt->writeback_msaa = MALI_MSAA_AVERAGE;
        else
                rt->writeback_msaa = MALI_MSAA_SINGLE;

        panfrost_mfbd_rt_init_format(dev, surf, rt);

        if (rsrc->layout.modifier == DRM_FORMAT_MOD_LINEAR) {
                mali_ptr base = panfrost_get_texture_address(rsrc, level, first_layer, 0);

                if (dev->arch >= 7)
                        rt->bifrost_v7.writeback_block_format = MALI_BLOCK_FORMAT_V7_LINEAR;
                else
                        rt->midgard.writeback_block_format = MALI_BLOCK_FORMAT_LINEAR;

                rt->rgb.base = base;
                rt->rgb.row_stride = row_stride;
                rt->rgb.surface_stride = layer_stride;
        } else if (rsrc->layout.modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
                mali_ptr base = panfrost_get_texture_address(rsrc, level, first_layer, 0);

                if (dev->arch >= 7)
                        rt->bifrost_v7.writeback_block_format = MALI_BLOCK_FORMAT_V7_TILED_U_INTERLEAVED;
                else
                        rt->midgard.writeback_block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;

                rt->rgb.base = base;
                rt->rgb.row_stride = row_stride;
                rt->rgb.surface_stride = layer_stride;
        } else if (drm_is_afbc(rsrc->layout.modifier)) {
                const struct panfrost_slice *slice = &rsrc->layout.slices[level];

                if (dev->arch >= 7)
                        rt->bifrost_v7.writeback_block_format = MALI_BLOCK_FORMAT_V7_AFBC;
                else
                        rt->midgard.writeback_block_format = MALI_BLOCK_FORMAT_AFBC;

                if (pan_is_bifrost(dev)) {
                        rt->afbc.row_stride = slice->afbc.row_stride /
                                              AFBC_HEADER_BYTES_PER_TILE;
                        rt->bifrost_afbc.afbc_wide_block_enable =
                                panfrost_block_dim(rsrc->layout.modifier, true, 0) > 16;
                } else {
                        rt->afbc.chunk_size = 9;
                        rt->midgard_afbc.sparse = true;
                        rt->afbc.body_size = slice->afbc.body_size;
                }

                panfrost_get_afbc_pointers(rsrc, level, first_layer,
                                           &rt->afbc.header,
                                           &rt->afbc.body);

                if (rsrc->layout.modifier & AFBC_FORMAT_MOD_YTR)
                        rt->afbc.yuv_transform_enable = true;
        } else {
                unreachable("Invalid mod");
        }
}

static void
panfrost_mfbd_emit_rt(struct panfrost_batch *batch,
                      void *rtp, struct pipe_surface *surf,
                      unsigned rt_offset, unsigned rt_idx)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        pan_pack(rtp, RENDER_TARGET, rt) {
                rt.clean_pixel_write_enable = true;
                if (surf) {
                        rt.write_enable = true;
                        rt.dithering_enable = true;
                        rt.internal_buffer_offset = rt_offset;
                        panfrost_mfbd_rt_set_buf(surf, &rt);
                } else {
                        rt.internal_format = MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8;
                        rt.internal_buffer_offset = rt_offset;
                        if (dev->arch >= 7) {
                                rt.bifrost_v7.writeback_block_format = MALI_BLOCK_FORMAT_V7_TILED_U_INTERLEAVED;
                                rt.dithering_enable = true;
                        }
                }

                if (batch->clear & (PIPE_CLEAR_COLOR0 << rt_idx)) {
                        rt.clear.color_0 = batch->clear_color[rt_idx][0];
                        rt.clear.color_1 = batch->clear_color[rt_idx][1];
                        rt.clear.color_2 = batch->clear_color[rt_idx][2];
                        rt.clear.color_3 = batch->clear_color[rt_idx][3];
                }
        }
}

static enum mali_z_internal_format
get_z_internal_format(struct panfrost_batch *batch)
{
        struct pipe_surface *zs_surf = batch->key.zsbuf;

        /* Default to 24 bit depth if there's no surface. */
        if (!zs_surf || !((batch->clear | batch->draws) & PIPE_CLEAR_DEPTHSTENCIL))
                return MALI_Z_INTERNAL_FORMAT_D24;

         return panfrost_get_z_internal_format(zs_surf->format);
}

static void
panfrost_mfbd_zs_crc_ext_set_bufs(struct panfrost_batch *batch,
                                  struct MALI_ZS_CRC_EXTENSION *ext,
                                  struct panfrost_slice **checksum_slice)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        /* Checksumming only works with a single render target */
        if (batch->key.nr_cbufs == 1 && batch->key.cbufs[0]) {
                struct pipe_surface *c_surf = batch->key.cbufs[0];
                struct panfrost_resource *rsrc = pan_resource(c_surf->texture);

                if (rsrc->checksummed) {
                        unsigned level = c_surf->u.tex.level;
                        struct panfrost_slice *slice = &rsrc->layout.slices[level];

                        *checksum_slice = slice;

                        ext->crc_row_stride = slice->crc.stride;
                        if (rsrc->checksum_bo)
                                ext->crc_base = rsrc->checksum_bo->ptr.gpu;
                        else
                                ext->crc_base = rsrc->bo->ptr.gpu + slice->crc.offset;

                        if ((batch->clear & PIPE_CLEAR_COLOR0) && dev->arch >= 7) {
                                ext->crc_clear_color = batch->clear_color[0][0] |
                                                      0xc000000000000000 |
                                                      ((uint64_t)batch->clear_color[0][0] & 0xffff) << 32;
                        }
                }
        }

        struct pipe_surface *zs_surf = batch->key.zsbuf;

        if (!((batch->clear | batch->draws) & PIPE_CLEAR_DEPTHSTENCIL))
                zs_surf = NULL;

        if (!zs_surf)
                return;

        struct panfrost_resource *rsrc = pan_resource(zs_surf->texture);
        unsigned nr_samples = MAX2(zs_surf->texture->nr_samples, 1);
        unsigned level = zs_surf->u.tex.level;
        unsigned first_layer = zs_surf->u.tex.first_layer;
        assert(zs_surf->u.tex.last_layer == first_layer);

        if (dev->arch < 7)
                ext->zs_msaa = nr_samples > 1 ? MALI_MSAA_LAYERED : MALI_MSAA_SINGLE;
        else
                ext->zs_msaa_v7 = nr_samples > 1 ? MALI_MSAA_LAYERED : MALI_MSAA_SINGLE;

        if (drm_is_afbc(rsrc->layout.modifier)) {
                struct panfrost_slice *slice = &rsrc->layout.slices[level];

                panfrost_get_afbc_pointers(rsrc, level, first_layer,
                                           &ext->zs_afbc_header,
                                           &ext->zs_afbc_body);

                if (dev->arch >= 7)
                        ext->zs_block_format_v7 = MALI_BLOCK_FORMAT_V7_AFBC;
                else
                        ext->zs_block_format = MALI_BLOCK_FORMAT_AFBC;

                if (pan_is_bifrost(dev)) {
                        ext->zs_afbc_row_stride = slice->afbc.row_stride /
                                                  AFBC_HEADER_BYTES_PER_TILE;
		} else {
                        ext->zs_block_format = MALI_BLOCK_FORMAT_AFBC;
                        ext->zs_afbc_body_size = 0x1000;
                        ext->zs_afbc_chunk_size = 9;
                        ext->zs_afbc_sparse = true;
                }
        } else {
                assert(rsrc->layout.modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED ||
                       rsrc->layout.modifier == DRM_FORMAT_MOD_LINEAR);
                mali_ptr base = panfrost_get_texture_address(rsrc, level, first_layer, 0);

                /* TODO: Z32F(S8) support, which is always linear */

                ext->zs_writeback_base = base;
                ext->zs_writeback_row_stride =
                        rsrc->layout.slices[level].row_stride;
                ext->zs_writeback_surface_stride =
                        (nr_samples > 1) ?
                        rsrc->layout.slices[level].surface_stride : 0;

                if (rsrc->layout.modifier == DRM_FORMAT_MOD_LINEAR) {
                        if (dev->arch >= 7)
                                ext->zs_block_format_v7 = MALI_BLOCK_FORMAT_V7_LINEAR;
                        else
                                ext->zs_block_format = MALI_BLOCK_FORMAT_LINEAR;
                } else {
                        if (dev->arch >= 7)
                                ext->zs_block_format_v7 = MALI_BLOCK_FORMAT_V7_TILED_U_INTERLEAVED;
                        else
                                ext->zs_block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;
                }
        }

        switch (zs_surf->format) {
        case PIPE_FORMAT_Z16_UNORM:
                ext->zs_write_format = MALI_ZS_FORMAT_D16;
                break;
        case PIPE_FORMAT_Z24_UNORM_S8_UINT:
                ext->zs_write_format = MALI_ZS_FORMAT_D24S8;
                ext->s_writeback_base = ext->zs_writeback_base;
                break;
        case PIPE_FORMAT_Z24X8_UNORM:
                ext->zs_write_format = MALI_ZS_FORMAT_D24X8;
                break;
        case PIPE_FORMAT_Z32_FLOAT:
                ext->zs_write_format = MALI_ZS_FORMAT_D32;
                break;
        case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
                /* Midgard/Bifrost support interleaved depth/stencil
                 * buffers, but we always treat them as multi-planar.
                 */
                ext->zs_write_format = MALI_ZS_FORMAT_D32;
                ext->s_write_format = MALI_S_FORMAT_S8;
                if (dev->arch < 7) {
                        ext->s_block_format = ext->zs_block_format;
                        ext->s_msaa = ext->zs_msaa;
                } else {
                        ext->s_block_format_v7 = ext->zs_block_format_v7;
                        ext->s_msaa_v7 = ext->zs_msaa_v7;
                }

                struct panfrost_resource *stencil = rsrc->separate_stencil;
                struct panfrost_slice *stencil_slice =
			&stencil->layout.slices[level];

                ext->s_writeback_base =
                        panfrost_get_texture_address(stencil, level, first_layer, 0);
                ext->s_writeback_row_stride = stencil_slice->row_stride;
                ext->s_writeback_surface_stride =
                        (nr_samples > 1) ? stencil_slice->surface_stride : 0;
                break;
        default:
                unreachable("Unsupported depth/stencil format.");
        }
}

static void
panfrost_mfbd_emit_zs_crc_ext(struct panfrost_batch *batch, void *extp,
                              struct panfrost_slice **checksum_slice)
{
        pan_pack(extp, ZS_CRC_EXTENSION, ext) {
                ext.zs_clean_pixel_write_enable = true;
                panfrost_mfbd_zs_crc_ext_set_bufs(batch, &ext, checksum_slice);
        }
}

/* Measure format as it appears in the tile buffer */

static unsigned
pan_bytes_per_pixel_tib(enum pipe_format format)
{
        if (panfrost_blend_format(format).internal) {
                /* Blendable formats are always 32-bits in the tile buffer,
                 * extra bits are used as padding or to dither */
                return 4;
        } else {
                /* Non-blendable formats are raw, rounded up to the nearest
                 * power-of-two size */
                unsigned bytes = util_format_get_blocksize(format);
                return util_next_power_of_two(bytes);
        }
}

/* Calculates the internal color buffer size and tile size based on the number
 * of RT, the format and the number of pixels. If things do not fit in 4KB, we
 * shrink the tile size to make it fit.
 */

static unsigned
pan_internal_cbuf_size(struct panfrost_batch *batch, unsigned *tile_size)
{
        unsigned total_size = 0;

        *tile_size = 16 * 16;
        for (int cb = 0; cb < batch->key.nr_cbufs; ++cb) {
                struct pipe_surface *surf = batch->key.cbufs[cb];

                if (!surf)
                        continue;

                unsigned nr_samples = MAX3(surf->nr_samples, surf->texture->nr_samples, 1);
                total_size += pan_bytes_per_pixel_tib(surf->format) *
                              nr_samples * (*tile_size);
        }

        /* We have a 4KB budget, let's reduce the tile size until it fits. */
        while (total_size > 4096) {
                total_size >>= 1;
                *tile_size >>= 1;
        }

        /* Align on 1k. */
        total_size = ALIGN_POT(total_size, 1024);

        /* Minimum tile size is 4x4. */
        assert(*tile_size >= 4 * 4);
        return total_size;
}

static void
panfrost_mfbd_emit_local_storage(struct panfrost_batch *batch, void *fb)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        pan_section_pack(fb, MULTI_TARGET_FRAMEBUFFER, LOCAL_STORAGE, ls) {
                if (batch->stack_size) {
                        unsigned shift =
                                panfrost_get_stack_shift(batch->stack_size);
                        struct panfrost_bo *bo =
                                panfrost_batch_get_scratchpad(batch,
                                                              batch->stack_size,
                                                              dev->thread_tls_alloc,
                                                              dev->core_count);
                        ls.tls_size = shift;
                        ls.tls_base_pointer = bo->ptr.gpu;
                }

                ls.wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM;
        }
}

static void
panfrost_mfbd_emit_midgard_tiler(struct panfrost_batch *batch, void *fb,
                                 unsigned vertex_count)
{
        void *t = pan_section_ptr(fb, MULTI_TARGET_FRAMEBUFFER, TILER);

        panfrost_emit_midg_tiler(batch, t, vertex_count);

        /* All weights set to 0, nothing to do here */
        pan_section_pack(fb, MULTI_TARGET_FRAMEBUFFER, TILER_WEIGHTS, w);
}

static void
panfrost_mfbd_emit_bifrost_parameters(struct panfrost_batch *batch, void *fb)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        pan_section_pack(fb, MULTI_TARGET_FRAMEBUFFER, BIFROST_PARAMETERS, params) {
                unsigned samples = util_framebuffer_get_num_samples(&batch->key);
                params.sample_locations = panfrost_sample_positions(dev, panfrost_sample_pattern(samples));
        }
}

static void
panfrost_mfbd_emit_bifrost_tiler(struct panfrost_batch *batch, void *fb,
                                 unsigned vertex_count)
{
        pan_section_pack(fb, MULTI_TARGET_FRAMEBUFFER, BIFROST_TILER_POINTER, tiler) {
                tiler.address = panfrost_batch_get_bifrost_tiler(batch, vertex_count);
        }
        pan_section_pack(fb, MULTI_TARGET_FRAMEBUFFER, BIFROST_PADDING, padding);
}

void
panfrost_attach_mfbd(struct panfrost_batch *batch, unsigned vertex_count)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        void *fb = batch->framebuffer.cpu;

        panfrost_mfbd_emit_local_storage(batch, fb);

        if (pan_is_bifrost(dev))
                return;

        pan_section_pack(fb, MULTI_TARGET_FRAMEBUFFER, PARAMETERS, params) {
                params.width = batch->key.width;
                params.height = batch->key.height;
                params.bound_max_x = batch->key.width - 1;
                params.bound_max_y = batch->key.height - 1;
                params.color_buffer_allocation =
                        pan_internal_cbuf_size(batch, &params.effective_tile_size);
                params.tie_break_rule = MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT;
                params.render_target_count = MAX2(batch->key.nr_cbufs, 1);
                params.sample_count = util_framebuffer_get_num_samples(&batch->key);
                params.sample_pattern = panfrost_sample_pattern(params.sample_count);
        }

        panfrost_mfbd_emit_midgard_tiler(batch, fb, vertex_count);
}

/* Creates an MFBD for the FRAGMENT section of the bound framebuffer */

mali_ptr
panfrost_mfbd_fragment(struct panfrost_batch *batch, bool has_draws)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        unsigned vertex_count = has_draws;
        unsigned zs_crc_count = panfrost_mfbd_has_zs_crc_ext(batch) ? 1 : 0;
        unsigned rt_count = MAX2(batch->key.nr_cbufs, 1);
        
        struct panfrost_ptr t =
                panfrost_pool_alloc_desc_aggregate(&batch->pool,
                                                   PAN_DESC(MULTI_TARGET_FRAMEBUFFER),
                                                   PAN_DESC_ARRAY(zs_crc_count, ZS_CRC_EXTENSION),
                                                   PAN_DESC_ARRAY(rt_count, RENDER_TARGET));
        void *fb = t.cpu, *zs_crc_ext, *rts;

        if (panfrost_mfbd_has_zs_crc_ext(batch)) {
                zs_crc_ext = fb + MALI_MULTI_TARGET_FRAMEBUFFER_LENGTH;
                rts = zs_crc_ext + MALI_ZS_CRC_EXTENSION_LENGTH;
        } else {
                zs_crc_ext = NULL;
                rts = fb + MALI_MULTI_TARGET_FRAMEBUFFER_LENGTH;
        }

        struct panfrost_slice *checksum_slice = NULL;

        if (zs_crc_ext)
                panfrost_mfbd_emit_zs_crc_ext(batch, zs_crc_ext, &checksum_slice);

        /* We always upload at least one dummy GL_NONE render target */

        unsigned rt_descriptors = MAX2(batch->key.nr_cbufs, 1);

        /* Upload either the render target or a dummy GL_NONE target */

        unsigned rt_offset = 0, tib_size;
        unsigned internal_cbuf_size = pan_internal_cbuf_size(batch, &tib_size);

        for (int cb = 0; cb < rt_descriptors; ++cb) {
                struct pipe_surface *surf = batch->key.cbufs[cb];
                void *rt = rts + (cb * MALI_RENDER_TARGET_LENGTH);

                if (!((batch->clear | batch->draws) & (PIPE_CLEAR_COLOR0 << cb)))
                        surf = NULL;

                panfrost_mfbd_emit_rt(batch, rt, surf, rt_offset, cb);

                if (surf) {
                        unsigned samples = MAX2(surf->nr_samples, surf->texture->nr_samples);

                        rt_offset += pan_bytes_per_pixel_tib(surf->format) * tib_size *
                                MAX2(samples, 1);

                        struct panfrost_resource *prsrc = pan_resource(surf->texture);
                        if (!checksum_slice)
                                prsrc->layout.slices[surf->u.tex.level].checksum_valid = false;
                }
        }

        if (pan_is_bifrost(dev))
                panfrost_mfbd_emit_bifrost_parameters(batch, fb);
        else
                panfrost_mfbd_emit_local_storage(batch, fb);

        pan_section_pack(fb, MULTI_TARGET_FRAMEBUFFER, PARAMETERS, params) {
                params.width = batch->key.width;
                params.height = batch->key.height;
                params.bound_max_x = batch->key.width - 1;
                params.bound_max_y = batch->key.height - 1;
                params.effective_tile_size = tib_size;
                params.tie_break_rule = MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT;
                params.render_target_count = rt_descriptors;
                params.z_internal_format = get_z_internal_format(batch);

                if (batch->clear & PIPE_CLEAR_DEPTH)
                        params.z_clear = batch->clear_depth;
                if (batch->clear & PIPE_CLEAR_STENCIL)
                        params.s_clear = batch->clear_stencil & 0xff;

                params.color_buffer_allocation = internal_cbuf_size;

                params.sample_count = util_framebuffer_get_num_samples(&batch->key);
                params.sample_pattern = panfrost_sample_pattern(params.sample_count);

                if (batch->key.zsbuf &&
                    ((batch->clear | batch->draws) & PIPE_CLEAR_DEPTHSTENCIL)) {
                        params.z_write_enable = true;
                        if (batch->key.zsbuf->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)
                                params.s_write_enable = true;
                }

                params.has_zs_crc_extension = !!zs_crc_ext;

                if (checksum_slice) {
                        bool valid = checksum_slice->checksum_valid;
                        bool full = !batch->minx && !batch->miny &&
                                batch->maxx == batch->key.width &&
                                batch->maxy == batch->key.height;

                        params.crc_read_enable = valid;

                        /* If the data is currently invalid, still write CRC
                         * data if we are doing a full write, so that it is
                         * valid for next time. */
                        params.crc_write_enable = valid || full;

                        checksum_slice->checksum_valid |= full;
                }
        }

        if (pan_is_bifrost(dev))
                panfrost_mfbd_emit_bifrost_tiler(batch, fb, vertex_count);
        else
                panfrost_mfbd_emit_midgard_tiler(batch, fb, vertex_count);

        /* Return pointer suitable for the fragment section */
        unsigned tag =
                 MALI_FBD_TAG_IS_MFBD |
                 (zs_crc_ext ? MALI_FBD_TAG_HAS_ZS_RT : 0) |
                 (MALI_POSITIVE(rt_descriptors) << 2);

        return t.gpu | tag;
}
