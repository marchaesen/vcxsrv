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
#include "panfrost-quirks.h"

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

/* Check if we need to set a custom stride by computing the "expected"
 * stride and comparing it to what the user actually wants. Only applies
 * to linear textures, since tiled/compressed textures have strict
 * alignment requirements for their strides as it is */

static bool
panfrost_needs_explicit_stride(const struct panfrost_device *dev,
                               const struct pan_image_layout *layout,
                               enum pipe_format format,
                               uint16_t width,
                               unsigned first_level,
                               unsigned last_level)
{
        /* Stride is explicit on Bifrost */
        if (pan_is_bifrost(dev))
                return true;

        if (layout->modifier != DRM_FORMAT_MOD_LINEAR)
                return false;

        unsigned bytes_per_block = util_format_get_blocksize(format);
        unsigned block_w = util_format_get_blockwidth(format);

        for (unsigned l = first_level; l <= last_level; ++l) {
                unsigned actual = layout->slices[l].line_stride;
                unsigned expected =
                        DIV_ROUND_UP(u_minify(width, l), block_w) *
                        bytes_per_block;

                if (actual != expected)
                        return true;
        }

        return false;
}

/* A Scalable Texture Compression (ASTC) corresponds to just a few texture type
 * in the hardware, but in fact can be parametrized to have various widths and
 * heights for the so-called "stretch factor". It turns out these parameters
 * are stuffed in the bottom bits of the payload pointers. This functions
 * computes these magic stuffing constants based on the ASTC format in use. The
 * constant in a given dimension is 3-bits, and two are stored side-by-side for
 * each active dimension.
 */

static unsigned
panfrost_astc_stretch(unsigned dim)
{
        assert(dim >= 4 && dim <= 12);
        return MIN2(dim, 11) - 4;
}

/* Texture addresses are tagged with information about compressed formats.
 * AFBC uses a bit for whether the colorspace transform is enabled (RGB and
 * RGBA only).
 * For ASTC, this is a "stretch factor" encoding the block size. */

static unsigned
panfrost_compression_tag(const struct panfrost_device *dev,
                         const struct util_format_description *desc,
                         enum mali_texture_dimension dim,
                         uint64_t modifier)
{
        if (drm_is_afbc(modifier)) {
                unsigned flags = (modifier & AFBC_FORMAT_MOD_YTR) ?
                                 MALI_AFBC_SURFACE_FLAG_YTR : 0;

                if (!pan_is_bifrost(dev))
                        return flags;

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
                if (dev->arch >= 7 && dim != MALI_TEXTURE_DIMENSION_3D)
                        flags |= MALI_AFBC_SURFACE_FLAG_CHECK_PAYLOAD_RANGE;

                return flags;
        } else if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC) {
                return (panfrost_astc_stretch(desc->block.height) << 3) |
                        panfrost_astc_stretch(desc->block.width);
        } else {
                return 0;
        }
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
panfrost_estimate_texture_payload_size(const struct panfrost_device *dev,
                                       unsigned first_level,
                                       unsigned last_level,
                                       unsigned first_layer,
                                       unsigned last_layer,
                                       unsigned nr_samples,
                                       enum mali_texture_dimension dim,
                                       uint64_t modifier)
{
        /* Assume worst case */
        unsigned manual_stride = pan_is_bifrost(dev) ||
                                 (modifier == DRM_FORMAT_MOD_LINEAR);

        unsigned elements = panfrost_texture_num_elements(
                        first_level, last_level,
                        first_layer, last_layer,
                        nr_samples,
                        dim == MALI_TEXTURE_DIMENSION_CUBE, manual_stride);

        return sizeof(mali_ptr) * elements;
}

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

