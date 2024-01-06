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
*  CrDataFI.c:                                                                *
*                                                                             *
*  XPM library                                                                *
*  Scan an image and possibly its mask and create an XPM array                *
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

LFUNC(CreateColors, int, (char **dataptr, unsigned int *data_size,
			  XpmColor *colors, unsigned int ncolors,
			  unsigned int cpp));

LFUNC(CreatePixels, void, (char **dataptr, unsigned int data_size,
			   unsigned int width,
			   unsigned int height, unsigned int cpp,
			   unsigned int *pixels, XpmColor *colors));

LFUNC(CountExtensions, int, (XpmExtension *ext, unsigned int num,
			      unsigned int *ext_size,
			      unsigned int *ext_nlines));

LFUNC(CreateExtensions, void, (char **dataptr, unsigned int data_size,
			       unsigned int offset,
			       XpmExtension *ext, unsigned int num,
			       unsigned int ext_nlines));

int
XpmCreateDataFromImage(
    Display		  *display,
    char		***data_return,
    XImage		  *image,
    XImage		  *shapeimage,
    XpmAttributes	  *attributes)
{
    XpmImage xpmimage;
    XpmInfo info;
    int ErrorStatus;

    /* initialize return value */
    if (data_return)
	*data_return = NULL;

    /* create an XpmImage from the image */
    ErrorStatus = XpmCreateXpmImageFromImage(display, image, shapeimage,
					     &xpmimage, attributes);
    if (ErrorStatus != XpmSuccess)
	return (ErrorStatus);

    /* create the data from the XpmImage */
    if (attributes) {
	xpmSetInfo(&info, attributes);
	ErrorStatus = XpmCreateDataFromXpmImage(data_return, &xpmimage, &info);
    } else
	ErrorStatus = XpmCreateDataFromXpmImage(data_return, &xpmimage, NULL);

    /* free the XpmImage */
    XpmFreeXpmImage(&xpmimage);

    return (ErrorStatus);
}

#undef RETURN
#define RETURN(status) \
do \
{ \
      ErrorStatus = status; \
      goto exit; \
} while(0)

