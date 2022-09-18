/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
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

#include "util/macros.h"
#include "util/u_math.h"
#include "pan_texture.h"

#if PAN_ARCH >= 5
/*
 * Arm Scalable Texture Compression (ASTC) corresponds to just a few formats.
 * The block dimension is not part of the format. Instead, it is encoded as a
 * 6-bit tag on the payload pointer. Map the block size for a single dimension.
 */
static inline enum mali_astc_2d_dimension
panfrost_astc_dim_2d(unsigned dim)
{
        switch (dim) {
        case  4: return MALI_ASTC_2D_DIMENSION_4;
        case  5: return MALI_ASTC_2D_DIMENSION_5;
        case  6: return MALI_ASTC_2D_DIMENSION_6;
        case  8: return MALI_ASTC_2D_DIMENSION_8;
        case 10: return MALI_ASTC_2D_DIMENSION_10;
        case 12: return MALI_ASTC_2D_DIMENSION_12;
        default: unreachable("Invalid ASTC dimension");
        }
}

static inline enum mali_astc_3d_dimension
panfrost_astc_dim_3d(unsigned dim)
{
        switch (dim) {
        case  3: return MALI_ASTC_3D_DIMENSION_3;
        case  4: return MALI_ASTC_3D_DIMENSION_4;
        case  5: return MALI_ASTC_3D_DIMENSION_5;
        case  6: return MALI_ASTC_3D_DIMENSION_6;
        default: unreachable("Invalid ASTC dimension");
        }
}
#endif

/* Texture addresses are tagged with information about compressed formats.
 * AFBC uses a bit for whether the colorspace transform is enabled (RGB and
 * RGBA only).
 * For ASTC, this is a "stretch factor" encoding the block size. */

static unsigned
panfrost_compression_tag(const struct util_format_description *desc,
                         enum mali_texture_dimension dim,
                         uint64_t modifier)
{
#if PAN_ARCH >= 5 && PAN_ARCH <= 8
        if (drm_is_afbc(modifier)) {
                unsigned flags = (modifier & AFBC_FORMAT_MOD_YTR) ?
                                 MALI_AFBC_SURFACE_FLAG_YTR : 0;

#if PAN_ARCH >= 6
                /* Prefetch enable */
                flags |= MALI_AFBC_SURFACE_FLAG_PREFETCH;

                if (panfrost_afbc_is_wide(modifier))
                        flags |= MALI_AFBC_SURFACE_FLAG_WIDE_BLOCK;
#endif

#if PAN_ARCH >= 7
                /* Tiled headers */
                if (modifier & AFBC_FORMAT_MOD_TILED)
                        flags |= MALI_AFBC_SURFACE_FLAG_TILED_HEADER;

                /* Used to make sure AFBC headers don't point outside the AFBC
                 * body. HW is using the AFBC surface stride to do this check,
                 * which doesn't work for 3D textures because the surface
                 * stride does not cover the body. Only supported on v7+.
                 */
                if (dim != MALI_TEXTURE_DIMENSION_3D)
                        flags |= MALI_AFBC_SURFACE_FLAG_CHECK_PAYLOAD_RANGE;
#endif

                return flags;
        } else if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC) {
                if (desc->block.depth > 1) {
                        return (panfrost_astc_dim_3d(desc->block.depth) << 4) |
                               (panfrost_astc_dim_3d(desc->block.height) << 2) |
                                panfrost_astc_dim_3d(desc->block.width);
                } else {
                        return (panfrost_astc_dim_2d(desc->block.height) << 3) |
                                panfrost_astc_dim_2d(desc->block.width);
                }
        }
#endif

        /* Tags are not otherwise used */
        return 0;
}

/* Cubemaps have 6 faces as "layers" in between each actual layer. We
 * need to fix this up. TODO: logic wrong in the asserted out cases ...
 * can they happen, perhaps from cubemap arrays? */

