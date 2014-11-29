
/*

Copyright 1995  Kaleb S. KEITHLEY

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL Kaleb S. KEITHLEY BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Kaleb S. KEITHLEY
shall not be used in advertising or otherwise to promote the sale, use
or other dealings in this Software without prior written authorization
from Kaleb S. KEITHLEY

*/
/* THIS IS NOT AN X CONSORTIUM STANDARD OR AN X PROJECT TEAM SPECIFICATION */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include <X11/Xproto.h>
#include "misc.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "xf86Extensions.h"
#include "scrnintstr.h"
#include "servermd.h"
#include <X11/extensions/xf86vmproto.h>
#include "swaprep.h"
#include "xf86.h"
#include "vidmodeproc.h"
#include "globals.h"
#include "protocol-versions.h"

#define DEFAULT_XF86VIDMODE_VERBOSITY	3

static int VidModeErrorBase;
static DevPrivateKeyRec VidModeClientPrivateKeyRec;

#define VidModeClientPrivateKey (&VidModeClientPrivateKeyRec)

/* This holds the client's version information */
typedef struct {
    int major;
    int minor;
} VidModePrivRec, *VidModePrivPtr;

#define VM_GETPRIV(c) ((VidModePrivPtr) \
    dixLookupPrivate(&(c)->devPrivates, VidModeClientPrivateKey))
#define VM_SETPRIV(c,p) \
    dixSetPrivate(&(c)->devPrivates, VidModeClientPrivateKey, p)

#if 0
static unsigned char XF86VidModeReqCode = 0;
#endif

/* The XF86VIDMODE_EVENTS code is far from complete */

#ifdef XF86VIDMODE_EVENTS
static int XF86VidModeEventBase = 0;

static void SXF86VidModeNotifyEvent(xXF86VidModeNotifyEvent * /* from */ , xXF86VidModeNotifyEvent *    /* to */
    );

static RESTYPE EventType;       /* resource type for event masks */

typedef struct _XF86VidModeEvent *XF86VidModeEventPtr;

typedef struct _XF86VidModeEvent {
    XF86VidModeEventPtr next;
    ClientPtr client;
    ScreenPtr screen;
    XID resource;
    CARD32 mask;
} XF86VidModeEventRec;

static int XF86VidModeFreeEvents();

typedef struct _XF86VidModeScreenPrivate {
    XF86VidModeEventPtr events;
    Bool hasWindow;
} XF86VidModeScreenPrivateRec, *XF86VidModeScreenPrivatePtr;

static DevPrivateKeyRec ScreenPrivateKeyRec;

#define ScreenPrivateKey (&ScreenPrivateKeyRec)

#define GetScreenPrivate(s) ((ScreenSaverScreenPrivatePtr) \
    dixLookupPrivate(&(s)->devPrivates, ScreenPrivateKey))
#define SetScreenPrivate(s,v) \
    dixSetPrivate(&(s)->devPrivates, ScreenPrivateKey, v)
#define SetupScreen(s)  ScreenSaverScreenPrivatePtr pPriv = GetScreenPrivate(s)

#define New(t)  (malloc(sizeof (t)))
#endif

#ifdef DEBUG
#define DEBUG_P(x) ErrorF(x"\n");
#else
#define DEBUG_P(x) /**/
#endif
    static int
ClientMajorVersion(ClientPtr client)
{
    VidModePrivPtr pPriv;

    pPriv = VM_GETPRIV(client);
    if (!pPriv)
        return 0;
    else
        return pPriv->major;
}

#ifdef XF86VIDMODE_EVENTS
static void
CheckScreenPrivate(pScreen)
ScreenPtr
 pScreen;
{
    SetupScreen(pScreen);

    if (!pPriv)
        return;
    if (!pPriv->events && !pPriv->hasWindow) {
        free(pPriv);
        SetScreenPrivate(pScreen, NULL);
    }
}

static XF86VidModeScreenPrivatePtr
MakeScreenPrivate(pScreen)
ScreenPtr
 pScreen;
{
    SetupScreen(pScreen);

    if (pPriv)
        return pPriv;
    pPriv = New(XF86VidModeScreenPrivateRec);
    if (!pPriv)
        return 0;
    pPriv->events = 0;
    pPriv->hasWindow = FALSE;
    SetScreenPrivate(pScreen, pPriv);
    return pPriv;
}

static unsigned long
getEventMask(ScreenPtr pScreen, ClientPtr client)
{
    SetupScreen(pScreen);
    XF86VidModeEventPtr pEv;

    if (!pPriv)
        return 0;
    for (pEv = pPriv->events; pEv; pEv = pEv->next)
        if (pEv->client == client)
            return pEv->mask;
    return 0;
}

static Bool
setEventMask(ScreenPtr pScreen, ClientPtr client, unsigned long mask)
{
    SetupScreen(pScreen);
    XF86VidModeEventPtr pEv, *pPrev;

    if (getEventMask(pScreen, client) == mask)
        return TRUE;
    if (!pPriv) {
        pPriv = MakeScreenPrivate(pScreen);
        if (!pPriv)
            return FALSE;
    }
    for (pPrev = &pPriv->events; pEv = *pPrev; pPrev = &pEv->next)
        if (pEv->client == client)
            break;
    if (mask == 0) {
        *pPrev = pEv->next;
        free(pEv);
        CheckScreenPrivate(pScreen);
    }
    else {
        if (!pEv) {
            pEv = New(ScreenSaverEventRec);
            if (!pEv) {
                CheckScreenPrivate(pScreen);
                return FALSE;
            }
            *pPrev = pEv;
            pEv->next = NULL;
            pEv->client = client;
            pEv->screen = pScreen;
            pEv->resource = FakeClientID(client->index);
        }
        pEv->mask = mask;
    }
    return TRUE;
}

static int
XF86VidModeFreeEvents(void *value, XID id)
{
    XF86VidModeEventPtr pOld = (XF86VidModeEventPtr) value;
    ScreenPtr pScreen = pOld->screen;

    SetupScreen(pScreen);
    XF86VidModeEventPtr pEv, *pPrev;

    if (!pPriv)
        return TRUE;
    for (pPrev = &pPriv->events; pEv = *pPrev; pPrev = &pEv->next)
        if (pEv == pOld)
            break;
    if (!pEv)
        return TRUE;
    *pPrev = pEv->next;
    free(pEv);
    CheckScreenPrivate(pScreen);
    return TRUE;
}

static void
SendXF86VidModeNotify(ScreenPtr pScreen, int state, Bool forced)
{
    XF86VidModeScreenPrivatePtr pPriv;
    unsigned long mask;
    xXF86VidModeNotifyEvent ev;
    int kind;

    UpdateCurrentTimeIf();
    mask = XF86VidModeNotifyMask;
    pScreen = screenInfo.screens[pScreen->myNum];
    pPriv = GetScreenPrivate(pScreen);
    if (!pPriv)
        return;
    kind = XF86VidModeModeChange;
    for (pEv = pPriv->events; pEv; pEv = pEv->next) {
        if (pEv->mask & mask) {
            XF86VidModeEventPtr pEv = {
                .type = XF86VidModeNotify + XF86VidModeEventBase,
                .state = state,
                .timestamp = currentTime.milliseconds,
                .root = pScreen->root->drawable.id,
                .kind = kind,
                .forced = forced
            };
            WriteEventsToClient(pEv->client, 1, (xEvent *) &ev);
        }
    }
}

static void
SXF86VidModeNotifyEvent(xXF86VidModeNotifyEvent * from,
                        xXF86VidModeNotifyEvent * to)
{
    to->type = from->type;
    to->state = from->state;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->timestamp, to->timestamp);
    cpswapl(from->root, to->root);
    to->kind = from->kind;
    to->forced = from->forced;
}
#endif

static int
ProcXF86VidModeQueryVersion(ClientPtr client)
{
    xXF86VidModeQueryVersionReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = SERVER_XF86VIDMODE_MAJOR_VERSION,
        .minorVersion = SERVER_XF86VIDMODE_MINOR_VERSION
    };

    DEBUG_P("XF86VidModeQueryVersion");

    REQUEST_SIZE_MATCH(xXF86VidModeQueryVersionReq);

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swaps(&rep.majorVersion);
        swaps(&rep.minorVersion);
    }
    WriteToClient(client, sizeof(xXF86VidModeQueryVersionReply), &rep);
    return Success;
}

