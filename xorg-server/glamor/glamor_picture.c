/*
 * Copyright © 2009 Intel Corporation
 * Copyright © 1998 Keith Packard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Zhigang Gong <zhigang.gong@gmail.com>
 *
 */

#include <stdlib.h>

#include "glamor_priv.h"
#include "mipict.h"

/*
 * Map picture's format to the correct gl texture format and type.
 * no_alpha is used to indicate whehter we need to wire alpha to 1.
 *
 * Although opengl support A1/GL_BITMAP, we still don't use it
 * here, it seems that mesa has bugs when uploading a A1 bitmap.
 *
 * Return 0 if find a matched texture type. Otherwise return -1.
 **/
static int
glamor_get_tex_format_type_from_pictformat_gl(ScreenPtr pScreen,
                                              PictFormatShort format,
                                              GLenum *tex_format,
                                              GLenum *tex_type,
                                              int *no_alpha,
                                              int *revert,
                                              int *swap_rb, int is_upload)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(pScreen);
    *no_alpha = 0;
    *revert = REVERT_NONE;
    *swap_rb = is_upload ? SWAP_NONE_UPLOADING : SWAP_NONE_DOWNLOADING;
    switch (format) {
    case PICT_a1:
        *tex_format = glamor_priv->one_channel_format;
        *tex_type = GL_UNSIGNED_BYTE;
        *revert = is_upload ? REVERT_UPLOADING_A1 : REVERT_DOWNLOADING_A1;
        break;
    case PICT_b8g8r8x8:
        *no_alpha = 1;
    case PICT_b8g8r8a8:
        *tex_format = GL_BGRA;
        *tex_type = GL_UNSIGNED_INT_8_8_8_8;
        break;

    case PICT_x8r8g8b8:
        *no_alpha = 1;
    case PICT_a8r8g8b8:
        *tex_format = GL_BGRA;
        *tex_type = GL_UNSIGNED_INT_8_8_8_8_REV;
        break;
    case PICT_x8b8g8r8:
        *no_alpha = 1;
    case PICT_a8b8g8r8:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_INT_8_8_8_8_REV;
        break;
    case PICT_x2r10g10b10:
        *no_alpha = 1;
    case PICT_a2r10g10b10:
        *tex_format = GL_BGRA;
        *tex_type = GL_UNSIGNED_INT_2_10_10_10_REV;
        break;
    case PICT_x2b10g10r10:
        *no_alpha = 1;
    case PICT_a2b10g10r10:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_INT_2_10_10_10_REV;
        break;

    case PICT_r5g6b5:
        *tex_format = GL_RGB;
        *tex_type = GL_UNSIGNED_SHORT_5_6_5;
        break;
    case PICT_b5g6r5:
        *tex_format = GL_RGB;
        *tex_type = GL_UNSIGNED_SHORT_5_6_5_REV;
        break;
    case PICT_x1b5g5r5:
        *no_alpha = 1;
    case PICT_a1b5g5r5:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
        break;

    case PICT_x1r5g5b5:
        *no_alpha = 1;
    case PICT_a1r5g5b5:
        *tex_format = GL_BGRA;
        *tex_type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
        break;
    case PICT_a8:
        *tex_format = glamor_priv->one_channel_format;
        *tex_type = GL_UNSIGNED_BYTE;
        break;
    case PICT_x4r4g4b4:
        *no_alpha = 1;
    case PICT_a4r4g4b4:
        *tex_format = GL_BGRA;
        *tex_type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
        break;

    case PICT_x4b4g4r4:
        *no_alpha = 1;
    case PICT_a4b4g4r4:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
        break;

    default:
        return -1;
    }
    return 0;
}

#define IS_LITTLE_ENDIAN  (IMAGE_BYTE_ORDER == LSBFirst)

