/* WindowsWM extension is based on AppleWM extension */
/**************************************************************************

Copyright (c) 2002 Apple Computer, Inc. All Rights Reserved.
Copyright (c) 2003 Torrey T. Lyons. All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

#include "misc.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "scrnintstr.h"
#include "servermd.h"
#include "swaprep.h"
#define _WINDOWSWM_SERVER_
#include <X11/extensions/windowswmstr.h>
#include "protocol-versions.h"

static int WMErrorBase;
static unsigned char WMReqCode = 0;
static int WMEventBase = 0;

static RESTYPE ClientType, eventResourceType;   /* resource types for event masks */
static XID eventResource;

/* Currently selected events */
static unsigned int eventMask = 0;

static int WMFreeClient(void *data, XID id);
static int WMFreeEvents(void *data, XID id);
static void SNotifyEvent(xWindowsWMNotifyEvent * from,
                         xWindowsWMNotifyEvent * to);

typedef struct _WMEvent *WMEventPtr;
typedef struct _WMEvent {
    WMEventPtr next;
    ClientPtr client;
    XID clientResource;
    unsigned int mask;
} WMEventRec;

static int
ProcWindowsWMQueryVersion(ClientPtr client)
{
    xWindowsWMQueryVersionReply rep;

    REQUEST_SIZE_MATCH(xWindowsWMQueryVersionReq);
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.majorVersion = SERVER_WINDOWSWM_MAJOR_VERSION;
    rep.minorVersion = SERVER_WINDOWSWM_MINOR_VERSION;
    rep.patchVersion = SERVER_WINDOWSWM_PATCH_VERSION;
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }
    WriteToClient(client, sizeof(xWindowsWMQueryVersionReply), &rep);
    return Success;
}

/* events */

static inline void
updateEventMask(WMEventPtr * pHead)
{
    WMEventPtr pCur;

    eventMask = 0;
    for (pCur = *pHead; pCur != NULL; pCur = pCur->next)
        eventMask |= pCur->mask;
}

 /*ARGSUSED*/ static int
WMFreeClient(void *data, XID id)
{
    WMEventPtr pEvent;
    WMEventPtr *pHead, pCur, pPrev;

    pEvent = (WMEventPtr) data;
    dixLookupResourceByType((void *) &pHead, eventResource, eventResourceType,
                            NullClient, DixUnknownAccess);
    if (pHead) {
        pPrev = 0;
        for (pCur = *pHead; pCur && pCur != pEvent; pCur = pCur->next)
            pPrev = pCur;
        if (pCur) {
            if (pPrev)
                pPrev->next = pEvent->next;
            else
                *pHead = pEvent->next;
        }
        updateEventMask(pHead);
    }
    free((void *) pEvent);
    return 1;
}

 /*ARGSUSED*/ static int
WMFreeEvents(void *data, XID id)
{
    WMEventPtr *pHead, pCur, pNext;

    pHead = (WMEventPtr *) data;
    for (pCur = *pHead; pCur; pCur = pNext) {
        pNext = pCur->next;
        FreeResource(pCur->clientResource, ClientType);
        free((void *) pCur);
    }
    free((void *) pHead);
    eventMask = 0;
    return 1;
}