static int
ProcXF86VidModeGetModeLine(ClientPtr client)
{
    REQUEST(xXF86VidModeGetModeLineReq);
    xXF86VidModeGetModeLineReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence
    };
    void *mode;
    int dotClock;
    int ver;

    DEBUG_P("XF86VidModeGetModeline");

    ver = ClientMajorVersion(client);
    REQUEST_SIZE_MATCH(xXF86VidModeGetModeLineReq);

    if (ver < 2) {
        rep.length = bytes_to_int32(SIZEOF(xXF86OldVidModeGetModeLineReply) -
                                    SIZEOF(xGenericReply));
    }
    else {
        rep.length = bytes_to_int32(SIZEOF(xXF86VidModeGetModeLineReply) -
                                    SIZEOF(xGenericReply));
    }

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeGetCurrentModeline(stuff->screen, &mode, &dotClock))
        return BadValue;

    rep.dotclock = dotClock;
    rep.hdisplay = VidModeGetModeValue(mode, VIDMODE_H_DISPLAY);
    rep.hsyncstart = VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART);
    rep.hsyncend = VidModeGetModeValue(mode, VIDMODE_H_SYNCEND);
    rep.htotal = VidModeGetModeValue(mode, VIDMODE_H_TOTAL);
    rep.hskew = VidModeGetModeValue(mode, VIDMODE_H_SKEW);
    rep.vdisplay = VidModeGetModeValue(mode, VIDMODE_V_DISPLAY);
    rep.vsyncstart = VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART);
    rep.vsyncend = VidModeGetModeValue(mode, VIDMODE_V_SYNCEND);
    rep.vtotal = VidModeGetModeValue(mode, VIDMODE_V_TOTAL);
    rep.flags = VidModeGetModeValue(mode, VIDMODE_FLAGS);

    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
        ErrorF("GetModeLine - scrn: %d clock: %ld\n",
               stuff->screen, (unsigned long) rep.dotclock);
        ErrorF("GetModeLine - hdsp: %d hbeg: %d hend: %d httl: %d\n",
               rep.hdisplay, rep.hsyncstart, rep.hsyncend, rep.htotal);
        ErrorF("              vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
               rep.vdisplay, rep.vsyncstart, rep.vsyncend,
               rep.vtotal, (unsigned long) rep.flags);
    }

    /*
     * Older servers sometimes had server privates that the VidMode
     * extention made available. So to be compatiable pretend that
     * there are no server privates to pass to the client
     */
    rep.privsize = 0;

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.dotclock);
        swaps(&rep.hdisplay);
        swaps(&rep.hsyncstart);
        swaps(&rep.hsyncend);
        swaps(&rep.htotal);
        swaps(&rep.hskew);
        swaps(&rep.vdisplay);
        swaps(&rep.vsyncstart);
        swaps(&rep.vsyncend);
        swaps(&rep.vtotal);
        swapl(&rep.flags);
        swapl(&rep.privsize);
    }
    if (ver < 2) {
        xXF86OldVidModeGetModeLineReply oldrep = {
            .type = rep.type,
            .sequenceNumber = rep.sequenceNumber,
            .length = rep.length,
            .dotclock = rep.dotclock,
            .hdisplay = rep.hdisplay,
            .hsyncstart = rep.hsyncstart,
            .hsyncend = rep.hsyncend,
            .htotal = rep.htotal,
            .vdisplay = rep.vdisplay,
            .vsyncstart = rep.vsyncstart,
            .vsyncend = rep.vsyncend,
            .vtotal = rep.vtotal,
            .flags = rep.flags,
            .privsize = rep.privsize
        };
        WriteToClient(client, sizeof(xXF86OldVidModeGetModeLineReply), &oldrep);
    }
    else {
        WriteToClient(client, sizeof(xXF86VidModeGetModeLineReply), &rep);
    }
    return Success;
}

static int
ProcXF86VidModeGetAllModeLines(ClientPtr client)
{
    REQUEST(xXF86VidModeGetAllModeLinesReq);
    xXF86VidModeGetAllModeLinesReply rep;
    void *mode;
    int modecount, dotClock;
    int ver;

    DEBUG_P("XF86VidModeGetAllModelines");

    REQUEST_SIZE_MATCH(xXF86VidModeGetAllModeLinesReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    ver = ClientMajorVersion(client);

    modecount = VidModeGetNumOfModes(stuff->screen);
    if (modecount < 1)
        return VidModeErrorBase + XF86VidModeExtensionDisabled;

    if (!VidModeGetFirstModeline(stuff->screen, &mode, &dotClock))
        return BadValue;

    rep = (xXF86VidModeGetAllModeLinesReply) {
        .type = X_Reply,
        .length = SIZEOF(xXF86VidModeGetAllModeLinesReply) -
            SIZEOF(xGenericReply),
        .sequenceNumber = client->sequence,
        .modecount = modecount
    };
    if (ver < 2)
        rep.length += modecount * sizeof(xXF86OldVidModeModeInfo);
    else
        rep.length += modecount * sizeof(xXF86VidModeModeInfo);
    rep.length >>= 2;
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.modecount);
    }
    WriteToClient(client, sizeof(xXF86VidModeGetAllModeLinesReply), &rep);

    do {
        xXF86VidModeModeInfo mdinf = {
            .dotclock = dotClock,
            .hdisplay = VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
            .hsyncstart = VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
            .hsyncend = VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
            .htotal = VidModeGetModeValue(mode, VIDMODE_H_TOTAL),
            .hskew = VidModeGetModeValue(mode, VIDMODE_H_SKEW),
            .vdisplay = VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
            .vsyncstart = VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
            .vsyncend = VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
            .vtotal = VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
            .flags = VidModeGetModeValue(mode, VIDMODE_FLAGS),
            .privsize = 0
        };
        if (client->swapped) {
            swapl(&mdinf.dotclock);
            swaps(&mdinf.hdisplay);
            swaps(&mdinf.hsyncstart);
            swaps(&mdinf.hsyncend);
            swaps(&mdinf.htotal);
            swapl(&mdinf.hskew);
            swaps(&mdinf.vdisplay);
            swaps(&mdinf.vsyncstart);
            swaps(&mdinf.vsyncend);
            swaps(&mdinf.vtotal);
            swapl(&mdinf.flags);
            swapl(&mdinf.privsize);
        }
        if (ver < 2) {
            xXF86OldVidModeModeInfo oldmdinf = {
                .dotclock = mdinf.dotclock,
                .hdisplay = mdinf.hdisplay,
                .hsyncstart = mdinf.hsyncstart,
                .hsyncend = mdinf.hsyncend,
                .htotal = mdinf.htotal,
                .vdisplay = mdinf.vdisplay,
                .vsyncstart = mdinf.vsyncstart,
                .vsyncend = mdinf.vsyncend,
                .vtotal = mdinf.vtotal,
                .flags = mdinf.flags,
                .privsize = mdinf.privsize
            };
            WriteToClient(client, sizeof(xXF86OldVidModeModeInfo), &oldmdinf);
        }
        else {
            WriteToClient(client, sizeof(xXF86VidModeModeInfo), &mdinf);
        }

    } while (VidModeGetNextModeline(stuff->screen, &mode, &dotClock));

    return Success;
}

#define MODEMATCH(mode,stuff)	  \
     (VidModeGetModeValue(mode, VIDMODE_H_DISPLAY)  == stuff->hdisplay \
     && VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART)  == stuff->hsyncstart \
     && VidModeGetModeValue(mode, VIDMODE_H_SYNCEND)  == stuff->hsyncend \
     && VidModeGetModeValue(mode, VIDMODE_H_TOTAL)  == stuff->htotal \
     && VidModeGetModeValue(mode, VIDMODE_V_DISPLAY)  == stuff->vdisplay \
     && VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART)  == stuff->vsyncstart \
     && VidModeGetModeValue(mode, VIDMODE_V_SYNCEND)  == stuff->vsyncend \
     && VidModeGetModeValue(mode, VIDMODE_V_TOTAL)  == stuff->vtotal \
     && VidModeGetModeValue(mode, VIDMODE_FLAGS)  == stuff->flags )

