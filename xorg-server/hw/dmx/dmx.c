/*
 * Copyright 2002-2004 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * This file implements the server-side part of the DMX protocol. A
 * vector of fucntions is provided at extension initialization time, so
 * most all of the useful functions in this file are declared static and
 * do not appear in the doxygen documentation.
 *
 * Much of the low-level work is done by functions in \a dmxextension.c
 *
 * Please see the Client-to-Server DMX Extension to the X Protocol
 * document for details about the protocol.  */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include <X11/X.h>
#include <X11/Xproto.h>
#include "misc.h"
#include "os.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "extinit.h"
#include "opaque.h"

#include "dmx.h"
#include "dmxextension.h"
#include <X11/extensions/dmxproto.h>
#include <X11/extensions/dmx.h>
#include "protocol-versions.h"

#ifdef PANORAMIX
#include "panoramiX.h"
extern unsigned long XRT_WINDOW;
extern int PanoramiXNumScreens;
#endif

static unsigned char DMXCode;

static int
_DMXXineramaActive(void)
{
#ifdef PANORAMIX
    return !noPanoramiXExtension;
#else
    return 0;
#endif
}

static void
dmxSetScreenAttribute(int bit, DMXScreenAttributesPtr attr, CARD32 value)
{
    switch (1 << bit) {
    case DMXScreenWindowWidth:
        attr->screenWindowWidth = value;
        break;
    case DMXScreenWindowHeight:
        attr->screenWindowHeight = value;
        break;
    case DMXScreenWindowXoffset:
        attr->screenWindowXoffset = value;
        break;
    case DMXScreenWindowYoffset:
        attr->screenWindowYoffset = value;
        break;
    case DMXRootWindowWidth:
        attr->rootWindowWidth = value;
        break;
    case DMXRootWindowHeight:
        attr->rootWindowHeight = value;
        break;
    case DMXRootWindowXoffset:
        attr->rootWindowXoffset = value;
        break;
    case DMXRootWindowYoffset:
        attr->rootWindowYoffset = value;
        break;
    case DMXRootWindowXorigin:
        attr->rootWindowXorigin = value;
        break;
    case DMXRootWindowYorigin:
        attr->rootWindowYorigin = value;
        break;
    }
}

static int
dmxFetchScreenAttributes(unsigned int mask,
                         DMXScreenAttributesPtr attr, CARD32 *value_list)
{
    int i;
    CARD32 *value = value_list;
    int count = 0;

    for (i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            dmxSetScreenAttribute(i, attr, *value);
            ++value;
            ++count;
        }
    }
    return count;
}

static void
dmxSetDesktopAttribute(int bit, DMXDesktopAttributesPtr attr, CARD32 value)
{
    switch (1 << bit) {
    case DMXDesktopWidth:
        attr->width = value;
        break;
    case DMXDesktopHeight:
        attr->height = value;
        break;
    case DMXDesktopShiftX:
        attr->shiftX = value;
        break;
    case DMXDesktopShiftY:
        attr->shiftY = value;
        break;
    }
}

static int
dmxFetchDesktopAttributes(unsigned int mask,
                          DMXDesktopAttributesPtr attr, CARD32 *value_list)
{
    int i;
    CARD32 *value = value_list;
    int count = 0;

    for (i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            dmxSetDesktopAttribute(i, attr, *value);
            ++value;
            ++count;
        }
    }
    return count;
}

static void
dmxSetInputAttribute(int bit, DMXInputAttributesPtr attr, CARD32 value)
{
    switch (1 << bit) {
    case DMXInputType:
        attr->inputType = value;
        break;
    case DMXInputPhysicalScreen:
        attr->physicalScreen = value;
        break;
    case DMXInputSendsCore:
        attr->sendsCore = ! !value;
        break;
    }
}

