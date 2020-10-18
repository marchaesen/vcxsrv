/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 * Copyright 2015 Patrick Rudolph <siro@das-labor.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include "buffer9.h"
#include "device9.h"
#include "nine_buffer_upload.h"
#include "nine_helpers.h"
#include "nine_pipe.h"

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "util/u_box.h"
#include "util/u_inlines.h"

#define DBG_CHANNEL (DBG_INDEXBUFFER|DBG_VERTEXBUFFER)

HRESULT
NineBuffer9_ctor( struct NineBuffer9 *This,
                        struct NineUnknownParams *pParams,
                        D3DRESOURCETYPE Type,
                        DWORD Usage,
                        UINT Size,
                        D3DPOOL Pool )
{
    struct pipe_resource *info = &This->base.info;
    HRESULT hr;

    DBG("This=%p Size=0x%x Usage=%x Pool=%u\n", This, Size, Usage, Pool);

    user_assert(Pool != D3DPOOL_SCRATCH, D3DERR_INVALIDCALL);

    This->maps = MALLOC(sizeof(struct NineTransfer));
    if (!This->maps)
        return E_OUTOFMEMORY;
    This->nmaps = 0;
    This->maxmaps = 1;
    This->size = Size;

    info->screen = pParams->device->screen;
    info->target = PIPE_BUFFER;
    info->format = PIPE_FORMAT_R8_UNORM;
    info->width0 = Size;
    info->flags = 0;

    /* Note: WRITEONLY is just tip for resource placement, the resource
     * can still be read (but slower). */
    info->bind = PIPE_BIND_VERTEX_BUFFER;

    /* It is hard to find clear information on where to place the buffer in
     * memory depending on the flag.
     * MSDN: resources are static, except for those with DYNAMIC, thus why you
     *   can only use DISCARD on them.
     * ATI doc: The driver has the liberty it wants for having things static
     *   or not.
     *   MANAGED: Ram + uploads to Vram copy at unlock (msdn and nvidia doc say
     *   at first draw call using the buffer)
     *   DEFAULT + Usage = 0 => System memory backing for easy read access
     *   (That doc is very unclear on the details, like whether some copies to
     *   vram copy are involved or not).
     *   DEFAULT + WRITEONLY => Vram
     *   DEFAULT + WRITEONLY + DYNAMIC => Either Vram buffer or GTT_WC, depending on what the driver wants.
     */
    if (Pool == D3DPOOL_SYSTEMMEM)
        info->usage = PIPE_USAGE_STAGING;
    else if (Pool == D3DPOOL_MANAGED)
        info->usage = PIPE_USAGE_DEFAULT;
    else if (Usage & D3DUSAGE_DYNAMIC && Usage & D3DUSAGE_WRITEONLY)
        info->usage = PIPE_USAGE_STREAM;
    else if (Usage & D3DUSAGE_WRITEONLY)
        info->usage = PIPE_USAGE_DEFAULT;
    /* For the remaining two, PIPE_USAGE_STAGING would probably be
     * a good fit according to the doc. However it seems rather a mistake
     * from apps to use these (mistakes that do really happen). Try
     * to put the flags that are the best compromise between the real
     * behaviour and what buggy apps should get for better performance. */
    else if (Usage & D3DUSAGE_DYNAMIC)
        info->usage = PIPE_USAGE_STREAM;
    else
        info->usage = PIPE_USAGE_DYNAMIC;

    /* When Writeonly is not set, we don't want to enable the
     * optimizations */
    This->discard_nooverwrite_only = !!(Usage & D3DUSAGE_WRITEONLY) &&
                                     pParams->device->buffer_upload;
    /* if (pDesc->Usage & D3DUSAGE_DONOTCLIP) { } */
    /* if (pDesc->Usage & D3DUSAGE_NONSECURE) { } */
    /* if (pDesc->Usage & D3DUSAGE_NPATCHES) { } */
    /* if (pDesc->Usage & D3DUSAGE_POINTS) { } */
    /* if (pDesc->Usage & D3DUSAGE_RTPATCHES) { } */
    /* The buffer must be usable with both sw and hw
     * vertex processing. It is expected to be slower with hw. */
    if (Usage & D3DUSAGE_SOFTWAREPROCESSING)
        info->usage = PIPE_USAGE_STAGING;
    /* if (pDesc->Usage & D3DUSAGE_TEXTAPI) { } */

    info->height0 = 1;
    info->depth0 = 1;
    info->array_size = 1;
    info->last_level = 0;
    info->nr_samples = 0;
    info->nr_storage_samples = 0;

    hr = NineResource9_ctor(&This->base, pParams, NULL, TRUE,
                            Type, Pool, Usage);

    if (FAILED(hr))
        return hr;

    if (Pool == D3DPOOL_MANAGED) {
        This->managed.data = align_calloc(
            nine_format_get_level_alloc_size(This->base.info.format,
                                             Size, 1, 0), 32);
        if (!This->managed.data)
            return E_OUTOFMEMORY;
        memset(This->managed.data, 0, Size);
        This->managed.dirty = TRUE;
        u_box_1d(0, Size, &This->managed.dirty_box);
        list_inithead(&This->managed.list);
        list_inithead(&This->managed.list2);
        list_add(&This->managed.list2, &pParams->device->managed_buffers);
    }

    return D3D_OK;
}