static int
ProcXF86VidModeAddModeLine(ClientPtr client)
{
    REQUEST(xXF86VidModeAddModeLineReq);
    xXF86OldVidModeAddModeLineReq *oldstuff =
        (xXF86OldVidModeAddModeLineReq *) client->requestBuffer;
    xXF86VidModeAddModeLineReq newstuff;
    void *mode;
    int len;
    int dotClock;
    int ver;

    DEBUG_P("XF86VidModeAddModeline");

    ver = ClientMajorVersion(client);
    if (ver < 2) {
        /* convert from old format */
        stuff = &newstuff;
        stuff->length = oldstuff->length;
        stuff->screen = oldstuff->screen;
        stuff->dotclock = oldstuff->dotclock;
        stuff->hdisplay = oldstuff->hdisplay;
        stuff->hsyncstart = oldstuff->hsyncstart;
        stuff->hsyncend = oldstuff->hsyncend;
        stuff->htotal = oldstuff->htotal;
        stuff->hskew = 0;
        stuff->vdisplay = oldstuff->vdisplay;
        stuff->vsyncstart = oldstuff->vsyncstart;
        stuff->vsyncend = oldstuff->vsyncend;
        stuff->vtotal = oldstuff->vtotal;
        stuff->flags = oldstuff->flags;
        stuff->privsize = oldstuff->privsize;
        stuff->after_dotclock = oldstuff->after_dotclock;
        stuff->after_hdisplay = oldstuff->after_hdisplay;
        stuff->after_hsyncstart = oldstuff->after_hsyncstart;
        stuff->after_hsyncend = oldstuff->after_hsyncend;
        stuff->after_htotal = oldstuff->after_htotal;
        stuff->after_hskew = 0;
        stuff->after_vdisplay = oldstuff->after_vdisplay;
        stuff->after_vsyncstart = oldstuff->after_vsyncstart;
        stuff->after_vsyncend = oldstuff->after_vsyncend;
        stuff->after_vtotal = oldstuff->after_vtotal;
        stuff->after_flags = oldstuff->after_flags;
    }
    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
        ErrorF("AddModeLine - scrn: %d clock: %ld\n",
               (int) stuff->screen, (unsigned long) stuff->dotclock);
        ErrorF("AddModeLine - hdsp: %d hbeg: %d hend: %d httl: %d\n",
               stuff->hdisplay, stuff->hsyncstart,
               stuff->hsyncend, stuff->htotal);
        ErrorF("              vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
               stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend,
               stuff->vtotal, (unsigned long) stuff->flags);
        ErrorF("      after - scrn: %d clock: %ld\n",
               (int) stuff->screen, (unsigned long) stuff->after_dotclock);
        ErrorF("              hdsp: %d hbeg: %d hend: %d httl: %d\n",
               stuff->after_hdisplay, stuff->after_hsyncstart,
               stuff->after_hsyncend, stuff->after_htotal);
        ErrorF("              vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
               stuff->after_vdisplay, stuff->after_vsyncstart,
               stuff->after_vsyncend, stuff->after_vtotal,
               (unsigned long) stuff->after_flags);
    }

    if (ver < 2) {
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeAddModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeAddModeLineReq));
    }
    else {
        REQUEST_AT_LEAST_SIZE(xXF86VidModeAddModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeAddModeLineReq));
    }
    if (len != stuff->privsize)
        return BadLength;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (stuff->hsyncstart < stuff->hdisplay ||
        stuff->hsyncend < stuff->hsyncstart ||
        stuff->htotal < stuff->hsyncend ||
        stuff->vsyncstart < stuff->vdisplay ||
        stuff->vsyncend < stuff->vsyncstart || stuff->vtotal < stuff->vsyncend)
        return BadValue;

    if (stuff->after_hsyncstart < stuff->after_hdisplay ||
        stuff->after_hsyncend < stuff->after_hsyncstart ||
        stuff->after_htotal < stuff->after_hsyncend ||
        stuff->after_vsyncstart < stuff->after_vdisplay ||
        stuff->after_vsyncend < stuff->after_vsyncstart ||
        stuff->after_vtotal < stuff->after_vsyncend)
        return BadValue;

    if (stuff->after_htotal != 0 || stuff->after_vtotal != 0) {
        Bool found = FALSE;

        if (VidModeGetFirstModeline(stuff->screen, &mode, &dotClock)) {
            do {
                if ((VidModeGetDotClock(stuff->screen, stuff->dotclock)
                     == dotClock) && MODEMATCH(mode, stuff)) {
                    found = TRUE;
                    break;
                }
            } while (VidModeGetNextModeline(stuff->screen, &mode, &dotClock));
        }
        if (!found)
            return BadValue;
    }

    mode = VidModeCreateMode();
    if (mode == NULL)
        return BadValue;

    VidModeSetModeValue(mode, VIDMODE_CLOCK, stuff->dotclock);
    VidModeSetModeValue(mode, VIDMODE_H_DISPLAY, stuff->hdisplay);
    VidModeSetModeValue(mode, VIDMODE_H_SYNCSTART, stuff->hsyncstart);
    VidModeSetModeValue(mode, VIDMODE_H_SYNCEND, stuff->hsyncend);
    VidModeSetModeValue(mode, VIDMODE_H_TOTAL, stuff->htotal);
    VidModeSetModeValue(mode, VIDMODE_H_SKEW, stuff->hskew);
    VidModeSetModeValue(mode, VIDMODE_V_DISPLAY, stuff->vdisplay);
    VidModeSetModeValue(mode, VIDMODE_V_SYNCSTART, stuff->vsyncstart);
    VidModeSetModeValue(mode, VIDMODE_V_SYNCEND, stuff->vsyncend);
    VidModeSetModeValue(mode, VIDMODE_V_TOTAL, stuff->vtotal);
    VidModeSetModeValue(mode, VIDMODE_FLAGS, stuff->flags);

    if (stuff->privsize)
        ErrorF("AddModeLine - Privates in request have been ignored\n");

    /* Check that the mode is consistent with the monitor specs */
    switch (VidModeCheckModeForMonitor(stuff->screen, mode)) {
    case MODE_OK:
        break;
    case MODE_HSYNC:
    case MODE_H_ILLEGAL:
        free(mode);
        return VidModeErrorBase + XF86VidModeBadHTimings;
    case MODE_VSYNC:
    case MODE_V_ILLEGAL:
        free(mode);
        return VidModeErrorBase + XF86VidModeBadVTimings;
    default:
        free(mode);
        return VidModeErrorBase + XF86VidModeModeUnsuitable;
    }

    /* Check that the driver is happy with the mode */
    if (VidModeCheckModeForDriver(stuff->screen, mode) != MODE_OK) {
        free(mode);
        return VidModeErrorBase + XF86VidModeModeUnsuitable;
    }

    VidModeSetCrtcForMode(stuff->screen, mode);

    VidModeAddModeline(stuff->screen, mode);

    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY)
        ErrorF("AddModeLine - Succeeded\n");
    return Success;
}

static int
ProcXF86VidModeDeleteModeLine(ClientPtr client)
{
    REQUEST(xXF86VidModeDeleteModeLineReq);
    xXF86OldVidModeDeleteModeLineReq *oldstuff =
        (xXF86OldVidModeDeleteModeLineReq *) client->requestBuffer;
    xXF86VidModeDeleteModeLineReq newstuff;
    void *mode;
    int len, dotClock;
    int ver;

    DEBUG_P("XF86VidModeDeleteModeline");

    ver = ClientMajorVersion(client);
    if (ver < 2) {
        /* convert from old format */
        stuff = &newstuff;
        stuff->length = oldstuff->length;
        stuff->screen = oldstuff->screen;
        stuff->dotclock = oldstuff->dotclock;
        stuff->hdisplay = oldstuff->hdisplay;
        stuff->hsyncstart = oldstuff->hsyncstart;
        stuff->hsyncend = oldstuff->hsyncend;
        stuff->htotal = oldstuff->htotal;
        stuff->hskew = 0;
        stuff->vdisplay = oldstuff->vdisplay;
        stuff->vsyncstart = oldstuff->vsyncstart;
        stuff->vsyncend = oldstuff->vsyncend;
        stuff->vtotal = oldstuff->vtotal;
        stuff->flags = oldstuff->flags;
        stuff->privsize = oldstuff->privsize;
    }
    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
        ErrorF("DeleteModeLine - scrn: %d clock: %ld\n",
               (int) stuff->screen, (unsigned long) stuff->dotclock);
        ErrorF("                 hdsp: %d hbeg: %d hend: %d httl: %d\n",
               stuff->hdisplay, stuff->hsyncstart,
               stuff->hsyncend, stuff->htotal);
        ErrorF
            ("                 vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
             stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend, stuff->vtotal,
             (unsigned long) stuff->flags);
    }

    if (ver < 2) {
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeDeleteModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeDeleteModeLineReq));
    }
    else {
        REQUEST_AT_LEAST_SIZE(xXF86VidModeDeleteModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeDeleteModeLineReq));
    }
    if (len != stuff->privsize) {
        if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
            ErrorF("req_len = %ld, sizeof(Req) = %d, privsize = %ld, "
                   "len = %d, length = %d\n",
                   (unsigned long) client->req_len,
                   (int) sizeof(xXF86VidModeDeleteModeLineReq) >> 2,
                   (unsigned long) stuff->privsize, len, stuff->length);
        }
        return BadLength;
    }

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeGetCurrentModeline(stuff->screen, &mode, &dotClock))
        return BadValue;

    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
        ErrorF("Checking against clock: %d (%d)\n",
               VidModeGetModeValue(mode, VIDMODE_CLOCK), dotClock);
        ErrorF("                 hdsp: %d hbeg: %d hend: %d httl: %d\n",
               VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
               VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
               VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
               VidModeGetModeValue(mode, VIDMODE_H_TOTAL));
        ErrorF
            ("                 vdsp: %d vbeg: %d vend: %d vttl: %d flags: %d\n",
             VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
             VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
             VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
             VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
             VidModeGetModeValue(mode, VIDMODE_FLAGS));
    }
    if ((VidModeGetDotClock(stuff->screen, stuff->dotclock) == dotClock) &&
        MODEMATCH(mode, stuff))
        return BadValue;

    if (!VidModeGetFirstModeline(stuff->screen, &mode, &dotClock))
        return BadValue;

    do {
        if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
            ErrorF("Checking against clock: %d (%d)\n",
                   VidModeGetModeValue(mode, VIDMODE_CLOCK), dotClock);
            ErrorF("                 hdsp: %d hbeg: %d hend: %d httl: %d\n",
                   VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
                   VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
                   VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
                   VidModeGetModeValue(mode, VIDMODE_H_TOTAL));
            ErrorF
                ("                 vdsp: %d vbeg: %d vend: %d vttl: %d flags: %d\n",
                 VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
                 VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
                 VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
                 VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
                 VidModeGetModeValue(mode, VIDMODE_FLAGS));
        }
        if ((VidModeGetDotClock(stuff->screen, stuff->dotclock) == dotClock) &&
            MODEMATCH(mode, stuff)) {
            VidModeDeleteModeline(stuff->screen, mode);
            if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY)
                ErrorF("DeleteModeLine - Succeeded\n");
            return Success;
        }
    } while (VidModeGetNextModeline(stuff->screen, &mode, &dotClock));

    return BadValue;
}

