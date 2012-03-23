/*
 *Copyright (C) 2003-2004 Harold L Hunt II All Rights Reserved.
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 *"Software"), to deal in the Software without restriction, including
 *without limitation the rights to use, copy, modify, merge, publish,
 *distribute, sublicense, and/or sell copies of the Software, and to
 *permit persons to whom the Software is furnished to do so, subject to
 *the following conditions:
 *
 *The above copyright notice and this permission notice shall be
 *included in all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *NONINFRINGEMENT. IN NO EVENT SHALL HAROLD L HUNT II BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of Harold L Hunt II
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from Harold L Hunt II.
 *
 * Authors:	Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvproto.h>

void
 winInitVideo(ScreenPtr pScreen);

/*
 * winInitVideo - Initialize support for the X Video (Xv) Extension.
 */

void
winInitVideo(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;

    if (pScreenInfo->dwBPP > 8) {

    }

}

#if 0
#include "../xfree86/common/xf86.h"
#include "../Xext/xvdix.h"
#include "../xfree86/common/xf86xv.h"
#include <X11/extensions/Xv.h>
#endif

#if 0
/* client libraries expect an encoding */
static XF86VideoEncodingRec DummyEncoding[1] = {
    {
     0,
     "XV_IMAGE",
     IMAGE_MAX_WIDTH, IMAGE_MAX_HEIGHT,
     {1, 1}
     }
};

#define NUM_FORMATS 3

static XF86VideoFormatRec Formats[NUM_FORMATS] = {
    {15, TrueColor}, {16, TrueColor}, {24, TrueColor}
};

#define NUM_ATTRIBUTES 3

static XF86AttributeRec Attributes[NUM_ATTRIBUTES] = {
    {XvSettable | XvGettable, 0, (1 << 24) - 1, "XV_COLORKEY"},
    {XvSettable | XvGettable, -128, 127, "XV_BRIGHTNESS"},
    {XvSettable | XvGettable, 0, 255, "XV_CONTRAST"}
};

#define NUM_IMAGES 4

static XF86ImageRec Images[NUM_IMAGES] = {
    XVIMAGE_YUY2,
    XVIMAGE_YV12,
    XVIMAGE_I420,
    XVIMAGE_UYVY
};

/*
 * winInitVideo - Initialize support for the X Video (Xv) Extension.
 */

void
winInitVideo(ScreenPtr pScreen)
{
    winScreenPriv(pScreen);
    winScreenInfo *pScreenInfo = pScreenPriv->pScreenInfo;
    XF86VideoAdaptorPtr newAdaptor = NULL;

    if (pScreenInfo->dwBPP > 8) {
        newAdaptor = I810SetupImageVideo(pScreen);
        I810InitOffscreenImages(pScreen);
    }

    xf86XVScreenInit(pScreen, adaptors, 1);
}

static XF86VideoAdaptorPtr
winSetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

#if 0
    I810Ptr pI810 = I810PTR(pScrn);
#endif
    XF86VideoAdaptorPtr adapt;

    if (!(adapt = calloc(1, sizeof(XF86VideoAdaptorRec))))
        return NULL;

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;
    adapt->name = PROJECT_NAME " Video Overlay";
    adapt->nEncodings = 1;
    adapt->pEncodings = DummyEncoding;
    adapt->nFormats = NUM_FORMATS;
    adapt->pFormats = Formats;
    adapt->nPorts = 1;
    adapt->pPortPrivates = NULL;

    adapt->pPortPrivates[0].ptr = NULL;
    adapt->pAttributes = Attributes;
    adapt->nImages = NUM_IMAGES;
    adapt->nAttributes = NUM_ATTRIBUTES;
    adapt->pImages = Images;
    adapt->PutVideo = NULL;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
#if 0
    adapt->StopVideo = I810StopVideo;
    adapt->SetPortAttribute = I810SetPortAttribute;
    adapt->GetPortAttribute = I810GetPortAttribute;
    adapt->QueryBestSize = I810QueryBestSize;
    adapt->PutImage = I810PutImage;
    adapt->QueryImageAttributes = I810QueryImageAttributes;
#endif

#if 0
    pPriv->colorKey = pI810->colorKey & ((1 << pScrn->depth) - 1);
#endif
    pPriv->videoStatus = 0;
    pPriv->brightness = 0;
    pPriv->contrast = 64;
    pPriv->linear = NULL;
    pPriv->currentBuf = 0;

#if 0
    /* gotta uninit this someplace */
    RegionNull(&pPriv->clip);
#endif

#if 0
    pI810->adaptor = adapt;

    pI810->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = I810BlockHandler;
#endif

#if 0
    xvBrightness = MAKE_ATOM("XV_BRIGHTNESS");
    xvContrast = MAKE_ATOM("XV_CONTRAST");
    xvColorKey = MAKE_ATOM("XV_COLORKEY");
#endif

#if 0
    I810ResetVideo(pScrn);
#endif

    return adapt;
}
#endif
