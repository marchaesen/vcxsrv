/*
 * Copyright © 2014 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "v3d_query.h"

int
v3d_get_driver_query_group_info(struct pipe_screen *pscreen, unsigned index,
                                struct pipe_driver_query_group_info *info)
{
        struct v3d_screen *screen = v3d_screen(pscreen);

        if (!screen->has_perfmon)
                return 0;

        if (!info)
                return 1;

        if (index > 0)
                return 0;

        info->name = "V3D counters";
        info->max_active_queries = DRM_V3D_MAX_PERF_COUNTERS;
        info->num_queries = screen->perfcnt->max_perfcnt;

        return 1;
}

int
v3d_get_driver_query_info(struct pipe_screen *pscreen, unsigned index,
                          struct pipe_driver_query_info *info)
{
        struct v3d_screen *screen = v3d_screen(pscreen);
        struct v3d_perfcntr_desc *desc;

        if (!screen->has_perfmon)
                return 0;

        if (!info)
                return screen->perfcnt->max_perfcnt;

        desc = v3d_perfcntrs_get_by_index(screen->perfcnt, index);
        if (!desc)
                return 0;

        info->name = desc->name;
        info->group_id = 0;
        info->query_type = PIPE_QUERY_DRIVER_SPECIFIC + index;
        info->result_type = PIPE_DRIVER_QUERY_RESULT_TYPE_CUMULATIVE;
        info->type = PIPE_DRIVER_QUERY_TYPE_UINT64;
        info->flags = PIPE_DRIVER_QUERY_FLAG_BATCH;

        return 1;
}

static struct pipe_query *
v3d_create_query(struct pipe_context *pctx, unsigned query_type, unsigned index)
{
        struct v3d_context *v3d = v3d_context(pctx);

        return v3d_create_query_pipe(v3d, query_type, index);
}

static struct pipe_query *
v3d_create_batch_query(struct pipe_context *pctx, unsigned num_queries,
                       unsigned *query_types)
{
        struct v3d_context *v3d = v3d_context(pctx);

        return v3d_create_batch_query_pipe(v3d, num_queries, query_types);
}

static void
v3d_destroy_query(struct pipe_context *pctx, struct pipe_query *query)
{
        struct v3d_context *v3d = v3d_context(pctx);
        struct v3d_query *q = (struct v3d_query *)query;

        q->funcs->destroy_query(v3d, q);
}

static bool
v3d_begin_query(struct pipe_context *pctx, struct pipe_query *query)
{
        struct v3d_context *v3d = v3d_context(pctx);
        struct v3d_query *q = (struct v3d_query *)query;

        return q->funcs->begin_query(v3d, q);
}

static bool
v3d_end_query(struct pipe_context *pctx, struct pipe_query *query)
{
        struct v3d_context *v3d = v3d_context(pctx);
        struct v3d_query *q = (struct v3d_query *)query;

        return q->funcs->end_query(v3d, q);
}

static bool
v3d_get_query_result(struct pipe_context *pctx, struct pipe_query *query,
                     bool wait, union pipe_query_result *vresult)
{
        struct v3d_context *v3d = v3d_context(pctx);
        struct v3d_query *q = (struct v3d_query *)query;

        return q->funcs->get_query_result(v3d, q, wait, vresult);
}

static void
v3d_set_active_query_state(struct pipe_context *pctx, bool enable)
{
        struct v3d_context *v3d = v3d_context(pctx);

        v3d->active_queries = enable;
        v3d->dirty |= V3D_DIRTY_OQ;
        v3d->dirty |= V3D_DIRTY_STREAMOUT;
}

static void
v3d_render_condition(struct pipe_context *pipe,
                     struct pipe_query *query,
                     bool condition,
                     enum pipe_render_cond_flag mode)
{
        struct v3d_context *v3d = v3d_context(pipe);

        v3d->cond_query = query;
        v3d->cond_cond = condition;
        v3d->cond_mode = mode;
}

static void
extension_set(struct drm_v3d_extension *ext, struct drm_v3d_extension *next,
              uint32_t id, uintptr_t flags)
{
        ext->next = (uintptr_t)(void *)next;
        ext->id = id;
        ext->flags = flags;
}

static struct drm_v3d_sem *
in_syncs_set(struct v3d_context *v3d, uint32_t *count,
             struct v3d_submit_sync_info *sync_info)
{
        uint32_t nsyncs = sync_info->wait_count;

        *count = nsyncs;

        struct drm_v3d_sem *syncs =
             rzalloc_array(v3d, struct drm_v3d_sem, *count);

        if (!syncs) return NULL;

        for (int i = 0; i < nsyncs; i++) {
           syncs[i].handle = sync_info->waits[i];
        }

        assert(*count == nsyncs);

        return syncs;
}

static struct drm_v3d_sem *
out_syncs_set(struct v3d_context *v3d, uint32_t *count,
              struct v3d_submit_sync_info *sync_info)
{
        (*count) = sync_info->signal_count;

        struct drm_v3d_sem *syncs =
             rzalloc_array(v3d, struct drm_v3d_sem, *count);

        if (!syncs) return NULL;

        for (unsigned i = 0; i < *count; i++) {
           syncs[i].handle = sync_info->signals[i];
        }

        return syncs;
}

static void
multisync_set(struct v3d_context *v3d, struct drm_v3d_multi_sync *ms,
              struct v3d_submit_sync_info *sync_info,
              struct drm_v3d_extension *next, uint32_t wait_stage)
{
        uint32_t ocount = 0, icount = 0;
        struct drm_v3d_sem *out_syncs = NULL, *in_syncs = NULL;

        in_syncs = in_syncs_set(v3d, &icount, sync_info);
        if (!in_syncs && icount) goto out;

        out_syncs = out_syncs_set(v3d, &ocount, sync_info);
        if (!out_syncs) goto out;

        extension_set(&ms->base, next, DRM_V3D_EXT_ID_MULTI_SYNC, 0);
        ms->wait_stage = wait_stage;
        ms->out_sync_count = ocount;
        ms->out_syncs = (uintptr_t)(void *)out_syncs;
        ms->in_sync_count = icount;
        ms->in_syncs = (uintptr_t)(void *)in_syncs;

        return;

out:
        fprintf(stderr, "Multisync Set Failed\n");
        if (in_syncs) {
           free(in_syncs);
        }
}

static void
multisync_free(struct drm_v3d_multi_sync *ms)
{
        ralloc_free((void *)(uintptr_t)ms->out_syncs);
        ralloc_free((void *)(uintptr_t)ms->in_syncs);
}

uint64_t
v3d_get_timestamp(struct pipe_context *pctx)
{
        /* Calling glGetInteger64v with GL_TIMESTAMP will return the GPU
         * timestamp when all previously given commands have issued, but not
         * necessarily completed
         */
        v3d_flush(pctx);

        /* Use os_time_get_nano as all of our timestamps come from the CPU clock */
        return os_time_get_nano();
}