static int
ProcXF86VidModeModModeLine(ClientPtr client)
{
    REQUEST(xXF86VidModeModModeLineReq);
    xXF86OldVidModeModModeLineReq *oldstuff =
        (xXF86OldVidModeModModeLineReq *) client->requestBuffer;
    xXF86VidModeModModeLineReq newstuff;
    void *mode, *modetmp;
    int len, dotClock;
    int ver;

    DEBUG_P("XF86VidModeModModeline");

    ver = ClientMajorVersion(client);
    if (ver < 2) {
        /* convert from old format */
        stuff = &newstuff;
        stuff->length = oldstuff->length;
        stuff->screen = oldstuff->screen;
        stuff->hdisplay = oldstuff->hdisplay;
        stuff->hsyncstart = oldstuff->hsyncstart;
        stuff->hsyncend = oldstuff->hsyncend;
        stuff->htotal = oldstuff->htotal;
        stuff->hskew = 0;
        stuff->vdisplay = oldstuff->vdisplay;
        stuff->vsyncstart = oldstuff->vsyncstart;
        stuff->vsyncend = oldstuff->vsyncend;
        stuff->vtotal = oldstuff->vtotal;
        stuff->flags = oldstuff->flags;
        stuff->privsize = oldstuff->privsize;
    }
    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
        ErrorF("ModModeLine - scrn: %d hdsp: %d hbeg: %d hend: %d httl: %d\n",
               (int) stuff->screen, stuff->hdisplay, stuff->hsyncstart,
               stuff->hsyncend, stuff->htotal);
        ErrorF("              vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
               stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend,
               stuff->vtotal, (unsigned long) stuff->flags);
    }

    if (ver < 2) {
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeModModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeModModeLineReq));
    }
    else {
        REQUEST_AT_LEAST_SIZE(xXF86VidModeModModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeModModeLineReq));
    }
    if (len != stuff->privsize)
        return BadLength;

    if (stuff->hsyncstart < stuff->hdisplay ||
        stuff->hsyncend < stuff->hsyncstart ||
        stuff->htotal < stuff->hsyncend ||
        stuff->vsyncstart < stuff->vdisplay ||
        stuff->vsyncend < stuff->vsyncstart || stuff->vtotal < stuff->vsyncend)
        return BadValue;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeGetCurrentModeline(stuff->screen, &mode, &dotClock))
        return BadValue;

    modetmp = VidModeCreateMode();
    VidModeCopyMode(mode, modetmp);

    VidModeSetModeValue(modetmp, VIDMODE_H_DISPLAY, stuff->hdisplay);
    VidModeSetModeValue(modetmp, VIDMODE_H_SYNCSTART, stuff->hsyncstart);
    VidModeSetModeValue(modetmp, VIDMODE_H_SYNCEND, stuff->hsyncend);
    VidModeSetModeValue(modetmp, VIDMODE_H_TOTAL, stuff->htotal);
    VidModeSetModeValue(modetmp, VIDMODE_H_SKEW, stuff->hskew);
    VidModeSetModeValue(modetmp, VIDMODE_V_DISPLAY, stuff->vdisplay);
    VidModeSetModeValue(modetmp, VIDMODE_V_SYNCSTART, stuff->vsyncstart);
    VidModeSetModeValue(modetmp, VIDMODE_V_SYNCEND, stuff->vsyncend);
    VidModeSetModeValue(modetmp, VIDMODE_V_TOTAL, stuff->vtotal);
    VidModeSetModeValue(modetmp, VIDMODE_FLAGS, stuff->flags);

    if (stuff->privsize)
        ErrorF("ModModeLine - Privates in request have been ignored\n");

    /* Check that the mode is consistent with the monitor specs */
    switch (VidModeCheckModeForMonitor(stuff->screen, modetmp)) {
    case MODE_OK:
        break;
    case MODE_HSYNC:
    case MODE_H_ILLEGAL:
        free(modetmp);
        return VidModeErrorBase + XF86VidModeBadHTimings;
    case MODE_VSYNC:
    case MODE_V_ILLEGAL:
        free(modetmp);
        return VidModeErrorBase + XF86VidModeBadVTimings;
    default:
        free(modetmp);
        return VidModeErrorBase + XF86VidModeModeUnsuitable;
    }

    /* Check that the driver is happy with the mode */
    if (VidModeCheckModeForDriver(stuff->screen, modetmp) != MODE_OK) {
        free(modetmp);
        return VidModeErrorBase + XF86VidModeModeUnsuitable;
    }
    free(modetmp);

    VidModeSetModeValue(mode, VIDMODE_H_DISPLAY, stuff->hdisplay);
    VidModeSetModeValue(mode, VIDMODE_H_SYNCSTART, stuff->hsyncstart);
    VidModeSetModeValue(mode, VIDMODE_H_SYNCEND, stuff->hsyncend);
    VidModeSetModeValue(mode, VIDMODE_H_TOTAL, stuff->htotal);
    VidModeSetModeValue(mode, VIDMODE_H_SKEW, stuff->hskew);
    VidModeSetModeValue(mode, VIDMODE_V_DISPLAY, stuff->vdisplay);
    VidModeSetModeValue(mode, VIDMODE_V_SYNCSTART, stuff->vsyncstart);
    VidModeSetModeValue(mode, VIDMODE_V_SYNCEND, stuff->vsyncend);
    VidModeSetModeValue(mode, VIDMODE_V_TOTAL, stuff->vtotal);
    VidModeSetModeValue(mode, VIDMODE_FLAGS, stuff->flags);

    VidModeSetCrtcForMode(stuff->screen, mode);
    VidModeSwitchMode(stuff->screen, mode);

    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY)
        ErrorF("ModModeLine - Succeeded\n");
    return Success;
}

static int
ProcXF86VidModeValidateModeLine(ClientPtr client)
{
    REQUEST(xXF86VidModeValidateModeLineReq);
    xXF86OldVidModeValidateModeLineReq *oldstuff =
        (xXF86OldVidModeValidateModeLineReq *) client->requestBuffer;
    xXF86VidModeValidateModeLineReq newstuff;
    xXF86VidModeValidateModeLineReply rep;
    void *mode, *modetmp = NULL;
    int len, status, dotClock;
    int ver;

    DEBUG_P("XF86VidModeValidateModeline");

    ver = ClientMajorVersion(client);
    if (ver < 2) {
        /* convert from old format */
        stuff = &newstuff;
        stuff->length = oldstuff->length;
        stuff->screen = oldstuff->screen;
        stuff->dotclock = oldstuff->dotclock;
        stuff->hdisplay = oldstuff->hdisplay;
        stuff->hsyncstart = oldstuff->hsyncstart;
        stuff->hsyncend = oldstuff->hsyncend;
        stuff->htotal = oldstuff->htotal;
        stuff->hskew = 0;
        stuff->vdisplay = oldstuff->vdisplay;
        stuff->vsyncstart = oldstuff->vsyncstart;
        stuff->vsyncend = oldstuff->vsyncend;
        stuff->vtotal = oldstuff->vtotal;
        stuff->flags = oldstuff->flags;
        stuff->privsize = oldstuff->privsize;
    }
    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
        ErrorF("ValidateModeLine - scrn: %d clock: %ld\n",
               (int) stuff->screen, (unsigned long) stuff->dotclock);
        ErrorF("                   hdsp: %d hbeg: %d hend: %d httl: %d\n",
               stuff->hdisplay, stuff->hsyncstart,
               stuff->hsyncend, stuff->htotal);
        ErrorF
            ("                   vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
             stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend, stuff->vtotal,
             (unsigned long) stuff->flags);
    }

    if (ver < 2) {
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeValidateModeLineReq);
        len = client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeValidateModeLineReq));
    }
    else {
        REQUEST_AT_LEAST_SIZE(xXF86VidModeValidateModeLineReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeValidateModeLineReq));
    }
    if (len != stuff->privsize)
        return BadLength;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    status = MODE_OK;

    if (stuff->hsyncstart < stuff->hdisplay ||
        stuff->hsyncend < stuff->hsyncstart ||
        stuff->htotal < stuff->hsyncend ||
        stuff->vsyncstart < stuff->vdisplay ||
        stuff->vsyncend < stuff->vsyncstart ||
        stuff->vtotal < stuff->vsyncend) {
        status = MODE_BAD;
        goto status_reply;
    }

    if (!VidModeGetCurrentModeline(stuff->screen, &mode, &dotClock))
        return BadValue;

    modetmp = VidModeCreateMode();
    VidModeCopyMode(mode, modetmp);

    VidModeSetModeValue(modetmp, VIDMODE_H_DISPLAY, stuff->hdisplay);
    VidModeSetModeValue(modetmp, VIDMODE_H_SYNCSTART, stuff->hsyncstart);
    VidModeSetModeValue(modetmp, VIDMODE_H_SYNCEND, stuff->hsyncend);
    VidModeSetModeValue(modetmp, VIDMODE_H_TOTAL, stuff->htotal);
    VidModeSetModeValue(modetmp, VIDMODE_H_SKEW, stuff->hskew);
    VidModeSetModeValue(modetmp, VIDMODE_V_DISPLAY, stuff->vdisplay);
    VidModeSetModeValue(modetmp, VIDMODE_V_SYNCSTART, stuff->vsyncstart);
    VidModeSetModeValue(modetmp, VIDMODE_V_SYNCEND, stuff->vsyncend);
    VidModeSetModeValue(modetmp, VIDMODE_V_TOTAL, stuff->vtotal);
    VidModeSetModeValue(modetmp, VIDMODE_FLAGS, stuff->flags);
    if (stuff->privsize)
        ErrorF("ValidateModeLine - Privates in request have been ignored\n");

    /* Check that the mode is consistent with the monitor specs */
    if ((status =
         VidModeCheckModeForMonitor(stuff->screen, modetmp)) != MODE_OK)
        goto status_reply;

    /* Check that the driver is happy with the mode */
    status = VidModeCheckModeForDriver(stuff->screen, modetmp);

 status_reply:
    free(modetmp);

    rep = (xXF86VidModeValidateModeLineReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = bytes_to_int32(SIZEOF(xXF86VidModeValidateModeLineReply)
                                 - SIZEOF(xGenericReply)),
        .status = status
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.status);
    }
    WriteToClient(client, sizeof(xXF86VidModeValidateModeLineReply), &rep);
    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY)
        ErrorF("ValidateModeLine - Succeeded (status = %d)\n", status);
    return Success;
}

