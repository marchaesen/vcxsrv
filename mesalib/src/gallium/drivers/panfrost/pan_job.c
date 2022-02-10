/*
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright (C) 2019 Alyssa Rosenzweig
 * Copyright (C) 2014-2017 Broadcom
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

#include <assert.h>

#include "drm-uapi/panfrost_drm.h"

#include "pan_bo.h"
#include "pan_context.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/format/u_format.h"
#include "util/u_pack_color.h"
#include "util/rounding.h"
#include "util/u_framebuffer.h"
#include "pan_util.h"
#include "decode.h"

#define foreach_batch(ctx, idx) \
        BITSET_FOREACH_SET(idx, ctx->batches.active, PAN_MAX_BATCHES)

static unsigned
panfrost_batch_idx(struct panfrost_batch *batch)
{
        return batch - batch->ctx->batches.slots;
}

/* Adds the BO backing surface to a batch if the surface is non-null */

static void
panfrost_batch_add_surface(struct panfrost_batch *batch, struct pipe_surface *surf)
{
        if (surf) {
                struct panfrost_resource *rsrc = pan_resource(surf->texture);
                panfrost_batch_write_rsrc(batch, rsrc, PIPE_SHADER_FRAGMENT);
        }
}

static void
panfrost_batch_init(struct panfrost_context *ctx,
                    const struct pipe_framebuffer_state *key,
                    struct panfrost_batch *batch)
{
        struct pipe_screen *pscreen = ctx->base.screen;
        struct panfrost_screen *screen = pan_screen(pscreen);
        struct panfrost_device *dev = &screen->dev;

        batch->ctx = ctx;

        batch->seqnum = ++ctx->batches.seqnum;

        batch->first_bo = INT32_MAX;
        batch->last_bo = INT32_MIN;
        util_sparse_array_init(&batch->bos, sizeof(uint32_t), 64);

        batch->minx = batch->miny = ~0;
        batch->maxx = batch->maxy = 0;

        util_copy_framebuffer_state(&batch->key, key);
        batch->resources =_mesa_set_create(NULL, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);

        /* Preallocate the main pool, since every batch has at least one job
         * structure so it will be used */
        panfrost_pool_init(&batch->pool, NULL, dev, 0, 65536, "Batch pool", true, true);

        /* Don't preallocate the invisible pool, since not every batch will use
         * the pre-allocation, particularly if the varyings are larger than the
         * preallocation and a reallocation is needed after anyway. */
        panfrost_pool_init(&batch->invisible_pool, NULL, dev,
                        PAN_BO_INVISIBLE, 65536, "Varyings", false, true);

        for (unsigned i = 0; i < batch->key.nr_cbufs; ++i)
                panfrost_batch_add_surface(batch, batch->key.cbufs[i]);

        panfrost_batch_add_surface(batch, batch->key.zsbuf);

        screen->vtbl.init_batch(batch);
}

static void
panfrost_batch_cleanup(struct panfrost_context *ctx, struct panfrost_batch *batch)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);

        assert(batch->seqnum);

        if (ctx->batch == batch)
                ctx->batch = NULL;

        unsigned batch_idx = panfrost_batch_idx(batch);

        for (int i = batch->first_bo; i <= batch->last_bo; i++) {
                uint32_t *flags = util_sparse_array_get(&batch->bos, i);

                if (!*flags)
                        continue;

                struct panfrost_bo *bo = pan_lookup_bo(dev, i);
                panfrost_bo_unreference(bo);
        }

        set_foreach_remove(batch->resources, entry) {
                struct panfrost_resource *rsrc = (void *) entry->key;

                if (_mesa_hash_table_search(ctx->writers, rsrc)) {
                        _mesa_hash_table_remove_key(ctx->writers, rsrc);
                        rsrc->track.nr_writers--;
                }

                rsrc->track.nr_users--;

                pipe_resource_reference((struct pipe_resource **) &rsrc, NULL);
        }

        _mesa_set_destroy(batch->resources, NULL);
        panfrost_pool_cleanup(&batch->pool);
        panfrost_pool_cleanup(&batch->invisible_pool);

        util_unreference_framebuffer_state(&batch->key);

        util_sparse_array_finish(&batch->bos);

        memset(batch, 0, sizeof(*batch));
        BITSET_CLEAR(ctx->batches.active, batch_idx);
}