static int
dmxFetchInputAttributes(unsigned int mask,
                        DMXInputAttributesPtr attr, CARD32 *value_list)
{
    int i;
    CARD32 *value = value_list;
    int count = 0;

    for (i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            dmxSetInputAttribute(i, attr, *value);
            ++value;
            ++count;
        }
    }
    return count;
}

static int
ProcDMXQueryVersion(ClientPtr client)
{
    xDMXQueryVersionReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = SERVER_DMX_MAJOR_VERSION,
        .minorVersion = SERVER_DMX_MINOR_VERSION,
        .patchVersion = SERVER_DMX_PATCH_VERSION
    };

    REQUEST_SIZE_MATCH(xDMXQueryVersionReq);

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.majorVersion);
        swapl(&rep.minorVersion);
        swapl(&rep.patchVersion);
    }
    WriteToClient(client, sizeof(xDMXQueryVersionReply), &rep);
    return Success;
}

static int
ProcDMXSync(ClientPtr client)
{
    xDMXSyncReply rep;

    REQUEST_SIZE_MATCH(xDMXSyncReq);

    dmxFlushPendingSyncs();

    rep = (xDMXSyncReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = 0
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
    }
    WriteToClient(client, sizeof(xDMXSyncReply), &rep);
    return Success;
}

static int
ProcDMXForceWindowCreation(ClientPtr client)
{
    xDMXForceWindowCreationReply rep;

    REQUEST(xDMXForceWindowCreationReq);
    WindowPtr pWin;

    REQUEST_SIZE_MATCH(xDMXForceWindowCreationReq);

#ifdef PANORAMIX
    if (!noPanoramiXExtension) {
        PanoramiXRes *win;
        int i;

        if (Success != dixLookupResourceByType((void **) &win,
                                               stuff->window, XRT_WINDOW,
                                               client, DixReadAccess))
            return -1;          /* BadWindow */

        FOR_NSCREENS(i) {
            if (Success != dixLookupWindow(&pWin, win->info[i].id, client,
                                           DixReadAccess))
                return -1;      /* BadWindow */

            dmxForceWindowCreation(pWin);
        }
        goto doreply;
    }
#endif

    if (Success != dixLookupWindow(&pWin, stuff->window, client, DixReadAccess))
        return -1;              /* BadWindow */

    dmxForceWindowCreation(pWin);
 doreply:
    dmxFlushPendingSyncs();
    rep = (xDMXForceWindowCreationReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = 0
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
    }
    WriteToClient(client, sizeof(xDMXForceWindowCreationReply), &rep);
    return Success;
}

static int
ProcDMXGetScreenCount(ClientPtr client)
{
    xDMXGetScreenCountReply rep;

    REQUEST_SIZE_MATCH(xDMXGetScreenCountReq);

    rep = (xDMXGetScreenCountReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .screenCount = dmxGetNumScreens()
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.screenCount);
    }
    WriteToClient(client, sizeof(xDMXGetScreenCountReply), &rep);
    return Success;
}

