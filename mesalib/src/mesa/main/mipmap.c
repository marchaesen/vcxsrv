/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/**
 * \file mipmap.c  mipmap generation and teximage resizing functions.
 */

#include "errors.h"

#include "formats.h"
#include "glformats.h"
#include "mipmap.h"
#include "mtypes.h"
#include "teximage.h"
#include "texobj.h"
#include "texstore.h"
#include "image.h"
#include "macros.h"
#include "util/half_float.h"
#include "util/format_rgb9e5.h"
#include "util/format_r11g11b10f.h"

#include "state_tracker/st_cb_texture.h"

/**
 * Compute the expected number of mipmap levels in the texture given
 * the width/height/depth of the base image and the GL_TEXTURE_BASE_LEVEL/
 * GL_TEXTURE_MAX_LEVEL settings.  This will tell us how many mipmap
 * levels should be generated.
 */
unsigned
_mesa_compute_num_levels(struct gl_context *ctx,
                         struct gl_texture_object *texObj,
                         GLenum target)
{
   const struct gl_texture_image *baseImage;
   GLuint numLevels;

   baseImage = _mesa_get_tex_image(ctx, texObj, target, texObj->Attrib.BaseLevel);

   numLevels = texObj->Attrib.BaseLevel + baseImage->MaxNumLevels;
   numLevels = MIN2(numLevels, (GLuint) texObj->Attrib.MaxLevel + 1);
   if (texObj->Immutable)
      numLevels = MIN2(numLevels, texObj->Attrib.NumLevels);
   assert(numLevels >= 1);

   return numLevels;
}

#define MAX_SPAN_WIDTH 64

static void
do_span_zs(enum pipe_format format, int srcWidth,
           const void *srcRowA, const void *srcRowB,
           int dstWidth, void *dstRow)
{
   ASSERTED const struct util_format_description *desc =
      util_format_description(format);

   assert(desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS);
   assert(srcWidth <= MAX_SPAN_WIDTH);
   assert(dstWidth <= MAX_SPAN_WIDTH);
   assert(util_format_has_depth(desc) &&
          !util_format_has_stencil(desc));

   float rowA[MAX_SPAN_WIDTH], rowB[MAX_SPAN_WIDTH],
         result[MAX_SPAN_WIDTH];

   util_format_unpack_z_float(format, rowA, srcRowA, srcWidth);
   util_format_unpack_z_float(format, rowB, srcRowB, srcWidth);

   if (srcWidth == dstWidth) {
      for (unsigned i = 0; i < dstWidth; ++i) {
         result[i] = (rowA[i] + rowB[i]) / 2;
      }
   } else {
      for (unsigned i = 0; i < dstWidth; ++i) {
         result[i] = (rowA[i * 2 + 0] + rowA[i * 2 + 1] +
                      rowB[i * 2 + 0] + rowB[i * 2 + 1]) / 4;
      }
   }

   util_format_pack_z_float(format, dstRow, result, dstWidth);
}

static void
do_span_rgba_unorm8(enum pipe_format format, int srcWidth,
                    const void *srcRowA, const void *srcRowB,
                    int dstWidth, void *dstRow)
{
   assert(util_format_description(format)->colorspace !=
          UTIL_FORMAT_COLORSPACE_ZS);
   assert(srcWidth <= MAX_SPAN_WIDTH);
   assert(dstWidth <= MAX_SPAN_WIDTH);

   const struct util_format_unpack_description *unpack =
      util_format_unpack_description(format);

   const struct util_format_pack_description *pack =
      util_format_pack_description(format);

   uint8_t rowA[MAX_SPAN_WIDTH * 4], rowB[MAX_SPAN_WIDTH * 4];
   uint8_t result[MAX_SPAN_WIDTH * 4];

   unpack->unpack_rgba_8unorm(rowA, srcRowA, srcWidth);
   unpack->unpack_rgba_8unorm(rowB, srcRowB, srcWidth);

   if (srcWidth == dstWidth) {
      for (unsigned i = 0; i < dstWidth; ++i) {
         int idx = i * 4;
         for (unsigned c = 0; c < 4; ++c)
            result[idx + c] = (rowA[idx + c] + rowB[idx + c]) / 2;
      }
   } else {
      for (unsigned i = 0; i < dstWidth; ++i) {
         int idx = i * 2 * 4;
         for (unsigned c = 0; c < 4; ++c) {
            result[i * 4 + c] = (rowA[idx + c] + rowA[idx + 4 + c] +
                                 rowB[idx + c] + rowB[idx + 4 + c]) / 4;
         }
      }
   }

   pack->pack_rgba_8unorm(dstRow, 0, result, 0, dstWidth, 1);
}

