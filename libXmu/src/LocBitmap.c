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

/*
 * Author:  Jim Fulton, MIT X Consortium
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/Xmu/CvtCache.h>
#include <X11/Xmu/Drawing.h>
#include <X11/Xmu/SysUtil.h>

#ifndef X_NOT_POSIX
#ifdef _POSIX_SOURCE
#include <limits.h>
#else
#define _POSIX_SOURCE
#include <limits.h>
#undef _POSIX_SOURCE
#endif
#endif /* X_NOT_POSIX */
#ifndef PATH_MAX
#ifdef WIN32
#define PATH_MAX 512
#else
#include <sys/param.h>
#endif
#ifndef PATH_MAX
#ifdef MAXPATHLEN
#define PATH_MAX MAXPATHLEN
#else
#define PATH_MAX 1024
#endif
#endif
#endif /* PATH_MAX */

/*
 * Prototypes
 */
static char **split_path_string(char*);

/*
 * XmuLocateBitmapFile - read a bitmap file using the normal defaults
 */

Pixmap
XmuLocateBitmapFile(Screen *screen, _Xconst char *name, char *srcname,
			    int srcnamelen, int *widthp, int *heightp,
			    int *xhotp, int *yhotp)
{
    return XmuLocatePixmapFile (screen, name,
				(unsigned long) 1, (unsigned long) 0,
				(unsigned int) 1, srcname, srcnamelen,
				widthp, heightp, xhotp, yhotp);
}


/*
 * version that reads pixmap data as well as bitmap data
 */
Pixmap
XmuLocatePixmapFile(Screen *screen, _Xconst char *name,
			    unsigned long fore, unsigned long back,
			    unsigned int depth,
			    char *srcname, int srcnamelen,
			    int *widthp, int *heightp, int *xhotp, int *yhotp)
{

#ifndef BITMAPDIR
#define BITMAPDIR "/usr/include/X11/bitmaps"
#endif

    Display *dpy = DisplayOfScreen (screen);
    Window root = RootWindowOfScreen (screen);
    Bool try_plain_name = True;
    XmuCvtCache *cache = _XmuCCLookupDisplay (dpy);
    char **file_paths = (char **) NULL;
    char filename[PATH_MAX];
#if 0
    char* bitmapdir = BITMAPDIR;
#endif
    unsigned int width, height;
    int xhot, yhot;
    int i;

    /*
     * look in cache for bitmap path
     */
    if (cache) {
	if (!cache->string_to_bitmap.bitmapFilePath) {
	    XrmName xrm_name[2];
	    XrmClass xrm_class[2];
	    XrmRepresentation rep_type;
	    XrmValue value;

	    xrm_name[0] = XrmPermStringToQuark ("bitmapFilePath");
	    xrm_name[1] = NULLQUARK;
	    xrm_class[0] = XrmPermStringToQuark ("BitmapFilePath");
	    xrm_class[1] = NULLQUARK;
	    if (!XrmGetDatabase(dpy)) {
		/* what a hack; need to initialize it */
		(void) XGetDefault (dpy, "", "");
	    }
	    if (XrmQGetResource (XrmGetDatabase(dpy), xrm_name, xrm_class,
				 &rep_type, &value) &&
		rep_type == XrmPermStringToQuark("String")) {
		cache->string_to_bitmap.bitmapFilePath =
		  split_path_string (value.addr);
	    }
	}
	file_paths = cache->string_to_bitmap.bitmapFilePath;
    }

    /*
     * Search order:
     *    1.  name if it begins with / or ./
     *    2.  "each prefix in file_paths"/name
     *    3.  BITMAPDIR/name
     *    4.  name if didn't begin with / or .
     */

    for (i = 1; i <= 4; i++) {
	const char *fn = filename;
	Pixmap pixmap;
	unsigned char *data;

	switch (i) {
	  case 1:
	    if (!(name[0] == '/' || ((name[0] == '.') && name[1] == '/')))
	      continue;
	    fn = name;
	    try_plain_name = False;
	    break;
	  case 2:
	    if (file_paths && *file_paths) {
		XmuSnprintf(filename, sizeof(filename),
			    "%s/%s", *file_paths, name);
		file_paths++;
		i--;
		break;
	    }
	    continue;
	  case 3:
	    XmuSnprintf(filename, sizeof(filename), "%s/%s", BITMAPDIR, name);
	    break;
	  case 4:
	    if (!try_plain_name) continue;
	    fn = name;
	    break;
	}

	data = NULL;
	pixmap = None;
	if (XmuReadBitmapDataFromFile (fn, &width, &height, &data,
				       &xhot, &yhot) == BitmapSuccess) {
	    pixmap = XCreatePixmapFromBitmapData (dpy, root, (char *) data,
						  width, height,
						  fore, back, depth);
	    XFree ((char *)data);
	}

	if (pixmap) {
	    if (widthp) *widthp = (int)width;
	    if (heightp) *heightp = (int)height;
	    if (xhotp) *xhotp = xhot;
	    if (yhotp) *yhotp = yhot;
	    if (srcname && srcnamelen > 0) {
		strncpy (srcname, fn, srcnamelen - 1);
		srcname[srcnamelen - 1] = '\0';
	    }
	    return pixmap;
	}
    }

    return None;
}


/*
 * split_path_string - split a colon-separated list into its constituent
 * parts; to release, free list[0] and list.
 */
static char **
split_path_string(register char *src)
{
    int nelems = 1;
    register char *dst;
    char **elemlist, **elem;

    /* count the number of elements */
    for (dst = src; *dst; dst++) if (*dst == ':') nelems++;

    /* get memory for everything */
    dst = (char *) malloc (dst - src + 1);
    if (!dst) return NULL;
    elemlist = (char **) calloc ((nelems + 1), sizeof (char *));
    if (!elemlist) {
	free (dst);
	return NULL;
    }

    /* copy to new list and walk up nulling colons and setting list pointers */
    strcpy (dst, src);
    for (elem = elemlist, src = dst; *src; src++) {
	if (*src == ':') {
	    *elem++ = dst;
	    *src = '\0';
	    dst = src + 1;
	}
    }
    *elem = dst;

    return elemlist;
}


void
_XmuStringToBitmapInitCache(register XmuCvtCache *c)
{
    c->string_to_bitmap.bitmapFilePath = NULL;
}

void
_XmuStringToBitmapFreeCache(register XmuCvtCache *c)
{
    if (c->string_to_bitmap.bitmapFilePath) {
	if (c->string_to_bitmap.bitmapFilePath[0])
	  free (c->string_to_bitmap.bitmapFilePath[0]);
	free ((char *) (c->string_to_bitmap.bitmapFilePath));
    }
}
