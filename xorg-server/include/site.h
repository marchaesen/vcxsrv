/************************************************************

Copyright 1987, 1998  The Open Group

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

Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

********************************************************/

#ifndef SITE_H
#define SITE_H
/*
 * The vendor string identifies the vendor responsible for the
 * server executable.
 */
#ifndef VENDOR_STRING
#define VENDOR_STRING "The X.Org Foundation"
#endif

/*
 * The vendor release number identifies, for the purpose of submitting
 * traceable bug reports, the release number of software produced
 * by the vendor.
 */
#ifndef VENDOR_RELEASE
#define VENDOR_RELEASE	6600
#endif

/*
 * The following constants are provided solely as a last line of defense.  The
 * normal build ALWAYS overrides them using a special rule given in
 * server/dix/Imakefile.  If you want to change either of these constants, 
 * you should set the DefaultFontPath or DefaultRGBDatabase configuration 
 * parameters.
 * DO NOT CHANGE THESE VALUES OR THE DIX IMAKEFILE!
 */
#ifndef COMPILEDDEFAULTFONTPATH
#define COMPILEDDEFAULTFONTPATH	"/usr/share/fonts/X11/misc/"
#endif

/*
 * The following constants contain default values for all of the variables 
 * that can be initialized on the server command line or in the environment.
 */
#define COMPILEDDEFAULTFONT	"fixed"
#define COMPILEDCURSORFONT	"cursor"
#ifndef COMPILEDDISPLAYCLASS
#define COMPILEDDISPLAYCLASS	"MIT-unspecified"
#endif
#define DEFAULT_TIMEOUT		60      /* seconds */
#define DEFAULT_KEYBOARD_CLICK 	0
#define DEFAULT_BELL		50
#define DEFAULT_BELL_PITCH	400
#define DEFAULT_BELL_DURATION	100
#define DEFAULT_AUTOREPEAT	TRUE
#define DEFAULT_AUTOREPEATS	{\
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,\
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,\
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,\
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

#define DEFAULT_LEDS		0x0     /* all off */
#define DEFAULT_LEDS_MASK	0xffffffff      /* 32 */
#define DEFAULT_INT_RESOLUTION		1000
#define DEFAULT_INT_MIN_VALUE		0
#define DEFAULT_INT_MAX_VALUE		100
#define DEFAULT_INT_DISPLAYED		0

#define DEFAULT_PTR_NUMERATOR	2
#define DEFAULT_PTR_DENOMINATOR	1
#define DEFAULT_PTR_THRESHOLD	4

#define DEFAULT_SCREEN_SAVER_TIME (10 * (60 * 1000))
#define DEFAULT_SCREEN_SAVER_INTERVAL (10 * (60 * 1000))
#define DEFAULT_SCREEN_SAVER_BLANKING PreferBlanking
#define DEFAULT_SCREEN_SAVER_EXPOSURES AllowExposures
#ifndef DEFAULT_ACCESS_CONTROL
#define DEFAULT_ACCESS_CONTROL TRUE
#endif

/* Default logging parameters. */
#ifndef DEFAULT_LOG_VERBOSITY
#define DEFAULT_LOG_VERBOSITY		0
#endif
#ifndef DEFAULT_LOG_FILE_VERBOSITY
#define DEFAULT_LOG_FILE_VERBOSITY	3
#endif

#endif                          /* SITE_H */
