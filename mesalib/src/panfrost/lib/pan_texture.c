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

#ifndef PAN_ARCH

/* Generates a texture descriptor. Ideally, descriptors are immutable after the
 * texture is created, so we can keep these hanging around in GPU memory in a
 * dedicated BO and not have to worry. In practice there are some minor gotchas
 * with this (the driver sometimes will change the format of a texture on the
 * fly for compression) but it's fast enough to just regenerate the descriptor
 * in those cases, rather than monkeypatching at drawtime. A texture descriptor
 * consists of a 32-byte header followed by pointers. 
 */

/* List of supported modifiers, in descending order of preference. AFBC is
 * faster than u-interleaved tiling which is faster than linear. Within AFBC,
 * enabling the YUV-like transform is typically a win where possible. */

uint64_t pan_best_modifiers[PAN_MODIFIER_COUNT] = {
        DRM_FORMAT_MOD_ARM_AFBC(
                AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
                AFBC_FORMAT_MOD_SPARSE |
                AFBC_FORMAT_MOD_YTR),

        DRM_FORMAT_MOD_ARM_AFBC(
                AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
                AFBC_FORMAT_MOD_SPARSE),

        DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED,
        DRM_FORMAT_MOD_LINEAR
};

/* If not explicitly, line stride is calculated for block-based formats as
 * (ceil(width / block_width) * block_size). As a special case, this is left
 * zero if there is only a single block vertically. So, we have a helper to
 * extract the dimensions of a block-based format and use that to calculate the
 * line stride as such.
 */

unsigned
panfrost_block_dim(uint64_t modifier, bool width, unsigned plane)
{
        if (!drm_is_afbc(modifier)) {
                assert(modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED);
                return 16;
        }

        switch (modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK) {
        case AFBC_FORMAT_MOD_BLOCK_SIZE_16x16:
                return 16;
        case AFBC_FORMAT_MOD_BLOCK_SIZE_32x8:
                return width ? 32 : 8;
        case AFBC_FORMAT_MOD_BLOCK_SIZE_64x4:
                return width ? 64 : 4;
        case AFBC_FORMAT_MOD_BLOCK_SIZE_32x8_64x4:
                return plane ? (width ? 64 : 4) : (width ? 32 : 8);
        default:
                unreachable("Invalid AFBC block size");
        }
}

/* Computes sizes for checksumming, which is 8 bytes per 16x16 tile.
 * Checksumming is believed to be a CRC variant (CRC64 based on the size?).
 * This feature is also known as "transaction elimination". */

#define CHECKSUM_TILE_WIDTH 16
#define CHECKSUM_TILE_HEIGHT 16
#define CHECKSUM_BYTES_PER_TILE 8

unsigned
panfrost_compute_checksum_size(
        struct pan_image_slice_layout *slice,
        unsigned width,
        unsigned height)
{
        unsigned tile_count_x = DIV_ROUND_UP(width, CHECKSUM_TILE_WIDTH);
        unsigned tile_count_y = DIV_ROUND_UP(height, CHECKSUM_TILE_HEIGHT);

        slice->crc.stride = tile_count_x * CHECKSUM_BYTES_PER_TILE;

        return slice->crc.stride * tile_count_y;
}

unsigned
panfrost_get_layer_stride(const struct pan_image_layout *layout,
                          unsigned level)
{
        if (layout->dim != MALI_TEXTURE_DIMENSION_3D)
                return layout->array_stride;
        else if (drm_is_afbc(layout->modifier))
                return layout->slices[level].afbc.surface_stride;
        else
                return layout->slices[level].surface_stride;
}

/* Computes the offset into a texture at a particular level/face. Add to
 * the base address of a texture to get the address to that level/face */

unsigned
panfrost_texture_offset(const struct pan_image_layout *layout,
                        unsigned level, unsigned array_idx,
                        unsigned surface_idx)
{
        return layout->slices[level].offset +
               (array_idx * layout->array_stride) +
               (surface_idx * layout->slices[level].surface_stride);
}