static int
glamor_get_tex_format_type_from_pictformat_gles2(ScreenPtr pScreen,
                                                 PictFormatShort format,
                                                 GLenum *tex_format,
                                                 GLenum *tex_type,
                                                 int *no_alpha,
                                                 int *revert,
                                                 int *swap_rb, int is_upload)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(pScreen);
    int need_swap_rb = 0;

    *no_alpha = 0;
    *revert = IS_LITTLE_ENDIAN ? REVERT_NONE : REVERT_NORMAL;

    switch (format) {
    case PICT_b8g8r8x8:
        *no_alpha = 1;
    case PICT_b8g8r8a8:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_BYTE;
        need_swap_rb = 1;
        *revert = IS_LITTLE_ENDIAN ? REVERT_NORMAL : REVERT_NONE;
        break;

    case PICT_x8r8g8b8:
        *no_alpha = 1;
    case PICT_a8r8g8b8:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_BYTE;
        need_swap_rb = 1;
        break;

    case PICT_x8b8g8r8:
        *no_alpha = 1;
    case PICT_a8b8g8r8:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_BYTE;
        break;

    case PICT_x2r10g10b10:
        *no_alpha = 1;
    case PICT_a2r10g10b10:
        *tex_format = GL_RGBA;
        /* glReadPixmap doesn't support GL_UNSIGNED_INT_10_10_10_2.
         * we have to use GL_UNSIGNED_BYTE and do the conversion in
         * shader latter.*/
        *tex_type = GL_UNSIGNED_BYTE;
        if (is_upload == 1) {
            if (!IS_LITTLE_ENDIAN)
                *revert = REVERT_UPLOADING_10_10_10_2;
            else
                *revert = REVERT_UPLOADING_2_10_10_10;
        }
        else {
            if (!IS_LITTLE_ENDIAN) {
                *revert = REVERT_DOWNLOADING_10_10_10_2;
            }
            else {
                *revert = REVERT_DOWNLOADING_2_10_10_10;
            }
        }
        need_swap_rb = 1;

        break;

    case PICT_x2b10g10r10:
        *no_alpha = 1;
    case PICT_a2b10g10r10:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_BYTE;
        if (is_upload == 1) {
            if (!IS_LITTLE_ENDIAN)
                *revert = REVERT_UPLOADING_10_10_10_2;
            else
                *revert = REVERT_UPLOADING_2_10_10_10;
        }
        else {
            if (!IS_LITTLE_ENDIAN) {
                *revert = REVERT_DOWNLOADING_10_10_10_2;
            }
            else {
                *revert = REVERT_DOWNLOADING_2_10_10_10;
            }
        }
        break;

    case PICT_r5g6b5:
        *tex_format = GL_RGB;
        *tex_type = GL_UNSIGNED_SHORT_5_6_5;
        *revert = IS_LITTLE_ENDIAN ? REVERT_NONE : REVERT_NORMAL;

        break;

    case PICT_b5g6r5:
        *tex_format = GL_RGB;
        *tex_type = GL_UNSIGNED_SHORT_5_6_5;
        need_swap_rb = IS_LITTLE_ENDIAN ? 1 : 0;
        break;

    case PICT_x1b5g5r5:
        *no_alpha = 1;
    case PICT_a1b5g5r5:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_SHORT_5_5_5_1;
        if (IS_LITTLE_ENDIAN) {
            *revert =
                is_upload ? REVERT_UPLOADING_1_5_5_5 :
                REVERT_DOWNLOADING_1_5_5_5;
        }
        else
            *revert = REVERT_NONE;
        break;

    case PICT_x1r5g5b5:
        *no_alpha = 1;
    case PICT_a1r5g5b5:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_SHORT_5_5_5_1;
        if (IS_LITTLE_ENDIAN) {
            *revert =
                is_upload ? REVERT_UPLOADING_1_5_5_5 :
                REVERT_DOWNLOADING_1_5_5_5;
        }
        else
            *revert = REVERT_NONE;
        need_swap_rb = 1;
        break;

    case PICT_a1:
        *tex_format = glamor_priv->one_channel_format;
        *tex_type = GL_UNSIGNED_BYTE;
        *revert = is_upload ? REVERT_UPLOADING_A1 : REVERT_DOWNLOADING_A1;
        break;

    case PICT_a8:
        *tex_format = glamor_priv->one_channel_format;
        *tex_type = GL_UNSIGNED_BYTE;
        *revert = REVERT_NONE;
        break;

    case PICT_x4r4g4b4:
        *no_alpha = 1;
    case PICT_a4r4g4b4:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_SHORT_4_4_4_4;
        *revert = IS_LITTLE_ENDIAN ? REVERT_NORMAL : REVERT_NONE;
        need_swap_rb = 1;
        break;

    case PICT_x4b4g4r4:
        *no_alpha = 1;
    case PICT_a4b4g4r4:
        *tex_format = GL_RGBA;
        *tex_type = GL_UNSIGNED_SHORT_4_4_4_4;
        *revert = IS_LITTLE_ENDIAN ? REVERT_NORMAL : REVERT_NONE;
        break;

    default:
        LogMessageVerb(X_INFO, 0,
                       "fail to get matched format for %x \n", format);
        return -1;
    }

    if (need_swap_rb)
        *swap_rb = is_upload ? SWAP_UPLOADING : SWAP_DOWNLOADING;
    else
        *swap_rb = is_upload ? SWAP_NONE_UPLOADING : SWAP_NONE_DOWNLOADING;
    return 0;
}

