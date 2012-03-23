/***********************************************************
Copyright 1991 by Digital Equipment Corporation, Maynard, Massachusetts,
and the Massachusetts Institute of Technology, Cambridge, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the names of Digital or MIT not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

/*
** File: 
**
**   xvmain.c --- Xv server extension main device independent module.
**   
** Author: 
**
**   David Carver (Digital Workstation Engineering/Project Athena)
**
** Revisions:
**
**   04.09.91 Carver
**     - change: stop video always generates an event even when video
**       wasn't active
**
**   29.08.91 Carver
**     - change: unrealizing windows no longer preempts video
**
**   11.06.91 Carver
**     - changed SetPortControl to SetPortAttribute
**     - changed GetPortControl to GetPortAttribute
**     - changed QueryBestSize
**
**   28.05.91 Carver
**     - fixed Put and Get requests to not preempt operations to same drawable
**
**   15.05.91 Carver
**     - version 2.0 upgrade
**
**   19.03.91 Carver
**     - fixed Put and Get requests to honor grabbed ports.
**     - fixed Video requests to update di structure with new drawable, and
**       client after calling ddx.
**
**   24.01.91 Carver
**     - version 1.4 upgrade
**       
** Notes:
**
**   Port structures reference client structures in a two different
**   ways: when grabs, or video is active.  Each reference is encoded
**   as fake client resources and thus when the client is goes away so
**   does the reference (it is zeroed).  No other action is taken, so
**   video doesn't necessarily stop.  It probably will as a result of
**   other resources going away, but if a client starts video using
**   none of its own resources, then the video will continue to play
**   after the client disappears.
**
**
*/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <string.h>

#include <X11/X.h>
#include <X11/Xproto.h>
#include "misc.h"
#include "os.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "pixmapstr.h"
#include "gc.h"
#include "extnsionst.h"
#include "dixstruct.h"
#include "resource.h"
#include "opaque.h"
#include "input.h"

#define GLOBAL

#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvproto.h>
#include "xvdix.h"

#ifdef PANORAMIX
#include "panoramiX.h"
#include "panoramiXsrv.h"
#endif
#include "xvdisp.h"

static DevPrivateKeyRec XvScreenKeyRec;

#define XvScreenKey (&XvScreenKeyRec)
unsigned long XvExtensionGeneration = 0;
unsigned long XvScreenGeneration = 0;
unsigned long XvResourceGeneration = 0;

int XvReqCode;
int XvEventBase;
int XvErrorBase;

RESTYPE XvRTPort;
RESTYPE XvRTEncoding;
RESTYPE XvRTGrab;
RESTYPE XvRTVideoNotify;
RESTYPE XvRTVideoNotifyList;
RESTYPE XvRTPortNotify;

/* EXTERNAL */

static void WriteSwappedVideoNotifyEvent(xvEvent *, xvEvent *);
static void WriteSwappedPortNotifyEvent(xvEvent *, xvEvent *);
static Bool CreateResourceTypes(void);

static Bool XvCloseScreen(int, ScreenPtr);
static Bool XvDestroyPixmap(PixmapPtr);
static Bool XvDestroyWindow(WindowPtr);
static void XvResetProc(ExtensionEntry *);
static int XvdiDestroyGrab(pointer, XID);
static int XvdiDestroyEncoding(pointer, XID);
static int XvdiDestroyVideoNotify(pointer, XID);
static int XvdiDestroyPortNotify(pointer, XID);
static int XvdiDestroyVideoNotifyList(pointer, XID);
static int XvdiDestroyPort(pointer, XID);
static int XvdiSendVideoNotify(XvPortPtr, DrawablePtr, int);

/*
** XvExtensionInit
**
**
*/

