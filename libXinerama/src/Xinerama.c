/*****************************************************************
Copyright (c) 1991, 1997 Digital Equipment Corporation, Maynard, Massachusetts.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
DIGITAL EQUIPMENT CORPORATION BE LIABLE FOR ANY CLAIM, DAMAGES, INCLUDING,
BUT NOT LIMITED TO CONSEQUENTIAL OR INCIDENTAL DAMAGES, OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Digital Equipment Corporation
shall not be used in advertising or otherwise to promote the sale, use or other
dealings in this Software without prior written authorization from Digital
Equipment Corporation.
******************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <X11/Xlibint.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/panoramiXext.h>
#include <X11/extensions/panoramiXproto.h>
#include <X11/extensions/Xinerama.h>

#ifndef HAVE__XEATDATAWORDS
#include <X11/Xmd.h>  /* for LONG64 on 64-bit platforms */
#include <limits.h>

static inline void _XEatDataWords(Display *dpy, unsigned long n)
{
# ifndef LONG64
    if (n >= (ULONG_MAX >> 2))
        _XIOError(dpy);
# endif
    _XEatData (dpy, n << 2);
}
#endif

static XExtensionInfo _panoramiX_ext_info_data;
static XExtensionInfo *panoramiX_ext_info = &_panoramiX_ext_info_data;
static const char *panoramiX_extension_name = PANORAMIX_PROTOCOL_NAME;

#define PanoramiXCheckExtension(dpy,i,val) \
  XextCheckExtension (dpy, i, panoramiX_extension_name, val)
#define PanoramiXSimpleCheckExtension(dpy,i) \
  XextSimpleCheckExtension (dpy, i, panoramiX_extension_name)

static int close_display(Display *dpy, XExtCodes *codes);

static /* const */ XExtensionHooks panoramiX_extension_hooks = {
    NULL,				/* create_gc */
    NULL,				/* copy_gc */
    NULL,				/* flush_gc */
    NULL,				/* free_gc */
    NULL,				/* create_font */
    NULL,				/* free_font */
    close_display,			/* close_display */
    NULL,				/* wire_to_event */
    NULL,				/* event_to_wire */
    NULL,				/* error */
    NULL,				/* error_string */
};

static XEXT_GENERATE_FIND_DISPLAY (find_display, panoramiX_ext_info,
				   panoramiX_extension_name,
				   &panoramiX_extension_hooks,
				   0, NULL)

static XEXT_GENERATE_CLOSE_DISPLAY (close_display, panoramiX_ext_info)



/****************************************************************************
 *                                                                          *
 *			    PanoramiX public interfaces                         *
 *                                                                          *
 ****************************************************************************/

Bool XPanoramiXQueryExtension (
    Display *dpy,
    int *event_base_return,
    int *error_base_return
)
{
    XExtDisplayInfo *info = find_display (dpy);

    if (XextHasExtension(info)) {
	*event_base_return = info->codes->first_event;
	*error_base_return = info->codes->first_error;
	return True;
    } else {
	return False;
    }
}