static int
glamor_get_tex_format_type_from_pixmap(PixmapPtr pixmap,
                                       PictFormatShort pict_format,
                                       GLenum *format,
                                       GLenum *type,
                                       int *no_alpha,
                                       int *revert, int *swap_rb, int is_upload)
{
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);

    if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP) {
        return glamor_get_tex_format_type_from_pictformat_gl(pixmap->drawable.pScreen,
                                                             pict_format,
                                                             format, type,
                                                             no_alpha,
                                                             revert,
                                                             swap_rb,
                                                             is_upload);
    } else {
        return glamor_get_tex_format_type_from_pictformat_gles2(pixmap->drawable.pScreen,
                                                                pict_format,
                                                                format, type,
                                                                no_alpha,
                                                                revert,
                                                                swap_rb,
                                                                is_upload);
    }
}

static void *
_glamor_color_convert_a1_a8(void *src_bits, void *dst_bits, int w, int h,
                            int stride, int revert)
{
    PictFormatShort dst_format, src_format;
    pixman_image_t *dst_image;
    pixman_image_t *src_image;
    int src_stride;

    if (revert == REVERT_UPLOADING_A1) {
        src_format = PICT_a1;
        dst_format = PICT_a8;
        src_stride = PixmapBytePad(w, 1);
    }
    else {
        dst_format = PICT_a1;
        src_format = PICT_a8;
        src_stride = (((w * 8 + 7) / 8) + 3) & ~3;
    }

    dst_image = pixman_image_create_bits(dst_format, w, h, dst_bits, stride);
    if (dst_image == NULL) {
        return NULL;
    }

    src_image = pixman_image_create_bits(src_format,
                                         w, h, src_bits, src_stride);

    if (src_image == NULL) {
        pixman_image_unref(dst_image);
        return NULL;
    }

    pixman_image_composite(PictOpSrc, src_image, NULL, dst_image,
                           0, 0, 0, 0, 0, 0, w, h);

    pixman_image_unref(src_image);
    pixman_image_unref(dst_image);
    return dst_bits;
}

#define ADJUST_BITS(d, src_bits, dst_bits)	(((dst_bits) == (src_bits)) ? (d) : 				\
							(((dst_bits) > (src_bits)) ? 				\
							  (((d) << ((dst_bits) - (src_bits))) 			\
								   + (( 1 << ((dst_bits) - (src_bits))) >> 1))	\
								:  ((d) >> ((src_bits) - (dst_bits)))))

