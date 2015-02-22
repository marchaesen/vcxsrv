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
#include "dmxlog.h"

#include "glxserver.h"

#include <windowstr.h>

#include "glxfbconfig.h"

#ifdef PANORAMIX
#include "panoramiXsrv.h"
#endif

__GLXscreenInfo *__glXActiveScreens;
GLint __glXNumActiveScreens;

__GLXFBConfig **__glXFBConfigs;
int __glXNumFBConfigs;

static char GLXServerVendorName[] = "SGI DMX/glxProxy";
static char GLXServerVersion[64];
static char GLXServerExtensions[] =
    "GLX_EXT_visual_info "
    "GLX_EXT_visual_rating "
    "GLX_EXT_import_context "
    "GLX_SGIX_fbconfig " "GLX_SGI_make_current_read " "GLX_SGI_swap_control ";

static char ExtensionsString[1024];

static void
CalcServerVersionAndExtensions(void)
{
    int s;
    char **be_extensions;
    char *ext;
    char *denied_extensions;

    /*
     * set the server glx version to be the minimum version
     * supported by all back-end servers
     */
    __glXVersionMajor = 0;
    __glXVersionMinor = 0;
    for (s = 0; s < __glXNumActiveScreens; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = dmxScreen->beDisplay;
        xGLXQueryVersionReq *req;
        xGLXQueryVersionReply reply;

        /* Send the glXQueryVersion request */
        LockDisplay(dpy);
        GetReq(GLXQueryVersion, req);
        req->reqType = dmxScreen->glxMajorOpcode;
        req->glxCode = X_GLXQueryVersion;
        req->majorVersion = GLX_SERVER_MAJOR_VERSION;
        req->minorVersion = GLX_SERVER_MINOR_VERSION;
        _XReply(dpy, (xReply *) &reply, 0, False);
        UnlockDisplay(dpy);
        SyncHandle();

        if (s == 0) {
            __glXVersionMajor = reply.majorVersion;
            __glXVersionMinor = reply.minorVersion;
        }
        else {
            if (reply.majorVersion < __glXVersionMajor) {
                __glXVersionMajor = reply.majorVersion;
                __glXVersionMinor = reply.minorVersion;
            }
            else if ((reply.majorVersion == __glXVersionMajor) &&
                     (reply.minorVersion < __glXVersionMinor)) {
                __glXVersionMinor = reply.minorVersion;
            }
        }

    }

    if (GLX_SERVER_MAJOR_VERSION < __glXVersionMajor) {
        __glXVersionMajor = GLX_SERVER_MAJOR_VERSION;
        __glXVersionMinor = GLX_SERVER_MINOR_VERSION;
    }
    else if ((GLX_SERVER_MAJOR_VERSION == __glXVersionMajor) &&
             (GLX_SERVER_MINOR_VERSION < __glXVersionMinor)) {
        __glXVersionMinor = GLX_SERVER_MINOR_VERSION;
    }

    snprintf(GLXServerVersion, sizeof(GLXServerVersion),
             "%d.%d DMX %d back-end server(s)",
             __glXVersionMajor, __glXVersionMinor, __glXNumActiveScreens);
    /*
     * set the ExtensionsString to the minimum extensions string
     */
    ExtensionsString[0] = '\0';

    /*
     * read extensions strings of all back-end servers
     */
    be_extensions = (char **) malloc(__glXNumActiveScreens * sizeof(char *));
    if (!be_extensions)
        return;

    for (s = 0; s < __glXNumActiveScreens; s++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[s];
        Display *dpy = dmxScreen->beDisplay;
        xGLXQueryServerStringReq *req;
        xGLXQueryServerStringReply reply;
        int length, numbytes;

        /* Send the glXQueryServerString request */
        LockDisplay(dpy);
        GetReq(GLXQueryServerString, req);
        req->reqType = dmxScreen->glxMajorOpcode;
        req->glxCode = X_GLXQueryServerString;
        req->screen = DefaultScreen(dpy);
        req->name = GLX_EXTENSIONS;
        _XReply(dpy, (xReply *) &reply, 0, False);

        length = (int) reply.length;
        numbytes = (int) reply.n;
        be_extensions[s] = (char *) malloc(numbytes);
        if (!be_extensions[s]) {
            /* Throw data on the floor */
            _XEatDataWords(dpy, length);
        }
        else {
            _XReadPad(dpy, (char *) be_extensions[s], numbytes);
        }
        UnlockDisplay(dpy);
        SyncHandle();
    }

    /*
     * extensions string will include only extensions that our
     * server supports as well as all back-end servers supports.
     * extensions that are in the DMX_DENY_EXTENSIONS string will
     * not be supported.
     */
    denied_extensions = getenv("DMX_DENY_GLX_EXTENSIONS");
    ext = strtok(GLXServerExtensions, " ");
    while (ext) {
        int supported = 1;

        if (denied_extensions && strstr(denied_extensions, ext)) {
            supported = 0;
        }
        else {
            for (s = 0; s < __glXNumActiveScreens && supported; s++) {
                if (!strstr(be_extensions[s], ext)) {
                    supported = 0;
                }
            }
        }

        if (supported) {
            strcat(ExtensionsString, ext);
            strcat(ExtensionsString, " ");
        }

        ext = strtok(NULL, " ");
    }

    /*
     * release temporary storage
     */
    for (s = 0; s < __glXNumActiveScreens; s++) {
        free(be_extensions[s]);
    }
    free(be_extensions);

    if (dmxGLXSwapGroupSupport) {
        if (!denied_extensions ||
            !strstr(denied_extensions, "GLX_SGIX_swap_group")) {
            strcat(ExtensionsString, "GLX_SGIX_swap_group");
            if (!denied_extensions ||
                !strstr(denied_extensions, "GLX_SGIX_swap_barrier")) {
                strcat(ExtensionsString, " GLX_SGIX_swap_barrier");
            }
        }
    }

}