static void
do_span_rgba_float(enum pipe_format format, int srcWidth,
                   const void *srcRowA, const void *srcRowB,
                   int dstWidth, void *dstRow)
{
   assert(util_format_description(format)->colorspace !=
          UTIL_FORMAT_COLORSPACE_ZS);
   assert(srcWidth <= MAX_SPAN_WIDTH);
   assert(dstWidth <= MAX_SPAN_WIDTH);

   float rowA[MAX_SPAN_WIDTH][4], rowB[MAX_SPAN_WIDTH][4];
   float result[MAX_SPAN_WIDTH][4];
   util_format_unpack_rgba(format, rowA, srcRowA, srcWidth);
   util_format_unpack_rgba(format, rowB, srcRowB, srcWidth);

   if (srcWidth == dstWidth) {
      for (unsigned i = 0; i < dstWidth; ++i) {
         for (unsigned c = 0; c < 4; ++c)
            result[i][c] = (rowA[i][c] + rowB[i][c]) / 2;
      }
   } else {
      for (unsigned i = 0; i < dstWidth; ++i) {
         int idx = i * 2;
         for (unsigned c = 0; c < 4; ++c)
            result[i][c] = (rowA[idx][c] + rowA[idx + 1][c] +
                            rowB[idx][c] + rowB[idx + 1][c]) / 4;
      }
   }

   util_format_pack_rgba(format, dstRow, result, dstWidth);
}

/**
 * Average together two spans of a source image to produce a single
 * new span in the dest image. The difference between a row and a span
 * is that a span is limited to MAX_SPAN_WIDTH pixels, which means
 * that they can be processed with stack-allocated immediate buffers.
 * The dest width must be equal to either the source width or half the
 * source width.
 */

static void
do_span(enum pipe_format format, int srcWidth,
        const void *srcRowA, const void *srcRowB,
        int dstWidth, void *dstRow)
{
   assert(dstWidth == srcWidth || dstWidth == srcWidth / 2);
   const struct util_format_description *desc =
      util_format_description(format);

   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
      do_span_zs(format, srcWidth, srcRowA, srcRowB, dstWidth, dstRow);
   else if (util_format_fits_8unorm(desc))
      do_span_rgba_unorm8(format, srcWidth, srcRowA, srcRowB, dstWidth,
                          dstRow);
   else
      do_span_rgba_float(format, srcWidth, srcRowA, srcRowB, dstWidth,
                         dstRow);
}

static void
do_span_3D(enum pipe_format format, int srcWidth,
           const void *srcRowA, const void *srcRowB,
           const void *srcRowC, const void *srcRowD,
           int dstWidth, void *dstRow)
{
   uint32_t tmp1[MAX_SPAN_WIDTH * 4], tmp2[MAX_SPAN_WIDTH * 4];
   do_span(format, srcWidth, srcRowA, srcRowB, dstWidth, tmp1);
   do_span(format, srcWidth, srcRowC, srcRowD, dstWidth, tmp2);
   do_span(format, dstWidth, tmp1, tmp2, dstWidth, dstRow);
}

/**
 * Average together two rows of a source image to produce a single new
 * row in the dest image.  It's legal for the two source rows to point
 * to the same data.  The dest width must be equal to the largest of
 * half the source width or one.
 */
static void
do_row(enum pipe_format format, int srcWidth,
       const uint8_t *srcRowA, const uint8_t *srcRowB,
       int dstWidth, uint8_t *dstRow)
{
   assert(dstWidth == MAX2(srcWidth / 2, 1));
   assert(srcWidth > 0 && dstWidth > 0);

   do {
      unsigned blocksize = util_format_get_blocksize(format);
      int w = MIN2(srcWidth, MAX_SPAN_WIDTH);
      do_span(format, w, srcRowA, srcRowB, MAX2(w / 2, 1), dstRow);
      srcWidth -= MAX_SPAN_WIDTH;
      srcRowA += MAX_SPAN_WIDTH * blocksize;
      srcRowB += MAX_SPAN_WIDTH * blocksize;
      dstWidth -= MAX_SPAN_WIDTH / 2;
      dstRow += (MAX_SPAN_WIDTH / 2) * blocksize;
   } while (dstWidth > 0);
}

/**
 * Average together four rows of a source image to produce a single new
 * row in the dest image.  It's legal for the two source rows to point
 * to the same data.  The source width must be equal to either the
 * dest width or one.
 *
 * \param srcWidth  Width of a row in the source data
 * \param srcRowA   Pointer to one of the rows of source data
 * \param srcRowB   Pointer to one of the rows of source data
 * \param srcRowC   Pointer to one of the rows of source data
 * \param srcRowD   Pointer to one of the rows of source data
 * \param dstWidth  Width of a row in the destination data
 * \param srcRowA   Pointer to the row of destination data
 */
static void
do_row_3D(enum pipe_format format, int srcWidth,
          const uint8_t *srcRowA, const uint8_t *srcRowB,
          const uint8_t *srcRowC, const uint8_t *srcRowD,
          int dstWidth, uint8_t *dstRow)
{
   assert(dstWidth == MAX2(srcWidth / 2, 1));
   assert(srcWidth > 0 && dstWidth > 0);

   do {
      unsigned blocksize = util_format_get_blocksize(format);
      int w = MIN2(srcWidth, MAX_SPAN_WIDTH);
      do_span_3D(format, w, srcRowA, srcRowB, srcRowC, srcRowD, MAX2(w / 2, 1),
                 dstRow);
      srcWidth -= MAX_SPAN_WIDTH;
      srcRowA += MAX_SPAN_WIDTH * blocksize;
      srcRowB += MAX_SPAN_WIDTH * blocksize;
      dstWidth -= MAX_SPAN_WIDTH / 2;
      dstRow += (MAX_SPAN_WIDTH / 2) * blocksize;
   } while (dstWidth > 0);
}


