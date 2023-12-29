/*
 *
 * Copyright © 2002 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xrenderint.h"
#include <limits.h>

XFilters *
XRenderQueryFilters (Display *dpy, Drawable drawable)
{
    XRenderExtDisplayInfo	*info = XRenderFindDisplay (dpy);
    XRenderInfo			*xri;
    xRenderQueryFiltersReq	*req;
    xRenderQueryFiltersReply	rep;
    XFilters			*filters;
    char			*name;
    char			len;
    unsigned int		i;
    unsigned long		nbytesName, reply_left;

    if (!RenderHasExtension (info))
	return NULL;

    if (!XRenderQueryFormats (dpy))
	return NULL;

    xri = info->info;
    if (xri->minor_version < 6)
	return NULL;

    LockDisplay (dpy);
    GetReq (RenderQueryFilters, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderQueryFilters;
    req->drawable = (CARD32) drawable;
    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse))
    {
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }

    /*
     * Limit each component of combined size to 1/4 the max, which is far
     * more than they should ever possibly need.
     */
    if ((rep.length < (INT_MAX >> 2)) &&
	(rep.numFilters < ((INT_MAX / 4) / sizeof (char *))) &&
	(rep.numAliases < ((INT_MAX / 4) / sizeof (short)))) {
	unsigned long		nbytes, nbytesAlias;

	/*
	 * Compute total number of bytes for filter names
	 */
	nbytes = (unsigned long)rep.length << 2;
	nbytesAlias = rep.numAliases * 2;
	if (rep.numAliases & 1)
	    nbytesAlias += 2;
	nbytesName = nbytes - nbytesAlias;

	/*
	 * Allocate one giant block for the whole data structure
	 */
	filters = Xmalloc (sizeof (XFilters) +
			   (rep.numFilters * sizeof (char *)) +
			   (rep.numAliases * sizeof (short)) +
			   nbytesName);
    } else
	filters = NULL;

    if (!filters)
    {
	_XEatDataWords(dpy, rep.length);
	UnlockDisplay (dpy);
	SyncHandle ();
	return NULL;
    }

    /*
     * Layout:
     *	XFilters
     *	numFilters  char * pointers to filter names
     *	numAliases  short alias values
     *	nbytesName  char strings
     */

    filters->nfilter = (int) rep.numFilters;
    filters->nalias = (int) rep.numAliases;
    filters->filter = (char **) (filters + 1);
    filters->alias = (short *) (filters->filter + rep.numFilters);
    name = (char *) (filters->alias + rep.numAliases);

    /*
     * Read the filter aliases
     */
    _XRead16Pad (dpy, filters->alias, 2 * rep.numAliases);
    reply_left = 8 + rep.length - 2 * rep.numAliases;;

    /*
     * Read the filter names
     */
    for (i = 0; i < rep.numFilters; i++)
    {
	int	l;
	_XRead (dpy, &len, 1);
	reply_left--;
	l = len & 0xff;
	if ((unsigned long)l + 1 > nbytesName) {
            _XEatDataWords(dpy, reply_left);
	    Xfree(filters);
	    UnlockDisplay (dpy);
	    SyncHandle ();
	    return NULL;
	}
	nbytesName -= (unsigned long) (l + 1);
	filters->filter[i] = name;
	_XRead (dpy, name, l);
        reply_left -= (unsigned long) l;
	name[l] = '\0';
	name += l + 1;
    }
    i = (unsigned) (name - (char *) (filters->alias + rep.numAliases));

    if (i & 3)
	_XEatData (dpy, 4 - (i & 3));

    UnlockDisplay (dpy);
    SyncHandle ();
    return filters;
}

void
XRenderSetPictureFilter  (Display   *dpy,
			  Picture   picture,
			  const char *filter,
			  XFixed    *params,
			  int	    nparams)
{
    XRenderExtDisplayInfo	*info = XRenderFindDisplay (dpy);
    xRenderSetPictureFilterReq	*req;
    int				nbytes = (int) strlen (filter);

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay(dpy);
    GetReq(RenderSetPictureFilter, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderSetPictureFilter;
    req->picture = (CARD32) picture;
    req->nbytes = (CARD16) nbytes;
    req->length = (CARD16) (req->length + (((nbytes + 3) >> 2) + nparams));
    Data (dpy, filter, nbytes);
    Data (dpy, (_Xconst char *)params, (nparams << 2));
    UnlockDisplay(dpy);
    SyncHandle();
}
