/*
 *
 * Copyright © 2000 SuSE, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of SuSE not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  SuSE makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * SuSE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL SuSE
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Keith Packard, SuSE, Inc.
 */

#ifndef _XRENDERINT_H_
#define _XRENDERINT_H_

#include "config.h"
#include <X11/Xlibint.h>
#include <X11/Xutil.h>
#include <X11/extensions/renderproto.h>
#include "Xrender.h"

typedef struct {
    Visual		*visual;
    XRenderPictFormat	*format;
} XRenderVisual;

typedef struct {
    int			depth;
    int			nvisuals;
    XRenderVisual	*visuals;
} XRenderDepth;

typedef struct {
    XRenderDepth	*depths;
    int			ndepths;
    XRenderPictFormat	*fallback;
    int			subpixel;
} XRenderScreen;

typedef struct _XRenderInfo {
    int			major_version;
    int			minor_version;
    XRenderPictFormat	*format;
    int			nformat;
    XRenderScreen	*screen;
    int			nscreen;
    XRenderDepth	*depth;
    int			ndepth;
    XRenderVisual	*visual;
    int			nvisual;
    int			*subpixel;
    int			nsubpixel;
    char		**filter;
    int			nfilter;
    short    		*filter_alias;
    int			nfilter_alias;
} XRenderInfo;

/* replaces XRenderExtDisplayInfo */
typedef struct _XRenderExtDisplayInfo {
    struct _XRenderExtDisplayInfo *next;    /* keep a linked list */
    Display                       *display; /* which display this is */
    XExtCodes                     *codes;   /* the extension protocol codes */
    XRenderInfo                   *info;    /* extra data for the extension to use */
} XRenderExtDisplayInfo;

/* replaces XExtensionInfo */
typedef struct _XRenderExtInfo {
    XRenderExtDisplayInfo  *head;           /* start of the list */
    XRenderExtDisplayInfo  *cur;            /* most recently used */
    int                     ndisplays;      /* number of displays */
} XRenderExtInfo;

extern XRenderExtInfo XRenderExtensionInfo;
extern char XRenderExtensionName[];

XRenderExtDisplayInfo *
XRenderFindDisplay (Display *dpy);

#define RenderHasExtension(i) ((i) && ((i)->codes))

#define RenderCheckExtension(dpy,i,val) \
  if (!RenderHasExtension(i)) { return val; }

#define RenderSimpleCheckExtension(dpy,i) \
  if (!RenderHasExtension(i)) { return; }

/*
 * Xlib uses long for 32-bit values.  Xrender uses int.  This
 * matters on alpha.  Note that this macro assumes that int is 32 bits
 * except on WORD64 machines where it is 64 bits.
 */

#ifdef WORD64
#define DataInt32(dpy,d,len)	Data32(dpy,(long *) (d),len)
#else
#define DataInt32(dpy,d,len)	Data(dpy,(char *) (d),len)
#endif

#endif /* _XRENDERINT_H_ */