static void
panfrost_batch_submit(struct panfrost_context *ctx,
                      struct panfrost_batch *batch,
                      uint32_t in_sync, uint32_t out_sync);

static struct panfrost_batch *
panfrost_get_batch(struct panfrost_context *ctx,
                   const struct pipe_framebuffer_state *key)
{
        struct panfrost_batch *batch = NULL;

        for (unsigned i = 0; i < PAN_MAX_BATCHES; i++) {
                if (ctx->batches.slots[i].seqnum &&
                    util_framebuffer_state_equal(&ctx->batches.slots[i].key, key)) {
                        /* We found a match, increase the seqnum for the LRU
                         * eviction logic.
                         */
                        ctx->batches.slots[i].seqnum = ++ctx->batches.seqnum;
                        return &ctx->batches.slots[i];
                }

                if (!batch || batch->seqnum > ctx->batches.slots[i].seqnum)
                        batch = &ctx->batches.slots[i];
        }

        assert(batch);

        /* The selected slot is used, we need to flush the batch */
        if (batch->seqnum)
                panfrost_batch_submit(ctx, batch, 0, 0);

        panfrost_batch_init(ctx, key, batch);

        unsigned batch_idx = panfrost_batch_idx(batch);
        BITSET_SET(ctx->batches.active, batch_idx);

        return batch;
}

/* Get the job corresponding to the FBO we're currently rendering into */

struct panfrost_batch *
panfrost_get_batch_for_fbo(struct panfrost_context *ctx)
{
        /* If we already began rendering, use that */

        if (ctx->batch) {
                assert(util_framebuffer_state_equal(&ctx->batch->key,
                                                    &ctx->pipe_framebuffer));
                return ctx->batch;
        }

        /* If not, look up the job */
        struct panfrost_batch *batch = panfrost_get_batch(ctx,
                                                          &ctx->pipe_framebuffer);

        /* Set this job as the current FBO job. Will be reset when updating the
         * FB state and when submitting or releasing a job.
         */
        ctx->batch = batch;
        panfrost_dirty_state_all(ctx);
        return batch;
}

struct panfrost_batch *
panfrost_get_fresh_batch_for_fbo(struct panfrost_context *ctx, const char *reason)
{
        struct panfrost_batch *batch;

        batch = panfrost_get_batch(ctx, &ctx->pipe_framebuffer);
        panfrost_dirty_state_all(ctx);

        /* We only need to submit and get a fresh batch if there is no
         * draw/clear queued. Otherwise we may reuse the batch. */

        if (batch->scoreboard.first_job) {
                perf_debug_ctx(ctx, "Flushing the current FBO due to: %s", reason);
                panfrost_batch_submit(ctx, batch, 0, 0);
                batch = panfrost_get_batch(ctx, &ctx->pipe_framebuffer);
        }

        ctx->batch = batch;
        return batch;
}

static void
panfrost_batch_update_access(struct panfrost_batch *batch,
                             struct panfrost_resource *rsrc, bool writes)
{
        struct panfrost_context *ctx = batch->ctx;
        uint32_t batch_idx = panfrost_batch_idx(batch);
        struct hash_entry *entry = _mesa_hash_table_search(ctx->writers, rsrc);
        struct panfrost_batch *writer = entry ? entry->data : NULL;
        bool found = false;

        _mesa_set_search_or_add(batch->resources, rsrc, &found);

        if (!found) {
                /* Cache number of batches accessing a resource */
                rsrc->track.nr_users++;

                /* Reference the resource on the batch */
                pipe_reference(NULL, &rsrc->base.reference);
        }