void
XvExtensionInit(void)
{
    ExtensionEntry *extEntry;

    if (!dixRegisterPrivateKey(&XvScreenKeyRec, PRIVATE_SCREEN, 0))
        return;

    /* LOOK TO SEE IF ANY SCREENS WERE INITIALIZED; IF NOT THEN
       INIT GLOBAL VARIABLES SO THE EXTENSION CAN FUNCTION */
    if (XvScreenGeneration != serverGeneration) {
        if (!CreateResourceTypes()) {
            ErrorF("XvExtensionInit: Unable to allocate resource types\n");
            return;
        }
#ifdef PANORAMIX
        XineramaRegisterConnectionBlockCallback(XineramifyXv);
#endif
        XvScreenGeneration = serverGeneration;
    }

    if (XvExtensionGeneration != serverGeneration) {
        XvExtensionGeneration = serverGeneration;

        extEntry = AddExtension(XvName, XvNumEvents, XvNumErrors,
                                ProcXvDispatch, SProcXvDispatch,
                                XvResetProc, StandardMinorOpcode);
        if (!extEntry) {
            FatalError("XvExtensionInit: AddExtensions failed\n");
        }

        XvReqCode = extEntry->base;
        XvEventBase = extEntry->eventBase;
        XvErrorBase = extEntry->errorBase;

        EventSwapVector[XvEventBase + XvVideoNotify] =
            (EventSwapPtr) WriteSwappedVideoNotifyEvent;
        EventSwapVector[XvEventBase + XvPortNotify] =
            (EventSwapPtr) WriteSwappedPortNotifyEvent;

        SetResourceTypeErrorValue(XvRTPort, _XvBadPort);
        (void) MakeAtom(XvName, strlen(XvName), xTrue);

    }
}

static Bool
CreateResourceTypes(void)
{

    if (XvResourceGeneration == serverGeneration)
        return TRUE;

    XvResourceGeneration = serverGeneration;

    if (!(XvRTPort = CreateNewResourceType(XvdiDestroyPort, "XvRTPort"))) {
        ErrorF("CreateResourceTypes: failed to allocate port resource.\n");
        return FALSE;
    }

    if (!(XvRTGrab = CreateNewResourceType(XvdiDestroyGrab, "XvRTGrab"))) {
        ErrorF("CreateResourceTypes: failed to allocate grab resource.\n");
        return FALSE;
    }

    if (!(XvRTEncoding = CreateNewResourceType(XvdiDestroyEncoding,
                                               "XvRTEncoding"))) {
        ErrorF("CreateResourceTypes: failed to allocate encoding resource.\n");
        return FALSE;
    }

    if (!(XvRTVideoNotify = CreateNewResourceType(XvdiDestroyVideoNotify,
                                                  "XvRTVideoNotify"))) {
        ErrorF
            ("CreateResourceTypes: failed to allocate video notify resource.\n");
        return FALSE;
    }

    if (!
        (XvRTVideoNotifyList =
         CreateNewResourceType(XvdiDestroyVideoNotifyList,
                               "XvRTVideoNotifyList"))) {
        ErrorF
            ("CreateResourceTypes: failed to allocate video notify list resource.\n");
        return FALSE;
    }

    if (!(XvRTPortNotify = CreateNewResourceType(XvdiDestroyPortNotify,
                                                 "XvRTPortNotify"))) {
        ErrorF
            ("CreateResourceTypes: failed to allocate port notify resource.\n");
        return FALSE;
    }

    return TRUE;

}

int
XvScreenInit(ScreenPtr pScreen)
{
    XvScreenPtr pxvs;

    if (XvScreenGeneration != serverGeneration) {
        if (!CreateResourceTypes()) {
            ErrorF("XvScreenInit: Unable to allocate resource types\n");
            return BadAlloc;
        }
#ifdef PANORAMIX
        XineramaRegisterConnectionBlockCallback(XineramifyXv);
#endif
        XvScreenGeneration = serverGeneration;
    }

    if (!dixRegisterPrivateKey(&XvScreenKeyRec, PRIVATE_SCREEN, 0))
        return BadAlloc;

    if (dixLookupPrivate(&pScreen->devPrivates, XvScreenKey)) {
        ErrorF("XvScreenInit: screen devPrivates ptr non-NULL before init\n");
    }

    /* ALLOCATE SCREEN PRIVATE RECORD */

    pxvs = malloc(sizeof(XvScreenRec));
    if (!pxvs) {
        ErrorF("XvScreenInit: Unable to allocate screen private structure\n");
        return BadAlloc;
    }

    dixSetPrivate(&pScreen->devPrivates, XvScreenKey, pxvs);

    pxvs->DestroyPixmap = pScreen->DestroyPixmap;
    pxvs->DestroyWindow = pScreen->DestroyWindow;
    pxvs->CloseScreen = pScreen->CloseScreen;

    pScreen->DestroyPixmap = XvDestroyPixmap;
    pScreen->DestroyWindow = XvDestroyWindow;
    pScreen->CloseScreen = XvCloseScreen;

    return Success;
}

