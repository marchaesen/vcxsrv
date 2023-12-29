/*
 * Copyright (C) 1989-95 GROUPE BULL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * GROUPE BULL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of GROUPE BULL shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from GROUPE BULL.
 */

/*****************************************************************************\
*  CrBufFrI.c:                                                                *
*                                                                             *
*  XPM library                                                                *
*  Scan an image and possibly its mask and create an XPM buffer               *
*                                                                             *
*  Developed by Arnaud Le Hors                                                *
\*****************************************************************************/

/* October 2004, source code review by Thomas Biege <thomas@suse.de> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "XpmI.h"

#ifdef FOR_MSW
#define snprintf _snprintf
#endif

LFUNC(WriteColors, int, (char **dataptr, unsigned int *data_size,
			 unsigned int *used_size, XpmColor *colors,
			 unsigned int ncolors, unsigned int cpp));

LFUNC(WritePixels, void, (char *dataptr, unsigned int data_size,
			  unsigned int *used_size,
			  unsigned int width, unsigned int height,
			  unsigned int cpp, unsigned int *pixels,
			  XpmColor *colors));

LFUNC(WriteExtensions, void, (char *dataptr, unsigned int data_size,
			      unsigned int *used_size,
			      XpmExtension *ext, unsigned int num));

LFUNC(ExtensionsSize, unsigned int, (XpmExtension *ext, unsigned int num));
LFUNC(CommentsSize, int, (XpmInfo *info));

int
XpmCreateBufferFromImage(
    Display		 *display,
    char		**buffer_return,
    XImage		 *image,
    XImage		 *shapeimage,
    XpmAttributes	 *attributes)
{
    XpmImage xpmimage;
    XpmInfo info;
    int ErrorStatus;

    /* initialize return value */
    if (buffer_return)
	*buffer_return = NULL;

    /* create an XpmImage from the image */
    ErrorStatus = XpmCreateXpmImageFromImage(display, image, shapeimage,
					     &xpmimage, attributes);
    if (ErrorStatus != XpmSuccess)
	return (ErrorStatus);

    /* create the buffer from the XpmImage */
    if (attributes) {
	xpmSetInfo(&info, attributes);
	ErrorStatus =
	    XpmCreateBufferFromXpmImage(buffer_return, &xpmimage, &info);
    } else
	ErrorStatus =
	    XpmCreateBufferFromXpmImage(buffer_return, &xpmimage, NULL);

    /* free the XpmImage */
    XpmFreeXpmImage(&xpmimage);

    return (ErrorStatus);
}


#undef RETURN
#define RETURN(status) \
do \
{ \
      ErrorStatus = status; \
      goto error; \
}while(0)

