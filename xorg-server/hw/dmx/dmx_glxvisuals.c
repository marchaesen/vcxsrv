/*
 * SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
 * Copyright (C) 1991-2000 Silicon Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice including the dates of first publication and
 * either this permission notice or a reference to
 * http://oss.sgi.com/projects/FreeB/
 * shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * SILICON GRAPHICS, INC. BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of Silicon Graphics, Inc.
 * shall not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization from
 * Silicon Graphics, Inc.
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include <GL/glx.h>
#include <GL/glxproto.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <limits.h>

#include "dmx_glxvisuals.h"

__GLXvisualConfig *
GetGLXVisualConfigs(Display * dpy, int screen, int *nconfigs)
{
    xGLXGetVisualConfigsReq *req;
    xGLXGetVisualConfigsReply reply;
    __GLXvisualConfig *config, *configs;
    GLint i, j, nvisuals, nprops;
    INT32 *props, *p;
    int majorOpcode, dummy;
    int num_good_visuals;

    if (!XQueryExtension(dpy, "GLX", &majorOpcode, &dummy, &dummy)) {
        return NULL;
    }

    /* Send the glXGetVisualConfigs request */
    LockDisplay(dpy);
    GetReq(GLXGetVisualConfigs, req);
    req->reqType = majorOpcode;
    req->glxCode = X_GLXGetVisualConfigs;
    req->screen = screen;
    if (!_XReply(dpy, (xReply *) &reply, 0, False)) {
        /* Something is busted. Punt. */
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }

    nvisuals = (int) reply.numVisuals;
    if (!nvisuals) {
        /* This screen does not support GL rendering */
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }

    /* Check number of properties per visual */
    nprops = (int) reply.numProps;
    if (nprops < __GLX_MIN_CONFIG_PROPS) {
        /* Huh?  Not in protocol defined limits.  Punt */
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }
    if (nprops < (INT_MAX / __GLX_SIZE_CARD32))
        props = Xmalloc(nprops * __GLX_SIZE_CARD32);
    else
        props = NULL;
    if (!props) {
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }

    /* Allocate memory for our config structure */
    if (nvisuals < (INT_MAX / sizeof(__GLXvisualConfig)))
        config = Xcalloc(nvisuals, sizeof(__GLXvisualConfig));
    else
        config = NULL;
    if (!config) {
        free(props);
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }
    configs = config;
    num_good_visuals = 0;

    /* Convert config structure into our format */
    for (i = 0; i < nvisuals; i++) {

        /* Read config structure */
        _XRead(dpy, (char *) props, (nprops * __GLX_SIZE_CARD32));

        /* fill in default values */
        config->visualRating = GLX_NONE_EXT;
        config->transparentPixel = GLX_NONE_EXT;

        /* Copy in the first set of properties */
        config->vid = props[0];
        config->class = props[1];

        config->rgba = (Bool) props[2];

        config->redSize = props[3];
        config->greenSize = props[4];
        config->blueSize = props[5];
        config->alphaSize = props[6];

        config->accumRedSize = props[7];
        config->accumGreenSize = props[8];
        config->accumBlueSize = props[9];
        config->accumAlphaSize = props[10];

        config->doubleBuffer = (Bool) props[11];
        config->stereo = (Bool) props[12];

        config->bufferSize = props[13];
        config->depthSize = props[14];
        config->stencilSize = props[15];

        config->auxBuffers = props[16];
        config->level = props[17];

        /* Process remaining properties */
        p = &props[18];
        for (j = __GLX_MIN_CONFIG_PROPS; j < nprops; j += 2) {
            int property = *p++;
            int value = *p++;

            switch (property) {
            case GLX_SAMPLES_SGIS:
                config->multiSampleSize = value;
                break;
            case GLX_SAMPLE_BUFFERS_SGIS:
                config->nMultiSampleBuffers = value;
                break;

            case GLX_TRANSPARENT_TYPE_EXT:
                config->transparentPixel = value;
                break;
            case GLX_TRANSPARENT_INDEX_VALUE_EXT:
                config->transparentIndex = value;
                break;
            case GLX_TRANSPARENT_RED_VALUE_EXT:
                config->transparentRed = value;
                break;
            case GLX_TRANSPARENT_GREEN_VALUE_EXT:
                config->transparentGreen = value;
                break;
            case GLX_TRANSPARENT_BLUE_VALUE_EXT:
                config->transparentBlue = value;
                break;
            case GLX_TRANSPARENT_ALPHA_VALUE_EXT:
                config->transparentAlpha = value;
                break;

            case GLX_VISUAL_CAVEAT_EXT:
                config->visualRating = value;
                break;

                /* visualSelectGroup is an internal used property */
            case GLX_VISUAL_SELECT_GROUP_SGIX:
                config->visualSelectGroup = value;
                break;

            default:
                /* Ignore properties we don't recognize */
                break;
            }
        }                       /* for j */

        /*
           // filter out overlay visuals (dmx does not support overlays)
         */
        if (config->level == 0) {
            config++;
            num_good_visuals++;
        }

    }                           /* for i */

    UnlockDisplay(dpy);

    nvisuals = num_good_visuals;

    config = configs;
    for (i = 0; i < nvisuals; i++) {
        /* XXX hack to fill-in mask info (need a better way to do this) */
        {
            XVisualInfo *vis, template;
            int n;

            template.screen = screen;
            template.visualid = config->vid;
            vis = XGetVisualInfo(dpy, VisualScreenMask | VisualIDMask,
                                 &template, &n);

            if (vis != NULL) {
                config->redMask = vis->red_mask;
                config->greenMask = vis->green_mask;
                config->blueMask = vis->blue_mask;
                config->alphaMask = 0;  /* XXX */
                free(vis);
            }
        }
        config++;
    }                           /* for i */

    XFree(props);
    SyncHandle();

    *nconfigs = nvisuals;
    return configs;
}