Status XPanoramiXQueryVersion(
    Display *dpy,
    int     *major_version_return,
    int     *minor_version_return
)
{
    XExtDisplayInfo *info = find_display (dpy);
    xPanoramiXQueryVersionReply	    rep;
    register xPanoramiXQueryVersionReq  *req;

    PanoramiXCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    GetReq (PanoramiXQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->panoramiXReqType = X_PanoramiXQueryVersion;
    req->clientMajor = PANORAMIX_MAJOR_VERSION;
    req->clientMinor = PANORAMIX_MINOR_VERSION;
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    *major_version_return = rep.majorVersion;
    *minor_version_return = rep.minorVersion;
    UnlockDisplay (dpy);
    SyncHandle ();
    return 1;
}

XPanoramiXInfo *XPanoramiXAllocInfo(void)
{
	return (XPanoramiXInfo *) Xmalloc (sizeof (XPanoramiXInfo));
}

Status XPanoramiXGetState (
    Display		*dpy,
    Drawable		drawable,
    XPanoramiXInfo	*panoramiX_info
)
{
    XExtDisplayInfo			*info = find_display (dpy);
    xPanoramiXGetStateReply	rep;
    register xPanoramiXGetStateReq	*req;

    PanoramiXCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    GetReq (PanoramiXGetState, req);
    req->reqType = info->codes->major_opcode;
    req->panoramiXReqType = X_PanoramiXGetState;
    req->window = drawable;
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    UnlockDisplay (dpy);
    SyncHandle ();
    panoramiX_info->window = rep.window;
    panoramiX_info->State = rep.state;
    return 1;
}

Status XPanoramiXGetScreenCount (
    Display		*dpy,
    Drawable		drawable,
    XPanoramiXInfo	*panoramiX_info
)
{
    XExtDisplayInfo			*info = find_display (dpy);
    xPanoramiXGetScreenCountReply	rep;
    register xPanoramiXGetScreenCountReq	*req;

    PanoramiXCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    GetReq (PanoramiXGetScreenCount, req);
    req->reqType = info->codes->major_opcode;
    req->panoramiXReqType = X_PanoramiXGetScreenCount;
    req->window = drawable;
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    UnlockDisplay (dpy);
    SyncHandle ();
    panoramiX_info->window = rep.window;
    panoramiX_info->ScreenCount = rep.ScreenCount;
    return 1;
}

Status XPanoramiXGetScreenSize (
    Display		*dpy,
    Drawable		drawable,
    int			screen_num,
    XPanoramiXInfo	*panoramiX_info
)
{
    XExtDisplayInfo			*info = find_display (dpy);
    xPanoramiXGetScreenSizeReply	rep;
    register xPanoramiXGetScreenSizeReq	*req;

    PanoramiXCheckExtension (dpy, info, 0);

    LockDisplay (dpy);
    GetReq (PanoramiXGetScreenSize, req);
    req->reqType = info->codes->major_opcode;
    req->panoramiXReqType = X_PanoramiXGetScreenSize;
    req->window = drawable;
    req->screen = screen_num;			/* need to define */
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    UnlockDisplay (dpy);
    SyncHandle ();
    panoramiX_info->window = rep.window;
    panoramiX_info->screen = rep.screen;
    panoramiX_info->width =  rep.width;
    panoramiX_info->height = rep.height;
    return 1;
}

/*******************************************************************\
  Alternate interface to make up for shortcomings in the original,
  namely, the omission of the screen origin.  The new interface is
  in the "Xinerama" namespace instead of "PanoramiX".
\*******************************************************************/

Bool XineramaQueryExtension (
   Display *dpy,
   int     *event_base_return,
   int     *error_base_return
)
{
   return XPanoramiXQueryExtension(dpy, event_base_return, error_base_return);
}

Status XineramaQueryVersion(
   Display *dpy,
   int     *major,
   int     *minor
)
{
   return XPanoramiXQueryVersion(dpy, major, minor);
}

Bool XineramaIsActive(Display *dpy)
{
    xXineramaIsActiveReply	rep;
    xXineramaIsActiveReq  	*req;
    XExtDisplayInfo 		*info = find_display (dpy);

    if(!XextHasExtension(info))
	return False;  /* server doesn't even have the extension */

    LockDisplay (dpy);
    GetReq (XineramaIsActive, req);
    req->reqType = info->codes->major_opcode;
    req->panoramiXReqType = X_XineramaIsActive;
    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return False;
    }
    UnlockDisplay (dpy);
    SyncHandle ();
    return rep.state;
}

XineramaScreenInfo *
XineramaQueryScreens(
   Display *dpy,
   int     *number
)
{
    XExtDisplayInfo		*info = find_display (dpy);
    xXineramaQueryScreensReply	rep;
    xXineramaQueryScreensReq	*req;
    XineramaScreenInfo		*scrnInfo = NULL;

    PanoramiXCheckExtension (dpy, info, NULL);

    LockDisplay (dpy);
    GetReq (XineramaQueryScreens, req);
    req->reqType = info->codes->major_opcode;
    req->panoramiXReqType = X_XineramaQueryScreens;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }

    /*
     * rep.number is a CARD32 so could be as large as 2^32
     * The X11 protocol limits the total screen size to 64k x 64k,
     * and no screen can be smaller than a pixel.  While technically
     * that means we could theoretically reach 2^32 screens, and that's
     * not even taking overlap into account, Xorg is currently limited
     * to 16 screens, and few known servers have a much higher limit,
     * so 1024 seems more than enough to prevent both integer overflow
     * and insane X server responses causing massive memory allocation.
     */
    if ((rep.number > 0) && (rep.number <= 1024))
	scrnInfo = Xmalloc(sizeof(XineramaScreenInfo) * rep.number);
    if (scrnInfo != NULL) {
	int i;

	for (i = 0; i < rep.number; i++) {
	    xXineramaScreenInfo scratch;

	    _XRead(dpy, (char*)(&scratch), sz_XineramaScreenInfo);

	    scrnInfo[i].screen_number = i;
	    scrnInfo[i].x_org	= scratch.x_org;
	    scrnInfo[i].y_org	= scratch.y_org;
	    scrnInfo[i].width	= scratch.width;
	    scrnInfo[i].height	= scratch.height;
	}

	*number = rep.number;
    } else {
	_XEatDataWords(dpy, rep.length);
	*number = 0;
    }

    UnlockDisplay (dpy);
    SyncHandle ();
    return scrnInfo;
}
