/*
 * Copyright 2001-2004 Red Hat Inc., Durham, North Carolina.
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
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 *  Provide support for the RENDER extension (version 0.8).
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxsync.h"
#include "dmxpict.h"
#include "dmxwindow.h"
#include "dmxpixmap.h"

#include "fb.h"
#include "pixmapstr.h"
#include "dixstruct.h"

#include <X11/extensions/render.h>
#include <X11/extensions/renderproto.h>
#include <X11/extensions/Xfixes.h>
#include "picture.h"
#include "picturestr.h"
#include "mipict.h"
#include "fbpict.h"

extern int (*ProcRenderVector[RenderNumberRequests]) (ClientPtr);

static int (*dmxSaveRenderVector[RenderNumberRequests]) (ClientPtr);

static int dmxProcRenderCreateGlyphSet(ClientPtr client);
static int dmxProcRenderFreeGlyphSet(ClientPtr client);
static int dmxProcRenderAddGlyphs(ClientPtr client);
static int dmxProcRenderFreeGlyphs(ClientPtr client);
static int dmxProcRenderCompositeGlyphs(ClientPtr client);
static int dmxProcRenderSetPictureTransform(ClientPtr client);
static int dmxProcRenderSetPictureFilter(ClientPtr client);

#if 0
/* FIXME: Not (yet) supported */
static int dmxProcRenderCreateCursor(ClientPtr client);
static int dmxProcRenderCreateAnimCursor(ClientPtr client);
#endif

/** Catch errors that might occur when allocating Glyph Sets.  Errors
 *  are saved in dmxGlyphLastError for later handling. */
static int dmxGlyphLastError;
static int
dmxGlyphErrorHandler(Display * dpy, XErrorEvent * ev)
{
    dmxGlyphLastError = ev->error_code;
    return 0;
}

/** Initialize the Proc Vector for the RENDER extension.  The functions
 *  here cannot be handled by the mi layer RENDER hooks either because
 *  the required information is no longer available when it reaches the
 *  mi layer or no mi layer hooks exist.  This function is called from
 *  InitOutput() since it should be initialized only once per server
 *  generation. */
void
dmxInitRender(void)
{
    int i;

    for (i = 0; i < RenderNumberRequests; i++)
        dmxSaveRenderVector[i] = ProcRenderVector[i];

    ProcRenderVector[X_RenderCreateGlyphSet]
        = dmxProcRenderCreateGlyphSet;
    ProcRenderVector[X_RenderFreeGlyphSet]
        = dmxProcRenderFreeGlyphSet;
    ProcRenderVector[X_RenderAddGlyphs]
        = dmxProcRenderAddGlyphs;
    ProcRenderVector[X_RenderFreeGlyphs]
        = dmxProcRenderFreeGlyphs;
    ProcRenderVector[X_RenderCompositeGlyphs8]
        = dmxProcRenderCompositeGlyphs;
    ProcRenderVector[X_RenderCompositeGlyphs16]
        = dmxProcRenderCompositeGlyphs;
    ProcRenderVector[X_RenderCompositeGlyphs32]
        = dmxProcRenderCompositeGlyphs;
    ProcRenderVector[X_RenderSetPictureTransform]
        = dmxProcRenderSetPictureTransform;
    ProcRenderVector[X_RenderSetPictureFilter]
        = dmxProcRenderSetPictureFilter;
}

/** Reset the Proc Vector for the RENDER extension back to the original
 *  functions.  This function is called from dmxCloseScreen() during the
 *  server reset (only for screen #0). */
void
dmxResetRender(void)
{
    int i;

    for (i = 0; i < RenderNumberRequests; i++)
        ProcRenderVector[i] = dmxSaveRenderVector[i];
}

/** Initialize the RENDER extension, allocate the picture privates and
 *  wrap mi function hooks.  If the shadow frame buffer is used, then
 *  call the appropriate fb initialization function. */
Bool
dmxPictureInit(ScreenPtr pScreen, PictFormatPtr formats, int nformats)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps;

    if (!miPictureInit(pScreen, formats, nformats))
        return FALSE;

    if (!dixRegisterPrivateKey
        (&dmxPictPrivateKeyRec, PRIVATE_PICTURE, sizeof(dmxPictPrivRec)))
        return FALSE;

    ps = GetPictureScreen(pScreen);

    DMX_WRAP(CreatePicture, dmxCreatePicture, dmxScreen, ps);
    DMX_WRAP(DestroyPicture, dmxDestroyPicture, dmxScreen, ps);

    DMX_WRAP(ChangePictureClip, dmxChangePictureClip, dmxScreen, ps);
    DMX_WRAP(DestroyPictureClip, dmxDestroyPictureClip, dmxScreen, ps);

    DMX_WRAP(ChangePicture, dmxChangePicture, dmxScreen, ps);
    DMX_WRAP(ValidatePicture, dmxValidatePicture, dmxScreen, ps);

    DMX_WRAP(Composite, dmxComposite, dmxScreen, ps);
    DMX_WRAP(Glyphs, dmxGlyphs, dmxScreen, ps);
    DMX_WRAP(CompositeRects, dmxCompositeRects, dmxScreen, ps);

    DMX_WRAP(Trapezoids, dmxTrapezoids, dmxScreen, ps);
    DMX_WRAP(Triangles, dmxTriangles, dmxScreen, ps);

    return TRUE;
}

