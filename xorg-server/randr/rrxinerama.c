/*
 * Copyright © 2006 Keith Packard
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
/*
 * This Xinerama implementation comes from the SiS driver which has
 * the following notice:
 */
/*
 * SiS driver main code
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1) Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3) The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Thomas Winischhofer <thomas@winischhofer.net>
 *	- driver entirely rewritten since 2001, only basic structure taken from
 *	  old code (except sis_dri.c, sis_shadow.c, sis_accel.c and parts of
 *	  sis_dga.c; these were mostly taken over; sis_dri.c was changed for
 *	  new versions of the DRI layer)
 *
 * This notice covers the entire driver code unless indicated otherwise.
 *
 * Formerly based on code which was
 * 	     Copyright (C) 1998, 1999 by Alan Hourihane, Wigan, England.
 * 	     Written by:
 *           Alan Hourihane <alanh@fairlite.demon.co.uk>,
 *           Mike Chapman <mike@paranoia.com>,
 *           Juanjo Santamarta <santamarta@ctv.es>,
 *           Mitani Hiroshi <hmitani@drl.mei.co.jp>,
 *           David Thomas <davtom@dream.org.uk>.
 */

#include "randrstr.h"
#include "swaprep.h"
#include <X11/extensions/panoramiXproto.h>
#include "protocol-versions.h"

/* Xinerama is not multi-screen capable; just report about screen 0 */
#define RR_XINERAMA_SCREEN  0

static int ProcRRXineramaQueryVersion(ClientPtr client);
static int ProcRRXineramaGetState(ClientPtr client);
static int ProcRRXineramaGetScreenCount(ClientPtr client);
static int ProcRRXineramaGetScreenSize(ClientPtr client);
static int ProcRRXineramaIsActive(ClientPtr client);
static int ProcRRXineramaQueryScreens(ClientPtr client);
static int SProcRRXineramaDispatch(ClientPtr client);

/* Proc */

int
ProcRRXineramaQueryVersion(ClientPtr client)
{
    xPanoramiXQueryVersionReply rep = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = SERVER_RRXINERAMA_MAJOR_VERSION,
        .minorVersion = SERVER_RRXINERAMA_MINOR_VERSION
    };

    REQUEST_SIZE_MATCH(xPanoramiXQueryVersionReq);
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swaps(&rep.majorVersion);
        swaps(&rep.minorVersion);
    }
    WriteToClient(client, sizeof(xPanoramiXQueryVersionReply), &rep);
    return Success;
}

int
ProcRRXineramaGetState(ClientPtr client)
{
    REQUEST(xPanoramiXGetStateReq);
    WindowPtr pWin;
    xPanoramiXGetStateReply rep;
    register int rc;
    ScreenPtr pScreen;
    rrScrPrivPtr pScrPriv;
    Bool active = FALSE;

    REQUEST_SIZE_MATCH(xPanoramiXGetStateReq);
    rc = dixLookupWindow(&pWin, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
        return rc;

    pScreen = pWin->drawable.pScreen;
    pScrPriv = rrGetScrPriv(pScreen);
    if (pScrPriv) {
        /* XXX do we need more than this? */
        active = TRUE;
    }

    rep = (xPanoramiXGetStateReply) {
        .type = X_Reply,
        .state = active,
        .sequenceNumber = client->sequence,
        .length = 0,
        .window = stuff->window
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.window);
    }
    WriteToClient(client, sizeof(xPanoramiXGetStateReply), &rep);
    return Success;
}

static Bool
RRXineramaCrtcActive(RRCrtcPtr crtc)
{
    return crtc->mode != NULL && crtc->numOutputs > 0;
}

static int
RRXineramaScreenCount(ScreenPtr pScreen)
{
    int i, n;
    ScreenPtr slave;

    n = 0;
    if (rrGetScrPriv(pScreen)) {
        rrScrPriv(pScreen);
        for (i = 0; i < pScrPriv->numCrtcs; i++)
            if (RRXineramaCrtcActive(pScrPriv->crtcs[i]))
                n++;
    }

    xorg_list_for_each_entry(slave, &pScreen->output_slave_list, output_head) {
        rrScrPrivPtr pSlavePriv;
        pSlavePriv = rrGetScrPriv(slave);
        for (i = 0; i < pSlavePriv->numCrtcs; i++)
            if (RRXineramaCrtcActive(pSlavePriv->crtcs[i]))
                n++;
    }

    return n;
}

static Bool
RRXineramaScreenActive(ScreenPtr pScreen)
{
    return RRXineramaScreenCount(pScreen) > 0;
}

