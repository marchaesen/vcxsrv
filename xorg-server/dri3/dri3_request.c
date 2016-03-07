/*
 * Copyright © 2013 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "dri3_priv.h"
#include <syncsrv.h>
#include <unistd.h>
#include <xace.h>
#include "../Xext/syncsdk.h"
#include <protocol-versions.h>

static int
proc_dri3_query_version(ClientPtr client)
{
    REQUEST(xDRI3QueryVersionReq);
    xDRI3QueryVersionReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = SERVER_DRI3_MAJOR_VERSION,
        .minorVersion = SERVER_DRI3_MINOR_VERSION
    };

    REQUEST_SIZE_MATCH(xDRI3QueryVersionReq);
    (void) stuff;
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.majorVersion);
        swapl(&rep.minorVersion);
    }
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

int
dri3_send_open_reply(ClientPtr client, int fd)
{
    xDRI3OpenReply rep = {
        .type = X_Reply,
        .nfd = 1,
        .sequenceNumber = client->sequence,
        .length = 0,
    };

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }

    if (WriteFdToClient(client, fd, TRUE) < 0) {
        close(fd);
        return BadAlloc;
    }

    WriteToClient(client, sizeof (rep), &rep);

    return Success;
}

static int
proc_dri3_open(ClientPtr client)
{
    REQUEST(xDRI3OpenReq);
    RRProviderPtr provider;
    DrawablePtr drawable;
    ScreenPtr screen;
    int fd;
    int status;

    REQUEST_SIZE_MATCH(xDRI3OpenReq);

    status = dixLookupDrawable(&drawable, stuff->drawable, client, 0, DixReadAccess);
    if (status != Success)
        return status;

    if (stuff->provider == None)
        provider = NULL;
    else if (!RRProviderType) {
        return BadMatch;
    } else {
        VERIFY_RR_PROVIDER(stuff->provider, provider, DixReadAccess);
        if (drawable->pScreen != provider->pScreen)
            return BadMatch;
    }
    screen = drawable->pScreen;

    status = dri3_open(client, screen, provider, &fd);
    if (status != Success)
        return status;

    if (client->ignoreCount == 0)
        return dri3_send_open_reply(client, fd);

    return Success;
}

static int
proc_dri3_pixmap_from_buffer(ClientPtr client)
{
    REQUEST(xDRI3PixmapFromBufferReq);
    int fd;
    DrawablePtr drawable;
    PixmapPtr pixmap;
    int rc;

    SetReqFds(client, 1);
    REQUEST_SIZE_MATCH(xDRI3PixmapFromBufferReq);
    LEGAL_NEW_RESOURCE(stuff->pixmap, client);
    rc = dixLookupDrawable(&drawable, stuff->drawable, client, M_ANY, DixGetAttrAccess);
    if (rc != Success) {
        client->errorValue = stuff->drawable;
        return rc;
    }

    if (!stuff->width || !stuff->height) {
        client->errorValue = 0;
        return BadValue;
    }

    if (stuff->width > 32767 || stuff->height > 32767)
        return BadAlloc;

    if (stuff->depth != 1) {
        DepthPtr depth = drawable->pScreen->allowedDepths;
        int i;
        for (i = 0; i < drawable->pScreen->numDepths; i++, depth++)
            if (depth->depth == stuff->depth)
                break;
        if (i == drawable->pScreen->numDepths) {
            client->errorValue = stuff->depth;
            return BadValue;
        }
    }

    fd = ReadFdFromClient(client);
    if (fd < 0)
        return BadValue;

    rc = dri3_pixmap_from_fd(&pixmap,
                             drawable->pScreen, fd,
                             stuff->width, stuff->height,
                             stuff->stride, stuff->depth,
                             stuff->bpp);
    close (fd);
    if (rc != Success)
        return rc;

    pixmap->drawable.id = stuff->pixmap;

    /* security creation/labeling check */
    rc = XaceHook(XACE_RESOURCE_ACCESS, client, stuff->pixmap, RT_PIXMAP,
                  pixmap, RT_NONE, NULL, DixCreateAccess);

    if (rc != Success) {
        (*drawable->pScreen->DestroyPixmap) (pixmap);
        return rc;
    }
    if (AddResource(stuff->pixmap, RT_PIXMAP, (void *) pixmap))
        return Success;

    return Success;
}

