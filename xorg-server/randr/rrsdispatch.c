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

#include "randrstr.h"

static int
SProcRRQueryVersion(ClientPtr client)
{
    REQUEST(xRRQueryVersionReq);

    REQUEST_SIZE_MATCH(xRRQueryVersionReq);
    swaps(&stuff->length);
    swapl(&stuff->majorVersion);
    swapl(&stuff->minorVersion);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetScreenInfo(ClientPtr client)
{
    REQUEST(xRRGetScreenInfoReq);

    REQUEST_SIZE_MATCH(xRRGetScreenInfoReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRSetScreenConfig(ClientPtr client)
{
    REQUEST(xRRSetScreenConfigReq);

    if (RRClientKnowsRates(client)) {
        REQUEST_SIZE_MATCH(xRRSetScreenConfigReq);
        swaps(&stuff->rate);
    }
    else {
        REQUEST_SIZE_MATCH(xRR1_0SetScreenConfigReq);
    }

    swaps(&stuff->length);
    swapl(&stuff->drawable);
    swapl(&stuff->timestamp);
    swaps(&stuff->sizeID);
    swaps(&stuff->rotation);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRSelectInput(ClientPtr client)
{
    REQUEST(xRRSelectInputReq);

    REQUEST_SIZE_MATCH(xRRSelectInputReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    swaps(&stuff->enable);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetScreenSizeRange(ClientPtr client)
{
    REQUEST(xRRGetScreenSizeRangeReq);

    REQUEST_SIZE_MATCH(xRRGetScreenSizeRangeReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRSetScreenSize(ClientPtr client)
{
    REQUEST(xRRSetScreenSizeReq);

    REQUEST_SIZE_MATCH(xRRSetScreenSizeReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    swaps(&stuff->width);
    swaps(&stuff->height);
    swapl(&stuff->widthInMillimeters);
    swapl(&stuff->heightInMillimeters);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetScreenResources(ClientPtr client)
{
    REQUEST(xRRGetScreenResourcesReq);

    REQUEST_SIZE_MATCH(xRRGetScreenResourcesReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetOutputInfo(ClientPtr client)
{
    REQUEST(xRRGetOutputInfoReq);

    REQUEST_SIZE_MATCH(xRRGetOutputInfoReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    swapl(&stuff->configTimestamp);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRListOutputProperties(ClientPtr client)
{
    REQUEST(xRRListOutputPropertiesReq);

    REQUEST_SIZE_MATCH(xRRListOutputPropertiesReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRQueryOutputProperty(ClientPtr client)
{
    REQUEST(xRRQueryOutputPropertyReq);

    REQUEST_SIZE_MATCH(xRRQueryOutputPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    swapl(&stuff->property);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRConfigureOutputProperty(ClientPtr client)
{
    REQUEST(xRRConfigureOutputPropertyReq);

    REQUEST_AT_LEAST_SIZE(xRRConfigureOutputPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    swapl(&stuff->property);
    SwapRestL(stuff);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRChangeOutputProperty(ClientPtr client)
{
    REQUEST(xRRChangeOutputPropertyReq);

    REQUEST_AT_LEAST_SIZE(xRRChangeOutputPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    swapl(&stuff->property);
    swapl(&stuff->type);
    swapl(&stuff->nUnits);
    switch (stuff->format) {
    case 8:
        break;
    case 16:
        SwapRestS(stuff);
        break;
    case 32:
        SwapRestL(stuff);
        break;
    default:
        client->errorValue = stuff->format;
        return BadValue;
    }
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRDeleteOutputProperty(ClientPtr client)
{
    REQUEST(xRRDeleteOutputPropertyReq);

    REQUEST_SIZE_MATCH(xRRDeleteOutputPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    swapl(&stuff->property);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetOutputProperty(ClientPtr client)
{
    REQUEST(xRRGetOutputPropertyReq);

    REQUEST_SIZE_MATCH(xRRGetOutputPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    swapl(&stuff->property);
    swapl(&stuff->type);
    swapl(&stuff->longOffset);
    swapl(&stuff->longLength);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRCreateMode(ClientPtr client)
{
    xRRModeInfo *modeinfo;

    REQUEST(xRRCreateModeReq);

    REQUEST_AT_LEAST_SIZE(xRRCreateModeReq);
    swaps(&stuff->length);
    swapl(&stuff->window);

    modeinfo = &stuff->modeInfo;
    swapl(&modeinfo->id);
    swaps(&modeinfo->width);
    swaps(&modeinfo->height);
    swapl(&modeinfo->dotClock);
    swaps(&modeinfo->hSyncStart);
    swaps(&modeinfo->hSyncEnd);
    swaps(&modeinfo->hTotal);
    swaps(&modeinfo->vSyncStart);
    swaps(&modeinfo->vSyncEnd);
    swaps(&modeinfo->vTotal);
    swaps(&modeinfo->nameLength);
    swapl(&modeinfo->modeFlags);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRDestroyMode(ClientPtr client)
{
    REQUEST(xRRDestroyModeReq);

    REQUEST_SIZE_MATCH(xRRDestroyModeReq);
    swaps(&stuff->length);
    swapl(&stuff->mode);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRAddOutputMode(ClientPtr client)
{
    REQUEST(xRRAddOutputModeReq);

    REQUEST_SIZE_MATCH(xRRAddOutputModeReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    swapl(&stuff->mode);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRDeleteOutputMode(ClientPtr client)
{
    REQUEST(xRRDeleteOutputModeReq);

    REQUEST_SIZE_MATCH(xRRDeleteOutputModeReq);
    swaps(&stuff->length);
    swapl(&stuff->output);
    swapl(&stuff->mode);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetCrtcInfo(ClientPtr client)
{
    REQUEST(xRRGetCrtcInfoReq);

    REQUEST_SIZE_MATCH(xRRGetCrtcInfoReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    swapl(&stuff->configTimestamp);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRSetCrtcConfig(ClientPtr client)
{
    REQUEST(xRRSetCrtcConfigReq);

    REQUEST_AT_LEAST_SIZE(xRRSetCrtcConfigReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    swapl(&stuff->timestamp);
    swapl(&stuff->configTimestamp);
    swaps(&stuff->x);
    swaps(&stuff->y);
    swapl(&stuff->mode);
    swaps(&stuff->rotation);
    SwapRestL(stuff);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetCrtcGammaSize(ClientPtr client)
{
    REQUEST(xRRGetCrtcGammaSizeReq);

    REQUEST_SIZE_MATCH(xRRGetCrtcGammaSizeReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetCrtcGamma(ClientPtr client)
{
    REQUEST(xRRGetCrtcGammaReq);

    REQUEST_SIZE_MATCH(xRRGetCrtcGammaReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRSetCrtcGamma(ClientPtr client)
{
    REQUEST(xRRSetCrtcGammaReq);

    REQUEST_AT_LEAST_SIZE(xRRSetCrtcGammaReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    swaps(&stuff->size);
    SwapRestS(stuff);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRSetCrtcTransform(ClientPtr client)
{
    int nparams;
    char *filter;
    CARD32 *params;

    REQUEST(xRRSetCrtcTransformReq);

    REQUEST_AT_LEAST_SIZE(xRRSetCrtcTransformReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    SwapLongs((CARD32 *) &stuff->transform,
              bytes_to_int32(sizeof(xRenderTransform)));
    swaps(&stuff->nbytesFilter);
    filter = (char *) (stuff + 1);
    params = (CARD32 *) (filter + pad_to_int32(stuff->nbytesFilter));
    nparams = ((CARD32 *) stuff + client->req_len) - params;
    if (nparams < 0)
        return BadLength;

    SwapLongs(params, nparams);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetCrtcTransform(ClientPtr client)
{
    REQUEST(xRRGetCrtcTransformReq);

    REQUEST_SIZE_MATCH(xRRGetCrtcTransformReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRGetPanning(ClientPtr client)
{
    REQUEST(xRRGetPanningReq);

    REQUEST_SIZE_MATCH(xRRGetPanningReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRSetPanning(ClientPtr client)
{
    REQUEST(xRRSetPanningReq);

    REQUEST_SIZE_MATCH(xRRSetPanningReq);
    swaps(&stuff->length);
    swapl(&stuff->crtc);
    swapl(&stuff->timestamp);
    swaps(&stuff->left);
    swaps(&stuff->top);
    swaps(&stuff->width);
    swaps(&stuff->height);
    swaps(&stuff->track_left);
    swaps(&stuff->track_top);
    swaps(&stuff->track_width);
    swaps(&stuff->track_height);
    swaps(&stuff->border_left);
    swaps(&stuff->border_top);
    swaps(&stuff->border_right);
    swaps(&stuff->border_bottom);
    return (*ProcRandrVector[stuff->randrReqType]) (client);
}

static int
SProcRRSetOutputPrimary(ClientPtr client)
{
    REQUEST(xRRSetOutputPrimaryReq);

    REQUEST_SIZE_MATCH(xRRSetOutputPrimaryReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    swapl(&stuff->output);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int
SProcRRGetOutputPrimary(ClientPtr client)
{
    REQUEST(xRRGetOutputPrimaryReq);

    REQUEST_SIZE_MATCH(xRRGetOutputPrimaryReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRGetProviders(ClientPtr client)
{
    REQUEST(xRRGetProvidersReq);

    REQUEST_SIZE_MATCH(xRRGetProvidersReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRGetProviderInfo(ClientPtr client)
{
    REQUEST(xRRGetProviderInfoReq);

    REQUEST_SIZE_MATCH(xRRGetProviderInfoReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    swapl(&stuff->configTimestamp);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRSetProviderOffloadSink(ClientPtr client)
{
    REQUEST(xRRSetProviderOffloadSinkReq);

    REQUEST_SIZE_MATCH(xRRSetProviderOffloadSinkReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    swapl(&stuff->sink_provider);
    swapl(&stuff->configTimestamp);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRSetProviderOutputSource(ClientPtr client)
{
    REQUEST(xRRSetProviderOutputSourceReq);

    REQUEST_SIZE_MATCH(xRRSetProviderOutputSourceReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    swapl(&stuff->source_provider);
    swapl(&stuff->configTimestamp);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRListProviderProperties(ClientPtr client)
{
    REQUEST(xRRListProviderPropertiesReq);

    REQUEST_SIZE_MATCH(xRRListProviderPropertiesReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRQueryProviderProperty(ClientPtr client)
{
    REQUEST(xRRQueryProviderPropertyReq);

    REQUEST_SIZE_MATCH(xRRQueryProviderPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    swapl(&stuff->property);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRConfigureProviderProperty(ClientPtr client)
{
    REQUEST(xRRConfigureProviderPropertyReq);

    REQUEST_AT_LEAST_SIZE(xRRConfigureProviderPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    swapl(&stuff->property);
    /* TODO: no way to specify format? */
    SwapRestL(stuff);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRChangeProviderProperty(ClientPtr client)
{
    REQUEST(xRRChangeProviderPropertyReq);

    REQUEST_AT_LEAST_SIZE(xRRChangeProviderPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    swapl(&stuff->property);
    swapl(&stuff->type);
    swapl(&stuff->nUnits);
    switch (stuff->format) {
    case 8:
        break;
    case 16:
        SwapRestS(stuff);
        break;
    case 32:
        SwapRestL(stuff);
        break;
    }
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRDeleteProviderProperty(ClientPtr client)
{
    REQUEST(xRRDeleteProviderPropertyReq);

    REQUEST_SIZE_MATCH(xRRDeleteProviderPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    swapl(&stuff->property);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRGetProviderProperty(ClientPtr client)
{
    REQUEST(xRRGetProviderPropertyReq);

    REQUEST_SIZE_MATCH(xRRGetProviderPropertyReq);
    swaps(&stuff->length);
    swapl(&stuff->provider);
    swapl(&stuff->property);
    swapl(&stuff->type);
    swapl(&stuff->longOffset);
    swapl(&stuff->longLength);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRGetMonitors(ClientPtr client) {
    REQUEST(xRRGetMonitorsReq);

    REQUEST_SIZE_MATCH(xRRGetMonitorsReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRSetMonitor(ClientPtr client) {
    REQUEST(xRRSetMonitorReq);

    REQUEST_AT_LEAST_SIZE(xRRGetMonitorsReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    swapl(&stuff->monitor.name);
    swaps(&stuff->monitor.noutput);
    swaps(&stuff->monitor.x);
    swaps(&stuff->monitor.y);
    swaps(&stuff->monitor.width);
    swaps(&stuff->monitor.height);
    SwapRestL(stuff);
    return ProcRandrVector[stuff->randrReqType] (client);
}

static int SProcRRDeleteMonitor(ClientPtr client) {
    REQUEST(xRRDeleteMonitorReq);

    REQUEST_SIZE_MATCH(xRRDeleteMonitorReq);
    swaps(&stuff->length);
    swapl(&stuff->window);
    swapl(&stuff->name);
    return ProcRandrVector[stuff->randrReqType] (client);
}

int (*SProcRandrVector[RRNumberRequests]) (ClientPtr) = {
    SProcRRQueryVersion,        /* 0 */
/* we skip 1 to make old clients fail pretty immediately */
        NULL,                   /* 1 SProcRandrOldGetScreenInfo */
/* V1.0 apps share the same set screen config request id */
        SProcRRSetScreenConfig, /* 2 */
        NULL,                   /* 3 SProcRandrOldScreenChangeSelectInput */
/* 3 used to be ScreenChangeSelectInput; deprecated */
        SProcRRSelectInput,     /* 4 */
        SProcRRGetScreenInfo,   /* 5 */
/* V1.2 additions */
        SProcRRGetScreenSizeRange,      /* 6 */
        SProcRRSetScreenSize,   /* 7 */
        SProcRRGetScreenResources,      /* 8 */
        SProcRRGetOutputInfo,   /* 9 */
        SProcRRListOutputProperties,    /* 10 */
        SProcRRQueryOutputProperty,     /* 11 */
        SProcRRConfigureOutputProperty, /* 12 */
        SProcRRChangeOutputProperty,    /* 13 */
        SProcRRDeleteOutputProperty,    /* 14 */
        SProcRRGetOutputProperty,       /* 15 */
        SProcRRCreateMode,      /* 16 */
        SProcRRDestroyMode,     /* 17 */
        SProcRRAddOutputMode,   /* 18 */
        SProcRRDeleteOutputMode,        /* 19 */
        SProcRRGetCrtcInfo,     /* 20 */
        SProcRRSetCrtcConfig,   /* 21 */
        SProcRRGetCrtcGammaSize,        /* 22 */
        SProcRRGetCrtcGamma,    /* 23 */
        SProcRRSetCrtcGamma,    /* 24 */
/* V1.3 additions */
        SProcRRGetScreenResources,      /* 25 GetScreenResourcesCurrent */
        SProcRRSetCrtcTransform,        /* 26 */
        SProcRRGetCrtcTransform,        /* 27 */
        SProcRRGetPanning,      /* 28 */
        SProcRRSetPanning,      /* 29 */
        SProcRRSetOutputPrimary,        /* 30 */
        SProcRRGetOutputPrimary,        /* 31 */
/* V1.4 additions */
        SProcRRGetProviders,            /* 32 */
        SProcRRGetProviderInfo,         /* 33 */
        SProcRRSetProviderOffloadSink,  /* 34 */
        SProcRRSetProviderOutputSource, /* 35 */
        SProcRRListProviderProperties,  /* 36 */
        SProcRRQueryProviderProperty,   /* 37 */
        SProcRRConfigureProviderProperty, /* 38 */
        SProcRRChangeProviderProperty,  /* 39 */
        SProcRRDeleteProviderProperty,  /* 40 */
        SProcRRGetProviderProperty,     /* 41 */
        SProcRRGetMonitors,            /* 42 */
        SProcRRSetMonitor,             /* 43 */
        SProcRRDeleteMonitor,          /* 44 */
};
