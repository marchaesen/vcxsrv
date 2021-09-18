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
 */

#include "pan_context.h"
#include "util/u_vbuf.h"

void
panfrost_analyze_sysvals(struct panfrost_shader_state *ss)
{
        unsigned dirty = 0;
        unsigned dirty_shader =
                PAN_DIRTY_STAGE_RENDERER | PAN_DIRTY_STAGE_CONST;

        for (unsigned i = 0; i < ss->info.sysvals.sysval_count; ++i) {
                switch (PAN_SYSVAL_TYPE(ss->info.sysvals.sysvals[i])) {
                case PAN_SYSVAL_VIEWPORT_SCALE:
                case PAN_SYSVAL_VIEWPORT_OFFSET:
                        dirty |= PAN_DIRTY_VIEWPORT;
                        break;

                case PAN_SYSVAL_TEXTURE_SIZE:
                        dirty_shader |= PAN_DIRTY_STAGE_TEXTURE;
                        break;

                case PAN_SYSVAL_SSBO:
                        dirty_shader |= PAN_DIRTY_STAGE_SSBO;
                        break;

                case PAN_SYSVAL_SAMPLER:
                        dirty_shader |= PAN_DIRTY_STAGE_SAMPLER;
                        break;

                case PAN_SYSVAL_IMAGE_SIZE:
                        dirty_shader |= PAN_DIRTY_STAGE_IMAGE;
                        break;

                case PAN_SYSVAL_NUM_WORK_GROUPS:
                case PAN_SYSVAL_LOCAL_GROUP_SIZE:
                case PAN_SYSVAL_WORK_DIM:
                case PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS:
                        dirty |= PAN_DIRTY_PARAMS;
                        break;

                case PAN_SYSVAL_DRAWID:
                        dirty |= PAN_DIRTY_DRAWID;
                        break;

                case PAN_SYSVAL_SAMPLE_POSITIONS:
                case PAN_SYSVAL_MULTISAMPLED:
                case PAN_SYSVAL_RT_CONVERSION:
                        /* Nothing beyond the batch itself */
                        break;
                default:
                        unreachable("Invalid sysval");
                }
        }

        ss->dirty_3d = dirty;
        ss->dirty_shader = dirty_shader;
}

/* Gets a GPU address for the associated index buffer. Only gauranteed to be
 * good for the duration of the draw (transient), could last longer. Also get
 * the bounds on the index buffer for the range accessed by the draw. We do
 * these operations together because there are natural optimizations which
 * require them to be together. */

mali_ptr
panfrost_get_index_buffer_bounded(struct panfrost_batch *batch,
                                  const struct pipe_draw_info *info,
                                  const struct pipe_draw_start_count_bias *draw,
                                  unsigned *min_index, unsigned *max_index)
{
        struct panfrost_resource *rsrc = pan_resource(info->index.resource);
        struct panfrost_context *ctx = batch->ctx;
        off_t offset = draw->start * info->index_size;
        bool needs_indices = true;
        mali_ptr out = 0;

        if (info->index_bounds_valid) {
                *min_index = info->min_index;
                *max_index = info->max_index;
                needs_indices = false;
        }

        if (!info->has_user_indices) {
                /* Only resources can be directly mapped */
                panfrost_batch_read_rsrc(batch, rsrc, PIPE_SHADER_VERTEX);
                out = rsrc->image.data.bo->ptr.gpu + offset;

                /* Check the cache */
                needs_indices = !panfrost_minmax_cache_get(rsrc->index_cache,
                                                           draw->start,
                                                           draw->count,
                                                           min_index,
                                                           max_index);
        } else {
                /* Otherwise, we need to upload to transient memory */
                const uint8_t *ibuf8 = (const uint8_t *) info->index.user;
                struct panfrost_ptr T =
                        pan_pool_alloc_aligned(&batch->pool.base,
                                               draw->count *
                                               info->index_size,
                                               info->index_size);

                memcpy(T.cpu, ibuf8 + offset, draw->count * info->index_size);
                out = T.gpu;
        }

        if (needs_indices) {
                /* Fallback */
                u_vbuf_get_minmax_index(&ctx->base, info, draw, min_index, max_index);

                if (!info->has_user_indices)
                        panfrost_minmax_cache_add(rsrc->index_cache,
                                                  draw->start, draw->count,
                                                  *min_index, *max_index);
        }

        return out;
}