#define GLAMOR_DO_CONVERT(src, dst, no_alpha, swap,		\
			  a_shift_src, a_bits_src,		\
			  b_shift_src, b_bits_src,		\
			  g_shift_src, g_bits_src,		\
			  r_shift_src, r_bits_src,		\
			  a_shift, a_bits,			\
			  b_shift, b_bits,			\
			  g_shift, g_bits,			\
			  r_shift, r_bits)			\
	do {								\
		typeof(src) a,b,g,r;					\
		typeof(src) a_mask_src, b_mask_src, g_mask_src, r_mask_src;\
		a_mask_src = (((1 << (a_bits_src)) - 1) << a_shift_src);\
		b_mask_src = (((1 << (b_bits_src)) - 1) << b_shift_src);\
		g_mask_src = (((1 << (g_bits_src)) - 1) << g_shift_src);\
		r_mask_src = (((1 << (r_bits_src)) - 1) << r_shift_src);\
		if (no_alpha)						\
			a = (a_mask_src) >> (a_shift_src);			\
		else							\
			a = ((src) & (a_mask_src)) >> (a_shift_src);	\
		b = ((src) & (b_mask_src)) >> (b_shift_src);		\
		g = ((src) & (g_mask_src)) >> (g_shift_src);		\
		r = ((src) & (r_mask_src)) >> (r_shift_src);		\
		a = ADJUST_BITS(a, a_bits_src, a_bits);			\
		b = ADJUST_BITS(b, b_bits_src, b_bits);			\
		g = ADJUST_BITS(g, g_bits_src, g_bits);			\
		r = ADJUST_BITS(r, r_bits_src, r_bits);			\
		if (swap == 0)						\
			(*dst) = ((a) << (a_shift)) | ((b) << (b_shift)) | ((g) << (g_shift)) | ((r) << (r_shift)); \
		else 												    \
			(*dst) = ((a) << (a_shift)) | ((r) << (b_shift)) | ((g) << (g_shift)) | ((b) << (r_shift)); \
	} while (0)

static void *
_glamor_color_revert_x2b10g10r10(void *src_bits, void *dst_bits, int w, int h,
                                 int stride, int no_alpha, int revert,
                                 int swap_rb)
{
    int x, y;
    unsigned int *words, *saved_words, *source_words;
    int swap = !(swap_rb == SWAP_NONE_DOWNLOADING ||
                 swap_rb == SWAP_NONE_UPLOADING);

    source_words = src_bits;
    words = dst_bits;
    saved_words = words;

    for (y = 0; y < h; y++) {
        DEBUGF("Line %d :  ", y);
        for (x = 0; x < w; x++) {
            unsigned int pixel = source_words[x];

            if (revert == REVERT_DOWNLOADING_2_10_10_10)
                GLAMOR_DO_CONVERT(pixel, &words[x], no_alpha, swap,
                                  24, 8, 16, 8, 8, 8, 0, 8,
                                  30, 2, 20, 10, 10, 10, 0, 10);
            else
                GLAMOR_DO_CONVERT(pixel, &words[x], no_alpha, swap,
                                  30, 2, 20, 10, 10, 10, 0, 10,
                                  24, 8, 16, 8, 8, 8, 0, 8);
            DEBUGF("%x:%x ", pixel, words[x]);
        }
        DEBUGF("\n");
        words += stride / sizeof(*words);
        source_words += stride / sizeof(*words);
    }
    DEBUGF("\n");
    return saved_words;

}

static void *
_glamor_color_revert_x1b5g5r5(void *src_bits, void *dst_bits, int w, int h,
                              int stride, int no_alpha, int revert, int swap_rb)
{
    int x, y;
    unsigned short *words, *saved_words, *source_words;
    int swap = !(swap_rb == SWAP_NONE_DOWNLOADING ||
                 swap_rb == SWAP_NONE_UPLOADING);

    words = dst_bits;
    source_words = src_bits;
    saved_words = words;

    for (y = 0; y < h; y++) {
        DEBUGF("Line %d :  ", y);
        for (x = 0; x < w; x++) {
            unsigned short pixel = source_words[x];

            if (revert == REVERT_DOWNLOADING_1_5_5_5)
                GLAMOR_DO_CONVERT(pixel, &words[x], no_alpha, swap,
                                  0, 1, 1, 5, 6, 5, 11, 5,
                                  15, 1, 10, 5, 5, 5, 0, 5);
            else
                GLAMOR_DO_CONVERT(pixel, &words[x], no_alpha, swap,
                                  15, 1, 10, 5, 5, 5, 0, 5,
                                  0, 1, 1, 5, 6, 5, 11, 5);
            DEBUGF("%04x:%04x ", pixel, words[x]);
        }
        DEBUGF("\n");
        words += stride / sizeof(*words);
        source_words += stride / sizeof(*words);
    }
    DEBUGF("\n");
    return saved_words;
}