        /* Flush users if required */
        if (writes || ((writer != NULL) && (writer != batch))) {
                unsigned i;
                foreach_batch(ctx, i) {
                        struct panfrost_batch *batch = &ctx->batches.slots[i];

                        /* Skip the entry if this our batch. */
                        if (i == batch_idx)
                                continue;

                        /* Submit if it's a user */
                        if (_mesa_set_search(batch->resources, rsrc))
                                panfrost_batch_submit(ctx, batch, 0, 0);
                }
        }

        if (writes) {
                _mesa_hash_table_insert(ctx->writers, rsrc, batch);
                rsrc->track.nr_writers++;
        }
}

static void
panfrost_batch_add_bo_old(struct panfrost_batch *batch,
                struct panfrost_bo *bo, uint32_t flags)
{
        if (!bo)
                return;

        uint32_t *entry = util_sparse_array_get(&batch->bos, bo->gem_handle);
        uint32_t old_flags = *entry;

        if (!old_flags) {
                batch->num_bos++;
                batch->first_bo = MIN2(batch->first_bo, bo->gem_handle);
                batch->last_bo = MAX2(batch->last_bo, bo->gem_handle);
                panfrost_bo_reference(bo);
        }

        if (old_flags == flags)
                return;

        flags |= old_flags;
        *entry = flags;
}

static uint32_t
panfrost_access_for_stage(enum pipe_shader_type stage)
{
        return (stage == PIPE_SHADER_FRAGMENT) ?
                PAN_BO_ACCESS_FRAGMENT : PAN_BO_ACCESS_VERTEX_TILER;
}

void
panfrost_batch_add_bo(struct panfrost_batch *batch,
                struct panfrost_bo *bo, enum pipe_shader_type stage)
{
        panfrost_batch_add_bo_old(batch, bo, PAN_BO_ACCESS_READ |
                        panfrost_access_for_stage(stage));
}

void
panfrost_batch_read_rsrc(struct panfrost_batch *batch,
                         struct panfrost_resource *rsrc,
                         enum pipe_shader_type stage)
{
        uint32_t access = PAN_BO_ACCESS_READ |
                panfrost_access_for_stage(stage);

        panfrost_batch_add_bo_old(batch, rsrc->image.data.bo, access);

        if (rsrc->image.crc.bo)
                panfrost_batch_add_bo_old(batch, rsrc->image.crc.bo, access);

        if (rsrc->separate_stencil)
                panfrost_batch_add_bo_old(batch, rsrc->separate_stencil->image.data.bo, access);

        panfrost_batch_update_access(batch, rsrc, false);
}

void
panfrost_batch_write_rsrc(struct panfrost_batch *batch,
                         struct panfrost_resource *rsrc,
                         enum pipe_shader_type stage)
{
        uint32_t access = PAN_BO_ACCESS_WRITE |
                panfrost_access_for_stage(stage);

        panfrost_batch_add_bo_old(batch, rsrc->image.data.bo, access);

        if (rsrc->image.crc.bo)
                panfrost_batch_add_bo_old(batch, rsrc->image.crc.bo, access);

        if (rsrc->separate_stencil)
                panfrost_batch_add_bo_old(batch, rsrc->separate_stencil->image.data.bo, access);

        panfrost_batch_update_access(batch, rsrc, true);
}

struct panfrost_bo *
panfrost_batch_create_bo(struct panfrost_batch *batch, size_t size,
                         uint32_t create_flags, enum pipe_shader_type stage,
                         const char *label)
{
        struct panfrost_bo *bo;

        bo = panfrost_bo_create(pan_device(batch->ctx->base.screen), size,
                                create_flags, label);
        panfrost_batch_add_bo(batch, bo, stage);

        /* panfrost_batch_add_bo() has retained a reference and
         * panfrost_bo_create() initialize the refcnt to 1, so let's
         * unreference the BO here so it gets released when the batch is
         * destroyed (unless it's retained by someone else in the meantime).
         */
        panfrost_bo_unreference(bo);
        return bo;
}