/*
 * These functions generate a 1/2-size mipmap image from a source image.
 * Texture borders are handled by copying or averaging the source image's
 * border texels, depending on the scale-down factor.
 */

static void
make_1d_mipmap(enum pipe_format format, GLint border,
               GLint srcWidth, const GLubyte *srcPtr,
               GLint dstWidth, GLubyte *dstPtr)
{
   const GLint bpt = util_format_get_blocksize(format);
   const GLubyte *src;
   GLubyte *dst;

   /* skip the border pixel, if any */
   src = srcPtr + border * bpt;
   dst = dstPtr + border * bpt;

   /* we just duplicate the input row, kind of hack, saves code */
   do_row(format, srcWidth - 2 * border, src, src,
          dstWidth - 2 * border, dst);

   if (border) {
      /* copy left-most pixel from source */
      assert(dstPtr);
      assert(srcPtr);
      memcpy(dstPtr, srcPtr, bpt);
      /* copy right-most pixel from source */
      memcpy(dstPtr + (dstWidth - 1) * bpt,
             srcPtr + (srcWidth - 1) * bpt,
             bpt);
   }
}


static void
make_2d_mipmap(enum pipe_format format, GLint border,
               GLint srcWidth, GLint srcHeight,
               const GLubyte *srcPtr, GLint srcRowStride,
               GLint dstWidth, GLint dstHeight,
               GLubyte *dstPtr, GLint dstRowStride)
{
   const GLint bpt = util_format_get_blocksize(format);
   const GLint srcWidthNB = srcWidth - 2 * border;  /* sizes w/out border */
   const GLint dstWidthNB = dstWidth - 2 * border;
   const GLint dstHeightNB = dstHeight - 2 * border;
   const GLubyte *srcA, *srcB;
   GLubyte *dst;
   GLint row, srcRowStep;

   /* Compute src and dst pointers, skipping any border */
   srcA = srcPtr + border * ((srcWidth + 1) * bpt);
   if (srcHeight > 1 && srcHeight > dstHeight) {
      /* sample from two source rows */
      srcB = srcA + srcRowStride;
      srcRowStep = 2;
   }
   else {
      /* sample from one source row */
      srcB = srcA;
      srcRowStep = 1;
   }

   dst = dstPtr + border * ((dstWidth + 1) * bpt);

   for (row = 0; row < dstHeightNB; row++) {
      do_row(format, srcWidthNB, srcA, srcB,
             dstWidthNB, dst);
      srcA += srcRowStep * srcRowStride;
      srcB += srcRowStep * srcRowStride;
      dst += dstRowStride;
   }

   /* This is ugly but probably won't be used much */
   if (border > 0) {
      /* fill in dest border */
      /* lower-left border pixel */
      assert(dstPtr);
      assert(srcPtr);
      memcpy(dstPtr, srcPtr, bpt);
      /* lower-right border pixel */
      memcpy(dstPtr + (dstWidth - 1) * bpt,
             srcPtr + (srcWidth - 1) * bpt, bpt);
      /* upper-left border pixel */
      memcpy(dstPtr + dstWidth * (dstHeight - 1) * bpt,
             srcPtr + srcWidth * (srcHeight - 1) * bpt, bpt);
      /* upper-right border pixel */
      memcpy(dstPtr + (dstWidth * dstHeight - 1) * bpt,
             srcPtr + (srcWidth * srcHeight - 1) * bpt, bpt);
      /* lower border */
      do_row(format, srcWidthNB,
             srcPtr + bpt,
             srcPtr + bpt,
             dstWidthNB, dstPtr + bpt);
      /* upper border */
      do_row(format, srcWidthNB,
             srcPtr + (srcWidth * (srcHeight - 1) + 1) * bpt,
             srcPtr + (srcWidth * (srcHeight - 1) + 1) * bpt,
             dstWidthNB,
             dstPtr + (dstWidth * (dstHeight - 1) + 1) * bpt);
      /* left and right borders */
      if (srcHeight == dstHeight) {
         /* copy border pixel from src to dst */
         for (row = 1; row < srcHeight; row++) {
            memcpy(dstPtr + dstWidth * row * bpt,
                   srcPtr + srcWidth * row * bpt, bpt);
            memcpy(dstPtr + (dstWidth * row + dstWidth - 1) * bpt,
                   srcPtr + (srcWidth * row + srcWidth - 1) * bpt, bpt);
         }
      }
      else {
         /* average two src pixels each dest pixel */
         for (row = 0; row < dstHeightNB; row += 2) {
            do_row(format, 1,
                   srcPtr + (srcWidth * (row * 2 + 1)) * bpt,
                   srcPtr + (srcWidth * (row * 2 + 2)) * bpt,
                   1, dstPtr + (dstWidth * row + 1) * bpt);
            do_row(format, 1,
                   srcPtr + (srcWidth * (row * 2 + 1) + srcWidth - 1) * bpt,
                   srcPtr + (srcWidth * (row * 2 + 2) + srcWidth - 1) * bpt,
                   1, dstPtr + (dstWidth * row + 1 + dstWidth - 1) * bpt);
         }
      }
   }
}