/*
 * This function is to convert an unsupported color format to/from a
 * supported GL format.
 * Here are the current scenarios:
 *
 * @no_alpha:
 * 	If it is set, then we need to wire the alpha value to 1.
 * @revert:
	REVERT_DOWNLOADING_A1		: convert an Alpha8 buffer to a A1 buffer.
	REVERT_UPLOADING_A1		: convert an A1 buffer to an Alpha8 buffer
	REVERT_DOWNLOADING_2_10_10_10 	: convert r10G10b10X2 to X2B10G10R10
	REVERT_UPLOADING_2_10_10_10 	: convert X2B10G10R10 to R10G10B10X2
	REVERT_DOWNLOADING_1_5_5_5  	: convert B5G5R5X1 to X1R5G5B5
	REVERT_UPLOADING_1_5_5_5    	: convert X1R5G5B5 to B5G5R5X1
   @swap_rb: if we have the swap_rb set, then we need to swap the R and B's position.
 *
 */

static void *
glamor_color_convert_to_bits(void *src_bits, void *dst_bits, int w, int h,
                             int stride, int no_alpha, int revert, int swap_rb)
{
    if (revert == REVERT_DOWNLOADING_A1 || revert == REVERT_UPLOADING_A1) {
        return _glamor_color_convert_a1_a8(src_bits, dst_bits, w, h, stride,
                                           revert);
    }
    else if (revert == REVERT_DOWNLOADING_2_10_10_10 ||
             revert == REVERT_UPLOADING_2_10_10_10) {
        return _glamor_color_revert_x2b10g10r10(src_bits, dst_bits, w, h,
                                                stride, no_alpha, revert,
                                                swap_rb);
    }
    else if (revert == REVERT_DOWNLOADING_1_5_5_5 ||
             revert == REVERT_UPLOADING_1_5_5_5) {
        return _glamor_color_revert_x1b5g5r5(src_bits, dst_bits, w, h, stride,
                                             no_alpha, revert, swap_rb);
    }
    else
        ErrorF("convert a non-supported mode %x.\n", revert);

    return NULL;
}

/**
 * Upload pixmap to a specified texture.
 * This texture may not be the one attached to it.
 **/
static Bool
__glamor_upload_pixmap_to_texture(PixmapPtr pixmap, unsigned int *tex,
                                  GLenum format,
                                  GLenum type,
                                  int x, int y, int w, int h,
                                  void *bits, int pbo)
{
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);
    int non_sub = 0;
    unsigned int iformat = 0;

    glamor_make_current(glamor_priv);
    if (*tex == 0) {
        glGenTextures(1, tex);
        if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP)
            iformat = gl_iformat_for_pixmap(pixmap);
        else
            iformat = format;
        non_sub = 1;
        assert(x == 0 && y == 0);
    }

    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    assert(pbo || bits != 0);
    if (bits == NULL) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    glamor_priv->suppress_gl_out_of_memory_logging = true;
    if (non_sub)
        glTexImage2D(GL_TEXTURE_2D, 0, iformat, w, h, 0, format, type, bits);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, format, type, bits);
    glamor_priv->suppress_gl_out_of_memory_logging = false;
    if (glGetError() == GL_OUT_OF_MEMORY) {
        if (non_sub) {
            glDeleteTextures(1, tex);
            *tex = 0;
        }
        return FALSE;
    }

    if (bits == NULL)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    return TRUE;
}