struct panfrost_bo *
panfrost_batch_get_scratchpad(struct panfrost_batch *batch,
                unsigned size_per_thread,
                unsigned thread_tls_alloc,
                unsigned core_count)
{
        unsigned size = panfrost_get_total_stack_size(size_per_thread,
                        thread_tls_alloc,
                        core_count);

        if (batch->scratchpad) {
                assert(batch->scratchpad->size >= size);
        } else {
                batch->scratchpad = panfrost_batch_create_bo(batch, size,
                                             PAN_BO_INVISIBLE,
                                             PIPE_SHADER_VERTEX,
                                             "Thread local storage");

                panfrost_batch_add_bo(batch, batch->scratchpad,
                                PIPE_SHADER_FRAGMENT);
        }

        return batch->scratchpad;
}

struct panfrost_bo *
panfrost_batch_get_shared_memory(struct panfrost_batch *batch,
                unsigned size,
                unsigned workgroup_count)
{
        if (batch->shared_memory) {
                assert(batch->shared_memory->size >= size);
        } else {
                batch->shared_memory = panfrost_batch_create_bo(batch, size,
                                             PAN_BO_INVISIBLE,
                                             PIPE_SHADER_VERTEX,
                                             "Workgroup shared memory");
        }

        return batch->shared_memory;
}

