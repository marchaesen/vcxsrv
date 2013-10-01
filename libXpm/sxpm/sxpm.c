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
* sxpm.c:                                                                     *
*                                                                             *
*  Show XPM File program                                                      *
*                                                                             *
*  Developed by Arnaud Le Hors                                                *
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <X11/StringDefs.h>
#include <X11/Intrinsic.h>
#include <X11/IntrinsicP.h>
#include <X11/Shell.h>

#ifdef VMS
#include <X11/shape.h>
#else
#include <X11/extensions/shape.h>
#endif

#include <X11/xpm.h>

#ifdef USE_GETTEXT
#include <locale.h>
#include <libintl.h>
#else
#define gettext(a) (a)
#endif

/* XPM */
/* plaid pixmap */
static char *plaid[] = {
    /* width height ncolors chars_per_pixel */
    "22 22 4 2 XPMEXT",
    /* colors */
    "   c red 	m white  s light_color",
    "Y  c green	m black  s lines_in_mix",
    "+  c yellow	m white  s lines_in_dark",
    "x 		m black  s dark_color",
    /* pixels */
    "x   x   x x x   x   x x x x x x + x x x x x ",
    "  x   x   x   x   x   x x x x x x x x x x x ",
    "x   x   x x x   x   x x x x x x + x x x x x ",
    "  x   x   x   x   x   x x x x x x x x x x x ",
    "x   x   x x x   x   x x x x x x + x x x x x ",
    "Y Y Y Y Y x Y Y Y Y Y + x + x + x + x + x + ",
    "x   x   x x x   x   x x x x x x + x x x x x ",
    "  x   x   x   x   x   x x x x x x x x x x x ",
    "x   x   x x x   x   x x x x x x + x x x x x ",
    "  x   x   x   x   x   x x x x x x x x x x x ",
    "x   x   x x x   x   x x x x x x + x x x x x ",
    "          x           x   x   x Y x   x   x ",
    "          x             x   x   Y   x   x   ",
    "          x           x   x   x Y x   x   x ",
    "          x             x   x   Y   x   x   ",
    "          x           x   x   x Y x   x   x ",
    "x x x x x x x x x x x x x x x x x x x x x x ",
    "          x           x   x   x Y x   x   x ",
    "          x             x   x   Y   x   x   ",
    "          x           x   x   x Y x   x   x ",
    "          x             x   x   Y   x   x   ",
    "          x           x   x   x Y x   x   x ",
    "bullshit",
    "XPMEXT ext1 data1",
    "XPMEXT ext2",
    "data2_1",
    "data2_2",
    "XPMEXT",
    "foo",
    "",
    "XPMEXT ext3",
    "data3",
    "XPMENDEXT"
};

#define win XtWindow(topw)
#define dpy XtDisplay(topw)
#define root XRootWindowOfScreen(XtScreen(topw))
#define xrdb XtDatabase(dpy)
static Colormap colormap;

void Usage(void) _X_NORETURN;
void ErrorMessage(int ErrorStatus, const char *tag);
void Punt(int i) _X_NORETURN;
void VersionInfo(void);
void kinput(Widget widget, char *tag, XEvent *xe, Boolean *b);
void GetNumbers(int num, int *format_return,
		int *libmajor_return,
		char *libminor_return);

#define IWIDTH      50
#define IHEIGHT     50

typedef struct _XpmIcon {
    Pixmap pixmap;
    Pixmap mask;
    XpmAttributes attributes;
}        XpmIcon;

static char **command;
static Widget topw;
static XpmIcon view, icon;
static XrmOptionDescRec options[] = {
    {"-hints", ".hints", XrmoptionNoArg, (XtPointer) "True"},
    {"-icon", ".icon", XrmoptionSepArg, (XtPointer) NULL},
};