__GLXFBConfig *
GetGLXFBConfigs(Display * dpy, int glxMajorOpcode, int *nconfigs)
{
    xGLXGetFBConfigsReq *req;
    xGLXGetFBConfigsReply reply;
    __GLXFBConfig *config, *fbconfigs;
    GLint i, j, numFBConfigs, numAttribs;
    INT32 *attrs, *p;
    int screen = DefaultScreen(dpy);
    int numValidConfigs = 0;

    /* Send the glXGetFBConfigs request */
    LockDisplay(dpy);
    GetReq(GLXGetFBConfigs, req);
    req->reqType = glxMajorOpcode;
    req->glxCode = X_GLXGetFBConfigs;
    req->screen = screen;

    *nconfigs = 0;

    if (!_XReply(dpy, (xReply *) &reply, 0, False)) {
        /* Something is busted. Punt. */
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }

    numFBConfigs = (int) reply.numFBConfigs;
    if (!numFBConfigs) {
        /* This screen does not support GL rendering */
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }

    numAttribs = (int) reply.numAttribs;
    if (!numAttribs) {
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }

    if (numAttribs < (INT_MAX / (2 * __GLX_SIZE_CARD32)))
        attrs = Xmalloc(2 * numAttribs * __GLX_SIZE_CARD32);
    else
        attrs = NULL;
    if (!attrs) {
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }

    /* Allocate memory for our config structure */
    if (numFBConfigs < (INT_MAX / sizeof(__GLXFBConfig)))
        config = Xcalloc(numFBConfigs, sizeof(__GLXFBConfig));
    else
        config = NULL;
    if (!config) {
        free(attrs);
        UnlockDisplay(dpy);
        SyncHandle();
        return NULL;
    }
    fbconfigs = config;

    /* Convert attribute list into our format */
    for (i = 0; i < numFBConfigs; i++) {

        /* Fill in default properties */
        config->transparentType = GLX_NONE_EXT;
        config->visualCaveat = GLX_NONE_EXT;
        config->minRed = 0.;
        config->maxRed = 1.;
        config->minGreen = 0.;
        config->maxGreen = 1.;
        config->minBlue = 0.;
        config->maxBlue = 1.;
        config->minAlpha = 0.;
        config->maxAlpha = 1.;

        /* Read attribute list */
        _XRead(dpy, (char *) attrs, (2 * numAttribs * __GLX_SIZE_CARD32));

        p = attrs;
        for (j = 0; j < numAttribs; j++) {
            int attribute = *p++;
            int value = *p++;

            switch (attribute) {
                /* core attributes */
            case GLX_FBCONFIG_ID:
                config->id = value;
                break;
            case GLX_BUFFER_SIZE:
                config->indexBits = value;
                break;
            case GLX_LEVEL:
                config->level = value;
                break;
            case GLX_DOUBLEBUFFER:
                config->doubleBufferMode = value;
                break;
            case GLX_STEREO:
                config->stereoMode = value;
                break;
            case GLX_AUX_BUFFERS:
                config->maxAuxBuffers = value;
                break;
            case GLX_RED_SIZE:
                config->redBits = value;
                break;
            case GLX_GREEN_SIZE:
                config->greenBits = value;
                break;
            case GLX_BLUE_SIZE:
                config->blueBits = value;
                break;
            case GLX_ALPHA_SIZE:
                config->alphaBits = value;
                break;
            case GLX_DEPTH_SIZE:
                config->depthBits = value;
                break;
            case GLX_STENCIL_SIZE:
                config->stencilBits = value;
                break;
            case GLX_ACCUM_RED_SIZE:
                config->accumRedBits = value;
                break;
            case GLX_ACCUM_GREEN_SIZE:
                config->accumGreenBits = value;
                break;
            case GLX_ACCUM_BLUE_SIZE:
                config->accumBlueBits = value;
                break;
            case GLX_ACCUM_ALPHA_SIZE:
                config->accumAlphaBits = value;
                break;
            case GLX_RENDER_TYPE:
                config->renderType = value;
                break;
            case GLX_DRAWABLE_TYPE:
                config->drawableType = value;
                break;
            case GLX_X_VISUAL_TYPE:
                config->visualType = value;
                break;
            case GLX_CONFIG_CAVEAT:
                config->visualCaveat = value;
                break;
            case GLX_TRANSPARENT_TYPE:
                config->transparentType = value;
                break;
            case GLX_TRANSPARENT_INDEX_VALUE:
                config->transparentIndex = value;
                break;
            case GLX_TRANSPARENT_RED_VALUE:
                config->transparentRed = value;
                break;
            case GLX_TRANSPARENT_GREEN_VALUE:
                config->transparentGreen = value;
                break;
            case GLX_TRANSPARENT_BLUE_VALUE:
                config->transparentBlue = value;
                break;
            case GLX_TRANSPARENT_ALPHA_VALUE:
                config->transparentAlpha = value;
                break;
            case GLX_MAX_PBUFFER_WIDTH:
                config->maxPbufferWidth = value;
                break;
            case GLX_MAX_PBUFFER_HEIGHT:
                config->maxPbufferHeight = value;
                break;
            case GLX_MAX_PBUFFER_PIXELS:
                config->maxPbufferPixels = value;
                break;
            case GLX_VISUAL_ID:
                config->associatedVisualId = value;
                break;

                /* visualSelectGroup is an internal used property */
            case GLX_VISUAL_SELECT_GROUP_SGIX:
                config->visualSelectGroup = value;
                break;

                /* SGIS_multisample attributes */
            case GLX_SAMPLES_SGIS:
                config->multiSampleSize = value;
                break;
            case GLX_SAMPLE_BUFFERS_SGIS:
                config->nMultiSampleBuffers = value;
                break;

                /* SGIX_pbuffer specific attributes */
            case GLX_OPTIMAL_PBUFFER_WIDTH_SGIX:
                config->optimalPbufferWidth = value;
                break;
            case GLX_OPTIMAL_PBUFFER_HEIGHT_SGIX:
                config->optimalPbufferHeight = value;
                break;

            default:
                /* Ignore attributes we don't recognize */
                break;
            }
        }                       /* for j */

        /* Fill in derived values */
        config->screen = screen;

        /* The rgbMode should be true for any mode which has distinguishible
         * R, G and B components
         */
        config->rgbMode = (config->renderType
                           & (GLX_RGBA_BIT | GLX_RGBA_FLOAT_BIT_ARB
                              | GLX_RGBA_UNSIGNED_FLOAT_BIT_EXT)) != 0;
        config->colorIndexMode = !config->rgbMode;

        config->haveAccumBuffer =
            config->accumRedBits > 0 ||
            config->accumGreenBits > 0 || config->accumBlueBits > 0;
        /* Can't have alpha without color */

        config->haveDepthBuffer = config->depthBits > 0;
        config->haveStencilBuffer = config->stencilBits > 0;

        /* overlay visuals are not valid for now */
        if (!config->level) {
            config++;
            numValidConfigs++;
        }

    }                           /* for i */
    UnlockDisplay(dpy);

    config = fbconfigs;
    for (i = 0; i < numValidConfigs; i++) {

        /* XXX hack to fill-in mask info (need a better way to do this) */
        if (config->associatedVisualId != 0) {
            XVisualInfo *vis, template;
            int n;

            template.screen = screen;
            template.visualid = config->associatedVisualId;
            vis = XGetVisualInfo(dpy, VisualScreenMask | VisualIDMask,
                                 &template, &n);

            if (vis != NULL) {
                config->redMask = (GLuint) vis->red_mask;
                config->greenMask = (GLuint) vis->green_mask;
                config->blueMask = (GLuint) vis->blue_mask;
                config->alphaMask = 0;  /* XXX */
                free(vis);
            }
        }

        config++;
    }                           /* for i */

    XFree(attrs);
    SyncHandle();

    *nconfigs = numValidConfigs;
    return fbconfigs;
}