bool
pan_image_layout_init(const struct panfrost_device *dev,
                      struct pan_image_layout *layout,
                      uint64_t modifier,
                      enum pipe_format format,
                      enum mali_texture_dimension dim,
                      unsigned width, unsigned height, unsigned depth,
                      unsigned array_size, unsigned nr_samples,
                      unsigned nr_slices, enum pan_image_crc_mode crc_mode,
                      const struct pan_image_explicit_layout *explicit_layout)
{
        /* Explicit stride only work with non-mipmap, non-array; single-sample
         * 2D image, and in-band CRC can't be used.
         */
        if (explicit_layout &&
	    (depth > 1 || nr_samples > 1 || array_size > 1 ||
             dim != MALI_TEXTURE_DIMENSION_2D || nr_slices > 1 ||
             crc_mode == PAN_IMAGE_CRC_INBAND))
                return false;

        /* Mandate 64 byte alignement */
        if (explicit_layout && (explicit_layout->offset & 63))
                return false;

        layout->crc_mode = crc_mode;
        layout->modifier = modifier;
        layout->format = format;
        layout->dim = dim;
        layout->width = width;
        layout->height = height;
        layout->depth = depth;
        layout->array_size = array_size;
        layout->nr_samples = nr_samples;
        layout->nr_slices = nr_slices;

        unsigned bytes_per_pixel = util_format_get_blocksize(format);

        /* MSAA is implemented as a 3D texture with z corresponding to the
         * sample #, horrifyingly enough */

        assert(depth == 1 || nr_samples == 1);

        bool afbc = drm_is_afbc(layout->modifier);
        bool tiled = layout->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED;
        bool linear = layout->modifier == DRM_FORMAT_MOD_LINEAR;
        bool should_align = tiled || afbc;
        bool is_3d = layout->dim == MALI_TEXTURE_DIMENSION_3D;

        unsigned oob_crc_offset = 0;
        unsigned offset = explicit_layout ? explicit_layout->offset : 0;
        unsigned tile_h = 1, tile_w = 1, tile_shift = 0;

        if (tiled || afbc) {
                tile_w = panfrost_block_dim(layout->modifier, true, 0);
                tile_h = panfrost_block_dim(layout->modifier, false, 0);
                if (util_format_is_compressed(format))
                        tile_shift = 2;
        }

        for (unsigned l = 0; l < nr_slices; ++l) {
                struct pan_image_slice_layout *slice = &layout->slices[l];

                unsigned effective_width = width;
                unsigned effective_height = height;
                unsigned effective_depth = depth;

                if (should_align) {
                        effective_width = ALIGN_POT(effective_width, tile_w) >> tile_shift;
                        effective_height = ALIGN_POT(effective_height, tile_h);

                        /* We don't need to align depth */
                }

                /* Align levels to cache-line as a performance improvement for
                 * linear/tiled and as a requirement for AFBC */

                offset = ALIGN_POT(offset, 64);

                slice->offset = offset;

                /* Compute the would-be stride */
                unsigned stride = bytes_per_pixel * effective_width;

                if (explicit_layout) {
                        /* Make sure the explicit stride is valid */
                        if (explicit_layout->line_stride < stride)
                                return false;

                        stride = explicit_layout->line_stride;
                } else if (linear) {
                        /* Keep lines alignment on 64 byte for performance */
                        stride = ALIGN_POT(stride, 64);
                }

                slice->line_stride = stride;
                slice->row_stride = stride * (tile_h >> tile_shift);

                unsigned slice_one_size = slice->line_stride * effective_height;

                /* Compute AFBC sizes if necessary */
                if (afbc) {
                        slice->afbc.header_size =
                                panfrost_afbc_header_size(width, height);

                        /* Stride between two rows of AFBC headers */
                        slice->afbc.row_stride =
                                (effective_width / tile_w) *
                                AFBC_HEADER_BYTES_PER_TILE;

                        /* AFBC body size */
                        slice->afbc.body_size = slice_one_size;

                        /* 3D AFBC resources have all headers placed at the
                         * beginning instead of having them split per depth
                         * level
                         */
                        if (is_3d) {
                                slice->afbc.surface_stride =
                                        slice->afbc.header_size;
                                slice->afbc.header_size *= effective_depth;
                                slice->afbc.body_size *= effective_depth;
                                offset += slice->afbc.header_size;
                        } else {
                                slice_one_size += slice->afbc.header_size;
                                slice->afbc.surface_stride = slice_one_size;
                        }
                }

                unsigned slice_full_size =
                        slice_one_size * effective_depth * nr_samples;

                slice->surface_stride = slice_one_size;

                /* Compute AFBC sizes if necessary */

                offset += slice_full_size;
                slice->size = slice_full_size;

                /* Add a checksum region if necessary */
                if (crc_mode != PAN_IMAGE_CRC_NONE) {
                        slice->crc.size =
                                panfrost_compute_checksum_size(slice, width, height);

                        if (crc_mode == PAN_IMAGE_CRC_INBAND) {
                                slice->crc.offset = offset;
                                offset += slice->crc.size;
                                slice->size += slice->crc.size;
                        } else {
                                slice->crc.offset = oob_crc_offset;
                                oob_crc_offset += slice->crc.size;
                        }
                }

                width = u_minify(width, 1);
                height = u_minify(height, 1);
                depth = u_minify(depth, 1);
        }

        /* Arrays and cubemaps have the entire miptree duplicated */
        layout->array_stride = ALIGN_POT(offset, 64);
        if (explicit_layout)
                layout->data_size = offset;
        else
                layout->data_size = ALIGN_POT(layout->array_stride * array_size, 4096);
        layout->crc_size = oob_crc_offset;

        return true;
}

