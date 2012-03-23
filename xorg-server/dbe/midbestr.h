/******************************************************************************
 * 
 * Copyright (c) 1994, 1995  Hewlett-Packard Company
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL HEWLETT-PACKARD COMPANY BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 * Except as contained in this notice, the name of the Hewlett-Packard
 * Company shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization from the Hewlett-Packard Company.
 * 
 *     Header file for users of machine-independent DBE code
 * 
 *****************************************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifndef MIDBE_STRUCT_H
#define MIDBE_STRUCT_H

/* DEFINES */

#define MI_DBE_WINDOW_PRIV_PRIV(pDbeWindowPriv) \
    (!(pDbeWindowPriv) ? NULL : (MiDbeWindowPrivPrivPtr) \
     dixLookupPrivate(&(pDbeWindowPriv)->devPrivates, miDbeWindowPrivPrivKey))

#define MI_DBE_WINDOW_PRIV_PRIV_FROM_WINDOW(pWin)\
    MI_DBE_WINDOW_PRIV_PRIV(DBE_WINDOW_PRIV(pWin))

/* TYPEDEFS */

typedef struct _MiDbeWindowPrivPrivRec {
    /* Place machine-specific fields in here.
     * Since this is mi code, we do not really have machine-specific fields.
     */

    /* Pointer to a drawable that contains the contents of the back buffer.
     */
    PixmapPtr pBackBuffer;

    /* Pointer to a drawable that contains the contents of the front buffer.
     * This pointer is only used for the XdbeUntouched swap action.  For that
     * swap action, we need to copy the front buffer (window) contents into
     * this drawable, copy the contents of current back buffer drawable (the
     * back buffer) into the window, swap the front and back drawable pointers,
     * and then swap the drawable/resource associations in the resource
     * database.
     */
    PixmapPtr pFrontBuffer;

    /* Pointer back to our window private with which we are associated. */
    DbeWindowPrivPtr pDbeWindowPriv;

} MiDbeWindowPrivPrivRec, *MiDbeWindowPrivPrivPtr;

#endif                          /* MIDBE_STRUCT_H */