static int
ProcDMXGetScreenAttributes(ClientPtr client)
{
    REQUEST(xDMXGetScreenAttributesReq);
    xDMXGetScreenAttributesReply rep;
    int length;
    int paddedLength;
    DMXScreenAttributesRec attr;

    REQUEST_SIZE_MATCH(xDMXGetScreenAttributesReq);

    if (stuff->physicalScreen < 0
        || stuff->physicalScreen >= dmxGetNumScreens())
        return BadValue;

    if (!dmxGetScreenAttributes(stuff->physicalScreen, &attr))
        return BadValue;

    length = attr.displayName ? strlen(attr.displayName) : 0;
    paddedLength = pad_to_int32(length);

    rep = (xDMXGetScreenAttributesReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length =
            bytes_to_int32((sizeof(xDMXGetScreenAttributesReply) -
                            sizeof(xGenericReply))
                           + paddedLength),
        .displayNameLength = length,
        .logicalScreen = attr.logicalScreen,
        .screenWindowWidth = attr.screenWindowWidth,
        .screenWindowHeight = attr.screenWindowHeight,
        .screenWindowXoffset = attr.screenWindowXoffset,
        .screenWindowYoffset = attr.screenWindowYoffset,
        .rootWindowWidth = attr.rootWindowWidth,
        .rootWindowHeight = attr.rootWindowHeight,
        .rootWindowXoffset = attr.rootWindowXoffset,
        .rootWindowYoffset = attr.rootWindowYoffset,
        .rootWindowXorigin = attr.rootWindowXorigin,
        .rootWindowYorigin = attr.rootWindowYorigin
    };

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.displayNameLength);
        swapl(&rep.logicalScreen);
        swaps(&rep.screenWindowWidth);
        swaps(&rep.screenWindowHeight);
        swaps(&rep.screenWindowXoffset);
        swaps(&rep.screenWindowYoffset);
        swaps(&rep.rootWindowWidth);
        swaps(&rep.rootWindowHeight);
        swaps(&rep.rootWindowXoffset);
        swaps(&rep.rootWindowYoffset);
        swaps(&rep.rootWindowXorigin);
        swaps(&rep.rootWindowYorigin);
    }
    WriteToClient(client, sizeof(xDMXGetScreenAttributesReply), &rep);
    if (length)
        WriteToClient(client, length, attr.displayName);
    return Success;
}

static int
ProcDMXChangeScreensAttributes(ClientPtr client)
{
    REQUEST(xDMXChangeScreensAttributesReq);
    xDMXChangeScreensAttributesReply rep;
    int status = DMX_BAD_XINERAMA;
    unsigned int mask = 0;
    unsigned int i;
    CARD32 *screen_list;
    CARD32 *mask_list;
    CARD32 *value_list;
    DMXScreenAttributesPtr attribs;
    int errorScreen = 0;
    unsigned int len;
    int ones = 0;

    REQUEST_AT_LEAST_SIZE(xDMXChangeScreensAttributesReq);
    len =
        client->req_len -
        bytes_to_int32(sizeof(xDMXChangeScreensAttributesReq));
    if (len < stuff->screenCount + stuff->maskCount)
        return BadLength;

    screen_list = (CARD32 *) (stuff + 1);
    mask_list = &screen_list[stuff->screenCount];
    value_list = &mask_list[stuff->maskCount];

    for (i = 0; i < stuff->maskCount; i++)
        ones += Ones(mask_list[i]);
    if (len != stuff->screenCount + stuff->maskCount + ones)
        return BadLength;

    if (!_DMXXineramaActive())
        goto noxinerama;

    if (!(attribs = malloc(stuff->screenCount * sizeof(*attribs))))
        return BadAlloc;

    for (i = 0; i < stuff->screenCount; i++) {
        int count;

        if (i < stuff->maskCount)
            mask = mask_list[i];
        dmxGetScreenAttributes(screen_list[i], &attribs[i]);
        count = dmxFetchScreenAttributes(mask, &attribs[i], value_list);
        value_list += count;
    }

#if PANORAMIX
    status = dmxConfigureScreenWindows(stuff->screenCount,
                                       screen_list, attribs, &errorScreen);
#endif

    free(attribs);

    if (status == BadValue)
        return status;

 noxinerama:
    rep = (xDMXChangeScreensAttributesReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = status,
        .errorScreen = errorScreen
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
        swapl(&rep.errorScreen);
    }
    WriteToClient(client, sizeof(xDMXChangeScreensAttributesReply), &rep);
    return Success;
}