static Bool
_glamor_upload_bits_to_pixmap_texture(PixmapPtr pixmap, GLenum format,
                                      GLenum type, int no_alpha, int revert,
                                      int swap_rb, int x, int y, int w, int h,
                                      int stride, void *bits, int pbo)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);
    float dst_xscale, dst_yscale;
    GLuint tex = 0;
    int need_free_bits = 0;

    if (bits == NULL)
        goto ready_to_upload;

    if (revert > REVERT_NORMAL) {
        /* XXX if we are restoring the pixmap, then we may not need to allocate
         * new buffer */
        void *converted_bits;

        if (pixmap->drawable.depth == 1)
            stride = (((w * 8 + 7) / 8) + 3) & ~3;

        converted_bits = xallocarray(h, stride);

        if (converted_bits == NULL)
            return FALSE;
        bits = glamor_color_convert_to_bits(bits, converted_bits, w, h,
                                            stride, no_alpha, revert, swap_rb);
        if (bits == NULL) {
            free(converted_bits);
            ErrorF("Failed to convert pixmap no_alpha %d,"
                   "revert mode %d, swap mode %d\n", no_alpha, revert, swap_rb);
            return FALSE;
        }
        no_alpha = 0;
        revert = REVERT_NONE;
        swap_rb = SWAP_NONE_UPLOADING;
        need_free_bits = TRUE;
    }

 ready_to_upload:

    /* Try fast path firstly, upload the pixmap to the texture attached
     * to the fbo directly. */
    if (no_alpha == 0
        && revert == REVERT_NONE && swap_rb == SWAP_NONE_UPLOADING
#ifdef WALKAROUND_LARGE_TEXTURE_MAP
        && glamor_pixmap_priv_is_small(pixmap_priv)
#endif
        ) {
        int fbo_x_off, fbo_y_off;

        assert(pixmap_priv->fbo->tex);
        pixmap_priv_get_fbo_off(pixmap_priv, &fbo_x_off, &fbo_y_off);

        assert(x + fbo_x_off >= 0 && y + fbo_y_off >= 0);
        assert(x + fbo_x_off + w <= pixmap_priv->fbo->width);
        assert(y + fbo_y_off + h <= pixmap_priv->fbo->height);
        if (!__glamor_upload_pixmap_to_texture(pixmap, &pixmap_priv->fbo->tex,
                                               format, type,
                                               x + fbo_x_off, y + fbo_y_off,
                                               w, h,
                                               bits, pbo)) {
            if (need_free_bits)
                free(bits);
            return FALSE;
        }
    } else {
        static const float texcoords_inv[8] = { 0, 0,
                                                1, 0,
                                                1, 1,
                                                0, 1
        };
        GLfloat *v;
        char *vbo_offset;

        v = glamor_get_vbo_space(screen, 16 * sizeof(GLfloat), &vbo_offset);

        pixmap_priv_get_dest_scale(pixmap, pixmap_priv, &dst_xscale, &dst_yscale);
        glamor_set_normalize_vcoords(pixmap_priv, dst_xscale,
                                     dst_yscale,
                                     x, y,
                                     x + w, y + h,
                                     v);
        /* Slow path, we need to flip y or wire alpha to 1. */
        glamor_make_current(glamor_priv);

        if (!__glamor_upload_pixmap_to_texture(pixmap, &tex,
                                               format, type, 0, 0, w, h, bits,
                                               pbo)) {
            if (need_free_bits)
                free(bits);
            return FALSE;
        }

        memcpy(&v[8], texcoords_inv, 8 * sizeof(GLfloat));

        glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT,
                              GL_FALSE, 2 * sizeof(float), vbo_offset);
        glEnableVertexAttribArray(GLAMOR_VERTEX_POS);
        glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2, GL_FLOAT,
                              GL_FALSE, 2 * sizeof(float), vbo_offset + 8 * sizeof(GLfloat));
        glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);

        glamor_put_vbo_space(screen);
        glamor_set_destination_pixmap_priv_nc(glamor_priv, pixmap, pixmap_priv);
        glamor_set_alu(screen, GXcopy);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glUseProgram(glamor_priv->finish_access_prog[no_alpha]);
        glUniform1i(glamor_priv->finish_access_revert[no_alpha], revert);
        glUniform1i(glamor_priv->finish_access_swap_rb[no_alpha], swap_rb);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
        glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
        glDeleteTextures(1, &tex);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (need_free_bits)
        free(bits);
    return TRUE;
}

