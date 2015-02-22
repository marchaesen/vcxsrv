#ifndef _glxext_h_
#define _glxext_h_

/*
 * SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
 * Copyright (C) 1991-2000 Silicon Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice including the dates of first publication and
 * either this permission notice or a reference to
 * http://oss.sgi.com/projects/FreeB/
 * shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * SILICON GRAPHICS, INC. BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of Silicon Graphics, Inc.
 * shall not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization from
 * Silicon Graphics, Inc.
 */

/*
 * Added by VA Linux for XFree86 4.0.x
 */
typedef struct {
    int type;
    void (*resetExtension) (void);
    Bool (*initVisuals) (VisualPtr * visualp,
                         DepthPtr * depthp,
                         int *nvisualp,
                         int *ndepthp,
                         int *rootDepthp,
                         VisualID * defaultVisp,
                         unsigned long sizes, int bitsPerRGB);
    void (*setVisualConfigs) (int nconfigs,
                              __GLXvisualConfig * configs, void **privates);
} __GLXextensionInfo;

extern GLboolean __glXFreeContext(__GLXcontext * glxc);
extern void __glXFlushContextCache(void);

extern void __glXFreeGLXWindow(__glXWindow * pGlxWindow);
extern void __glXFreeGLXPixmap(__GLXpixmap * pGlxPixmap);
extern void __glXFreeGLXPbuffer(__glXPbuffer * pGlxPbuffer);

extern void __glXNoSuchRenderOpcode(GLbyte *);
extern int __glXNoSuchSingleOpcode(__GLXclientState *, GLbyte *);
extern void __glXErrorCallBack(GLenum code);
extern void __glXClearErrorOccured(void);
extern GLboolean __glXErrorOccured(void);
extern void __glXResetLargeCommandStatus(__GLXclientState *);

extern int __glXQueryContextInfoEXT(__GLXclientState * cl, GLbyte * pc);
extern int __glXSwapQueryContextInfoEXT(__GLXclientState * cl, char *pc);

extern Bool __glXCoreType(void);

#endif                          /* _glxext_h_ */