/** Find the appropriate format on the requested screen given the
 *  internal format requested.  The list of formats is searched
 *  sequentially as the XRenderFindFormat() function does not always
 *  find the appropriate format when a specific format is requested. */
static XRenderPictFormat *
dmxFindFormat(DMXScreenInfo * dmxScreen, PictFormatPtr pFmt)
{
    XRenderPictFormat *pFormat = NULL;
    int i = 0;

    if (!pFmt || !dmxScreen->beDisplay)
        return pFormat;

    while (1) {
        pFormat = XRenderFindFormat(dmxScreen->beDisplay, 0, 0, i++);
        if (!pFormat)
            break;

        if (pFormat->type != pFmt->type)
            continue;
        if (pFormat->depth != pFmt->depth)
            continue;
        if (pFormat->direct.red != pFmt->direct.red)
            continue;
        if (pFormat->direct.redMask != pFmt->direct.redMask)
            continue;
        if (pFormat->direct.green != pFmt->direct.green)
            continue;
        if (pFormat->direct.greenMask != pFmt->direct.greenMask)
            continue;
        if (pFormat->direct.blue != pFmt->direct.blue)
            continue;
        if (pFormat->direct.blueMask != pFmt->direct.blueMask)
            continue;
        if (pFormat->direct.alpha != pFmt->direct.alpha)
            continue;
        if (pFormat->direct.alphaMask != pFmt->direct.alphaMask)
            continue;

        /* We have a match! */
        break;
    }

    return pFormat;
}

/** Free \a glyphSet on back-end screen number \a idx. */
Bool
dmxBEFreeGlyphSet(ScreenPtr pScreen, GlyphSetPtr glyphSet)
{
    dmxGlyphPrivPtr glyphPriv = DMX_GET_GLYPH_PRIV(glyphSet);
    int idx = pScreen->myNum;
    DMXScreenInfo *dmxScreen = &dmxScreens[idx];

    if (glyphPriv->glyphSets[idx]) {
        XRenderFreeGlyphSet(dmxScreen->beDisplay, glyphPriv->glyphSets[idx]);
        glyphPriv->glyphSets[idx] = (GlyphSet) 0;
        return TRUE;
    }

    return FALSE;
}

/** Create \a glyphSet on the backend screen number \a idx. */
int
dmxBECreateGlyphSet(int idx, GlyphSetPtr glyphSet)
{
    XRenderPictFormat *pFormat;
    DMXScreenInfo *dmxScreen = &dmxScreens[idx];
    dmxGlyphPrivPtr glyphPriv = DMX_GET_GLYPH_PRIV(glyphSet);
    PictFormatPtr pFmt = glyphSet->format;
    int (*oldErrorHandler) (Display *, XErrorEvent *);

    pFormat = dmxFindFormat(dmxScreen, pFmt);
    if (!pFormat) {
        return BadMatch;
    }

    dmxGlyphLastError = 0;
    oldErrorHandler = XSetErrorHandler(dmxGlyphErrorHandler);

    /* Catch when this fails */
    glyphPriv->glyphSets[idx]
        = XRenderCreateGlyphSet(dmxScreen->beDisplay, pFormat);

    XSetErrorHandler(oldErrorHandler);

    if (dmxGlyphLastError) {
        return dmxGlyphLastError;
    }

    return Success;
}

/** Create a Glyph Set on each screen.  Save the glyphset ID from each
 *  screen in the Glyph Set's private structure.  Fail if the format
 *  requested is not available or if the Glyph Set cannot be created on
 *  the screen. */
static int
dmxProcRenderCreateGlyphSet(ClientPtr client)
{
    int ret;

    REQUEST(xRenderCreateGlyphSetReq);

    ret = dmxSaveRenderVector[stuff->renderReqType] (client);

    if (ret == Success) {
        GlyphSetPtr glyphSet;
        dmxGlyphPrivPtr glyphPriv;
        int i;

        /* Look up glyphSet that was just created ???? */
        /* Store glyphsets from backends in glyphSet->devPrivate ????? */
        /* Make sure we handle all errors here!! */

        dixLookupResourceByType((void **) &glyphSet,
                                stuff->gsid, GlyphSetType,
                                client, DixDestroyAccess);

        glyphPriv = malloc(sizeof(dmxGlyphPrivRec));
        if (!glyphPriv)
            return BadAlloc;
        glyphPriv->glyphSets = NULL;
        MAXSCREENSALLOC_RETURN(glyphPriv->glyphSets, BadAlloc);
        DMX_SET_GLYPH_PRIV(glyphSet, glyphPriv);

        for (i = 0; i < dmxNumScreens; i++) {
            DMXScreenInfo *dmxScreen = &dmxScreens[i];
            int beret;

            if (!dmxScreen->beDisplay) {
                glyphPriv->glyphSets[i] = 0;
                continue;
            }

            if ((beret = dmxBECreateGlyphSet(i, glyphSet)) != Success) {
                int j;

                /* Free the glyph sets we've allocated thus far */
                for (j = 0; j < i; j++)
                    dmxBEFreeGlyphSet(screenInfo.screens[j], glyphSet);

                /* Free the resource created by render */
                FreeResource(stuff->gsid, RT_NONE);

                return beret;
            }
        }
    }

    return ret;
}