static int
ProcWindowsWMSelectInput(ClientPtr client)
{
    REQUEST(xWindowsWMSelectInputReq);
    WMEventPtr pEvent, pNewEvent, *pHead;
    XID clientResource;

    REQUEST_SIZE_MATCH(xWindowsWMSelectInputReq);
    dixLookupResourceByType((void *) &pHead, eventResource, eventResourceType,
                            client, DixWriteAccess);
    if (stuff->mask != 0) {
        if (pHead) {
            /* check for existing entry. */
            for (pEvent = *pHead; pEvent; pEvent = pEvent->next) {
                if (pEvent->client == client) {
                    pEvent->mask = stuff->mask;
                    updateEventMask(pHead);
                    return Success;
                }
            }
        }

        /* build the entry */
        pNewEvent = malloc(sizeof(WMEventRec));
        if (!pNewEvent)
            return BadAlloc;
        pNewEvent->next = 0;
        pNewEvent->client = client;
        pNewEvent->mask = stuff->mask;
        /*
         * add a resource that will be deleted when
         * the client goes away
         */
        clientResource = FakeClientID(client->index);
        pNewEvent->clientResource = clientResource;
        if (!AddResource(clientResource, ClientType, (void *) pNewEvent))
            return BadAlloc;
        /*
         * create a resource to contain a pointer to the list
         * of clients selecting input.  This must be indirect as
         * the list may be arbitrarily rearranged which cannot be
         * done through the resource database.
         */
        if (!pHead) {
            pHead = malloc(sizeof(WMEventPtr));
            if (!pHead ||
                !AddResource(eventResource, eventResourceType, (void *) pHead))
            {
                FreeResource(clientResource, RT_NONE);
                return BadAlloc;
            }
            *pHead = 0;
        }
        pNewEvent->next = *pHead;
        *pHead = pNewEvent;
        updateEventMask(pHead);
    }
    else if (stuff->mask == 0) {
        /* delete the interest */
        if (pHead) {
            pNewEvent = 0;
            for (pEvent = *pHead; pEvent; pEvent = pEvent->next) {
                if (pEvent->client == client)
                    break;
                pNewEvent = pEvent;
            }
            if (pEvent) {
                FreeResource(pEvent->clientResource, ClientType);
                if (pNewEvent)
                    pNewEvent->next = pEvent->next;
                else
                    *pHead = pEvent->next;
                free(pEvent);
                updateEventMask(pHead);
            }
        }
    }
    else {
        client->errorValue = stuff->mask;
        return BadValue;
    }
    return Success;
}

/*
 * deliver the event
 */

void
winWindowsWMSendEvent(int type, unsigned int mask, int which, int arg,
                      Window window, int x, int y, int w, int h)
{
    WMEventPtr *pHead, pEvent;
    ClientPtr client;
    xWindowsWMNotifyEvent se;

#if CYGMULTIWINDOW_DEBUG
    ErrorF("winWindowsWMSendEvent %d %d %d %d,  %d %d - %d %d\n",
           type, mask, which, arg, x, y, w, h);
#endif
    dixLookupResourceByType((void *) &pHead, eventResource, eventResourceType,
                            NullClient, DixUnknownAccess);
    if (!pHead)
        return;
    for (pEvent = *pHead; pEvent; pEvent = pEvent->next) {
        client = pEvent->client;
#if CYGMULTIWINDOW_DEBUG
        ErrorF("winWindowsWMSendEvent - %p\n", client);
#endif
        if ((pEvent->mask & mask) == 0) {
            continue;
        }
#if CYGMULTIWINDOW_DEBUG
        ErrorF("winWindowsWMSendEvent - send\n");
#endif
        se.type = type + WMEventBase;
        se.kind = which;
        se.window = window;
        se.arg = arg;
        se.x = x;
        se.y = y;
        se.w = w;
        se.h = h;
        se.time = currentTime.milliseconds;
        WriteEventsToClient(client, 1, (xEvent *) &se);
    }
}

/* general utility functions */

static int
ProcWindowsWMDisableUpdate(ClientPtr client)
{
    REQUEST_SIZE_MATCH(xWindowsWMDisableUpdateReq);

    //winDisableUpdate();

    return Success;
}

static int
ProcWindowsWMReenableUpdate(ClientPtr client)
{
    REQUEST_SIZE_MATCH(xWindowsWMReenableUpdateReq);

    //winEnableUpdate();

    return Success;
}

/* window functions */

static int
ProcWindowsWMSetFrontProcess(ClientPtr client)
{
    REQUEST_SIZE_MATCH(xWindowsWMSetFrontProcessReq);

    //QuartzMessageMainThread(kWindowsSetFrontProcess, NULL, 0);

    return Success;
}

/* frame functions */

static int
ProcWindowsWMFrameGetRect(ClientPtr client)
{
    xWindowsWMFrameGetRectReply rep;
    RECT rcNew;

    REQUEST(xWindowsWMFrameGetRectReq);

#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameGetRect %d %d\n",
           (sizeof(xWindowsWMFrameGetRectReq) >> 2), (int) client->req_len);
#endif

    REQUEST_SIZE_MATCH(xWindowsWMFrameGetRectReq);
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;

    if (stuff->frame_rect != 0) {
        ErrorF("ProcWindowsWMFrameGetRect - stuff->frame_rect != 0\n");
        return BadValue;
    }

    /* Store the origin, height, and width in a rectangle structure */
    SetRect(&rcNew, stuff->ix, stuff->iy,
            stuff->ix + stuff->iw, stuff->iy + stuff->ih);