void
__glXScreenInit(GLint numscreens)
{
    int s;
    int c;
    DMXScreenInfo *dmxScreen0 = &dmxScreens[0];

    __glXNumActiveScreens = numscreens;

    CalcServerVersionAndExtensions();

    __glXFBConfigs = NULL;
    __glXNumFBConfigs = 0;

    if ((__glXVersionMajor == 1 && __glXVersionMinor >= 3) ||
        (__glXVersionMajor > 1) ||
        (strstr(ExtensionsString, "GLX_SGIX_fbconfig"))) {

        /*
           // Initialize FBConfig info.
           // find the set of FBConfigs that are present on all back-end
           // servers - only those configs will be supported
         */
        __glXFBConfigs = (__GLXFBConfig **) malloc(dmxScreen0->numFBConfigs *
                                                   (numscreens +
                                                    1) *
                                                   sizeof(__GLXFBConfig *));
        __glXNumFBConfigs = 0;

        for (c = 0; c < dmxScreen0->numFBConfigs; c++) {
            __GLXFBConfig *cfg = NULL;

            if (numscreens > 1) {
                for (s = 1; s < numscreens; s++) {
                    DMXScreenInfo *dmxScreen = &dmxScreens[s];

                    cfg = FindMatchingFBConfig(&dmxScreen0->fbconfigs[c],
                                               dmxScreen->fbconfigs,
                                               dmxScreen->numFBConfigs);
                    __glXFBConfigs[__glXNumFBConfigs * (numscreens + 1) + s +
                                   1] = cfg;
                    if (!cfg) {
                        dmxLog(dmxInfo,
                               "screen0 FBConfig 0x%x is missing on screen#%d\n",
                               dmxScreen0->fbconfigs[c].id, s);
                        break;
                    }
                    else {
                        dmxLog(dmxInfo,
                               "screen0 FBConfig 0x%x matched to  0x%x on screen#%d\n",
                               dmxScreen0->fbconfigs[c].id, cfg->id, s);
                    }
                }
            }
            else {
                cfg = &dmxScreen0->fbconfigs[c];
            }

            if (cfg) {

                /* filter out overlay visuals */
                if (cfg->level == 0) {
                    __GLXFBConfig *proxy_cfg;

                    __glXFBConfigs[__glXNumFBConfigs * (numscreens + 1) + 1] =
                        &dmxScreen0->fbconfigs[c];

                    proxy_cfg = malloc(sizeof(__GLXFBConfig));
                    memcpy(proxy_cfg, cfg, sizeof(__GLXFBConfig));
                    proxy_cfg->id = FakeClientID(0);
                    /* visual will be associated later in __glXGetFBConfigs */
                    proxy_cfg->associatedVisualId = (unsigned int) -1;

                    __glXFBConfigs[__glXNumFBConfigs * (numscreens + 1) + 0] =
                        proxy_cfg;

                    __glXNumFBConfigs++;
                }

            }

        }

    }

}

void
__glXScreenReset(void)
{
    __glXNumActiveScreens = 0;
}

char *
__glXGetServerString(unsigned int name)
{
    char *ret = NULL;

    switch (name) {

    case GLX_VENDOR:
        ret = GLXServerVendorName;
        break;

    case GLX_VERSION:
        ret = GLXServerVersion;
        break;

    case GLX_EXTENSIONS:
        ret = ExtensionsString;
        break;

    default:
        break;
    }

    return ret;

}

int
glxIsExtensionSupported(const char *ext)
{
    return (strstr(ExtensionsString, ext) != NULL);
}