static void
make_3d_mipmap(enum pipe_format format, GLint border,
               GLint srcWidth, GLint srcHeight, GLint srcDepth,
               const GLubyte **srcPtr, GLint srcRowStride,
               GLint dstWidth, GLint dstHeight, GLint dstDepth,
               GLubyte **dstPtr, GLint dstRowStride)
{
   const GLint bpt = util_format_get_blocksize(format);
   const GLint srcWidthNB = srcWidth - 2 * border;  /* sizes w/out border */
   const GLint srcDepthNB = srcDepth - 2 * border;
   const GLint dstWidthNB = dstWidth - 2 * border;
   const GLint dstHeightNB = dstHeight - 2 * border;
   const GLint dstDepthNB = dstDepth - 2 * border;
   GLint img, row;
   GLint bytesPerSrcImage, bytesPerDstImage;
   GLint srcImageOffset, srcRowOffset;

   (void) srcDepthNB; /* silence warnings */

   bytesPerSrcImage = srcRowStride * srcHeight * bpt;
   bytesPerDstImage = dstRowStride * dstHeight * bpt;

   /* Offset between adjacent src images to be averaged together */
   srcImageOffset = (srcDepth == dstDepth) ? 0 : 1;

   /* Offset between adjacent src rows to be averaged together */
   srcRowOffset = (srcHeight == dstHeight) ? 0 : srcRowStride;

   /*
    * Need to average together up to 8 src pixels for each dest pixel.
    * Break that down into 3 operations:
    *   1. take two rows from source image and average them together.
    *   2. take two rows from next source image and average them together.
    *   3. take the two averaged rows and average them for the final dst row.
    */

   /*
   printf("mip3d %d x %d x %d  ->  %d x %d x %d\n",
          srcWidth, srcHeight, srcDepth, dstWidth, dstHeight, dstDepth);
   */

   for (img = 0; img < dstDepthNB; img++) {
      /* first source image pointer, skipping border */
      const GLubyte *imgSrcA = srcPtr[img * 2 + border]
         + srcRowStride * border + bpt * border;
      /* second source image pointer, skipping border */
      const GLubyte *imgSrcB = srcPtr[img * 2 + srcImageOffset + border]
         + srcRowStride * border + bpt * border;

      /* address of the dest image, skipping border */
      GLubyte *imgDst = dstPtr[img + border]
         + dstRowStride * border + bpt * border;

      /* setup the four source row pointers and the dest row pointer */
      const GLubyte *srcImgARowA = imgSrcA;
      const GLubyte *srcImgARowB = imgSrcA + srcRowOffset;
      const GLubyte *srcImgBRowA = imgSrcB;
      const GLubyte *srcImgBRowB = imgSrcB + srcRowOffset;
      GLubyte *dstImgRow = imgDst;

      for (row = 0; row < dstHeightNB; row++) {
         do_row_3D(format, srcWidthNB,
                   srcImgARowA, srcImgARowB,
                   srcImgBRowA, srcImgBRowB,
                   dstWidthNB, dstImgRow);

         /* advance to next rows */
         srcImgARowA += srcRowStride + srcRowOffset;
         srcImgARowB += srcRowStride + srcRowOffset;
         srcImgBRowA += srcRowStride + srcRowOffset;
         srcImgBRowB += srcRowStride + srcRowOffset;
         dstImgRow += dstRowStride;
      }
   }


   /* Luckily we can leverage the make_2d_mipmap() function here! */
   if (border > 0) {
      /* do front border image */
      make_2d_mipmap(format, 1,
                     srcWidth, srcHeight, srcPtr[0], srcRowStride,
                     dstWidth, dstHeight, dstPtr[0], dstRowStride);
      /* do back border image */
      make_2d_mipmap(format, 1,
                     srcWidth, srcHeight, srcPtr[srcDepth - 1], srcRowStride,
                     dstWidth, dstHeight, dstPtr[dstDepth - 1], dstRowStride);

      /* do four remaining border edges that span the image slices */
      if (srcDepth == dstDepth) {
         /* just copy border pixels from src to dst */
         for (img = 0; img < dstDepthNB; img++) {
            const GLubyte *src;
            GLubyte *dst;

            /* do border along [img][row=0][col=0] */
            src = srcPtr[img * 2];
            dst = dstPtr[img];
            memcpy(dst, src, bpt);

            /* do border along [img][row=dstHeight-1][col=0] */
            src = srcPtr[img * 2] + (srcHeight - 1) * srcRowStride;
            dst = dstPtr[img] + (dstHeight - 1) * dstRowStride;
            memcpy(dst, src, bpt);

            /* do border along [img][row=0][col=dstWidth-1] */
            src = srcPtr[img * 2] + (srcWidth - 1) * bpt;
            dst = dstPtr[img] + (dstWidth - 1) * bpt;
            memcpy(dst, src, bpt);

            /* do border along [img][row=dstHeight-1][col=dstWidth-1] */
            src = srcPtr[img * 2] + (bytesPerSrcImage - bpt);
            dst = dstPtr[img] + (bytesPerDstImage - bpt);
            memcpy(dst, src, bpt);
         }
      }
      else {
         /* average border pixels from adjacent src image pairs */
         assert(srcDepthNB == 2 * dstDepthNB);
         for (img = 0; img < dstDepthNB; img++) {
            const GLubyte *srcA, *srcB;
            GLubyte *dst;

            /* do border along [img][row=0][col=0] */
            srcA = srcPtr[img * 2 + 0];
            srcB = srcPtr[img * 2 + srcImageOffset];
            dst = dstPtr[img];
            do_row(format, 1, srcA, srcB, 1, dst);

            /* do border along [img][row=dstHeight-1][col=0] */
            srcA = srcPtr[img * 2 + 0]
               + (srcHeight - 1) * srcRowStride;
            srcB = srcPtr[img * 2 + srcImageOffset]
               + (srcHeight - 1) * srcRowStride;
            dst = dstPtr[img] + (dstHeight - 1) * dstRowStride;
            do_row(format, 1, srcA, srcB, 1, dst);

            /* do border along [img][row=0][col=dstWidth-1] */
            srcA = srcPtr[img * 2 + 0] + (srcWidth - 1) * bpt;
            srcB = srcPtr[img * 2 + srcImageOffset] + (srcWidth - 1) * bpt;
            dst = dstPtr[img] + (dstWidth - 1) * bpt;
            do_row(format, 1, srcA, srcB, 1, dst);

            /* do border along [img][row=dstHeight-1][col=dstWidth-1] */
            srcA = srcPtr[img * 2 + 0] + (bytesPerSrcImage - bpt);
            srcB = srcPtr[img * 2 + srcImageOffset] + (bytesPerSrcImage - bpt);
            dst = dstPtr[img] + (bytesPerDstImage - bpt);
            do_row(format, 1, srcA, srcB, 1, dst);
         }
      }
   }
}