#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameGetRect - %d %d %d %d\n",
           stuff->ix, stuff->iy, stuff->ix + stuff->iw, stuff->iy + stuff->ih);
#endif

    /*
     * Calculate the required size of the Windows window rectangle,
     * given the size of the Windows window client area.
     */
    AdjustWindowRectEx(&rcNew, stuff->frame_style, FALSE,
                       stuff->frame_style_ex);
    rep.x = rcNew.left;
    rep.y = rcNew.top;
    rep.w = rcNew.right - rcNew.left;
    rep.h = rcNew.bottom - rcNew.top;
#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameGetRect - %d %d %d %d\n",
           rep.x, rep.y, rep.w, rep.h);
#endif

    WriteToClient(client, sizeof(xWindowsWMFrameGetRectReply), &rep);
    return Success;
}

static int
ProcWindowsWMFrameDraw(ClientPtr client)
{
    REQUEST(xWindowsWMFrameDrawReq);
    WindowPtr pWin;
    win32RootlessWindowPtr pRLWinPriv;
    RECT rcNew;
    int nCmdShow, rc;
    RegionRec newShape;

    REQUEST_SIZE_MATCH(xWindowsWMFrameDrawReq);

#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameDraw\n");
#endif
    rc = dixLookupWindow(&pWin, stuff->window, client, DixReadAccess);
    if (rc != Success)
        return rc;
#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameDraw - Window found\n");
#endif

    pRLWinPriv = (win32RootlessWindowPtr) RootlessFrameForWindow(pWin, TRUE);
    if (pRLWinPriv == 0)
        return BadWindow;

#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameDraw - HWND %p 0x%08x 0x%08x\n",
           pRLWinPriv->hWnd, (int) stuff->frame_style,
           (int) stuff->frame_style_ex);
    ErrorF("ProcWindowsWMFrameDraw - %d %d %d %d\n",
           stuff->ix, stuff->iy, stuff->iw, stuff->ih);
#endif

    /* Store the origin, height, and width in a rectangle structure */
    SetRect(&rcNew, stuff->ix, stuff->iy,
            stuff->ix + stuff->iw, stuff->iy + stuff->ih);

    /*
     * Calculate the required size of the Windows window rectangle,
     * given the size of the Windows window client area.
     */
    AdjustWindowRectEx(&rcNew, stuff->frame_style, FALSE,
                       stuff->frame_style_ex);

    /* Set the window extended style flags */
    if (!SetWindowLongPtr(pRLWinPriv->hWnd, GWL_EXSTYLE, stuff->frame_style_ex)) {
        return BadValue;
    }

    /* Set the window standard style flags */
    if (!SetWindowLongPtr(pRLWinPriv->hWnd, GWL_STYLE, stuff->frame_style)) {
        return BadValue;
    }

    /* Flush the window style */
    if (!SetWindowPos(pRLWinPriv->hWnd, NULL,
                      rcNew.left, rcNew.top,
                      rcNew.right - rcNew.left, rcNew.bottom - rcNew.top,
                      SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE)) {
        return BadValue;
    }
    if (!IsWindowVisible(pRLWinPriv->hWnd))
        nCmdShow = SW_HIDE;
    else
        nCmdShow = SW_SHOWNA;

    ShowWindow(pRLWinPriv->hWnd, nCmdShow);

    if (wBoundingShape(pWin) != NULL) {
        /* wBoundingShape is relative to *inner* origin of window.
           Translate by borderWidth to get the outside-relative position. */

        RegionNull(&newShape);
        RegionCopy(&newShape, wBoundingShape(pWin));
        RegionTranslate(&newShape, pWin->borderWidth, pWin->borderWidth);
        winMWExtWMReshapeFrame(pRLWinPriv, &newShape);
        RegionUninit(&newShape);
    }
#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameDraw - done\n");
#endif

    return Success;
}