int
main(
    int		  argc,
    char	**argv)
{
    int ErrorStatus;
    unsigned int verbose = 0;		/* performs verbose output */
    unsigned int stdinf = 1;		/* read from stdin */
    unsigned int stdoutf = 0;		/* write to stdout */
    unsigned int nod = 0;		/* no display */
    unsigned int nom = 0;		/* no mask display */
    unsigned int incResize = 0;
    unsigned int resize = 0;
    unsigned int w_rtn;
    unsigned int h_rtn;
    char *input = NULL;
    char *output = NULL;
    char *iconFile = NULL;
    unsigned int numsymbols = 0;
    XpmColorSymbol symbols[10];
    char *stype;
    XrmValue val;
    unsigned long valuemask = 0;
    int n;
    Arg args[4];

#ifdef Debug
    char **data;
    char *buffer;
#endif

#ifdef USE_GETTEXT
    XtSetLanguageProc(NULL,NULL,NULL);
    bindtextdomain("sxpm",LOCALEDIR);
    textdomain("sxpm");
#endif

    topw = XtInitialize(argv[0], "Sxpm",
			options, XtNumber(options), &argc, argv);

    if (!topw) {
	/* L10N_Comments : Error if no $DISPLAY or $DISPLAY can't be opened.
	   Not normally reached as Xt exits before we get here. */
	fprintf(stderr, gettext("Sxpm Error... [ Undefined DISPLAY ]\n"));
	exit(1);
    }
    colormap = XDefaultColormapOfScreen(XtScreen(topw));

    /*
     * geometry management
     */

    if (XrmGetResource(xrdb, NULL, "sxpm.geometry", &stype, &val)
	|| XrmGetResource(xrdb, NULL, "Sxpm.geometry", &stype, &val)) {

	int flags;
	int x_rtn;
	int y_rtn;
	char *geo = NULL;

	geo = (char *) val.addr;
	flags = XParseGeometry(geo, &x_rtn, &y_rtn,
			       (unsigned int *) &w_rtn,
			       (unsigned int *) &h_rtn);

	if (!((WidthValue & flags) && (HeightValue & flags)))
	    resize = 1;

    } else
	resize = 1;

    n = 0;
    if (resize) {
	w_rtn = 0;
	h_rtn = 0;
	XtSetArg(args[n], XtNwidth, 1);
	n++;
	XtSetArg(args[n], XtNheight, 1);
	n++;
    }
    XtSetArg(args[n], XtNmappedWhenManaged, False);
    n++;
    XtSetArg(args[n], XtNinput, True);
    n++;
    XtSetValues(topw, args, n);

    if ((XrmGetResource(xrdb, "sxpm.hints", "", &stype, &val)
	 || XrmGetResource(xrdb, "Sxpm.hints", "", &stype, &val))
	&& !strcmp((char *) val.addr, "True")) {
	/* gotcha */
	incResize = 1;
	resize = 1;
    }

    /*
     * icon management
     */

    if (XrmGetResource(xrdb, "sxpm.icon", "", &stype, &val)
	|| XrmGetResource(xrdb, "Sxpm.icon", "", &stype, &val)) {
	iconFile = (char *) val.addr;
    }
    if (iconFile) {

	XColor color, junk;
	Pixel bpix;
	Window iconW;

	if (XAllocNamedColor(dpy, colormap, "black", &color, &junk))
	    bpix = color.pixel;
	else
	    bpix = XBlackPixelOfScreen(XtScreen(topw));

	iconW = XCreateSimpleWindow(dpy, root, 0, 0,
				    IWIDTH, IHEIGHT, 1, bpix, bpix);

	icon.attributes.valuemask = XpmReturnAllocPixels;
	ErrorStatus = XpmReadFileToPixmap(dpy, root, iconFile, &icon.pixmap,
					  &icon.mask, &icon.attributes);
	ErrorMessage(ErrorStatus, "Icon");

	XSetWindowBackgroundPixmap(dpy, iconW, icon.pixmap);

	n = 0;
	XtSetArg(args[n], XtNbackground, bpix);
	n++;
	XtSetArg(args[n], XtNiconWindow, iconW);
	n++;
	XtSetValues(topw, args, n);
    }

    /*
     * arguments parsing
     */

    command = argv;
    for (n = 1; n < argc; n++) {
	if (strcmp(argv[n], "-plaid") == 0) {
	    stdinf = 0;
	    continue;
	}
	if (argv[n][0] != '-') {
	    stdinf = 0;
	    input = argv[n];
	    continue;
	}
	if ((strlen(argv[n]) == 1) && (argv[n][0] == '-'))
	    /* stdin */
	    continue;
	if (strcmp(argv[n], "-o") == 0) {
	    if (n < argc - 1) {
		if ((strlen(argv[n + 1]) == 1) && (argv[n + 1][0] == '-'))
		    stdoutf = 1;
		else
		    output = argv[n + 1];
		n++;
		continue;
	    } else
		Usage();
	}
	if (strcmp(argv[n], "-nod") == 0) {
	    nod = 1;
	    continue;
	}
	if (strcmp(argv[n], "-nom") == 0) {
	    nom = 1;
	    continue;
	}
	if (strcmp(argv[n], "-sc") == 0) {
	    if (n < argc - 2) {
		valuemask |= XpmColorSymbols;
		symbols[numsymbols].name = argv[++n];
		symbols[numsymbols++].value = argv[++n];
		continue;
	    } else
		Usage();
	}
	if (strcmp(argv[n], "-sp") == 0) {
	    if (n < argc - 2) {
		valuemask |= XpmColorSymbols;
		symbols[numsymbols].name = argv[++n];
		symbols[numsymbols].value = NULL;
		symbols[numsymbols++].pixel = atol(argv[++n]);
		continue;
	    }
	}
	if (strcmp(argv[n], "-cp") == 0) {
	    if (n < argc - 2) {
		valuemask |= XpmColorSymbols;
		symbols[numsymbols].name = NULL;
		symbols[numsymbols].value = argv[++n];
		symbols[numsymbols++].pixel = atol(argv[++n]);
		continue;
	    }
	}
	if (strcmp(argv[n], "-mono") == 0) {
	    valuemask |= XpmColorKey;
	    view.attributes.color_key = XPM_MONO;
	    continue;
	}
	if (strcmp(argv[n], "-gray4") == 0 || strcmp(argv[n], "-grey4") == 0) {
	    valuemask |= XpmColorKey;
	    view.attributes.color_key = XPM_GRAY4;
	    continue;
	}
	if (strcmp(argv[n], "-gray") == 0 || strcmp(argv[n], "-grey") == 0) {
	    valuemask |= XpmColorKey;
	    view.attributes.color_key = XPM_GRAY;
	    continue;
	}
	if (strcmp(argv[n], "-color") == 0) {
	    valuemask |= XpmColorKey;
	    view.attributes.color_key = XPM_COLOR;
	    continue;
	}
	if (strncmp(argv[n], "-closecolors", 6) == 0) {
	    valuemask |= XpmCloseness;
	    view.attributes.closeness = 40000;
	    continue;
	}
	if (strcmp(argv[n], "-rgb") == 0) {
	    if (n < argc - 1) {
		valuemask |= XpmRgbFilename;
		view.attributes.rgb_fname = argv[++n];
		continue;
	    } else
		Usage();

	}
	if (strncmp(argv[n], "-version", 4) == 0) {
	    VersionInfo();
	    exit(0);
	}
	if (strcmp(argv[n], "-v") == 0) {
	    verbose = 1;
	    continue;
	}
	if (strcmp(argv[n], "-pcmap") == 0) {
	    valuemask |= XpmColormap;
	    continue;
	}
	Usage();
    }

    XtRealizeWidget(topw);
    if (valuemask & XpmColormap) {
	colormap = XCreateColormap(dpy, win,
				   DefaultVisual(dpy, DefaultScreen(dpy)),
				   AllocNone);
	view.attributes.colormap = colormap;
	XSetWindowColormap(dpy, win, colormap);
    }
    view.attributes.colorsymbols = symbols;
    view.attributes.numsymbols = numsymbols;
    view.attributes.valuemask = valuemask;

#ifdef Debug
    /* this is just to test the XpmCreateDataFromPixmap function */

    view.attributes.valuemask |= XpmReturnAllocPixels;
    view.attributes.valuemask |= XpmReturnExtensions;
    ErrorStatus = XpmCreatePixmapFromData(dpy, win, plaid,
					  &view.pixmap, &view.mask,
					  &view.attributes);
    ErrorMessage(ErrorStatus, "Plaid");

    ErrorStatus = XpmCreateDataFromPixmap(dpy, &data, view.pixmap, view.mask,
					  &view.attributes);
    ErrorMessage(ErrorStatus, "Data");
    if (verbose && view.attributes.nextensions) {
	unsigned int i, j;

	for (i = 0; i < view.attributes.nextensions; i++) {
	    fprintf(stderr, "Xpm extension : %s\n",
		    view.attributes.extensions[i].name);
	    for (j = 0; j < view.attributes.extensions[i].nlines; j++)
		fprintf(stderr, "\t\t%s\n",
			view.attributes.extensions[i].lines[j]);
	}
    }
    XFreePixmap(dpy, view.pixmap);
    if (view.mask)
	XFreePixmap(dpy, view.mask);

    XFreeColors(dpy, colormap,
		view.attributes.alloc_pixels,
		view.attributes.nalloc_pixels, 0);

    XpmFreeAttributes(&view.attributes);
    view.attributes.valuemask = valuemask;
#endif

    if (input || stdinf) {
	view.attributes.valuemask |= XpmReturnInfos;
	view.attributes.valuemask |= XpmReturnAllocPixels;
	view.attributes.valuemask |= XpmReturnExtensions;

#ifdef Debug
	XpmFree(data);

	/*
	 * this is just to test the XpmCreatePixmapFromBuffer and
	 * XpmCreateBufferFromPixmap functions
	 */
	ErrorStatus = XpmReadFileToBuffer(input, &buffer);
	ErrorMessage(ErrorStatus, "CreateBufferFromFile");

	ErrorStatus = XpmCreatePixmapFromBuffer(dpy, win, buffer,
						&view.pixmap, &view.mask,
						&view.attributes);
	ErrorMessage(ErrorStatus, "CreatePixmapFromBuffer");
	XpmFree(buffer);
	ErrorStatus = XpmCreateBufferFromPixmap(dpy, &buffer,
						view.pixmap, view.mask,
						&view.attributes);
	ErrorMessage(ErrorStatus, "CreateBufferFromPixmap");
	ErrorStatus = XpmWriteFileFromBuffer("buffer_output", buffer);
	ErrorMessage(ErrorStatus, "WriteFileFromBuffer");
	XpmFree(buffer);
	if (view.pixmap) {
	    XFreePixmap(dpy, view.pixmap);
	    if (view.mask)
		XFreePixmap(dpy, view.mask);

	    XFreeColors(dpy, colormap, view.attributes.alloc_pixels,
			view.attributes.nalloc_pixels, 0);

	    XpmFreeAttributes(&view.attributes);
	}
	ErrorStatus = XpmReadFileToData(input, &data);
	ErrorMessage(ErrorStatus, "ReadFileToData");
	ErrorStatus = XpmCreatePixmapFromData(dpy, win, data,
					      &view.pixmap, &view.mask,
					      &view.attributes);
	ErrorMessage(ErrorStatus, "CreatePixmapFromData");
	ErrorStatus = XpmWriteFileFromData("sxpmout.xpm", data);
	ErrorMessage(ErrorStatus, "WriteFileFromData");
	XpmFree(data);
	XpmFreeAttributes(&view.attributes);
#endif
	ErrorStatus = XpmReadFileToPixmap(dpy, win, input,
					  &view.pixmap, &view.mask,
					  &view.attributes);
	ErrorMessage(ErrorStatus, "Read");
	if (verbose && view.attributes.nextensions) {
	    unsigned int i, j;

	    for (i = 0; i < view.attributes.nextensions; i++) {
		/* L10N_Comments : Output when -v & file has extensions
		   %s is replaced by extension name */
		fprintf(stderr, gettext("Xpm extension : %s\n"),
			view.attributes.extensions[i].name);
		for (j = 0; j < view.attributes.extensions[i].nlines; j++)
		    fprintf(stderr, "\t\t%s\n",
			    view.attributes.extensions[i].lines[j]);
	    }
	}
    } else {
#ifdef Debug
	ErrorStatus = XpmCreatePixmapFromData(dpy, win, data,
					      &view.pixmap, &view.mask,
					      &view.attributes);
	XpmFree(data);
#else
	ErrorStatus = XpmCreatePixmapFromData(dpy, win, plaid,
					      &view.pixmap, &view.mask,
					      &view.attributes);
#endif
	ErrorMessage(ErrorStatus, "Plaid");
    }
    if (output || stdoutf) {
	ErrorStatus = XpmWriteFileFromPixmap(dpy, output, view.pixmap,
					     view.mask, &view.attributes);
	ErrorMessage(ErrorStatus, "Write");
    }
    if (!nod) {

	/*
	 * manage display if requested
	 */

	XSizeHints size_hints;
	char *xString = NULL;

	if (w_rtn && h_rtn
	    && ((w_rtn < view.attributes.width)
		|| h_rtn < view.attributes.height)) {
	    resize = 1;
	}
	if (resize) {
	    XtResizeWidget(topw,
			   view.attributes.width, view.attributes.height, 1);
	}
	if (incResize) {
	    size_hints.flags = USSize | PMinSize | PResizeInc;
	    size_hints.height = view.attributes.height;
	    size_hints.width = view.attributes.width;
	    size_hints.height_inc = view.attributes.height;
	    size_hints.width_inc = view.attributes.width;
	} else
	    size_hints.flags = PMinSize;

	size_hints.min_height = view.attributes.height;
	size_hints.min_width = view.attributes.width;
	XSetWMNormalHints(dpy, win, &size_hints);

	if (input) {
	    xString = (char *) XtMalloc((sizeof(char) * strlen(input)) + 20);
	    sprintf(xString, "Sxpm: %s", input);
	    XStoreName(dpy, win, xString);
	    XSetIconName(dpy, win, xString);
	} else if (stdinf) {
	    XStoreName(dpy, win, "Sxpm: stdin");
	    XSetIconName(dpy, win, "Sxpm: stdin");
	} else {
	    XStoreName(dpy, win, "Sxpm");
	    XSetIconName(dpy, win, "Sxpm");
	}

	XtAddEventHandler(topw, KeyPressMask, False,
			  (XtEventHandler) kinput, NULL);
	XSetWindowBackgroundPixmap(dpy, win, view.pixmap);

	if (view.mask && !nom)
	    XShapeCombineMask(dpy, win, ShapeBounding, 0, 0,
			      view.mask, ShapeSet);

	XClearWindow(dpy, win);
	XtMapWidget(topw);
	if (xString)
	    XtFree(xString);
	XtMainLoop();
    }
    Punt(0);
}