/**
 * Down-sample a texture image to produce the next lower mipmap level.
 * \param comps  components per texel (1, 2, 3 or 4)
 * \param srcData  array[slice] of pointers to source image slices
 * \param dstData  array[slice] of pointers to dest image slices
 * \param srcRowStride  stride between source rows, in bytes
 * \param dstRowStride  stride between destination rows, in bytes
 */
static void
_mesa_generate_mipmap_level(GLenum target,
                            enum pipe_format format,
                            GLint border,
                            GLint srcWidth, GLint srcHeight, GLint srcDepth,
                            const GLubyte **srcData,
                            GLint srcRowStride,
                            GLint dstWidth, GLint dstHeight, GLint dstDepth,
                            GLubyte **dstData,
                            GLint dstRowStride)
{
   int i;

   switch (target) {
   case GL_TEXTURE_1D:
      make_1d_mipmap(format, border,
                     srcWidth, srcData[0],
                     dstWidth, dstData[0]);
      break;
   case GL_TEXTURE_2D:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
      make_2d_mipmap(format, border,
                     srcWidth, srcHeight, srcData[0], srcRowStride,
                     dstWidth, dstHeight, dstData[0], dstRowStride);
      break;
   case GL_TEXTURE_3D:
      make_3d_mipmap(format, border,
                     srcWidth, srcHeight, srcDepth,
                     srcData, srcRowStride,
                     dstWidth, dstHeight, dstDepth,
                     dstData, dstRowStride);
      break;
   case GL_TEXTURE_1D_ARRAY_EXT:
      assert(srcHeight == 1);
      assert(dstHeight == 1);
      for (i = 0; i < dstDepth; i++) {
         make_1d_mipmap(format, border,
                        srcWidth, srcData[i],
                        dstWidth, dstData[i]);
      }
      break;
   case GL_TEXTURE_2D_ARRAY_EXT:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
      for (i = 0; i < dstDepth; i++) {
         make_2d_mipmap(format, border,
                        srcWidth, srcHeight, srcData[i], srcRowStride,
                        dstWidth, dstHeight, dstData[i], dstRowStride);
      }
      break;
   case GL_TEXTURE_RECTANGLE_NV:
   case GL_TEXTURE_EXTERNAL_OES:
      /* no mipmaps, do nothing */
      break;
   default:
      unreachable("bad tex target in _mesa_generate_mipmaps");
   }
}


/**
 * compute next (level+1) image size
 * \return GL_FALSE if no smaller size can be generated (eg. src is 1x1x1 size)
 */
GLboolean
_mesa_next_mipmap_level_size(GLenum target, GLint border,
                       GLint srcWidth, GLint srcHeight, GLint srcDepth,
                       GLint *dstWidth, GLint *dstHeight, GLint *dstDepth)
{
   if (srcWidth - 2 * border > 1) {
      *dstWidth = (srcWidth - 2 * border) / 2 + 2 * border;
   }
   else {
      *dstWidth = srcWidth; /* can't go smaller */
   }

   if ((srcHeight - 2 * border > 1) &&
       target != GL_TEXTURE_1D_ARRAY_EXT &&
       target != GL_PROXY_TEXTURE_1D_ARRAY_EXT) {
      *dstHeight = (srcHeight - 2 * border) / 2 + 2 * border;
   }
   else {
      *dstHeight = srcHeight; /* can't go smaller */
   }

   if ((srcDepth - 2 * border > 1) &&
       target != GL_TEXTURE_2D_ARRAY_EXT &&
       target != GL_PROXY_TEXTURE_2D_ARRAY_EXT &&
       target != GL_TEXTURE_CUBE_MAP_ARRAY &&
       target != GL_PROXY_TEXTURE_CUBE_MAP_ARRAY) {
      *dstDepth = (srcDepth - 2 * border) / 2 + 2 * border;
   }
   else {
      *dstDepth = srcDepth; /* can't go smaller */
   }

   if (*dstWidth == srcWidth &&
       *dstHeight == srcHeight &&
       *dstDepth == srcDepth) {
      return GL_FALSE;
   }
   else {
      return GL_TRUE;
   }
}


