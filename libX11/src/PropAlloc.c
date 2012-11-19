/*

Copyright 1989, 1998  The Open Group

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
#include "Xlibint.h"
#include "Xutil.h"
#include <stdio.h>


/*
 * Routines for allocating space for structures that are expected to get
 * longer at some point.
 */

XSizeHints *XAllocSizeHints (void)
{
    return ((XSizeHints *) Xcalloc (1, (unsigned) sizeof (XSizeHints)));
}


XStandardColormap *XAllocStandardColormap (void)
{
    return ((XStandardColormap *)
	    Xcalloc (1, (unsigned) sizeof (XStandardColormap)));
}


XWMHints *XAllocWMHints (void)
{
    return ((XWMHints *) Xcalloc (1, (unsigned) sizeof (XWMHints)));
}


XClassHint *XAllocClassHint (void)
{
    register XClassHint *h;

    if ((h = (XClassHint *) Xcalloc (1, (unsigned) sizeof (XClassHint))))
      h->res_name = h->res_class = NULL;

    return h;
}


XIconSize *XAllocIconSize (void)
{
    return ((XIconSize *) Xcalloc (1, (unsigned) sizeof (XIconSize)));
}


