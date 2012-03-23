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
#include "glxext.h"
#include "g_disptab.h"
#include "unpack.h"
#include "compsize.h"

void
__glXDispSwap_CallList(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_ListBase(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_Begin(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_Color3bv(GLbyte * pc)
{
}

void
__glXDispSwap_Color3dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 24);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Color3fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 3);
}

void
__glXDispSwap_Color3iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Color3sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Color3ubv(GLbyte * pc)
{
}

void
__glXDispSwap_Color3uiv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 3);
}

void
__glXDispSwap_Color3usv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 3);
}

void
__glXDispSwap_Color4bv(GLbyte * pc)
{
}

void
__glXDispSwap_Color4dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 32);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 4);
}

void
__glXDispSwap_Color4fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Color4iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Color4sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Color4ubv(GLbyte * pc)
{

}

void
__glXDispSwap_Color4uiv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Color4usv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_EdgeFlagv(GLbyte * pc)
{
}

void
__glXDispSwap_End(GLbyte * pc)
{
}

void
__glXDispSwap_Indexdv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 8);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_Indexfv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_Indexiv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_Indexsv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_Normal3bv(GLbyte * pc)
{
}

void
__glXDispSwap_Normal3dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 24);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Normal3fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Normal3iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Normal3sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_RasterPos2dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 16);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_RasterPos2fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_RasterPos2iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_RasterPos2sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_RasterPos3dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 24);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_RasterPos3fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_RasterPos3iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_RasterPos3sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_RasterPos4dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 32);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_RasterPos4fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_RasterPos4iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_RasterPos4sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Rectdv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 32);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 2);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 16, 2);

}

void
__glXDispSwap_Rectfv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 2);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, 2);

}

void
__glXDispSwap_Rectiv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 2);
    __GLX_SWAP_INT_ARRAY(pc + 8, 2);

}

void
__glXDispSwap_Rectsv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 2);
    __GLX_SWAP_SHORT_ARRAY(pc + 4, 2);

}

void
__glXDispSwap_TexCoord1dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 8);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_TexCoord1fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_TexCoord1iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_TexCoord1sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_TexCoord2dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 16);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_TexCoord2fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_TexCoord2iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_TexCoord2sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_TexCoord3dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 24);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_TexCoord3fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_TexCoord3iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_TexCoord3sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_TexCoord4dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 32);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_TexCoord4fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_TexCoord4iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_TexCoord4sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Vertex2dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 16);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_Vertex2fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_Vertex2iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_Vertex2sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_Vertex3dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 24);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Vertex3fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Vertex3iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Vertex3sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_Vertex4dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 32);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Vertex4fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Vertex4iv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_Vertex4sv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_SHORT_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_ClipPlane(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 36);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 32);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_ColorMaterial(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_CullFace(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_Fogf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);

}

void
__glXDispSwap_Fogfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    pname = *(GLenum *) (pc + 0);
    compsize = __glFogfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_FLOAT_ARRAY(pc + 4, compsize);

}

void
__glXDispSwap_Fogi(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_Fogiv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    pname = *(GLenum *) (pc + 0);
    compsize = __glFogiv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT_ARRAY(pc + 4, compsize);

}

void
__glXDispSwap_FrontFace(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_Hint(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_Lightf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_Lightfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glLightfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_Lighti(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_Lightiv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glLightiv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_LightModelf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);

}

void
__glXDispSwap_LightModelfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    pname = *(GLenum *) (pc + 0);
    compsize = __glLightModelfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_FLOAT_ARRAY(pc + 4, compsize);

}

void
__glXDispSwap_LightModeli(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_LightModeliv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    pname = *(GLenum *) (pc + 0);
    compsize = __glLightModeliv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT_ARRAY(pc + 4, compsize);

}

void
__glXDispSwap_LineStipple(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_SHORT(pc + 4);

}

void
__glXDispSwap_LineWidth(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);

}

void
__glXDispSwap_Materialf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_Materialfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glMaterialfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_Materiali(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_Materialiv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glMaterialiv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_PointSize(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);

}

void
__glXDispSwap_PolygonMode(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_Scissor(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);

}