static int
ProcDMXAddScreen(ClientPtr client)
{
    REQUEST(xDMXAddScreenReq);
    xDMXAddScreenReply rep;
    int status = 0;
    CARD32 *value_list;
    DMXScreenAttributesRec attr;
    int count;
    char *name;
    int len;
    int paddedLength;

    REQUEST_AT_LEAST_SIZE(xDMXAddScreenReq);
    paddedLength = pad_to_int32(stuff->displayNameLength);
    len = client->req_len - bytes_to_int32(sizeof(xDMXAddScreenReq));
    if (len != Ones(stuff->valueMask) + paddedLength / 4)
        return BadLength;

    memset(&attr, 0, sizeof(attr));
    dmxGetScreenAttributes(stuff->physicalScreen, &attr);
    value_list = (CARD32 *) (stuff + 1);
    count = dmxFetchScreenAttributes(stuff->valueMask, &attr, value_list);

    if (!(name = malloc(stuff->displayNameLength + 1 + 4)))
        return BadAlloc;
    memcpy(name, &value_list[count], stuff->displayNameLength);
    name[stuff->displayNameLength] = '\0';
    attr.displayName = name;

    status = dmxAttachScreen(stuff->physicalScreen, &attr);

    free(name);

    rep = (xDMXAddScreenReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = status,
        .physicalScreen = stuff->physicalScreen
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
        swapl(&rep.physicalScreen);
    }
    WriteToClient(client, sizeof(xDMXAddScreenReply), &rep);
    return Success;
}

static int
ProcDMXRemoveScreen(ClientPtr client)
{
    REQUEST(xDMXRemoveScreenReq);
    xDMXRemoveScreenReply rep;
    int status = 0;

    REQUEST_SIZE_MATCH(xDMXRemoveScreenReq);

    status = dmxDetachScreen(stuff->physicalScreen);

    rep = (xDMXRemoveScreenReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = status
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
    }
    WriteToClient(client, sizeof(xDMXRemoveScreenReply), &rep);
    return Success;
}

#ifdef PANORAMIX
static int
dmxPopulatePanoramiX(ClientPtr client, Window window,
                     CARD32 *screens, CARD32 *windows,
                     xRectangle *pos, xRectangle *vis)
{
    WindowPtr pWin;
    PanoramiXRes *win;
    int i;
    int count = 0;
    DMXWindowAttributesRec attr;

    if (Success != dixLookupResourceByType((void **) &win,
                                           window, XRT_WINDOW,
                                           client, DixReadAccess))
        return -1;              /* BadWindow */

    FOR_NSCREENS(i) {
        if (Success != dixLookupWindow(&pWin, win->info[i].id, client,
                                       DixReadAccess))
            return -1;          /* BadWindow */
        if (dmxGetWindowAttributes(pWin, &attr)) {
            screens[count] = attr.screen;
            windows[count] = attr.window;
            pos[count] = attr.pos;
            vis[count] = attr.vis;
            ++count;            /* Only count existing windows */
        }
    }
    return count;
}
#endif

static int
dmxPopulate(ClientPtr client, Window window, CARD32 *screens,
            CARD32 *windows, xRectangle *pos, xRectangle *vis)
{
    WindowPtr pWin;
    DMXWindowAttributesRec attr;

#ifdef PANORAMIX
    if (!noPanoramiXExtension)
        return dmxPopulatePanoramiX(client, window, screens, windows, pos, vis);
#endif

    if (Success != dixLookupWindow(&pWin, window, client, DixReadAccess))
        return -1;              /* BadWindow */

    dmxGetWindowAttributes(pWin, &attr);
    *screens = attr.screen;
    *windows = attr.window;
    *pos = attr.pos;
    *vis = attr.vis;
    return 1;
}

static int
dmxMaxNumScreens(void)
{
#ifdef PANORAMIX
    if (!noPanoramiXExtension)
        return PanoramiXNumScreens;
#endif
    return 1;
}

