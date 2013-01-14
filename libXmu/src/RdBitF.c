/*

Copyright 1988, 1998  The Open Group

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
 * This file contains miscellaneous utility routines and is not part of the
 * Xlib standard.
 *
 * Public entry points:
 *
 *     XmuReadBitmapData		read data from FILE descriptor
 *     XmuReadBitmapDataFromFile	read X10 or X11 format bitmap files
 *					and return data
 *
 * Note that this file and ../X/XRdBitF.c look very similar....  Keep them
 * that way (but don't use common source code so that people can have one
 * without the other).
 */


/*
 * Based on an optimized version provided by Jim Becker, Auguest 5, 1988.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <stdio.h>
#include <ctype.h>
#include <X11/Xmu/Drawing.h>
#ifdef WIN32
#include <X11/Xwindows.h>
#endif

#define MAX_SIZE 255

/*
 * Prototypes
 */
static void initHexTable(void);
static int NextInt(FILE*);

/* shared data for the image read/parse logic */
static short hexTable[256];		/* conversion value */
static Bool initialized = False;	/* easier to fill in at run time */


/*
 *	Table index for the hex values. Initialized once, first time.
 *	Used for translation value or delimiter significance lookup.
 */
static void
initHexTable(void)
{
    /*
     * We build the table at run time for several reasons:
     *
     *     1.  portable to non-ASCII machines.
     *     2.  still reentrant since we set the init flag after setting table.
     *     3.  easier to extend.
     *     4.  less prone to bugs.
     */
    hexTable['0'] = 0;	hexTable['1'] = 1;
    hexTable['2'] = 2;	hexTable['3'] = 3;
    hexTable['4'] = 4;	hexTable['5'] = 5;
    hexTable['6'] = 6;	hexTable['7'] = 7;
    hexTable['8'] = 8;	hexTable['9'] = 9;
    hexTable['A'] = 10;	hexTable['B'] = 11;
    hexTable['C'] = 12;	hexTable['D'] = 13;
    hexTable['E'] = 14;	hexTable['F'] = 15;
    hexTable['a'] = 10;	hexTable['b'] = 11;
    hexTable['c'] = 12;	hexTable['d'] = 13;
    hexTable['e'] = 14;	hexTable['f'] = 15;

    /* delimiters of significance are flagged w/ negative value */
    hexTable[' '] = -1;	hexTable[','] = -1;
    hexTable['}'] = -1;	hexTable['\n'] = -1;
    hexTable['\t'] = -1;

    initialized = True;
}

/*
 *	read next hex value in the input stream, return -1 if EOF
 */
static int
NextInt(FILE *fstream)
{
    int	ch;
    int	value = 0;
    int gotone = 0;
    int done = 0;

    /* loop, accumulate hex value until find delimiter  */
    /* skip any initial delimiters found in read stream */

    while (!done) {
	ch = getc(fstream);
	if (ch == EOF) {
	    value	= -1;
	    done++;
	} else {
	    /* trim high bits, check type and accumulate */
	    ch &= 0xff;
	    if (isascii(ch) && isxdigit(ch)) {
		value = (value << 4) + hexTable[ch];
		gotone++;
	    } else if ((hexTable[ch]) < 0 && gotone)
	      done++;
	}
    }
    return value;
}


/*
 * The data returned by the following routine is always in left-most byte
 * first and left-most bit first.  If it doesn't return BitmapSuccess then
 * its arguments won't have been touched.  This routine should look as much
 * like the Xlib routine XReadBitmapfile as possible.
 */
int
XmuReadBitmapData(FILE *fstream, unsigned int *width, unsigned int *height,
		  unsigned char **datap, int *x_hot, int *y_hot)
{
    unsigned char *data = NULL;		/* working variable */
    char line[MAX_SIZE];		/* input line from file */
    int size;				/* number of bytes of data */
    char name_and_type[MAX_SIZE];	/* an input line */
    char *type;				/* for parsing */
    int value;				/* from an input line */
    int version10p;			/* boolean, old format */
    int padding;			/* to handle alignment */
    int bytes_per_line;			/* per scanline of data */
    unsigned int ww = 0;		/* width */
    unsigned int hh = 0;		/* height */
    int hx = -1;			/* x hotspot */
    int hy = -1;			/* y hotspot */

#undef  Xmalloc /* see MALLOC_0_RETURNS_NULL in Xlibint.h */
#define Xmalloc(size) malloc(size)

    /* first time initialization */
    if (initialized == False) initHexTable();

    /* error cleanup and return macro	*/
#define	RETURN(code) { if (data) free (data); return code; }

    while (fgets(line, MAX_SIZE, fstream)) {
	if (strlen(line) == MAX_SIZE-1) {
	    RETURN (BitmapFileInvalid);
	}
	if (sscanf(line,"#define %s %d",name_and_type,&value) == 2) {
	    if (!(type = strrchr(name_and_type, '_')))
	      type = name_and_type;
	    else
	      type++;

	    if (!strcmp("width", type))
	      ww = (unsigned int) value;
	    if (!strcmp("height", type))
	      hh = (unsigned int) value;
	    if (!strcmp("hot", type)) {
		if (type-- == name_and_type || type-- == name_and_type)
		  continue;
		if (!strcmp("x_hot", type))
		  hx = value;
		if (!strcmp("y_hot", type))
		  hy = value;
	    }
	    continue;
	}

	if (sscanf(line, "static short %s = {", name_and_type) == 1)
	  version10p = 1;
	else if (sscanf(line,"static unsigned char %s = {",name_and_type) == 1)
	  version10p = 0;
	else if (sscanf(line, "static char %s = {", name_and_type) == 1)
	  version10p = 0;
	else
	  continue;

	if (!(type = strrchr(name_and_type, '_')))
	  type = name_and_type;
	else
	  type++;

	if (strcmp("bits[]", type))
	  continue;

	if (!ww || !hh)
	  RETURN (BitmapFileInvalid);

	if ((ww % 16) && ((ww % 16) < 9) && version10p)
	  padding = 1;
	else
	  padding = 0;

	bytes_per_line = (ww+7)/8 + padding;

	size = bytes_per_line * hh;
	data = (unsigned char *) Xmalloc ((unsigned int) size);
	if (!data)
	  RETURN (BitmapNoMemory);

	if (version10p) {
	    unsigned char *ptr;
	    int bytes;

	    for (bytes=0, ptr=data; bytes<size; (bytes += 2)) {
		if ((value = NextInt(fstream)) < 0)
		  RETURN (BitmapFileInvalid);
		*(ptr++) = value;
		if (!padding || ((bytes+2) % bytes_per_line))
		  *(ptr++) = value >> 8;
	    }
	} else {
	    unsigned char *ptr;
	    int bytes;

	    for (bytes=0, ptr=data; bytes<size; bytes++, ptr++) {
		if ((value = NextInt(fstream)) < 0)
		  RETURN (BitmapFileInvalid);
		*ptr=value;
	    }
	}
	break;
    }					/* end while */

    if (data == NULL) {
	RETURN (BitmapFileInvalid);
    }

    *datap = data;
    data = NULL;
    *width = ww;
    *height = hh;
    if (x_hot) *x_hot = hx;
    if (y_hot) *y_hot = hy;

    RETURN (BitmapSuccess);
}