static int
ProcXF86VidModeSwitchMode(ClientPtr client)
{
    REQUEST(xXF86VidModeSwitchModeReq);

    DEBUG_P("XF86VidModeSwitchMode");

    REQUEST_SIZE_MATCH(xXF86VidModeSwitchModeReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    VidModeZoomViewport(stuff->screen, (short) stuff->zoom);

    return Success;
}

static int
ProcXF86VidModeSwitchToMode(ClientPtr client)
{
    REQUEST(xXF86VidModeSwitchToModeReq);
    xXF86OldVidModeSwitchToModeReq *oldstuff =
        (xXF86OldVidModeSwitchToModeReq *) client->requestBuffer;
    xXF86VidModeSwitchToModeReq newstuff;
    void *mode;
    int len, dotClock;
    int ver;

    DEBUG_P("XF86VidModeSwitchToMode");

    ver = ClientMajorVersion(client);
    if (ver < 2) {
        /* convert from old format */
        stuff = &newstuff;
        stuff->length = oldstuff->length;
        stuff->screen = oldstuff->screen;
        stuff->dotclock = oldstuff->dotclock;
        stuff->hdisplay = oldstuff->hdisplay;
        stuff->hsyncstart = oldstuff->hsyncstart;
        stuff->hsyncend = oldstuff->hsyncend;
        stuff->htotal = oldstuff->htotal;
        stuff->hskew = 0;
        stuff->vdisplay = oldstuff->vdisplay;
        stuff->vsyncstart = oldstuff->vsyncstart;
        stuff->vsyncend = oldstuff->vsyncend;
        stuff->vtotal = oldstuff->vtotal;
        stuff->flags = oldstuff->flags;
        stuff->privsize = oldstuff->privsize;
    }
    if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
        ErrorF("SwitchToMode - scrn: %d clock: %ld\n",
               (int) stuff->screen, (unsigned long) stuff->dotclock);
        ErrorF("               hdsp: %d hbeg: %d hend: %d httl: %d\n",
               stuff->hdisplay, stuff->hsyncstart,
               stuff->hsyncend, stuff->htotal);
        ErrorF
            ("               vdsp: %d vbeg: %d vend: %d vttl: %d flags: %ld\n",
             stuff->vdisplay, stuff->vsyncstart, stuff->vsyncend, stuff->vtotal,
             (unsigned long) stuff->flags);
    }

    if (ver < 2) {
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeSwitchToModeReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86OldVidModeSwitchToModeReq));
    }
    else {
        REQUEST_AT_LEAST_SIZE(xXF86VidModeSwitchToModeReq);
        len =
            client->req_len -
            bytes_to_int32(sizeof(xXF86VidModeSwitchToModeReq));
    }
    if (len != stuff->privsize)
        return BadLength;

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeGetCurrentModeline(stuff->screen, &mode, &dotClock))
        return BadValue;

    if ((VidModeGetDotClock(stuff->screen, stuff->dotclock) == dotClock)
        && MODEMATCH(mode, stuff))
        return Success;

    if (!VidModeGetFirstModeline(stuff->screen, &mode, &dotClock))
        return BadValue;

    do {
        if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY) {
            ErrorF("Checking against clock: %d (%d)\n",
                   VidModeGetModeValue(mode, VIDMODE_CLOCK), dotClock);
            ErrorF("                 hdsp: %d hbeg: %d hend: %d httl: %d\n",
                   VidModeGetModeValue(mode, VIDMODE_H_DISPLAY),
                   VidModeGetModeValue(mode, VIDMODE_H_SYNCSTART),
                   VidModeGetModeValue(mode, VIDMODE_H_SYNCEND),
                   VidModeGetModeValue(mode, VIDMODE_H_TOTAL));
            ErrorF
                ("                 vdsp: %d vbeg: %d vend: %d vttl: %d flags: %d\n",
                 VidModeGetModeValue(mode, VIDMODE_V_DISPLAY),
                 VidModeGetModeValue(mode, VIDMODE_V_SYNCSTART),
                 VidModeGetModeValue(mode, VIDMODE_V_SYNCEND),
                 VidModeGetModeValue(mode, VIDMODE_V_TOTAL),
                 VidModeGetModeValue(mode, VIDMODE_FLAGS));
        }
        if ((VidModeGetDotClock(stuff->screen, stuff->dotclock) == dotClock) &&
            MODEMATCH(mode, stuff)) {

            if (!VidModeSwitchMode(stuff->screen, mode))
                return BadValue;

            if (xf86GetVerbosity() > DEFAULT_XF86VIDMODE_VERBOSITY)
                ErrorF("SwitchToMode - Succeeded\n");
            return Success;
        }
    } while (VidModeGetNextModeline(stuff->screen, &mode, &dotClock));

    return BadValue;
}

static int
ProcXF86VidModeLockModeSwitch(ClientPtr client)
{
    REQUEST(xXF86VidModeLockModeSwitchReq);

    REQUEST_SIZE_MATCH(xXF86VidModeLockModeSwitchReq);

    DEBUG_P("XF86VidModeLockModeSwitch");

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeLockZoom(stuff->screen, (short) stuff->lock))
        return VidModeErrorBase + XF86VidModeZoomLocked;

    return Success;
}

static int
ProcXF86VidModeGetMonitor(ClientPtr client)
{
    REQUEST(xXF86VidModeGetMonitorReq);
    xXF86VidModeGetMonitorReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence
    };
    CARD32 *hsyncdata, *vsyncdata;
    int i, nHsync, nVrefresh;
    void *monitor;

    DEBUG_P("XF86VidModeGetMonitor");

    REQUEST_SIZE_MATCH(xXF86VidModeGetMonitorReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeGetMonitor(stuff->screen, &monitor))
        return BadValue;

    nHsync = VidModeGetMonitorValue(monitor, VIDMODE_MON_NHSYNC, 0).i;
    nVrefresh = VidModeGetMonitorValue(monitor, VIDMODE_MON_NVREFRESH, 0).i;

    if ((char *) (VidModeGetMonitorValue(monitor, VIDMODE_MON_VENDOR, 0)).ptr)
        rep.vendorLength = strlen((char *) (VidModeGetMonitorValue(monitor,
                                                                   VIDMODE_MON_VENDOR,
                                                                   0)).ptr);
    else
        rep.vendorLength = 0;
    if ((char *) (VidModeGetMonitorValue(monitor, VIDMODE_MON_MODEL, 0)).ptr)
        rep.modelLength = strlen((char *) (VidModeGetMonitorValue(monitor,
                                                                  VIDMODE_MON_MODEL,
                                                                  0)).ptr);
    else
        rep.modelLength = 0;
    rep.length =
        bytes_to_int32(SIZEOF(xXF86VidModeGetMonitorReply) -
                       SIZEOF(xGenericReply) + (nHsync +
                                                nVrefresh) * sizeof(CARD32) +
                       pad_to_int32(rep.vendorLength) +
                       pad_to_int32(rep.modelLength));
    rep.nhsync = nHsync;
    rep.nvsync = nVrefresh;
    hsyncdata = malloc(nHsync * sizeof(CARD32));
    if (!hsyncdata) {
        return BadAlloc;
    }
    vsyncdata = malloc(nVrefresh * sizeof(CARD32));

    if (!vsyncdata) {
        free(hsyncdata);
        return BadAlloc;
    }

    for (i = 0; i < nHsync; i++) {
        hsyncdata[i] = (unsigned short) (VidModeGetMonitorValue(monitor,
                                                                VIDMODE_MON_HSYNC_LO,
                                                                i)).f |
            (unsigned
             short) (VidModeGetMonitorValue(monitor, VIDMODE_MON_HSYNC_HI,
                                            i)).f << 16;
    }
    for (i = 0; i < nVrefresh; i++) {
        vsyncdata[i] = (unsigned short) (VidModeGetMonitorValue(monitor,
                                                                VIDMODE_MON_VREFRESH_LO,
                                                                i)).f |
            (unsigned
             short) (VidModeGetMonitorValue(monitor, VIDMODE_MON_VREFRESH_HI,
                                            i)).f << 16;
    }

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
    }
    WriteToClient(client, SIZEOF(xXF86VidModeGetMonitorReply), &rep);
    client->pSwapReplyFunc = (ReplySwapPtr) Swap32Write;
    WriteSwappedDataToClient(client, nHsync * sizeof(CARD32), hsyncdata);
    WriteSwappedDataToClient(client, nVrefresh * sizeof(CARD32), vsyncdata);
    if (rep.vendorLength)
        WriteToClient(client, rep.vendorLength,
                 (VidModeGetMonitorValue(monitor, VIDMODE_MON_VENDOR, 0)).ptr);
    if (rep.modelLength)
        WriteToClient(client, rep.modelLength,
                 (VidModeGetMonitorValue(monitor, VIDMODE_MON_MODEL, 0)).ptr);

    free(hsyncdata);
    free(vsyncdata);

    return Success;
}