static int
ProcDMXGetWindowAttributes(ClientPtr client)
{
    REQUEST(xDMXGetWindowAttributesReq);
    xDMXGetWindowAttributesReply rep;
    int i;
    CARD32 *screens;
    CARD32 *windows;
    xRectangle *pos, *vis;
    int count = dmxMaxNumScreens();

    REQUEST_SIZE_MATCH(xDMXGetWindowAttributesReq);

    if (!(screens = malloc(count * sizeof(*screens))))
        return BadAlloc;
    if (!(windows = malloc(count * sizeof(*windows)))) {
        free(screens);
        return BadAlloc;
    }
    if (!(pos = malloc(count * sizeof(*pos)))) {
        free(windows);
        free(screens);
        return BadAlloc;
    }
    if (!(vis = malloc(count * sizeof(*vis)))) {
        free(pos);
        free(windows);
        free(screens);
        return BadAlloc;
    }

    if ((count = dmxPopulate(client, stuff->window, screens, windows,
                             pos, vis)) < 0) {
        free(vis);
        free(pos);
        free(windows);
        free(screens);
        return BadWindow;
    }

    rep = (xDMXGetWindowAttributesReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = count * 6,
        .screenCount = count
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.screenCount);
        for (i = 0; i < count; i++) {
            swapl(&screens[i]);
            swapl(&windows[i]);

            swaps(&pos[i].x);
            swaps(&pos[i].y);
            swaps(&pos[i].width);
            swaps(&pos[i].height);

            swaps(&vis[i].x);
            swaps(&vis[i].y);
            swaps(&vis[i].width);
            swaps(&vis[i].height);
        }
    }

    dmxFlushPendingSyncs();

    WriteToClient(client, sizeof(xDMXGetWindowAttributesReply), &rep);
    if (count) {
        WriteToClient(client, count * sizeof(*screens), screens);
        WriteToClient(client, count * sizeof(*windows), windows);
        WriteToClient(client, count * sizeof(*pos), pos);
        WriteToClient(client, count * sizeof(*vis), vis);
    }

    free(vis);
    free(pos);
    free(windows);
    free(screens);

    return Success;
}

static int
ProcDMXGetDesktopAttributes(ClientPtr client)
{
    xDMXGetDesktopAttributesReply rep;
    DMXDesktopAttributesRec attr;

    REQUEST_SIZE_MATCH(xDMXGetDesktopAttributesReq);

    dmxGetDesktopAttributes(&attr);

    rep = (xDMXGetDesktopAttributesReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .width = attr.width,
        .height = attr.height,
        .shiftX = attr.shiftX,
        .shiftY = attr.shiftY
    };

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swaps(&rep.width);
        swaps(&rep.height);
        swaps(&rep.shiftX);
        swaps(&rep.shiftY);
    }
    WriteToClient(client, sizeof(xDMXGetDesktopAttributesReply), &rep);
    return Success;
}

static int
ProcDMXChangeDesktopAttributes(ClientPtr client)
{
    REQUEST(xDMXChangeDesktopAttributesReq);
    xDMXChangeDesktopAttributesReply rep;
    int status = DMX_BAD_XINERAMA;
    CARD32 *value_list;
    DMXDesktopAttributesRec attr;
    int len;

    REQUEST_AT_LEAST_SIZE(xDMXChangeDesktopAttributesReq);
    len = client->req_len - (sizeof(xDMXChangeDesktopAttributesReq) >> 2);
    if (len != Ones(stuff->valueMask))
        return BadLength;

    if (!_DMXXineramaActive())
        goto noxinerama;

    value_list = (CARD32 *) (stuff + 1);

    dmxGetDesktopAttributes(&attr);
    dmxFetchDesktopAttributes(stuff->valueMask, &attr, value_list);

#if PANORAMIX
    status = dmxConfigureDesktop(&attr);
#endif
    if (status == BadValue)
        return status;

 noxinerama:
    rep = (xDMXChangeDesktopAttributesReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = status
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
    }
    WriteToClient(client, sizeof(xDMXChangeDesktopAttributesReply), &rep);
    return Success;
}