void
__glXDispSwap_ShadeModel(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_TexParameterf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_TexParameterfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glTexParameterfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_TexParameteri(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_TexParameteriv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glTexParameteriv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_TexEnvf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_TexEnvfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glTexEnvfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_TexEnvi(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_TexEnviv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glTexEnviv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_TexGend(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 16);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_DOUBLE(pc + 0);

}

void
__glXDispSwap_TexGendv(GLbyte * pc)
{
    GLenum pname;
    GLint cmdlen;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glTexGendv_size(pname);
    if (compsize < 0)
        compsize = 0;
    cmdlen = __GLX_PAD(8 + compsize * 8);

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, cmdlen);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_TexGenf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_TexGenfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glTexGenfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_TexGeni(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_TexGeniv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glTexGeniv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_InitNames(GLbyte * pc)
{
}

void
__glXDispSwap_LoadName(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_PassThrough(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);

}

void
__glXDispSwap_PopName(GLbyte * pc)
{
}

void
__glXDispSwap_PushName(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_DrawBuffer(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_Clear(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_ClearAccum(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);
    __GLX_SWAP_FLOAT(pc + 12);

}

void
__glXDispSwap_ClearIndex(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);

}

void
__glXDispSwap_ClearColor(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);
    __GLX_SWAP_FLOAT(pc + 12);

}

void
__glXDispSwap_ClearStencil(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_ClearDepth(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 8);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE(pc + 0);

}

void
__glXDispSwap_StencilMask(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_ColorMask(GLbyte * pc)
{
}

void
__glXDispSwap_DepthMask(GLbyte * pc)
{
}

void
__glXDispSwap_IndexMask(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_Accum(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);

}

void
__glXDispSwap_Disable(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_Enable(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_PopAttrib(GLbyte * pc)
{
}

void
__glXDispSwap_PushAttrib(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_MapGrid1d(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 20);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);

}

void
__glXDispSwap_MapGrid1f(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_MapGrid2d(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 40);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 32);
    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);
    __GLX_SWAP_INT(pc + 36);
    __GLX_SWAP_DOUBLE(pc + 16);
    __GLX_SWAP_DOUBLE(pc + 24);

}

void
__glXDispSwap_MapGrid2f(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_FLOAT(pc + 16);
    __GLX_SWAP_FLOAT(pc + 20);

}

void
__glXDispSwap_EvalCoord1dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 8);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_EvalCoord1fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_EvalCoord2dv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 16);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_EvalCoord2fv(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_EvalMesh1(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_EvalPoint1(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_EvalMesh2(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);

}

void
__glXDispSwap_EvalPoint2(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_AlphaFunc(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);

}

void
__glXDispSwap_BlendFunc(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_LogicOp(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_StencilFunc(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_StencilOp(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_DepthFunc(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_PixelZoom(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);

}

void
__glXDispSwap_PixelTransferf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);

}

void
__glXDispSwap_PixelTransferi(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_PixelMapfv(GLbyte * pc)
{
    GLint mapsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    mapsize = *(GLint *) (pc + 4);
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, mapsize);

}

void
__glXDispSwap_PixelMapuiv(GLbyte * pc)
{
    GLint mapsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    mapsize = *(GLint *) (pc + 4);
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 8, mapsize);

}

void
__glXDispSwap_PixelMapusv(GLbyte * pc)
{
    GLint mapsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    mapsize = *(GLint *) (pc + 4);
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_SHORT_ARRAY(pc + 8, mapsize);

}

void
__glXDispSwap_ReadBuffer(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_CopyPixels(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);

}

void
__glXDispSwap_DepthRange(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 16);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);

}

void
__glXDispSwap_Frustum(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 48);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);
    __GLX_SWAP_DOUBLE(pc + 16);
    __GLX_SWAP_DOUBLE(pc + 24);
    __GLX_SWAP_DOUBLE(pc + 32);
    __GLX_SWAP_DOUBLE(pc + 40);

}

void
__glXDispSwap_LoadIdentity(GLbyte * pc)
{
}

void
__glXDispSwap_LoadMatrixf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 16);

}

void
__glXDispSwap_LoadMatrixd(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 128);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 16);

}

void
__glXDispSwap_MatrixMode(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_MultMatrixf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_FLOAT_ARRAY(pc + 0, 16);

}

void
__glXDispSwap_MultMatrixd(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 128);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 16);

}

void
__glXDispSwap_Ortho(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 48);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);
    __GLX_SWAP_DOUBLE(pc + 16);
    __GLX_SWAP_DOUBLE(pc + 24);
    __GLX_SWAP_DOUBLE(pc + 32);
    __GLX_SWAP_DOUBLE(pc + 40);

}

void
__glXDispSwap_PopMatrix(GLbyte * pc)
{
}

void
__glXDispSwap_PushMatrix(GLbyte * pc)
{
}

void
__glXDispSwap_Rotated(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 32);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);
    __GLX_SWAP_DOUBLE(pc + 16);
    __GLX_SWAP_DOUBLE(pc + 24);

}

void
__glXDispSwap_Rotatef(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);
    __GLX_SWAP_FLOAT(pc + 12);

}

void
__glXDispSwap_Scaled(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 24);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);
    __GLX_SWAP_DOUBLE(pc + 16);

}

void
__glXDispSwap_Scalef(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_Translated(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 24);
        pc -= 4;
    }
#endif
    __GLX_SWAP_DOUBLE(pc + 0);
    __GLX_SWAP_DOUBLE(pc + 8);
    __GLX_SWAP_DOUBLE(pc + 16);

}

void
__glXDispSwap_Translatef(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_Viewport(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);

}

void
__glXDispSwap_PolygonOffset(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);

}

