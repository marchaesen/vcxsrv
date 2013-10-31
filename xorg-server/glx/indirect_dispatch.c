/* DO NOT EDIT - This file generated automatically by glX_proto_recv.py (from Mesa) script */

/*
 * (C) Copyright IBM Corporation 2005
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * IBM,
 * AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <inttypes.h>
#include "glxserver.h"
#include "indirect_size.h"
#include "indirect_size_get.h"
#include "indirect_dispatch.h"
#include "glxbyteorder.h"
#include "indirect_util.h"
#include "singlesize.h"

#define __GLX_PAD(x)  (((x) + 3) & ~3)

typedef struct {
    __GLX_PIXEL_3D_HDR;
} __GLXpixel3DHeader;

extern GLboolean __glXErrorOccured(void);
extern void __glXClearErrorOccured(void);

static const unsigned dummy_answer[2] = { 0, 0 };

int
__glXDisp_NewList(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        glNewList(*(GLuint *) (pc + 0), *(GLenum *) (pc + 4));
        error = Success;
    }

    return error;
}

int
__glXDisp_EndList(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        glEndList();
        error = Success;
    }

    return error;
}

void
__glXDisp_CallList(GLbyte * pc)
{
    glCallList(*(GLuint *) (pc + 0));
}

void
__glXDisp_CallLists(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 0);
    const GLenum type = *(GLenum *) (pc + 4);
    const GLvoid *lists = (const GLvoid *) (pc + 8);

    lists = (const GLvoid *) (pc + 8);

    glCallLists(n, type, lists);
}

int
__glXDisp_DeleteLists(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        glDeleteLists(*(GLuint *) (pc + 0), *(GLsizei *) (pc + 4));
        error = Success;
    }

    return error;
}

int
__glXDisp_GenLists(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        GLuint retval;

        retval = glGenLists(*(GLsizei *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

void
__glXDisp_ListBase(GLbyte * pc)
{
    glListBase(*(GLuint *) (pc + 0));
}

void
__glXDisp_Begin(GLbyte * pc)
{
    glBegin(*(GLenum *) (pc + 0));
}

void
__glXDisp_Bitmap(GLbyte * pc)
{
    const GLubyte *const bitmap = (const GLubyte *) ((pc + 44));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glBitmap(*(GLsizei *) (pc + 20),
             *(GLsizei *) (pc + 24),
             *(GLfloat *) (pc + 28),
             *(GLfloat *) (pc + 32),
             *(GLfloat *) (pc + 36), *(GLfloat *) (pc + 40), bitmap);
}

void
__glXDisp_Color3bv(GLbyte * pc)
{
    glColor3bv((const GLbyte *) (pc + 0));
}

void
__glXDisp_Color3dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 24);
        pc -= 4;
    }
#endif

    glColor3dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_Color3fv(GLbyte * pc)
{
    glColor3fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_Color3iv(GLbyte * pc)
{
    glColor3iv((const GLint *) (pc + 0));
}

void
__glXDisp_Color3sv(GLbyte * pc)
{
    glColor3sv((const GLshort *) (pc + 0));
}

void
__glXDisp_Color3ubv(GLbyte * pc)
{
    glColor3ubv((const GLubyte *) (pc + 0));
}

void
__glXDisp_Color3uiv(GLbyte * pc)
{
    glColor3uiv((const GLuint *) (pc + 0));
}

void
__glXDisp_Color3usv(GLbyte * pc)
{
    glColor3usv((const GLushort *) (pc + 0));
}

void
__glXDisp_Color4bv(GLbyte * pc)
{
    glColor4bv((const GLbyte *) (pc + 0));
}

void
__glXDisp_Color4dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 32);
        pc -= 4;
    }
#endif

    glColor4dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_Color4fv(GLbyte * pc)
{
    glColor4fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_Color4iv(GLbyte * pc)
{
    glColor4iv((const GLint *) (pc + 0));
}

void
__glXDisp_Color4sv(GLbyte * pc)
{
    glColor4sv((const GLshort *) (pc + 0));
}

void
__glXDisp_Color4ubv(GLbyte * pc)
{
    glColor4ubv((const GLubyte *) (pc + 0));
}

void
__glXDisp_Color4uiv(GLbyte * pc)
{
    glColor4uiv((const GLuint *) (pc + 0));
}

void
__glXDisp_Color4usv(GLbyte * pc)
{
    glColor4usv((const GLushort *) (pc + 0));
}

void
__glXDisp_EdgeFlagv(GLbyte * pc)
{
    glEdgeFlagv((const GLboolean *) (pc + 0));
}

void
__glXDisp_End(GLbyte * pc)
{
    glEnd();
}

void
__glXDisp_Indexdv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 8);
        pc -= 4;
    }
#endif

    glIndexdv((const GLdouble *) (pc + 0));
}

void
__glXDisp_Indexfv(GLbyte * pc)
{
    glIndexfv((const GLfloat *) (pc + 0));
}

void
__glXDisp_Indexiv(GLbyte * pc)
{
    glIndexiv((const GLint *) (pc + 0));
}

void
__glXDisp_Indexsv(GLbyte * pc)
{
    glIndexsv((const GLshort *) (pc + 0));
}

void
__glXDisp_Normal3bv(GLbyte * pc)
{
    glNormal3bv((const GLbyte *) (pc + 0));
}

void
__glXDisp_Normal3dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 24);
        pc -= 4;
    }
#endif

    glNormal3dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_Normal3fv(GLbyte * pc)
{
    glNormal3fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_Normal3iv(GLbyte * pc)
{
    glNormal3iv((const GLint *) (pc + 0));
}

void
__glXDisp_Normal3sv(GLbyte * pc)
{
    glNormal3sv((const GLshort *) (pc + 0));
}

void
__glXDisp_RasterPos2dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 16);
        pc -= 4;
    }
#endif

    glRasterPos2dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_RasterPos2fv(GLbyte * pc)
{
    glRasterPos2fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_RasterPos2iv(GLbyte * pc)
{
    glRasterPos2iv((const GLint *) (pc + 0));
}

void
__glXDisp_RasterPos2sv(GLbyte * pc)
{
    glRasterPos2sv((const GLshort *) (pc + 0));
}

void
__glXDisp_RasterPos3dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 24);
        pc -= 4;
    }
#endif

    glRasterPos3dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_RasterPos3fv(GLbyte * pc)
{
    glRasterPos3fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_RasterPos3iv(GLbyte * pc)
{
    glRasterPos3iv((const GLint *) (pc + 0));
}

void
__glXDisp_RasterPos3sv(GLbyte * pc)
{
    glRasterPos3sv((const GLshort *) (pc + 0));
}

void
__glXDisp_RasterPos4dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 32);
        pc -= 4;
    }
#endif

    glRasterPos4dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_RasterPos4fv(GLbyte * pc)
{
    glRasterPos4fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_RasterPos4iv(GLbyte * pc)
{
    glRasterPos4iv((const GLint *) (pc + 0));
}

void
__glXDisp_RasterPos4sv(GLbyte * pc)
{
    glRasterPos4sv((const GLshort *) (pc + 0));
}

void
__glXDisp_Rectdv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 32);
        pc -= 4;
    }
#endif

    glRectdv((const GLdouble *) (pc + 0), (const GLdouble *) (pc + 16));
}

void
__glXDisp_Rectfv(GLbyte * pc)
{
    glRectfv((const GLfloat *) (pc + 0), (const GLfloat *) (pc + 8));
}

void
__glXDisp_Rectiv(GLbyte * pc)
{
    glRectiv((const GLint *) (pc + 0), (const GLint *) (pc + 8));
}

void
__glXDisp_Rectsv(GLbyte * pc)
{
    glRectsv((const GLshort *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_TexCoord1dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 8);
        pc -= 4;
    }
#endif

    glTexCoord1dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_TexCoord1fv(GLbyte * pc)
{
    glTexCoord1fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_TexCoord1iv(GLbyte * pc)
{
    glTexCoord1iv((const GLint *) (pc + 0));
}

void
__glXDisp_TexCoord1sv(GLbyte * pc)
{
    glTexCoord1sv((const GLshort *) (pc + 0));
}

void
__glXDisp_TexCoord2dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 16);
        pc -= 4;
    }
#endif

    glTexCoord2dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_TexCoord2fv(GLbyte * pc)
{
    glTexCoord2fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_TexCoord2iv(GLbyte * pc)
{
    glTexCoord2iv((const GLint *) (pc + 0));
}

void
__glXDisp_TexCoord2sv(GLbyte * pc)
{
    glTexCoord2sv((const GLshort *) (pc + 0));
}

void
__glXDisp_TexCoord3dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 24);
        pc -= 4;
    }
#endif

    glTexCoord3dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_TexCoord3fv(GLbyte * pc)
{
    glTexCoord3fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_TexCoord3iv(GLbyte * pc)
{
    glTexCoord3iv((const GLint *) (pc + 0));
}

void
__glXDisp_TexCoord3sv(GLbyte * pc)
{
    glTexCoord3sv((const GLshort *) (pc + 0));
}

void
__glXDisp_TexCoord4dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 32);
        pc -= 4;
    }
#endif

    glTexCoord4dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_TexCoord4fv(GLbyte * pc)
{
    glTexCoord4fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_TexCoord4iv(GLbyte * pc)
{
    glTexCoord4iv((const GLint *) (pc + 0));
}

void
__glXDisp_TexCoord4sv(GLbyte * pc)
{
    glTexCoord4sv((const GLshort *) (pc + 0));
}

void
__glXDisp_Vertex2dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 16);
        pc -= 4;
    }
#endif

    glVertex2dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_Vertex2fv(GLbyte * pc)
{
    glVertex2fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_Vertex2iv(GLbyte * pc)
{
    glVertex2iv((const GLint *) (pc + 0));
}

void
__glXDisp_Vertex2sv(GLbyte * pc)
{
    glVertex2sv((const GLshort *) (pc + 0));
}

void
__glXDisp_Vertex3dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 24);
        pc -= 4;
    }
#endif

    glVertex3dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_Vertex3fv(GLbyte * pc)
{
    glVertex3fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_Vertex3iv(GLbyte * pc)
{
    glVertex3iv((const GLint *) (pc + 0));
}

void
__glXDisp_Vertex3sv(GLbyte * pc)
{
    glVertex3sv((const GLshort *) (pc + 0));
}

void
__glXDisp_Vertex4dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 32);
        pc -= 4;
    }
#endif

    glVertex4dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_Vertex4fv(GLbyte * pc)
{
    glVertex4fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_Vertex4iv(GLbyte * pc)
{
    glVertex4iv((const GLint *) (pc + 0));
}

void
__glXDisp_Vertex4sv(GLbyte * pc)
{
    glVertex4sv((const GLshort *) (pc + 0));
}

void
__glXDisp_ClipPlane(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 36);
        pc -= 4;
    }
#endif

    glClipPlane(*(GLenum *) (pc + 32), (const GLdouble *) (pc + 0));
}

void
__glXDisp_ColorMaterial(GLbyte * pc)
{
    glColorMaterial(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4));
}

void
__glXDisp_CullFace(GLbyte * pc)
{
    glCullFace(*(GLenum *) (pc + 0));
}

void
__glXDisp_Fogf(GLbyte * pc)
{
    glFogf(*(GLenum *) (pc + 0), *(GLfloat *) (pc + 4));
}

void
__glXDisp_Fogfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 0);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 4);

    glFogfv(pname, params);
}

void
__glXDisp_Fogi(GLbyte * pc)
{
    glFogi(*(GLenum *) (pc + 0), *(GLint *) (pc + 4));
}

void
__glXDisp_Fogiv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 0);
    const GLint *params;

    params = (const GLint *) (pc + 4);

    glFogiv(pname, params);
}

void
__glXDisp_FrontFace(GLbyte * pc)
{
    glFrontFace(*(GLenum *) (pc + 0));
}

void
__glXDisp_Hint(GLbyte * pc)
{
    glHint(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4));
}

void
__glXDisp_Lightf(GLbyte * pc)
{
    glLightf(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_Lightfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 8);

    glLightfv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_Lighti(GLbyte * pc)
{
    glLighti(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4), *(GLint *) (pc + 8));
}

void
__glXDisp_Lightiv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLint *params;

    params = (const GLint *) (pc + 8);

    glLightiv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_LightModelf(GLbyte * pc)
{
    glLightModelf(*(GLenum *) (pc + 0), *(GLfloat *) (pc + 4));
}

void
__glXDisp_LightModelfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 0);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 4);

    glLightModelfv(pname, params);
}

void
__glXDisp_LightModeli(GLbyte * pc)
{
    glLightModeli(*(GLenum *) (pc + 0), *(GLint *) (pc + 4));
}

void
__glXDisp_LightModeliv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 0);
    const GLint *params;

    params = (const GLint *) (pc + 4);

    glLightModeliv(pname, params);
}

void
__glXDisp_LineStipple(GLbyte * pc)
{
    glLineStipple(*(GLint *) (pc + 0), *(GLushort *) (pc + 4));
}

void
__glXDisp_LineWidth(GLbyte * pc)
{
    glLineWidth(*(GLfloat *) (pc + 0));
}

void
__glXDisp_Materialf(GLbyte * pc)
{
    glMaterialf(*(GLenum *) (pc + 0),
                *(GLenum *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_Materialfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 8);

    glMaterialfv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_Materiali(GLbyte * pc)
{
    glMateriali(*(GLenum *) (pc + 0),
                *(GLenum *) (pc + 4), *(GLint *) (pc + 8));
}

void
__glXDisp_Materialiv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLint *params;

    params = (const GLint *) (pc + 8);

    glMaterialiv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_PointSize(GLbyte * pc)
{
    glPointSize(*(GLfloat *) (pc + 0));
}

void
__glXDisp_PolygonMode(GLbyte * pc)
{
    glPolygonMode(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4));
}

void
__glXDisp_PolygonStipple(GLbyte * pc)
{
    const GLubyte *const mask = (const GLubyte *) ((pc + 20));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glPolygonStipple(mask);
}

void
__glXDisp_Scissor(GLbyte * pc)
{
    glScissor(*(GLint *) (pc + 0),
              *(GLint *) (pc + 4),
              *(GLsizei *) (pc + 8), *(GLsizei *) (pc + 12));
}

void
__glXDisp_ShadeModel(GLbyte * pc)
{
    glShadeModel(*(GLenum *) (pc + 0));
}

void
__glXDisp_TexParameterf(GLbyte * pc)
{
    glTexParameterf(*(GLenum *) (pc + 0),
                    *(GLenum *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_TexParameterfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 8);

    glTexParameterfv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_TexParameteri(GLbyte * pc)
{
    glTexParameteri(*(GLenum *) (pc + 0),
                    *(GLenum *) (pc + 4), *(GLint *) (pc + 8));
}

void
__glXDisp_TexParameteriv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLint *params;

    params = (const GLint *) (pc + 8);

    glTexParameteriv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_TexImage1D(GLbyte * pc)
{
    const GLvoid *const pixels = (const GLvoid *) ((pc + 52));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glTexImage1D(*(GLenum *) (pc + 20),
                 *(GLint *) (pc + 24),
                 *(GLint *) (pc + 28),
                 *(GLsizei *) (pc + 32),
                 *(GLint *) (pc + 40),
                 *(GLenum *) (pc + 44), *(GLenum *) (pc + 48), pixels);
}

void
__glXDisp_TexImage2D(GLbyte * pc)
{
    const GLvoid *const pixels = (const GLvoid *) ((pc + 52));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glTexImage2D(*(GLenum *) (pc + 20),
                 *(GLint *) (pc + 24),
                 *(GLint *) (pc + 28),
                 *(GLsizei *) (pc + 32),
                 *(GLsizei *) (pc + 36),
                 *(GLint *) (pc + 40),
                 *(GLenum *) (pc + 44), *(GLenum *) (pc + 48), pixels);
}

void
__glXDisp_TexEnvf(GLbyte * pc)
{
    glTexEnvf(*(GLenum *) (pc + 0),
              *(GLenum *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_TexEnvfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 8);

    glTexEnvfv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_TexEnvi(GLbyte * pc)
{
    glTexEnvi(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4), *(GLint *) (pc + 8));
}

void
__glXDisp_TexEnviv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLint *params;

    params = (const GLint *) (pc + 8);

    glTexEnviv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_TexGend(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 16);
        pc -= 4;
    }
#endif

    glTexGend(*(GLenum *) (pc + 8),
              *(GLenum *) (pc + 12), *(GLdouble *) (pc + 0));
}

void
__glXDisp_TexGendv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLdouble *params;

#ifdef __GLX_ALIGN64
    const GLuint compsize = __glTexGendv_size(pname);
    const GLuint cmdlen = 12 + __GLX_PAD((compsize * 8)) - 4;

    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, cmdlen);
        pc -= 4;
    }
#endif

    params = (const GLdouble *) (pc + 8);

    glTexGendv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_TexGenf(GLbyte * pc)
{
    glTexGenf(*(GLenum *) (pc + 0),
              *(GLenum *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_TexGenfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 8);

    glTexGenfv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_TexGeni(GLbyte * pc)
{
    glTexGeni(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4), *(GLint *) (pc + 8));
}

void
__glXDisp_TexGeniv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLint *params;

    params = (const GLint *) (pc + 8);

    glTexGeniv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_InitNames(GLbyte * pc)
{
    glInitNames();
}

void
__glXDisp_LoadName(GLbyte * pc)
{
    glLoadName(*(GLuint *) (pc + 0));
}

void
__glXDisp_PassThrough(GLbyte * pc)
{
    glPassThrough(*(GLfloat *) (pc + 0));
}

void
__glXDisp_PopName(GLbyte * pc)
{
    glPopName();
}

void
__glXDisp_PushName(GLbyte * pc)
{
    glPushName(*(GLuint *) (pc + 0));
}

void
__glXDisp_DrawBuffer(GLbyte * pc)
{
    glDrawBuffer(*(GLenum *) (pc + 0));
}

void
__glXDisp_Clear(GLbyte * pc)
{
    glClear(*(GLbitfield *) (pc + 0));
}

void
__glXDisp_ClearAccum(GLbyte * pc)
{
    glClearAccum(*(GLfloat *) (pc + 0),
                 *(GLfloat *) (pc + 4),
                 *(GLfloat *) (pc + 8), *(GLfloat *) (pc + 12));
}

void
__glXDisp_ClearIndex(GLbyte * pc)
{
    glClearIndex(*(GLfloat *) (pc + 0));
}

void
__glXDisp_ClearColor(GLbyte * pc)
{
    glClearColor(*(GLclampf *) (pc + 0),
                 *(GLclampf *) (pc + 4),
                 *(GLclampf *) (pc + 8), *(GLclampf *) (pc + 12));
}

void
__glXDisp_ClearStencil(GLbyte * pc)
{
    glClearStencil(*(GLint *) (pc + 0));
}

void
__glXDisp_ClearDepth(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 8);
        pc -= 4;
    }
#endif

    glClearDepth(*(GLclampd *) (pc + 0));
}

void
__glXDisp_StencilMask(GLbyte * pc)
{
    glStencilMask(*(GLuint *) (pc + 0));
}

void
__glXDisp_ColorMask(GLbyte * pc)
{
    glColorMask(*(GLboolean *) (pc + 0),
                *(GLboolean *) (pc + 1),
                *(GLboolean *) (pc + 2), *(GLboolean *) (pc + 3));
}

void
__glXDisp_DepthMask(GLbyte * pc)
{
    glDepthMask(*(GLboolean *) (pc + 0));
}

void
__glXDisp_IndexMask(GLbyte * pc)
{
    glIndexMask(*(GLuint *) (pc + 0));
}

void
__glXDisp_Accum(GLbyte * pc)
{
    glAccum(*(GLenum *) (pc + 0), *(GLfloat *) (pc + 4));
}

void
__glXDisp_Disable(GLbyte * pc)
{
    glDisable(*(GLenum *) (pc + 0));
}

void
__glXDisp_Enable(GLbyte * pc)
{
    glEnable(*(GLenum *) (pc + 0));
}

void
__glXDisp_PopAttrib(GLbyte * pc)
{
    glPopAttrib();
}

void
__glXDisp_PushAttrib(GLbyte * pc)
{
    glPushAttrib(*(GLbitfield *) (pc + 0));
}

void
__glXDisp_MapGrid1d(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 20);
        pc -= 4;
    }
#endif

    glMapGrid1d(*(GLint *) (pc + 16),
                *(GLdouble *) (pc + 0), *(GLdouble *) (pc + 8));
}

void
__glXDisp_MapGrid1f(GLbyte * pc)
{
    glMapGrid1f(*(GLint *) (pc + 0),
                *(GLfloat *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_MapGrid2d(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 40);
        pc -= 4;
    }
#endif

    glMapGrid2d(*(GLint *) (pc + 32),
                *(GLdouble *) (pc + 0),
                *(GLdouble *) (pc + 8),
                *(GLint *) (pc + 36),
                *(GLdouble *) (pc + 16), *(GLdouble *) (pc + 24));
}

void
__glXDisp_MapGrid2f(GLbyte * pc)
{
    glMapGrid2f(*(GLint *) (pc + 0),
                *(GLfloat *) (pc + 4),
                *(GLfloat *) (pc + 8),
                *(GLint *) (pc + 12),
                *(GLfloat *) (pc + 16), *(GLfloat *) (pc + 20));
}

void
__glXDisp_EvalCoord1dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 8);
        pc -= 4;
    }
#endif

    glEvalCoord1dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_EvalCoord1fv(GLbyte * pc)
{
    glEvalCoord1fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_EvalCoord2dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 16);
        pc -= 4;
    }
#endif

    glEvalCoord2dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_EvalCoord2fv(GLbyte * pc)
{
    glEvalCoord2fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_EvalMesh1(GLbyte * pc)
{
    glEvalMesh1(*(GLenum *) (pc + 0), *(GLint *) (pc + 4), *(GLint *) (pc + 8));
}

void
__glXDisp_EvalPoint1(GLbyte * pc)
{
    glEvalPoint1(*(GLint *) (pc + 0));
}

void
__glXDisp_EvalMesh2(GLbyte * pc)
{
    glEvalMesh2(*(GLenum *) (pc + 0),
                *(GLint *) (pc + 4),
                *(GLint *) (pc + 8),
                *(GLint *) (pc + 12), *(GLint *) (pc + 16));
}

void
__glXDisp_EvalPoint2(GLbyte * pc)
{
    glEvalPoint2(*(GLint *) (pc + 0), *(GLint *) (pc + 4));
}

void
__glXDisp_AlphaFunc(GLbyte * pc)
{
    glAlphaFunc(*(GLenum *) (pc + 0), *(GLclampf *) (pc + 4));
}

void
__glXDisp_BlendFunc(GLbyte * pc)
{
    glBlendFunc(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4));
}

void
__glXDisp_LogicOp(GLbyte * pc)
{
    glLogicOp(*(GLenum *) (pc + 0));
}

void
__glXDisp_StencilFunc(GLbyte * pc)
{
    glStencilFunc(*(GLenum *) (pc + 0),
                  *(GLint *) (pc + 4), *(GLuint *) (pc + 8));
}

void
__glXDisp_StencilOp(GLbyte * pc)
{
    glStencilOp(*(GLenum *) (pc + 0),
                *(GLenum *) (pc + 4), *(GLenum *) (pc + 8));
}

void
__glXDisp_DepthFunc(GLbyte * pc)
{
    glDepthFunc(*(GLenum *) (pc + 0));
}

void
__glXDisp_PixelZoom(GLbyte * pc)
{
    glPixelZoom(*(GLfloat *) (pc + 0), *(GLfloat *) (pc + 4));
}

void
__glXDisp_PixelTransferf(GLbyte * pc)
{
    glPixelTransferf(*(GLenum *) (pc + 0), *(GLfloat *) (pc + 4));
}

void
__glXDisp_PixelTransferi(GLbyte * pc)
{
    glPixelTransferi(*(GLenum *) (pc + 0), *(GLint *) (pc + 4));
}

int
__glXDisp_PixelStoref(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        glPixelStoref(*(GLenum *) (pc + 0), *(GLfloat *) (pc + 4));
        error = Success;
    }

    return error;
}

int
__glXDisp_PixelStorei(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        glPixelStorei(*(GLenum *) (pc + 0), *(GLint *) (pc + 4));
        error = Success;
    }

    return error;
}

void
__glXDisp_PixelMapfv(GLbyte * pc)
{
    const GLsizei mapsize = *(GLsizei *) (pc + 4);

    glPixelMapfv(*(GLenum *) (pc + 0), mapsize, (const GLfloat *) (pc + 8));
}

void
__glXDisp_PixelMapuiv(GLbyte * pc)
{
    const GLsizei mapsize = *(GLsizei *) (pc + 4);

    glPixelMapuiv(*(GLenum *) (pc + 0), mapsize, (const GLuint *) (pc + 8));
}

void
__glXDisp_PixelMapusv(GLbyte * pc)
{
    const GLsizei mapsize = *(GLsizei *) (pc + 4);

    glPixelMapusv(*(GLenum *) (pc + 0), mapsize, (const GLushort *) (pc + 8));
}

void
__glXDisp_ReadBuffer(GLbyte * pc)
{
    glReadBuffer(*(GLenum *) (pc + 0));
}

void
__glXDisp_CopyPixels(GLbyte * pc)
{
    glCopyPixels(*(GLint *) (pc + 0),
                 *(GLint *) (pc + 4),
                 *(GLsizei *) (pc + 8),
                 *(GLsizei *) (pc + 12), *(GLenum *) (pc + 16));
}

void
__glXDisp_DrawPixels(GLbyte * pc)
{
    const GLvoid *const pixels = (const GLvoid *) ((pc + 36));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glDrawPixels(*(GLsizei *) (pc + 20),
                 *(GLsizei *) (pc + 24),
                 *(GLenum *) (pc + 28), *(GLenum *) (pc + 32), pixels);
}

int
__glXDisp_GetBooleanv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 0);

        const GLuint compsize = __glGetBooleanv_size(pname);
        GLboolean answerBuffer[200];
        GLboolean *params =
            __glXGetAnswerBuffer(cl, compsize, answerBuffer,
                                 sizeof(answerBuffer), 1);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetBooleanv(pname, params);
        __glXSendReply(cl->client, params, compsize, 1, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetClipPlane(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        GLdouble equation[4];

        glGetClipPlane(*(GLenum *) (pc + 0), equation);
        __glXSendReply(cl->client, equation, 4, 8, GL_TRUE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetDoublev(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 0);

        const GLuint compsize = __glGetDoublev_size(pname);
        GLdouble answerBuffer[200];
        GLdouble *params =
            __glXGetAnswerBuffer(cl, compsize * 8, answerBuffer,
                                 sizeof(answerBuffer), 8);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetDoublev(pname, params);
        __glXSendReply(cl->client, params, compsize, 8, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetError(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        GLenum retval;

        retval = glGetError();
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetFloatv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 0);

        const GLuint compsize = __glGetFloatv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetFloatv(pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetIntegerv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 0);

        const GLuint compsize = __glGetIntegerv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetIntegerv(pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetLightfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetLightfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetLightfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetLightiv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetLightiv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetLightiv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMapdv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum target = *(GLenum *) (pc + 0);
        const GLenum query = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMapdv_size(target, query);
        GLdouble answerBuffer[200];
        GLdouble *v =
            __glXGetAnswerBuffer(cl, compsize * 8, answerBuffer,
                                 sizeof(answerBuffer), 8);

        if (v == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMapdv(target, query, v);
        __glXSendReply(cl->client, v, compsize, 8, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMapfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum target = *(GLenum *) (pc + 0);
        const GLenum query = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMapfv_size(target, query);
        GLfloat answerBuffer[200];
        GLfloat *v =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (v == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMapfv(target, query, v);
        __glXSendReply(cl->client, v, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMapiv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum target = *(GLenum *) (pc + 0);
        const GLenum query = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMapiv_size(target, query);
        GLint answerBuffer[200];
        GLint *v =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (v == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMapiv(target, query, v);
        __glXSendReply(cl->client, v, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMaterialfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMaterialfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMaterialfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMaterialiv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMaterialiv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMaterialiv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetPixelMapfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum map = *(GLenum *) (pc + 0);

        const GLuint compsize = __glGetPixelMapfv_size(map);
        GLfloat answerBuffer[200];
        GLfloat *values =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (values == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetPixelMapfv(map, values);
        __glXSendReply(cl->client, values, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetPixelMapuiv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum map = *(GLenum *) (pc + 0);

        const GLuint compsize = __glGetPixelMapuiv_size(map);
        GLuint answerBuffer[200];
        GLuint *values =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (values == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetPixelMapuiv(map, values);
        __glXSendReply(cl->client, values, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetPixelMapusv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum map = *(GLenum *) (pc + 0);

        const GLuint compsize = __glGetPixelMapusv_size(map);
        GLushort answerBuffer[200];
        GLushort *values =
            __glXGetAnswerBuffer(cl, compsize * 2, answerBuffer,
                                 sizeof(answerBuffer), 2);

        if (values == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetPixelMapusv(map, values);
        __glXSendReply(cl->client, values, compsize, 2, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexEnvfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetTexEnvfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexEnvfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexEnviv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetTexEnviv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexEnviv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexGendv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetTexGendv_size(pname);
        GLdouble answerBuffer[200];
        GLdouble *params =
            __glXGetAnswerBuffer(cl, compsize * 8, answerBuffer,
                                 sizeof(answerBuffer), 8);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexGendv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 8, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexGenfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetTexGenfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexGenfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexGeniv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetTexGeniv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexGeniv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexParameterfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetTexParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexParameteriv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetTexParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexLevelParameterfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 8);

        const GLuint compsize = __glGetTexLevelParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexLevelParameterfv(*(GLenum *) (pc + 0),
                                 *(GLint *) (pc + 4), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetTexLevelParameteriv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 8);

        const GLuint compsize = __glGetTexLevelParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetTexLevelParameteriv(*(GLenum *) (pc + 0),
                                 *(GLint *) (pc + 4), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_IsEnabled(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        GLboolean retval;

        retval = glIsEnabled(*(GLenum *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

int
__glXDisp_IsList(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        GLboolean retval;

        retval = glIsList(*(GLuint *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

void
__glXDisp_DepthRange(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 16);
        pc -= 4;
    }
#endif

    glDepthRange(*(GLclampd *) (pc + 0), *(GLclampd *) (pc + 8));
}

void
__glXDisp_Frustum(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 48);
        pc -= 4;
    }
#endif

    glFrustum(*(GLdouble *) (pc + 0),
              *(GLdouble *) (pc + 8),
              *(GLdouble *) (pc + 16),
              *(GLdouble *) (pc + 24),
              *(GLdouble *) (pc + 32), *(GLdouble *) (pc + 40));
}

void
__glXDisp_LoadIdentity(GLbyte * pc)
{
    glLoadIdentity();
}

void
__glXDisp_LoadMatrixf(GLbyte * pc)
{
    glLoadMatrixf((const GLfloat *) (pc + 0));
}

void
__glXDisp_LoadMatrixd(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 128);
        pc -= 4;
    }
#endif

    glLoadMatrixd((const GLdouble *) (pc + 0));
}

void
__glXDisp_MatrixMode(GLbyte * pc)
{
    glMatrixMode(*(GLenum *) (pc + 0));
}

void
__glXDisp_MultMatrixf(GLbyte * pc)
{
    glMultMatrixf((const GLfloat *) (pc + 0));
}

void
__glXDisp_MultMatrixd(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 128);
        pc -= 4;
    }
#endif

    glMultMatrixd((const GLdouble *) (pc + 0));
}

void
__glXDisp_Ortho(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 48);
        pc -= 4;
    }
#endif

    glOrtho(*(GLdouble *) (pc + 0),
            *(GLdouble *) (pc + 8),
            *(GLdouble *) (pc + 16),
            *(GLdouble *) (pc + 24),
            *(GLdouble *) (pc + 32), *(GLdouble *) (pc + 40));
}

void
__glXDisp_PopMatrix(GLbyte * pc)
{
    glPopMatrix();
}

void
__glXDisp_PushMatrix(GLbyte * pc)
{
    glPushMatrix();
}

void
__glXDisp_Rotated(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 32);
        pc -= 4;
    }
#endif

    glRotated(*(GLdouble *) (pc + 0),
              *(GLdouble *) (pc + 8),
              *(GLdouble *) (pc + 16), *(GLdouble *) (pc + 24));
}

void
__glXDisp_Rotatef(GLbyte * pc)
{
    glRotatef(*(GLfloat *) (pc + 0),
              *(GLfloat *) (pc + 4),
              *(GLfloat *) (pc + 8), *(GLfloat *) (pc + 12));
}

void
__glXDisp_Scaled(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 24);
        pc -= 4;
    }
#endif

    glScaled(*(GLdouble *) (pc + 0),
             *(GLdouble *) (pc + 8), *(GLdouble *) (pc + 16));
}

void
__glXDisp_Scalef(GLbyte * pc)
{
    glScalef(*(GLfloat *) (pc + 0),
             *(GLfloat *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_Translated(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 24);
        pc -= 4;
    }
#endif

    glTranslated(*(GLdouble *) (pc + 0),
                 *(GLdouble *) (pc + 8), *(GLdouble *) (pc + 16));
}

void
__glXDisp_Translatef(GLbyte * pc)
{
    glTranslatef(*(GLfloat *) (pc + 0),
                 *(GLfloat *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_Viewport(GLbyte * pc)
{
    glViewport(*(GLint *) (pc + 0),
               *(GLint *) (pc + 4),
               *(GLsizei *) (pc + 8), *(GLsizei *) (pc + 12));
}

void
__glXDisp_BindTexture(GLbyte * pc)
{
    glBindTexture(*(GLenum *) (pc + 0), *(GLuint *) (pc + 4));
}

void
__glXDisp_Indexubv(GLbyte * pc)
{
    glIndexubv((const GLubyte *) (pc + 0));
}

void
__glXDisp_PolygonOffset(GLbyte * pc)
{
    glPolygonOffset(*(GLfloat *) (pc + 0), *(GLfloat *) (pc + 4));
}

int
__glXDisp_AreTexturesResident(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        GLboolean retval;
        GLboolean answerBuffer[200];
        GLboolean *residences =
            __glXGetAnswerBuffer(cl, n, answerBuffer, sizeof(answerBuffer), 1);
        retval =
            glAreTexturesResident(n, (const GLuint *) (pc + 4), residences);
        __glXSendReply(cl->client, residences, n, 1, GL_TRUE, retval);
        error = Success;
    }

    return error;
}

int
__glXDisp_AreTexturesResidentEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        GLboolean retval;
        GLboolean answerBuffer[200];
        GLboolean *residences =
            __glXGetAnswerBuffer(cl, n, answerBuffer, sizeof(answerBuffer), 1);
        retval =
            glAreTexturesResident(n, (const GLuint *) (pc + 4), residences);
        __glXSendReply(cl->client, residences, n, 1, GL_TRUE, retval);
        error = Success;
    }

    return error;
}

void
__glXDisp_CopyTexImage1D(GLbyte * pc)
{
    glCopyTexImage1D(*(GLenum *) (pc + 0),
                     *(GLint *) (pc + 4),
                     *(GLenum *) (pc + 8),
                     *(GLint *) (pc + 12),
                     *(GLint *) (pc + 16),
                     *(GLsizei *) (pc + 20), *(GLint *) (pc + 24));
}

void
__glXDisp_CopyTexImage2D(GLbyte * pc)
{
    glCopyTexImage2D(*(GLenum *) (pc + 0),
                     *(GLint *) (pc + 4),
                     *(GLenum *) (pc + 8),
                     *(GLint *) (pc + 12),
                     *(GLint *) (pc + 16),
                     *(GLsizei *) (pc + 20),
                     *(GLsizei *) (pc + 24), *(GLint *) (pc + 28));
}

void
__glXDisp_CopyTexSubImage1D(GLbyte * pc)
{
    glCopyTexSubImage1D(*(GLenum *) (pc + 0),
                        *(GLint *) (pc + 4),
                        *(GLint *) (pc + 8),
                        *(GLint *) (pc + 12),
                        *(GLint *) (pc + 16), *(GLsizei *) (pc + 20));
}

void
__glXDisp_CopyTexSubImage2D(GLbyte * pc)
{
    glCopyTexSubImage2D(*(GLenum *) (pc + 0),
                        *(GLint *) (pc + 4),
                        *(GLint *) (pc + 8),
                        *(GLint *) (pc + 12),
                        *(GLint *) (pc + 16),
                        *(GLint *) (pc + 20),
                        *(GLsizei *) (pc + 24), *(GLsizei *) (pc + 28));
}

int
__glXDisp_DeleteTextures(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        glDeleteTextures(n, (const GLuint *) (pc + 4));
        error = Success;
    }

    return error;
}

int
__glXDisp_DeleteTexturesEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        glDeleteTextures(n, (const GLuint *) (pc + 4));
        error = Success;
    }

    return error;
}

int
__glXDisp_GenTextures(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        GLuint answerBuffer[200];
        GLuint *textures =
            __glXGetAnswerBuffer(cl, n * 4, answerBuffer, sizeof(answerBuffer),
                                 4);
        glGenTextures(n, textures);
        __glXSendReply(cl->client, textures, n, 4, GL_TRUE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GenTexturesEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        GLuint answerBuffer[200];
        GLuint *textures =
            __glXGetAnswerBuffer(cl, n * 4, answerBuffer, sizeof(answerBuffer),
                                 4);
        glGenTextures(n, textures);
        __glXSendReply(cl->client, textures, n, 4, GL_TRUE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_IsTexture(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        GLboolean retval;

        retval = glIsTexture(*(GLuint *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

int
__glXDisp_IsTextureEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLboolean retval;

        retval = glIsTexture(*(GLuint *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

void
__glXDisp_PrioritizeTextures(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 0);

    glPrioritizeTextures(n,
                         (const GLuint *) (pc + 4),
                         (const GLclampf *) (pc + 4));
}

void
__glXDisp_TexSubImage1D(GLbyte * pc)
{
    const GLvoid *const pixels = (const GLvoid *) ((pc + 56));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glTexSubImage1D(*(GLenum *) (pc + 20),
                    *(GLint *) (pc + 24),
                    *(GLint *) (pc + 28),
                    *(GLsizei *) (pc + 36),
                    *(GLenum *) (pc + 44), *(GLenum *) (pc + 48), pixels);
}

void
__glXDisp_TexSubImage2D(GLbyte * pc)
{
    const GLvoid *const pixels = (const GLvoid *) ((pc + 56));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glTexSubImage2D(*(GLenum *) (pc + 20),
                    *(GLint *) (pc + 24),
                    *(GLint *) (pc + 28),
                    *(GLint *) (pc + 32),
                    *(GLsizei *) (pc + 36),
                    *(GLsizei *) (pc + 40),
                    *(GLenum *) (pc + 44), *(GLenum *) (pc + 48), pixels);
}

void
__glXDisp_BlendColor(GLbyte * pc)
{
    glBlendColor(*(GLclampf *) (pc + 0),
                 *(GLclampf *) (pc + 4),
                 *(GLclampf *) (pc + 8), *(GLclampf *) (pc + 12));
}

void
__glXDisp_BlendEquation(GLbyte * pc)
{
    glBlendEquation(*(GLenum *) (pc + 0));
}

void
__glXDisp_ColorTable(GLbyte * pc)
{
    const GLvoid *const table = (const GLvoid *) ((pc + 40));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glColorTable(*(GLenum *) (pc + 20),
                 *(GLenum *) (pc + 24),
                 *(GLsizei *) (pc + 28),
                 *(GLenum *) (pc + 32), *(GLenum *) (pc + 36), table);
}

void
__glXDisp_ColorTableParameterfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 8);

    glColorTableParameterfv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_ColorTableParameteriv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLint *params;

    params = (const GLint *) (pc + 8);

    glColorTableParameteriv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_CopyColorTable(GLbyte * pc)
{
    glCopyColorTable(*(GLenum *) (pc + 0),
                     *(GLenum *) (pc + 4),
                     *(GLint *) (pc + 8),
                     *(GLint *) (pc + 12), *(GLsizei *) (pc + 16));
}

int
__glXDisp_GetColorTableParameterfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetColorTableParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetColorTableParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetColorTableParameterfvSGI(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetColorTableParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetColorTableParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetColorTableParameteriv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetColorTableParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetColorTableParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetColorTableParameterivSGI(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetColorTableParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetColorTableParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

void
__glXDisp_ColorSubTable(GLbyte * pc)
{
    const GLvoid *const data = (const GLvoid *) ((pc + 40));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glColorSubTable(*(GLenum *) (pc + 20),
                    *(GLsizei *) (pc + 24),
                    *(GLsizei *) (pc + 28),
                    *(GLenum *) (pc + 32), *(GLenum *) (pc + 36), data);
}

void
__glXDisp_CopyColorSubTable(GLbyte * pc)
{
    glCopyColorSubTable(*(GLenum *) (pc + 0),
                        *(GLsizei *) (pc + 4),
                        *(GLint *) (pc + 8),
                        *(GLint *) (pc + 12), *(GLsizei *) (pc + 16));
}

void
__glXDisp_ConvolutionFilter1D(GLbyte * pc)
{
    const GLvoid *const image = (const GLvoid *) ((pc + 44));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glConvolutionFilter1D(*(GLenum *) (pc + 20),
                          *(GLenum *) (pc + 24),
                          *(GLsizei *) (pc + 28),
                          *(GLenum *) (pc + 36), *(GLenum *) (pc + 40), image);
}

void
__glXDisp_ConvolutionFilter2D(GLbyte * pc)
{
    const GLvoid *const image = (const GLvoid *) ((pc + 44));
    __GLXpixelHeader *const hdr = (__GLXpixelHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glConvolutionFilter2D(*(GLenum *) (pc + 20),
                          *(GLenum *) (pc + 24),
                          *(GLsizei *) (pc + 28),
                          *(GLsizei *) (pc + 32),
                          *(GLenum *) (pc + 36), *(GLenum *) (pc + 40), image);
}

void
__glXDisp_ConvolutionParameterf(GLbyte * pc)
{
    glConvolutionParameterf(*(GLenum *) (pc + 0),
                            *(GLenum *) (pc + 4), *(GLfloat *) (pc + 8));
}

void
__glXDisp_ConvolutionParameterfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 8);

    glConvolutionParameterfv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_ConvolutionParameteri(GLbyte * pc)
{
    glConvolutionParameteri(*(GLenum *) (pc + 0),
                            *(GLenum *) (pc + 4), *(GLint *) (pc + 8));
}

void
__glXDisp_ConvolutionParameteriv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 4);
    const GLint *params;

    params = (const GLint *) (pc + 8);

    glConvolutionParameteriv(*(GLenum *) (pc + 0), pname, params);
}

void
__glXDisp_CopyConvolutionFilter1D(GLbyte * pc)
{
    glCopyConvolutionFilter1D(*(GLenum *) (pc + 0),
                              *(GLenum *) (pc + 4),
                              *(GLint *) (pc + 8),
                              *(GLint *) (pc + 12), *(GLsizei *) (pc + 16));
}

void
__glXDisp_CopyConvolutionFilter2D(GLbyte * pc)
{
    glCopyConvolutionFilter2D(*(GLenum *) (pc + 0),
                              *(GLenum *) (pc + 4),
                              *(GLint *) (pc + 8),
                              *(GLint *) (pc + 12),
                              *(GLsizei *) (pc + 16), *(GLsizei *) (pc + 20));
}

int
__glXDisp_GetConvolutionParameterfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetConvolutionParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetConvolutionParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetConvolutionParameterfvEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetConvolutionParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetConvolutionParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetConvolutionParameteriv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetConvolutionParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetConvolutionParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetConvolutionParameterivEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetConvolutionParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetConvolutionParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetHistogramParameterfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetHistogramParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetHistogramParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetHistogramParameterfvEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetHistogramParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetHistogramParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetHistogramParameteriv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetHistogramParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetHistogramParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetHistogramParameterivEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetHistogramParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetHistogramParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMinmaxParameterfv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMinmaxParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMinmaxParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMinmaxParameterfvEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMinmaxParameterfv_size(pname);
        GLfloat answerBuffer[200];
        GLfloat *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMinmaxParameterfv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMinmaxParameteriv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMinmaxParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMinmaxParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetMinmaxParameterivEXT(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetMinmaxParameteriv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetMinmaxParameteriv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

void
__glXDisp_Histogram(GLbyte * pc)
{
    glHistogram(*(GLenum *) (pc + 0),
                *(GLsizei *) (pc + 4),
                *(GLenum *) (pc + 8), *(GLboolean *) (pc + 12));
}

void
__glXDisp_Minmax(GLbyte * pc)
{
    glMinmax(*(GLenum *) (pc + 0),
             *(GLenum *) (pc + 4), *(GLboolean *) (pc + 8));
}

void
__glXDisp_ResetHistogram(GLbyte * pc)
{
    glResetHistogram(*(GLenum *) (pc + 0));
}

void
__glXDisp_ResetMinmax(GLbyte * pc)
{
    glResetMinmax(*(GLenum *) (pc + 0));
}

void
__glXDisp_TexImage3D(GLbyte * pc)
{
    const CARD32 ptr_is_null = *(CARD32 *) (pc + 76);
    const GLvoid *const pixels =
        (const GLvoid *) ((ptr_is_null != 0) ? NULL : (pc + 80));
    __GLXpixel3DHeader *const hdr = (__GLXpixel3DHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, (GLint) hdr->imageHeight);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, (GLint) hdr->skipImages);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glTexImage3D(*(GLenum *) (pc + 36),
                 *(GLint *) (pc + 40),
                 *(GLint *) (pc + 44),
                 *(GLsizei *) (pc + 48),
                 *(GLsizei *) (pc + 52),
                 *(GLsizei *) (pc + 56),
                 *(GLint *) (pc + 64),
                 *(GLenum *) (pc + 68), *(GLenum *) (pc + 72), pixels);
}

void
__glXDisp_TexSubImage3D(GLbyte * pc)
{
    const GLvoid *const pixels = (const GLvoid *) ((pc + 88));
    __GLXpixel3DHeader *const hdr = (__GLXpixel3DHeader *) (pc);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, hdr->swapBytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, hdr->lsbFirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) hdr->rowLength);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, (GLint) hdr->imageHeight);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint) hdr->skipRows);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, (GLint) hdr->skipImages);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint) hdr->skipPixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, (GLint) hdr->alignment);

    glTexSubImage3D(*(GLenum *) (pc + 36),
                    *(GLint *) (pc + 40),
                    *(GLint *) (pc + 44),
                    *(GLint *) (pc + 48),
                    *(GLint *) (pc + 52),
                    *(GLsizei *) (pc + 60),
                    *(GLsizei *) (pc + 64),
                    *(GLsizei *) (pc + 68),
                    *(GLenum *) (pc + 76), *(GLenum *) (pc + 80), pixels);
}

void
__glXDisp_CopyTexSubImage3D(GLbyte * pc)
{
    glCopyTexSubImage3D(*(GLenum *) (pc + 0),
                        *(GLint *) (pc + 4),
                        *(GLint *) (pc + 8),
                        *(GLint *) (pc + 12),
                        *(GLint *) (pc + 16),
                        *(GLint *) (pc + 20),
                        *(GLint *) (pc + 24),
                        *(GLsizei *) (pc + 28), *(GLsizei *) (pc + 32));
}

void
__glXDisp_ActiveTexture(GLbyte * pc)
{
    glActiveTexture(*(GLenum *) (pc + 0));
}

void
__glXDisp_MultiTexCoord1dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 12);
        pc -= 4;
    }
#endif

    glMultiTexCoord1dv(*(GLenum *) (pc + 8), (const GLdouble *) (pc + 0));
}

void
__glXDisp_MultiTexCoord1fvARB(GLbyte * pc)
{
    glMultiTexCoord1fvARB(*(GLenum *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_MultiTexCoord1iv(GLbyte * pc)
{
    glMultiTexCoord1iv(*(GLenum *) (pc + 0), (const GLint *) (pc + 4));
}

void
__glXDisp_MultiTexCoord1sv(GLbyte * pc)
{
    glMultiTexCoord1sv(*(GLenum *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_MultiTexCoord2dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 20);
        pc -= 4;
    }
#endif

    glMultiTexCoord2dv(*(GLenum *) (pc + 16), (const GLdouble *) (pc + 0));
}

void
__glXDisp_MultiTexCoord2fvARB(GLbyte * pc)
{
    glMultiTexCoord2fvARB(*(GLenum *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_MultiTexCoord2iv(GLbyte * pc)
{
    glMultiTexCoord2iv(*(GLenum *) (pc + 0), (const GLint *) (pc + 4));
}

void
__glXDisp_MultiTexCoord2sv(GLbyte * pc)
{
    glMultiTexCoord2sv(*(GLenum *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_MultiTexCoord3dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 28);
        pc -= 4;
    }
#endif

    glMultiTexCoord3dv(*(GLenum *) (pc + 24), (const GLdouble *) (pc + 0));
}

void
__glXDisp_MultiTexCoord3fvARB(GLbyte * pc)
{
    glMultiTexCoord3fvARB(*(GLenum *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_MultiTexCoord3iv(GLbyte * pc)
{
    glMultiTexCoord3iv(*(GLenum *) (pc + 0), (const GLint *) (pc + 4));
}

void
__glXDisp_MultiTexCoord3sv(GLbyte * pc)
{
    glMultiTexCoord3sv(*(GLenum *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_MultiTexCoord4dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 36);
        pc -= 4;
    }
#endif

    glMultiTexCoord4dv(*(GLenum *) (pc + 32), (const GLdouble *) (pc + 0));
}

void
__glXDisp_MultiTexCoord4fvARB(GLbyte * pc)
{
    glMultiTexCoord4fvARB(*(GLenum *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_MultiTexCoord4iv(GLbyte * pc)
{
    glMultiTexCoord4iv(*(GLenum *) (pc + 0), (const GLint *) (pc + 4));
}

void
__glXDisp_MultiTexCoord4sv(GLbyte * pc)
{
    glMultiTexCoord4sv(*(GLenum *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_CompressedTexImage1D(GLbyte * pc)
{
    const GLsizei imageSize = *(GLsizei *) (pc + 20);

    glCompressedTexImage1D(*(GLenum *) (pc + 0),
                           *(GLint *) (pc + 4),
                           *(GLenum *) (pc + 8),
                           *(GLsizei *) (pc + 12),
                           *(GLint *) (pc + 16),
                           imageSize, (const GLvoid *) (pc + 24));
}

void
__glXDisp_CompressedTexImage2D(GLbyte * pc)
{
    const GLsizei imageSize = *(GLsizei *) (pc + 24);

    glCompressedTexImage2D(*(GLenum *) (pc + 0),
                           *(GLint *) (pc + 4),
                           *(GLenum *) (pc + 8),
                           *(GLsizei *) (pc + 12),
                           *(GLsizei *) (pc + 16),
                           *(GLint *) (pc + 20),
                           imageSize, (const GLvoid *) (pc + 28));
}

void
__glXDisp_CompressedTexImage3D(GLbyte * pc)
{
    const GLsizei imageSize = *(GLsizei *) (pc + 28);

    glCompressedTexImage3D(*(GLenum *) (pc + 0),
                           *(GLint *) (pc + 4),
                           *(GLenum *) (pc + 8),
                           *(GLsizei *) (pc + 12),
                           *(GLsizei *) (pc + 16),
                           *(GLsizei *) (pc + 20),
                           *(GLint *) (pc + 24),
                           imageSize, (const GLvoid *) (pc + 32));
}

void
__glXDisp_CompressedTexSubImage1D(GLbyte * pc)
{
    const GLsizei imageSize = *(GLsizei *) (pc + 20);

    glCompressedTexSubImage1D(*(GLenum *) (pc + 0),
                              *(GLint *) (pc + 4),
                              *(GLint *) (pc + 8),
                              *(GLsizei *) (pc + 12),
                              *(GLenum *) (pc + 16),
                              imageSize, (const GLvoid *) (pc + 24));
}

void
__glXDisp_CompressedTexSubImage2D(GLbyte * pc)
{
    const GLsizei imageSize = *(GLsizei *) (pc + 28);

    glCompressedTexSubImage2D(*(GLenum *) (pc + 0),
                              *(GLint *) (pc + 4),
                              *(GLint *) (pc + 8),
                              *(GLint *) (pc + 12),
                              *(GLsizei *) (pc + 16),
                              *(GLsizei *) (pc + 20),
                              *(GLenum *) (pc + 24),
                              imageSize, (const GLvoid *) (pc + 32));
}

void
__glXDisp_CompressedTexSubImage3D(GLbyte * pc)
{
    const GLsizei imageSize = *(GLsizei *) (pc + 36);

    glCompressedTexSubImage3D(*(GLenum *) (pc + 0),
                              *(GLint *) (pc + 4),
                              *(GLint *) (pc + 8),
                              *(GLint *) (pc + 12),
                              *(GLint *) (pc + 16),
                              *(GLsizei *) (pc + 20),
                              *(GLsizei *) (pc + 24),
                              *(GLsizei *) (pc + 28),
                              *(GLenum *) (pc + 32),
                              imageSize, (const GLvoid *) (pc + 40));
}

void
__glXDisp_SampleCoverage(GLbyte * pc)
{
    glSampleCoverage(*(GLclampf *) (pc + 0), *(GLboolean *) (pc + 4));
}

void
__glXDisp_BlendFuncSeparate(GLbyte * pc)
{
    glBlendFuncSeparate(*(GLenum *) (pc + 0),
                        *(GLenum *) (pc + 4),
                        *(GLenum *) (pc + 8), *(GLenum *) (pc + 12));
}

void
__glXDisp_FogCoorddv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 8);
        pc -= 4;
    }
#endif

    glFogCoorddv((const GLdouble *) (pc + 0));
}

void
__glXDisp_PointParameterf(GLbyte * pc)
{
    glPointParameterf(*(GLenum *) (pc + 0), *(GLfloat *) (pc + 4));
}

void
__glXDisp_PointParameterfv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 0);
    const GLfloat *params;

    params = (const GLfloat *) (pc + 4);

    glPointParameterfv(pname, params);
}

void
__glXDisp_PointParameteri(GLbyte * pc)
{
    glPointParameteri(*(GLenum *) (pc + 0), *(GLint *) (pc + 4));
}

void
__glXDisp_PointParameteriv(GLbyte * pc)
{
    const GLenum pname = *(GLenum *) (pc + 0);
    const GLint *params;

    params = (const GLint *) (pc + 4);

    glPointParameteriv(pname, params);
}

void
__glXDisp_SecondaryColor3bv(GLbyte * pc)
{
    glSecondaryColor3bv((const GLbyte *) (pc + 0));
}

void
__glXDisp_SecondaryColor3dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 24);
        pc -= 4;
    }
#endif

    glSecondaryColor3dv((const GLdouble *) (pc + 0));
}

void
__glXDisp_SecondaryColor3iv(GLbyte * pc)
{
    glSecondaryColor3iv((const GLint *) (pc + 0));
}

void
__glXDisp_SecondaryColor3sv(GLbyte * pc)
{
    glSecondaryColor3sv((const GLshort *) (pc + 0));
}

void
__glXDisp_SecondaryColor3ubv(GLbyte * pc)
{
    glSecondaryColor3ubv((const GLubyte *) (pc + 0));
}

void
__glXDisp_SecondaryColor3uiv(GLbyte * pc)
{
    glSecondaryColor3uiv((const GLuint *) (pc + 0));
}

void
__glXDisp_SecondaryColor3usv(GLbyte * pc)
{
    glSecondaryColor3usv((const GLushort *) (pc + 0));
}

void
__glXDisp_WindowPos3fv(GLbyte * pc)
{
    glWindowPos3fv((const GLfloat *) (pc + 0));
}

void
__glXDisp_BeginQuery(GLbyte * pc)
{
    glBeginQuery(*(GLenum *) (pc + 0), *(GLuint *) (pc + 4));
}

int
__glXDisp_DeleteQueries(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        glDeleteQueries(n, (const GLuint *) (pc + 4));
        error = Success;
    }

    return error;
}

void
__glXDisp_EndQuery(GLbyte * pc)
{
    glEndQuery(*(GLenum *) (pc + 0));
}

int
__glXDisp_GenQueries(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        GLuint answerBuffer[200];
        GLuint *ids =
            __glXGetAnswerBuffer(cl, n * 4, answerBuffer, sizeof(answerBuffer),
                                 4);
        glGenQueries(n, ids);
        __glXSendReply(cl->client, ids, n, 4, GL_TRUE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetQueryObjectiv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetQueryObjectiv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetQueryObjectiv(*(GLuint *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetQueryObjectuiv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetQueryObjectuiv_size(pname);
        GLuint answerBuffer[200];
        GLuint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetQueryObjectuiv(*(GLuint *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetQueryiv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetQueryiv_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetQueryiv(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_IsQuery(__GLXclientState * cl, GLbyte * pc)
{
    xGLXSingleReq *const req = (xGLXSingleReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_SINGLE_HDR_SIZE;
    if (cx != NULL) {
        GLboolean retval;

        retval = glIsQuery(*(GLuint *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

void
__glXDisp_BlendEquationSeparate(GLbyte * pc)
{
    glBlendEquationSeparate(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4));
}

void
__glXDisp_DrawBuffers(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 0);

    glDrawBuffers(n, (const GLenum *) (pc + 4));
}

void
__glXDisp_VertexAttrib1dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 12);
        pc -= 4;
    }
#endif

    glVertexAttrib1dv(*(GLuint *) (pc + 0), (const GLdouble *) (pc + 4));
}

void
__glXDisp_VertexAttrib1sv(GLbyte * pc)
{
    glVertexAttrib1sv(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib2dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 20);
        pc -= 4;
    }
#endif

    glVertexAttrib2dv(*(GLuint *) (pc + 0), (const GLdouble *) (pc + 4));
}

void
__glXDisp_VertexAttrib2sv(GLbyte * pc)
{
    glVertexAttrib2sv(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib3dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 28);
        pc -= 4;
    }
#endif

    glVertexAttrib3dv(*(GLuint *) (pc + 0), (const GLdouble *) (pc + 4));
}

void
__glXDisp_VertexAttrib3sv(GLbyte * pc)
{
    glVertexAttrib3sv(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib4Nbv(GLbyte * pc)
{
    glVertexAttrib4Nbv(*(GLuint *) (pc + 0), (const GLbyte *) (pc + 4));
}

void
__glXDisp_VertexAttrib4Niv(GLbyte * pc)
{
    glVertexAttrib4Niv(*(GLuint *) (pc + 0), (const GLint *) (pc + 4));
}

void
__glXDisp_VertexAttrib4Nsv(GLbyte * pc)
{
    glVertexAttrib4Nsv(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib4Nubv(GLbyte * pc)
{
    glVertexAttrib4Nubv(*(GLuint *) (pc + 0), (const GLubyte *) (pc + 4));
}

void
__glXDisp_VertexAttrib4Nuiv(GLbyte * pc)
{
    glVertexAttrib4Nuiv(*(GLuint *) (pc + 0), (const GLuint *) (pc + 4));
}

void
__glXDisp_VertexAttrib4Nusv(GLbyte * pc)
{
    glVertexAttrib4Nusv(*(GLuint *) (pc + 0), (const GLushort *) (pc + 4));
}

void
__glXDisp_VertexAttrib4bv(GLbyte * pc)
{
    glVertexAttrib4bv(*(GLuint *) (pc + 0), (const GLbyte *) (pc + 4));
}

void
__glXDisp_VertexAttrib4dv(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 36);
        pc -= 4;
    }
#endif

    glVertexAttrib4dv(*(GLuint *) (pc + 0), (const GLdouble *) (pc + 4));
}

void
__glXDisp_VertexAttrib4iv(GLbyte * pc)
{
    glVertexAttrib4iv(*(GLuint *) (pc + 0), (const GLint *) (pc + 4));
}

void
__glXDisp_VertexAttrib4sv(GLbyte * pc)
{
    glVertexAttrib4sv(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib4ubv(GLbyte * pc)
{
    glVertexAttrib4ubv(*(GLuint *) (pc + 0), (const GLubyte *) (pc + 4));
}

void
__glXDisp_VertexAttrib4uiv(GLbyte * pc)
{
    glVertexAttrib4uiv(*(GLuint *) (pc + 0), (const GLuint *) (pc + 4));
}

void
__glXDisp_VertexAttrib4usv(GLbyte * pc)
{
    glVertexAttrib4usv(*(GLuint *) (pc + 0), (const GLushort *) (pc + 4));
}

void
__glXDisp_ClampColor(GLbyte * pc)
{
    glClampColor(*(GLenum *) (pc + 0), *(GLenum *) (pc + 4));
}

void
__glXDisp_BindProgramARB(GLbyte * pc)
{
    glBindProgramARB(*(GLenum *) (pc + 0), *(GLuint *) (pc + 4));
}

int
__glXDisp_DeleteProgramsARB(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        glDeleteProgramsARB(n, (const GLuint *) (pc + 4));
        error = Success;
    }

    return error;
}

int
__glXDisp_GenProgramsARB(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        GLuint answerBuffer[200];
        GLuint *programs =
            __glXGetAnswerBuffer(cl, n * 4, answerBuffer, sizeof(answerBuffer),
                                 4);
        glGenProgramsARB(n, programs);
        __glXSendReply(cl->client, programs, n, 4, GL_TRUE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetProgramEnvParameterdvARB(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLdouble params[4];

        glGetProgramEnvParameterdvARB(*(GLenum *) (pc + 0),
                                      *(GLuint *) (pc + 4), params);
        __glXSendReply(cl->client, params, 4, 8, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetProgramEnvParameterfvARB(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLfloat params[4];

        glGetProgramEnvParameterfvARB(*(GLenum *) (pc + 0),
                                      *(GLuint *) (pc + 4), params);
        __glXSendReply(cl->client, params, 4, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetProgramLocalParameterdvARB(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLdouble params[4];

        glGetProgramLocalParameterdvARB(*(GLenum *) (pc + 0),
                                        *(GLuint *) (pc + 4), params);
        __glXSendReply(cl->client, params, 4, 8, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetProgramLocalParameterfvARB(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLfloat params[4];

        glGetProgramLocalParameterfvARB(*(GLenum *) (pc + 0),
                                        *(GLuint *) (pc + 4), params);
        __glXSendReply(cl->client, params, 4, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetProgramivARB(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLenum pname = *(GLenum *) (pc + 4);

        const GLuint compsize = __glGetProgramivARB_size(pname);
        GLint answerBuffer[200];
        GLint *params =
            __glXGetAnswerBuffer(cl, compsize * 4, answerBuffer,
                                 sizeof(answerBuffer), 4);

        if (params == NULL)
            return BadAlloc;
        __glXClearErrorOccured();

        glGetProgramivARB(*(GLenum *) (pc + 0), pname, params);
        __glXSendReply(cl->client, params, compsize, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_IsProgramARB(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLboolean retval;

        retval = glIsProgramARB(*(GLuint *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

void
__glXDisp_ProgramEnvParameter4dvARB(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 40);
        pc -= 4;
    }
#endif

    glProgramEnvParameter4dvARB(*(GLenum *) (pc + 0),
                                *(GLuint *) (pc + 4),
                                (const GLdouble *) (pc + 8));
}

void
__glXDisp_ProgramEnvParameter4fvARB(GLbyte * pc)
{
    glProgramEnvParameter4fvARB(*(GLenum *) (pc + 0),
                                *(GLuint *) (pc + 4),
                                (const GLfloat *) (pc + 8));
}

void
__glXDisp_ProgramLocalParameter4dvARB(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 40);
        pc -= 4;
    }
#endif

    glProgramLocalParameter4dvARB(*(GLenum *) (pc + 0),
                                  *(GLuint *) (pc + 4),
                                  (const GLdouble *) (pc + 8));
}

void
__glXDisp_ProgramLocalParameter4fvARB(GLbyte * pc)
{
    glProgramLocalParameter4fvARB(*(GLenum *) (pc + 0),
                                  *(GLuint *) (pc + 4),
                                  (const GLfloat *) (pc + 8));
}

void
__glXDisp_ProgramStringARB(GLbyte * pc)
{
    const GLsizei len = *(GLsizei *) (pc + 8);

    glProgramStringARB(*(GLenum *) (pc + 0),
                       *(GLenum *) (pc + 4), len, (const GLvoid *) (pc + 12));
}

void
__glXDisp_VertexAttrib1fvARB(GLbyte * pc)
{
    glVertexAttrib1fvARB(*(GLuint *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_VertexAttrib2fvARB(GLbyte * pc)
{
    glVertexAttrib2fvARB(*(GLuint *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_VertexAttrib3fvARB(GLbyte * pc)
{
    glVertexAttrib3fvARB(*(GLuint *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_VertexAttrib4fvARB(GLbyte * pc)
{
    glVertexAttrib4fvARB(*(GLuint *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_BindFramebuffer(GLbyte * pc)
{
    glBindFramebuffer(*(GLenum *) (pc + 0), *(GLuint *) (pc + 4));
}

void
__glXDisp_BindRenderbuffer(GLbyte * pc)
{
    glBindRenderbuffer(*(GLenum *) (pc + 0), *(GLuint *) (pc + 4));
}

void
__glXDisp_BlitFramebuffer(GLbyte * pc)
{
    glBlitFramebuffer(*(GLint *) (pc + 0),
                      *(GLint *) (pc + 4),
                      *(GLint *) (pc + 8),
                      *(GLint *) (pc + 12),
                      *(GLint *) (pc + 16),
                      *(GLint *) (pc + 20),
                      *(GLint *) (pc + 24),
                      *(GLint *) (pc + 28),
                      *(GLbitfield *) (pc + 32), *(GLenum *) (pc + 36));
}

int
__glXDisp_CheckFramebufferStatus(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLenum retval;

        retval = glCheckFramebufferStatus(*(GLenum *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

void
__glXDisp_DeleteFramebuffers(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 0);

    glDeleteFramebuffers(n, (const GLuint *) (pc + 4));
}

void
__glXDisp_DeleteRenderbuffers(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 0);

    glDeleteRenderbuffers(n, (const GLuint *) (pc + 4));
}

void
__glXDisp_FramebufferRenderbuffer(GLbyte * pc)
{
    glFramebufferRenderbuffer(*(GLenum *) (pc + 0),
                              *(GLenum *) (pc + 4),
                              *(GLenum *) (pc + 8), *(GLuint *) (pc + 12));
}

void
__glXDisp_FramebufferTexture1D(GLbyte * pc)
{
    glFramebufferTexture1D(*(GLenum *) (pc + 0),
                           *(GLenum *) (pc + 4),
                           *(GLenum *) (pc + 8),
                           *(GLuint *) (pc + 12), *(GLint *) (pc + 16));
}

void
__glXDisp_FramebufferTexture2D(GLbyte * pc)
{
    glFramebufferTexture2D(*(GLenum *) (pc + 0),
                           *(GLenum *) (pc + 4),
                           *(GLenum *) (pc + 8),
                           *(GLuint *) (pc + 12), *(GLint *) (pc + 16));
}

void
__glXDisp_FramebufferTexture3D(GLbyte * pc)
{
    glFramebufferTexture3D(*(GLenum *) (pc + 0),
                           *(GLenum *) (pc + 4),
                           *(GLenum *) (pc + 8),
                           *(GLuint *) (pc + 12),
                           *(GLint *) (pc + 16), *(GLint *) (pc + 20));
}

void
__glXDisp_FramebufferTextureLayer(GLbyte * pc)
{
    glFramebufferTextureLayer(*(GLenum *) (pc + 0),
                              *(GLenum *) (pc + 4),
                              *(GLuint *) (pc + 8),
                              *(GLint *) (pc + 12), *(GLint *) (pc + 16));
}

int
__glXDisp_GenFramebuffers(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        GLuint answerBuffer[200];
        GLuint *framebuffers =
            __glXGetAnswerBuffer(cl, n * 4, answerBuffer, sizeof(answerBuffer),
                                 4);
        glGenFramebuffers(n, framebuffers);
        __glXSendReply(cl->client, framebuffers, n, 4, GL_TRUE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GenRenderbuffers(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        const GLsizei n = *(GLsizei *) (pc + 0);

        GLuint answerBuffer[200];
        GLuint *renderbuffers =
            __glXGetAnswerBuffer(cl, n * 4, answerBuffer, sizeof(answerBuffer),
                                 4);
        glGenRenderbuffers(n, renderbuffers);
        __glXSendReply(cl->client, renderbuffers, n, 4, GL_TRUE, 0);
        error = Success;
    }

    return error;
}

void
__glXDisp_GenerateMipmap(GLbyte * pc)
{
    glGenerateMipmap(*(GLenum *) (pc + 0));
}

int
__glXDisp_GetFramebufferAttachmentParameteriv(__GLXclientState * cl,
                                              GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLint params[1];

        glGetFramebufferAttachmentParameteriv(*(GLenum *) (pc + 0),
                                              *(GLenum *) (pc + 4),
                                              *(GLenum *) (pc + 8), params);
        __glXSendReply(cl->client, params, 1, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_GetRenderbufferParameteriv(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLint params[1];

        glGetRenderbufferParameteriv(*(GLenum *) (pc + 0),
                                     *(GLenum *) (pc + 4), params);
        __glXSendReply(cl->client, params, 1, 4, GL_FALSE, 0);
        error = Success;
    }

    return error;
}

int
__glXDisp_IsFramebuffer(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLboolean retval;

        retval = glIsFramebuffer(*(GLuint *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

int
__glXDisp_IsRenderbuffer(__GLXclientState * cl, GLbyte * pc)
{
    xGLXVendorPrivateReq *const req = (xGLXVendorPrivateReq *) pc;
    int error;
    __GLXcontext *const cx = __glXForceCurrent(cl, req->contextTag, &error);

    pc += __GLX_VENDPRIV_HDR_SIZE;
    if (cx != NULL) {
        GLboolean retval;

        retval = glIsRenderbuffer(*(GLuint *) (pc + 0));
        __glXSendReply(cl->client, dummy_answer, 0, 0, GL_FALSE, retval);
        error = Success;
    }

    return error;
}

void
__glXDisp_RenderbufferStorage(GLbyte * pc)
{
    glRenderbufferStorage(*(GLenum *) (pc + 0),
                          *(GLenum *) (pc + 4),
                          *(GLsizei *) (pc + 8), *(GLsizei *) (pc + 12));
}

void
__glXDisp_RenderbufferStorageMultisample(GLbyte * pc)
{
    glRenderbufferStorageMultisample(*(GLenum *) (pc + 0),
                                     *(GLsizei *) (pc + 4),
                                     *(GLenum *) (pc + 8),
                                     *(GLsizei *) (pc + 12),
                                     *(GLsizei *) (pc + 16));
}

void
__glXDisp_SecondaryColor3fvEXT(GLbyte * pc)
{
    glSecondaryColor3fvEXT((const GLfloat *) (pc + 0));
}

void
__glXDisp_FogCoordfvEXT(GLbyte * pc)
{
    glFogCoordfvEXT((const GLfloat *) (pc + 0));
}

void
__glXDisp_VertexAttrib1dvNV(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 12);
        pc -= 4;
    }
#endif

    glVertexAttrib1dvNV(*(GLuint *) (pc + 0), (const GLdouble *) (pc + 4));
}

void
__glXDisp_VertexAttrib1fvNV(GLbyte * pc)
{
    glVertexAttrib1fvNV(*(GLuint *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_VertexAttrib1svNV(GLbyte * pc)
{
    glVertexAttrib1svNV(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib2dvNV(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 20);
        pc -= 4;
    }
#endif

    glVertexAttrib2dvNV(*(GLuint *) (pc + 0), (const GLdouble *) (pc + 4));
}

void
__glXDisp_VertexAttrib2fvNV(GLbyte * pc)
{
    glVertexAttrib2fvNV(*(GLuint *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_VertexAttrib2svNV(GLbyte * pc)
{
    glVertexAttrib2svNV(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib3dvNV(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 28);
        pc -= 4;
    }
#endif

    glVertexAttrib3dvNV(*(GLuint *) (pc + 0), (const GLdouble *) (pc + 4));
}

void
__glXDisp_VertexAttrib3fvNV(GLbyte * pc)
{
    glVertexAttrib3fvNV(*(GLuint *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_VertexAttrib3svNV(GLbyte * pc)
{
    glVertexAttrib3svNV(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib4dvNV(GLbyte * pc)
{
#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, 36);
        pc -= 4;
    }
#endif

    glVertexAttrib4dvNV(*(GLuint *) (pc + 0), (const GLdouble *) (pc + 4));
}

void
__glXDisp_VertexAttrib4fvNV(GLbyte * pc)
{
    glVertexAttrib4fvNV(*(GLuint *) (pc + 0), (const GLfloat *) (pc + 4));
}

void
__glXDisp_VertexAttrib4svNV(GLbyte * pc)
{
    glVertexAttrib4svNV(*(GLuint *) (pc + 0), (const GLshort *) (pc + 4));
}

void
__glXDisp_VertexAttrib4ubvNV(GLbyte * pc)
{
    glVertexAttrib4ubvNV(*(GLuint *) (pc + 0), (const GLubyte *) (pc + 4));
}

void
__glXDisp_VertexAttribs1dvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

#ifdef __GLX_ALIGN64
    const GLuint cmdlen = 12 + __GLX_PAD((n * 8)) - 4;

    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, cmdlen);
        pc -= 4;
    }
#endif

    glVertexAttribs1dvNV(*(GLuint *) (pc + 0), n, (const GLdouble *) (pc + 8));
}

void
__glXDisp_VertexAttribs1fvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs1fvNV(*(GLuint *) (pc + 0), n, (const GLfloat *) (pc + 8));
}

void
__glXDisp_VertexAttribs1svNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs1svNV(*(GLuint *) (pc + 0), n, (const GLshort *) (pc + 8));
}

void
__glXDisp_VertexAttribs2dvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

#ifdef __GLX_ALIGN64
    const GLuint cmdlen = 12 + __GLX_PAD((n * 16)) - 4;

    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, cmdlen);
        pc -= 4;
    }
#endif

    glVertexAttribs2dvNV(*(GLuint *) (pc + 0), n, (const GLdouble *) (pc + 8));
}

void
__glXDisp_VertexAttribs2fvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs2fvNV(*(GLuint *) (pc + 0), n, (const GLfloat *) (pc + 8));
}

void
__glXDisp_VertexAttribs2svNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs2svNV(*(GLuint *) (pc + 0), n, (const GLshort *) (pc + 8));
}

void
__glXDisp_VertexAttribs3dvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

#ifdef __GLX_ALIGN64
    const GLuint cmdlen = 12 + __GLX_PAD((n * 24)) - 4;

    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, cmdlen);
        pc -= 4;
    }
#endif

    glVertexAttribs3dvNV(*(GLuint *) (pc + 0), n, (const GLdouble *) (pc + 8));
}

void
__glXDisp_VertexAttribs3fvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs3fvNV(*(GLuint *) (pc + 0), n, (const GLfloat *) (pc + 8));
}

void
__glXDisp_VertexAttribs3svNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs3svNV(*(GLuint *) (pc + 0), n, (const GLshort *) (pc + 8));
}

void
__glXDisp_VertexAttribs4dvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

#ifdef __GLX_ALIGN64
    const GLuint cmdlen = 12 + __GLX_PAD((n * 32)) - 4;

    if ((unsigned long) (pc) & 7) {
        (void) memmove(pc - 4, pc, cmdlen);
        pc -= 4;
    }
#endif

    glVertexAttribs4dvNV(*(GLuint *) (pc + 0), n, (const GLdouble *) (pc + 8));
}

void
__glXDisp_VertexAttribs4fvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs4fvNV(*(GLuint *) (pc + 0), n, (const GLfloat *) (pc + 8));
}

void
__glXDisp_VertexAttribs4svNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs4svNV(*(GLuint *) (pc + 0), n, (const GLshort *) (pc + 8));
}

void
__glXDisp_VertexAttribs4ubvNV(GLbyte * pc)
{
    const GLsizei n = *(GLsizei *) (pc + 4);

    glVertexAttribs4ubvNV(*(GLuint *) (pc + 0), n, (const GLubyte *) (pc + 8));
}

void
__glXDisp_ActiveStencilFaceEXT(GLbyte * pc)
{
    glActiveStencilFaceEXT(*(GLenum *) (pc + 0));
}