static Bool
XvCloseScreen(int ii, ScreenPtr pScreen)
{

    XvScreenPtr pxvs;

    pxvs = (XvScreenPtr) dixLookupPrivate(&pScreen->devPrivates, XvScreenKey);

    pScreen->DestroyPixmap = pxvs->DestroyPixmap;
    pScreen->DestroyWindow = pxvs->DestroyWindow;
    pScreen->CloseScreen = pxvs->CloseScreen;

    (*pxvs->ddCloseScreen) (ii, pScreen);

    free(pxvs);

    dixSetPrivate(&pScreen->devPrivates, XvScreenKey, NULL);

    return (*pScreen->CloseScreen) (ii, pScreen);
}

static void
XvResetProc(ExtensionEntry * extEntry)
{
    XvResetProcVector();
}

DevPrivateKey
XvGetScreenKey(void)
{
    return XvScreenKey;
}

unsigned long
XvGetRTPort(void)
{
    return XvRTPort;
}

static Bool
XvDestroyPixmap(PixmapPtr pPix)
{
    Bool status;
    ScreenPtr pScreen;
    XvScreenPtr pxvs;
    XvAdaptorPtr pa;
    int na;
    XvPortPtr pp;
    int np;

    pScreen = pPix->drawable.pScreen;

    SCREEN_PROLOGUE(pScreen, DestroyPixmap);

    pxvs = (XvScreenPtr) dixLookupPrivate(&pScreen->devPrivates, XvScreenKey);

    /* CHECK TO SEE IF THIS PORT IS IN USE */

    pa = pxvs->pAdaptors;
    na = pxvs->nAdaptors;
    while (na--) {
        np = pa->nPorts;
        pp = pa->pPorts;

        while (np--) {
            if (pp->pDraw == (DrawablePtr) pPix) {
                XvdiSendVideoNotify(pp, pp->pDraw, XvPreempted);

                (void) (*pp->pAdaptor->ddStopVideo) (NULL, pp, pp->pDraw);

                pp->pDraw = NULL;
                pp->client = NULL;
                pp->time = currentTime;
            }
            pp++;
        }
        pa++;
    }

    status = (*pScreen->DestroyPixmap) (pPix);

    SCREEN_EPILOGUE(pScreen, DestroyPixmap, XvDestroyPixmap);

    return status;

}

static Bool
XvDestroyWindow(WindowPtr pWin)
{
    Bool status;
    ScreenPtr pScreen;
    XvScreenPtr pxvs;
    XvAdaptorPtr pa;
    int na;
    XvPortPtr pp;
    int np;

    pScreen = pWin->drawable.pScreen;

    SCREEN_PROLOGUE(pScreen, DestroyWindow);

    pxvs = (XvScreenPtr) dixLookupPrivate(&pScreen->devPrivates, XvScreenKey);

    /* CHECK TO SEE IF THIS PORT IS IN USE */

    pa = pxvs->pAdaptors;
    na = pxvs->nAdaptors;
    while (na--) {
        np = pa->nPorts;
        pp = pa->pPorts;

        while (np--) {
            if (pp->pDraw == (DrawablePtr) pWin) {
                XvdiSendVideoNotify(pp, pp->pDraw, XvPreempted);

                (void) (*pp->pAdaptor->ddStopVideo) (NULL, pp, pp->pDraw);

                pp->pDraw = NULL;
                pp->client = NULL;
                pp->time = currentTime;
            }
            pp++;
        }
        pa++;
    }

    status = (*pScreen->DestroyWindow) (pWin);

    SCREEN_EPILOGUE(pScreen, DestroyWindow, XvDestroyWindow);

    return status;

}