int
XpmCreateBufferFromXpmImage(
    char	**buffer_return,
    XpmImage	 *image,
    XpmInfo	 *info)
{
    /* calculation variables */
    int ErrorStatus;
    char buf[BUFSIZ];
    unsigned int cmts, extensions, ext_size = 0;
    unsigned int l, cmt_size = 0;
    char *ptr = NULL, *p;
    unsigned int ptr_size, used_size, tmp;

    *buffer_return = NULL;

    cmts = info && (info->valuemask & XpmComments);
    extensions = info && (info->valuemask & XpmExtensions)
	&& info->nextensions;

    /* compute the extensions and comments size */
    if (extensions)
	ext_size = ExtensionsSize(info->extensions, info->nextensions);
    if (cmts)
	cmt_size = CommentsSize(info);

    /* write the header line */
#ifndef VOID_SPRINTF
    used_size =
#endif
    sprintf(buf, "/* XPM */\nstatic char * image_name[] = {\n");
#ifdef VOID_SPRINTF
    used_size = strlen(buf);
#endif
    ptr_size = used_size + ext_size + cmt_size + 1; /* ptr_size can't be 0 */
    if(ptr_size <= used_size ||
       ptr_size <= ext_size  ||
       ptr_size <= cmt_size)
    {
        return XpmNoMemory;
    }
    ptr = (char *) XpmMalloc(ptr_size);
    if (!ptr)
	return XpmNoMemory;
    strcpy(ptr, buf);

    /* write the values line */
    if (cmts && info->hints_cmt) {
#ifndef VOID_SPRINTF
	used_size +=
#endif
	snprintf(ptr + used_size, ptr_size-used_size, "/*%s*/\n", info->hints_cmt);
#ifdef VOID_SPRINTF
	used_size += strlen(info->hints_cmt) + 5;
#endif
    }
#ifndef VOID_SPRINTF
    l =
#endif
    sprintf(buf, "\"%d %d %d %d", image->width, image->height,
	    image->ncolors, image->cpp);
#ifdef VOID_SPRINTF
    l = strlen(buf);
#endif

    if (info && (info->valuemask & XpmHotspot)) {
#ifndef VOID_SPRINTF
	l +=
#endif
	snprintf(buf + l, sizeof(buf)-l, " %d %d", info->x_hotspot, info->y_hotspot);
#ifdef VOID_SPRINTF
	l = strlen(buf);
#endif
    }
    if (extensions) {
#ifndef VOID_SPRINTF
	l +=
#endif
	sprintf(buf + l, " XPMEXT");
#ifdef VOID_SPRINTF
	l = strlen(buf);
#endif
    }
#ifndef VOID_SPRINTF
    l +=
#endif
    sprintf(buf + l, "\",\n");
#ifdef VOID_SPRINTF
    l = strlen(buf);
#endif
    ptr_size += l;
    if(ptr_size <= l)
        RETURN(XpmNoMemory);
    p = (char *) XpmRealloc(ptr, ptr_size);
    if (!p)
	RETURN(XpmNoMemory);
    ptr = p;
    strcpy(ptr + used_size, buf);
    used_size += l;

    /* write colors */
    if (cmts && info->colors_cmt) {
#ifndef VOID_SPRINTF
	used_size +=
#endif
	snprintf(ptr + used_size, ptr_size-used_size, "/*%s*/\n", info->colors_cmt);
#ifdef VOID_SPRINTF
	used_size += strlen(info->colors_cmt) + 5;
#endif
    }
    ErrorStatus = WriteColors(&ptr, &ptr_size, &used_size,
			      image->colorTable, image->ncolors, image->cpp);

    if (ErrorStatus != XpmSuccess)
	RETURN(ErrorStatus);

    /*
     * now we know the exact size we need, realloc the data
     * 4 = 1 (for '"') + 3 (for '",\n')
     * 1 = - 2 (because the last line does not end with ',\n') + 3 (for '};\n')
     */
     if(image->width  > UINT_MAX / image->cpp ||
       (tmp = image->width * image->cpp + 4) <= 4 ||
        image->height > UINT_MAX / tmp ||
       (tmp = image->height * tmp + 1) <= 1 ||
       (ptr_size += tmp) <= tmp)
	RETURN(XpmNoMemory);

    p = (char *) XpmRealloc(ptr, ptr_size);
    if (!p)
	RETURN(XpmNoMemory);
    ptr = p;

    /* print pixels */
    if (cmts && info->pixels_cmt) {
#ifndef VOID_SPRINTF
	used_size +=
#endif
	snprintf(ptr + used_size, ptr_size-used_size, "/*%s*/\n", info->pixels_cmt);
#ifdef VOID_SPRINTF
	used_size += strlen(info->pixels_cmt) + 5;
#endif
    }
    WritePixels(ptr + used_size, ptr_size - used_size, &used_size, image->width, image->height,
		image->cpp, image->data, image->colorTable);

    /* print extensions */
    if (extensions)
	WriteExtensions(ptr + used_size, ptr_size-used_size, &used_size,
			info->extensions, info->nextensions);

    /* close the array */
    strcpy(ptr + used_size, "};\n");

    *buffer_return = ptr;

    return (XpmSuccess);

/* exit point in case of error, free only locally allocated variables */
error:
    if (ptr)
	XpmFree(ptr);
    return (ErrorStatus);
}


static int
WriteColors(
    char		**dataptr,
    unsigned int	 *data_size,
    unsigned int	 *used_size,
    XpmColor		 *colors,
    unsigned int	  ncolors,
    unsigned int	  cpp)
{
    char buf[BUFSIZ] = {0};
    unsigned int a, key, l;
    char *s, *s2;
    char **defaults;

    *buf = '"';
    for (a = 0; a < ncolors; a++, colors++) {

	defaults = (char **) colors;
	s = buf + 1;
	if(cpp > (sizeof(buf) - (s-buf)))
		return(XpmNoMemory);
	strncpy(s, *defaults++, cpp);
	s += cpp;

	for (key = 1; key <= NKEYS; key++, defaults++) {
	    if ((s2 = *defaults)) {
#ifndef VOID_SPRINTF
		s +=
#endif
		/* assume C99 compliance */
		snprintf(s, sizeof(buf) - (s-buf), "\t%s %s", xpmColorKeys[key - 1], s2);
#ifdef VOID_SPRINTF
		s += strlen(s);
#endif
		/* now let's check if s points out-of-bounds */
		if((s-buf) > sizeof(buf))
			return(XpmNoMemory);
	    }
	}
	if(sizeof(buf) - (s-buf) < 4)
		return(XpmNoMemory);
	strcpy(s, "\",\n");
	l = s + 3 - buf;
	if( *data_size                   >= UINT_MAX-l ||
	    *data_size + l               <= *used_size ||
	   (*data_size + l - *used_size) <= sizeof(buf))
		return(XpmNoMemory);
	s = (char *) XpmRealloc(*dataptr, *data_size + l);
	if (!s)
	    return (XpmNoMemory);
	*data_size += l;
	strcpy(s + *used_size, buf);
	*used_size += l;
	*dataptr = s;
    }
    return (XpmSuccess);
}

