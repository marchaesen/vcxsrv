/************************************************************
Copyright (c) 1997 by Silicon Graphics Computer Systems, Inc.
Permission to use, copy, modify, and distribute this
software and its documentation for any purpose and without
fee is hereby granted, provided that the above copyright
notice appear in all copies and that both that copyright
notice and this permission notice appear in supporting
documentation, and that the name of Silicon Graphics not be
used in advertising or publicity pertaining to distribution
of the software without specific prior written permission.
Silicon Graphics makes no representation about the suitability
of this software for any purpose. It is provided "as is"
without any express or implied warranty.
SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
THE USE OR PERFORMANCE OF THIS SOFTWARE.
********************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xlibint.h>
#include <X11/extensions/XEVI.h>
#include <X11/extensions/EVIproto.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <X11/Xutil.h>
#include <limits.h>

static XExtensionInfo *xevi_info;/* needs to move to globals.c */
static const char *xevi_extension_name = EVINAME;
#define XeviCheckExtension(dpy,i,val) \
  XextCheckExtension (dpy, i, xevi_extension_name, val)
/*****************************************************************************
 *                                                                           *
 *			   private utility routines                          *
 *                                                                           *
 *****************************************************************************/
static /* const */ XExtensionHooks xevi_extension_hooks = {
    NULL,			/* create_gc */
    NULL,			/* copy_gc */
    NULL,			/* flush_gc */
    NULL,			/* free_gc */
    NULL,			/* create_font */
    NULL,			/* free_font */
    NULL,			/* close_display */
    NULL,			/* wire_to_event */
    NULL,			/* event_to_wire */
    NULL,			/* error */
    NULL,			/* error_string */
};
static XEXT_GENERATE_FIND_DISPLAY (find_display, xevi_info,
                                   xevi_extension_name,
                                   &xevi_extension_hooks, 0, NULL)
Bool XeviQueryExtension (Display *dpy)
{
    XExtDisplayInfo *info = find_display (dpy);
    if (XextHasExtension(info)) {
	return True;
    } else {
	return False;
    }
}
Bool XeviQueryVersion(Display *dpy, int *majorVersion, int *minorVersion)
{
    XExtDisplayInfo *info = find_display (dpy);
    xEVIQueryVersionReply rep;
    register xEVIQueryVersionReq *req;
    XeviCheckExtension (dpy, info, False);
    LockDisplay(dpy);
    GetReq(EVIQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->xeviReqType = X_EVIQueryVersion;
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return False;
    }
    *majorVersion = rep.majorVersion;
    *minorVersion = rep.minorVersion;
    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}