/** Free the previously allocated Glyph Sets for each screen. */
static int
dmxProcRenderFreeGlyphSet(ClientPtr client)
{
    GlyphSetPtr glyphSet;

    REQUEST(xRenderFreeGlyphSetReq);

    REQUEST_SIZE_MATCH(xRenderFreeGlyphSetReq);
    dixLookupResourceByType((void **) &glyphSet,
                            stuff->glyphset, GlyphSetType,
                            client, DixDestroyAccess);

    if (glyphSet && glyphSet->refcnt == 1) {
        dmxGlyphPrivPtr glyphPriv = DMX_GET_GLYPH_PRIV(glyphSet);
        int i;

        for (i = 0; i < dmxNumScreens; i++) {
            DMXScreenInfo *dmxScreen = &dmxScreens[i];

            if (dmxScreen->beDisplay) {
                if (dmxBEFreeGlyphSet(screenInfo.screens[i], glyphSet))
                    dmxSync(dmxScreen, FALSE);
            }
        }

        MAXSCREENSFREE(glyphPriv->glyphSets);
        free(glyphPriv);
        DMX_SET_GLYPH_PRIV(glyphSet, NULL);
    }

    return dmxSaveRenderVector[stuff->renderReqType] (client);
}

/** Add glyphs to the Glyph Set on each screen. */
static int
dmxProcRenderAddGlyphs(ClientPtr client)
{
    int ret;

    REQUEST(xRenderAddGlyphsReq);

    ret = dmxSaveRenderVector[stuff->renderReqType] (client);

    if (ret == Success) {
        GlyphSetPtr glyphSet;
        dmxGlyphPrivPtr glyphPriv;
        int i;
        int nglyphs;
        CARD32 *gids;
        Glyph *gidsCopy;
        xGlyphInfo *gi;
        CARD8 *bits;
        int nbytes;

        dixLookupResourceByType((void **) &glyphSet,
                                stuff->glyphset, GlyphSetType,
                                client, DixReadAccess);
        glyphPriv = DMX_GET_GLYPH_PRIV(glyphSet);

        nglyphs = stuff->nglyphs;
        gids = (CARD32 *) (stuff + 1);
        gi = (xGlyphInfo *) (gids + nglyphs);
        bits = (CARD8 *) (gi + nglyphs);
        nbytes = ((stuff->length << 2) -
                  sizeof(xRenderAddGlyphsReq) -
                  (sizeof(CARD32) + sizeof(xGlyphInfo)) * nglyphs);

        gidsCopy = xallocarray(nglyphs, sizeof(*gidsCopy));
        for (i = 0; i < nglyphs; i++)
            gidsCopy[i] = gids[i];

        /* FIXME: Will this ever fail? */
        for (i = 0; i < dmxNumScreens; i++) {
            DMXScreenInfo *dmxScreen = &dmxScreens[i];

            if (dmxScreen->beDisplay) {
                XRenderAddGlyphs(dmxScreen->beDisplay,
                                 glyphPriv->glyphSets[i],
                                 gidsCopy,
                                 (XGlyphInfo *) gi,
                                 nglyphs, (char *) bits, nbytes);
                dmxSync(dmxScreen, FALSE);
            }
        }
        free(gidsCopy);
    }

    return ret;
}

/** Free glyphs from the Glyph Set for each screen. */
static int
dmxProcRenderFreeGlyphs(ClientPtr client)
{
    GlyphSetPtr glyphSet;

    REQUEST(xRenderFreeGlyphsReq);

    REQUEST_AT_LEAST_SIZE(xRenderFreeGlyphsReq);
    dixLookupResourceByType((void **) &glyphSet,
                            stuff->glyphset, GlyphSetType,
                            client, DixWriteAccess);

    if (glyphSet) {
        dmxGlyphPrivPtr glyphPriv = DMX_GET_GLYPH_PRIV(glyphSet);
        int i;
        int nglyphs;
        Glyph *gids;

        nglyphs = ((client->req_len << 2) - sizeof(xRenderFreeGlyphsReq)) >> 2;
        if (nglyphs) {
            gids = xallocarray(nglyphs, sizeof(*gids));
            for (i = 0; i < nglyphs; i++)
                gids[i] = ((CARD32 *) (stuff + 1))[i];

            for (i = 0; i < dmxNumScreens; i++) {
                DMXScreenInfo *dmxScreen = &dmxScreens[i];

                if (dmxScreen->beDisplay) {
                    XRenderFreeGlyphs(dmxScreen->beDisplay,
                                      glyphPriv->glyphSets[i], gids, nglyphs);
                    dmxSync(dmxScreen, FALSE);
                }
            }
            free(gids);
        }
    }

    return dmxSaveRenderVector[stuff->renderReqType] (client);
}

/** Composite glyphs on each screen into the requested picture.  If
 *  either the src or dest picture has not been allocated due to lazy
 *  window creation, this request will gracefully return. */
