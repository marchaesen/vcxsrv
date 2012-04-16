/*
 * Copyright 2003 Red Hat Inc., Raleigh, North Carolina.
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

#ifndef __GLX_swap_h__
#define __GLX_swap_h__

extern int JoinSwapGroupSGIX(DrawablePtr pDraw, DrawablePtr pMember);
extern int SGSwapBuffers(__GLXclientState * cl, XID drawId, GLXContextTag tag,
                         DrawablePtr pDraw);

extern void SwapBarrierInit(void);
extern void SwapBarrierReset(void);
extern int QueryMaxSwapBarriersSGIX(int screen);
extern int BindSwapBarrierSGIX(DrawablePtr pDraw, int barrier);

#endif                          /* !__GLX_swap_h__ */
