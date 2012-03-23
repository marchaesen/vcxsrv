#ifndef _GLX_server_h_
#define _GLX_server_h_

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

#include "dmx.h"

#include <misc.h>
#include <dixstruct.h>
#include <pixmapstr.h>
#include <gcstruct.h>
#include <extnsionst.h>
#include <resource.h>
#include <scrnintstr.h>

#define GL_GLEXT_PROTOTYPES     /* we want prototypes */
#include <GL/gl.h>
#include <GL/glxproto.h>
#include <GL/glxint.h>

#include "glxscreens.h"
#include "glxdrawable.h"
#include "glxcontext.h"
#include "glxerror.h"

#define GLX_SERVER_MAJOR_VERSION 1
#define GLX_SERVER_MINOR_VERSION 3

#ifndef True
#define True 1
#endif
#ifndef False
#define False 0
#endif

/*
** GLX resources.
typedef XID GLXContextID;
typedef XID GLXPixmap;
typedef XID GLXDrawable;
typedef XID GLXWindow;
typedef XID GLXPbuffer;

typedef struct __GLXcontextRec *GLXContext;
*/
typedef struct __GLXclientStateRec __GLXclientState;

extern __GLXscreenInfo *__glXActiveScreens;
extern GLint __glXNumActiveScreens;

/************************************************************************/

/*
** The last context used (from the server's persective) is cached.
*/
extern __GLXcontext *__glXLastContext;
extern __GLXcontext *__glXForceCurrent(__GLXclientState *, GLXContextTag,
                                       int *);

/************************************************************************/

typedef struct {
    int elem_size;              /* element size in bytes */
    int nelems;                 /* number of elements to swap */
    void (*swapfunc) (GLbyte * pc);
} __GLXRenderSwapInfo;

/*
** State kept per client.
*/
struct __GLXclientStateRec {
    /*
     ** Whether this structure is currently being used to support a client.
     */
    Bool inUse;

    /*
     ** Buffer for returned data.
     */
    GLbyte *returnBuf;
    GLint returnBufSize;

    /*
     ** Keep a list of all the contexts that are current for this client's
     ** threads.
     */
    __GLXcontext **currentContexts;
    DrawablePtr *currentDrawables;
    GLint numCurrentContexts;

    /* Back pointer to X client record */
    ClientPtr client;

    int GLClientmajorVersion;
    int GLClientminorVersion;
    char *GLClientextensions;

    GLXContextTag *be_currentCTag;
    Display **be_displays;

    /*
     ** Keep track of large rendering commands, which span multiple requests.
     */
    GLint largeCmdBytesSoFar;   /* bytes received so far        */
    GLint largeCmdBytesTotal;   /* total bytes expected         */
    GLint largeCmdRequestsSoFar;        /* requests received so far     */
    GLint largeCmdRequestsTotal;        /* total requests expected      */
    void (*largeCmdRequestsSwapProc) (GLbyte *);
    __GLXRenderSwapInfo *largeCmdRequestsSwap_info;
    GLbyte *largeCmdBuf;
    GLint largeCmdBufSize;
    GLint largeCmdMaxReqDataSize;

};

extern __GLXclientState *__glXClients[];

/************************************************************************/

/*
** Dispatch tables.
*/
typedef void (*__GLXdispatchRenderProcPtr) (GLbyte *);
typedef int (*__GLXdispatchSingleProcPtr) (__GLXclientState *, GLbyte *);
typedef int (*__GLXdispatchVendorPrivProcPtr) (__GLXclientState *, GLbyte *);
extern __GLXdispatchSingleProcPtr __glXSingleTable[];
extern __GLXdispatchVendorPrivProcPtr __glXVendorPrivTable_EXT[];
extern __GLXdispatchSingleProcPtr __glXSwapSingleTable[];
extern __GLXdispatchVendorPrivProcPtr __glXSwapVendorPrivTable_EXT[];
extern __GLXdispatchRenderProcPtr __glXSwapRenderTable[];

extern __GLXRenderSwapInfo __glXSwapRenderTable_EXT[];

/*
 * Dispatch for GLX commands.
 */
typedef int (*__GLXprocPtr) (__GLXclientState *, char *pc);
extern __GLXprocPtr __glXProcTable[];

/*
 * Tables for computing the size of each rendering command.
 */
typedef struct {
    int bytes;
    int (*varsize) (GLbyte * pc, Bool swap);
} __GLXrenderSizeData;
extern __GLXrenderSizeData __glXRenderSizeTable[];
extern __GLXrenderSizeData __glXRenderSizeTable_EXT[];

/************************************************************************/

/*
** X resources.
*/
extern RESTYPE __glXContextRes;
extern RESTYPE __glXClientRes;
extern RESTYPE __glXPixmapRes;
extern RESTYPE __glXDrawableRes;
extern RESTYPE __glXWindowRes;
extern RESTYPE __glXPbufferRes;

/************************************************************************/

/*
** Prototypes.
*/

extern char *__glXcombine_strings(const char *, const char *);

extern void __glXDisp_DrawArrays(GLbyte *);
extern void __glXDispSwap_DrawArrays(GLbyte *);

/*
** Routines for sending swapped replies.
*/

extern void __glXSwapMakeCurrentReply(ClientPtr client,
                                      xGLXMakeCurrentReadSGIReply * reply);

extern void __glXSwapIsDirectReply(ClientPtr client, xGLXIsDirectReply * reply);
extern void __glXSwapQueryVersionReply(ClientPtr client,
                                       xGLXQueryVersionReply * reply);
extern void __glXSwapQueryContextInfoEXTReply(ClientPtr client,
                                              xGLXQueryContextInfoEXTReply *
                                              reply, int *buf);
