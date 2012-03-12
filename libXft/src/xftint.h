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

/*
 * These definitions are solely for use by the implementation of Xft
 * and constitute no kind of standard.  If you need any of these functions,
 * please drop me a note.  Either the library needs new functionality,
 * or there's a way to do what you need using the existing published
 * interfaces. keithp@freedesktop.org
 */

#ifndef _XFTINT_H_
#define _XFTINT_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#else /* X monolithic tree */
#define HAVE_STDLIB_H 1  /* assumed since all ANSI C platforms require it */
#include <X11/Xosdefs.h> /* get string.h or strings.h as appropriate */
#endif

#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#else
#if HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#include <ctype.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xmd.h>
#include <X11/Xlibint.h>
#define _XFT_NO_COMPAT_
#include "Xft.h"
#include <fontconfig/fcprivate.h>
#include <fontconfig/fcfreetype.h>

/* Added to <X11/Xfuncproto.h> in X11R6.9 and later */
#ifndef _X_HIDDEN
# define _X_HIDDEN /**/
#endif
#ifndef _X_EXPORT
# define _X_EXPORT /**/
#endif

typedef struct _XftMatcher {
    char    *object;
    double  (*compare) (char *object, FcValue value1, FcValue value2);
} XftMatcher;

typedef struct _XftSymbolic {
    const char	*name;
    int		value;
} XftSymbolic;

/*
 * Glyphs are stored in this structure
 */
typedef struct _XftGlyph {
    XGlyphInfo	    metrics;
    void	    *bitmap;
    unsigned long   glyph_memory;
} XftGlyph;

/*
 * A hash table translates Unicode values into glyph indicies
 */
typedef struct _XftUcsHash {
    FcChar32	    ucs4;
    FT_UInt	    glyph;
} XftUcsHash;

/*
 * Many fonts can share the same underlying face data; this
 * structure references that.  Note that many faces may in fact
 * live in the same font file; that is irrelevant to this structure
 * which is concerned only with the individual faces themselves
 */

typedef struct _XftFtFile {
    struct _XftFtFile	*next;
    int			ref;	    /* number of font infos using this file */

    char		*file;	    /* file name */
    int			id;	    /* font index within that file */

    FT_F26Dot6		xsize;	    /* current xsize setting */
    FT_F26Dot6		ysize;	    /* current ysize setting */
    FT_Matrix		matrix;	    /* current matrix setting */

    int			lock;	    /* lock count; can't unload unless 0 */
    FT_Face		face;	    /* pointer to face; only valid when lock */
} XftFtFile;

/*
 * This structure holds the data extracted from a pattern
 * needed to create a unique font object.
 */

struct _XftFontInfo {
    /*
     * Hash value (not include in hash value computation)
     */
    FcChar32		hash;
    XftFtFile		*file;		/* face source */
    /*
     * Rendering options
     */
    FT_F26Dot6		xsize, ysize;	/* pixel size */
    FcBool		antialias;	/* doing antialiasing */
    FcBool		embolden;	/* force emboldening */
    int			rgba;		/* subpixel order */
    int			lcd_filter;	/* lcd filter */
    FT_Matrix		matrix;		/* glyph transformation matrix */
    FcBool		transform;	/* non-identify matrix? */
    FT_Int		load_flags;	/* glyph load flags */
    FcBool		render;		/* whether to use the Render extension */
    /*
     * Internal fields
     */
    int			spacing;
    FcBool		minspace;
    int			char_width;
};

/*
 * Internal version of the font with private data
 */

typedef struct _XftFontInt {
    XftFont		public;		/* public fields */
    XftFont		*next;		/* all fonts on display */
    XftFont		*hash_next;	/* fonts in this hash chain */
    XftFontInfo		info;		/* Data from pattern */
    int			ref;		/* reference count */
    /*
     * Per-glyph information, indexed by glyph ID
     * This array follows the font in memory
     */
    XftGlyph		**glyphs;
    int			num_glyphs;	/* size of glyphs/bitmaps arrays */
    /*
     * Hash table to get from Unicode value to glyph ID
     * This array follows the glyphs in memory
     */
    XftUcsHash		*hash_table;
    int			hash_value;
    int			rehash_value;
    /*
     * X specific fields
     */
    GlyphSet		glyphset;	/* Render glyphset */
    XRenderPictFormat	*format;	/* Render format for glyphs */
    /*
     * Glyph memory management fields
     */
    unsigned long	glyph_memory;
    unsigned long	max_glyph_memory;
    FcBool		use_free_glyphs;   /* Use XRenderFreeGlyphs */
} XftFontInt;