void
Usage(void)
{
    /* L10N_Comments : Usage message (sxpm -h) in two parts.
       In the first part %s is replaced by the command name. */
    fprintf(stderr, gettext("\nUsage:  %s [options...]\n"), command[0]);
    fprintf(stderr, gettext("Where options are:\n\
\n\
[-d host:display]            Display to connect to.\n\
[-g geom]                    Geometry of window.\n\
[-hints]                     Set ResizeInc for window.\n\
[-icon filename]             Set pixmap for iconWindow.\n\
[-plaid]                     Read the included plaid pixmap.\n\
[filename]                   Read from file 'filename', and from standard\n\
                             input if 'filename' is '-'.\n\
[-o filename]                Write to file 'filename', and to standard\n\
                             output if 'filename' is '-'.\n\
[-pcmap]                     Use a private colormap.\n\
[-closecolors]               Try to use `close' colors.\n\
[-nod]                       Don't display in window.\n\
[-nom]                       Don't use clip mask if any.\n\
[-mono]                      Use the colors specified for a monochrome visual.\n\
[-grey4]                     Use the colors specified for a 4 greyscale visual.\n\
[-grey]                      Use the colors specified for a greyscale visual.\n\
[-color]                     Use the colors specified for a color visual.\n\
[-sc symbol color]           Override color defaults.\n\
[-sp symbol pixel]           Override color defaults.\n\
[-cp color pixel]            Override color defaults.\n\
[-rgb filename]              Search color names in the rgb text file 'filename'.\n\
[-v]                         Verbose - print out extensions.\n\
[-version]                   Print out program's version number\n\
                             and library's version number if different.\n\
if no input is specified sxpm reads from standard input.\n\
\n"));
    exit(0);
}