/*
 * Prepare to upload a pixmap to texture memory.
 * no_alpha equals 1 means the format needs to wire alpha to 1.
 */
static int
glamor_pixmap_upload_prepare(PixmapPtr pixmap, GLenum format, int no_alpha,
                             int revert, int swap_rb)
{
    int flag = 0;
    glamor_pixmap_private *pixmap_priv;
    glamor_screen_private *glamor_priv;
    glamor_pixmap_fbo *fbo;
    GLenum iformat;

    pixmap_priv = glamor_get_pixmap_private(pixmap);
    glamor_priv = glamor_get_screen_private(pixmap->drawable.pScreen);

    if (pixmap_priv->gl_fbo != GLAMOR_FBO_UNATTACHED)
        return 0;

    if (pixmap_priv->fbo
        && (pixmap_priv->fbo->width < pixmap->drawable.width
            || pixmap_priv->fbo->height < pixmap->drawable.height)) {
        fbo = glamor_pixmap_detach_fbo(pixmap_priv);
        glamor_destroy_fbo(glamor_priv, fbo);
    }

    if (pixmap_priv->fbo && pixmap_priv->fbo->fb)
        return 0;

    if (!(no_alpha || (revert == REVERT_NORMAL)
          || (swap_rb != SWAP_NONE_UPLOADING))) {
        /* We don't need a fbo, a simple texture uploading should work. */

        flag = GLAMOR_CREATE_FBO_NO_FBO;
    }

    if ((flag == GLAMOR_CREATE_FBO_NO_FBO
         && pixmap_priv->fbo && pixmap_priv->fbo->tex))
        return 0;

    if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP)
        iformat = gl_iformat_for_pixmap(pixmap);
    else
        iformat = format;

    if (!glamor_pixmap_ensure_fbo(pixmap, iformat, flag))
        return -1;

    return 0;
}

/*
 * upload sub region to a large region.
 * */
static void
glamor_put_bits(char *dst_bits, int dst_stride, char *src_bits,
                int src_stride, int bpp, int x, int y, int w, int h)
{
    int j;
    int byte_per_pixel;

    byte_per_pixel = bpp / 8;
    src_bits += y * src_stride + (x * byte_per_pixel);

    for (j = y; j < y + h; j++) {
        memcpy(dst_bits, src_bits, w * byte_per_pixel);
        src_bits += src_stride;
        dst_bits += dst_stride;
    }
}

