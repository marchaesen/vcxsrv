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

#include <GL/gl.h>
#include "compsize.h"

GLint
__glFogiv_size(GLenum pname)
{
    switch (pname) {
    case GL_FOG_COLOR:
        return 4;
    case GL_FOG_DENSITY:
        return 1;
    case GL_FOG_END:
        return 1;
    case GL_FOG_MODE:
        return 1;
    case GL_FOG_INDEX:
        return 1;
    case GL_FOG_START:
        return 1;
    default:
        return 0;
    }
}

GLint
__glFogfv_size(GLenum pname)
{
    return __glFogiv_size(pname);
}

GLint
__glCallLists_size(GLsizei n, GLenum type)
{
    GLint size;

    if (n < 0)
        return 0;
    switch (type) {
    case GL_BYTE:
        size = 1;
        break;
    case GL_UNSIGNED_BYTE:
        size = 1;
        break;
    case GL_SHORT:
        size = 2;
        break;
    case GL_UNSIGNED_SHORT:
        size = 2;
        break;
    case GL_INT:
        size = 4;
        break;
    case GL_UNSIGNED_INT:
        size = 4;
        break;
    case GL_FLOAT:
        size = 4;
        break;
    case GL_2_BYTES:
        size = 2;
        break;
    case GL_3_BYTES:
        size = 3;
        break;
    case GL_4_BYTES:
        size = 4;
        break;
    default:
        return 0;
    }
    return n * size;
}

GLint
__glDrawPixels_size(GLenum format, GLenum type, GLsizei w, GLsizei h)
{
    GLint elements, esize;

    switch (format) {
    case GL_COLOR_INDEX:
    case GL_STENCIL_INDEX:
    case GL_DEPTH_COMPONENT:
        elements = 1;
        break;
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        elements = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        elements = 2;
        break;
    case GL_RGB:
        elements = 3;
        break;
    case GL_RGBA:
    case GL_ABGR_EXT:
        elements = 4;
        break;
    default:
        return 0;
    }
    switch (type) {
    case GL_BITMAP:
        if (format == GL_COLOR_INDEX || format == GL_STENCIL_INDEX) {
            return (h * ((w + 7) / 8));
        }
        else {
            return 0;
        }
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        esize = 1;
        break;
    case GL_UNSIGNED_BYTE_3_3_2:
    case GL_UNSIGNED_BYTE_2_3_3_REV:
        esize = 1;
        elements = 1;
        break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        esize = 2;
        break;
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        esize = 2;
        elements = 1;
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        esize = 4;
        break;
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        esize = 4;
        elements = 1;
        break;
    default:
        return 0;
    }
    return elements * esize * w * h;
}

GLint
__glBitmap_size(GLsizei w, GLsizei h)
{
    return __glDrawPixels_size(GL_COLOR_INDEX, GL_BITMAP, w, h);
}

GLint
__glTexGendv_size(GLenum e)
{
    switch (e) {
    case GL_TEXTURE_GEN_MODE:
        return 1;
    case GL_OBJECT_PLANE:
    case GL_EYE_PLANE:
        return 4;
    default:
        return 0;
    }
}

GLint
__glTexGenfv_size(GLenum e)
{
    return __glTexGendv_size(e);
}

GLint
__glTexGeniv_size(GLenum e)
{
    return __glTexGendv_size(e);
}

GLint
__glTexParameterfv_size(GLenum e)
{
    switch (e) {
    case GL_TEXTURE_WRAP_S:
    case GL_TEXTURE_WRAP_T:
    case GL_TEXTURE_WRAP_R:
    case GL_TEXTURE_MIN_FILTER:
    case GL_TEXTURE_MAG_FILTER:
        return 1;
    case GL_TEXTURE_BORDER_COLOR:
        return 4;
    case GL_TEXTURE_PRIORITY:
        return 1;
    case GL_TEXTURE_MIN_LOD:
    case GL_TEXTURE_MAX_LOD:
    case GL_TEXTURE_BASE_LEVEL:
    case GL_TEXTURE_MAX_LEVEL:
        return 1;
    default:
        return 0;
    }
}

GLint
__glTexParameteriv_size(GLenum e)
{
    return __glTexParameterfv_size(e);
}

GLint
__glTexEnvfv_size(GLenum e)
{
    switch (e) {
    case GL_TEXTURE_ENV_MODE:
        return 1;
    case GL_TEXTURE_ENV_COLOR:
        return 4;
    default:
        return 0;
    }
}

GLint
__glTexEnviv_size(GLenum e)
{
    return __glTexEnvfv_size(e);
}

GLint
__glTexImage1D_size(GLenum format, GLenum type, GLsizei w)
{
    GLint elements, esize;

    if (w < 0)
        return 0;
    switch (format) {
    case GL_COLOR_INDEX:
        elements = 1;
        break;
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        elements = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        elements = 2;
        break;
    case GL_RGB:
        elements = 3;
        break;
    case GL_RGBA:
    case GL_ABGR_EXT:
        elements = 4;
        break;
    default:
        return 0;
    }
    switch (type) {
    case GL_BITMAP:
        if (format == GL_COLOR_INDEX) {
            return (w + 7) / 8;
        }
        else {
            return 0;
        }
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        esize = 1;
        break;
    case GL_UNSIGNED_BYTE_3_3_2:
    case GL_UNSIGNED_BYTE_2_3_3_REV:
        esize = 1;
        elements = 1;
        break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        esize = 2;
        break;
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        esize = 2;
        elements = 1;
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        esize = 4;
        break;
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        esize = 4;
        elements = 1;
        break;
    default:
        return 0;
    }
    return elements * esize * w;
}