void
NineBuffer9_dtor( struct NineBuffer9 *This )
{
    DBG("This=%p\n", This);

    if (This->maps) {
        while (This->nmaps) {
            NineBuffer9_Unlock(This);
        }
        FREE(This->maps);
    }

    if (This->base.pool == D3DPOOL_MANAGED) {
        if (This->managed.data)
            align_free(This->managed.data);
        if (This->managed.list.prev != NULL && This->managed.list.next != NULL)
            list_del(&This->managed.list);
        if (This->managed.list2.prev != NULL && This->managed.list2.next != NULL)
            list_del(&This->managed.list2);
    }

    if (This->buf)
        nine_upload_release_buffer(This->base.base.device->buffer_upload, This->buf);

    NineResource9_dtor(&This->base);
}

struct pipe_resource *
NineBuffer9_GetResource( struct NineBuffer9 *This, unsigned *offset )
{
    if (This->buf)
        return nine_upload_buffer_resource_and_offset(This->buf, offset);
    *offset = 0;
    return NineResource9_GetResource(&This->base);
}

static void
NineBuffer9_RebindIfRequired( struct NineBuffer9 *This,
                              struct NineDevice9 *device )
{
    int i;

    if (!This->bind_count)
        return;
    for (i = 0; i < device->caps.MaxStreams; i++) {
        if (device->state.stream[i] == (struct NineVertexBuffer9 *)This)
            nine_context_set_stream_source(device, i,
                                           (struct NineVertexBuffer9 *)This,
                                           device->state.vtxbuf[i].buffer_offset,
                                           device->state.vtxbuf[i].stride);
    }
    if (device->state.idxbuf == (struct NineIndexBuffer9 *)This)
        nine_context_set_indices(device, (struct NineIndexBuffer9 *)This);
}