static Bool notInList(VisualID32 *visual, int sz_visual, VisualID newVisualid)
{
    while  (sz_visual-- > 0)  {
	if (*visual == newVisualid)
	    return False;
	visual++;
    }
    return True;
}
Status XeviGetVisualInfo(
    register Display *dpy,
    VisualID *visual,
    int n_visual,
    ExtendedVisualInfo **evi_return,
    int *n_info_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    register xEVIGetVisualInfoReq *req;
    xEVIGetVisualInfoReply rep;
    int sz_info, sz_xInfo, sz_conflict, sz_xConflict;
    VisualID32 *temp_conflict, *temp_visual, *xConflictPtr;
    VisualID *conflict;
    xExtendedVisualInfo *temp_xInfo;
    XVisualInfo *vinfo;
    register ExtendedVisualInfo *infoPtr;
    register xExtendedVisualInfo *xInfoPtr;
    register int n_data, visualIndex, vinfoIndex;
    Bool isValid;
    XeviCheckExtension (dpy, info, 0);
    if (!n_info_return || !evi_return) {
	return BadValue;
    }
    *n_info_return = 0;
    *evi_return = NULL;
    vinfo = XGetVisualInfo(dpy, 0, NULL, &sz_info);
    if (!vinfo) {
	return BadValue;
    }
    if (!n_visual || !visual) {		/* copy the all visual */
    	temp_visual = (VisualID32 *)Xmalloc(sz_VisualID32 * sz_info);
    	n_visual = 0;
        for (vinfoIndex = 0; vinfoIndex < sz_info; vinfoIndex++)
	    if (notInList(temp_visual, n_visual, vinfo[vinfoIndex].visualid))
	        temp_visual[n_visual++] = vinfo[vinfoIndex].visualid;
    }
    else {	/* check if the visual is valid */
        for (visualIndex = 0; visualIndex < n_visual; visualIndex++) {
	    isValid = False;
            for (vinfoIndex = 0; vinfoIndex < sz_info; vinfoIndex++) {
	        if (visual[visualIndex] == vinfo[vinfoIndex].visualid) {
		    isValid = True;
		    break;
	        }
	    }
	    if (!isValid) {
		XFree(vinfo);
	        return BadValue;
	    }
	}
	temp_visual = (VisualID32 *)Xmalloc(sz_VisualID32 * n_visual);
        for (visualIndex = 0; visualIndex < n_visual; visualIndex++)
	    temp_visual[visualIndex] = visual[visualIndex];
    }
    XFree(vinfo);
    LockDisplay(dpy);
    GetReq(EVIGetVisualInfo, req);
    req->reqType = info->codes->major_opcode;
    req->xeviReqType = X_EVIGetVisualInfo;
    req->n_visual = n_visual;
    SetReqLen(req, n_visual, 1);
    Data(dpy, (char *)temp_visual, n_visual * sz_VisualID32);
    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	Xfree(temp_visual);
	return BadAccess;
    }
    Xfree(temp_visual);
    if ((rep.n_info < 65536) && (rep.n_conflicts < 65536)) {
	sz_info = rep.n_info * sizeof(ExtendedVisualInfo);
	sz_xInfo = rep.n_info * sz_xExtendedVisualInfo;
	sz_conflict = rep.n_conflicts * sizeof(VisualID);
	sz_xConflict = rep.n_conflicts * sz_VisualID32;
	*evi_return = Xmalloc(sz_info + sz_conflict);
	temp_xInfo = Xmalloc(sz_xInfo);
	temp_conflict = Xmalloc(sz_xConflict);
    } else {
	sz_xInfo = sz_xConflict = 0;
	*evi_return = NULL;
	temp_xInfo = NULL;
	temp_conflict = NULL;
    }
    if (!*evi_return || !temp_xInfo || !temp_conflict) {
	_XEatDataWords(dpy, rep.length);
	UnlockDisplay(dpy);
	SyncHandle();
	if (*evi_return) {
	   Xfree(*evi_return);
	   *evi_return = NULL;
	}
	if (temp_xInfo)
	   Xfree(temp_xInfo);
	if (temp_conflict)
	   Xfree(temp_conflict);
	return BadAlloc;
    }
    _XRead(dpy, (char *)temp_xInfo, sz_xInfo);
    _XRead(dpy, (char *)temp_conflict, sz_xConflict);
    UnlockDisplay(dpy);
    SyncHandle();
    infoPtr = *evi_return;
    xInfoPtr = temp_xInfo;
    xConflictPtr = temp_conflict;
    n_data = rep.n_info;
    conflict = (VisualID *)(infoPtr + n_data);
    while (n_data-- > 0) {
	infoPtr->core_visual_id		= xInfoPtr->core_visual_id;
	infoPtr->screen			= xInfoPtr->screen;
	infoPtr->level			= xInfoPtr->level;
	infoPtr->transparency_type	= xInfoPtr->transparency_type;
	infoPtr->transparency_value	= xInfoPtr->transparency_value;
	infoPtr->min_hw_colormaps	= xInfoPtr->min_hw_colormaps;
	infoPtr->max_hw_colormaps	= xInfoPtr->max_hw_colormaps;
	infoPtr->num_colormap_conflicts = xInfoPtr->num_colormap_conflicts;
	infoPtr->colormap_conflicts	= conflict;
	conflict += infoPtr->num_colormap_conflicts;
	infoPtr++;
	xInfoPtr++;
    }
    n_data = rep.n_conflicts;
    conflict = (VisualID *)(infoPtr);
    while (n_data-- > 0)
       *conflict++ = *xConflictPtr++;
    Xfree(temp_xInfo);
    Xfree(temp_conflict);
    *n_info_return = rep.n_info;
    return Success;
}
