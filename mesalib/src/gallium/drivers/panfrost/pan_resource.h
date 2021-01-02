/*
 * © Copyright2018-2019 Alyssa Rosenzweig
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


#ifndef PAN_RESOURCE_H
#define PAN_RESOURCE_H

#include <midgard_pack.h>
#include "pan_screen.h"
#include "pan_pool.h"
#include "pan_minmax_cache.h"
#include "pan_texture.h"
#include "pan_partial_update.h"
#include "drm-uapi/drm.h"
#include "util/u_range.h"

#define LAYOUT_CONVERT_THRESHOLD 8

struct panfrost_resource {
        struct pipe_resource base;
        struct {
                struct pipe_scissor_state extent;
                struct pan_rect *inverted_rects;
                unsigned inverted_len;
        } damage;

        struct panfrost_bo *bo;
        struct renderonly_scanout *scanout;

        struct panfrost_resource *separate_stencil;

        struct util_range valid_buffer_range;

        /* Description of the mip levels */
        struct panfrost_slice slices[MAX_MIP_LEVELS];

        /* Distance from tree to tree */
        unsigned cubemap_stride;

        /* DRM fourcc code: linear, 16x16 u-interleaved, AFBC */
        uint64_t modifier;

        /* Whether the modifier can be changed */
        bool modifier_constant;

        /* Is transaciton elimination enabled? */
        bool checksummed;

        /* Used to decide when to convert to another modifier */
        uint16_t modifier_updates;

        enum pipe_format internal_format;

        /* Cached min/max values for index buffers */
        struct panfrost_minmax_cache *index_cache;
};

static inline struct panfrost_resource *
pan_resource(struct pipe_resource *p)
{
        return (struct panfrost_resource *)p;
}

struct panfrost_transfer {
        struct pipe_transfer base;
        void *map;
        struct {
                struct pipe_resource *rsrc;
                struct pipe_box box;
        } staging;
};

static inline struct panfrost_transfer *
pan_transfer(struct pipe_transfer *p)
{
        return (struct panfrost_transfer *)p;
}

mali_ptr
panfrost_get_texture_address(
        struct panfrost_resource *rsrc,
        unsigned level, unsigned face, unsigned sample);

void panfrost_resource_screen_init(struct pipe_screen *screen);

void panfrost_resource_context_init(struct pipe_context *pctx);

/* Blitting */

void
panfrost_blit(struct pipe_context *pipe,
              const struct pipe_blit_info *info);

void
panfrost_resource_set_damage_region(struct pipe_screen *screen,
                                    struct pipe_resource *res,
                                    unsigned int nrects,
                                    const struct pipe_box *rects);

static inline enum mali_texture_dimension
panfrost_translate_texture_dimension(enum pipe_texture_target t) {
        switch (t)
        {
        case PIPE_BUFFER:
        case PIPE_TEXTURE_1D:
        case PIPE_TEXTURE_1D_ARRAY:
                return MALI_TEXTURE_DIMENSION_1D;

        case PIPE_TEXTURE_2D:
        case PIPE_TEXTURE_2D_ARRAY:
        case PIPE_TEXTURE_RECT:
                return MALI_TEXTURE_DIMENSION_2D;

        case PIPE_TEXTURE_3D:
                return MALI_TEXTURE_DIMENSION_3D;

        case PIPE_TEXTURE_CUBE:
        case PIPE_TEXTURE_CUBE_ARRAY:
                return MALI_TEXTURE_DIMENSION_CUBE;

        default:
                unreachable("Unknown target");
        }
}


#endif /* PAN_RESOURCE_H */