/**
 * Helper function for mipmap generation.
 * Make sure the specified destination mipmap level is the right size/format
 * for mipmap generation.  If not, (re) allocate it.
 * \return GL_TRUE if successful, GL_FALSE if mipmap generation should stop
 */
static GLboolean
prepare_mipmap_level(struct gl_context *ctx,
                     struct gl_texture_object *texObj, GLuint level,
                     GLsizei width, GLsizei height, GLsizei depth,
                     GLsizei border, GLenum intFormat, mesa_format format)
{
   const GLuint numFaces = _mesa_num_tex_faces(texObj->Target);
   GLuint face;

   if (texObj->Immutable) {
      /* The texture was created with glTexStorage() so the number/size of
       * mipmap levels is fixed and the storage for all images is already
       * allocated.
       */
      if (!texObj->Image[0][level]) {
         /* No more levels to create - we're done */
         return GL_FALSE;
      }
      else {
         /* Nothing to do - the texture memory must have already been
          * allocated to the right size so we're all set.
          */
         return GL_TRUE;
      }
   }

   for (face = 0; face < numFaces; face++) {
      struct gl_texture_image *dstImage;
      const GLenum target = _mesa_cube_face_target(texObj->Target, face);

      dstImage = _mesa_get_tex_image(ctx, texObj, target, level);
      if (!dstImage) {
         /* out of memory */
         return GL_FALSE;
      }

      if (dstImage->Width != width ||
          dstImage->Height != height ||
          dstImage->Depth != depth ||
          dstImage->Border != border ||
          dstImage->InternalFormat != intFormat ||
          dstImage->TexFormat != format) {
         /* need to (re)allocate image */
         st_FreeTextureImageBuffer(ctx, dstImage);

         _mesa_init_teximage_fields(ctx, dstImage,
                                    width, height, depth,
                                    border, intFormat, format);

         st_AllocTextureImageBuffer(ctx, dstImage);

         /* in case the mipmap level is part of an FBO: */
         _mesa_update_fbo_texture(ctx, texObj, face, level);

         ctx->NewState |= _NEW_TEXTURE_OBJECT;
         ctx->PopAttribState |= GL_TEXTURE_BIT;
      }
   }

   return GL_TRUE;
}


/**
 * Prepare all mipmap levels beyond 'baseLevel' for mipmap generation.
 * When finished, all the gl_texture_image structures for the smaller
 * mipmap levels will be consistent with the base level (in terms of
 * dimensions, format, etc).
 */
void
_mesa_prepare_mipmap_levels(struct gl_context *ctx,
                            struct gl_texture_object *texObj,
                            unsigned baseLevel, unsigned maxLevel)
{
   const struct gl_texture_image *baseImage =
      _mesa_select_tex_image(texObj, texObj->Target, baseLevel);

   if (baseImage == NULL)
      return;

   const GLint border = 0;
   GLint width = baseImage->Width;
   GLint height = baseImage->Height;
   GLint depth = baseImage->Depth;
   const GLenum intFormat = baseImage->InternalFormat;
   const mesa_format texFormat = baseImage->TexFormat;
   GLint newWidth, newHeight, newDepth;

   /* Prepare baseLevel + 1, baseLevel + 2, ... */
   for (unsigned level = baseLevel + 1; level <= maxLevel; level++) {
      if (!_mesa_next_mipmap_level_size(texObj->Target, border,
                                        width, height, depth,
                                        &newWidth, &newHeight, &newDepth)) {
         /* all done */
         break;
      }

      if (!prepare_mipmap_level(ctx, texObj, level,
                                newWidth, newHeight, newDepth,
                                border, intFormat, texFormat)) {
         break;
      }

      width = newWidth;
      height = newHeight;
      depth = newDepth;
   }
}