HRESULT NINE_WINAPI
NineBuffer9_Lock( struct NineBuffer9 *This,
                        UINT OffsetToLock,
                        UINT SizeToLock,
                        void **ppbData,
                        DWORD Flags )
{
    struct NineDevice9 *device = This->base.base.device;
    struct pipe_box box;
    struct pipe_context *pipe;
    void *data;
    unsigned usage;

    DBG("This=%p(pipe=%p) OffsetToLock=0x%x, SizeToLock=0x%x, Flags=0x%x\n",
        This, This->base.resource,
        OffsetToLock, SizeToLock, Flags);

    user_assert(ppbData, E_POINTER);
    user_assert(!(Flags & ~(D3DLOCK_DISCARD |
                            D3DLOCK_DONOTWAIT |
                            D3DLOCK_NO_DIRTY_UPDATE |
                            D3DLOCK_NOSYSLOCK |
                            D3DLOCK_READONLY |
                            D3DLOCK_NOOVERWRITE)), D3DERR_INVALIDCALL);

    if (SizeToLock == 0) {
        SizeToLock = This->size - OffsetToLock;
        user_warn(OffsetToLock != 0);
    }

    /* Write out of bound seems to have to be taken into account for these.
     * TODO: Do more tests (is it only at buffer first lock ? etc).
     * Since these buffers are supposed to be locked once and never
     * writen again (MANAGED or DYNAMIC is used for the other uses cases),
     * performance should be unaffected. */
    if (!(This->base.usage & D3DUSAGE_DYNAMIC) && This->base.pool != D3DPOOL_MANAGED)
        SizeToLock = This->size - OffsetToLock;

    u_box_1d(OffsetToLock, SizeToLock, &box);

    if (This->base.pool == D3DPOOL_MANAGED) {
        /* READONLY doesn't dirty the buffer */
        /* Tests on Win: READONLY doesn't wait for the upload */
        if (!(Flags & D3DLOCK_READONLY)) {
            if (!This->managed.dirty) {
                assert(list_is_empty(&This->managed.list));
                This->managed.dirty = TRUE;
                This->managed.dirty_box = box;
                if (p_atomic_read(&This->managed.pending_upload))
                    nine_csmt_process(This->base.base.device);
            } else
                u_box_union_2d(&This->managed.dirty_box, &This->managed.dirty_box, &box);
            /* Tests trying to draw while the buffer is locked show that
             * MANAGED buffers are made dirty at Lock time */
            BASEBUF_REGISTER_UPDATE(This);
        }
        *ppbData = (char *)This->managed.data + OffsetToLock;
        DBG("returning pointer %p\n", *ppbData);
        This->nmaps++;
        return D3D_OK;
    }

    /* Driver ddi doc: READONLY is never passed to the device. So it can only
     * have effect on things handled by the driver (MANAGED pool for example).
     * Msdn doc: DISCARD and NOOVERWRITE are only for DYNAMIC.
     * ATI doc: You can use DISCARD and NOOVERWRITE without DYNAMIC.
     * Msdn doc: D3DLOCK_DONOTWAIT is not among the valid flags for buffers.
     * Our tests: On win 7 nvidia, D3DLOCK_DONOTWAIT does return
     * D3DERR_WASSTILLDRAWING if the resource is in use, except for DYNAMIC.
     * Our tests: some apps do use both DISCARD and NOOVERWRITE at the same
     * time. On windows it seems to return different pointer, thus indicating
     * DISCARD is taken into account.
     * Our tests: SYSTEMMEM doesn't DISCARD */

    if (This->base.pool == D3DPOOL_SYSTEMMEM)
        Flags &= ~(D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE);

    if (Flags & D3DLOCK_DISCARD)
        usage = PIPE_MAP_WRITE | PIPE_MAP_DISCARD_WHOLE_RESOURCE;
    else if (Flags & D3DLOCK_NOOVERWRITE)
        usage = PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED;
    else
        /* Do not ask for READ if writeonly and default pool (should be safe enough,
         * as the doc says app shouldn't expect reading to work with writeonly).
         * Ignore for Systemmem as it has special behaviours. */
        usage = ((This->base.usage & D3DUSAGE_WRITEONLY) && This->base.pool == D3DPOOL_DEFAULT) ?
            PIPE_MAP_WRITE :
            PIPE_MAP_READ_WRITE;
    if (Flags & D3DLOCK_DONOTWAIT && !(This->base.usage & D3DUSAGE_DYNAMIC))
        usage |= PIPE_MAP_DONTBLOCK;

    This->discard_nooverwrite_only &= !!(Flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE));

    if (This->nmaps == This->maxmaps) {
        struct NineTransfer *newmaps =
            REALLOC(This->maps, sizeof(struct NineTransfer)*This->maxmaps,
                    sizeof(struct NineTransfer)*(This->maxmaps << 1));
        if (newmaps == NULL)
            return E_OUTOFMEMORY;

        This->maxmaps <<= 1;
        This->maps = newmaps;
    }

    if (This->buf && !This->discard_nooverwrite_only) {
        struct pipe_box src_box;
        unsigned offset;
        struct pipe_resource *src_res;
        DBG("Disabling nine_subbuffer for a buffer having"
            "used a nine_subbuffer buffer\n");
        /* Copy buffer content to the buffer resource, which
         * we will now use.
         * Note: The behaviour may be different from what is expected
         * with double lock. However applications can't really make expectations
         * about double locks, and don't really use them, so that's ok. */
        src_res = nine_upload_buffer_resource_and_offset(This->buf, &offset);
        u_box_1d(offset, This->size, &src_box);

        pipe = NineDevice9_GetPipe(device);
        pipe->resource_copy_region(pipe, This->base.resource, 0, 0, 0, 0,
                                   src_res, 0, &src_box);
        /* Release previous resource */
        if (This->nmaps >= 1)
            This->maps[This->nmaps-1].should_destroy_buf = true;
        else
            nine_upload_release_buffer(device->buffer_upload, This->buf);
        This->buf = NULL;
        /* Rebind buffer */
        NineBuffer9_RebindIfRequired(This, device);
    }

    This->maps[This->nmaps].transfer = NULL;
    This->maps[This->nmaps].is_pipe_secondary = false;
    This->maps[This->nmaps].buf = NULL;
    This->maps[This->nmaps].should_destroy_buf = false;

    if (This->discard_nooverwrite_only) {
        if (This->buf && (Flags & D3DLOCK_DISCARD)) {
            /* Release previous buffer */
            if (This->nmaps >= 1)
                This->maps[This->nmaps-1].should_destroy_buf = true;
            else
                nine_upload_release_buffer(device->buffer_upload, This->buf);
            This->buf = NULL;
        }

        if (!This->buf) {
            This->buf = nine_upload_create_buffer(device->buffer_upload, This->base.info.width0);
            NineBuffer9_RebindIfRequired(This, device);
        }

        if (This->buf) {
            This->maps[This->nmaps].buf = This->buf;
            This->nmaps++;
            *ppbData = nine_upload_buffer_get_map(This->buf) + OffsetToLock;
            return D3D_OK;
        } else {
            /* Fallback to normal path, and don't try again */
            This->discard_nooverwrite_only = false;
        }
    }

    /* Previous mappings may need pending commands to write to the
     * buffer (staging buffer for example). Before a NOOVERWRITE,
     * we thus need a finish, to guarantee any upload is finished.
     * Note for discard_nooverwrite_only we don't need to do this
     * check as neither discard nor nooverwrite have issues there */
    if (This->need_sync_if_nooverwrite && !(Flags & D3DLOCK_DISCARD) &&
        (Flags & D3DLOCK_NOOVERWRITE)) {
        struct pipe_screen *screen = NineDevice9_GetScreen(device);
        struct pipe_fence_handle *fence = NULL;

        pipe = NineDevice9_GetPipe(device);
        pipe->flush(pipe, &fence, 0);
        (void) screen->fence_finish(screen, NULL, fence, PIPE_TIMEOUT_INFINITE);
        screen->fence_reference(screen, &fence, NULL);
    }
    This->need_sync_if_nooverwrite = !(Flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE));

    /* When csmt is active, we want to avoid stalls as much as possible,
     * and thus we want to create a new resource on discard and map it
     * with the secondary pipe, instead of waiting on the main pipe. */
    if (Flags & D3DLOCK_DISCARD && device->csmt_active) {
        struct pipe_screen *screen = NineDevice9_GetScreen(device);
        struct pipe_resource *new_res = nine_resource_create_with_retry(device, screen, &This->base.info);
        if (new_res) {
            /* Use the new resource */
            pipe_resource_reference(&This->base.resource, new_res);
            pipe_resource_reference(&new_res, NULL);
            usage = PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED;
            NineBuffer9_RebindIfRequired(This, device);
            This->maps[This->nmaps].is_pipe_secondary = TRUE;
        }
    } else if (Flags & D3DLOCK_NOOVERWRITE && device->csmt_active)
        This->maps[This->nmaps].is_pipe_secondary = TRUE;

    if (This->maps[This->nmaps].is_pipe_secondary)
        pipe = device->pipe_secondary;
    else
        pipe = NineDevice9_GetPipe(device);

    data = pipe->transfer_map(pipe, This->base.resource, 0,
                              usage, &box, &This->maps[This->nmaps].transfer);

    if (!data) {
        DBG("pipe::transfer_map failed\n"
            " usage = %x\n"
            " box.x = %u\n"
            " box.width = %u\n",
            usage, box.x, box.width);

        if (Flags & D3DLOCK_DONOTWAIT)
            return D3DERR_WASSTILLDRAWING;
        return D3DERR_INVALIDCALL;
    }

    DBG("returning pointer %p\n", data);
    This->nmaps++;
    *ppbData = data;

    return D3D_OK;
}

