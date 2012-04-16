/*
 * Copyright © 2004 Eric Anholt
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Eric Anholt not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Eric Anholt makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * ERIC ANHOLT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ERIC ANHOLT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "gcstruct.h"
#include "picturestr.h"
#include "privates.h"

/*
 * One of these structures is allocated per GC that gets used with a window with
 * backing pixmap.
 */

typedef struct {
    GCPtr pBackingGC;           /* Copy of the GC but with graphicsExposures
                                 * set FALSE and the clientClip set to
                                 * clip output to the valid regions of the
                                 * backing pixmap. */
    unsigned long serialNumber; /* clientClip computed time */
    unsigned long stateChanges; /* changes in parent gc since last copy */
    GCOps *wrapOps;             /* wrapped ops */
    GCFuncs *wrapFuncs;         /* wrapped funcs */
} cwGCRec, *cwGCPtr;

extern _X_EXPORT DevPrivateKeyRec cwGCKeyRec;

#define cwGCKey (&cwGCKeyRec)

#define getCwGC(pGC) ((cwGCPtr)dixLookupPrivate(&(pGC)->devPrivates, cwGCKey))
#define setCwGC(pGC,p) dixSetPrivate(&(pGC)->devPrivates, cwGCKey, p)

/*
 * One of these structures is allocated per Picture that gets used with a
 * window with a backing pixmap
 */

typedef struct {
    PicturePtr pBackingPicture;
    unsigned long serialNumber;
    unsigned long stateChanges;
} cwPictureRec, *cwPicturePtr;

extern _X_EXPORT DevPrivateKeyRec cwPictureKeyRec;

#define cwPictureKey (&cwPictureKeyRec)

#define getCwPicture(pPicture) (pPicture->pDrawable ? \
    (cwPicturePtr)dixLookupPrivate(&(pPicture)->devPrivates, cwPictureKey) : 0)
#define setCwPicture(pPicture,p) dixSetPrivate(&(pPicture)->devPrivates, cwPictureKey, p)

extern _X_EXPORT DevPrivateKeyRec cwWindowKeyRec;

#define cwWindowKey (&cwWindowKeyRec)

#define cwWindowPrivate(pWin) dixLookupPrivate(&(pWin)->devPrivates, cwWindowKey)
#define getCwPixmap(pWindow)	    ((PixmapPtr) cwWindowPrivate(pWindow))
#define setCwPixmap(pWindow,pPixmap) \
    dixSetPrivate(&(pWindow)->devPrivates, cwWindowKey, pPixmap)

#define cwDrawableIsRedirWindow(pDraw)					\
	((pDraw)->type == DRAWABLE_WINDOW &&				\
	 getCwPixmap((WindowPtr) (pDraw)) != NULL)

typedef struct {
    /*
     * screen func wrappers
     */
    CloseScreenProcPtr CloseScreen;
    GetImageProcPtr GetImage;
    GetSpansProcPtr GetSpans;
    CreateGCProcPtr CreateGC;

    CopyWindowProcPtr CopyWindow;

    GetWindowPixmapProcPtr GetWindowPixmap;
    SetWindowPixmapProcPtr SetWindowPixmap;

    DestroyPictureProcPtr DestroyPicture;
    ChangePictureClipProcPtr ChangePictureClip;
    DestroyPictureClipProcPtr DestroyPictureClip;

    ChangePictureProcPtr ChangePicture;
    ValidatePictureProcPtr ValidatePicture;

    CompositeProcPtr Composite;
    CompositeRectsProcPtr CompositeRects;

    TrapezoidsProcPtr Trapezoids;
    TrianglesProcPtr Triangles;

    RasterizeTrapezoidProcPtr RasterizeTrapezoid;
} cwScreenRec, *cwScreenPtr;

extern _X_EXPORT DevPrivateKeyRec cwScreenKeyRec;

#define cwScreenKey (&cwScreenKeyRec)

#define getCwScreen(pScreen) ((cwScreenPtr)dixLookupPrivate(&(pScreen)->devPrivates, cwScreenKey))
#define setCwScreen(pScreen,p) dixSetPrivate(&(pScreen)->devPrivates, cwScreenKey, p)

#define CW_OFFSET_XYPOINTS(ppt, npt) do { \
    DDXPointPtr _ppt = (DDXPointPtr)(ppt); \
    int _i; \
    for (_i = 0; _i < npt; _i++) { \
	_ppt[_i].x += dst_off_x; \
	_ppt[_i].y += dst_off_y; \
    } \
} while (0)

#define CW_OFFSET_RECTS(prect, nrect) do { \
    int _i; \
    for (_i = 0; _i < nrect; _i++) { \
	(prect)[_i].x += dst_off_x; \
	(prect)[_i].y += dst_off_y; \
    } \
} while (0)

#define CW_OFFSET_ARCS(parc, narc) do { \
    int _i; \
    for (_i = 0; _i < narc; _i++) { \
	(parc)[_i].x += dst_off_x; \
	(parc)[_i].y += dst_off_y; \
    } \
} while (0)

#define CW_OFFSET_XY_DST(x, y) do { \
    (x) = (x) + dst_off_x; \
    (y) = (y) + dst_off_y; \
} while (0)

#define CW_OFFSET_XY_SRC(x, y) do { \
    (x) = (x) + src_off_x; \
    (y) = (y) + src_off_y; \
} while (0)

/* cw.c */
extern _X_EXPORT DrawablePtr
 cwGetBackingDrawable(DrawablePtr pDrawable, int *x_off, int *y_off);

/* cw_render.c */

extern _X_EXPORT void
 cwInitializeRender(ScreenPtr pScreen);

extern _X_EXPORT void
 cwFiniRender(ScreenPtr pScreen);

/* cw.c */

extern _X_EXPORT void
 miInitializeCompositeWrapper(ScreenPtr pScreen);