static void
panfrost_batch_to_fb_info(const struct panfrost_batch *batch,
                          struct pan_fb_info *fb,
                          struct pan_image_view *rts,
                          struct pan_image_view *zs,
                          struct pan_image_view *s,
                          bool reserve)
{
        memset(fb, 0, sizeof(*fb));
        memset(rts, 0, sizeof(*rts) * 8);
        memset(zs, 0, sizeof(*zs));
        memset(s, 0, sizeof(*s));

        fb->width = batch->key.width;
        fb->height = batch->key.height;
        fb->extent.minx = batch->minx;
        fb->extent.miny = batch->miny;
        fb->extent.maxx = batch->maxx - 1;
        fb->extent.maxy = batch->maxy - 1;
        fb->nr_samples = util_framebuffer_get_num_samples(&batch->key);
        fb->rt_count = batch->key.nr_cbufs;

        static const unsigned char id_swz[] = {
                PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W,
        };

        for (unsigned i = 0; i < fb->rt_count; i++) {
                struct pipe_surface *surf = batch->key.cbufs[i];

                if (!surf)
                        continue;

                struct panfrost_resource *prsrc = pan_resource(surf->texture);
                unsigned mask = PIPE_CLEAR_COLOR0 << i;

                if (batch->clear & mask) {
                        fb->rts[i].clear = true;
                        memcpy(fb->rts[i].clear_value, batch->clear_color[i],
                               sizeof((fb->rts[i].clear_value)));
                }

                fb->rts[i].discard = !reserve && !(batch->resolve & mask);

                rts[i].format = surf->format;
                rts[i].dim = MALI_TEXTURE_DIMENSION_2D;
                rts[i].last_level = rts[i].first_level = surf->u.tex.level;
                rts[i].first_layer = surf->u.tex.first_layer;
                rts[i].last_layer = surf->u.tex.last_layer;
                rts[i].image = &prsrc->image;
                rts[i].nr_samples = surf->nr_samples ? : MAX2(surf->texture->nr_samples, 1);
                memcpy(rts[i].swizzle, id_swz, sizeof(rts[i].swizzle));
                fb->rts[i].crc_valid = &prsrc->valid.crc;
                fb->rts[i].view = &rts[i];

                /* Preload if the RT is read or updated */
                if (!(batch->clear & mask) &&
                    ((batch->read & mask) ||
                     ((batch->draws & mask) &&
                      BITSET_TEST(prsrc->valid.data, fb->rts[i].view->first_level))))
                        fb->rts[i].preload = true;

        }

        const struct pan_image_view *s_view = NULL, *z_view = NULL;
        struct panfrost_resource *z_rsrc = NULL, *s_rsrc = NULL;

        if (batch->key.zsbuf) {
                struct pipe_surface *surf = batch->key.zsbuf;
                z_rsrc = pan_resource(surf->texture);

                zs->format = surf->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ?
                             PIPE_FORMAT_Z32_FLOAT : surf->format;
                zs->dim = MALI_TEXTURE_DIMENSION_2D;
                zs->last_level = zs->first_level = surf->u.tex.level;
                zs->first_layer = surf->u.tex.first_layer;
                zs->last_layer = surf->u.tex.last_layer;
                zs->image = &z_rsrc->image;
                zs->nr_samples = surf->nr_samples ? : MAX2(surf->texture->nr_samples, 1);
                memcpy(zs->swizzle, id_swz, sizeof(zs->swizzle));
                fb->zs.view.zs = zs;
                z_view = zs;
                if (util_format_is_depth_and_stencil(zs->format)) {
                        s_view = zs;
                        s_rsrc = z_rsrc;
                }

                if (z_rsrc->separate_stencil) {
                        s_rsrc = z_rsrc->separate_stencil;
                        s->format = PIPE_FORMAT_S8_UINT;
                        s->dim = MALI_TEXTURE_DIMENSION_2D;
                        s->last_level = s->first_level = surf->u.tex.level;
                        s->first_layer = surf->u.tex.first_layer;
                        s->last_layer = surf->u.tex.last_layer;
                        s->image = &s_rsrc->image;
                        s->nr_samples = surf->nr_samples ? : MAX2(surf->texture->nr_samples, 1);
                        memcpy(s->swizzle, id_swz, sizeof(s->swizzle));
                        fb->zs.view.s = s;
                        s_view = s;
                }
        }

        if (batch->clear & PIPE_CLEAR_DEPTH) {
                fb->zs.clear.z = true;
                fb->zs.clear_value.depth = batch->clear_depth;
        }

        if (batch->clear & PIPE_CLEAR_STENCIL) {
                fb->zs.clear.s = true;
                fb->zs.clear_value.stencil = batch->clear_stencil;
        }

        fb->zs.discard.z = !reserve && !(batch->resolve & PIPE_CLEAR_DEPTH);
        fb->zs.discard.s = !reserve && !(batch->resolve & PIPE_CLEAR_STENCIL);

        if (!fb->zs.clear.z &&
            ((batch->read & PIPE_CLEAR_DEPTH) ||
             ((batch->draws & PIPE_CLEAR_DEPTH) &&
              z_rsrc && BITSET_TEST(z_rsrc->valid.data, z_view->first_level))))
                fb->zs.preload.z = true;

        if (!fb->zs.clear.s &&
            ((batch->read & PIPE_CLEAR_STENCIL) ||
             ((batch->draws & PIPE_CLEAR_STENCIL) &&
              s_rsrc && BITSET_TEST(s_rsrc->valid.data, s_view->first_level))))
                fb->zs.preload.s = true;

        /* Preserve both component if we have a combined ZS view and
         * one component needs to be preserved.
         */
        if (s_view == z_view && fb->zs.discard.z != fb->zs.discard.s) {
                bool valid = BITSET_TEST(z_rsrc->valid.data, z_view->first_level);

                fb->zs.discard.z = false;
                fb->zs.discard.s = false;
                fb->zs.preload.z = !fb->zs.clear.z && valid;
                fb->zs.preload.s = !fb->zs.clear.s && valid;
        }
}