static void
panfrost_adjust_cube_dimensions(
                unsigned *first_face, unsigned *last_face,
                unsigned *first_layer, unsigned *last_layer)
{
        *first_face = *first_layer % 6;
        *last_face = *last_layer % 6;
        *first_layer /= 6;
        *last_layer /= 6;

        assert((*first_layer == *last_layer) || (*first_face == 0 && *last_face == 5));
}

/* Following the texture descriptor is a number of descriptors. How many? */

static unsigned
panfrost_texture_num_elements(
                unsigned first_level, unsigned last_level,
                unsigned first_layer, unsigned last_layer,
                unsigned nr_samples, bool is_cube)
{
        unsigned first_face  = 0, last_face = 0;

        if (is_cube) {
                panfrost_adjust_cube_dimensions(&first_face, &last_face,
                                &first_layer, &last_layer);
        }

        unsigned levels = 1 + last_level - first_level;
        unsigned layers = 1 + last_layer - first_layer;
        unsigned faces  = 1 + last_face  - first_face;

        return levels * layers * faces * MAX2(nr_samples, 1);
}

/* Conservative estimate of the size of the texture payload a priori.
 * Average case, size equal to the actual size. Worst case, off by 2x (if
 * a manual stride is not needed on a linear texture). Returned value
 * must be greater than or equal to the actual size, so it's safe to use
 * as an allocation amount */

unsigned
GENX(panfrost_estimate_texture_payload_size)(const struct pan_image_view *iview)
{
#if PAN_ARCH >= 9
        size_t element_size = pan_size(PLANE);
#else
        /* Assume worst case. Overestimates on Midgard, but that's ok. */
        size_t element_size = pan_size(SURFACE_WITH_STRIDE);
#endif

        unsigned elements =
                panfrost_texture_num_elements(iview->first_level, iview->last_level,
                                              iview->first_layer, iview->last_layer,
                                              iview->image->layout.nr_samples,
                                              iview->dim == MALI_TEXTURE_DIMENSION_CUBE);

        return element_size * elements;
}

struct panfrost_surface_iter {
        unsigned layer, last_layer;
        unsigned level, first_level, last_level;
        unsigned face, first_face, last_face;
        unsigned sample, first_sample, last_sample;
};

static void
panfrost_surface_iter_begin(struct panfrost_surface_iter *iter,
                            unsigned first_layer, unsigned last_layer,
                            unsigned first_level, unsigned last_level,
                            unsigned first_face, unsigned last_face,
                            unsigned nr_samples)
{
        iter->layer = first_layer;
        iter->last_layer = last_layer;
        iter->level = iter->first_level = first_level;
        iter->last_level = last_level;
        iter->face = iter->first_face = first_face;
        iter->last_face = last_face;
        iter->sample = iter->first_sample = 0;
        iter->last_sample = nr_samples - 1;
}

static bool
panfrost_surface_iter_end(const struct panfrost_surface_iter *iter)
{
        return iter->layer > iter->last_layer;
}