int
ProcRRXineramaGetScreenCount(ClientPtr client)
{
    REQUEST(xPanoramiXGetScreenCountReq);
    WindowPtr pWin;
    xPanoramiXGetScreenCountReply rep;
    register int rc;

    REQUEST_SIZE_MATCH(xPanoramiXGetScreenCountReq);
    rc = dixLookupWindow(&pWin, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
        return rc;

    rep = (xPanoramiXGetScreenCountReply)  {
        .type = X_Reply,
        .ScreenCount = RRXineramaScreenCount(pWin->drawable.pScreen),
        .sequenceNumber = client->sequence,
        .length = 0,
        .window = stuff->window
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.window);
    }
    WriteToClient(client, sizeof(xPanoramiXGetScreenCountReply), &rep);
    return Success;
}

int
ProcRRXineramaGetScreenSize(ClientPtr client)
{
    REQUEST(xPanoramiXGetScreenSizeReq);
    WindowPtr pWin, pRoot;
    ScreenPtr pScreen;
    xPanoramiXGetScreenSizeReply rep;
    register int rc;

    REQUEST_SIZE_MATCH(xPanoramiXGetScreenSizeReq);
    rc = dixLookupWindow(&pWin, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
        return rc;

    pScreen = pWin->drawable.pScreen;
    pRoot = pScreen->root;

    rep = (xPanoramiXGetScreenSizeReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .width = pRoot->drawable.width,
        .height = pRoot->drawable.height,
        .window = stuff->window,
        .screen = stuff->screen
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.width);
        swapl(&rep.height);
        swapl(&rep.window);
        swapl(&rep.screen);
    }
    WriteToClient(client, sizeof(xPanoramiXGetScreenSizeReply), &rep);
    return Success;
}

int
ProcRRXineramaIsActive(ClientPtr client)
{
    xXineramaIsActiveReply rep;

    REQUEST_SIZE_MATCH(xXineramaIsActiveReq);

    rep = (xXineramaIsActiveReply) {
        .type = X_Reply,
        .length = 0,
        .sequenceNumber = client->sequence,
        .state = RRXineramaScreenActive(screenInfo.screens[RR_XINERAMA_SCREEN])
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.state);
    }
    WriteToClient(client, sizeof(xXineramaIsActiveReply), &rep);
    return Success;
}

static void
RRXineramaWriteCrtc(ClientPtr client, RRCrtcPtr crtc)
{
    xXineramaScreenInfo scratch;

    if (RRXineramaCrtcActive(crtc)) {
        ScreenPtr pScreen = crtc->pScreen;
        rrScrPrivPtr pScrPriv = rrGetScrPriv(pScreen);
        BoxRec panned_area;

        /* Check to see if crtc is panned and return the full area when applicable. */
        if (pScrPriv && pScrPriv->rrGetPanning &&
            pScrPriv->rrGetPanning(pScreen, crtc, &panned_area, NULL, NULL) &&
            (panned_area.x2 > panned_area.x1) &&
            (panned_area.y2 > panned_area.y1)) {
            scratch.x_org = panned_area.x1;
            scratch.y_org = panned_area.y1;
            scratch.width = panned_area.x2 - panned_area.x1;
            scratch.height = panned_area.y2 - panned_area.y1;
        }
        else {
            int width, height;

            RRCrtcGetScanoutSize(crtc, &width, &height);
            scratch.x_org = crtc->x;
            scratch.y_org = crtc->y;
            scratch.width = width;
            scratch.height = height;
        }
        if (client->swapped) {
            swaps(&scratch.x_org);
            swaps(&scratch.y_org);
            swaps(&scratch.width);
            swaps(&scratch.height);
        }
        WriteToClient(client, sz_XineramaScreenInfo, &scratch);
    }
}

