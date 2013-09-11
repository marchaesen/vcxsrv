/*

Copyright 1996, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef WIN32
#include <X11/Xwindows.h>
#endif

#include <X11/Xlibint.h>
#include <X11/extensions/Xag.h>
#include <X11/extensions/agproto.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>

#include <stdarg.h>

struct xagstuff {
    int attrib_mask;
    Bool app_group_leader;
    Bool single_screen;
    Window default_root;
    VisualID root_visual;
    Colormap default_colormap;
    unsigned long black_pixel;
    unsigned long white_pixel;
};

static XExtensionInfo _xag_info_data;
static XExtensionInfo *xag_info = &_xag_info_data;
static const char *xag_extension_name = XAGNAME;

#define XagCheckExtension(dpy,i,val) \
  XextCheckExtension (dpy, i, xag_extension_name, val)

/*****************************************************************************
 *                                                                           *
 *			   private utility routines                          *
 *                                                                           *
 *****************************************************************************/

static int close_display(Display *dpy, XExtCodes *codes);
static /* const */ XExtensionHooks xag_extension_hooks = {
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

static XEXT_GENERATE_FIND_DISPLAY (find_display, xag_info,
				   xag_extension_name,
				   &xag_extension_hooks,
				   0, NULL)

static XEXT_GENERATE_CLOSE_DISPLAY (close_display, xag_info)


/*****************************************************************************
 *                                                                           *
 *		    public Xag Extension routines                            *
 *                                                                           *
 *****************************************************************************/

Bool
XagQueryVersion(
    Display *dpy,
    int *major_version_return,
    int *minor_version_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    xXagQueryVersionReply rep;
    xXagQueryVersionReq *req;

    XagCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(XagQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->xagReqType = X_XagQueryVersion;
    req->client_major_version = XAG_MAJOR_VERSION;
    req->client_minor_version = XAG_MINOR_VERSION;
    if (!_XReply(dpy, (xReply *)&rep, 0, xTrue)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return False;
    }
    *major_version_return = rep.server_major_version;
    *minor_version_return = rep.server_minor_version;
    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}

static void
StuffToWire (Display *dpy, struct xagstuff *stuff, xXagCreateReq *req)
{
    unsigned long values[8];
    unsigned long* value = values;
    unsigned int nvalues;

    /* the order these are in is important */
    if (stuff->attrib_mask & XagSingleScreenMask)
	*value++ = stuff->single_screen;

    if (stuff->attrib_mask & XagDefaultRootMask)
	*value++ = stuff->default_root;

    if (stuff->attrib_mask & XagRootVisualMask)
	*value++ = stuff->root_visual;

    if (stuff->attrib_mask & XagDefaultColormapMask)
	*value++ = stuff->default_colormap;

    if (stuff->attrib_mask & XagBlackPixelMask)
	*value++ = stuff->black_pixel;

    if (stuff->attrib_mask & XagWhitePixelMask)
	*value++ = stuff->white_pixel;

    if (stuff->attrib_mask & XagAppGroupLeaderMask)
	*value++ = stuff->app_group_leader;

    req->length += (nvalues = value - values);

    nvalues <<= 2;
    Data32 (dpy, (long*) values, (long) nvalues);
}

Bool
XagCreateEmbeddedApplicationGroup(
    Display* dpy,
    VisualID root_visual,
    Colormap default_colormap,
    unsigned long black_pixel,
    unsigned long white_pixel,
    XAppGroup* app_group_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    xXagCreateReq *req;
    struct xagstuff stuff;

    XagCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    stuff.app_group_leader = True;
    stuff.single_screen = True;
    stuff.default_root = RootWindow (dpy, DefaultScreen(dpy));
    stuff.root_visual = root_visual;
    stuff.default_colormap = default_colormap;
    stuff.attrib_mask =
	XagAppGroupLeaderMask | XagSingleScreenMask | XagDefaultRootMask |
	XagRootVisualMask | XagDefaultColormapMask;
    if (default_colormap != None) {
	stuff.black_pixel = black_pixel;
	stuff.white_pixel = white_pixel;
	stuff.attrib_mask |= XagBlackPixelMask | XagWhitePixelMask;
    }
    /* might do some validation here */
    GetReq(XagCreate, req);
    req->reqType = info->codes->major_opcode;
    req->xagReqType = X_XagCreate;
    *app_group_return = req->app_group = XAllocID(dpy);
    req->attrib_mask = stuff.attrib_mask;
    StuffToWire (dpy, &stuff, req);
    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}

Bool
XagCreateNonembeddedApplicationGroup(
    Display* dpy,
    XAppGroup* app_group_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    xXagCreateReq *req;
    struct xagstuff stuff;

    XagCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    stuff.app_group_leader = False;
    stuff.single_screen = False;
    stuff.attrib_mask = XagAppGroupLeaderMask | XagSingleScreenMask;
    /* might do some validation here */
    GetReq(XagCreate, req);
    req->reqType = info->codes->major_opcode;
    req->xagReqType = X_XagCreate;
    *app_group_return = req->app_group = XAllocID(dpy);
    req->attrib_mask = stuff.attrib_mask;
    StuffToWire (dpy, &stuff, req);
    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}

Bool XagDestroyApplicationGroup(Display* dpy, XAppGroup app_group)
{
    XExtDisplayInfo *info = find_display (dpy);
    xXagDestroyReq *req;

    XagCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(XagDestroy, req);
    req->reqType = info->codes->major_opcode;
    req->xagReqType = X_XagDestroy;
    req->app_group = app_group;
    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}

Bool
XagGetApplicationGroupAttributes(Display* dpy, XAppGroup app_group, ...)
{
    va_list var;
    XExtDisplayInfo *info = find_display (dpy);
    xXagGetAttrReq *req;
    xXagGetAttrReply rep;
    int attr;

    XagCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(XagGetAttr, req);
    req->reqType = info->codes->major_opcode;
    req->xagReqType = X_XagGetAttr;
    req->app_group = app_group;
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return False;
    }
    va_start (var, app_group);
    for (attr = va_arg(var, int); attr != 0; attr = va_arg(var, int)) {
	void* ptr;

	switch (attr) {
	case XagNappGroupLeader:
	    ptr = va_arg(var, void*);
	    *(Bool*)ptr = rep.app_group_leader;
	    break;
	case XagNsingleScreen:
	    ptr = va_arg(var, void*);
	    *(Bool*)ptr = rep.single_screen;
	    break;
	case XagNdefaultRoot:
	    ptr = va_arg(var, void*);
	    *(Window*)ptr = rep.default_root;
	    break;
	case XagNrootVisual:
	    ptr = va_arg(var, void*);
	    *(VisualID*)ptr = rep.root_visual;
	    break;
	case XagNdefaultColormap:
	    ptr = va_arg(var, void*);
	    *(Colormap*)ptr = rep.default_colormap;
	    break;
	case XagNblackPixel:
	    ptr = va_arg(var, void*);
	    *(unsigned long*)ptr = rep.black_pixel;
	    break;
	case XagNwhitePixel:
	    ptr = va_arg(var, void*);
	    *(unsigned long*)ptr = rep.white_pixel;
	    break;
	}
    }
    va_end (var);
    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}

Bool
XagQueryApplicationGroup(
    Display* dpy,
    XID resource,
    XAppGroup* app_group_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    xXagQueryReq *req;
    xXagQueryReply rep;

    XagCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(XagQuery, req);
    req->reqType = info->codes->major_opcode;
    req->xagReqType = X_XagQuery;
    req->resource = resource;
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return False;
    }
    *app_group_return = rep.app_group;
    UnlockDisplay(dpy);
    SyncHandle();
    return True;

}