static int
ProcDMXGetInputCount(ClientPtr client)
{
    xDMXGetInputCountReply rep;

    REQUEST_SIZE_MATCH(xDMXGetInputCountReq);

    rep = (xDMXGetInputCountReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .inputCount = dmxGetInputCount()
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.inputCount);
    }
    WriteToClient(client, sizeof(xDMXGetInputCountReply), &rep);
    return Success;
}

static int
ProcDMXGetInputAttributes(ClientPtr client)
{
    REQUEST(xDMXGetInputAttributesReq);
    xDMXGetInputAttributesReply rep;
    int length;
    int paddedLength;
    DMXInputAttributesRec attr;

    REQUEST_SIZE_MATCH(xDMXGetInputAttributesReq);

    if (dmxGetInputAttributes(stuff->deviceId, &attr))
        return BadValue;

    length = attr.name ? strlen(attr.name) : 0;
    paddedLength = pad_to_int32(length);

    rep = (xDMXGetInputAttributesReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = bytes_to_int32(paddedLength),

        .inputType = attr.inputType,
        .physicalScreen = attr.physicalScreen,
        .physicalId = attr.physicalId,
        .nameLength = length,
        .isCore = attr.isCore,
        .sendsCore = attr.sendsCore,
        .detached = attr.detached
    };

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.inputType);
        swapl(&rep.physicalScreen);
        swapl(&rep.physicalId);
        swapl(&rep.nameLength);
    }
    WriteToClient(client, sizeof(xDMXGetInputAttributesReply), &rep);
    if (length)
        WriteToClient(client, length, attr.name);
    return Success;
}

static int
ProcDMXAddInput(ClientPtr client)
{
    REQUEST(xDMXAddInputReq);
    xDMXAddInputReply rep;
    int status = 0;
    CARD32 *value_list;
    DMXInputAttributesRec attr;
    int count;
    char *name;
    int len;
    int paddedLength;
    int id = -1;

    REQUEST_AT_LEAST_SIZE(xDMXAddInputReq);
    paddedLength = pad_to_int32(stuff->displayNameLength);
    len = client->req_len - (sizeof(xDMXAddInputReq) >> 2);
    if (len != Ones(stuff->valueMask) + paddedLength / 4)
        return BadLength;

    memset(&attr, 0, sizeof(attr));
    value_list = (CARD32 *) (stuff + 1);
    count = dmxFetchInputAttributes(stuff->valueMask, &attr, value_list);

    if (!(name = malloc(stuff->displayNameLength + 1 + 4)))
        return BadAlloc;
    memcpy(name, &value_list[count], stuff->displayNameLength);
    name[stuff->displayNameLength] = '\0';
    attr.name = name;

    status = dmxAddInput(&attr, &id);

    free(name);

    if (status)
        return status;

    rep = (xDMXAddInputReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = status,
        .physicalId = id
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
        swapl(&rep.physicalId);
    }
    WriteToClient(client, sizeof(xDMXAddInputReply), &rep);
    return Success;
}

static int
ProcDMXRemoveInput(ClientPtr client)
{
    REQUEST(xDMXRemoveInputReq);
    xDMXRemoveInputReply rep;
    int status = 0;

    REQUEST_SIZE_MATCH(xDMXRemoveInputReq);

    status = dmxRemoveInput(stuff->physicalId);

    if (status)
        return status;

    rep = (xDMXRemoveInputReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = status
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
    }
    WriteToClient(client, sizeof(xDMXRemoveInputReply), &rep);
    return Success;
}