extern void glxSwapQueryExtensionsStringReply(ClientPtr client,
                                              xGLXQueryExtensionsStringReply *
                                              reply, char *buf);
extern void glxSwapQueryServerStringReply(ClientPtr client,
                                          xGLXQueryServerStringReply * reply,
                                          char *buf);
extern void __glXSwapQueryContextReply(ClientPtr client,
                                       xGLXQueryContextReply * reply, int *buf);
extern void __glXSwapGetDrawableAttributesReply(ClientPtr client,
                                                xGLXGetDrawableAttributesReply *
                                                reply, int *buf);
extern void __glXSwapQueryMaxSwapBarriersSGIXReply(ClientPtr client,
                                                   xGLXQueryMaxSwapBarriersSGIXReply
                                                   * reply);

/*
 * Routines for computing the size of variably-sized rendering commands.
 */

extern int __glXTypeSize(GLenum enm);
extern int __glXImageSize(GLenum format, GLenum type, GLsizei w, GLsizei h,
                          GLint rowLength, GLint skipRows, GLint alignment);
extern int __glXImage3DSize(GLenum format, GLenum type,
                            GLsizei w, GLsizei h, GLsizei d,
                            GLint imageHeight, GLint rowLength,
                            GLint skipImages, GLint skipRows, GLint alignment);

extern int __glXCallListsReqSize(GLbyte * pc, Bool swap);
extern int __glXBitmapReqSize(GLbyte * pc, Bool swap);
extern int __glXFogfvReqSize(GLbyte * pc, Bool swap);
extern int __glXFogivReqSize(GLbyte * pc, Bool swap);
extern int __glXLightfvReqSize(GLbyte * pc, Bool swap);
extern int __glXLightivReqSize(GLbyte * pc, Bool swap);
extern int __glXLightModelfvReqSize(GLbyte * pc, Bool swap);
extern int __glXLightModelivReqSize(GLbyte * pc, Bool swap);
extern int __glXMaterialfvReqSize(GLbyte * pc, Bool swap);
extern int __glXMaterialivReqSize(GLbyte * pc, Bool swap);
extern int __glXTexParameterfvReqSize(GLbyte * pc, Bool swap);
extern int __glXTexParameterivReqSize(GLbyte * pc, Bool swap);
extern int __glXTexImage1DReqSize(GLbyte * pc, Bool swap);
extern int __glXTexImage2DReqSize(GLbyte * pc, Bool swap);
extern int __glXTexEnvfvReqSize(GLbyte * pc, Bool swap);
extern int __glXTexEnvivReqSize(GLbyte * pc, Bool swap);
extern int __glXTexGendvReqSize(GLbyte * pc, Bool swap);
extern int __glXTexGenfvReqSize(GLbyte * pc, Bool swap);
extern int __glXTexGenivReqSize(GLbyte * pc, Bool swap);
extern int __glXMap1dReqSize(GLbyte * pc, Bool swap);
extern int __glXMap1fReqSize(GLbyte * pc, Bool swap);
extern int __glXMap2dReqSize(GLbyte * pc, Bool swap);
extern int __glXMap2fReqSize(GLbyte * pc, Bool swap);
extern int __glXPixelMapfvReqSize(GLbyte * pc, Bool swap);
extern int __glXPixelMapuivReqSize(GLbyte * pc, Bool swap);
extern int __glXPixelMapusvReqSize(GLbyte * pc, Bool swap);
extern int __glXDrawPixelsReqSize(GLbyte * pc, Bool swap);
extern int __glXDrawArraysSize(GLbyte * pc, Bool swap);
extern int __glXPrioritizeTexturesReqSize(GLbyte * pc, Bool swap);
extern int __glXTexSubImage1DReqSize(GLbyte * pc, Bool swap);
extern int __glXTexSubImage2DReqSize(GLbyte * pc, Bool swap);
extern int __glXTexImage3DReqSize(GLbyte * pc, Bool swap);
extern int __glXTexSubImage3DReqSize(GLbyte * pc, Bool swap);
extern int __glXConvolutionFilter1DReqSize(GLbyte * pc, Bool swap);
extern int __glXConvolutionFilter2DReqSize(GLbyte * pc, Bool swap);
extern int __glXConvolutionParameterivReqSize(GLbyte * pc, Bool swap);
extern int __glXConvolutionParameterfvReqSize(GLbyte * pc, Bool swap);
extern int __glXSeparableFilter2DReqSize(GLbyte * pc, Bool swap);
extern int __glXColorTableReqSize(GLbyte * pc, Bool swap);
extern int __glXColorSubTableReqSize(GLbyte * pc, Bool swap);
extern int __glXColorTableParameterfvReqSize(GLbyte * pc, Bool swap);
extern int __glXColorTableParameterivReqSize(GLbyte * pc, Bool swap);

/*
 * Routines for computing the size of returned data.
 */
extern int __glXConvolutionParameterivSize(GLenum pname);
extern int __glXConvolutionParameterfvSize(GLenum pname);
extern int __glXColorTableParameterfvSize(GLenum pname);
extern int __glXColorTableParameterivSize(GLenum pname);

extern void __glXFreeGLXWindow(__glXWindow * pGlxWindow);
extern void __glXFreeGLXPbuffer(__glXPbuffer * pGlxPbuffer);

extern int __glXVersionMajor;
extern int __glXVersionMinor;

#define __GLX_IS_VERSION_SUPPORTED(major,minor) \
         ( (__glXVersionMajor > (major)) || \
           ((__glXVersionMajor == (major)) && (__glXVersionMinor >= (minor))) )

#endif                          /* !__GLX_server_h__ */