Bool
XagCreateAssociation(Display* dpy, Window* window_return, void* system_window)
{
#ifdef WIN32
    HWND tmp = *(HWND*) system_window;
    XExtDisplayInfo *info = find_display (dpy);
    xXagCreateAssocReq *req;

    XagCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(XagCreateAssoc, req);
    req->reqType = info->codes->major_opcode;
    req->xagReqType = X_XagCreateAssoc;
    *window_return = req->window = XAllocID(dpy);
    req->window_type = XagWindowTypeWin32;
    req->system_window_len = sizeof(HWND);
    Data32 (dpy, (HWND*) tmp, sizeof(HWND)/4);
    req->length++;
    UnlockDisplay(dpy);
    SyncHandle();
#else
    /* other platforms go here */

    /* this whole thing could be arranged better, but since X need
     * only short-circuit the protocol and WIN32 is the only other
     * platform the XC supports, it will suffice for now.
     */
    *window_return = *(Window*)system_window;
#endif
    return True;
}

Bool
XagDestroyAssociation(Display* dpy, Window window)
{
#ifdef WIN32
    XExtDisplayInfo *info = find_display (dpy);
    xXagDestroyAssocReq *req;

    XagCheckExtension (dpy, info, False);

    LockDisplay(dpy);
    GetReq(XagDestroyAssoc, req);
    req->reqType = info->codes->major_opcode;
    req->xagReqType = X_XagDestroyAssoc;
    req->window = window;
    UnlockDisplay(dpy);
    SyncHandle();
#endif
    return True;
}