static Bool
glamor_upload_sub_pixmap_to_texture(PixmapPtr pixmap, int x, int y, int w,
                                    int h, int stride, void *bits, int pbo,
                                    PictFormatShort pict_format)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    GLenum format, type;
    int no_alpha, revert, swap_rb;
    glamor_pixmap_private *pixmap_priv;
    Bool force_clip;

    if (glamor_get_tex_format_type_from_pixmap(pixmap,
                                               pict_format,
                                               &format,
                                               &type,
                                               &no_alpha,
                                               &revert, &swap_rb, 1)) {
        glamor_fallback("Unknown pixmap depth %d.\n", pixmap->drawable.depth);
        return FALSE;
    }
    if (glamor_pixmap_upload_prepare(pixmap, format, no_alpha, revert, swap_rb))
        return FALSE;

    pixmap_priv = glamor_get_pixmap_private(pixmap);
    force_clip = glamor_priv->gl_flavor != GLAMOR_GL_DESKTOP
        && !glamor_check_fbo_size(glamor_priv, w, h);

    if (glamor_pixmap_priv_is_large(pixmap_priv) || force_clip) {
        RegionRec region;
        BoxRec box;
        int n_region;
        glamor_pixmap_clipped_regions *clipped_regions;
        void *sub_bits;
        int i, j;

        sub_bits = xallocarray(h, stride);
        if (sub_bits == NULL)
            return FALSE;
        box.x1 = x;
        box.y1 = y;
        box.x2 = x + w;
        box.y2 = y + h;
        RegionInitBoxes(&region, &box, 1);
        if (!force_clip)
            clipped_regions =
                glamor_compute_clipped_regions(pixmap, &region, &n_region,
                                               0, 0, 0);
        else
            clipped_regions =
                glamor_compute_clipped_regions_ext(pixmap, &region,
                                                   &n_region,
                                                   pixmap_priv->block_w,
                                                   pixmap_priv->block_h,
                                                   0,
                                                   0);
        DEBUGF("prepare upload %dx%d to a large pixmap %p\n", w, h, pixmap);
        for (i = 0; i < n_region; i++) {
            BoxPtr boxes;
            int nbox;
            int temp_stride;
            void *temp_bits;

            assert(pbo == 0);

            glamor_set_pixmap_fbo_current(pixmap_priv, clipped_regions[i].block_idx);

            boxes = RegionRects(clipped_regions[i].region);
            nbox = RegionNumRects(clipped_regions[i].region);
            DEBUGF("split to %d boxes\n", nbox);
            for (j = 0; j < nbox; j++) {
                temp_stride = PixmapBytePad(boxes[j].x2 - boxes[j].x1,
                                            pixmap->drawable.depth);

                if (boxes[j].x1 == x && temp_stride == stride) {
                    temp_bits = (char *) bits + (boxes[j].y1 - y) * stride;
                }
                else {
                    temp_bits = sub_bits;
                    glamor_put_bits(temp_bits, temp_stride, bits, stride,
                                    pixmap->drawable.bitsPerPixel,
                                    boxes[j].x1 - x, boxes[j].y1 - y,
                                    boxes[j].x2 - boxes[j].x1,
                                    boxes[j].y2 - boxes[j].y1);
                }
                DEBUGF("upload x %d y %d w %d h %d temp stride %d \n",
                       boxes[j].x1 - x, boxes[j].y1 - y,
                       boxes[j].x2 - boxes[j].x1,
                       boxes[j].y2 - boxes[j].y1, temp_stride);
                if (_glamor_upload_bits_to_pixmap_texture
                    (pixmap, format, type, no_alpha, revert, swap_rb,
                     boxes[j].x1, boxes[j].y1, boxes[j].x2 - boxes[j].x1,
                     boxes[j].y2 - boxes[j].y1, temp_stride, temp_bits,
                     pbo) == FALSE) {
                    RegionUninit(&region);
                    free(sub_bits);
                    assert(0);
                    return FALSE;
                }
            }
            RegionDestroy(clipped_regions[i].region);
        }
        free(sub_bits);
        free(clipped_regions);
        RegionUninit(&region);
        return TRUE;
    }
    else
        return _glamor_upload_bits_to_pixmap_texture(pixmap, format, type,
                                                     no_alpha, revert, swap_rb,
                                                     x, y, w, h, stride, bits,
                                                     pbo);
}

/* Upload picture to texture.  We may need to flip the y axis or
 * wire alpha to 1. So we may conditional create fbo for the picture.
 * */
enum glamor_pixmap_status
glamor_upload_picture_to_texture(PicturePtr picture)
{
    PixmapPtr pixmap;

    assert(picture->pDrawable);
    pixmap = glamor_get_drawable_pixmap(picture->pDrawable);

    if (glamor_upload_sub_pixmap_to_texture(pixmap, 0, 0,
                                            pixmap->drawable.width,
                                            pixmap->drawable.height,
                                            pixmap->devKind,
                                            pixmap->devPrivate.ptr, 0,
                                            picture->format))
        return GLAMOR_UPLOAD_DONE;
    else
        return GLAMOR_UPLOAD_FAILED;
}