static void
generate_mipmap_uncompressed(struct gl_context *ctx, GLenum target,
                             struct gl_texture_object *texObj,
                             const struct gl_texture_image *srcImage,
                             GLuint maxLevel)
{
   GLuint level;

   for (level = texObj->Attrib.BaseLevel; level < maxLevel; level++) {
      /* generate image[level+1] from image[level] */
      struct gl_texture_image *srcImage, *dstImage;
      GLint srcRowStride, dstRowStride;
      GLint srcWidth, srcHeight, srcDepth;
      GLint dstWidth, dstHeight, dstDepth;
      GLint border;
      GLint slice;
      GLubyte **srcMaps, **dstMaps;
      GLboolean success = GL_TRUE;

      /* get src image parameters */
      srcImage = _mesa_select_tex_image(texObj, target, level);
      assert(srcImage);
      srcWidth = srcImage->Width;
      srcHeight = srcImage->Height;
      srcDepth = srcImage->Depth;
      border = srcImage->Border;

      /* get dest gl_texture_image */
      dstImage = _mesa_select_tex_image(texObj, target, level + 1);
      if (!dstImage) {
         break;
      }
      dstWidth = dstImage->Width;
      dstHeight = dstImage->Height;
      dstDepth = dstImage->Depth;

      if (target == GL_TEXTURE_1D_ARRAY) {
         srcDepth = srcHeight;
         dstDepth = dstHeight;
         srcHeight = 1;
         dstHeight = 1;
      }

      /* Map src texture image slices */
      srcMaps = calloc(srcDepth, sizeof(GLubyte *));
      if (srcMaps) {
         for (slice = 0; slice < srcDepth; slice++) {
            st_MapTextureImage(ctx, srcImage, slice,
                               0, 0, srcWidth, srcHeight,
                               GL_MAP_READ_BIT,
                               &srcMaps[slice], &srcRowStride);
            if (!srcMaps[slice]) {
               success = GL_FALSE;
               break;
            }
         }
      }
      else {
         success = GL_FALSE;
      }

      /* Map dst texture image slices */
      dstMaps = calloc(dstDepth, sizeof(GLubyte *));
      if (dstMaps) {
         for (slice = 0; slice < dstDepth; slice++) {
            st_MapTextureImage(ctx, dstImage, slice,
                               0, 0, dstWidth, dstHeight,
                               GL_MAP_WRITE_BIT,
                               &dstMaps[slice], &dstRowStride);
            if (!dstMaps[slice]) {
               success = GL_FALSE;
               break;
            }
         }
      }
      else {
         success = GL_FALSE;
      }

      if (success) {
         /* generate one mipmap level (for 1D/2D/3D/array/etc texture) */
         _mesa_generate_mipmap_level(target, srcImage->TexFormat, border,
                                     srcWidth, srcHeight, srcDepth,
                                     (const GLubyte **) srcMaps, srcRowStride,
                                     dstWidth, dstHeight, dstDepth,
                                     dstMaps, dstRowStride);
      }

      /* Unmap src image slices */
      if (srcMaps) {
         for (slice = 0; slice < srcDepth; slice++) {
            if (srcMaps[slice]) {
               st_UnmapTextureImage(ctx, srcImage, slice);
            }
         }
         free(srcMaps);
      }

      /* Unmap dst image slices */
      if (dstMaps) {
         for (slice = 0; slice < dstDepth; slice++) {
            if (dstMaps[slice]) {
               st_UnmapTextureImage(ctx, dstImage, slice);
            }
         }
         free(dstMaps);
      }

      if (!success) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "mipmap generation");
         break;
      }
   } /* loop over mipmap levels */
}