static int
panfrost_batch_submit_ioctl(struct panfrost_batch *batch,
                            mali_ptr first_job_desc,
                            uint32_t reqs,
                            uint32_t in_sync,
                            uint32_t out_sync)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_device *dev = pan_device(gallium->screen);
        struct drm_panfrost_submit submit = {0,};
        uint32_t *bo_handles;
        int ret;

        /* If we trace, we always need a syncobj, so make one of our own if we
         * weren't given one to use. Remember that we did so, so we can free it
         * after we're done but preventing double-frees if we were given a
         * syncobj */

        if (!out_sync && dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC))
                out_sync = ctx->syncobj;

        submit.out_sync = out_sync;
        submit.jc = first_job_desc;
        submit.requirements = reqs;
        if (in_sync) {
                submit.in_syncs = (u64)(uintptr_t)(&in_sync);
                submit.in_sync_count = 1;
        }

        bo_handles = calloc(panfrost_pool_num_bos(&batch->pool) +
                            panfrost_pool_num_bos(&batch->invisible_pool) +
                            batch->num_bos + 2,
                            sizeof(*bo_handles));
        assert(bo_handles);

        for (int i = batch->first_bo; i <= batch->last_bo; i++) {
                uint32_t *flags = util_sparse_array_get(&batch->bos, i);

                if (!*flags)
                        continue;

                assert(submit.bo_handle_count < batch->num_bos);
                bo_handles[submit.bo_handle_count++] = i;

                /* Update the BO access flags so that panfrost_bo_wait() knows
                 * about all pending accesses.
                 * We only keep the READ/WRITE info since this is all the BO
                 * wait logic cares about.
                 * We also preserve existing flags as this batch might not
                 * be the first one to access the BO.
                 */
                struct panfrost_bo *bo = pan_lookup_bo(dev, i);

                bo->gpu_access |= *flags & (PAN_BO_ACCESS_RW);
        }

        panfrost_pool_get_bo_handles(&batch->pool, bo_handles + submit.bo_handle_count);
        submit.bo_handle_count += panfrost_pool_num_bos(&batch->pool);
        panfrost_pool_get_bo_handles(&batch->invisible_pool, bo_handles + submit.bo_handle_count);
        submit.bo_handle_count += panfrost_pool_num_bos(&batch->invisible_pool);

        /* Add the tiler heap to the list of accessed BOs if the batch has at
         * least one tiler job. Tiler heap is written by tiler jobs and read
         * by fragment jobs (the polygon list is coming from this heap).
         */
        if (batch->scoreboard.first_tiler)
                bo_handles[submit.bo_handle_count++] = dev->tiler_heap->gem_handle;

        /* Always used on Bifrost, occassionally used on Midgard */
        bo_handles[submit.bo_handle_count++] = dev->sample_positions->gem_handle;

        submit.bo_handles = (u64) (uintptr_t) bo_handles;
        if (ctx->is_noop)
                ret = 0;
        else
                ret = drmIoctl(dev->fd, DRM_IOCTL_PANFROST_SUBMIT, &submit);
        free(bo_handles);

        if (ret)
                return errno;

        /* Trace the job if we're doing that */
        if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC)) {
                /* Wait so we can get errors reported back */
                drmSyncobjWait(dev->fd, &out_sync, 1,
                               INT64_MAX, 0, NULL);

                if (dev->debug & PAN_DBG_TRACE)
                        pandecode_jc(submit.jc, dev->gpu_id);

                /* Jobs won't be complete if blackhole rendering, that's ok */
                if (!ctx->is_noop && dev->debug & PAN_DBG_SYNC)
                        pandecode_abort_on_fault(submit.jc, dev->gpu_id);
        }

        return 0;
}

/* Submit both vertex/tiler and fragment jobs for a batch, possibly with an
 * outsync corresponding to the later of the two (since there will be an
 * implicit dep between them) */