static int
proc_dri3_buffer_from_pixmap(ClientPtr client)
{
    REQUEST(xDRI3BufferFromPixmapReq);
    xDRI3BufferFromPixmapReply rep = {
        .type = X_Reply,
        .nfd = 1,
        .sequenceNumber = client->sequence,
        .length = 0,
    };
    int rc;
    int fd;
    PixmapPtr pixmap;

    REQUEST_SIZE_MATCH(xDRI3BufferFromPixmapReq);
    rc = dixLookupResourceByType((void **) &pixmap, stuff->pixmap, RT_PIXMAP,
                                 client, DixWriteAccess);
    if (rc != Success) {
        client->errorValue = stuff->pixmap;
        return rc;
    }

    rep.width = pixmap->drawable.width;
    rep.height = pixmap->drawable.height;
    rep.depth = pixmap->drawable.depth;
    rep.bpp = pixmap->drawable.bitsPerPixel;

    rc = dri3_fd_from_pixmap(&fd, pixmap, &rep.stride, &rep.size);
    if (rc != Success)
        return rc;

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.size);
        swaps(&rep.width);
        swaps(&rep.height);
        swaps(&rep.stride);
    }
    if (WriteFdToClient(client, fd, TRUE) < 0) {
        close(fd);
        return BadAlloc;
    }

    WriteToClient(client, sizeof(rep), &rep);

    return client->noClientException;
}

static int
proc_dri3_fence_from_fd(ClientPtr client)
{
    REQUEST(xDRI3FenceFromFDReq);
    DrawablePtr drawable;
    int fd;
    int status;

    SetReqFds(client, 1);
    REQUEST_SIZE_MATCH(xDRI3FenceFromFDReq);
    LEGAL_NEW_RESOURCE(stuff->fence, client);

    status = dixLookupDrawable(&drawable, stuff->drawable, client, M_ANY, DixGetAttrAccess);
    if (status != Success)
        return status;

    fd = ReadFdFromClient(client);
    if (fd < 0)
        return BadValue;

    status = SyncCreateFenceFromFD(client, drawable, stuff->fence,
                                   fd, stuff->initially_triggered);

    return status;
}

static int
proc_dri3_fd_from_fence(ClientPtr client)
{
    REQUEST(xDRI3FDFromFenceReq);
    xDRI3FDFromFenceReply rep = {
        .type = X_Reply,
        .nfd = 1,
        .sequenceNumber = client->sequence,
        .length = 0,
    };
    DrawablePtr drawable;
    int fd;
    int status;
    SyncFence *fence;

    REQUEST_SIZE_MATCH(xDRI3FDFromFenceReq);

    status = dixLookupDrawable(&drawable, stuff->drawable, client, M_ANY, DixGetAttrAccess);
    if (status != Success)
        return status;
    status = SyncVerifyFence(&fence, stuff->fence, client, DixWriteAccess);
    if (status != Success)
        return status;

    fd = SyncFDFromFence(client, drawable, fence);
    if (fd < 0)
        return BadMatch;

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }
    if (WriteFdToClient(client, fd, FALSE) < 0)
        return BadAlloc;

    WriteToClient(client, sizeof(rep), &rep);

    return client->noClientException;
}

int (*proc_dri3_vector[DRI3NumberRequests]) (ClientPtr) = {
    proc_dri3_query_version,            /* 0 */
    proc_dri3_open,                     /* 1 */
    proc_dri3_pixmap_from_buffer,       /* 2 */
    proc_dri3_buffer_from_pixmap,       /* 3 */
    proc_dri3_fence_from_fd,            /* 4 */
    proc_dri3_fd_from_fence,            /* 5 */
};