static int
dmxProcRenderCompositeGlyphs(ClientPtr client)
{
    int ret;

    REQUEST(xRenderCompositeGlyphsReq);

    ret = dmxSaveRenderVector[stuff->renderReqType] (client);

    /* For the following to work with PanoramiX, it assumes that Render
     * wraps the ProcRenderVector after dmxRenderInit has been called.
     */
    if (ret == Success) {
        PicturePtr pSrc;
        dmxPictPrivPtr pSrcPriv;
        PicturePtr pDst;
        dmxPictPrivPtr pDstPriv;
        PictFormatPtr pFmt;
        XRenderPictFormat *pFormat;
        int size;

        int scrnNum;
        DMXScreenInfo *dmxScreen;

        CARD8 *buffer;
        CARD8 *end;
        int space;

        int nglyph;
        char *glyphs;
        char *curGlyph;

        xGlyphElt *elt;
        int nelt;
        XGlyphElt8 *elts;
        XGlyphElt8 *curElt;

        GlyphSetPtr glyphSet;
        dmxGlyphPrivPtr glyphPriv;

        dixLookupResourceByType((void **) &pSrc,
                                stuff->src, PictureType, client, DixReadAccess);

        pSrcPriv = DMX_GET_PICT_PRIV(pSrc);
        if (!pSrcPriv->pict)
            return ret;

        dixLookupResourceByType((void **) &pDst,
                                stuff->dst, PictureType,
                                client, DixWriteAccess);

        pDstPriv = DMX_GET_PICT_PRIV(pDst);
        if (!pDstPriv->pict)
            return ret;

        scrnNum = pDst->pDrawable->pScreen->myNum;
        dmxScreen = &dmxScreens[scrnNum];

        /* Note: If the back-end display has been detached, then it
         * should not be possible to reach here since the pSrcPriv->pict
         * and pDstPriv->pict will have already been set to 0.
         */
        if (!dmxScreen->beDisplay)
            return ret;

        if (stuff->maskFormat)
            dixLookupResourceByType((void **) &pFmt,
                                    stuff->maskFormat, PictFormatType,
                                    client, DixReadAccess);
        else
            pFmt = NULL;

        pFormat = dmxFindFormat(dmxScreen, pFmt);

        switch (stuff->renderReqType) {
        case X_RenderCompositeGlyphs8:
            size = sizeof(CARD8);
            break;
        case X_RenderCompositeGlyphs16:
            size = sizeof(CARD16);
            break;
        case X_RenderCompositeGlyphs32:
            size = sizeof(CARD32);
            break;
        default:
            return BadPictOp;   /* Can't happen */
        }

        buffer = (CARD8 *) (stuff + 1);
        end = (CARD8 *) stuff + (stuff->length << 2);
        nelt = 0;
        nglyph = 0;
        while (buffer + sizeof(xGlyphElt) < end) {
            elt = (xGlyphElt *) buffer;
            buffer += sizeof(xGlyphElt);

            if (elt->len == 0xff) {
                buffer += 4;
            }
            else {
                nelt++;
                nglyph += elt->len;
                space = size * elt->len;
                if (space & 3)
                    space += 4 - (space & 3);
                buffer += space;
            }
        }

        /* The following only works for Render version > 0.2 */

        /* All of the XGlyphElt* structure sizes are identical */
        elts = xallocarray(nelt, sizeof(XGlyphElt8));
        if (!elts)
            return BadAlloc;

        glyphs = xallocarray(nglyph, size);
        if (!glyphs) {
            free(elts);
            return BadAlloc;
        }

        buffer = (CARD8 *) (stuff + 1);
        end = (CARD8 *) stuff + (stuff->length << 2);
        curGlyph = glyphs;
        curElt = elts;

        dixLookupResourceByType((void **) &glyphSet,
                                stuff->glyphset, GlyphSetType,
                                client, DixReadAccess);
        glyphPriv = DMX_GET_GLYPH_PRIV(glyphSet);

        while (buffer + sizeof(xGlyphElt) < end) {
            elt = (xGlyphElt *) buffer;
            buffer += sizeof(xGlyphElt);

            if (elt->len == 0xff) {
                dixLookupResourceByType((void **) &glyphSet,
                                        *((CARD32 *) buffer),
                                        GlyphSetType, client, DixReadAccess);
                glyphPriv = DMX_GET_GLYPH_PRIV(glyphSet);
                buffer += 4;
            }
            else {
                curElt->glyphset = glyphPriv->glyphSets[scrnNum];
                curElt->xOff = elt->deltax;
                curElt->yOff = elt->deltay;
                curElt->nchars = elt->len;
                curElt->chars = curGlyph;

                memcpy(curGlyph, buffer, size * elt->len);
                curGlyph += size * elt->len;

                curElt++;

                space = size * elt->len;
                if (space & 3)
                    space += 4 - (space & 3);
                buffer += space;
            }
        }

        switch (stuff->renderReqType) {
        case X_RenderCompositeGlyphs8:
            XRenderCompositeText8(dmxScreen->beDisplay, stuff->op,
                                  pSrcPriv->pict, pDstPriv->pict,
                                  pFormat,
                                  stuff->xSrc, stuff->ySrc, 0, 0, elts, nelt);
            break;
        case X_RenderCompositeGlyphs16:
            XRenderCompositeText16(dmxScreen->beDisplay, stuff->op,
                                   pSrcPriv->pict, pDstPriv->pict,
                                   pFormat,
                                   stuff->xSrc, stuff->ySrc,
                                   0, 0, (XGlyphElt16 *) elts, nelt);
            break;
        case X_RenderCompositeGlyphs32:
            XRenderCompositeText32(dmxScreen->beDisplay, stuff->op,
                                   pSrcPriv->pict, pDstPriv->pict,
                                   pFormat,
                                   stuff->xSrc, stuff->ySrc,
                                   0, 0, (XGlyphElt32 *) elts, nelt);
            break;
        }

        dmxSync(dmxScreen, FALSE);

        free(elts);
        free(glyphs);
    }

    return ret;
}

