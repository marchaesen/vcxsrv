/**************************************************************************

Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
All Rights Reserved.

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

/*
 * Authors:
 *   Kevin E. Martin <kevin@precisioninsight.com>
 *   Rickard E. Faith <faith@precisioninsight.com>
 *
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86Module.h"
#include "globals.h"

#include "xf86drm.h"
static MODULESETUPPROTO(driSetup);

drmServerInfo DRIDRMServerInfo;

static XF86ModuleVersionInfo VersRec = {
    "dri",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    1, 0, 0,
    ABI_CLASS_EXTENSION,
    ABI_EXTENSION_VERSION,
    MOD_CLASS_NONE,
    {0, 0, 0, 0}
};

extern void XFree86DRIExtensionInit(INITARGS);

#define _XF86DRI_SERVER_
#include <X11/dri/xf86driproto.h>

static ExtensionModule XF86DRIExt = {
    XFree86DRIExtensionInit,
    XF86DRINAME,
    &noXFree86DRIExtension,
    NULL,
    NULL
};

_X_EXPORT XF86ModuleData driModuleData = { &VersRec, driSetup, NULL };

static pointer
driSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    if (!setupDone) {
        setupDone = TRUE;
        LoadExtension(&XF86DRIExt, FALSE);
    }
    else {
        if (errmaj)
            *errmaj = LDR_ONCEONLY;
    }

    drmSetServerInfo(&DRIDRMServerInfo);

    /* Need a non-NULL return value to indicate success */
    return (pointer) 1;
}
