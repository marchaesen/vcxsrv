/*
 * Copyright 2004 Red Hat Inc., Raleigh, North Carolina.
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
 * Interface for initialization.  \see dmxinit.c */

#ifndef DMXINIT_H
#define DMXINIT_H

#include "scrnintstr.h"

extern Bool dmxOpenDisplay(DMXScreenInfo * dmxScreen);
extern void dmxSetErrorHandler(DMXScreenInfo * dmxScreen);
extern void dmxCheckForWM(DMXScreenInfo * dmxScreen);
extern void dmxGetScreenAttribs(DMXScreenInfo * dmxScreen);
extern Bool dmxGetVisualInfo(DMXScreenInfo * dmxScreen);
extern void dmxGetColormaps(DMXScreenInfo * dmxScreen);
extern void dmxGetPixmapFormats(DMXScreenInfo * dmxScreen);

#endif                          /* DMXINIT_H */