int
ProcRRXineramaQueryScreens(ClientPtr client)
{
    xXineramaQueryScreensReply rep;
    ScreenPtr pScreen = screenInfo.screens[RR_XINERAMA_SCREEN];
    int n = 0;
    int i;

    REQUEST_SIZE_MATCH(xXineramaQueryScreensReq);

    if (RRXineramaScreenActive(pScreen)) {
        RRGetInfo(pScreen, FALSE);
        n = RRXineramaScreenCount(pScreen);
    }

    rep = (xXineramaQueryScreensReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = bytes_to_int32(n * sz_XineramaScreenInfo),
        .number = n
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.number);
    }
    WriteToClient(client, sizeof(xXineramaQueryScreensReply), &rep);

    if (n) {
        ScreenPtr slave;
        rrScrPriv(pScreen);
        int has_primary = 0;
        RRCrtcPtr primary_crtc = NULL;

        if (pScrPriv->primaryOutput && pScrPriv->primaryOutput->crtc) {
            has_primary = 1;
            primary_crtc = pScrPriv->primaryOutput->crtc;
            RRXineramaWriteCrtc(client, pScrPriv->primaryOutput->crtc);
        }

        for (i = 0; i < pScrPriv->numCrtcs; i++) {
            if (has_primary &&
                primary_crtc == pScrPriv->crtcs[i]) {
                has_primary = 0;
                continue;
            }
            RRXineramaWriteCrtc(client, pScrPriv->crtcs[i]);
        }

        xorg_list_for_each_entry(slave, &pScreen->output_slave_list, output_head) {
            rrScrPrivPtr pSlavePriv;
            pSlavePriv = rrGetScrPriv(slave);
            for (i = 0; i < pSlavePriv->numCrtcs; i++) {
                if (has_primary &&
                    primary_crtc == pSlavePriv->crtcs[i]) {
                    has_primary = 0;
                    continue;
                }
                RRXineramaWriteCrtc(client, pSlavePriv->crtcs[i]);
            }
        }
    }

    return Success;
}

static int
ProcRRXineramaDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_PanoramiXQueryVersion:
        return ProcRRXineramaQueryVersion(client);
    case X_PanoramiXGetState:
        return ProcRRXineramaGetState(client);
    case X_PanoramiXGetScreenCount:
        return ProcRRXineramaGetScreenCount(client);
    case X_PanoramiXGetScreenSize:
        return ProcRRXineramaGetScreenSize(client);
    case X_XineramaIsActive:
        return ProcRRXineramaIsActive(client);
    case X_XineramaQueryScreens:
        return ProcRRXineramaQueryScreens(client);
    }
    return BadRequest;
}

/* SProc */

static int
SProcRRXineramaQueryVersion(ClientPtr client)
{
    REQUEST(xPanoramiXQueryVersionReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xPanoramiXQueryVersionReq);
    return ProcRRXineramaQueryVersion(client);
}

static int
SProcRRXineramaGetState(ClientPtr client)
{
    REQUEST(xPanoramiXGetStateReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xPanoramiXGetStateReq);
    swapl(&stuff->window);
    return ProcRRXineramaGetState(client);
}

static int
SProcRRXineramaGetScreenCount(ClientPtr client)
{
    REQUEST(xPanoramiXGetScreenCountReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xPanoramiXGetScreenCountReq);
    swapl(&stuff->window);
    return ProcRRXineramaGetScreenCount(client);
}

static int
SProcRRXineramaGetScreenSize(ClientPtr client)
{
    REQUEST(xPanoramiXGetScreenSizeReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xPanoramiXGetScreenSizeReq);
    swapl(&stuff->window);
    swapl(&stuff->screen);
    return ProcRRXineramaGetScreenSize(client);
}

static int
SProcRRXineramaIsActive(ClientPtr client)
{
    REQUEST(xXineramaIsActiveReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXineramaIsActiveReq);
    return ProcRRXineramaIsActive(client);
}

static int
SProcRRXineramaQueryScreens(ClientPtr client)
{
    REQUEST(xXineramaQueryScreensReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xXineramaQueryScreensReq);
    return ProcRRXineramaQueryScreens(client);
}

int
SProcRRXineramaDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_PanoramiXQueryVersion:
        return SProcRRXineramaQueryVersion(client);
    case X_PanoramiXGetState:
        return SProcRRXineramaGetState(client);
    case X_PanoramiXGetScreenCount:
        return SProcRRXineramaGetScreenCount(client);
    case X_PanoramiXGetScreenSize:
        return SProcRRXineramaGetScreenSize(client);
    case X_XineramaIsActive:
        return SProcRRXineramaIsActive(client);
    case X_XineramaQueryScreens:
        return SProcRRXineramaQueryScreens(client);
    }
    return BadRequest;
}

void
RRXineramaExtensionInit(void)
{
#ifdef PANORAMIX
    if (!noPanoramiXExtension)
        return;
#endif

    /*
     * Xinerama isn't capable enough to have multiple protocol screens each
     * with their own output geometry.  So if there's more than one protocol
     * screen, just don't even try.
     */
    if (screenInfo.numScreens > 1)
        return;

    (void) AddExtension(PANORAMIX_PROTOCOL_NAME, 0, 0,
                        ProcRRXineramaDispatch,
                        SProcRRXineramaDispatch, NULL, StandardMinorOpcode);
}