static void
panfrost_surface_iter_next(struct panfrost_surface_iter *iter)
{
#define INC_TEST(field) \
        do { \
                if (iter->field++ < iter->last_ ## field) \
                       return; \
                iter->field = iter->first_ ## field; \
        } while (0)

        /* Ordering is different on v7: inner loop is iterating on levels */
        if (PAN_ARCH >= 7)
                INC_TEST(level);

        INC_TEST(sample);
        INC_TEST(face);

        if (PAN_ARCH < 7)
                INC_TEST(level);

        iter->layer++;

#undef INC_TEST
}

static void
panfrost_get_surface_strides(const struct pan_image_layout *layout,
                             unsigned l,
                             int32_t *row_stride, int32_t *surf_stride)
{
        const struct pan_image_slice_layout *slice = &layout->slices[l];

        if (drm_is_afbc(layout->modifier)) {
                /* Pre v7 don't have a row stride field. This field is
                 * repurposed as a Y offset which we don't use */
                *row_stride = PAN_ARCH < 7 ? 0 : slice->row_stride;
                *surf_stride = slice->afbc.surface_stride;
        } else {
                *row_stride = slice->row_stride;
                *surf_stride = slice->surface_stride;
        }
}

static mali_ptr
panfrost_get_surface_pointer(const struct pan_image_layout *layout,
                             enum mali_texture_dimension dim,
                             mali_ptr base,
                             unsigned l, unsigned w, unsigned f, unsigned s)
{
        unsigned face_mult = dim == MALI_TEXTURE_DIMENSION_CUBE ? 6 : 1;
        unsigned offset;

        if (layout->dim == MALI_TEXTURE_DIMENSION_3D) {
                assert(!f && !s);
                offset = layout->slices[l].offset +
                         (w * panfrost_get_layer_stride(layout, l));
        } else {
                offset = panfrost_texture_offset(layout, l, (w * face_mult) + f, s);
        }

        return base + offset;
}

#if PAN_ARCH >= 9

#define CLUMP_FMT(pipe, mali) [PIPE_FORMAT_ ## pipe] = MALI_CLUMP_FORMAT_ ## mali
static enum mali_clump_format special_clump_formats[PIPE_FORMAT_COUNT] = {
        CLUMP_FMT(X32_S8X24_UINT, X32S8X24),
        CLUMP_FMT(X24S8_UINT, X24S8),
        CLUMP_FMT(S8X24_UINT, S8X24),
        CLUMP_FMT(S8_UINT, S8),
        CLUMP_FMT(L4A4_UNORM, L4A4),
        CLUMP_FMT(L8A8_UNORM, L8A8),
        CLUMP_FMT(L8A8_UINT, L8A8),
        CLUMP_FMT(L8A8_SINT, L8A8),
        CLUMP_FMT(A8_UNORM, A8),
        CLUMP_FMT(A8_UINT, A8),
        CLUMP_FMT(A8_SINT, A8),
        CLUMP_FMT(ETC1_RGB8, ETC2_RGB8),
        CLUMP_FMT(ETC2_RGB8, ETC2_RGB8),
        CLUMP_FMT(ETC2_SRGB8, ETC2_RGB8),
        CLUMP_FMT(ETC2_RGB8A1, ETC2_RGB8A1),
        CLUMP_FMT(ETC2_SRGB8A1, ETC2_RGB8A1),
        CLUMP_FMT(ETC2_RGBA8, ETC2_RGBA8),
        CLUMP_FMT(ETC2_SRGBA8, ETC2_RGBA8),
        CLUMP_FMT(ETC2_R11_UNORM, ETC2_R11_UNORM),
        CLUMP_FMT(ETC2_R11_SNORM, ETC2_R11_SNORM),
        CLUMP_FMT(ETC2_RG11_UNORM, ETC2_RG11_UNORM),
        CLUMP_FMT(ETC2_RG11_SNORM, ETC2_RG11_SNORM),
        CLUMP_FMT(DXT1_RGB,                BC1_UNORM),
        CLUMP_FMT(DXT1_RGBA,               BC1_UNORM),
        CLUMP_FMT(DXT1_SRGB,               BC1_UNORM),
        CLUMP_FMT(DXT1_SRGBA,              BC1_UNORM),
        CLUMP_FMT(DXT3_RGBA,               BC2_UNORM),
        CLUMP_FMT(DXT3_SRGBA,              BC2_UNORM),
        CLUMP_FMT(DXT5_RGBA,               BC3_UNORM),
        CLUMP_FMT(DXT5_SRGBA,              BC3_UNORM),
        CLUMP_FMT(RGTC1_UNORM,             BC4_UNORM),
        CLUMP_FMT(RGTC1_SNORM,             BC4_SNORM),
        CLUMP_FMT(RGTC2_UNORM,             BC5_UNORM),
        CLUMP_FMT(RGTC2_SNORM,             BC5_SNORM),
        CLUMP_FMT(BPTC_RGB_FLOAT,          BC6H_SF16),
        CLUMP_FMT(BPTC_RGB_UFLOAT,         BC6H_UF16),
        CLUMP_FMT(BPTC_RGBA_UNORM,         BC7_UNORM),
        CLUMP_FMT(BPTC_SRGBA,              BC7_UNORM),
};
#undef CLUMP_FMT

static enum mali_clump_format
panfrost_clump_format(enum pipe_format format)
{
        /* First, try a special clump format. Note that the 0 encoding is for a
         * raw clump format, which will never be in the special table.
         */
        if (special_clump_formats[format])
                return special_clump_formats[format];

        /* Else, it's a raw format. Raw formats must not be compressed. */
        assert(!util_format_is_compressed(format));

        /* Select the appropriate raw format. */
        switch (util_format_get_blocksize(format)) {
        case  1: return MALI_CLUMP_FORMAT_RAW8;
        case  2: return MALI_CLUMP_FORMAT_RAW16;
        case  3: return MALI_CLUMP_FORMAT_RAW24;
        case  4: return MALI_CLUMP_FORMAT_RAW32;
        case  6: return MALI_CLUMP_FORMAT_RAW48;
        case  8: return MALI_CLUMP_FORMAT_RAW64;
        case 12: return MALI_CLUMP_FORMAT_RAW96;
        case 16: return MALI_CLUMP_FORMAT_RAW128;
        default: unreachable("Invalid bpp");
        }
}

static enum mali_afbc_superblock_size
translate_superblock_size(uint64_t modifier)
{
        assert(drm_is_afbc(modifier));

        switch (modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK) {
        case AFBC_FORMAT_MOD_BLOCK_SIZE_16x16:
                return MALI_AFBC_SUPERBLOCK_SIZE_16X16;
        case AFBC_FORMAT_MOD_BLOCK_SIZE_32x8:
                return MALI_AFBC_SUPERBLOCK_SIZE_32X8;
        case AFBC_FORMAT_MOD_BLOCK_SIZE_64x4:
                return MALI_AFBC_SUPERBLOCK_SIZE_64X4;
        default:
                unreachable("Invalid superblock size");
        }
}

static void
panfrost_emit_plane(const struct pan_image_layout *layout,
                    enum pipe_format format,
                    mali_ptr pointer,
                    unsigned level,
                    void *payload)
{
        const struct util_format_description *desc =
                util_format_description(layout->format);

        int32_t row_stride, surface_stride;

        panfrost_get_surface_strides(layout, level, &row_stride, &surface_stride);
        assert(row_stride >= 0 && surface_stride >= 0 && "negative stride");

        bool afbc = drm_is_afbc(layout->modifier);

        pan_pack(payload, PLANE, cfg) {
                cfg.pointer = pointer;
                cfg.row_stride = row_stride;
                cfg.size = layout->data_size - layout->slices[level].offset;

                cfg.slice_stride = layout->nr_samples ?
                                   layout->slices[level].surface_stride :
                                   panfrost_get_layer_stride(layout, level);

                if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC) {
                        assert(!afbc);

                        if (desc->block.depth > 1) {
                                cfg.plane_type = MALI_PLANE_TYPE_ASTC_3D;
                                cfg.astc._3d.block_width = panfrost_astc_dim_3d(desc->block.width);
                                cfg.astc._3d.block_height = panfrost_astc_dim_3d(desc->block.height);
                                cfg.astc._3d.block_depth = panfrost_astc_dim_3d(desc->block.depth);
                        } else {
                                cfg.plane_type = MALI_PLANE_TYPE_ASTC_2D;
                                cfg.astc._2d.block_width = panfrost_astc_dim_2d(desc->block.width);
                                cfg.astc._2d.block_height = panfrost_astc_dim_2d(desc->block.height);
                        }

                        bool srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);

                        /* Mesa does not advertise _HDR formats yet */
                        cfg.astc.decode_hdr = false;

                        /* sRGB formats decode to RGBA8 sRGB, which is narrow.
                         *
                         * Non-sRGB formats decode to RGBA16F which is wide.
                         * With a future extension, we could decode non-sRGB
                         * formats narrowly too, but this isn't wired up in Mesa
                         * yet.
                         */
                        cfg.astc.decode_wide = !srgb;
                } else if (afbc) {
                        cfg.plane_type = MALI_PLANE_TYPE_AFBC;
                        cfg.afbc.superblock_size = translate_superblock_size(layout->modifier);
                        cfg.afbc.ytr = (layout->modifier & AFBC_FORMAT_MOD_YTR);
                        cfg.afbc.tiled_header = (layout->modifier & AFBC_FORMAT_MOD_TILED);
                        cfg.afbc.prefetch = true;
                        cfg.afbc.compression_mode = pan_afbc_compression_mode(format);
                        cfg.afbc.header_stride = layout->slices[level].afbc.header_size;
                } else {
                        cfg.plane_type = MALI_PLANE_TYPE_GENERIC;
                        cfg.clump_format = panfrost_clump_format(format);
                }

                if (!afbc && layout->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED)
                        cfg.clump_ordering = MALI_CLUMP_ORDERING_TILED_U_INTERLEAVED;
                else if (!afbc)
                        cfg.clump_ordering = MALI_CLUMP_ORDERING_LINEAR;
        }
}
#endif

