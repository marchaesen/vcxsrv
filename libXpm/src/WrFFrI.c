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
*  WrFFrI.c:                                                                  *
*                                                                             *
*  XPM library                                                                *
*  Write an image and possibly its mask to an XPM file                        *
*                                                                             *
*  Developed by Arnaud Le Hors                                                *
\*****************************************************************************/

/*
 * The code related to AMIGA has been added by
 * Lorens Younes (d93-hyo@nada.kth.se) 4/96
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "XpmI.h"

#ifndef NO_ZPIPE
#include "sys/wait.h"
#include "sys/types.h"
#include "unistd.h"
#include "errno.h"
#endif

#include "fcntl.h"

#ifdef FOR_MSW
#define O_WRONLY _O_WRONLY
#define O_CREAT  _O_CREAT
#define O_TRUNC  _O_TRUNC
#endif

/* MS Windows define a function called WriteFile @#%#&!!! */
LFUNC(xpmWriteFile, int, (FILE *file, XpmImage *image, const char *name,
			  XpmInfo *info));

LFUNC(WriteColors, void, (FILE *file, XpmColor *colors, unsigned int ncolors));

LFUNC(WritePixels, int, (FILE *file, unsigned int width, unsigned int height,
			 unsigned int cpp, unsigned int *pixels,
			 XpmColor *colors));

LFUNC(WriteExtensions, void, (FILE *file, XpmExtension *ext,
			      unsigned int num));

LFUNC(OpenWriteFile, int, (const char *filename, xpmData *mdata));
LFUNC(xpmDataClose, void, (xpmData *mdata));

int
XpmWriteFileFromImage(
    Display		*display,
    const char		*filename,
    XImage		*image,
    XImage		*shapeimage,
    XpmAttributes	*attributes)
{
    XpmImage xpmimage;
    XpmInfo info;
    int ErrorStatus;

    /* create an XpmImage from the image */
    ErrorStatus = XpmCreateXpmImageFromImage(display, image, shapeimage,
					     &xpmimage, attributes);
    if (ErrorStatus != XpmSuccess)
	return (ErrorStatus);

    /* write the file from the XpmImage */
    if (attributes) {
	xpmSetInfo(&info, attributes);
	ErrorStatus = XpmWriteFileFromXpmImage(filename, &xpmimage, &info);
    } else
	ErrorStatus = XpmWriteFileFromXpmImage(filename, &xpmimage, NULL);

    /* free the XpmImage */
    XpmFreeXpmImage(&xpmimage);

    return (ErrorStatus);
}

int
XpmWriteFileFromXpmImage(
    const char	*filename,
    XpmImage	*image,
    XpmInfo	*info)
{
    xpmData mdata;
    const char *name;
    char *dot, *s, new_name[BUFSIZ] = {0};
    int ErrorStatus;

    /* open file to write */
    if ((ErrorStatus = OpenWriteFile(filename, &mdata)) != XpmSuccess)
	return (ErrorStatus);

    /* figure out a name */
    if (filename) {
#ifdef VMS
	name = filename;
#else
	if (!(name = strrchr(filename, '/'))
#ifdef AMIGA
	    && !(name = strrchr(filename, ':'))
#endif
     )
	    name = filename;
	else
	    name++;
#endif
	/* let's try to make a valid C syntax name */
	if (strchr(name, '.')) {
	    strncpy(new_name, name, sizeof(new_name));
	    new_name[sizeof(new_name)-1] = '\0';
	    /* change '.' to '_' */
	    name = s = new_name;
	    while ((dot = strchr(s, '.'))) {
		*dot = '_';
		s = dot;
	    }
	}
	if (strchr(name, '-')) {
	    if (name != new_name) {
		strncpy(new_name, name, sizeof(new_name));
		new_name[sizeof(new_name)-1] = '\0';
		name = new_name;
	    }
	    /* change '-' to '_' */
	    s = new_name;
	    while ((dot = strchr(s, '-'))) {
		*dot = '_';
		s = dot;
	    }
	}
    } else
	name = "image_name";

    /* write the XpmData from the XpmImage */
    if (ErrorStatus == XpmSuccess)
	ErrorStatus = xpmWriteFile(mdata.stream.file, image, name, info);

    xpmDataClose(&mdata);

    return (ErrorStatus);
}

