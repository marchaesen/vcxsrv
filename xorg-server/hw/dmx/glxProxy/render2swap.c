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

#include "glxserver.h"
#include "unpack.h"
#include "g_disptab.h"

static GLint
__glEvalComputeK(GLenum target)
{
    switch (target) {
    case GL_MAP1_VERTEX_4:
    case GL_MAP1_COLOR_4:
    case GL_MAP1_TEXTURE_COORD_4:
    case GL_MAP2_VERTEX_4:
    case GL_MAP2_COLOR_4:
    case GL_MAP2_TEXTURE_COORD_4:
        return 4;
    case GL_MAP1_VERTEX_3:
    case GL_MAP1_TEXTURE_COORD_3:
    case GL_MAP1_NORMAL:
    case GL_MAP2_VERTEX_3:
    case GL_MAP2_TEXTURE_COORD_3:
    case GL_MAP2_NORMAL:
        return 3;
    case GL_MAP1_TEXTURE_COORD_2:
    case GL_MAP2_TEXTURE_COORD_2:
        return 2;
    case GL_MAP1_TEXTURE_COORD_1:
    case GL_MAP2_TEXTURE_COORD_1:
    case GL_MAP1_INDEX:
    case GL_MAP2_INDEX:
        return 1;
    default:
        return 0;
    }
}

void
__glXDispSwap_Map1f(GLbyte * pc)
{
    GLint order, k;
    GLfloat *points;
    GLenum target;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

    target = *(GLenum *) (pc + 0);
    order = *(GLint *) (pc + 12);
    points = (GLfloat *) (pc + 16);
    k = __glEvalComputeK(target);

    if (order <= 0 || k < 0) {
        /* Erroneous command. */
        compsize = 0;
    }
    else {
        compsize = order * k;
    }
    __GLX_SWAP_FLOAT_ARRAY(points, compsize);

}

void
__glXDispSwap_Map2f(GLbyte * pc)
{
    GLint uorder, vorder, k;
    GLfloat *points;
    GLenum target;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 24);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);
    __GLX_SWAP_FLOAT(pc + 16);
    __GLX_SWAP_FLOAT(pc + 20);

    target = *(GLenum *) (pc + 0);
    uorder = *(GLint *) (pc + 12);
    vorder = *(GLint *) (pc + 24);
    points = (GLfloat *) (pc + 28);

    k = __glEvalComputeK(target);

    if (vorder <= 0 || uorder <= 0 || k < 0) {
        /* Erroneous command. */
        compsize = 0;
    }
    else {
        compsize = uorder * vorder * k;
    }
    __GLX_SWAP_FLOAT_ARRAY(points, compsize);

}

void
__glXDispSwap_Map1d(GLbyte * pc)
{
    GLint order, k, compsize;
    GLenum target;
    GLdouble u1, u2;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_INT(pc + 20);

    target = *(GLenum *) (pc + 16);
    order = *(GLint *) (pc + 20);
    k = __glEvalComputeK(target);
    if (order <= 0 || k < 0) {
        /* Erroneous command. */
        compsize = 0;
    }
    else {
        compsize = order * k;
    }
    __GLX_GET_DOUBLE(u1, pc);
    __GLX_GET_DOUBLE(u2, pc + 8);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 24, compsize);
    pc += 24;

#ifdef __GLX_ALIGN64
    if (((unsigned long) pc) & 7) {
        /*
         ** Copy the doubles up 4 bytes, trashing the command but aligning
         ** the data in the process
         */
        __GLX_MEM_COPY(pc - 4, pc, compsize * 8);
    }
#endif
}

void
__glXDispSwap_Map2d(GLbyte * pc)
{
    GLdouble u1, u2, v1, v2;
    GLint uorder, vorder, k, compsize;
    GLenum target;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);
    __GLX_SWAP_DOUBLE(pc + 16);
    __GLX_SWAP_DOUBLE(pc + 24);
    __GLX_SWAP_INT(pc + 32);
    __GLX_SWAP_INT(pc + 36);
    __GLX_SWAP_INT(pc + 40);

    target = *(GLenum *) (pc + 32);
    uorder = *(GLint *) (pc + 36);
    vorder = *(GLint *) (pc + 40);
    k = __glEvalComputeK(target);
    if (vorder <= 0 || uorder <= 0 || k < 0) {
        /* Erroneous command. */
        compsize = 0;
    }
    else {
        compsize = uorder * vorder * k;
    }
    __GLX_GET_DOUBLE(u1, pc);
    __GLX_GET_DOUBLE(u2, pc + 8);
    __GLX_GET_DOUBLE(v1, pc + 16);
    __GLX_GET_DOUBLE(v2, pc + 24);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 44, compsize);
    pc += 44;

#ifdef __GLX_ALIGN64
    if (((unsigned long) pc) & 7) {
        /*
         ** Copy the doubles up 4 bytes, trashing the command but aligning
         ** the data in the process
         */
        __GLX_MEM_COPY(pc - 4, pc, compsize * 8);
    }
#endif
}

void
__glXDispSwap_CallLists(GLbyte * pc)
{
    GLenum type;
    GLsizei n;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 0);
    type = *(GLenum *) (pc + 4);
    n = *(GLsizei *) (pc + 0);

    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
    case GL_2_BYTES:
    case GL_3_BYTES:
    case GL_4_BYTES:
        break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        __GLX_SWAP_SHORT_ARRAY(pc + 8, n);
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
        __GLX_SWAP_INT_ARRAY(pc + 8, n);
        break;
    case GL_FLOAT:
        __GLX_SWAP_FLOAT_ARRAY(pc + 8, n);
        break;
    }

}

void
__glXDispSwap_DrawArrays(GLbyte * pc)
{
    __GLXdispatchDrawArraysHeader *hdr = (__GLXdispatchDrawArraysHeader *) pc;
    __GLXdispatchDrawArraysComponentHeader *compHeader;
    int i;

    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(&hdr->numVertexes);
    __GLX_SWAP_INT(&hdr->numComponents);
    __GLX_SWAP_INT(&hdr->primType);

    pc += sizeof(__GLXdispatchDrawArraysHeader);
    compHeader = (__GLXdispatchDrawArraysComponentHeader *) pc;

    /* compute stride (same for all component arrays) */
    for (i = 0; i < hdr->numComponents; i++) {
        __GLX_SWAP_INT(&compHeader[i].datatype);
        __GLX_SWAP_INT(&compHeader[i].numVals);
        __GLX_SWAP_INT(&compHeader[i].component);

    }

}