static void
panfrost_emit_texture_payload(const struct pan_image_view *iview,
                              enum pipe_format format,
                              void *payload)
{
        const struct pan_image_layout *layout = &iview->image->layout;
        ASSERTED const struct util_format_description *desc =
                util_format_description(format);

        mali_ptr base = iview->image->data.bo->ptr.gpu + iview->image->data.offset;

        if (iview->buf.size) {
                assert (iview->dim == MALI_TEXTURE_DIMENSION_1D);
                base += iview->buf.offset;
        }

        /* panfrost_compression_tag() wants the dimension of the resource, not the
         * one of the image view (those might differ).
         */
        base |= panfrost_compression_tag(desc, layout->dim, layout->modifier);

        /* v4 does not support compression */
        assert(PAN_ARCH >= 5 || !drm_is_afbc(layout->modifier));
        assert(PAN_ARCH >= 5 || desc->layout != UTIL_FORMAT_LAYOUT_ASTC);

        /* Inject the addresses in, interleaving array indices, mip levels,
         * cube faces, and strides in that order. On Bifrost and older, each
         * sample had its own surface descriptor; on Valhall, they are fused
         * into a single plane descriptor.
         */

        unsigned first_layer = iview->first_layer, last_layer = iview->last_layer;
        unsigned nr_samples = PAN_ARCH <= 7 ? layout->nr_samples : 1;
        unsigned first_face = 0, last_face = 0;

        if (iview->dim == MALI_TEXTURE_DIMENSION_CUBE) {
                panfrost_adjust_cube_dimensions(&first_face, &last_face,
                                                &first_layer, &last_layer);
        }

        struct panfrost_surface_iter iter;

        for (panfrost_surface_iter_begin(&iter, first_layer, last_layer,
                                         iview->first_level, iview->last_level,
                                         first_face, last_face, nr_samples);
             !panfrost_surface_iter_end(&iter);
             panfrost_surface_iter_next(&iter)) {
                mali_ptr pointer =
                        panfrost_get_surface_pointer(layout, iview->dim, base,
                                                     iter.level, iter.layer,
                                                     iter.face, iter.sample);

#if PAN_ARCH >= 9
                panfrost_emit_plane(layout, format, pointer, iter.level, payload);
                payload += pan_size(PLANE);
#else
                pan_pack(payload, SURFACE_WITH_STRIDE, cfg) {
                        cfg.pointer = pointer;
                        panfrost_get_surface_strides(layout, iter.level,
                                                     &cfg.row_stride,
                                                     &cfg.surface_stride);
                }
                payload += pan_size(SURFACE_WITH_STRIDE);
#endif
        }
}