__GLXvisualConfig *
GetGLXVisualConfigsFromFBConfigs(__GLXFBConfig * fbconfigs, int nfbconfigs,
                                 XVisualInfo * visuals, int nvisuals,
                                 __GLXvisualConfig * glxConfigs,
                                 int nGlxConfigs, int *nconfigs)
{
    __GLXvisualConfig *configs = NULL;
    int i;

    if (!fbconfigs || !nfbconfigs || !nconfigs)
        return NULL;
    *nconfigs = 0;

    /* Allocate memory for our config structure */
    configs = (__GLXvisualConfig *)
        Xmalloc(nfbconfigs * sizeof(__GLXvisualConfig));
    if (!configs) {
        return NULL;
    }
    memset(configs, 0, nfbconfigs * sizeof(__GLXvisualConfig));

    for (i = 0; i < nfbconfigs; i++) {
        __GLXFBConfig *fbcfg = &fbconfigs[i];

        if (fbcfg->associatedVisualId > 0) {
            __GLXvisualConfig *cfg = configs + (*nconfigs);
            int j;
            XVisualInfo *vinfo = NULL;

            for (j = 0; j < nvisuals; j++) {
                if (visuals[j].visualid == fbcfg->associatedVisualId) {
                    vinfo = &visuals[j];
                    break;
                }
            }
            if (!vinfo)
                continue;

            /* skip 16 bit colormap visuals */
            if (vinfo->depth == 16 &&
                vinfo->class != TrueColor && vinfo->class != DirectColor) {
                continue;
            }

            (*nconfigs)++;

            /*
             * if the same visualid exists in the glx configs,
             * copy the glx attributes from the glx config
             */
            for (j = 0; j < nGlxConfigs; j++) {
                if (glxConfigs[j].vid == vinfo->visualid)
                    break;
            }
            if (j < nGlxConfigs) {
                memcpy(cfg, &glxConfigs[j], sizeof(__GLXvisualConfig));
                continue;
            }

            /*
             * make glx attributes from the FB config attributes
             */
            cfg->vid = fbcfg->associatedVisualId;
            cfg->class = vinfo->class;
            cfg->rgba = !(fbcfg->renderType & GLX_COLOR_INDEX_BIT_SGIX);
            cfg->redSize = fbcfg->redBits;
            cfg->greenSize = fbcfg->greenBits;
            cfg->blueSize = fbcfg->blueBits;
            cfg->alphaSize = fbcfg->alphaBits;
            cfg->redMask = fbcfg->redMask;
            cfg->greenMask = fbcfg->greenMask;
            cfg->blueMask = fbcfg->blueMask;
            cfg->alphaMask = fbcfg->alphaMask;
            cfg->accumRedSize = fbcfg->accumRedBits;
            cfg->accumGreenSize = fbcfg->accumGreenBits;
            cfg->accumBlueSize = fbcfg->accumBlueBits;
            cfg->accumAlphaSize = fbcfg->accumAlphaBits;
            cfg->doubleBuffer = fbcfg->doubleBufferMode;
            cfg->stereo = fbcfg->stereoMode;
            if (vinfo->class == TrueColor || vinfo->class == DirectColor) {
                cfg->bufferSize = (fbcfg->rgbMode ? (fbcfg->redBits +
                                                     fbcfg->greenBits +
                                                     fbcfg->blueBits +
                                                     fbcfg->alphaBits)
                                   : fbcfg->indexBits);
            }
            else {
                cfg->bufferSize = vinfo->depth;
            }
            cfg->depthSize = fbcfg->depthBits;
            cfg->stencilSize = fbcfg->stencilBits;
            cfg->auxBuffers = fbcfg->maxAuxBuffers;
            cfg->level = fbcfg->level;
            cfg->visualRating = fbcfg->visualCaveat;
            cfg->transparentPixel = fbcfg->transparentType;
            cfg->transparentRed = fbcfg->transparentRed;
            cfg->transparentGreen = fbcfg->transparentGreen;
            cfg->transparentBlue = fbcfg->transparentBlue;
            cfg->transparentAlpha = fbcfg->transparentAlpha;
            cfg->transparentIndex = fbcfg->transparentIndex;
            cfg->multiSampleSize = fbcfg->multiSampleSize;
            cfg->nMultiSampleBuffers = fbcfg->nMultiSampleBuffers;
            cfg->visualSelectGroup = fbcfg->visualSelectGroup;
        }
    }

    return configs;
}