/** Set the picture transform on each screen. */
static int
dmxProcRenderSetPictureTransform(ClientPtr client)
{
    DMXScreenInfo *dmxScreen;
    PicturePtr pPicture;
    dmxPictPrivPtr pPictPriv;
    XTransform xform;

    REQUEST(xRenderSetPictureTransformReq);

    REQUEST_SIZE_MATCH(xRenderSetPictureTransformReq);
    VERIFY_PICTURE(pPicture, stuff->picture, client, DixWriteAccess);

    /* For the following to work with PanoramiX, it assumes that Render
     * wraps the ProcRenderVector after dmxRenderInit has been called.
     */
    dmxScreen = &dmxScreens[pPicture->pDrawable->pScreen->myNum];
    pPictPriv = DMX_GET_PICT_PRIV(pPicture);

    if (pPictPriv->pict) {
        xform.matrix[0][0] = stuff->transform.matrix11;
        xform.matrix[0][1] = stuff->transform.matrix12;
        xform.matrix[0][2] = stuff->transform.matrix13;
        xform.matrix[1][0] = stuff->transform.matrix21;
        xform.matrix[1][1] = stuff->transform.matrix22;
        xform.matrix[1][2] = stuff->transform.matrix23;
        xform.matrix[2][0] = stuff->transform.matrix31;
        xform.matrix[2][1] = stuff->transform.matrix32;
        xform.matrix[2][2] = stuff->transform.matrix33;

        XRenderSetPictureTransform(dmxScreen->beDisplay,
                                   pPictPriv->pict, &xform);
        dmxSync(dmxScreen, FALSE);
    }

    return dmxSaveRenderVector[stuff->renderReqType] (client);
}

/** Set the picture filter on each screen. */
static int
dmxProcRenderSetPictureFilter(ClientPtr client)
{
    DMXScreenInfo *dmxScreen;
    PicturePtr pPicture;
    dmxPictPrivPtr pPictPriv;
    char *filter;
    XFixed *params;
    int nparams;

    REQUEST(xRenderSetPictureFilterReq);

    REQUEST_AT_LEAST_SIZE(xRenderSetPictureFilterReq);
    VERIFY_PICTURE(pPicture, stuff->picture, client, DixWriteAccess);

    /* For the following to work with PanoramiX, it assumes that Render
     * wraps the ProcRenderVector after dmxRenderInit has been called.
     */
    dmxScreen = &dmxScreens[pPicture->pDrawable->pScreen->myNum];
    pPictPriv = DMX_GET_PICT_PRIV(pPicture);

    if (pPictPriv->pict) {
        filter = (char *) (stuff + 1);
        params = (XFixed *) (filter + ((stuff->nbytes + 3) & ~3));
        nparams = ((XFixed *) stuff + client->req_len) - params;

        XRenderSetPictureFilter(dmxScreen->beDisplay,
                                pPictPriv->pict, filter, params, nparams);
        dmxSync(dmxScreen, FALSE);
    }

    return dmxSaveRenderVector[stuff->renderReqType] (client);
}

/** Create a picture on the appropriate screen.  This is the actual
 *  function that creates the picture.  However, if the associated
 *  window has not yet been created due to lazy window creation, then
 *  delay the picture creation until the window is mapped. */
static Picture
dmxDoCreatePicture(PicturePtr pPicture)
{
    DrawablePtr pDraw = pPicture->pDrawable;
    ScreenPtr pScreen = pDraw->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    XRenderPictFormat *pFormat;
    Drawable draw;

    if (pPicture->pDrawable->type == DRAWABLE_WINDOW) {
        dmxWinPrivPtr pWinPriv = DMX_GET_WINDOW_PRIV((WindowPtr) (pDraw));

        if (!(draw = pWinPriv->window)) {
            /* Window has not been created yet due to the window
             * optimization.  Delay picture creation until window is
             * mapped.
             */
            pWinPriv->hasPict = TRUE;
            return 0;
        }
    }
    else {
        dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV((PixmapPtr) (pDraw));

        if (!(draw = pPixPriv->pixmap)) {
            /* FIXME: Zero width/height pixmap?? */
            return 0;
        }
    }

    /* This should not be reached if the back-end display has been
     * detached because the pWinPriv->window or the pPixPriv->pixmap
     * will be NULL; however, we add it here for completeness
     */
    if (!dmxScreen->beDisplay)
        return 0;

    pFormat = dmxFindFormat(dmxScreen, pPicture->pFormat);

    return XRenderCreatePicture(dmxScreen->beDisplay, draw, pFormat, 0, 0);
}

/** Create a list of pictures.  This function is called by
 *  dmxCreateAndRealizeWindow() during the lazy window creation
 *  realization process.  It creates the entire list of pictures that
 *  are associated with the given window. */