/* The XvdiVideoStopped procedure is a hook for the device dependent layer.
   It provides a way for the dd layer to inform the di layer that video has
   stopped in a port for reasons that the di layer had no control over; note
   that it doesn't call back into the dd layer */

int
XvdiVideoStopped(XvPortPtr pPort, int reason)
{

    /* IF PORT ISN'T ACTIVE THEN WE'RE DONE */

    if (!pPort->pDraw)
        return Success;

    XvdiSendVideoNotify(pPort, pPort->pDraw, reason);

    pPort->pDraw = NULL;
    pPort->client = NULL;
    pPort->time = currentTime;

    return Success;

}

static int
XvdiDestroyPort(pointer pPort, XID id)
{
    return (*((XvPortPtr) pPort)->pAdaptor->ddFreePort) (pPort);
}

static int
XvdiDestroyGrab(pointer pGrab, XID id)
{
    ((XvGrabPtr) pGrab)->client = NULL;
    return Success;
}

static int
XvdiDestroyVideoNotify(pointer pn, XID id)
{
    /* JUST CLEAR OUT THE client POINTER FIELD */

    ((XvVideoNotifyPtr) pn)->client = NULL;
    return Success;
}

static int
XvdiDestroyPortNotify(pointer pn, XID id)
{
    /* JUST CLEAR OUT THE client POINTER FIELD */

    ((XvPortNotifyPtr) pn)->client = NULL;
    return Success;
}

static int
XvdiDestroyVideoNotifyList(pointer pn, XID id)
{
    XvVideoNotifyPtr npn, cpn;

    /* ACTUALLY DESTROY THE NOTITY LIST */

    cpn = (XvVideoNotifyPtr) pn;

    while (cpn) {
        npn = cpn->next;
        if (cpn->client)
            FreeResource(cpn->id, XvRTVideoNotify);
        free(cpn);
        cpn = npn;
    }
    return Success;
}

static int
XvdiDestroyEncoding(pointer value, XID id)
{
    return Success;
}

static int
XvdiSendVideoNotify(XvPortPtr pPort, DrawablePtr pDraw, int reason)
{
    xvEvent event;
    XvVideoNotifyPtr pn;

    dixLookupResourceByType((pointer *) &pn, pDraw->id, XvRTVideoNotifyList,
                            serverClient, DixReadAccess);

    while (pn) {
        event.u.u.type = XvEventBase + XvVideoNotify;
        event.u.videoNotify.time = currentTime.milliseconds;
        event.u.videoNotify.drawable = pDraw->id;
        event.u.videoNotify.port = pPort->id;
        event.u.videoNotify.reason = reason;
        WriteEventsToClient(pn->client, 1, (xEventPtr) & event);
        pn = pn->next;
    }

    return Success;

}

int
XvdiSendPortNotify(XvPortPtr pPort, Atom attribute, INT32 value)
{
    xvEvent event;
    XvPortNotifyPtr pn;

    pn = pPort->pNotify;

    while (pn) {
        event.u.u.type = XvEventBase + XvPortNotify;
        event.u.portNotify.time = currentTime.milliseconds;
        event.u.portNotify.port = pPort->id;
        event.u.portNotify.attribute = attribute;
        event.u.portNotify.value = value;
        WriteEventsToClient(pn->client, 1, (xEventPtr) & event);
        pn = pn->next;
    }

    return Success;

}

#define CHECK_SIZE(dw, dh, sw, sh) {                                  \
  if(!dw || !dh || !sw || !sh)  return Success;                       \
  /* The region code will break these if they are too large */        \
  if((dw > 32767) || (dh > 32767) || (sw > 32767) || (sh > 32767))    \
        return BadValue;                                              \
}