static void
generate_mipmap_compressed(struct gl_context *ctx, GLenum target,
                           struct gl_texture_object *texObj,
                           struct gl_texture_image *srcImage,
                           GLuint maxLevel)
{
   GLuint level;
   mesa_format temp_format;
   GLuint temp_src_row_stride, temp_src_img_stride; /* in bytes */
   GLubyte *temp_src = NULL, *temp_dst = NULL;
   GLenum temp_datatype;
   GLenum temp_base_format;
   GLubyte **temp_src_slices = NULL, **temp_dst_slices = NULL;

   /* only two types of compressed textures at this time */
   assert(texObj->Target == GL_TEXTURE_2D ||
          texObj->Target == GL_TEXTURE_2D_ARRAY ||
          texObj->Target == GL_TEXTURE_CUBE_MAP ||
          texObj->Target == GL_TEXTURE_CUBE_MAP_ARRAY);

   /*
    * Choose a format for the temporary, uncompressed base image.
    * Then, get number of components, choose temporary image datatype,
    * and get base format.
    */
   temp_format = _mesa_get_uncompressed_format(srcImage->TexFormat);

   switch (_mesa_get_format_datatype(srcImage->TexFormat)) {
   case GL_FLOAT:
      temp_datatype = GL_FLOAT;
      break;
   case GL_SIGNED_NORMALIZED:
      /* Revisit this if we get compressed formats with >8 bits per component */
      temp_datatype = GL_BYTE;
      break;
   default:
      temp_datatype = GL_UNSIGNED_BYTE;
   }

   temp_base_format = _mesa_get_format_base_format(temp_format);


   /* allocate storage for the temporary, uncompressed image */
   temp_src_row_stride = _mesa_format_row_stride(temp_format, srcImage->Width);
   temp_src_img_stride = _mesa_format_image_size(temp_format, srcImage->Width,
                                                 srcImage->Height, 1);
   temp_src = malloc(temp_src_img_stride * srcImage->Depth);

   /* Allocate storage for arrays of slice pointers */
   temp_src_slices = malloc(srcImage->Depth * sizeof(GLubyte *));
   temp_dst_slices = malloc(srcImage->Depth * sizeof(GLubyte *));

   if (!temp_src || !temp_src_slices || !temp_dst_slices) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "generate mipmaps");
      goto end;
   }

   /* decompress base image to the temporary src buffer */
   {
      /* save pixel packing mode */
      struct gl_pixelstore_attrib save = ctx->Pack;
      /* use default/tight packing parameters */
      ctx->Pack = ctx->DefaultPacking;

      /* Get the uncompressed image */
      assert(srcImage->Level == texObj->Attrib.BaseLevel);
      st_GetTexSubImage(ctx,
                        0, 0, 0,
                        srcImage->Width, srcImage->Height,
                        srcImage->Depth,
                        temp_base_format, temp_datatype,
                        temp_src, srcImage);
      /* restore packing mode */
      ctx->Pack = save;
   }

   for (level = texObj->Attrib.BaseLevel; level < maxLevel; level++) {
      /* generate image[level+1] from image[level] */
      const struct gl_texture_image *srcImage;
      struct gl_texture_image *dstImage;
      GLint srcWidth, srcHeight, srcDepth;
      GLint dstWidth, dstHeight, dstDepth;
      GLint border;
      GLuint temp_dst_row_stride, temp_dst_img_stride; /* in bytes */
      GLint i;

      /* get src image parameters */
      srcImage = _mesa_select_tex_image(texObj, target, level);
      assert(srcImage);
      srcWidth = srcImage->Width;
      srcHeight = srcImage->Height;
      srcDepth = srcImage->Depth;
      border = srcImage->Border;

      /* get dest gl_texture_image */
      dstImage = _mesa_select_tex_image(texObj, target, level + 1);
      if (!dstImage) {
         break;
      }
      dstWidth = dstImage->Width;
      dstHeight = dstImage->Height;
      dstDepth = dstImage->Depth;

      /* Compute dst image strides and alloc memory on first iteration */
      temp_dst_row_stride = _mesa_format_row_stride(temp_format, dstWidth);
      temp_dst_img_stride = _mesa_format_image_size(temp_format, dstWidth,
                                                    dstHeight, 1);
      if (!temp_dst) {
         temp_dst = malloc(temp_dst_img_stride * dstDepth);
         if (!temp_dst) {
            _mesa_error(ctx, GL_OUT_OF_MEMORY, "generate mipmaps");
            goto end;
         }
      }

      /* for 2D arrays, setup array[depth] of slice pointers */
      for (i = 0; i < srcDepth; i++) {
         temp_src_slices[i] = temp_src + temp_src_img_stride * i;
      }
      for (i = 0; i < dstDepth; i++) {
         temp_dst_slices[i] = temp_dst + temp_dst_img_stride * i;
      }

      /* Rescale src image to dest image.
       * This will loop over the slices of a 2D array.
       */
      _mesa_generate_mipmap_level(target, temp_format, border,
                                  srcWidth, srcHeight, srcDepth,
                                  (const GLubyte **) temp_src_slices,
                                  temp_src_row_stride,
                                  dstWidth, dstHeight, dstDepth,
                                  temp_dst_slices, temp_dst_row_stride);

      /* The image space was allocated above so use glTexSubImage now */
      st_TexSubImage(ctx, 2, dstImage,
                     0, 0, 0, dstWidth, dstHeight, dstDepth,
                     temp_base_format, temp_datatype,
                     temp_dst, &ctx->DefaultPacking);

      /* swap src and dest pointers */
      {
         GLubyte *temp = temp_src;
         temp_src = temp_dst;
         temp_dst = temp;
         temp_src_row_stride = temp_dst_row_stride;
         temp_src_img_stride = temp_dst_img_stride;
      }
   } /* loop over mipmap levels */

end:
   free(temp_src);
   free(temp_dst);
   free(temp_src_slices);
   free(temp_dst_slices);
}

/**
 * Automatic mipmap generation.
 * This is the fallback/default function for mipmap generation.
 * Generate a complete set of mipmaps from texObj's BaseLevel image.
 * Stop at texObj's MaxLevel or when we get to the 1x1 texture.
 * For cube maps, target will be one of
 * GL_TEXTURE_CUBE_MAP_POSITIVE/NEGATIVE_X/Y/Z; never GL_TEXTURE_CUBE_MAP.
 */
void
_mesa_generate_mipmap(struct gl_context *ctx, GLenum target,
                      struct gl_texture_object *texObj)
{
   struct gl_texture_image *srcImage;
   GLint maxLevel;

   assert(texObj);
   srcImage = _mesa_select_tex_image(texObj, target, texObj->Attrib.BaseLevel);
   assert(srcImage);

   maxLevel = _mesa_max_texture_levels(ctx, texObj->Target) - 1;
   assert(maxLevel >= 0);  /* bad target */

   maxLevel = MIN2(maxLevel, texObj->Attrib.MaxLevel);

   _mesa_prepare_mipmap_levels(ctx, texObj, texObj->Attrib.BaseLevel, maxLevel);

   if (_mesa_is_format_compressed(srcImage->TexFormat)) {
      generate_mipmap_compressed(ctx, target, texObj, srcImage, maxLevel);
   } else {
      generate_mipmap_uncompressed(ctx, target, texObj, srcImage, maxLevel);
   }
}