static int
ProcXF86VidModeGetViewPort(ClientPtr client)
{
    REQUEST(xXF86VidModeGetViewPortReq);
    xXF86VidModeGetViewPortReply rep;
    int x, y;

    DEBUG_P("XF86VidModeGetViewPort");

    REQUEST_SIZE_MATCH(xXF86VidModeGetViewPortReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    VidModeGetViewPort(stuff->screen, &x, &y);

    rep = (xXF86VidModeGetViewPortReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .x = x,
        .y = y
    };

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.x);
        swapl(&rep.y);
    }
    WriteToClient(client, SIZEOF(xXF86VidModeGetViewPortReply), &rep);
    return Success;
}

static int
ProcXF86VidModeSetViewPort(ClientPtr client)
{
    REQUEST(xXF86VidModeSetViewPortReq);

    DEBUG_P("XF86VidModeSetViewPort");

    REQUEST_SIZE_MATCH(xXF86VidModeSetViewPortReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeSetViewPort(stuff->screen, stuff->x, stuff->y))
        return BadValue;

    return Success;
}

static int
ProcXF86VidModeGetDotClocks(ClientPtr client)
{
    REQUEST(xXF86VidModeGetDotClocksReq);
    xXF86VidModeGetDotClocksReply rep;
    int n;
    int numClocks;
    CARD32 dotclock;
    int *Clocks = NULL;
    Bool ClockProg;

    DEBUG_P("XF86VidModeGetDotClocks");

    REQUEST_SIZE_MATCH(xXF86VidModeGetDotClocksReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    numClocks = VidModeGetNumOfClocks(stuff->screen, &ClockProg);

    rep = (xXF86VidModeGetDotClocksReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = bytes_to_int32(SIZEOF(xXF86VidModeGetDotClocksReply)
                                 - SIZEOF(xGenericReply) + numClocks),
        .clocks = numClocks,
        .maxclocks = MAXCLOCKS,
        .flags = 0
    };

    if (!ClockProg) {
        Clocks = calloc(numClocks, sizeof(int));
        if (!Clocks)
            return BadValue;
        if (!VidModeGetClocks(stuff->screen, Clocks)) {
            free(Clocks);
            return BadValue;
        }
    }
    if (ClockProg) {
        rep.flags |= CLKFLAG_PROGRAMABLE;
    }
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.clocks);
        swapl(&rep.maxclocks);
        swapl(&rep.flags);
    }
    WriteToClient(client, sizeof(xXF86VidModeGetDotClocksReply), &rep);
    if (!ClockProg) {
        for (n = 0; n < numClocks; n++) {
            dotclock = *Clocks++;
            if (client->swapped) {
                WriteSwappedDataToClient(client, 4, (char *) &dotclock);
            }
            else {
                WriteToClient(client, 4, &dotclock);
            }
        }
    }

    free(Clocks);
    return Success;
}

static int
ProcXF86VidModeSetGamma(ClientPtr client)
{
    REQUEST(xXF86VidModeSetGammaReq);

    DEBUG_P("XF86VidModeSetGamma");

    REQUEST_SIZE_MATCH(xXF86VidModeSetGammaReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeSetGamma(stuff->screen, ((float) stuff->red) / 10000.,
                         ((float) stuff->green) / 10000.,
                         ((float) stuff->blue) / 10000.))
        return BadValue;

    return Success;
}

static int
ProcXF86VidModeGetGamma(ClientPtr client)
{
    REQUEST(xXF86VidModeGetGammaReq);
    xXF86VidModeGetGammaReply rep;
    float red, green, blue;

    DEBUG_P("XF86VidModeGetGamma");

    REQUEST_SIZE_MATCH(xXF86VidModeGetGammaReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (!VidModeGetGamma(stuff->screen, &red, &green, &blue))
        return BadValue;
    rep = (xXF86VidModeGetGammaReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .red = (CARD32) (red * 10000.),
        .green = (CARD32) (green * 10000.),
        .blue = (CARD32) (blue * 10000.)
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.red);
        swapl(&rep.green);
        swapl(&rep.blue);
    }
    WriteToClient(client, sizeof(xXF86VidModeGetGammaReply), &rep);

    return Success;
}

static int
ProcXF86VidModeSetGammaRamp(ClientPtr client)
{
    CARD16 *r, *g, *b;
    int length;

    REQUEST(xXF86VidModeSetGammaRampReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (stuff->size != VidModeGetGammaRampSize(stuff->screen))
        return BadValue;

    length = (stuff->size + 1) & ~1;

    REQUEST_FIXED_SIZE(xXF86VidModeSetGammaRampReq, length * 6);

    r = (CARD16 *) &stuff[1];
    g = r + length;
    b = g + length;

    if (!VidModeSetGammaRamp(stuff->screen, stuff->size, r, g, b))
        return BadValue;

    return Success;
}

static int
ProcXF86VidModeGetGammaRamp(ClientPtr client)
{
    CARD16 *ramp = NULL;
    int length;
    size_t ramplen = 0;
    xXF86VidModeGetGammaRampReply rep;

    REQUEST(xXF86VidModeGetGammaRampReq);

    REQUEST_SIZE_MATCH(xXF86VidModeGetGammaRampReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (stuff->size != VidModeGetGammaRampSize(stuff->screen))
        return BadValue;

    length = (stuff->size + 1) & ~1;

    if (stuff->size) {
        ramplen = length * 3 * sizeof(CARD16);
        if (!(ramp = malloc(ramplen)))
            return BadAlloc;

        if (!VidModeGetGammaRamp(stuff->screen, stuff->size,
                                 ramp, ramp + length, ramp + (length * 2))) {
            free(ramp);
            return BadValue;
        }
    }
    rep = (xXF86VidModeGetGammaRampReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = (length >> 1) * 3,
        .size = stuff->size
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swaps(&rep.size);
        SwapShorts((short *) ramp, length * 3);
    }
    WriteToClient(client, sizeof(xXF86VidModeGetGammaRampReply), &rep);

    if (stuff->size) {
        WriteToClient(client, ramplen, ramp);
        free(ramp);
    }

    return Success;
}

static int
ProcXF86VidModeGetGammaRampSize(ClientPtr client)
{
    xXF86VidModeGetGammaRampSizeReply rep;

    REQUEST(xXF86VidModeGetGammaRampSizeReq);

    REQUEST_SIZE_MATCH(xXF86VidModeGetGammaRampSizeReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    rep = (xXF86VidModeGetGammaRampSizeReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .size = VidModeGetGammaRampSize(stuff->screen)
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swaps(&rep.size);
    }
    WriteToClient(client, sizeof(xXF86VidModeGetGammaRampSizeReply), &rep);

    return Success;
}

static int
ProcXF86VidModeGetPermissions(ClientPtr client)
{
    xXF86VidModeGetPermissionsReply rep =  {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .permissions = XF86VM_READ_PERMISSION
    };

    REQUEST(xXF86VidModeGetPermissionsReq);

    REQUEST_SIZE_MATCH(xXF86VidModeGetPermissionsReq);

    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;

    if (xf86GetVidModeEnabled() &&
        (xf86GetVidModeAllowNonLocal() || client->local)) {
        rep.permissions |= XF86VM_WRITE_PERMISSION;
    }
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.permissions);
    }
    WriteToClient(client, sizeof(xXF86VidModeGetPermissionsReply), &rep);

    return Success;
}

static int
ProcXF86VidModeSetClientVersion(ClientPtr client)
{
    REQUEST(xXF86VidModeSetClientVersionReq);

    VidModePrivPtr pPriv;

    DEBUG_P("XF86VidModeSetClientVersion");

    REQUEST_SIZE_MATCH(xXF86VidModeSetClientVersionReq);

    if ((pPriv = VM_GETPRIV(client)) == NULL) {
        pPriv = malloc(sizeof(VidModePrivRec));
        if (!pPriv)
            return BadAlloc;
        VM_SETPRIV(client, pPriv);
    }
    pPriv->major = stuff->major;

    pPriv->minor = stuff->minor;

    return Success;
}

static int
ProcXF86VidModeDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_XF86VidModeQueryVersion:
        return ProcXF86VidModeQueryVersion(client);
    case X_XF86VidModeGetModeLine:
        return ProcXF86VidModeGetModeLine(client);
    case X_XF86VidModeGetMonitor:
        return ProcXF86VidModeGetMonitor(client);
    case X_XF86VidModeGetAllModeLines:
        return ProcXF86VidModeGetAllModeLines(client);
    case X_XF86VidModeValidateModeLine:
        return ProcXF86VidModeValidateModeLine(client);
    case X_XF86VidModeGetViewPort:
        return ProcXF86VidModeGetViewPort(client);
    case X_XF86VidModeGetDotClocks:
        return ProcXF86VidModeGetDotClocks(client);
    case X_XF86VidModeSetClientVersion:
        return ProcXF86VidModeSetClientVersion(client);
    case X_XF86VidModeGetGamma:
        return ProcXF86VidModeGetGamma(client);
    case X_XF86VidModeGetGammaRamp:
        return ProcXF86VidModeGetGammaRamp(client);
    case X_XF86VidModeGetGammaRampSize:
        return ProcXF86VidModeGetGammaRampSize(client);
    case X_XF86VidModeGetPermissions:
        return ProcXF86VidModeGetPermissions(client);
    default:
        if (!xf86GetVidModeEnabled())
            return VidModeErrorBase + XF86VidModeExtensionDisabled;
        if (xf86GetVidModeAllowNonLocal() || client->local) {
            switch (stuff->data) {
            case X_XF86VidModeAddModeLine:
                return ProcXF86VidModeAddModeLine(client);
            case X_XF86VidModeDeleteModeLine:
                return ProcXF86VidModeDeleteModeLine(client);
            case X_XF86VidModeModModeLine:
                return ProcXF86VidModeModModeLine(client);
            case X_XF86VidModeSwitchMode:
                return ProcXF86VidModeSwitchMode(client);
            case X_XF86VidModeSwitchToMode:
                return ProcXF86VidModeSwitchToMode(client);
            case X_XF86VidModeLockModeSwitch:
                return ProcXF86VidModeLockModeSwitch(client);
            case X_XF86VidModeSetViewPort:
                return ProcXF86VidModeSetViewPort(client);
            case X_XF86VidModeSetGamma:
                return ProcXF86VidModeSetGamma(client);
            case X_XF86VidModeSetGammaRamp:
                return ProcXF86VidModeSetGammaRamp(client);
            default:
                return BadRequest;
            }
        }
        else
            return VidModeErrorBase + XF86VidModeClientNotLocal;
    }
}