int
XvdiPutVideo(ClientPtr client,
             DrawablePtr pDraw,
             XvPortPtr pPort,
             GCPtr pGC,
             INT16 vid_x, INT16 vid_y,
             CARD16 vid_w, CARD16 vid_h,
             INT16 drw_x, INT16 drw_y, CARD16 drw_w, CARD16 drw_h)
{
    DrawablePtr pOldDraw;

    CHECK_SIZE(drw_w, drw_h, vid_w, vid_h);

    /* UPDATE TIME VARIABLES FOR USE IN EVENTS */

    UpdateCurrentTime();

    /* CHECK FOR GRAB; IF THIS CLIENT DOESN'T HAVE THE PORT GRABBED THEN
       INFORM CLIENT OF ITS FAILURE */

    if (pPort->grab.client && (pPort->grab.client != client)) {
        XvdiSendVideoNotify(pPort, pDraw, XvBusy);
        return Success;
    }

    /* CHECK TO SEE IF PORT IS IN USE; IF SO THEN WE MUST DELIVER INTERRUPTED
       EVENTS TO ANY CLIENTS WHO WANT THEM */

    pOldDraw = pPort->pDraw;
    if ((pOldDraw) && (pOldDraw != pDraw)) {
        XvdiSendVideoNotify(pPort, pPort->pDraw, XvPreempted);
    }

    (void) (*pPort->pAdaptor->ddPutVideo) (client, pDraw, pPort, pGC,
                                           vid_x, vid_y, vid_w, vid_h,
                                           drw_x, drw_y, drw_w, drw_h);

    if ((pPort->pDraw) && (pOldDraw != pDraw)) {
        pPort->client = client;
        XvdiSendVideoNotify(pPort, pPort->pDraw, XvStarted);
    }

    pPort->time = currentTime;

    return Success;

}

int
XvdiPutStill(ClientPtr client,
             DrawablePtr pDraw,
             XvPortPtr pPort,
             GCPtr pGC,
             INT16 vid_x, INT16 vid_y,
             CARD16 vid_w, CARD16 vid_h,
             INT16 drw_x, INT16 drw_y, CARD16 drw_w, CARD16 drw_h)
{
    int status;

    CHECK_SIZE(drw_w, drw_h, vid_w, vid_h);

    /* UPDATE TIME VARIABLES FOR USE IN EVENTS */

    UpdateCurrentTime();

    /* CHECK FOR GRAB; IF THIS CLIENT DOESN'T HAVE THE PORT GRABBED THEN
       INFORM CLIENT OF ITS FAILURE */

    if (pPort->grab.client && (pPort->grab.client != client)) {
        XvdiSendVideoNotify(pPort, pDraw, XvBusy);
        return Success;
    }

    pPort->time = currentTime;

    status = (*pPort->pAdaptor->ddPutStill) (client, pDraw, pPort, pGC,
                                             vid_x, vid_y, vid_w, vid_h,
                                             drw_x, drw_y, drw_w, drw_h);

    return status;

}

int
XvdiPutImage(ClientPtr client,
             DrawablePtr pDraw,
             XvPortPtr pPort,
             GCPtr pGC,
             INT16 src_x, INT16 src_y,
             CARD16 src_w, CARD16 src_h,
             INT16 drw_x, INT16 drw_y,
             CARD16 drw_w, CARD16 drw_h,
             XvImagePtr image,
             unsigned char *data, Bool sync, CARD16 width, CARD16 height)
{
    CHECK_SIZE(drw_w, drw_h, src_w, src_h);

    /* UPDATE TIME VARIABLES FOR USE IN EVENTS */

    UpdateCurrentTime();

    /* CHECK FOR GRAB; IF THIS CLIENT DOESN'T HAVE THE PORT GRABBED THEN
       INFORM CLIENT OF ITS FAILURE */

    if (pPort->grab.client && (pPort->grab.client != client)) {
        XvdiSendVideoNotify(pPort, pDraw, XvBusy);
        return Success;
    }

    pPort->time = currentTime;

    return (*pPort->pAdaptor->ddPutImage) (client, pDraw, pPort, pGC,
                                           src_x, src_y, src_w, src_h,
                                           drw_x, drw_y, drw_w, drw_h,
                                           image, data, sync, width, height);
}