int
XpmCreateDataFromXpmImage(
    char	***data_return,
    XpmImage	  *image,
    XpmInfo	  *info)
{
    /* calculation variables */
    int ErrorStatus;
    char buf[BUFSIZ];
    char **header = NULL, **data, **sptr, **sptr2, *s;
    unsigned int header_size, header_nlines;
    unsigned int data_size, data_nlines;
    unsigned int extensions = 0, ext_size = 0, ext_nlines = 0;
    unsigned int offset, l, n;

    *data_return = NULL;

    extensions = info && (info->valuemask & XpmExtensions)
	&& info->nextensions;

    /* compute the number of extensions lines and size */
    if (extensions)
	if (CountExtensions(info->extensions, info->nextensions,
			&ext_size, &ext_nlines))
	    return(XpmNoMemory);

    /*
     * alloc a temporary array of char pointer for the header section which
     * is the hints line + the color table lines
     */
    header_nlines = 1 + image->ncolors; /* this may wrap and/or become 0 */

    /* 2nd check superfluous if we do not need header_nlines any further */
    if(header_nlines <= image->ncolors ||
       header_nlines >= UINT_MAX / sizeof(char *))
    	return(XpmNoMemory);

    header_size = sizeof(char *) * header_nlines;
    if (header_size >= UINT_MAX / sizeof(char *))
	return (XpmNoMemory);
    header = (char **) XpmCalloc(header_size, sizeof(char *)); /* can we trust image->ncolors */
    if (!header)
	return (XpmNoMemory);

    /* print the hints line */
    s = buf;
#ifndef VOID_SPRINTF
    s +=
#endif
    sprintf(s, "%d %d %d %d", image->width, image->height,
	    image->ncolors, image->cpp);
#ifdef VOID_SPRINTF
    s += strlen(s);
#endif

    if (info && (info->valuemask & XpmHotspot)) {
#ifndef VOID_SPRINTF
	s +=
#endif
	sprintf(s, " %d %d", info->x_hotspot, info->y_hotspot);
#ifdef VOID_SPRINTF
	s += strlen(s);
#endif
    }
    if (extensions) {
	strcpy(s, " XPMEXT");
	s += 7;
    }
    l = s - buf + 1;
    *header = (char *) XpmMalloc(l);
    if (!*header)
	RETURN(XpmNoMemory);
    header_size += l;
    strcpy(*header, buf);

    /* print colors */
    ErrorStatus = CreateColors(header + 1, &header_size,
			       image->colorTable, image->ncolors, image->cpp);

    if (ErrorStatus != XpmSuccess)
	RETURN(ErrorStatus);

    /* now we know the size needed, alloc the data and copy the header lines */
    offset = image->width * image->cpp + 1;

    if(offset <= image->width || offset <= image->cpp)
	RETURN(XpmNoMemory);

    if (image->height > UINT_MAX - ext_nlines ||
	image->height + ext_nlines >= UINT_MAX / sizeof(char *))
	RETURN(XpmNoMemory);
    data_size = (image->height + ext_nlines) * sizeof(char *);

    if (image->height > UINT_MAX / offset ||
        image->height * offset > UINT_MAX - data_size)
	RETURN(XpmNoMemory);
    data_size += image->height * offset;

    if (header_size > UINT_MAX - ext_size ||
	header_size + ext_size >= (UINT_MAX - data_size) )
	RETURN(XpmNoMemory);
    data_size += header_size + ext_size;

    data = (char **) XpmMalloc(data_size);
    if (!data)
	RETURN(XpmNoMemory);

    data_nlines = header_nlines + image->height + ext_nlines;
    *data = (char *) (data + data_nlines);

    /* can header have less elements then n suggests? */
    n = image->ncolors;
    for (l = 0, sptr = data, sptr2 = header; l <= n && sptr && sptr2; l++, sptr++, sptr2++) {
	strcpy(*sptr, *sptr2);
	*(sptr + 1) = *sptr + strlen(*sptr2) + 1;
    }

    /* print pixels */
    data[header_nlines] = (char *) data + header_size
	+ (image->height + ext_nlines) * sizeof(char *);

    CreatePixels(data + header_nlines, data_size-header_nlines, image->width, image->height,
		 image->cpp, image->data, image->colorTable);

    /* print extensions */
    if (extensions)
	CreateExtensions(data + header_nlines + image->height - 1,
			 data_size - header_nlines - image->height + 1, offset,
			 info->extensions, info->nextensions,
			 ext_nlines);

    *data_return = data;
    ErrorStatus = XpmSuccess;

/* exit point, free only locally allocated variables */
exit:
    if (header) {
	for (l = 0; l < header_nlines; l++) {
	    if (header[l])
		XpmFree(header[l]);
	}
	XpmFree(header);
    }
    return(ErrorStatus);
}

static int
CreateColors(
    char		**dataptr,
    unsigned int	 *data_size,
    XpmColor		 *colors,
    unsigned int	  ncolors,
    unsigned int	  cpp)
{
    char buf[BUFSIZ];
    unsigned int a, key, l;
    char *s, *s2;
    char **defaults;

    /* can ncolors be trusted here? */
    for (a = 0; a < ncolors; a++, colors++, dataptr++) {

	defaults = (char **) colors;
	if(sizeof(buf) <= cpp)
	    return(XpmNoMemory);
	strncpy(buf, *defaults++, cpp);
	s = buf + cpp;

	if(sizeof(buf) <= (s-buf))
		return XpmNoMemory;

	for (key = 1; key <= NKEYS; key++, defaults++) {
	    if ((s2 = *defaults)) {
#ifndef VOID_SPRINTF
		s +=
#endif
		/* assume C99 compliance */
			snprintf(s, sizeof(buf)-(s-buf), "\t%s %s", xpmColorKeys[key - 1], s2);
#ifdef VOID_SPRINTF
		s += strlen(s);
#endif
		/* does s point out-of-bounds? */
		if(sizeof(buf) < (s-buf))
			return XpmNoMemory;
	    }
	}
	/* what about using strdup()? */
	l = s - buf + 1;
	s = (char *) XpmMalloc(l);
	if (!s)
	    return (XpmNoMemory);
	*data_size += l;
	*dataptr = strcpy(s, buf);
    }
    return (XpmSuccess);
}