void
v3d_submit_timestamp_query(struct pipe_context *pctx, struct v3d_bo *bo,
                           uint32_t sync, uint32_t offset)
{
        struct v3d_context *v3d = v3d_context(pctx);
        struct v3d_screen *screen = v3d->screen;
        int ret;

        /* check for multisync support */
        assert(screen->has_multisync);

        /* check for a valid bo to store the timestamp result */
        assert(bo);

        /* check for a valid syncobj */
        assert(sync);

        struct drm_v3d_timestamp_query timestamp = {0};

        extension_set(&timestamp.base, NULL, DRM_V3D_EXT_ID_CPU_TIMESTAMP_QUERY, 0);

        timestamp.count = 1;
        timestamp.offsets = (uintptr_t)(void *)&offset;
        timestamp.syncs = (uintptr_t)(void *)&sync;

        struct v3d_submit_sync_info sync_info = {
           .wait_count = 1,
           .waits = &v3d->out_sync,
           .signal_count = 1,
           .signals = &v3d->out_sync,
        };

        struct drm_v3d_multi_sync ms = {0};

        multisync_set(v3d, &ms, &sync_info, (void *)&timestamp, V3D_CPU);

        struct drm_v3d_submit_cpu submit = {0};

        submit.bo_handle_count = 1;
        submit.bo_handles = (uintptr_t)(void *)&bo->handle;
        submit.flags |= DRM_V3D_SUBMIT_EXTENSION;
        submit.extensions = (uintptr_t)(void *)&ms;

        ret = v3d_ioctl(screen->fd, DRM_IOCTL_V3D_SUBMIT_CPU, &submit);
        if (ret)
           fprintf(stderr, "Failed to submit cpu job: %s\n", strerror(errno));

        multisync_free(&ms);
}

void
v3d_query_init(struct pipe_context *pctx)
{
        pctx->create_query = v3d_create_query;
        pctx->create_batch_query = v3d_create_batch_query;
        pctx->destroy_query = v3d_destroy_query;
        pctx->begin_query = v3d_begin_query;
        pctx->end_query = v3d_end_query;
        pctx->get_query_result = v3d_get_query_result;
        pctx->set_active_query_state = v3d_set_active_query_state;
        pctx->render_condition = v3d_render_condition;
        pctx->get_timestamp = v3d_get_timestamp;
}