int
XvdiGetVideo(ClientPtr client,
             DrawablePtr pDraw,
             XvPortPtr pPort,
             GCPtr pGC,
             INT16 vid_x, INT16 vid_y,
             CARD16 vid_w, CARD16 vid_h,
             INT16 drw_x, INT16 drw_y, CARD16 drw_w, CARD16 drw_h)
{
    DrawablePtr pOldDraw;

    CHECK_SIZE(drw_w, drw_h, vid_w, vid_h);

    /* UPDATE TIME VARIABLES FOR USE IN EVENTS */

    UpdateCurrentTime();

    /* CHECK FOR GRAB; IF THIS CLIENT DOESN'T HAVE THE PORT GRABBED THEN
       INFORM CLIENT OF ITS FAILURE */

    if (pPort->grab.client && (pPort->grab.client != client)) {
        XvdiSendVideoNotify(pPort, pDraw, XvBusy);
        return Success;
    }

    /* CHECK TO SEE IF PORT IS IN USE; IF SO THEN WE MUST DELIVER INTERRUPTED
       EVENTS TO ANY CLIENTS WHO WANT THEM */

    pOldDraw = pPort->pDraw;
    if ((pOldDraw) && (pOldDraw != pDraw)) {
        XvdiSendVideoNotify(pPort, pPort->pDraw, XvPreempted);
    }

    (void) (*pPort->pAdaptor->ddGetVideo) (client, pDraw, pPort, pGC,
                                           vid_x, vid_y, vid_w, vid_h,
                                           drw_x, drw_y, drw_w, drw_h);

    if ((pPort->pDraw) && (pOldDraw != pDraw)) {
        pPort->client = client;
        XvdiSendVideoNotify(pPort, pPort->pDraw, XvStarted);
    }

    pPort->time = currentTime;

    return Success;

}

int
XvdiGetStill(ClientPtr client,
             DrawablePtr pDraw,
             XvPortPtr pPort,
             GCPtr pGC,
             INT16 vid_x, INT16 vid_y,
             CARD16 vid_w, CARD16 vid_h,
             INT16 drw_x, INT16 drw_y, CARD16 drw_w, CARD16 drw_h)
{
    int status;

    CHECK_SIZE(drw_w, drw_h, vid_w, vid_h);

    /* UPDATE TIME VARIABLES FOR USE IN EVENTS */

    UpdateCurrentTime();

    /* CHECK FOR GRAB; IF THIS CLIENT DOESN'T HAVE THE PORT GRABBED THEN
       INFORM CLIENT OF ITS FAILURE */

    if (pPort->grab.client && (pPort->grab.client != client)) {
        XvdiSendVideoNotify(pPort, pDraw, XvBusy);
        return Success;
    }

    status = (*pPort->pAdaptor->ddGetStill) (client, pDraw, pPort, pGC,
                                             vid_x, vid_y, vid_w, vid_h,
                                             drw_x, drw_y, drw_w, drw_h);

    pPort->time = currentTime;

    return status;

}

int
XvdiGrabPort(ClientPtr client, XvPortPtr pPort, Time ctime, int *p_result)
{
    unsigned long id;
    TimeStamp time;

    UpdateCurrentTime();
    time = ClientTimeToServerTime(ctime);

    if (pPort->grab.client && (client != pPort->grab.client)) {
        *p_result = XvAlreadyGrabbed;
        return Success;
    }

    if ((CompareTimeStamps(time, currentTime) == LATER) ||
        (CompareTimeStamps(time, pPort->time) == EARLIER)) {
        *p_result = XvInvalidTime;
        return Success;
    }

    if (client == pPort->grab.client) {
        *p_result = Success;
        return Success;
    }

    id = FakeClientID(client->index);

    if (!AddResource(id, XvRTGrab, &pPort->grab)) {
        return BadAlloc;
    }

    /* IF THERE IS ACTIVE VIDEO THEN STOP IT */

    if ((pPort->pDraw) && (client != pPort->client)) {
        XvdiStopVideo(NULL, pPort, pPort->pDraw);
    }

    pPort->grab.client = client;
    pPort->grab.id = id;

    pPort->time = currentTime;

    *p_result = Success;

    return Success;

}