static void
CreatePixels(
    char		**dataptr,
    unsigned int	  data_size,
    unsigned int	  width,
    unsigned int	  height,
    unsigned int	  cpp,
    unsigned int	 *pixels,
    XpmColor		 *colors)
{
    char *s;
    unsigned int x, y, h, offset;

    if(height <= 1)
    	return;

    h = height - 1;

    offset = width * cpp + 1;

    if(offset <= width || offset <= cpp)
    	return;

    /* why trust h? */
    for (y = 0; y < h; y++, dataptr++) {
	s = *dataptr;
	/* why trust width? */
	for (x = 0; x < width; x++, pixels++) {
	    if(cpp > (data_size - (s - *dataptr)))
	    	return;
	    strncpy(s, colors[*pixels].string, cpp); /* why trust pixel? */
	    s += cpp;
	}
	*s = '\0';
	if(offset > data_size)
		return;
	*(dataptr + 1) = *dataptr + offset;
    }
    /* duplicate some code to avoid a test in the loop */
    s = *dataptr;
    /* why trust width? */
    for (x = 0; x < width; x++, pixels++) {
	if(cpp > data_size - (s - *dataptr))
	    	return;
	strncpy(s, colors[*pixels].string, cpp); /* why should we trust *pixel? */
	s += cpp;
    }
    *s = '\0';
}

static int
CountExtensions(
    XpmExtension	*ext,
    unsigned int	 num,
    unsigned int	*ext_size,
    unsigned int	*ext_nlines)
{
    size_t len;
    unsigned int x, y, a, size, nlines;
    char **line;

    size = 0;
    nlines = 0;
    for (x = 0; x < num; x++, ext++) {
	/* 1 for the name */
	if (ext->nlines == UINT_MAX || nlines > UINT_MAX - ext->nlines - 1)
	    return (1);
	nlines += ext->nlines + 1;
	/* 8 = 7 (for "XPMEXT ") + 1 (for 0) */
	len = strlen(ext->name) + 8;
	if (len > UINT_MAX - size)
	    return (1);
	size += len;
	a = ext->nlines;
	for (y = 0, line = ext->lines; y < a; y++, line++) {
	    len = strlen(*line) + 1;
	    if (len > UINT_MAX - size)
		return (1);
	    size += len;
	}
    }
    if (size > UINT_MAX - 10 || nlines > UINT_MAX - 1)
	return (1);
    /* 10 and 1 are for the ending "XPMENDEXT" */
    *ext_size = size + 10;
    *ext_nlines = nlines + 1;
    return (0);
}

static void
CreateExtensions(
    char		**dataptr,
    unsigned int	  data_size,
    unsigned int	  offset,
    XpmExtension	 *ext,
    unsigned int	  num,
    unsigned int	  ext_nlines)
{
    unsigned int x, y, a, b;
    char **line;

    *(dataptr + 1) = *dataptr + offset;
    dataptr++;
    a = 0;
    for (x = 0; x < num; x++, ext++) {
	snprintf(*dataptr, data_size, "XPMEXT %s", ext->name);
	a++;
	if (a < ext_nlines)
	    *(dataptr + 1) = *dataptr + strlen(ext->name) + 8;
	dataptr++;
	b = ext->nlines; /* can we trust these values? */
	for (y = 0, line = ext->lines; y < b; y++, line++) {
	    strcpy(*dataptr, *line);
	    a++;
	    if (a < ext_nlines)
		*(dataptr + 1) = *dataptr + strlen(*line) + 1;
	    dataptr++;
	}
    }
    strcpy(*dataptr, "XPMENDEXT");
}