void
dmxCreatePictureList(WindowPtr pWindow)
{
    PicturePtr pPicture = GetPictureWindow(pWindow);

    while (pPicture) {
        dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pPicture);

        /* Create the picture for this window */
        pPictPriv->pict = dmxDoCreatePicture(pPicture);

        /* ValidatePicture takes care of the state changes */

        pPicture = pPicture->pNext;
    }
}

/** Create \a pPicture on the backend. */
int
dmxBECreatePicture(PicturePtr pPicture)
{
    dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pPicture);

    /* Create picutre on BE */
    pPictPriv->pict = dmxDoCreatePicture(pPicture);

    /* Flush changes to the backend server */
    dmxValidatePicture(pPicture, (1 << (CPLastBit + 1)) - 1);

    return Success;
}

/** Create a picture.  This function handles the CreatePicture
 *  unwrapping/wrapping and calls dmxDoCreatePicture to actually create
 *  the picture on the appropriate screen.  */
int
dmxCreatePicture(PicturePtr pPicture)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pPicture);
    int ret = Success;

    DMX_UNWRAP(CreatePicture, dmxScreen, ps);
#if 1
    if (ps->CreatePicture)
        ret = ps->CreatePicture(pPicture);
#endif

    /* Create picture on back-end server */
    pPictPriv->pict = dmxDoCreatePicture(pPicture);
    pPictPriv->savedMask = 0;

    DMX_WRAP(CreatePicture, dmxCreatePicture, dmxScreen, ps);

    return ret;
}

/** Destroy \a pPicture on the back-end server. */
Bool
dmxBEFreePicture(PicturePtr pPicture)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pPicture);

    if (pPictPriv->pict) {
        XRenderFreePicture(dmxScreen->beDisplay, pPictPriv->pict);
        pPictPriv->pict = (Picture) 0;
        return TRUE;
    }

    return FALSE;
}

/** Destroy a list of pictures that are associated with the window that
 *  is being destroyed.  This function is called by #dmxDestroyWindow().
 *  */
Bool
dmxDestroyPictureList(WindowPtr pWindow)
{
    PicturePtr pPicture = GetPictureWindow(pWindow);
    Bool ret = FALSE;

    while (pPicture) {
        ret |= dmxBEFreePicture(pPicture);
        pPicture = pPicture->pNext;
    }

    return ret;
}

/** Destroy a picture.  This function calls the wrapped function that
 *  frees the resources in the DMX server associated with this
 *  picture. */
void
dmxDestroyPicture(PicturePtr pPicture)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);

    DMX_UNWRAP(DestroyPicture, dmxScreen, ps);

    /* Destroy picture on back-end server */
    if (dmxBEFreePicture(pPicture))
        dmxSync(dmxScreen, FALSE);

#if 1
    if (ps->DestroyPicture)
        ps->DestroyPicture(pPicture);
#endif
    DMX_WRAP(DestroyPicture, dmxDestroyPicture, dmxScreen, ps);
}

/** Change the picture's list of clip rectangles. */
int
dmxChangePictureClip(PicturePtr pPicture, int clipType, void *value, int n)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pPicture);

    DMX_UNWRAP(ChangePictureClip, dmxScreen, ps);
#if 1
    if (ps->ChangePictureClip)
        ps->ChangePictureClip(pPicture, clipType, value, n);
#endif

    /* Change picture clip rects on back-end server */
    if (pPictPriv->pict) {
        /* The clip has already been changed into a region by the mi
         * routine called above.
         */
        if (clipType == CT_NONE) {
            /* Disable clipping, show all */
            XFixesSetPictureClipRegion(dmxScreen->beDisplay,
                                       pPictPriv->pict, 0, 0, None);
        }
        else if (pPicture->clientClip) {
            RegionPtr pClip = pPicture->clientClip;
            BoxPtr pBox = RegionRects(pClip);
            int nBox = RegionNumRects(pClip);
            XRectangle *pRects;
            XRectangle *pRect;
            int nRects;

            nRects = nBox;
            pRects = pRect = xallocarray(nRects, sizeof(*pRect));

            while (nBox--) {
                pRect->x = pBox->x1;
                pRect->y = pBox->y1;
                pRect->width = pBox->x2 - pBox->x1;
                pRect->height = pBox->y2 - pBox->y1;
                pBox++;
                pRect++;
            }

            XRenderSetPictureClipRectangles(dmxScreen->beDisplay,
                                            pPictPriv->pict,
                                            0, 0, pRects, nRects);
            free(pRects);
        }
        else {
            XRenderSetPictureClipRectangles(dmxScreen->beDisplay,
                                            pPictPriv->pict, 0, 0, NULL, 0);
        }
        dmxSync(dmxScreen, FALSE);
    }
    else {
        /* FIXME: Handle saving clip region when offscreen */
    }

    DMX_WRAP(ChangePictureClip, dmxChangePictureClip, dmxScreen, ps);

    return Success;
}

/** Destroy the picture's list of clip rectangles. */
void
dmxDestroyPictureClip(PicturePtr pPicture)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pPicture);

    DMX_UNWRAP(DestroyPictureClip, dmxScreen, ps);
#if 1
    if (ps->DestroyPictureClip)
        ps->DestroyPictureClip(pPicture);
