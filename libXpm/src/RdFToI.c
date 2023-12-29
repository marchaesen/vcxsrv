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
*  RdFToI.c:                                                                  *
*                                                                             *
*  XPM library                                                                *
*  Parse an XPM file and create the image and possibly its mask               *
*                                                                             *
*  Developed by Arnaud Le Hors                                                *
\*****************************************************************************/

/* October 2004, source code review by Thomas Biege <thomas@suse.de> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "XpmI.h"
#ifndef NO_ZPIPE
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#ifdef FOR_MSW
#include <fcntl.h>
#endif
#endif

LFUNC(OpenReadFile, int, (const char *filename, xpmData *mdata));
LFUNC(xpmDataClose, void, (xpmData *mdata));

HFUNC(xpmPipeThrough, FILE*, (int fd,
			     const char *cmd,
			     const char *arg1,
			     const char *mode));

#ifndef CXPMPROG
int
XpmReadFileToImage(
    Display		 *display,
    const char		 *filename,
    XImage		**image_return,
    XImage		**shapeimage_return,
    XpmAttributes	 *attributes)
{
    XpmImage image;
    XpmInfo info;
    int ErrorStatus;
    xpmData mdata;

    xpmInitXpmImage(&image);
    xpmInitXpmInfo(&info);

    /* open file to read */
    if ((ErrorStatus = OpenReadFile(filename, &mdata)) != XpmSuccess)
	return (ErrorStatus);

    /* create the XImage from the XpmData */
    if (attributes) {
	xpmInitAttributes(attributes);
	xpmSetInfoMask(&info, attributes);
	ErrorStatus = xpmParseDataAndCreate(display, &mdata,
					    image_return, shapeimage_return,
					    &image, &info, attributes);
    } else
	ErrorStatus = xpmParseDataAndCreate(display, &mdata,
					    image_return, shapeimage_return,
					    &image, NULL, attributes);
    if (attributes) {
	if (ErrorStatus >= 0)		/* no fatal error */
	    xpmSetAttributes(attributes, &image, &info);
	XpmFreeXpmInfo(&info);
    }

    xpmDataClose(&mdata);
    /* free the XpmImage */
    XpmFreeXpmImage(&image);

    return (ErrorStatus);
}

int
XpmReadFileToXpmImage(
    const char	*filename,
    XpmImage	*image,
    XpmInfo	*info)
{
    xpmData mdata;
    int ErrorStatus;

    /* init returned values */
    xpmInitXpmImage(image);
    xpmInitXpmInfo(info);

    /* open file to read */
    if ((ErrorStatus = OpenReadFile(filename, &mdata)) != XpmSuccess)
	return (ErrorStatus);

    /* create the XpmImage from the XpmData */
    ErrorStatus = xpmParseData(&mdata, image, info);

    xpmDataClose(&mdata);

    return (ErrorStatus);
}
#endif /* CXPMPROG */

#ifndef NO_ZPIPE
/* Do not depend on errno after read_through */
FILE*
xpmPipeThrough(
    int		 fd,
    const char	*cmd,
    const char	*arg1,
    const char	*mode)
{
    FILE* fp;
    int status, fds[2], in = 0, out = 1;
    pid_t pid;
    if ( 'w' == *mode )
	out = 0, in = 1;
    if ( pipe(fds) < 0 )
	return NULL;
    pid = fork();
    if ( pid < 0 )
	goto fail1;
    if ( 0 == pid )
    {
	close(fds[in]);
	if ( dup2(fds[out], out) < 0 )
	    goto err;
	close(fds[out]);
	if ( dup2(fd, in) < 0 )
	    goto err;
	close(fd);
	pid = fork();
	if ( pid < 0 )
	    goto err;
	if ( 0 == pid )
	{
#ifdef HAVE_CLOSEFROM
	    closefrom(3);
#elif defined(HAVE_CLOSE_RANGE)
# ifdef CLOSE_RANGE_UNSHARE
#  define close_range_flags CLOSE_RANGE_UNSHARE
# else
#  define close_range_flags 0
#endif
	    close_range(3, ~0U, close_range_flags);
#endif
	    execl(cmd, cmd, arg1, (char *)NULL);
	    perror(cmd);
	    goto err;
	}
	_exit(0);
    err:
	_exit(1);
    }
    close(fds[out]);
    /* calling process: wait for first child */
    while ( waitpid(pid, &status, 0) < 0 && EINTR == errno )
	;
    if ( WIFSIGNALED(status) ||
	 (WIFEXITED(status) && WEXITSTATUS(status) != 0) )
	goto fail2;
    fp = fdopen(fds[in], mode);
    if ( !fp )
	goto fail2;
    close(fd); /* still open in 2nd child */
    return fp;
fail1:
    close(fds[out]);
fail2:
    close(fds[in]);
    return NULL;
}
#endif

/*
 * open the given file to be read as an xpmData which is returned.
 */
static int
OpenReadFile(
    const char	*filename,
    xpmData	*mdata)
{
    if (!filename) {
	mdata->stream.file = (stdin);
	mdata->type = XPMFILE;
    } else {
	int fd = open(filename, O_RDONLY | O_CLOEXEC);
#if defined(NO_ZPIPE)
	if ( fd < 0 )
	    return XpmOpenFailed;
#else
	const char* ext = NULL;
	if ( fd >= 0 )
	    ext = strrchr(filename, '.');
#ifdef STAT_ZFILE /* searching for z-files if the given name not found */
	else
	{
	    size_t len = strlen(filename);
	    char *compressfile = (char *) XpmMalloc(len + 4);
	    if ( !compressfile )
		return (XpmNoMemory);
	    strcpy(compressfile, filename);
	    strcpy(compressfile + len, ext = ".Z");
	    fd = open(compressfile, O_RDONLY | O_CLOEXEC);
	    if ( fd < 0 )
	    {
		strcpy(compressfile + len, ext = ".gz");
		fd = open(compressfile, O_RDONLY | O_CLOEXEC);
		if ( fd < 0 )
		{
		    XpmFree(compressfile);
		    return XpmOpenFailed;
		}
	    }
	    XpmFree(compressfile);
	}
#endif
	if ( ext && !strcmp(ext, ".Z") )
	{
	    mdata->type = XPMPIPE;
#ifdef XPM_PATH_UNCOMPRESS
	    mdata->stream.file = xpmPipeThrough(fd, XPM_PATH_UNCOMPRESS, "-c", "r");
#else
	    mdata->stream.file = xpmPipeThrough(fd, XPM_PATH_GZIP, "-dqc", "r");
#endif
	}
	else if ( ext && !strcmp(ext, ".gz") )
	{
	    mdata->type = XPMPIPE;
	    mdata->stream.file = xpmPipeThrough(fd, XPM_PATH_GZIP, "-dqc", "r");
	}
	else
#endif /* z-files */
	{
	    mdata->type = XPMFILE;
	    mdata->stream.file = fdopen(fd, "r");
	}
	if (!mdata->stream.file)
	{
	    close(fd);
	    return (XpmOpenFailed);
	}
    }
    mdata->CommentLength = 0;
#ifdef CXPMPROG
    mdata->lineNum = 0;
    mdata->charNum = 0;
#endif
    return (XpmSuccess);
}

/*
 * close the file related to the xpmData if any
 */
static void
xpmDataClose(xpmData *mdata)
{
    if (mdata->stream.file != (stdin))
	fclose(mdata->stream.file);
}