void
__glXDispSwap_CopyTexImage1D(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_INT(pc + 20);
    __GLX_SWAP_INT(pc + 24);

}

void
__glXDispSwap_CopyTexImage2D(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_INT(pc + 20);
    __GLX_SWAP_INT(pc + 24);
    __GLX_SWAP_INT(pc + 28);

}

void
__glXDispSwap_CopyTexSubImage1D(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_INT(pc + 20);

}

void
__glXDispSwap_CopyTexSubImage2D(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_INT(pc + 20);
    __GLX_SWAP_INT(pc + 24);
    __GLX_SWAP_INT(pc + 28);

}

void
__glXDispSwap_BindTexture(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_PrioritizeTextures(GLbyte * pc)
{
    GLsizei n;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    n = *(GLsizei *) (pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 4, n);
    __GLX_SWAP_FLOAT_ARRAY(pc + 4 + n * 4, n);

}

void
__glXDispSwap_Indexubv(GLbyte * pc)
{
}

void
__glXDispSwap_BlendColor(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_FLOAT(pc + 0);
    __GLX_SWAP_FLOAT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);
    __GLX_SWAP_FLOAT(pc + 12);

}

void
__glXDispSwap_BlendEquation(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_ColorTableParameterfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glColorTableParameterfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_ColorTableParameteriv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glColorTableParameteriv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_CopyColorTable(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);

}

void
__glXDispSwap_CopyColorSubTable(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);

}

void
__glXDispSwap_ConvolutionParameterf(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_FLOAT(pc + 8);

}

void
__glXDispSwap_ConvolutionParameterfv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glConvolutionParameterfv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_ConvolutionParameteri(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_ConvolutionParameteriv(GLbyte * pc)
{
    GLenum pname;
    GLint compsize;

    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 4);
    pname = *(GLenum *) (pc + 4);
    compsize = __glConvolutionParameteriv_size(pname);
    if (compsize < 0)
        compsize = 0;
    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 8, compsize);

}

void
__glXDispSwap_CopyConvolutionFilter1D(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);

}

void
__glXDispSwap_CopyConvolutionFilter2D(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_INT(pc + 20);

}

void
__glXDispSwap_Histogram(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);

}

void
__glXDispSwap_Minmax(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);

}

void
__glXDispSwap_ResetHistogram(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_ResetMinmax(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_CopyTexSubImage3D(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT(pc + 4);
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_INT(pc + 12);
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_INT(pc + 20);
    __GLX_SWAP_INT(pc + 24);
    __GLX_SWAP_INT(pc + 28);
    __GLX_SWAP_INT(pc + 32);

}

void
__glXDispSwap_ActiveTextureARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;

    __GLX_SWAP_INT(pc + 0);

}

void
__glXDispSwap_MultiTexCoord1dvARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 12);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 8);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 1);

}

void
__glXDispSwap_MultiTexCoord1fvARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 4, 1);

}

void
__glXDispSwap_MultiTexCoord1ivARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 4, 1);

}

void
__glXDispSwap_MultiTexCoord1svARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_SHORT_ARRAY(pc + 4, 1);

}

void
__glXDispSwap_MultiTexCoord2dvARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 20);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 16);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 2);

}

void
__glXDispSwap_MultiTexCoord2fvARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 4, 2);

}

void
__glXDispSwap_MultiTexCoord2ivARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 4, 2);

}

void
__glXDispSwap_MultiTexCoord2svARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_SHORT_ARRAY(pc + 4, 2);

}

void
__glXDispSwap_MultiTexCoord3dvARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 28);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 24);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 3);

}

void
__glXDispSwap_MultiTexCoord3fvARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 4, 3);

}

void
__glXDispSwap_MultiTexCoord3ivARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 4, 3);

}

void
__glXDispSwap_MultiTexCoord3svARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_SHORT_ARRAY(pc + 4, 3);

}

void
__glXDispSwap_MultiTexCoord4dvARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

#ifdef __GLX_ALIGN64
    if ((unsigned long) (pc) & 7) {
        __GLX_MEM_COPY(pc - 4, pc, 36);
        pc -= 4;
    }
#endif
    __GLX_SWAP_INT(pc + 32);
    __GLX_SWAP_DOUBLE_ARRAY(pc + 0, 4);

}

void
__glXDispSwap_MultiTexCoord4fvARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_FLOAT_ARRAY(pc + 4, 4);

}

void
__glXDispSwap_MultiTexCoord4ivARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_INT_ARRAY(pc + 4, 4);

}

void
__glXDispSwap_MultiTexCoord4svARB(GLbyte * pc)
{
    __GLX_DECLARE_SWAP_VARIABLES;
    __GLX_DECLARE_SWAP_ARRAY_VARIABLES;

    __GLX_SWAP_INT(pc + 0);
    __GLX_SWAP_SHORT_ARRAY(pc + 4, 4);

}