typedef enum _XftClipType {
    XftClipTypeNone, XftClipTypeRegion, XftClipTypeRectangles
} XftClipType;

typedef struct _XftClipRect {
    int			xOrigin;
    int			yOrigin;
    int			n;
} XftClipRect;

#define XftClipRects(cr)    ((XRectangle *) ((cr) + 1))

typedef union _XftClip {
    XftClipRect	    *rect;
    Region	    region;
} XftClip;

struct _XftDraw {
    Display	    *dpy;
    int		    screen;
    unsigned int    bits_per_pixel;
    unsigned int    depth;
    Drawable	    drawable;
    Visual	    *visual;	/* NULL for bitmaps */
    Colormap	    colormap;
    XftClipType	    clip_type;
    XftClip	    clip;
    int		    subwindow_mode;
    struct {
	Picture		pict;
    } render;
    struct {
	GC		gc;
	int		use_pixmap;
    } core;
};

/*
 * Instead of taking two round trips for each blending request,
 * assume that if a particular drawable fails GetImage that it will
 * fail for a "while"; use temporary pixmaps to avoid the errors
 */

#define XFT_ASSUME_PIXMAP	20

typedef struct _XftSolidColor {
    XRenderColor    color;
    int		    screen;
    Picture	    pict;
} XftSolidColor;

#define XFT_NUM_SOLID_COLOR	16

#define XFT_NUM_FONT_HASH	127

typedef struct _XftDisplayInfo {
    struct _XftDisplayInfo  *next;
    Display		    *display;
    XExtCodes		    *codes;
    FcPattern		    *defaults;
    FcBool		    hasRender;
    XftFont		    *fonts;
    XRenderPictFormat	    *solidFormat;
    unsigned long	    glyph_memory;
    unsigned long	    max_glyph_memory;
    FcBool		    use_free_glyphs;
    int			    num_unref_fonts;
    int			    max_unref_fonts;
    XftSolidColor	    colors[XFT_NUM_SOLID_COLOR];
    XftFont		    *fontHash[XFT_NUM_FONT_HASH];
} XftDisplayInfo;

/*
 * By default, use no more than 4 meg of server memory total, and no
 * more than 1 meg for any one font
 */
#define XFT_DPY_MAX_GLYPH_MEMORY    (4 * 1024 * 1024)
#define XFT_FONT_MAX_GLYPH_MEMORY   (1024 * 1024)

/*
 * By default, keep the last 16 unreferenced fonts around to
 * speed reopening them.  Note that the glyph caching code
 * will keep the global memory usage reasonably limited
 */
#define XFT_DPY_MAX_UNREF_FONTS	    16

extern XftDisplayInfo	*_XftDisplayInfo;

#define XFT_DBG_OPEN	1
#define XFT_DBG_OPENV	2
#define XFT_DBG_RENDER	4
#define XFT_DBG_DRAW	8
#define XFT_DBG_REF	16
#define XFT_DBG_GLYPH	32
#define XFT_DBG_GLYPHV	64
#define XFT_DBG_CACHE	128
#define XFT_DBG_CACHEV	256
#define XFT_DBG_MEMORY	512

#define XFT_MEM_DRAW	0
#define XFT_MEM_FONT	1
#define XFT_MEM_FILE	2
#define XFT_MEM_GLYPH	3
#define XFT_MEM_NUM	4