void
ErrorMessage(
    int		 ErrorStatus,
    const char	*tag)
{
    char *error = NULL;
    char *warning = NULL;

    switch (ErrorStatus) {
    case XpmSuccess:
	return;
    case XpmColorError:
/* L10N_Comments : The following set of messages are classified as
   either errors or warnings.  Based on the class of message, different
   wrappers are selected at the end to state the message source & class.

	   L10N_Comments : WARNING produced when filename can be read, but
	   contains an invalid color specification (need to create test case)*/
	warning = gettext("Could not parse or alloc requested color");
	break;
    case XpmOpenFailed:
	/* L10N_Comments : ERROR produced when filename does not exist
	   or insufficient permissions to open (i.e. sxpm /no/such/file ) */
	error = gettext("Cannot open file");
	break;
    case XpmFileInvalid:
	/* L10N_Comments : ERROR produced when filename can be read, but
	   is not an XPM file (i.e. sxpm /dev/null ) */
	error = gettext("Invalid XPM file");
	break;
    case XpmNoMemory:
	/* L10N_Comments : ERROR produced when filename can be read, but
	   is too big for memory
	   (i.e. limit datasize 32 ; sxpm /usr/dt/backdrops/Crochet.pm ) */
	error = gettext("Not enough memory");
	break;
    case XpmColorFailed:
	/* L10N_Comments : ERROR produced when filename can be read, but
	   contains an invalid color specification (need to create test case)*/
	error = gettext("Failed to parse or alloc some color");
	break;
    }

    if (warning)
	/* L10N_Comments : Wrapper around above WARNING messages.
	   First %s is the tag for the operation that produced the warning.
	   Second %s is the message selected from the above set. */
	fprintf(stderr, gettext("%s Xpm Warning: %s.\n"), tag, warning);

    if (error) {
	/* L10N_Comments : Wrapper around above ERROR messages.
	   First %s is the tag for the operation that produced the error.
	   Second %s is the message selected from the above set */
	fprintf(stderr, gettext("%s Xpm Error: %s.\n"), tag, error);
	Punt(1);
    }
}

