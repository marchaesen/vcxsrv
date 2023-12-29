/*
 * Copyright (C) 1998 Arnaud LE HORS
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
 * Arnaud LE HORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Arnaud LE HORS shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from Arnaud LE HORS.
 */

/*****************************************************************************\
* cxpm.c:                                                                     *
*                                                                             *
*  Check XPM File program                                                     *
*                                                                             *
*  Developed by Arnaud Le Hors                                                *
\*****************************************************************************/

#define CXPMPROG

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "XpmI.h"
#ifdef USE_GETTEXT
#include <locale.h>
#include <libintl.h>
#else
#define gettext(a) (a)
#endif

#undef xpmGetC
#define xpmGetC(data) sGetc(data, data->stream.file)
#define Getc sGetc
#define Ungetc sUngetc


/*
 * special getc and ungetc counting read lines and characters
 * note that 's' could stand both for "special" and "slow" ;-)
 */
static int
sGetc(xpmData *data, FILE *file)
{
    int c = getc(data->stream.file);
    if (c == '\n') {
        data->lineNum++;
        data->charNum = 0;
    } else if (c != EOF) {
        data->charNum++;
    }
    return c;
}

static void
sUngetc(xpmData *data, int c, FILE *file)
{
    ungetc(c, data->stream.file);
    if (c == '\n') {
        data->lineNum--;
        data->charNum = 0;
    } else if (c != EOF) {
        data->charNum--;
    }
}

/* include all the code we need (yeah, I know, quite ugly...) */
#include "data.c"
#include "parse.c"
#include "RdFToI.c"	/* only for OpenReadFile and xpmDataClose */
#include "hashtab.c"
#include "misc.c"
#include "Attrib.c"
#include "Image.c"

static void
ErrorMessage(
    int ErrorStatus,
    xpmData *data)

{
    char *error = NULL;

    switch (ErrorStatus) {
    case XpmSuccess:
	return;
    case XpmOpenFailed:
	/* L10N_Comments : Error produced when filename does not exist
	   or insufficient permissions to open (i.e. cxpm /no/such/file ) */
	error = gettext("Cannot open file");
	break;
    case XpmFileInvalid:
	/* L10N_Comments : Error produced when filename can be read, but
	   is not an XPM file (i.e. cxpm /dev/null ) */
	error = gettext("Invalid XPM file");
	break;
    case XpmNoMemory:
	/* L10N_Comments : Error produced when filename can be read, but
	   is too big for memory
	   (i.e. limit datasize 32 ; cxpm /usr/dt/backdrops/Crochet.pm ) */
	error = gettext("Not enough memory");
	break;
    case XpmColorFailed:
	/* L10N_Comments : Error produced when filename can be read, but
	   contains an invalid color specification (need to create test case)*/
	error = gettext("Failed to parse color");
	break;
    }

    if (error) {
	/* L10N_Comments : Wrapper around above Xpm errors - %s is
	   replaced with the contents of the error message retrieved above */
	fprintf(stderr, gettext("Xpm Error: %s.\n"), error);
	if (ErrorStatus == XpmFileInvalid && data)
	/* L10N_Comments : Error produced when filename can be read, but
	   is not an XPM file (i.e. cxpm /dev/null ) */
	  fprintf(stderr, gettext("Error found line %d near character %d\n"),
		  data->lineNum + 1,
		  data->charNum + 1);
	exit(1);
    }
}

int
main(int argc, char **argv)
{
    XpmImage image;
    char *filename;
    int ErrorStatus;
    xpmData data;

#ifdef USE_GETTEXT
    setlocale(LC_ALL,"");
    bindtextdomain("cxpm",LOCALEDIR);
    textdomain("cxpm");
#endif

    if (argc > 1) {
        if (!strcmp(argv[1], "-?") || !strncmp(argv[1], "-h", 2)) {
	    /* L10N_Comments : Usage message produced by running cxpm -h
		%s will be replaced by argv[0] (program name) */
	    fprintf(stderr, gettext("Usage: %s [filename]\n"), argv[0]);
	    exit(1);
	}
        filename = argv[1];
    } else {
        filename = NULL;
    }

    xpmInitXpmImage(&image);

    if ((ErrorStatus = OpenReadFile(filename, &data)) != XpmSuccess)
	ErrorMessage(ErrorStatus, NULL);

    ErrorStatus = xpmParseData(&data, &image, NULL);
    ErrorMessage(ErrorStatus, &data);

    xpmDataClose(&data);
    XpmFreeXpmImage(&image);

    exit(0);
}