#endif

    /* Destroy picture clip rects on back-end server */
    if (pPictPriv->pict) {
        XRenderSetPictureClipRectangles(dmxScreen->beDisplay,
                                        pPictPriv->pict, 0, 0, NULL, 0);
        dmxSync(dmxScreen, FALSE);
    }
    else {
        /* FIXME: Handle destroying clip region when offscreen */
    }

    DMX_WRAP(DestroyPictureClip, dmxDestroyPictureClip, dmxScreen, ps);
}

/** Change the attributes of the pictures.  If the picture has not yet
 *  been created due to lazy window creation, save the mask so that it
 *  can be used to appropriately initialize the picture's attributes
 *  when it is created later. */
void
dmxChangePicture(PicturePtr pPicture, Mask mask)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pPicture);

    DMX_UNWRAP(ChangePicture, dmxScreen, ps);
#if 1
    if (ps->ChangePicture)
        ps->ChangePicture(pPicture, mask);
#endif

    /* Picture attribute changes are handled in ValidatePicture */
    pPictPriv->savedMask |= mask;

    DMX_WRAP(ChangePicture, dmxChangePicture, dmxScreen, ps);
}

/** Validate the picture's attributes before rendering to it.  Update
 *  any picture attributes that have been changed by one of the higher
 *  layers. */
void
dmxValidatePicture(PicturePtr pPicture, Mask mask)
{
    ScreenPtr pScreen = pPicture->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pPicture);

    DMX_UNWRAP(ValidatePicture, dmxScreen, ps);

    /* Change picture attributes on back-end server */
    if (pPictPriv->pict) {
        XRenderPictureAttributes attribs;

        if (mask & CPRepeat) {
            attribs.repeat = pPicture->repeatType;
        }
        if (mask & CPAlphaMap) {
            if (pPicture->alphaMap) {
                dmxPictPrivPtr pAlphaPriv;

                pAlphaPriv = DMX_GET_PICT_PRIV(pPicture->alphaMap);
                if (pAlphaPriv->pict) {
                    attribs.alpha_map = pAlphaPriv->pict;
                }
                else {
                    /* FIXME: alpha picture drawable has not been created?? */
                    return;     /* or should this be: attribs.alpha_map = None; */
                }
            }
            else {
                attribs.alpha_map = None;
            }
        }
        if (mask & CPAlphaXOrigin)
            attribs.alpha_x_origin = pPicture->alphaOrigin.x;
        if (mask & CPAlphaYOrigin)
            attribs.alpha_y_origin = pPicture->alphaOrigin.y;
        if (mask & CPClipXOrigin)
            attribs.clip_x_origin = pPicture->clipOrigin.x;
        if (mask & CPClipYOrigin)
            attribs.clip_y_origin = pPicture->clipOrigin.y;
        if (mask & CPClipMask)
            mask &= ~CPClipMask;        /* Handled in ChangePictureClip */
        if (mask & CPGraphicsExposure)
            attribs.graphics_exposures = pPicture->graphicsExposures;
        if (mask & CPSubwindowMode)
            attribs.subwindow_mode = pPicture->subWindowMode;
        if (mask & CPPolyEdge)
            attribs.poly_edge = pPicture->polyEdge;
        if (mask & CPPolyMode)
            attribs.poly_mode = pPicture->polyMode;
        if (mask & CPComponentAlpha)
            attribs.component_alpha = pPicture->componentAlpha;

        XRenderChangePicture(dmxScreen->beDisplay, pPictPriv->pict,
                             mask, &attribs);
        dmxSync(dmxScreen, FALSE);
    }
    else {
        pPictPriv->savedMask |= mask;
    }

#if 1
    if (ps->ValidatePicture)
        ps->ValidatePicture(pPicture, mask);
#endif

    DMX_WRAP(ValidatePicture, dmxValidatePicture, dmxScreen, ps);
}

/** Composite a picture on the appropriate screen by combining the
 *  specified rectangle of the transformed src and mask operands with
 *  the specified rectangle of the dst using op as the compositing
 *  operator.  For a complete description see the protocol document of
 *  the RENDER library. */
void
dmxComposite(CARD8 op,
             PicturePtr pSrc, PicturePtr pMask, PicturePtr pDst,
             INT16 xSrc, INT16 ySrc,
             INT16 xMask, INT16 yMask,
             INT16 xDst, INT16 yDst, CARD16 width, CARD16 height)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pSrcPriv = DMX_GET_PICT_PRIV(pSrc);
    dmxPictPrivPtr pMaskPriv = NULL;
    dmxPictPrivPtr pDstPriv = DMX_GET_PICT_PRIV(pDst);

    if (pMask)
        pMaskPriv = DMX_GET_PICT_PRIV(pMask);

    DMX_UNWRAP(Composite, dmxScreen, ps);
#if 0
    if (ps->Composite)
        ps->Composite(op, pSrc, pMask, pDst,
                      xSrc, ySrc, xMask, yMask, xDst, yDst, width, height);
#endif

    /* Composite on back-end server */
    if (pSrcPriv->pict && pDstPriv->pict &&
        ((pMaskPriv && pMaskPriv->pict) || !pMaskPriv)) {
        XRenderComposite(dmxScreen->beDisplay,
                         op,
                         pSrcPriv->pict,
                         pMaskPriv ? pMaskPriv->pict : None,
                         pDstPriv->pict,
                         xSrc, ySrc, xMask, yMask, xDst, yDst, width, height);
        dmxSync(dmxScreen, FALSE);
    }

    DMX_WRAP(Composite, dmxComposite, dmxScreen, ps);
}