int
XvdiUngrabPort(ClientPtr client, XvPortPtr pPort, Time ctime)
{
    TimeStamp time;

    UpdateCurrentTime();
    time = ClientTimeToServerTime(ctime);

    if ((!pPort->grab.client) || (client != pPort->grab.client)) {
        return Success;
    }

    if ((CompareTimeStamps(time, currentTime) == LATER) ||
        (CompareTimeStamps(time, pPort->time) == EARLIER)) {
        return Success;
    }

    /* FREE THE GRAB RESOURCE; AND SET THE GRAB CLIENT TO NULL */

    FreeResource(pPort->grab.id, XvRTGrab);
    pPort->grab.client = NULL;

    pPort->time = currentTime;

    return Success;

}

int
XvdiSelectVideoNotify(ClientPtr client, DrawablePtr pDraw, BOOL onoff)
{
    XvVideoNotifyPtr pn, tpn, fpn;
    int rc;

    /* FIND VideoNotify LIST */

    rc = dixLookupResourceByType((pointer *) &pn, pDraw->id,
                                 XvRTVideoNotifyList, client, DixWriteAccess);
    if (rc != Success && rc != BadValue)
        return rc;

    /* IF ONE DONES'T EXIST AND NO MASK, THEN JUST RETURN */

    if (!onoff && !pn)
        return Success;

    /* IF ONE DOESN'T EXIST CREATE IT AND ADD A RESOURCE SO THAT THE LIST
       WILL BE DELETED WHEN THE DRAWABLE IS DESTROYED */

    if (!pn) {
        if (!(tpn = malloc(sizeof(XvVideoNotifyRec))))
            return BadAlloc;
        tpn->next = NULL;
        if (!AddResource(pDraw->id, XvRTVideoNotifyList, tpn)) {
            free(tpn);
            return BadAlloc;
        }
    }
    else {
        /* LOOK TO SEE IF ENTRY ALREADY EXISTS */

        fpn = NULL;
        tpn = pn;
        while (tpn) {
            if (tpn->client == client) {
                if (!onoff)
                    tpn->client = NULL;
                return Success;
            }
            if (!tpn->client)
                fpn = tpn;      /* TAKE NOTE OF FREE ENTRY */
            tpn = tpn->next;
        }

        /* IF TUNNING OFF, THEN JUST RETURN */

        if (!onoff)
            return Success;

        /* IF ONE ISN'T FOUND THEN ALLOCATE ONE AND LINK IT INTO THE LIST */

        if (fpn) {
            tpn = fpn;
        }
        else {
            if (!(tpn = malloc(sizeof(XvVideoNotifyRec))))
                return BadAlloc;
            tpn->next = pn->next;
            pn->next = tpn;
        }
    }

    /* INIT CLIENT PTR IN CASE WE CAN'T ADD RESOURCE */
    /* ADD RESOURCE SO THAT IF CLIENT EXITS THE CLIENT PTR WILL BE CLEARED */

    tpn->client = NULL;
    tpn->id = FakeClientID(client->index);
    AddResource(tpn->id, XvRTVideoNotify, tpn);

    tpn->client = client;
    return Success;

}

int
XvdiSelectPortNotify(ClientPtr client, XvPortPtr pPort, BOOL onoff)
{
    XvPortNotifyPtr pn, tpn;

    /* SEE IF CLIENT IS ALREADY IN LIST */

    tpn = NULL;
    pn = pPort->pNotify;
    while (pn) {
        if (!pn->client)
            tpn = pn;           /* TAKE NOTE OF FREE ENTRY */
        if (pn->client == client)
            break;
        pn = pn->next;
    }

    /* IS THE CLIENT ALREADY ON THE LIST? */

    if (pn) {
        /* REMOVE IT? */

        if (!onoff) {
            pn->client = NULL;
            FreeResource(pn->id, XvRTPortNotify);
        }

        return Success;
    }

    /* DIDN'T FIND IT; SO REUSE LIST ELEMENT IF ONE IS FREE OTHERWISE 
       CREATE A NEW ONE AND ADD IT TO THE BEGINNING OF THE LIST */

    if (!tpn) {
        if (!(tpn = malloc(sizeof(XvPortNotifyRec))))
            return BadAlloc;
        tpn->next = pPort->pNotify;
        pPort->pNotify = tpn;
    }

    tpn->client = client;
    tpn->id = FakeClientID(client->index);
    AddResource(tpn->id, XvRTPortNotify, tpn);

    return Success;

}