static int
SProcXF86VidModeQueryVersion(ClientPtr client)
{
    REQUEST(xXF86VidModeQueryVersionReq);
    swaps(&stuff->length);
    return ProcXF86VidModeQueryVersion(client);
}

static int
SProcXF86VidModeGetModeLine(ClientPtr client)
{
    REQUEST(xXF86VidModeGetModeLineReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetModeLineReq);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetModeLine(client);
}

static int
SProcXF86VidModeGetAllModeLines(ClientPtr client)
{
    REQUEST(xXF86VidModeGetAllModeLinesReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetAllModeLinesReq);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetAllModeLines(client);
}

static int
SProcXF86VidModeAddModeLine(ClientPtr client)
{
    xXF86OldVidModeAddModeLineReq *oldstuff =
        (xXF86OldVidModeAddModeLineReq *) client->requestBuffer;
    int ver;

    REQUEST(xXF86VidModeAddModeLineReq);
    ver = ClientMajorVersion(client);
    if (ver < 2) {
        swaps(&oldstuff->length);
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeAddModeLineReq);
        swapl(&oldstuff->screen);
        swaps(&oldstuff->hdisplay);
        swaps(&oldstuff->hsyncstart);
        swaps(&oldstuff->hsyncend);
        swaps(&oldstuff->htotal);
        swaps(&oldstuff->vdisplay);
        swaps(&oldstuff->vsyncstart);
        swaps(&oldstuff->vsyncend);
        swaps(&oldstuff->vtotal);
        swapl(&oldstuff->flags);
        swapl(&oldstuff->privsize);
        SwapRestL(oldstuff);
    }
    else {
        swaps(&stuff->length);
        REQUEST_AT_LEAST_SIZE(xXF86VidModeAddModeLineReq);
        swapl(&stuff->screen);
        swaps(&stuff->hdisplay);
        swaps(&stuff->hsyncstart);
        swaps(&stuff->hsyncend);
        swaps(&stuff->htotal);
        swaps(&stuff->hskew);
        swaps(&stuff->vdisplay);
        swaps(&stuff->vsyncstart);
        swaps(&stuff->vsyncend);
        swaps(&stuff->vtotal);
        swapl(&stuff->flags);
        swapl(&stuff->privsize);
        SwapRestL(stuff);
    }
    return ProcXF86VidModeAddModeLine(client);
}

static int
SProcXF86VidModeDeleteModeLine(ClientPtr client)
{
    xXF86OldVidModeDeleteModeLineReq *oldstuff =
        (xXF86OldVidModeDeleteModeLineReq *) client->requestBuffer;
    int ver;

    REQUEST(xXF86VidModeDeleteModeLineReq);
    ver = ClientMajorVersion(client);
    if (ver < 2) {
        swaps(&oldstuff->length);
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeDeleteModeLineReq);
        swapl(&oldstuff->screen);
        swaps(&oldstuff->hdisplay);
        swaps(&oldstuff->hsyncstart);
        swaps(&oldstuff->hsyncend);
        swaps(&oldstuff->htotal);
        swaps(&oldstuff->vdisplay);
        swaps(&oldstuff->vsyncstart);
        swaps(&oldstuff->vsyncend);
        swaps(&oldstuff->vtotal);
        swapl(&oldstuff->flags);
        swapl(&oldstuff->privsize);
        SwapRestL(oldstuff);
    }
    else {
        swaps(&stuff->length);
        REQUEST_AT_LEAST_SIZE(xXF86VidModeDeleteModeLineReq);
        swapl(&stuff->screen);
        swaps(&stuff->hdisplay);
        swaps(&stuff->hsyncstart);
        swaps(&stuff->hsyncend);
        swaps(&stuff->htotal);
        swaps(&stuff->hskew);
        swaps(&stuff->vdisplay);
        swaps(&stuff->vsyncstart);
        swaps(&stuff->vsyncend);
        swaps(&stuff->vtotal);
        swapl(&stuff->flags);
        swapl(&stuff->privsize);
        SwapRestL(stuff);
    }
    return ProcXF86VidModeDeleteModeLine(client);
}

static int
SProcXF86VidModeModModeLine(ClientPtr client)
{
    xXF86OldVidModeModModeLineReq *oldstuff =
        (xXF86OldVidModeModModeLineReq *) client->requestBuffer;
    int ver;

    REQUEST(xXF86VidModeModModeLineReq);
    ver = ClientMajorVersion(client);
    if (ver < 2) {
        swaps(&oldstuff->length);
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeModModeLineReq);
        swapl(&oldstuff->screen);
        swaps(&oldstuff->hdisplay);
        swaps(&oldstuff->hsyncstart);
        swaps(&oldstuff->hsyncend);
        swaps(&oldstuff->htotal);
        swaps(&oldstuff->vdisplay);
        swaps(&oldstuff->vsyncstart);
        swaps(&oldstuff->vsyncend);
        swaps(&oldstuff->vtotal);
        swapl(&oldstuff->flags);
        swapl(&oldstuff->privsize);
        SwapRestL(oldstuff);
    }
    else {
        swaps(&stuff->length);
        REQUEST_AT_LEAST_SIZE(xXF86VidModeModModeLineReq);
        swapl(&stuff->screen);
        swaps(&stuff->hdisplay);
        swaps(&stuff->hsyncstart);
        swaps(&stuff->hsyncend);
        swaps(&stuff->htotal);
        swaps(&stuff->hskew);
        swaps(&stuff->vdisplay);
        swaps(&stuff->vsyncstart);
        swaps(&stuff->vsyncend);
        swaps(&stuff->vtotal);
        swapl(&stuff->flags);
        swapl(&stuff->privsize);
        SwapRestL(stuff);
    }
    return ProcXF86VidModeModModeLine(client);
}