static void
WritePixels(
    char		*dataptr,
    unsigned int	 data_size,
    unsigned int	*used_size,
    unsigned int	 width,
    unsigned int	 height,
    unsigned int	 cpp,
    unsigned int	*pixels,
    XpmColor		*colors)
{
    char *s = dataptr;
    unsigned int x, y, h;

    if(height <= 1)
    	return;

    h = height - 1;
    for (y = 0; y < h; y++) {
	*s++ = '"';
	for (x = 0; x < width; x++, pixels++) {
	    if(cpp >= (data_size - (s-dataptr)))
		return;
	    strncpy(s, colors[*pixels].string, cpp); /* how can we trust *pixels? :-\ */
	    s += cpp;
	}
	if((data_size - (s-dataptr)) < 4)
		return;
	strcpy(s, "\",\n");
	s += 3;
    }
    /* duplicate some code to avoid a test in the loop */
    *s++ = '"';
    for (x = 0; x < width; x++, pixels++) {
	if(cpp >= (data_size - (s-dataptr)))
	    return;
	strncpy(s, colors[*pixels].string, cpp); /* how can we trust *pixels? */
	s += cpp;
    }
    *s++ = '"';
    *used_size += s - dataptr;
}

static unsigned int
ExtensionsSize(
    XpmExtension	*ext,
    unsigned int	 num)
{
    unsigned int x, y, a, size;
    char **line;

    size = 0;
    if(num == 0)
    	return(0); /* ok? */
    for (x = 0; x < num; x++, ext++) {
	/* 11 = 10 (for ',\n"XPMEXT ') + 1 (for '"') */
	size += strlen(ext->name) + 11;
	a = ext->nlines; /* how can we trust ext->nlines to be not out-of-bounds? */
	for (y = 0, line = ext->lines; y < a; y++, line++)
	    /* 4 = 3 (for ',\n"') + 1 (for '"') */
	    size += strlen(*line) + 4;
    }
    /* 13 is for ',\n"XPMENDEXT"' */
    if(size > UINT_MAX - 13) /* unlikely */
    	return(0);
    return size + 13;
}

static void
WriteExtensions(
    char		*dataptr,
    unsigned int	 data_size,
    unsigned int	*used_size,
    XpmExtension	*ext,
    unsigned int	 num)
{
    unsigned int x, y, a;
    char **line;
    char *s = dataptr;

    for (x = 0; x < num; x++, ext++) {
#ifndef VOID_SPRINTF
	s +=
#endif
	snprintf(s, data_size - (s-dataptr), ",\n\"XPMEXT %s\"", ext->name);
#ifdef VOID_SPRINTF
	s += strlen(ext->name) + 11;
#endif
	a = ext->nlines;
	for (y = 0, line = ext->lines; y < a; y++, line++) {
#ifndef VOID_SPRINTF
	    s +=
#endif
	    snprintf(s, data_size - (s-dataptr), ",\n\"%s\"", *line);
#ifdef VOID_SPRINTF
	    s += strlen(*line) + 4;
#endif
	}
    }
    strncpy(s, ",\n\"XPMENDEXT\"", data_size - (s-dataptr)-1);
    *used_size += s - dataptr + 13;
}

static int
CommentsSize(XpmInfo *info)
{
    int size = 0;

    /* 5 = 2 (for "/_*") + 3 (for "*_/\n") */
    /* wrap possible but *very* unlikely */
    if (info->hints_cmt)
	size += 5 + strlen(info->hints_cmt);

    if (info->colors_cmt)
	size += 5 + strlen(info->colors_cmt);

    if (info->pixels_cmt)
	size += 5 + strlen(info->pixels_cmt);

    return size;
}