static int
ProcDMXDispatch(ClientPtr client)
{
    REQUEST(xReq);

    switch (stuff->data) {
    case X_DMXQueryVersion:
        return ProcDMXQueryVersion(client);
    case X_DMXSync:
        return ProcDMXSync(client);
    case X_DMXForceWindowCreation:
        return ProcDMXForceWindowCreation(client);
    case X_DMXGetScreenCount:
        return ProcDMXGetScreenCount(client);
    case X_DMXGetScreenAttributes:
        return ProcDMXGetScreenAttributes(client);
    case X_DMXChangeScreensAttributes:
        return ProcDMXChangeScreensAttributes(client);
    case X_DMXAddScreen:
        return ProcDMXAddScreen(client);
    case X_DMXRemoveScreen:
        return ProcDMXRemoveScreen(client);
    case X_DMXGetWindowAttributes:
        return ProcDMXGetWindowAttributes(client);
    case X_DMXGetDesktopAttributes:
        return ProcDMXGetDesktopAttributes(client);
    case X_DMXChangeDesktopAttributes:
        return ProcDMXChangeDesktopAttributes(client);
    case X_DMXGetInputCount:
        return ProcDMXGetInputCount(client);
    case X_DMXGetInputAttributes:
        return ProcDMXGetInputAttributes(client);
    case X_DMXAddInput:
        return ProcDMXAddInput(client);
    case X_DMXRemoveInput:
        return ProcDMXRemoveInput(client);

    case X_DMXGetScreenInformationDEPRECATED:
    case X_DMXForceWindowCreationDEPRECATED:
    case X_DMXReconfigureScreenDEPRECATED:
        return BadImplementation;

    default:
        return BadRequest;
    }
}

static int
SProcDMXQueryVersion(ClientPtr client)
{
    REQUEST(xDMXQueryVersionReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXQueryVersionReq);
    return ProcDMXQueryVersion(client);
}

static int
SProcDMXSync(ClientPtr client)
{
    REQUEST(xDMXSyncReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXSyncReq);
    return ProcDMXSync(client);
}

static int
SProcDMXForceWindowCreation(ClientPtr client)
{
    REQUEST(xDMXForceWindowCreationReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXForceWindowCreationReq);
    swapl(&stuff->window);
    return ProcDMXForceWindowCreation(client);
}

static int
SProcDMXGetScreenCount(ClientPtr client)
{
    REQUEST(xDMXGetScreenCountReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXGetScreenCountReq);
    return ProcDMXGetScreenCount(client);
}

static int
SProcDMXGetScreenAttributes(ClientPtr client)
{
    REQUEST(xDMXGetScreenAttributesReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXGetScreenAttributesReq);
    swapl(&stuff->physicalScreen);
    return ProcDMXGetScreenAttributes(client);
}

static int
SProcDMXChangeScreensAttributes(ClientPtr client)
{
    REQUEST(xDMXChangeScreensAttributesReq);

    swaps(&stuff->length);
    REQUEST_AT_LEAST_SIZE(xDMXGetScreenAttributesReq);
    swapl(&stuff->screenCount);
    swapl(&stuff->maskCount);
    SwapRestL(stuff);
    return ProcDMXGetScreenAttributes(client);
}

static int
SProcDMXAddScreen(ClientPtr client)
{
    int paddedLength;

    REQUEST(xDMXAddScreenReq);

    swaps(&stuff->length);
    REQUEST_AT_LEAST_SIZE(xDMXAddScreenReq);
    swapl(&stuff->displayNameLength);
    swapl(&stuff->valueMask);
    paddedLength = pad_to_int32(stuff->displayNameLength);
    SwapLongs((CARD32 *) (stuff + 1), LengthRestL(stuff) - paddedLength / 4);
    return ProcDMXAddScreen(client);
}

static int
SProcDMXRemoveScreen(ClientPtr client)
{
    REQUEST(xDMXRemoveScreenReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXRemoveScreenReq);
    swapl(&stuff->physicalScreen);
    return ProcDMXRemoveScreen(client);
}

static int
SProcDMXGetWindowAttributes(ClientPtr client)
{
    REQUEST(xDMXGetWindowAttributesReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXGetWindowAttributesReq);
    swapl(&stuff->window);
    return ProcDMXGetWindowAttributes(client);
}