#if defined(WIN32)
static int
access_file(char *path, char *pathbuf, int len_pathbuf, char **pathret)
{
    if (access (path, F_OK) == 0) {
	if (strlen (path) < len_pathbuf)
	    *pathret = pathbuf;
	else
	    *pathret = malloc (strlen (path) + 1);
	if (*pathret) {
	    strcpy (*pathret, path);
	    return 1;
	}
    }
    return 0;
}

static int
AccessFile(char *path, char *pathbuf, int len_pathbuf, char **pathret)
{
#ifndef MAX_PATH
#define MAX_PATH 512
#endif

    unsigned long drives;
    int i, len;
    char* drive;
    char buf[MAX_PATH];
    char* bufp;

    /* just try the "raw" name first and see if it works */
    if (access_file (path, pathbuf, len_pathbuf, pathret))
	return 1;

    /* try the places set in the environment */
    drive = getenv ("_XBASEDRIVE");
    if (!drive)
	drive = "C:";
    len = strlen (drive) + strlen (path);
    if (len < MAX_PATH) bufp = buf;
    else bufp = malloc (len + 1);
    strcpy (bufp, drive);
    strcat (bufp, path);
    if (access_file (bufp, pathbuf, len_pathbuf, pathret)) {
	if (bufp != buf) free (bufp);
	return 1;
    }

    /* one last place to look */
    drive = getenv ("HOMEDRIVE");
    if (drive) {
	len = strlen (drive) + strlen (path);
	if (len < MAX_PATH) bufp = buf;
	else bufp = malloc (len + 1);
	strcpy (bufp, drive);
	strcat (bufp, path);
	if (access_file (bufp, pathbuf, len_pathbuf, pathret)) {
	    if (bufp != buf) free (bufp);
	    return 1;
	}
    }

    /* does OS/2 (with or with gcc-emx) have getdrives? */
    /* tried everywhere else, go fishing */
#define C_DRIVE ('C' - 'A')
#define Z_DRIVE ('Z' - 'A')
    drives = _getdrives ();
    for (i = C_DRIVE; i <= Z_DRIVE; i++) { /* don't check on A: or B: */
	if ((1 << i) & drives) {
	    len = 2 + strlen (path);
	    if (len < MAX_PATH) bufp = buf;
	    else bufp = malloc (len + 1);
	    *bufp = 'A' + i;
	    *(bufp + 1) = ':';
	    *(bufp + 2) = '\0';
	    strcat (bufp, path);
	    if (access_file (bufp, pathbuf, len_pathbuf, pathret)) {
		if (bufp != buf) free (bufp);
		return 1;
	    }
	}
    }
    return 0;
}

FILE *
fopen_file(char *path, char *mode)
{
    char buf[MAX_PATH];
    char* bufp;
    void* ret = NULL;
    UINT olderror = SetErrorMode (SEM_FAILCRITICALERRORS);

    if (AccessFile (path, buf, MAX_PATH, &bufp))
	ret = fopen (bufp, mode);

    (void) SetErrorMode (olderror);

    if (bufp != buf) free (bufp);

    return ret;
}

#else
#define fopen_file fopen
#endif


int
XmuReadBitmapDataFromFile(_Xconst char *filename, unsigned int *width,
			       unsigned int *height, unsigned char **datap,
			       int *x_hot, int *y_hot)
{
    FILE *fstream;
    int status;

    if ((fstream = fopen_file (filename, "r")) == NULL) {
	return BitmapOpenFailed;
    }
    status = XmuReadBitmapData(fstream, width, height, datap, x_hot, y_hot);
    fclose (fstream);
    return status;
}