GLint
__glTexImage2D_size(GLenum format, GLenum type, GLsizei w, GLsizei h)
{
    GLint elements, esize;

    if (w < 0)
        return 0;
    if (h < 0)
        return 0;
    switch (format) {
    case GL_COLOR_INDEX:
        elements = 1;
        break;
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        elements = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        elements = 2;
        break;
    case GL_RGB:
        elements = 3;
        break;
    case GL_RGBA:
    case GL_ABGR_EXT:
        elements = 4;
        break;
    default:
        return 0;
    }
    switch (type) {
    case GL_BITMAP:
        if (format == GL_COLOR_INDEX) {
            return (h * ((w + 7) / 8));
        }
        else {
            return 0;
        }
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        esize = 1;
        break;
    case GL_UNSIGNED_BYTE_3_3_2:
    case GL_UNSIGNED_BYTE_2_3_3_REV:
        esize = 1;
        elements = 1;
        break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        esize = 2;
        break;
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        esize = 2;
        elements = 1;
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        esize = 4;
        break;
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        esize = 4;
        elements = 1;
        break;
    default:
        return 0;
    }
    return elements * esize * w * h;
}

GLint
__glTexImage3D_size(GLenum format, GLenum type, GLsizei w, GLsizei h, GLsizei d)
{
    GLint elements, esize;

    if (w < 0)
        return 0;
    if (h < 0)
        return 0;
    if (d < 0)
        return 0;
    switch (format) {
    case GL_COLOR_INDEX:
        elements = 1;
        break;
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        elements = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        elements = 2;
        break;
    case GL_RGB:
        elements = 3;
        break;
    case GL_RGBA:
    case GL_ABGR_EXT:
        elements = 4;
        break;
    default:
        return 0;
    }
    switch (type) {
    case GL_BITMAP:
        if (format == GL_COLOR_INDEX) {
            return (d * (h * ((w + 7) / 8)));
        }
        else {
            return 0;
        }
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        esize = 1;
        break;
    case GL_UNSIGNED_BYTE_3_3_2:
    case GL_UNSIGNED_BYTE_2_3_3_REV:
        esize = 1;
        elements = 1;
        break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        esize = 2;
        break;
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_6_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_4_4_4_4_REV:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
        esize = 2;
        elements = 1;
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
    case GL_FLOAT:
        esize = 4;
        break;
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_10_10_10_2:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        esize = 4;
        elements = 1;
        break;
    default:
        return 0;
    }
    return elements * esize * w * h * d;
}

GLint
__glLightfv_size(GLenum pname)
{
    switch (pname) {
    case GL_SPOT_EXPONENT:
        return 1;
    case GL_SPOT_CUTOFF:
        return 1;
    case GL_AMBIENT:
        return 4;
    case GL_DIFFUSE:
        return 4;
    case GL_SPECULAR:
        return 4;
    case GL_POSITION:
        return 4;
    case GL_SPOT_DIRECTION:
        return 3;
    case GL_CONSTANT_ATTENUATION:
        return 1;
    case GL_LINEAR_ATTENUATION:
        return 1;
    case GL_QUADRATIC_ATTENUATION:
        return 1;
    default:
        return 0;
    }
}

GLint
__glLightiv_size(GLenum pname)
{
    return __glLightfv_size(pname);
}

GLint
__glLightModelfv_size(GLenum pname)
{
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT:
        return 4;
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
        return 1;
    case GL_LIGHT_MODEL_TWO_SIDE:
        return 1;
    case GL_LIGHT_MODEL_COLOR_CONTROL:
        return 1;
    default:
        return 0;
    }
}

GLint
__glLightModeliv_size(GLenum pname)
{
    return __glLightModelfv_size(pname);
}

GLint
__glMaterialfv_size(GLenum pname)
{
    switch (pname) {
    case GL_SHININESS:
        return 1;
    case GL_EMISSION:
        return 4;
    case GL_AMBIENT:
        return 4;
    case GL_DIFFUSE:
        return 4;
    case GL_SPECULAR:
        return 4;
    case GL_AMBIENT_AND_DIFFUSE:
        return 4;
    case GL_COLOR_INDEXES:
        return 3;
    default:
        return 0;
    }
}

GLint
__glMaterialiv_size(GLenum pname)
{
    return __glMaterialfv_size(pname);
}

GLint
__glColorTableParameterfv_size(GLenum pname)
{
    switch (pname) {
    case GL_COLOR_TABLE_FORMAT:
    case GL_COLOR_TABLE_WIDTH:
    case GL_COLOR_TABLE_RED_SIZE:
    case GL_COLOR_TABLE_GREEN_SIZE:
    case GL_COLOR_TABLE_BLUE_SIZE:
    case GL_COLOR_TABLE_ALPHA_SIZE:
    case GL_COLOR_TABLE_LUMINANCE_SIZE:
    case GL_COLOR_TABLE_INTENSITY_SIZE:
        return 1;
    case GL_COLOR_TABLE_SCALE:
    case GL_COLOR_TABLE_BIAS:
        return 4;
    default:
        return -1;
    }
}

GLint
__glColorTableParameteriv_size(GLenum pname)
{
    return __glColorTableParameterfv_size(pname);
}

GLint
__glConvolutionParameterfv_size(GLenum pname)
{
    switch (pname) {
    case GL_CONVOLUTION_BORDER_MODE:
        return 1;
    case GL_CONVOLUTION_BORDER_COLOR:
    case GL_CONVOLUTION_FILTER_SCALE:
    case GL_CONVOLUTION_FILTER_BIAS:
        return 4;
    default:                   /* error: bad enum value */
        return -1;
    }
}

GLint
__glConvolutionParameteriv_size(GLenum pname)
{
    return __glConvolutionParameterfv_size(pname);
}