HRESULT NINE_WINAPI
NineBuffer9_Unlock( struct NineBuffer9 *This )
{
    struct NineDevice9 *device = This->base.base.device;
    struct pipe_context *pipe;
    DBG("This=%p\n", This);

    user_assert(This->nmaps > 0, D3DERR_INVALIDCALL);
    This->nmaps--;
    if (This->base.pool != D3DPOOL_MANAGED) {
        if (!This->maps[This->nmaps].buf) {
            pipe = This->maps[This->nmaps].is_pipe_secondary ?
                device->pipe_secondary :
                nine_context_get_pipe_acquire(device);
            pipe->transfer_unmap(pipe, This->maps[This->nmaps].transfer);
            /* We need to flush in case the driver does implicit copies */
            if (This->maps[This->nmaps].is_pipe_secondary)
                pipe->flush(pipe, NULL, 0);
            else
                nine_context_get_pipe_release(device);
        } else if (This->maps[This->nmaps].should_destroy_buf)
            nine_upload_release_buffer(device->buffer_upload, This->maps[This->nmaps].buf);
    }
    return D3D_OK;
}

void
NineBuffer9_SetDirty( struct NineBuffer9 *This )
{
    assert(This->base.pool == D3DPOOL_MANAGED);

    This->managed.dirty = TRUE;
    u_box_1d(0, This->size, &This->managed.dirty_box);
    BASEBUF_REGISTER_UPDATE(This);
}