static int
panfrost_batch_submit_jobs(struct panfrost_batch *batch,
                           const struct pan_fb_info *fb,
                           uint32_t in_sync, uint32_t out_sync)
{
        struct pipe_screen *pscreen = batch->ctx->base.screen;
        struct panfrost_screen *screen = pan_screen(pscreen);
        struct panfrost_device *dev = pan_device(pscreen);
        bool has_draws = batch->scoreboard.first_job;
        bool has_tiler = batch->scoreboard.first_tiler;
        bool has_frag = has_tiler || batch->clear;
        int ret = 0;

        /* Take the submit lock to make sure no tiler jobs from other context
         * are inserted between our tiler and fragment jobs, failing to do that
         * might result in tiler heap corruption.
         */
        if (has_tiler)
                pthread_mutex_lock(&dev->submit_lock);

        if (has_draws) {
                ret = panfrost_batch_submit_ioctl(batch, batch->scoreboard.first_job,
                                                  0, in_sync, has_frag ? 0 : out_sync);

                if (ret)
                        goto done;
        }

        if (has_frag) {
                /* Whether we program the fragment job for draws or not depends
                 * on whether there is any *tiler* activity (so fragment
                 * shaders). If there are draws but entirely RASTERIZER_DISCARD
                 * (say, for transform feedback), we want a fragment job that
                 * *only* clears, since otherwise the tiler structures will be
                 * uninitialized leading to faults (or state leaks) */

                mali_ptr fragjob = screen->vtbl.emit_fragment_job(batch, fb);
                ret = panfrost_batch_submit_ioctl(batch, fragjob,
                                                  PANFROST_JD_REQ_FS, 0,
                                                  out_sync);
                if (ret)
                        goto done;
        }

done:
        if (has_tiler)
                pthread_mutex_unlock(&dev->submit_lock);

        return ret;
}

static void
panfrost_emit_tile_map(struct panfrost_batch *batch, struct pan_fb_info *fb)
{
        if (batch->key.nr_cbufs < 1 || !batch->key.cbufs[0])
                return;

        struct pipe_surface *surf = batch->key.cbufs[0];
        struct panfrost_resource *pres = surf ? pan_resource(surf->texture) : NULL;

        if (pres && pres->damage.tile_map.enable) {
                fb->tile_map.base =
                        pan_pool_upload_aligned(&batch->pool.base,
                                                pres->damage.tile_map.data,
                                                pres->damage.tile_map.size,
                                                64);
                fb->tile_map.stride = pres->damage.tile_map.stride;
        }
}

static void
panfrost_batch_submit(struct panfrost_context *ctx,
                      struct panfrost_batch *batch,
                      uint32_t in_sync, uint32_t out_sync)
{
        struct pipe_screen *pscreen = ctx->base.screen;
        struct panfrost_screen *screen = pan_screen(pscreen);
        int ret;

        /* Nothing to do! */
        if (!batch->scoreboard.first_job && !batch->clear)
                goto out;

        struct pan_fb_info fb;
        struct pan_image_view rts[8], zs, s;

        panfrost_batch_to_fb_info(batch, &fb, rts, &zs, &s, false);

        screen->vtbl.preload(batch, &fb);
        screen->vtbl.init_polygon_list(batch);

        /* Now that all draws are in, we can finally prepare the
         * FBD for the batch (if there is one). */

        screen->vtbl.emit_tls(batch);
        panfrost_emit_tile_map(batch, &fb);

        if (batch->scoreboard.first_tiler || batch->clear)
                screen->vtbl.emit_fbd(batch, &fb);

        ret = panfrost_batch_submit_jobs(batch, &fb, in_sync, out_sync);

        if (ret)
                fprintf(stderr, "panfrost_batch_submit failed: %d\n", ret);

        /* We must reset the damage info of our render targets here even
         * though a damage reset normally happens when the DRI layer swaps
         * buffers. That's because there can be implicit flushes the GL
         * app is not aware of, and those might impact the damage region: if
         * part of the damaged portion is drawn during those implicit flushes,
         * you have to reload those areas before next draws are pushed, and
         * since the driver can't easily know what's been modified by the draws
         * it flushed, the easiest solution is to reload everything.
         */
        for (unsigned i = 0; i < batch->key.nr_cbufs; i++) {
                if (!batch->key.cbufs[i])
                        continue;

                panfrost_resource_set_damage_region(ctx->base.screen,
                                                    batch->key.cbufs[i]->texture,
                                                    0, NULL);
        }

out:
        panfrost_batch_cleanup(ctx, batch);
}

/* Submit all batches, applying the out_sync to the currently bound batch */