/** Null function to catch when/if RENDER calls lower level mi hooks.
 *  Compositing glyphs is handled by dmxProcRenderCompositeGlyphs().
 *  This function should never be called. */
void
dmxGlyphs(CARD8 op,
          PicturePtr pSrc, PicturePtr pDst,
          PictFormatPtr maskFormat,
          INT16 xSrc, INT16 ySrc,
          int nlists, GlyphListPtr lists, GlyphPtr * glyphs)
{
    /* This won't work, so we need to wrap ProcRenderCompositeGlyphs */
}

/** Fill a rectangle on the appropriate screen by combining the color
 *  with the dest picture in the area specified by the list of
 *  rectangles.  For a complete description see the protocol document of
 *  the RENDER library. */
void
dmxCompositeRects(CARD8 op,
                  PicturePtr pDst,
                  xRenderColor * color, int nRect, xRectangle *rects)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pPictPriv = DMX_GET_PICT_PRIV(pDst);

    DMX_UNWRAP(CompositeRects, dmxScreen, ps);
#if 0
    if (ps->CompositeRects)
        ps->CompositeRects(op, pDst, color, nRect, rects);
#endif

    /* CompositeRects on back-end server */
    if (pPictPriv->pict) {
        XRenderFillRectangles(dmxScreen->beDisplay,
                              op,
                              pPictPriv->pict,
                              (XRenderColor *) color,
                              (XRectangle *) rects, nRect);
        dmxSync(dmxScreen, FALSE);
    }

    DMX_WRAP(CompositeRects, dmxCompositeRects, dmxScreen, ps);
}

/** Indexed color visuals are not yet supported. */
Bool
dmxInitIndexed(ScreenPtr pScreen, PictFormatPtr pFormat)
{
    return TRUE;
}

/** Indexed color visuals are not yet supported. */
void
dmxCloseIndexed(ScreenPtr pScreen, PictFormatPtr pFormat)
{
}

/** Indexed color visuals are not yet supported. */
void
dmxUpdateIndexed(ScreenPtr pScreen, PictFormatPtr pFormat,
                 int ndef, xColorItem * pdef)
{
}

/** Composite a list of trapezoids on the appropriate screen.  For a
 *  complete description see the protocol document of the RENDER
 *  library. */
void
dmxTrapezoids(CARD8 op, PicturePtr pSrc, PicturePtr pDst,
              PictFormatPtr maskFormat,
              INT16 xSrc, INT16 ySrc, int ntrap, xTrapezoid * traps)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pSrcPriv = DMX_GET_PICT_PRIV(pSrc);
    dmxPictPrivPtr pDstPriv = DMX_GET_PICT_PRIV(pDst);

    DMX_UNWRAP(Trapezoids, dmxScreen, ps);
#if 0
    if (ps->Trapezoids)
        ps->Trapezoids(op, pSrc, pDst, maskFormat, xSrc, ySrc, ntrap, *traps);
#endif

    /* Draw trapezoids on back-end server */
    if (pDstPriv->pict) {
        XRenderPictFormat *pFormat;

        pFormat = dmxFindFormat(dmxScreen, maskFormat);
        if (!pFormat) {
            /* FIXME: Error! */
        }

        XRenderCompositeTrapezoids(dmxScreen->beDisplay,
                                   op,
                                   pSrcPriv->pict,
                                   pDstPriv->pict,
                                   pFormat,
                                   xSrc, ySrc, (XTrapezoid *) traps, ntrap);
        dmxSync(dmxScreen, FALSE);
    }

    DMX_WRAP(Trapezoids, dmxTrapezoids, dmxScreen, ps);
}

/** Composite a list of triangles on the appropriate screen.  For a
 *  complete description see the protocol document of the RENDER
 *  library. */
void
dmxTriangles(CARD8 op, PicturePtr pSrc, PicturePtr pDst,
             PictFormatPtr maskFormat,
             INT16 xSrc, INT16 ySrc, int ntri, xTriangle * tris)
{
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    PictureScreenPtr ps = GetPictureScreen(pScreen);
    dmxPictPrivPtr pSrcPriv = DMX_GET_PICT_PRIV(pSrc);
    dmxPictPrivPtr pDstPriv = DMX_GET_PICT_PRIV(pDst);

    DMX_UNWRAP(Triangles, dmxScreen, ps);
#if 0
    if (ps->Triangles)
        ps->Triangles(op, pSrc, pDst, maskFormat, xSrc, ySrc, ntri, *tris);
#endif

    /* Draw trapezoids on back-end server */
    if (pDstPriv->pict) {
        XRenderPictFormat *pFormat;

        pFormat = dmxFindFormat(dmxScreen, maskFormat);
        if (!pFormat) {
            /* FIXME: Error! */
        }

        XRenderCompositeTriangles(dmxScreen->beDisplay,
                                  op,
                                  pSrcPriv->pict,
                                  pDstPriv->pict,
                                  pFormat,
                                  xSrc, ySrc, (XTriangle *) tris, ntri);
        dmxSync(dmxScreen, FALSE);
    }

    DMX_WRAP(Triangles, dmxTriangles, dmxScreen, ps);
}