#if PAN_ARCH <= 7
/* Map modifiers to mali_texture_layout for packing in a texture descriptor */

static enum mali_texture_layout
panfrost_modifier_to_layout(uint64_t modifier)
{
        if (drm_is_afbc(modifier))
                return MALI_TEXTURE_LAYOUT_AFBC;
        else if (modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED)
                return MALI_TEXTURE_LAYOUT_TILED;
        else if (modifier == DRM_FORMAT_MOD_LINEAR)
                return MALI_TEXTURE_LAYOUT_LINEAR;
        else
                unreachable("Invalid modifer");
}
#endif

/*
 * Generates a texture descriptor. Ideally, descriptors are immutable after the
 * texture is created, so we can keep these hanging around in GPU memory in a
 * dedicated BO and not have to worry. In practice there are some minor gotchas
 * with this (the driver sometimes will change the format of a texture on the
 * fly for compression) but it's fast enough to just regenerate the descriptor
 * in those cases, rather than monkeypatching at drawtime. A texture descriptor
 * consists of a 32-byte header followed by pointers.
 */
void
GENX(panfrost_new_texture)(const struct panfrost_device *dev,
                           const struct pan_image_view *iview,
                           void *out, const struct panfrost_ptr *payload)
{
        const struct pan_image_layout *layout = &iview->image->layout;
        enum pipe_format format = iview->format;
        unsigned swizzle;

        if (PAN_ARCH >= 7 && util_format_is_depth_or_stencil(format)) {
                /* v7+ doesn't have an _RRRR component order, combine the
                 * user swizzle with a .XXXX swizzle to emulate that.
                 */
                static const unsigned char replicate_x[4] = {
                        PIPE_SWIZZLE_X, PIPE_SWIZZLE_X,
                        PIPE_SWIZZLE_X, PIPE_SWIZZLE_X,
                };
                unsigned char patched_swizzle[4];

                util_format_compose_swizzles(replicate_x,
                                             iview->swizzle,
                                             patched_swizzle);
                swizzle = panfrost_translate_swizzle_4(patched_swizzle);
        } else {
                swizzle = panfrost_translate_swizzle_4(iview->swizzle);
        }

        panfrost_emit_texture_payload(iview, format, payload->cpu);

        unsigned array_size = iview->last_layer - iview->first_layer + 1;

        if (iview->dim == MALI_TEXTURE_DIMENSION_CUBE) {
                assert(iview->first_layer % 6 == 0);
                assert(iview->last_layer % 6 == 5);
                array_size /=  6;
        }

        unsigned width;

        if (iview->buf.size) {
                assert(iview->dim == MALI_TEXTURE_DIMENSION_1D);
                assert(!iview->first_level && !iview->last_level);
                assert(!iview->first_layer && !iview->last_layer);
                assert(layout->nr_samples == 1);
                assert(layout->height == 1 && layout->depth == 1);
                assert(iview->buf.offset + iview->buf.size <= layout->width);
                width = iview->buf.size;
        } else {
                width = u_minify(layout->width, iview->first_level);
        }

        pan_pack(out, TEXTURE, cfg) {
                cfg.dimension = iview->dim;
                cfg.format = dev->formats[format].hw;
                cfg.width = width;
                cfg.height = u_minify(layout->height, iview->first_level);
                if (iview->dim == MALI_TEXTURE_DIMENSION_3D)
                        cfg.depth = u_minify(layout->depth, iview->first_level);
                else
                        cfg.sample_count = layout->nr_samples;
                cfg.swizzle = swizzle;
#if PAN_ARCH >= 9
                cfg.texel_interleave =
                        (layout->modifier != DRM_FORMAT_MOD_LINEAR) ||
                        util_format_is_compressed(format);
#else
                cfg.texel_ordering =
                        panfrost_modifier_to_layout(layout->modifier);
#endif
                cfg.levels = iview->last_level - iview->first_level + 1;
                cfg.array_size = array_size;

#if PAN_ARCH >= 6
                cfg.surfaces = payload->gpu;

                /* We specify API-level LOD clamps in the sampler descriptor
                 * and use these clamps simply for bounds checking */
                cfg.minimum_lod = FIXED_16(0, false);
                cfg.maximum_lod = FIXED_16(cfg.levels - 1, false);
#else
                cfg.manual_stride = true;
#endif
        }
}