int
proc_dri3_dispatch(ClientPtr client)
{
    REQUEST(xReq);
    if (!client->local)
        return BadMatch;
    if (stuff->data >= DRI3NumberRequests || !proc_dri3_vector[stuff->data])
        return BadRequest;
    return (*proc_dri3_vector[stuff->data]) (client);
}

static int
sproc_dri3_query_version(ClientPtr client)
{
    REQUEST(xDRI3QueryVersionReq);
    REQUEST_SIZE_MATCH(xDRI3QueryVersionReq);

    swaps(&stuff->length);
    swapl(&stuff->majorVersion);
    swapl(&stuff->minorVersion);
    return (*proc_dri3_vector[stuff->dri3ReqType]) (client);
}

static int
sproc_dri3_open(ClientPtr client)
{
    REQUEST(xDRI3OpenReq);
    REQUEST_SIZE_MATCH(xDRI3OpenReq);

    swaps(&stuff->length);
    swapl(&stuff->drawable);
    swapl(&stuff->provider);
    return (*proc_dri3_vector[stuff->dri3ReqType]) (client);
}

static int
sproc_dri3_pixmap_from_buffer(ClientPtr client)
{
    REQUEST(xDRI3PixmapFromBufferReq);
    REQUEST_SIZE_MATCH(xDRI3PixmapFromBufferReq);

    swaps(&stuff->length);
    swapl(&stuff->pixmap);
    swapl(&stuff->drawable);
    swapl(&stuff->size);
    swaps(&stuff->width);
    swaps(&stuff->height);
    swaps(&stuff->stride);
    return (*proc_dri3_vector[stuff->dri3ReqType]) (client);
}

static int
sproc_dri3_buffer_from_pixmap(ClientPtr client)
{
    REQUEST(xDRI3BufferFromPixmapReq);
    REQUEST_SIZE_MATCH(xDRI3BufferFromPixmapReq);

    swaps(&stuff->length);
    swapl(&stuff->pixmap);
    return (*proc_dri3_vector[stuff->dri3ReqType]) (client);
}

static int
sproc_dri3_fence_from_fd(ClientPtr client)
{
    REQUEST(xDRI3FenceFromFDReq);
    REQUEST_SIZE_MATCH(xDRI3FenceFromFDReq);

    swaps(&stuff->length);
    swapl(&stuff->drawable);
    swapl(&stuff->fence);
    return (*proc_dri3_vector[stuff->dri3ReqType]) (client);
}

static int
sproc_dri3_fd_from_fence(ClientPtr client)
{
    REQUEST(xDRI3FDFromFenceReq);
    REQUEST_SIZE_MATCH(xDRI3FDFromFenceReq);

    swaps(&stuff->length);
    swapl(&stuff->drawable);
    swapl(&stuff->fence);
    return (*proc_dri3_vector[stuff->dri3ReqType]) (client);
}

int (*sproc_dri3_vector[DRI3NumberRequests]) (ClientPtr) = {
    sproc_dri3_query_version,           /* 0 */
    sproc_dri3_open,                    /* 1 */
    sproc_dri3_pixmap_from_buffer,      /* 2 */
    sproc_dri3_buffer_from_pixmap,      /* 3 */
    sproc_dri3_fence_from_fd,           /* 4 */
    sproc_dri3_fd_from_fence,           /* 5 */
};

int
sproc_dri3_dispatch(ClientPtr client)
{
    REQUEST(xReq);
    if (!client->local)
        return BadMatch;
    if (stuff->data >= DRI3NumberRequests || !sproc_dri3_vector[stuff->data])
        return BadRequest;
    return (*sproc_dri3_vector[stuff->data]) (client);
}