static int
SProcDMXGetDesktopAttributes(ClientPtr client)
{
    REQUEST(xDMXGetDesktopAttributesReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXGetDesktopAttributesReq);
    return ProcDMXGetDesktopAttributes(client);
}

static int
SProcDMXChangeDesktopAttributes(ClientPtr client)
{
    REQUEST(xDMXChangeDesktopAttributesReq);

    swaps(&stuff->length);
    REQUEST_AT_LEAST_SIZE(xDMXChangeDesktopAttributesReq);
    swapl(&stuff->valueMask);
    SwapRestL(stuff);
    return ProcDMXChangeDesktopAttributes(client);
}

static int
SProcDMXGetInputCount(ClientPtr client)
{
    REQUEST(xDMXGetInputCountReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXGetInputCountReq);
    return ProcDMXGetInputCount(client);
}

static int
SProcDMXGetInputAttributes(ClientPtr client)
{
    REQUEST(xDMXGetInputAttributesReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXGetInputAttributesReq);
    swapl(&stuff->deviceId);
    return ProcDMXGetInputAttributes(client);
}

static int
SProcDMXAddInput(ClientPtr client)
{
    int paddedLength;

    REQUEST(xDMXAddInputReq);

    swaps(&stuff->length);
    REQUEST_AT_LEAST_SIZE(xDMXAddInputReq);
    swapl(&stuff->displayNameLength);
    swapl(&stuff->valueMask);
    paddedLength = pad_to_int32(stuff->displayNameLength);
    SwapLongs((CARD32 *) (stuff + 1), LengthRestL(stuff) - paddedLength / 4);
    return ProcDMXAddInput(client);
}

static int
SProcDMXRemoveInput(ClientPtr client)
{
    REQUEST(xDMXRemoveInputReq);

    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xDMXRemoveInputReq);
    swapl(&stuff->physicalId);
    return ProcDMXRemoveInput(client);
}

static int
SProcDMXDispatch(ClientPtr client)
{
    REQUEST(xReq);

    switch (stuff->data) {
    case X_DMXQueryVersion:
        return SProcDMXQueryVersion(client);
    case X_DMXSync:
        return SProcDMXSync(client);
    case X_DMXForceWindowCreation:
        return SProcDMXForceWindowCreation(client);
    case X_DMXGetScreenCount:
        return SProcDMXGetScreenCount(client);
    case X_DMXGetScreenAttributes:
        return SProcDMXGetScreenAttributes(client);
    case X_DMXChangeScreensAttributes:
        return SProcDMXChangeScreensAttributes(client);
    case X_DMXAddScreen:
        return SProcDMXAddScreen(client);
    case X_DMXRemoveScreen:
        return SProcDMXRemoveScreen(client);
    case X_DMXGetWindowAttributes:
        return SProcDMXGetWindowAttributes(client);
    case X_DMXGetDesktopAttributes:
        return SProcDMXGetDesktopAttributes(client);
    case X_DMXChangeDesktopAttributes:
        return SProcDMXChangeDesktopAttributes(client);
    case X_DMXGetInputCount:
        return SProcDMXGetInputCount(client);
    case X_DMXGetInputAttributes:
        return SProcDMXGetInputAttributes(client);
    case X_DMXAddInput:
        return SProcDMXAddInput(client);
    case X_DMXRemoveInput:
        return SProcDMXRemoveInput(client);

    case X_DMXGetScreenInformationDEPRECATED:
    case X_DMXForceWindowCreationDEPRECATED:
    case X_DMXReconfigureScreenDEPRECATED:
        return BadImplementation;

    default:
        return BadRequest;
    }
}

/** Initialize the extension. */
void
DMXExtensionInit(void)
{
    ExtensionEntry *extEntry;

    if ((extEntry = AddExtension(DMX_EXTENSION_NAME, 0, 0,
                                 ProcDMXDispatch, SProcDMXDispatch,
                                 NULL, StandardMinorOpcode)))
        DMXCode = extEntry->base;
}