static int
SProcXF86VidModeValidateModeLine(ClientPtr client)
{
    xXF86OldVidModeValidateModeLineReq *oldstuff =
        (xXF86OldVidModeValidateModeLineReq *) client->requestBuffer;
    int ver;

    REQUEST(xXF86VidModeValidateModeLineReq);
    ver = ClientMajorVersion(client);
    if (ver < 2) {
        swaps(&oldstuff->length);
        REQUEST_AT_LEAST_SIZE(xXF86OldVidModeValidateModeLineReq);
        swapl(&oldstuff->screen);
        swaps(&oldstuff->hdisplay);
        swaps(&oldstuff->hsyncstart);
        swaps(&oldstuff->hsyncend);
        swaps(&oldstuff->htotal);
        swaps(&oldstuff->vdisplay);
        swaps(&oldstuff->vsyncstart);
        swaps(&oldstuff->vsyncend);
        swaps(&oldstuff->vtotal);
        swapl(&oldstuff->flags);
        swapl(&oldstuff->privsize);
        SwapRestL(oldstuff);
    }
    else {
        swaps(&stuff->length);
        REQUEST_AT_LEAST_SIZE(xXF86VidModeValidateModeLineReq);
        swapl(&stuff->screen);
        swaps(&stuff->hdisplay);
        swaps(&stuff->hsyncstart);
        swaps(&stuff->hsyncend);
        swaps(&stuff->htotal);
        swaps(&stuff->hskew);
        swaps(&stuff->vdisplay);
        swaps(&stuff->vsyncstart);
        swaps(&stuff->vsyncend);
        swaps(&stuff->vtotal);
        swapl(&stuff->flags);
        swapl(&stuff->privsize);
        SwapRestL(stuff);
    }
    return ProcXF86VidModeValidateModeLine(client);
}

static int
SProcXF86VidModeSwitchMode(ClientPtr client)
{
    REQUEST(xXF86VidModeSwitchModeReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeSwitchModeReq);
    swaps(&stuff->screen);
    swaps(&stuff->zoom);
    return ProcXF86VidModeSwitchMode(client);
}

static int
SProcXF86VidModeSwitchToMode(ClientPtr client)
{
    REQUEST(xXF86VidModeSwitchToModeReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeSwitchToModeReq);
    swapl(&stuff->screen);
    return ProcXF86VidModeSwitchToMode(client);
}

static int
SProcXF86VidModeLockModeSwitch(ClientPtr client)
{
    REQUEST(xXF86VidModeLockModeSwitchReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeLockModeSwitchReq);
    swaps(&stuff->screen);
    swaps(&stuff->lock);
    return ProcXF86VidModeLockModeSwitch(client);
}

static int
SProcXF86VidModeGetMonitor(ClientPtr client)
{
    REQUEST(xXF86VidModeGetMonitorReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetMonitorReq);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetMonitor(client);
}

static int
SProcXF86VidModeGetViewPort(ClientPtr client)
{
    REQUEST(xXF86VidModeGetViewPortReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetViewPortReq);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetViewPort(client);
}

static int
SProcXF86VidModeSetViewPort(ClientPtr client)
{
    REQUEST(xXF86VidModeSetViewPortReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeSetViewPortReq);
    swaps(&stuff->screen);
    swapl(&stuff->x);
    swapl(&stuff->y);
    return ProcXF86VidModeSetViewPort(client);
}

static int
SProcXF86VidModeGetDotClocks(ClientPtr client)
{
    REQUEST(xXF86VidModeGetDotClocksReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetDotClocksReq);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetDotClocks(client);
}

static int
SProcXF86VidModeSetClientVersion(ClientPtr client)
{
    REQUEST(xXF86VidModeSetClientVersionReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeSetClientVersionReq);
    swaps(&stuff->major);
    swaps(&stuff->minor);
    return ProcXF86VidModeSetClientVersion(client);
}

static int
SProcXF86VidModeSetGamma(ClientPtr client)
{
    REQUEST(xXF86VidModeSetGammaReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeSetGammaReq);
    swaps(&stuff->screen);
    swapl(&stuff->red);
    swapl(&stuff->green);
    swapl(&stuff->blue);
    return ProcXF86VidModeSetGamma(client);
}

static int
SProcXF86VidModeGetGamma(ClientPtr client)
{
    REQUEST(xXF86VidModeGetGammaReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetGammaReq);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetGamma(client);
}

static int
SProcXF86VidModeSetGammaRamp(ClientPtr client)
{
    int length;

    REQUEST(xXF86VidModeSetGammaRampReq);
    swaps(&stuff->length);
    REQUEST_AT_LEAST_SIZE(xXF86VidModeSetGammaRampReq);
    swaps(&stuff->size);
    swaps(&stuff->screen);
    length = ((stuff->size + 1) & ~1) * 6;
    REQUEST_FIXED_SIZE(xXF86VidModeSetGammaRampReq, length);
    SwapRestS(stuff);
    return ProcXF86VidModeSetGammaRamp(client);
}

static int
SProcXF86VidModeGetGammaRamp(ClientPtr client)
{
    REQUEST(xXF86VidModeGetGammaRampReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetGammaRampReq);
    swaps(&stuff->size);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetGammaRamp(client);
}

static int
SProcXF86VidModeGetGammaRampSize(ClientPtr client)
{
    REQUEST(xXF86VidModeGetGammaRampSizeReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetGammaRampSizeReq);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetGammaRampSize(client);
}

static int
SProcXF86VidModeGetPermissions(ClientPtr client)
{
    REQUEST(xXF86VidModeGetPermissionsReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXF86VidModeGetPermissionsReq);
    swaps(&stuff->screen);
    return ProcXF86VidModeGetPermissions(client);
}

static int
SProcXF86VidModeDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_XF86VidModeQueryVersion:
        return SProcXF86VidModeQueryVersion(client);
    case X_XF86VidModeGetModeLine:
        return SProcXF86VidModeGetModeLine(client);
    case X_XF86VidModeGetMonitor:
        return SProcXF86VidModeGetMonitor(client);
    case X_XF86VidModeGetAllModeLines:
        return SProcXF86VidModeGetAllModeLines(client);
    case X_XF86VidModeGetViewPort:
        return SProcXF86VidModeGetViewPort(client);
    case X_XF86VidModeValidateModeLine:
        return SProcXF86VidModeValidateModeLine(client);
    case X_XF86VidModeGetDotClocks:
        return SProcXF86VidModeGetDotClocks(client);
    case X_XF86VidModeSetClientVersion:
        return SProcXF86VidModeSetClientVersion(client);
    case X_XF86VidModeGetGamma:
        return SProcXF86VidModeGetGamma(client);
    case X_XF86VidModeGetGammaRamp:
        return SProcXF86VidModeGetGammaRamp(client);
    case X_XF86VidModeGetGammaRampSize:
        return SProcXF86VidModeGetGammaRampSize(client);
    case X_XF86VidModeGetPermissions:
        return SProcXF86VidModeGetPermissions(client);
    default:
        if (!xf86GetVidModeEnabled())
            return VidModeErrorBase + XF86VidModeExtensionDisabled;
        if (xf86GetVidModeAllowNonLocal() || client->local) {
            switch (stuff->data) {
            case X_XF86VidModeAddModeLine:
                return SProcXF86VidModeAddModeLine(client);
            case X_XF86VidModeDeleteModeLine:
                return SProcXF86VidModeDeleteModeLine(client);
            case X_XF86VidModeModModeLine:
                return SProcXF86VidModeModModeLine(client);
            case X_XF86VidModeSwitchMode:
                return SProcXF86VidModeSwitchMode(client);
            case X_XF86VidModeSwitchToMode:
                return SProcXF86VidModeSwitchToMode(client);
            case X_XF86VidModeLockModeSwitch:
                return SProcXF86VidModeLockModeSwitch(client);
            case X_XF86VidModeSetViewPort:
                return SProcXF86VidModeSetViewPort(client);
            case X_XF86VidModeSetGamma:
                return SProcXF86VidModeSetGamma(client);
            case X_XF86VidModeSetGammaRamp:
                return SProcXF86VidModeSetGammaRamp(client);
            default:
                return BadRequest;
            }
        }
        else
            return VidModeErrorBase + XF86VidModeClientNotLocal;
    }
}

void
XFree86VidModeExtensionInit(void)
{
    ExtensionEntry *extEntry;
    ScreenPtr pScreen;
    int i;
    Bool enabled = FALSE;

    DEBUG_P("XFree86VidModeExtensionInit");

    if (!dixRegisterPrivateKey(&VidModeClientPrivateKeyRec, PRIVATE_CLIENT, 0))
        return;
#ifdef XF86VIDMODE_EVENTS
    if (!dixRegisterPrivateKey(&ScreenPrivateKeyRec, PRIVATE_SCREEN, 0))
        return;
#endif

#ifdef XF86VIDMODE_EVENTS
    EventType = CreateNewResourceType(XF86VidModeFreeEvents, "VidModeEvent");
#endif

    for (i = 0; i < screenInfo.numScreens; i++) {
        pScreen = screenInfo.screens[i];
        if (VidModeExtensionInit(pScreen))
            enabled = TRUE;
    }
    /* This means that the DDX doesn't want the vidmode extension enabled */
    if (!enabled)
        return;

    if (
#ifdef XF86VIDMODE_EVENTS
           EventType &&
#endif
           (extEntry = AddExtension(XF86VIDMODENAME,
                                    XF86VidModeNumberEvents,
                                    XF86VidModeNumberErrors,
                                    ProcXF86VidModeDispatch,
                                    SProcXF86VidModeDispatch,
                                    NULL, StandardMinorOpcode))) {
#if 0
        XF86VidModeReqCode = (unsigned char) extEntry->base;
#endif
        VidModeErrorBase = extEntry->errorBase;
#ifdef XF86VIDMODE_EVENTS
        XF86VidModeEventBase = extEntry->eventBase;
        EventSwapVector[XF86VidModeEventBase] =
            (EventSwapPtr) SXF86VidModeNotifyEvent;
#endif
    }
}