void
pan_iview_get_surface(const struct pan_image_view *iview,
                      unsigned level, unsigned layer, unsigned sample,
                      struct pan_surface *surf)
{
        level += iview->first_level;
        assert(level < iview->image->layout.nr_slices);

       layer += iview->first_layer;

        bool is_3d = iview->image->layout.dim == MALI_TEXTURE_DIMENSION_3D;
        const struct pan_image_slice_layout *slice = &iview->image->layout.slices[level];
        mali_ptr base = iview->image->data.bo->ptr.gpu + iview->image->data.offset;

        if (drm_is_afbc(iview->image->layout.modifier)) {
                assert(!sample);

                if (is_3d) {
                        ASSERTED unsigned depth = u_minify(iview->image->layout.depth, level);
                        assert(layer < depth);
                        surf->afbc.header = base + slice->offset +
                                           (layer * slice->afbc.surface_stride);
                        surf->afbc.body = base + slice->offset +
                                          slice->afbc.header_size +
                                          (slice->surface_stride * layer);
                } else {
                        assert(layer < iview->image->layout.array_size);
                        surf->afbc.header = base +
                                            panfrost_texture_offset(&iview->image->layout,
                                                                    level, layer, 0);
                        surf->afbc.body = surf->afbc.header + slice->afbc.header_size;
                }
        } else {
                unsigned array_idx = is_3d ? 0 : layer;
                unsigned surface_idx = is_3d ? layer : sample;

                surf->data = base +
                             panfrost_texture_offset(&iview->image->layout, level,
                                                     array_idx, surface_idx);
        }
}

#else /* ifndef PAN_ARCH */

#if PAN_ARCH >= 5
/* Arm Scalable Texture Compression (ASTC) corresponds to just a few formats.
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

/* Texture addresses are tagged with information about compressed formats.
 * AFBC uses a bit for whether the colorspace transform is enabled (RGB and
 * RGBA only).
 * For ASTC, this is a "stretch factor" encoding the block size. */

static unsigned
panfrost_compression_tag(const struct util_format_description *desc,
                         enum mali_texture_dimension dim,
                         uint64_t modifier)
{
        if (drm_is_afbc(modifier)) {
                unsigned flags = (modifier & AFBC_FORMAT_MOD_YTR) ?
                                 MALI_AFBC_SURFACE_FLAG_YTR : 0;

#if PAN_ARCH >= 6
                /* Prefetch enable */
                flags |= MALI_AFBC_SURFACE_FLAG_PREFETCH;

                /* Wide blocks (> 16x16) */
                if (panfrost_block_dim(modifier, true, 0) > 16)
                        flags |= MALI_AFBC_SURFACE_FLAG_WIDE_BLOCK;

                /* Used to make sure AFBC headers don't point outside the AFBC
                 * body. HW is using the AFBC surface stride to do this check,
                 * which doesn't work for 3D textures because the surface
                 * stride does not cover the body. Only supported on v7+.
                 */
#endif

#if PAN_ARCH >= 7
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
        } else {
                return 0;
        }
}
#endif

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

/* Following the texture descriptor is a number of pointers. How many? */

static unsigned
panfrost_texture_num_elements(
                unsigned first_level, unsigned last_level,
                unsigned first_layer, unsigned last_layer,
                unsigned nr_samples,
                bool is_cube, bool manual_stride)
{
        unsigned first_face  = 0, last_face = 0;

        if (is_cube) {
                panfrost_adjust_cube_dimensions(&first_face, &last_face,
                                &first_layer, &last_layer);
        }

        unsigned levels = 1 + last_level - first_level;
        unsigned layers = 1 + last_layer - first_layer;
        unsigned faces  = 1 + last_face  - first_face;
        unsigned num_elements = levels * layers * faces * MAX2(nr_samples, 1);

        if (manual_stride)
                num_elements *= 2;

        return num_elements;
}