static int
xpmWriteFile(
    FILE	*file,
    XpmImage	*image,
    const char	*name,
    XpmInfo	*info)
{
    /* calculation variables */
    unsigned int cmts, extensions;
    int ErrorStatus;

    cmts = info && (info->valuemask & XpmComments);
    extensions = info && (info->valuemask & XpmExtensions)
	&& info->nextensions;

    /* print the header line */
    fprintf(file, "/* XPM */\nstatic char * %s[] = {\n", name);

    /* print the hints line */
    if (cmts && info->hints_cmt)
	fprintf(file, "/*%s*/\n", info->hints_cmt);

    fprintf(file, "\"%d %d %d %d", image->width, image->height,
	    image->ncolors, image->cpp);

    if (info && (info->valuemask & XpmHotspot))
	fprintf(file, " %d %d", info->x_hotspot, info->y_hotspot);

    if (extensions)
	fprintf(file, " XPMEXT");

    fprintf(file, "\",\n");

    /* print colors */
    if (cmts && info->colors_cmt)
	fprintf(file, "/*%s*/\n", info->colors_cmt);

    WriteColors(file, image->colorTable, image->ncolors);

    /* print pixels */
    if (cmts && info->pixels_cmt)
	fprintf(file, "/*%s*/\n", info->pixels_cmt);

    ErrorStatus = WritePixels(file, image->width, image->height, image->cpp,
			      image->data, image->colorTable);
    if (ErrorStatus != XpmSuccess)
	return (ErrorStatus);

    /* print extensions */
    if (extensions)
	WriteExtensions(file, info->extensions, info->nextensions);

    /* close the array */
    fprintf(file, "};\n");

    return (XpmSuccess);
}

static void
WriteColors(
    FILE		*file,
    XpmColor		*colors,
    unsigned int	 ncolors)
{
    unsigned int a, key;
    char *s;
    char **defaults;

    for (a = 0; a < ncolors; a++, colors++) {

	defaults = (char **) colors;
	fprintf(file, "\"%s", *defaults++);

	for (key = 1; key <= NKEYS; key++, defaults++) {
	    if ((s = *defaults))
		fprintf(file, "\t%s %s", xpmColorKeys[key - 1], s);
	}
	fprintf(file, "\",\n");
    }
}


static int
WritePixels(
    FILE		*file,
    unsigned int	 width,
    unsigned int	 height,
    unsigned int	 cpp,
    unsigned int	*pixels,
    XpmColor		*colors)
{
    char *s, *p, *buf;
    unsigned int x, y, h;

    h = height - 1;
    if (cpp != 0 && width >= (UINT_MAX - 3)/cpp)
	return XpmNoMemory;
    p = buf = (char *) XpmMalloc(width * cpp + 3);
    if (!buf)
	return (XpmNoMemory);
    *buf = '"';
    p++;
    for (y = 0; y < h; y++) {
	s = p;
	for (x = 0; x < width; x++, pixels++) {
	    strncpy(s, colors[*pixels].string, cpp);
	    s += cpp;
	}
	*s++ = '"';
	*s = '\0';
	fprintf(file, "%s,\n", buf);
    }
    /* duplicate some code to avoid a test in the loop */
    s = p;
    for (x = 0; x < width; x++, pixels++) {
	strncpy(s, colors[*pixels].string, cpp);
	s += cpp;
    }
    *s++ = '"';
    *s = '\0';
    fprintf(file, "%s", buf);

    XpmFree(buf);
    return (XpmSuccess);
}

static void
WriteExtensions(
    FILE		*file,
    XpmExtension	*ext,
    unsigned int	 num)
{
    unsigned int x, y, n;
    char **line;

    for (x = 0; x < num; x++, ext++) {
	fprintf(file, ",\n\"XPMEXT %s\"", ext->name);
	n = ext->nlines;
	for (y = 0, line = ext->lines; y < n; y++, line++)
	    fprintf(file, ",\n\"%s\"", *line);
    }
    fprintf(file, ",\n\"XPMENDEXT\"");
}


#ifndef NO_ZPIPE
HFUNC(xpmPipeThrough, FILE*, (int fd,
			     const char* cmd,
			     const char* arg1,
			     const char* mode));
#endif

/*
 * open the given file to be written as an xpmData which is returned
 */
static int
OpenWriteFile(
    const char	*filename,
    xpmData	*mdata)
{
    if (!filename) {
	mdata->stream.file = (stdout);
	mdata->type = XPMFILE;
    } else {
#ifndef NO_ZPIPE
	size_t len;
#endif
	int fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC, 0644);
	if ( fd < 0 )
	    return(XpmOpenFailed);
#ifndef NO_ZPIPE
	len = strlen(filename);
	if (len > 2 && !strcmp(".Z", filename + (len - 2))) {
#ifdef XPM_PATH_COMPRESS
	    mdata->stream.file = xpmPipeThrough(fd, XPM_PATH_COMPRESS, NULL, "w");
#else
	    mdata->stream.file = NULL;
#endif
	    mdata->type = XPMPIPE;
	} else if (len > 3 && !strcmp(".gz", filename + (len - 3))) {
	    mdata->stream.file = xpmPipeThrough(fd, XPM_PATH_GZIP, "-q", "w");
	    mdata->type = XPMPIPE;
	} else
#endif
	{
	    mdata->stream.file = fdopen(fd, "w");
	    mdata->type = XPMFILE;
	}
	if (!mdata->stream.file) {
	    close(fd);
	    return (XpmOpenFailed);
	}
    }
    return (XpmSuccess);
}

/*
 * close the file related to the xpmData if any
 */
static void
xpmDataClose(xpmData *mdata)
{
    if (mdata->stream.file != (stdout))
	fclose(mdata->stream.file);
}

