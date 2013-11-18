/*

Copyright 1987, 1988, 1998  The Open Group

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
#include <X11/extensions/Xcup.h>
#include <X11/extensions/cupproto.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <limits.h>

static XExtensionInfo _xcup_info_data;
static XExtensionInfo *xcup_info = &_xcup_info_data;
static const char *xcup_extension_name = XCUPNAME;

/*****************************************************************************
 *                                                                           *
 *			   private utility routines                          *
 *                                                                           *
 *****************************************************************************/

static int close_display(Display *dpy, XExtCodes *codes);
static /* const */ XExtensionHooks xcup_extension_hooks = {
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

static XEXT_GENERATE_FIND_DISPLAY (find_display, xcup_info,
				   xcup_extension_name,
				   &xcup_extension_hooks,
				   0, NULL)

static XEXT_GENERATE_CLOSE_DISPLAY (close_display, xcup_info)


/*****************************************************************************
 *                                                                           *
 *		    public Xcup Extension routines                           *
 *                                                                           *
 *****************************************************************************/

Status
XcupQueryVersion(
    Display* dpy,
    int* major_version_return,
    int* minor_version_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    xXcupQueryVersionReply rep;
    xXcupQueryVersionReq *req;

    XextCheckExtension (dpy, info, xcup_extension_name, False);

    LockDisplay(dpy);
    GetReq(XcupQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->xcupReqType = X_XcupQueryVersion;
    req->client_major_version = XCUP_MAJOR_VERSION;
    req->client_minor_version = XCUP_MINOR_VERSION;
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

/* Win32 reserves 20 colormap entries for its desktop */
#ifndef TYP_RESERVED_ENTRIES
#define TYP_RESERVED_ENTRIES 20
#endif

Status
XcupGetReservedColormapEntries(
    Display* dpy,
    int screen,
    XColor** colors_out,
    int* ncolors)
{
    XExtDisplayInfo *info = find_display (dpy);
    xXcupGetReservedColormapEntriesReply rep;
    xXcupGetReservedColormapEntriesReq *req;
    xColorItem rbuf[TYP_RESERVED_ENTRIES];

    *ncolors = 0;

    XextCheckExtension (dpy, info, xcup_extension_name, False);

    LockDisplay(dpy);
    GetReq(XcupGetReservedColormapEntries, req);
    req->reqType = info->codes->major_opcode;
    req->xcupReqType = X_XcupGetReservedColormapEntries;
    req->screen = screen;
    if (_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	unsigned long nbytes;
	xColorItem* rbufp;
	unsigned int nentries = rep.length / 3;

	if (nentries < (INT_MAX / SIZEOF (xColorItem))) {
	    nbytes = nentries * SIZEOF (xColorItem);

	    if (nentries > TYP_RESERVED_ENTRIES)
		rbufp = Xmalloc (nbytes);
	    else
		rbufp = rbuf;
	} else
	    rbufp = NULL;

	if (rbufp == NULL) {
	    _XEatDataWords(dpy, rep.length);
	    UnlockDisplay (dpy);
	    SyncHandle ();
	    return False;
	}
	_XRead (dpy, (char*) rbufp, nbytes);

	*colors_out = (XColor*) Xmalloc (nentries  * sizeof (XColor));
	if (*colors_out) {
	    xColorItem* cs = (xColorItem *) rbufp;
	    XColor* cd = *colors_out;
	    int i;

	    *ncolors = nentries;
	    for (i = 0; i < *ncolors; i++, cd++) {
		cd->pixel = cs->pixel;
		cd->red = cs->red;
		cd->green = cs->green;
		cd->blue = cs->blue;
		cs = (xColorItem*) (((char*) cs) + SIZEOF(xColorItem));
	    }
	    if (rbufp != rbuf) XFree ((char*) rbufp);
	    UnlockDisplay(dpy);
    	    SyncHandle();
	    return True;
	}
	if (rbufp != rbuf) XFree ((char*) rbufp);
    }
    UnlockDisplay(dpy);
    SyncHandle();
    return False;
}

Status
XcupStoreColors(
    Display* dpy,
    Colormap colormap,
    XColor* colors_in_out,
    int ncolors)
{
    XExtDisplayInfo *info = find_display (dpy);
    xXcupStoreColorsReply rep;
    xXcupStoreColorsReq *req;
    xColorItem rbuf[256];
    xColorItem citem;
    int i;
    XColor* xcp;

    XextCheckExtension (dpy, info, xcup_extension_name, False);

    LockDisplay(dpy);
    GetReq(XcupStoreColors, req);
    req->reqType = info->codes->major_opcode;
    req->xcupReqType = X_XcupStoreColors;
    req->cmap = colormap;
    req->length += (ncolors * SIZEOF(xColorItem)) >> 2;

    for (i = 0, xcp = colors_in_out; i < ncolors; i++, xcp++) {
	citem.pixel = xcp->pixel;
	citem.red = xcp->red;
	citem.green = xcp->green;
	citem.blue = xcp->blue;

	/* note that xColorItem doesn't contain all 16-bit quantities, so
	   we can't use Data16 */
	Data(dpy, (char *)&citem, (long) SIZEOF(xColorItem));
    }

    if (_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	unsigned long nbytes;
	xColorItem* rbufp;
	xColorItem* cs;
	unsigned int nentries = rep.length / 3;

	if ((nentries == ncolors) &&
	    (nentries < (INT_MAX / SIZEOF (xColorItem)))) {
	    nbytes = nentries * SIZEOF (xColorItem);

	    if (ncolors > 256)
		rbufp = Xmalloc (nbytes);
	    else
		rbufp = rbuf;
	} else
	    rbufp = NULL;

	if (rbufp == NULL) {
	    _XEatDataWords(dpy, rep.length);
	    UnlockDisplay (dpy);
	    SyncHandle ();
	    return False;

	}

	_XRead (dpy, (char*) rbufp, nbytes);

	for (i = 0, xcp = colors_in_out, cs = rbufp; i < ncolors; i++, xcp++, cs++) {
	    xcp->pixel = cs->pixel;
	    xcp->red = cs->red;
	    xcp->green = cs->green;
	    xcp->blue = cs->blue;
	    xcp->flags = cs->flags;
	}
	if (rbufp != rbuf) XFree ((char*)rbufp);

	UnlockDisplay(dpy);
	SyncHandle();
	return True;
    }
    UnlockDisplay(dpy);
    SyncHandle();
    return False;
}