/* Conservative estimate of the size of the texture payload a priori.
 * Average case, size equal to the actual size. Worst case, off by 2x (if
 * a manual stride is not needed on a linear texture). Returned value
 * must be greater than or equal to the actual size, so it's safe to use
 * as an allocation amount */

unsigned
GENX(panfrost_estimate_texture_payload_size)(const struct pan_image_view *iview)
{
        /* Assume worst case */
        unsigned manual_stride = PAN_ARCH >= 6 ||
                                 (iview->image->layout.modifier == DRM_FORMAT_MOD_LINEAR);

        unsigned elements =
                panfrost_texture_num_elements(iview->first_level, iview->last_level,
                                              iview->first_layer, iview->last_layer,
                                              iview->image->layout.nr_samples,
                                              iview->dim == MALI_TEXTURE_DIMENSION_CUBE,
                                              manual_stride);

        return sizeof(mali_ptr) * elements;
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
                *row_stride = PAN_ARCH < 7 ? 0 : slice->afbc.row_stride;
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

static void
panfrost_emit_texture_payload(const struct pan_image_view *iview,
                              enum pipe_format format,
                              bool manual_stride,
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

#if PAN_ARCH >= 5
        /* panfrost_compression_tag() wants the dimension of the resource, not the
         * one of the image view (those might differ).
         */
        base |= panfrost_compression_tag(desc, layout->dim, layout->modifier);
#else
        assert(!drm_is_afbc(layout->modifier) && "no AFBC on v4");
        assert(desc->layout != UTIL_FORMAT_LAYOUT_ASTC && "no ASTC on v4");
#endif

        /* Inject the addresses in, interleaving array indices, mip levels,
         * cube faces, and strides in that order */

        unsigned first_layer = iview->first_layer, last_layer = iview->last_layer;
        unsigned nr_samples = layout->nr_samples;
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

                if (!manual_stride) {
                        pan_pack(payload, SURFACE, cfg) {
                                cfg.pointer = pointer;
                        }
                        payload += pan_size(SURFACE);
                } else {
                        pan_pack(payload, SURFACE_WITH_STRIDE, cfg) {
                                cfg.pointer = pointer;
                                panfrost_get_surface_strides(layout, iter.level,
                                                             &cfg.row_stride,
                                                             &cfg.surface_stride);
                        }
                        payload += pan_size(SURFACE_WITH_STRIDE);
                }
        }
}

/* Check if we need to set a custom stride by computing the "expected"
 * stride and comparing it to what the user actually wants. Only applies
 * to linear textures, since tiled/compressed textures have strict
 * alignment requirements for their strides as it is */

static bool
panfrost_needs_explicit_stride(const struct pan_image_view *iview)
{
        /* Stride is explicit on Bifrost */
        if (PAN_ARCH >= 6)
                return true;

        if (iview->image->layout.modifier != DRM_FORMAT_MOD_LINEAR)
                return false;

        unsigned bytes_per_block = util_format_get_blocksize(iview->format);
        unsigned block_w = util_format_get_blockwidth(iview->format);

        for (unsigned l = iview->first_level; l <= iview->last_level; ++l) {
                unsigned actual = iview->image->layout.slices[l].line_stride;
                unsigned expected =
                        DIV_ROUND_UP(u_minify(iview->image->layout.width, l), block_w) *
                        bytes_per_block;

                if (actual != expected)
                        return true;
        }

        return false;
}

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

void
GENX(panfrost_new_texture)(const struct panfrost_device *dev,
                           const struct pan_image_view *iview,
                           void *out, const struct panfrost_ptr *payload)
{
        const struct pan_image_layout *layout = &iview->image->layout;
        enum pipe_format format = iview->format;
        unsigned swizzle;

        if (PAN_ARCH == 7 && util_format_is_depth_or_stencil(format)) {
                /* v7 doesn't have an _RRRR component order, combine the
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

        bool manual_stride =
                panfrost_needs_explicit_stride(iview);

        panfrost_emit_texture_payload(iview, format,
                                      manual_stride,
                                      payload->cpu);

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
                cfg.texel_ordering =
                        panfrost_modifier_to_layout(layout->modifier);
                cfg.levels = iview->last_level - iview->first_level + 1;
                cfg.array_size = array_size;

#if PAN_ARCH >= 6
                cfg.surfaces = payload->gpu;

                /* We specify API-level LOD clamps in the sampler descriptor
                 * and use these clamps simply for bounds checking */
                cfg.minimum_lod = FIXED_16(0, false);
                cfg.maximum_lod = FIXED_16(cfg.levels - 1, false);
#else
                cfg.manual_stride = manual_stride;
#endif
        }
}
#endif /* ifdef PAN_ARCH */
