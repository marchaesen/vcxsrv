/*
 * Copyright © 2000 Keith Packard
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

#include "xftint.h"

static Bool _XftConfigInitialized;

_X_EXPORT Bool
XftInit (_Xconst char *config _X_UNUSED)
{
    if (_XftConfigInitialized)
	return True;
    _XftConfigInitialized = True;
    if (!FcInit ())
	return False;
    return True;
}

_X_EXPORT int
XftGetVersion (void)
{
    return XftVersion;
}

static struct {
    const char *name;
    int	    alloc_count;
    size_t  alloc_mem;
    int	    free_count;
    size_t  free_mem;
} XftInUse[XFT_MEM_NUM] = {
    { "XftDraw",   0, 0, 0, 0 },	/* XFT_MEM_DRAW */
    { "XftFont",   0, 0, 0, 0 },	/* XFT_MEM_FONT */
    { "XftFtFile", 0, 0, 0, 0 },	/* XFT_MEM_FILE */
    { "XftGlyph",  0, 0, 0, 0 },	/* XFT_MEM_GLYPH */
};

static int  XftAllocCount;
static size_t XftAllocMem;

static int  XftFreeCount;
static size_t XftFreeMem;

static const size_t  XftMemNotice = 1*1024*1024;

static size_t  XftAllocNotify, XftFreeNotify;

_X_HIDDEN void
XftMemReport (void)
{
    int	i;
    printf ("Xft Memory Usage:\n");
    printf ("\t    Which       Alloc           Free\n");
    printf ("\t            count   bytes   count   bytes\n");
    for (i = 0; i < XFT_MEM_NUM; i++)
	printf ("\t%9.9s%8d%8lu%8d%8lu\n",
		XftInUse[i].name,
		XftInUse[i].alloc_count, (unsigned long) XftInUse[i].alloc_mem,
		XftInUse[i].free_count, (unsigned long) XftInUse[i].free_mem);
    printf ("\t%9.9s%8d%8lu%8d%8lu\n",
	    "Total",
	    XftAllocCount, (unsigned long) XftAllocMem,
	    XftFreeCount, (unsigned long) XftFreeMem);
    XftAllocNotify = 0;
    XftFreeNotify = 0;
}

_X_HIDDEN void
XftMemAlloc (int kind, size_t size)
{
    if (XftDebug() & XFT_DBG_MEMORY)
    {
	XftInUse[kind].alloc_count++;
	XftInUse[kind].alloc_mem += size;
	XftAllocCount++;
	XftAllocMem += size;
	XftAllocNotify += size;
	if (XftAllocNotify > XftMemNotice)
	    XftMemReport ();
    }
}

_X_HIDDEN void
XftMemFree (int kind, size_t size)
{
    if (XftDebug() & XFT_DBG_MEMORY)
    {
	XftInUse[kind].free_count++;
	XftInUse[kind].free_mem += size;
	XftFreeCount++;
	XftFreeMem += size;
	XftFreeNotify += size;
	if (XftFreeNotify > XftMemNotice)
	    XftMemReport ();
    }
}
