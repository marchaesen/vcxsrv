/*
 * Copyright 2001-2004 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 * Interface for GC support.  \see dmxgc.c */

#ifndef DMXGC_H
#define DMXGC_H

#include "gcstruct.h"

/** GC private area. */
typedef struct _dmxGCPriv {
    GCOps *ops;
    GCFuncs *funcs;
    XlibGC gc;
    Bool msc;
} dmxGCPrivRec, *dmxGCPrivPtr;

extern Bool dmxInitGC(ScreenPtr pScreen);

extern Bool dmxCreateGC(GCPtr pGC);
extern void dmxValidateGC(GCPtr pGC, unsigned long changes,
                          DrawablePtr pDrawable);
extern void dmxChangeGC(GCPtr pGC, unsigned long mask);
extern void dmxCopyGC(GCPtr pGCSrc, unsigned long changes, GCPtr pGCDst);
extern void dmxDestroyGC(GCPtr pGC);
extern void dmxChangeClip(GCPtr pGC, int type, void *pvalue, int nrects);
extern void dmxDestroyClip(GCPtr pGC);
extern void dmxCopyClip(GCPtr pGCDst, GCPtr pGCSrc);

extern void dmxBECreateGC(ScreenPtr pScreen, GCPtr pGC);
extern Bool dmxBEFreeGC(GCPtr pGC);

/** Get private. */
#define DMX_GET_GC_PRIV(_pGC)						\
    (dmxGCPrivPtr)dixLookupPrivate(&(_pGC)->devPrivates, dmxGCPrivateKey)

#define DMX_GC_FUNC_PROLOGUE(_pGC)					\
do {									\
    dmxGCPrivPtr _pGCPriv = DMX_GET_GC_PRIV(_pGC);			\
    DMX_UNWRAP(funcs, _pGCPriv, (_pGC));				\
    if (_pGCPriv->ops)							\
	DMX_UNWRAP(ops, _pGCPriv, (_pGC));				\
} while (0)

#define DMX_GC_FUNC_EPILOGUE(_pGC)					\
do {									\
    dmxGCPrivPtr _pGCPriv = DMX_GET_GC_PRIV(_pGC);			\
    DMX_WRAP(funcs, &dmxGCFuncs, _pGCPriv, (_pGC));			\
    if (_pGCPriv->ops)							\
	DMX_WRAP(ops, &dmxGCOps, _pGCPriv, (_pGC));			\
} while (0)

#endif                          /* DMXGC_H */