void
Punt(int i)
{
    if (icon.pixmap) {
	XFreePixmap(dpy, icon.pixmap);
	if (icon.mask)
	    XFreePixmap(dpy, icon.mask);

	XFreeColors(dpy, colormap,
		    icon.attributes.alloc_pixels,
		    icon.attributes.nalloc_pixels, 0);

	XpmFreeAttributes(&icon.attributes);
    }
    if (view.pixmap) {
	XFreePixmap(dpy, view.pixmap);
	if (view.mask)
	    XFreePixmap(dpy, view.mask);

	XFreeColors(dpy, colormap,
		    view.attributes.alloc_pixels,
		    view.attributes.nalloc_pixels, 0);

	XpmFreeAttributes(&view.attributes);
    }
    exit(i);
}

void
kinput(
    Widget	 widget,
    char	*tag,
    XEvent	*xe,
    Boolean	*b)
{
    char c = '\0';

    XLookupString(&(xe->xkey), &c, 1, NULL, NULL);
    if (c == 'q' || c == 'Q')
	Punt(0);
}

/*
 * small function to extract various version numbers from the given global
 * number (following the rule described in xpm.h).
 */
void
GetNumbers(
    int		 num,
    int		*format_return,
    int		*libmajor_return,
    char	*libminor_return)
{
    *format_return = num / 10000;
    *libmajor_return = (num % 10000) / 100;
    *libminor_return = 'a' + (num % 10000) % 100 - 1;
}

void
VersionInfo(void)
{
    int format, libmajor;
    char libminor;

    GetNumbers(XpmIncludeVersion, &format, &libmajor, &libminor);
    /* L10N_Comments : sxpm -version output */
    fprintf(stderr, gettext("sxpm version: %d.%d%c\n"),
	    format, libmajor, libminor);
    /* L10N_Comments :
     * if we are linked to an XPM library different from the one we've been
     * compiled with, print its own number too when sxpm -version is called.
     */
    if (XpmIncludeVersion != XpmLibraryVersion()) {
	GetNumbers(XpmLibraryVersion(), &format, &libmajor, &libminor);
	fprintf(stderr, gettext("using the XPM library version: %d.%d%c\n"),
		format, libmajor, libminor);
    }
}