int
XvdiStopVideo(ClientPtr client, XvPortPtr pPort, DrawablePtr pDraw)
{
    int status;

    /* IF PORT ISN'T ACTIVE THEN WE'RE DONE */

    if (!pPort->pDraw || (pPort->pDraw != pDraw)) {
        XvdiSendVideoNotify(pPort, pDraw, XvStopped);
        return Success;
    }

    /* CHECK FOR GRAB; IF THIS CLIENT DOESN'T HAVE THE PORT GRABBED THEN
       INFORM CLIENT OF ITS FAILURE */

    if ((client) && (pPort->grab.client) && (pPort->grab.client != client)) {
        XvdiSendVideoNotify(pPort, pDraw, XvBusy);
        return Success;
    }

    XvdiSendVideoNotify(pPort, pDraw, XvStopped);

    status = (*pPort->pAdaptor->ddStopVideo) (client, pPort, pDraw);

    pPort->pDraw = NULL;
    pPort->client = (ClientPtr) client;
    pPort->time = currentTime;

    return status;

}

int
XvdiPreemptVideo(ClientPtr client, XvPortPtr pPort, DrawablePtr pDraw)
{
    int status;

    /* IF PORT ISN'T ACTIVE THEN WE'RE DONE */

    if (!pPort->pDraw || (pPort->pDraw != pDraw))
        return Success;

    XvdiSendVideoNotify(pPort, pPort->pDraw, XvPreempted);

    status = (*pPort->pAdaptor->ddStopVideo) (client, pPort, pPort->pDraw);

    pPort->pDraw = NULL;
    pPort->client = (ClientPtr) client;
    pPort->time = currentTime;

    return status;

}

int
XvdiMatchPort(XvPortPtr pPort, DrawablePtr pDraw)
{

    XvAdaptorPtr pa;
    XvFormatPtr pf;
    int nf;

    pa = pPort->pAdaptor;

    if (pa->pScreen != pDraw->pScreen)
        return BadMatch;

    nf = pa->nFormats;
    pf = pa->pFormats;

    while (nf--) {
        if (pf->depth == pDraw->depth)
            return Success;
        pf++;
    }

    return BadMatch;

}

int
XvdiSetPortAttribute(ClientPtr client,
                     XvPortPtr pPort, Atom attribute, INT32 value)
{
    int status;

    status =
        (*pPort->pAdaptor->ddSetPortAttribute) (client, pPort, attribute,
                                                value);
    if (status == Success)
        XvdiSendPortNotify(pPort, attribute, value);

    return status;
}

int
XvdiGetPortAttribute(ClientPtr client,
                     XvPortPtr pPort, Atom attribute, INT32 *p_value)
{

    return
        (*pPort->pAdaptor->ddGetPortAttribute) (client, pPort, attribute,
                                                p_value);

}

static void
WriteSwappedVideoNotifyEvent(xvEvent * from, xvEvent * to)
{

    to->u.u.type = from->u.u.type;
    to->u.u.detail = from->u.u.detail;
    cpswaps(from->u.videoNotify.sequenceNumber,
            to->u.videoNotify.sequenceNumber);
    cpswapl(from->u.videoNotify.time, to->u.videoNotify.time);
    cpswapl(from->u.videoNotify.drawable, to->u.videoNotify.drawable);
    cpswapl(from->u.videoNotify.port, to->u.videoNotify.port);

}

static void
WriteSwappedPortNotifyEvent(xvEvent * from, xvEvent * to)
{

    to->u.u.type = from->u.u.type;
    to->u.u.detail = from->u.u.detail;
    cpswaps(from->u.portNotify.sequenceNumber, to->u.portNotify.sequenceNumber);
    cpswapl(from->u.portNotify.time, to->u.portNotify.time);
    cpswapl(from->u.portNotify.port, to->u.portNotify.port);
    cpswapl(from->u.portNotify.value, to->u.portNotify.value);

}