static void
panfrost_get_surface_strides(const struct panfrost_device *dev,
                             const struct pan_image_layout *layout,
                             unsigned l,
                             int32_t *row_stride, int32_t *surf_stride)
{
        const struct panfrost_slice *slice = &layout->slices[l];

        if (drm_is_afbc(layout->modifier)) {
                /* Pre v7 don't have a row stride field. This field is
                 * repurposed as a Y offset which we don't use */
                *row_stride = dev->arch < 7 ? 0 : slice->afbc.row_stride;
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

        return base + panfrost_texture_offset(layout, l, w * face_mult + f, s);
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
panfrost_surface_iter_next(const struct panfrost_device *dev,
                           struct panfrost_surface_iter *iter)
{
#define INC_TEST(field) \
        do { \
                if (iter->field++ < iter->last_ ## field) \
                       return; \
                iter->field = iter->first_ ## field; \
        } while (0)

        /* Ordering is different on v7: inner loop is iterating on levels */
        if (dev->arch >= 7)
                INC_TEST(level);

        INC_TEST(sample);
        INC_TEST(face);

        if (dev->arch < 7)
                INC_TEST(level);

        iter->layer++;

#undef INC_TEST
}

static void
panfrost_emit_texture_payload(const struct panfrost_device *dev,
                              const struct pan_image_layout *layout,
                              void *payload,
                              const struct util_format_description *desc,
                              enum mali_texture_dimension dim,
                              unsigned first_level, unsigned last_level,
                              unsigned first_layer, unsigned last_layer,
                              unsigned nr_samples,
                              bool manual_stride,
                              mali_ptr base)
{
        /* panfrost_compression_tag() wants the dimension of the resource, not the
         * one of the image view (those might differ).
         */
        base |= panfrost_compression_tag(dev, desc, layout->dim, layout->modifier);

        /* Inject the addresses in, interleaving array indices, mip levels,
         * cube faces, and strides in that order */

        unsigned first_face  = 0, last_face = 0;

        if (dim == MALI_TEXTURE_DIMENSION_CUBE) {
                panfrost_adjust_cube_dimensions(&first_face, &last_face,
                                                &first_layer, &last_layer);
        }

        nr_samples = MAX2(nr_samples, 1);

        struct panfrost_surface_iter iter;

        for (panfrost_surface_iter_begin(&iter, first_layer, last_layer,
                                         first_level, last_level,
                                         first_face, last_face, nr_samples);
             !panfrost_surface_iter_end(&iter);
             panfrost_surface_iter_next(dev, &iter)) {
                mali_ptr pointer =
                        panfrost_get_surface_pointer(layout, dim, base,
                                                     iter.level, iter.layer,
                                                     iter.face, iter.sample);

                if (!manual_stride) {
                        pan_pack(payload, SURFACE, cfg) {
                                cfg.pointer = pointer;
                        }
                        payload += MALI_SURFACE_LENGTH;
                } else {
                        pan_pack(payload, SURFACE_WITH_STRIDE, cfg) {
                                cfg.pointer = pointer;
                                panfrost_get_surface_strides(dev, layout, iter.level,
                                                             &cfg.row_stride,
                                                             &cfg.surface_stride);
                        }
                        payload += MALI_SURFACE_WITH_STRIDE_LENGTH;
                }
        }
}

void
panfrost_new_texture(const struct panfrost_device *dev,
                     const struct pan_image_layout *layout,
                     void *out,
                     unsigned width, uint16_t height,
                     uint16_t depth, uint16_t array_size,
                     enum pipe_format format,
                     enum mali_texture_dimension dim,
                     unsigned first_level, unsigned last_level,
                     unsigned first_layer, unsigned last_layer,
                     unsigned nr_samples,
                     const unsigned char user_swizzle[4],
                     mali_ptr base,
                     const struct panfrost_ptr *payload)
{
        unsigned swizzle = panfrost_translate_swizzle_4(user_swizzle);

        if (drm_is_afbc(layout->modifier))
                format = panfrost_afbc_format_fixup(dev, format);

        const struct util_format_description *desc =
                util_format_description(format);

        bool manual_stride =
                panfrost_needs_explicit_stride(dev, layout, format, width,
                                               first_level, last_level);

        panfrost_emit_texture_payload(dev, layout,
                                      payload->cpu,
                                      desc, dim,
                                      first_level, last_level,
                                      first_layer, last_layer,
                                      nr_samples,
                                      manual_stride,
                                      base);

        if (pan_is_bifrost(dev)) {
                pan_pack(out, BIFROST_TEXTURE, cfg) {
                        cfg.dimension = dim;
                        cfg.format = dev->formats[format].hw;
                        cfg.width = u_minify(width, first_level);
                        cfg.height = u_minify(height, first_level);
                        if (dim == MALI_TEXTURE_DIMENSION_3D)
                                cfg.depth = u_minify(depth, first_level);
                        else
                                cfg.sample_count = MAX2(nr_samples, 1);
                        cfg.swizzle = swizzle;
                        cfg.texel_ordering =
                                panfrost_modifier_to_layout(layout->modifier);
                        cfg.levels = last_level - first_level + 1;
                        cfg.array_size = array_size;
                        cfg.surfaces = payload->gpu;

                        /* We specify API-level LOD clamps in the sampler descriptor
                         * and use these clamps simply for bounds checking */
                        cfg.minimum_lod = FIXED_16(0, false);
                        cfg.maximum_lod = FIXED_16(cfg.levels - 1, false);
                }
        } else {
                pan_pack(out, MIDGARD_TEXTURE, cfg) {
                        cfg.width = u_minify(width, first_level);
                        cfg.height = u_minify(height, first_level);
                        if (dim == MALI_TEXTURE_DIMENSION_3D)
                                cfg.depth = u_minify(depth, first_level);
                        else
                                cfg.sample_count = MAX2(1, nr_samples);
                        cfg.array_size = array_size;
                        cfg.format = panfrost_pipe_format_v6[format].hw;
                        cfg.dimension = dim;
                        cfg.texel_ordering =
                                panfrost_modifier_to_layout(layout->modifier);
                        cfg.manual_stride = manual_stride;
                        cfg.levels = last_level - first_level + 1;
                        cfg.swizzle = swizzle;
                };
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
        struct panfrost_slice *slice,
        unsigned width,
        unsigned height)
{
        unsigned aligned_width = ALIGN_POT(width, CHECKSUM_TILE_WIDTH);
        unsigned aligned_height = ALIGN_POT(height, CHECKSUM_TILE_HEIGHT);

        unsigned tile_count_x = aligned_width / CHECKSUM_TILE_WIDTH;
        unsigned tile_count_y = aligned_height / CHECKSUM_TILE_HEIGHT;

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