static int
ProcWindowsWMFrameSetTitle(ClientPtr client)
{
    unsigned int title_length, title_max;
    char *title_bytes;

    REQUEST(xWindowsWMFrameSetTitleReq);
    WindowPtr pWin;
    win32RootlessWindowPtr pRLWinPriv;
    int rc;

#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameSetTitle\n");
#endif

    REQUEST_AT_LEAST_SIZE(xWindowsWMFrameSetTitleReq);

    rc = dixLookupWindow(&pWin, stuff->window, client, DixReadAccess);
    if (rc != Success)
        return rc;
#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameSetTitle - Window found\n");
#endif

    title_length = stuff->title_length;
    title_max = (stuff->length << 2) - sizeof(xWindowsWMFrameSetTitleReq);

    if (title_max < title_length)
        return BadValue;

#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameSetTitle - length is valid\n");
#endif

    title_bytes = malloc(title_length + 1);
    strncpy(title_bytes, (char *) &stuff[1], title_length);
    title_bytes[title_length] = '\0';

    pRLWinPriv = (win32RootlessWindowPtr) RootlessFrameForWindow(pWin, FALSE);

    if (pRLWinPriv == 0) {
        free(title_bytes);
        return BadWindow;
    }

    /* Flush the window style */
    SetWindowText(pRLWinPriv->hWnd, title_bytes);

    free(title_bytes);

#if CYGMULTIWINDOW_DEBUG
    ErrorF("ProcWindowsWMFrameSetTitle - done\n");
#endif

    return Success;
}

/* dispatch */

static int
ProcWindowsWMDispatch(ClientPtr client)
{
    REQUEST(xReq);

    switch (stuff->data) {
    case X_WindowsWMQueryVersion:
        return ProcWindowsWMQueryVersion(client);
    }

    if (!client->local)
        return WMErrorBase + WindowsWMClientNotLocal;

    switch (stuff->data) {
    case X_WindowsWMSelectInput:
        return ProcWindowsWMSelectInput(client);
    case X_WindowsWMDisableUpdate:
        return ProcWindowsWMDisableUpdate(client);
    case X_WindowsWMReenableUpdate:
        return ProcWindowsWMReenableUpdate(client);
    case X_WindowsWMSetFrontProcess:
        return ProcWindowsWMSetFrontProcess(client);
    case X_WindowsWMFrameGetRect:
        return ProcWindowsWMFrameGetRect(client);
    case X_WindowsWMFrameDraw:
        return ProcWindowsWMFrameDraw(client);
    case X_WindowsWMFrameSetTitle:
        return ProcWindowsWMFrameSetTitle(client);
    default:
        return BadRequest;
    }
}

static void
SNotifyEvent(xWindowsWMNotifyEvent * from, xWindowsWMNotifyEvent * to)
{
    to->type = from->type;
    to->kind = from->kind;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->window, to->window);
    cpswapl(from->time, to->time);
    cpswapl(from->arg, to->arg);
}

static int
SProcWindowsWMQueryVersion(ClientPtr client)
{
    REQUEST(xWindowsWMQueryVersionReq);
    swaps(&stuff->length);
    return ProcWindowsWMQueryVersion(client);
}

static int
SProcWindowsWMDispatch(ClientPtr client)
{
    REQUEST(xReq);

    /* It is bound to be non-local when there is byte swapping */
    if (!client->local)
        return WMErrorBase + WindowsWMClientNotLocal;

    /* only local clients are allowed WM access */
    switch (stuff->data) {
    case X_WindowsWMQueryVersion:
        return SProcWindowsWMQueryVersion(client);
    default:
        return BadRequest;
    }
}

void
winWindowsWMExtensionInit(void)
{
    ExtensionEntry *extEntry;

    ClientType = CreateNewResourceType(WMFreeClient, "WMClient");
    eventResourceType = CreateNewResourceType(WMFreeEvents, "WMEvent");
    eventResource = FakeClientID(0);

    if (ClientType && eventResourceType &&
        (extEntry = AddExtension(WINDOWSWMNAME,
                                 WindowsWMNumberEvents,
                                 WindowsWMNumberErrors,
                                 ProcWindowsWMDispatch,
                                 SProcWindowsWMDispatch,
                                 NULL, StandardMinorOpcode))) {
        size_t i;

        WMReqCode = (unsigned char) extEntry->base;
        WMErrorBase = extEntry->errorBase;
        WMEventBase = extEntry->eventBase;
        for (i = 0; i < WindowsWMNumberEvents; i++)
            EventSwapVector[WMEventBase + i] = (EventSwapPtr) SNotifyEvent;
    }
}