void
panfrost_flush_all_batches(struct panfrost_context *ctx, const char *reason)
{
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        panfrost_batch_submit(ctx, batch, ctx->syncobj, ctx->syncobj);

        for (unsigned i = 0; i < PAN_MAX_BATCHES; i++) {
                if (ctx->batches.slots[i].seqnum) {
                        if (reason)
                                perf_debug_ctx(ctx, "Flushing everything due to: %s", reason);

                        panfrost_batch_submit(ctx, &ctx->batches.slots[i],
                                              ctx->syncobj, ctx->syncobj);
                }
        }
}

void
panfrost_flush_writer(struct panfrost_context *ctx,
                      struct panfrost_resource *rsrc,
                      const char *reason)
{
        struct hash_entry *entry = _mesa_hash_table_search(ctx->writers, rsrc);

        if (entry) {
                perf_debug_ctx(ctx, "Flushing writer due to: %s", reason);
                panfrost_batch_submit(ctx, entry->data, ctx->syncobj, ctx->syncobj);
        }
}

void
panfrost_flush_batches_accessing_rsrc(struct panfrost_context *ctx,
                                      struct panfrost_resource *rsrc,
                                      const char *reason)
{
        unsigned i;
        foreach_batch(ctx, i) {
                struct panfrost_batch *batch = &ctx->batches.slots[i];

                if (!_mesa_set_search(batch->resources, rsrc))
                        continue;

                perf_debug_ctx(ctx, "Flushing user due to: %s", reason);
                panfrost_batch_submit(ctx, batch, ctx->syncobj, ctx->syncobj);
        }
}

void
panfrost_batch_adjust_stack_size(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;

        for (unsigned i = 0; i < PIPE_SHADER_TYPES; ++i) {
                struct panfrost_shader_state *ss;

                ss = panfrost_get_shader_state(ctx, i);
                if (!ss)
                        continue;

                batch->stack_size = MAX2(batch->stack_size, ss->info.tls_size);
        }
}

void
panfrost_batch_clear(struct panfrost_batch *batch,
                     unsigned buffers,
                     const union pipe_color_union *color,
                     double depth, unsigned stencil)
{
        struct panfrost_context *ctx = batch->ctx;

        if (buffers & PIPE_CLEAR_COLOR) {
                for (unsigned i = 0; i < ctx->pipe_framebuffer.nr_cbufs; ++i) {
                        if (!(buffers & (PIPE_CLEAR_COLOR0 << i)))
                                continue;

                        enum pipe_format format = ctx->pipe_framebuffer.cbufs[i]->format;
                        pan_pack_color(batch->clear_color[i], color, format, false);
                }
        }

        if (buffers & PIPE_CLEAR_DEPTH) {
                batch->clear_depth = depth;
        }

        if (buffers & PIPE_CLEAR_STENCIL) {
                batch->clear_stencil = stencil;
        }

        batch->clear |= buffers;
        batch->resolve |= buffers;

        /* Clearing affects the entire framebuffer (by definition -- this is
         * the Gallium clear callback, which clears the whole framebuffer. If
         * the scissor test were enabled from the GL side, the gallium frontend
         * would emit a quad instead and we wouldn't go down this code path) */

        panfrost_batch_union_scissor(batch, 0, 0,
                                     ctx->pipe_framebuffer.width,
                                     ctx->pipe_framebuffer.height);
}

/* Given a new bounding rectangle (scissor), let the job cover the union of the
 * new and old bounding rectangles */

void
panfrost_batch_union_scissor(struct panfrost_batch *batch,
                             unsigned minx, unsigned miny,
                             unsigned maxx, unsigned maxy)
{
        batch->minx = MIN2(batch->minx, minx);
        batch->miny = MIN2(batch->miny, miny);
        batch->maxx = MAX2(batch->maxx, maxx);
        batch->maxy = MAX2(batch->maxy, maxy);
}

/**
 * Checks if rasterization should be skipped. If not, a TILER job must be
 * created for each draw, or the IDVS flow must be used.
 */
bool
panfrost_batch_skip_rasterization(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_rasterizer_state *rast = (void *) ctx->rasterizer;

        return (rast->rasterizer_discard ||
                batch->scissor_culls_everything);
}