/* xftcompat.c */
void XftFontSetDestroy (FcFontSet *s);
FcBool XftMatrixEqual (_Xconst FcMatrix *mat1, _Xconst FcMatrix *mat2);
void XftMatrixMultiply (FcMatrix *result, FcMatrix *a, FcMatrix *b);
void XftMatrixRotate (FcMatrix *m, double c, double s);
void XftMatrixScale (FcMatrix *m, double sx, double sy);
void XftMatrixShear (FcMatrix *m, double sh, double sv);
FcPattern *XftPatternCreate (void);
void XftValueDestroy (FcValue v);
void XftPatternDestroy (FcPattern *p);
FcBool XftPatternAdd (FcPattern *p, _Xconst char *object, FcValue value, FcBool append);
FcBool XftPatternDel (FcPattern *p, _Xconst char *object);
FcBool XftPatternAddInteger (FcPattern *p, _Xconst char *object, int i);
FcBool XftPatternAddDouble (FcPattern *p, _Xconst char *object, double i);
FcBool XftPatternAddMatrix (FcPattern *p, _Xconst char *object, FcMatrix *i);
FcBool XftPatternAddString (FcPattern *p, _Xconst char *object, char *i);
FcBool XftPatternAddBool (FcPattern *p, _Xconst char *object, FcBool i);
FcResult XftPatternGet (FcPattern *p, _Xconst char *object, int id, FcValue *v);
FcResult XftPatternGetInteger (FcPattern *p, _Xconst char *object, int id, int *i);
FcResult XftPatternGetDouble (FcPattern *p, _Xconst char *object, int id, double *i);
FcResult XftPatternGetString (FcPattern *p, _Xconst char *object, int id, char **i);
FcResult XftPatternGetMatrix (FcPattern *p, _Xconst char *object, int id, FcMatrix **i);
FcResult XftPatternGetBool (FcPattern *p, _Xconst char *object, int id, FcBool *i);
FcPattern *XftPatternDuplicate (FcPattern *orig);
FcPattern *XftPatternVaBuild (FcPattern *orig, va_list va);
FcPattern *XftPatternBuild (FcPattern *orig, ...);
FcBool XftNameUnparse (FcPattern *pat, char *dest, int len);
FcBool XftGlyphExists (Display *dpy, XftFont *font, FcChar32 ucs4);
FcObjectSet *XftObjectSetCreate (void);
Bool XftObjectSetAdd (FcObjectSet *os, _Xconst char *object);
void XftObjectSetDestroy (FcObjectSet *os);
FcObjectSet *XftObjectSetVaBuild (_Xconst char *first, va_list va);
FcObjectSet *XftObjectSetBuild (_Xconst char *first, ...);
FcFontSet *XftListFontSets (FcFontSet **sets, int nsets, FcPattern *p, FcObjectSet *os);

/* xftcore.c */
void
XftRectCore (XftDraw		*draw,
	     _Xconst XftColor	*color,
	     int		x,
	     int		y,
	     unsigned int	width,
	     unsigned int	height);

void
XftGlyphCore (XftDraw		*draw,
	      _Xconst XftColor	*color,
	      XftFont		*public,
	      int		x,
	      int		y,
	      _Xconst FT_UInt	*glyphs,
	      int		nglyphs);

void
XftGlyphSpecCore (XftDraw		*draw,
		  _Xconst XftColor	*color,
		  XftFont		*public,
		  _Xconst XftGlyphSpec	*glyphs,
		  int			nglyphs);

void
XftGlyphFontSpecCore (XftDraw			*draw,
		      _Xconst XftColor		*color,
		      _Xconst XftGlyphFontSpec	*glyphs,
		      int			nglyphs);

/* xftdbg.c */
int
XftDebug (void);

/* xftdpy.c */
XftDisplayInfo *
_XftDisplayInfoGet (Display *dpy, FcBool createIfNecessary);

void
_XftDisplayManageMemory (Display *dpy);

int
XftDefaultParseBool (const char *v);

FcBool
XftDefaultGetBool (Display *dpy, const char *object, int screen, FcBool def);

int
XftDefaultGetInteger (Display *dpy, const char *object, int screen, int def);

double
XftDefaultGetDouble (Display *dpy, const char *object, int screen, double def);

FcFontSet *
XftDisplayGetFontSet (Display *dpy);

/* xftdraw.c */
unsigned int
XftDrawDepth (XftDraw *draw);

unsigned int
XftDrawBitsPerPixel (XftDraw *draw);

FcBool
XftDrawRenderPrepare (XftDraw	*draw);

/* xftextent.c */

/* xftfont.c */

/* xftfreetype.c */
FcBool
_XftSetFace (XftFtFile *f, FT_F26Dot6 xsize, FT_F26Dot6 ysize, FT_Matrix *matrix);

void
XftFontManageMemory (Display *dpy);

/* xftglyph.c */
void
_XftFontUncacheGlyph (Display *dpy, XftFont *public);

void
_XftFontManageMemory (Display *dpy, XftFont *public);

/* xftinit.c */
void
XftMemReport (void);

void
XftMemAlloc (int kind, int size);

void
XftMemFree (int kind, int size);

/* xftlist.c */
FcFontSet *
XftListFontsPatternObjects (Display	    *dpy,
			    int		    screen,
			    FcPattern	    *pattern,
			    FcObjectSet    *os);

/* xftname.c */
void
_XftNameInit (void);

/* xftrender.c */

/* xftstr.c */
int
_XftMatchSymbolic (XftSymbolic *s, int n, const char *name, int def);

/* xftswap.c */
int
XftNativeByteOrder (void);

void
XftSwapCARD32 (CARD32 *data, int n);

void
XftSwapCARD24 (CARD8 *data, int width, int height);

void
XftSwapCARD16 (CARD16 *data, int n);

void
XftSwapImage (XImage *image);

/* xftxlfd.c */
#endif /* _XFT_INT_H_ */
