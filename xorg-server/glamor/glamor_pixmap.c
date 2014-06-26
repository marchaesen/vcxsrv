/*
 * Copyright © 2001 Keith Packard
 * Copyright © 2008 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 *
 */

#include <stdlib.h>

#include "glamor_priv.h"
/**
 * Sets the offsets to add to coordinates to make them address the same bits in
 * the backing drawable. These coordinates are nonzero only for redirected
 * windows.
 */
void
glamor_get_drawable_deltas(DrawablePtr drawable, PixmapPtr pixmap,
                           int *x, int *y)
{
#ifdef COMPOSITE
    if (drawable->type == DRAWABLE_WINDOW) {
        *x = -pixmap->screen_x;
        *y = -pixmap->screen_y;
        return;
    }
#endif

    *x = 0;
    *y = 0;
}

void
glamor_pixmap_init(ScreenPtr screen)
{

}

void
glamor_pixmap_fini(ScreenPtr screen)
{
}

void
glamor_set_destination_pixmap_fbo(glamor_pixmap_fbo *fbo, int x0, int y0,
                                  int width, int height)
{
    glamor_make_current(fbo->glamor_priv);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo->fb);
    glViewport(x0, y0, width, height);
}

void
glamor_set_destination_pixmap_priv_nc(glamor_pixmap_private *pixmap_priv)
{
    int w, h;

    PIXMAP_PRIV_GET_ACTUAL_SIZE(pixmap_priv, w, h);
    glamor_set_destination_pixmap_fbo(pixmap_priv->base.fbo, 0, 0, w, h);
}

int
glamor_set_destination_pixmap_priv(glamor_pixmap_private *pixmap_priv)
{
    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return -1;

    glamor_set_destination_pixmap_priv_nc(pixmap_priv);
    return 0;
}

int
glamor_set_destination_pixmap(PixmapPtr pixmap)
{
    int err;
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);

    err = glamor_set_destination_pixmap_priv(pixmap_priv);
    return err;
}

Bool
glamor_set_planemask(PixmapPtr pixmap, unsigned long planemask)
{
    if (glamor_pm_is_solid(&pixmap->drawable, planemask)) {
        return GL_TRUE;
    }

    glamor_fallback("unsupported planemask %lx\n", planemask);
    return GL_FALSE;
}

Bool
glamor_set_alu(ScreenPtr screen, unsigned char alu)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    if (glamor_priv->gl_flavor == GLAMOR_GL_ES2) {
        if (alu != GXcopy)
            return FALSE;
        else
            return TRUE;
    }

    if (alu == GXcopy) {
        glDisable(GL_COLOR_LOGIC_OP);
        return TRUE;
    }
    glEnable(GL_COLOR_LOGIC_OP);
    switch (alu) {
    case GXclear:
        glLogicOp(GL_CLEAR);
        break;
    case GXand:
        glLogicOp(GL_AND);
        break;
    case GXandReverse:
        glLogicOp(GL_AND_REVERSE);
        break;
    case GXandInverted:
        glLogicOp(GL_AND_INVERTED);
        break;
    case GXnoop:
        glLogicOp(GL_NOOP);
        break;
    case GXxor:
        glLogicOp(GL_XOR);
        break;
    case GXor:
        glLogicOp(GL_OR);
        break;
    case GXnor:
        glLogicOp(GL_NOR);
        break;
    case GXequiv:
        glLogicOp(GL_EQUIV);
        break;
    case GXinvert:
        glLogicOp(GL_INVERT);
        break;
    case GXorReverse:
        glLogicOp(GL_OR_REVERSE);
        break;
    case GXcopyInverted:
        glLogicOp(GL_COPY_INVERTED);
        break;
    case GXorInverted:
        glLogicOp(GL_OR_INVERTED);
        break;
    case GXnand:
        glLogicOp(GL_NAND);
        break;
    case GXset:
        glLogicOp(GL_SET);
        break;
    default:
        glamor_fallback("unsupported alu %x\n", alu);
        return FALSE;
    }

    return TRUE;
}

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
glamor_get_tex_format_type_from_pictformat_gl(PictFormatShort format,
                                              GLenum *tex_format,
                                              GLenum *tex_type,
                                              int *no_alpha,
                                              int *revert,
                                              int *swap_rb, int is_upload)
{
    *no_alpha = 0;
    *revert = REVERT_NONE;
    *swap_rb = is_upload ? SWAP_NONE_UPLOADING : SWAP_NONE_DOWNLOADING;
    switch (format) {
    case PICT_a1:
        *tex_format = GL_ALPHA;
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
        *tex_format = GL_ALPHA;
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
glamor_get_tex_format_type_from_pictformat_gles2(PictFormatShort format,
                                                 GLenum *tex_format,
                                                 GLenum *tex_type,
                                                 int *no_alpha,
                                                 int *revert,
                                                 int *swap_rb, int is_upload)
{
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
        need_swap_rb = IS_LITTLE_ENDIAN ? 1 : 0;;
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
        *tex_format = GL_ALPHA;
        *tex_type = GL_UNSIGNED_BYTE;
        *revert = is_upload ? REVERT_UPLOADING_A1 : REVERT_DOWNLOADING_A1;
        break;

    case PICT_a8:
        *tex_format = GL_ALPHA;
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
                                       GLenum *format,
                                       GLenum *type,
                                       int *no_alpha,
                                       int *revert, int *swap_rb, int is_upload)
{
    glamor_pixmap_private *pixmap_priv;
    PictFormatShort pict_format;
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);

    pixmap_priv = glamor_get_pixmap_private(pixmap);
    if (GLAMOR_PIXMAP_PRIV_IS_PICTURE(pixmap_priv))
        pict_format = pixmap_priv->base.picture->format;
    else
        pict_format = format_for_depth(pixmap->drawable.depth);

    if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP) {
        return glamor_get_tex_format_type_from_pictformat_gl(pict_format,
                                                             format, type,
                                                             no_alpha,
                                                             revert,
                                                             swap_rb,
                                                             is_upload);
    } else {
        return glamor_get_tex_format_type_from_pictformat_gles2(pict_format,
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
static void
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    assert(pbo || bits != 0);
    if (bits == NULL) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    if (non_sub)
        glTexImage2D(GL_TEXTURE_2D, 0, iformat, w, h, 0, format, type, bits);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, format, type, bits);

    if (bits == NULL)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

static Bool
_glamor_upload_bits_to_pixmap_texture(PixmapPtr pixmap, GLenum format,
                                      GLenum type, int no_alpha, int revert,
                                      int swap_rb, int x, int y, int w, int h,
                                      int stride, void *bits, int pbo)
{
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);
    static float vertices[8];

    static float texcoords[8] = { 0, 1,
        1, 1,
        1, 0,
        0, 0
    };
    static float texcoords_inv[8] = { 0, 0,
        1, 0,
        1, 1,
        0, 1
    };
    float *ptexcoords;
    float dst_xscale, dst_yscale;
    GLuint tex = 0;
    int need_flip;
    int need_free_bits = 0;

    need_flip = !glamor_priv->yInverted;

    if (bits == NULL)
        goto ready_to_upload;

    if (revert > REVERT_NORMAL) {
        /* XXX if we are restoring the pixmap, then we may not need to allocate
         * new buffer */
        void *converted_bits;

        if (pixmap->drawable.depth == 1)
            stride = (((w * 8 + 7) / 8) + 3) & ~3;

        converted_bits = malloc(h * stride);

        if (converted_bits == NULL)
            return FALSE;
        bits = glamor_color_convert_to_bits(bits, converted_bits, w, h,
                                            stride, no_alpha, revert, swap_rb);
        if (bits == NULL) {
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
        && revert == REVERT_NONE && swap_rb == SWAP_NONE_UPLOADING && !need_flip
#ifdef WALKAROUND_LARGE_TEXTURE_MAP
        && pixmap_priv->type != GLAMOR_TEXTURE_LARGE
#endif
        ) {
        int fbo_x_off, fbo_y_off;

        assert(pixmap_priv->base.fbo->tex);
        pixmap_priv_get_fbo_off(pixmap_priv, &fbo_x_off, &fbo_y_off);

        assert(x + fbo_x_off >= 0 && y + fbo_y_off >= 0);
        assert(x + fbo_x_off + w <= pixmap_priv->base.fbo->width);
        assert(y + fbo_y_off + h <= pixmap_priv->base.fbo->height);
        __glamor_upload_pixmap_to_texture(pixmap, &pixmap_priv->base.fbo->tex,
                                          format, type,
                                          x + fbo_x_off, y + fbo_y_off, w, h,
                                          bits, pbo);
        return TRUE;
    }

    if (need_flip)
        ptexcoords = texcoords;
    else
        ptexcoords = texcoords_inv;

    pixmap_priv_get_dest_scale(pixmap_priv, &dst_xscale, &dst_yscale);
    glamor_set_normalize_vcoords(pixmap_priv, dst_xscale,
                                 dst_yscale,
                                 x, y,
                                 x + w, y + h,
                                 glamor_priv->yInverted, vertices);
    /* Slow path, we need to flip y or wire alpha to 1. */
    glamor_make_current(glamor_priv);
    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT,
                          GL_FALSE, 2 * sizeof(float), vertices);
    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);
    glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2, GL_FLOAT,
                          GL_FALSE, 2 * sizeof(float), ptexcoords);
    glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);

    glamor_set_destination_pixmap_priv_nc(pixmap_priv);
    __glamor_upload_pixmap_to_texture(pixmap, &tex,
                                      format, type, 0, 0, w, h, bits, pbo);
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

    if (need_free_bits)
        free(bits);
    return TRUE;
}

/*
 * Prepare to upload a pixmap to texture memory.
 * no_alpha equals 1 means the format needs to wire alpha to 1.
 * Two condtion need to setup a fbo for a pixmap
 * 1. !yInverted, we need to do flip if we are not yInverted.
 * 2. no_alpha != 0, we need to wire the alpha.
 * */
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

    if (pixmap_priv->base.gl_fbo != GLAMOR_FBO_UNATTACHED)
        return 0;

    if (pixmap_priv->base.fbo
        && (pixmap_priv->base.fbo->width < pixmap->drawable.width
            || pixmap_priv->base.fbo->height < pixmap->drawable.height)) {
        fbo = glamor_pixmap_detach_fbo(pixmap_priv);
        glamor_destroy_fbo(fbo);
    }

    if (pixmap_priv->base.fbo && pixmap_priv->base.fbo->fb)
        return 0;

    if (!(no_alpha || (revert == REVERT_NORMAL)
          || (swap_rb != SWAP_NONE_UPLOADING)
          || !glamor_priv->yInverted)) {
        /* We don't need a fbo, a simple texture uploading should work. */

        flag = GLAMOR_CREATE_FBO_NO_FBO;
    }

    if ((flag == GLAMOR_CREATE_FBO_NO_FBO
         && pixmap_priv->base.fbo && pixmap_priv->base.fbo->tex)
        || (flag == 0 && pixmap_priv->base.fbo && pixmap_priv->base.fbo->fb))
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

/*
 * download sub region from a large region.
 */
static void
glamor_get_bits(char *dst_bits, int dst_stride, char *src_bits,
                int src_stride, int bpp, int x, int y, int w, int h)
{
    int j;
    int byte_per_pixel;

    byte_per_pixel = bpp / 8;
    dst_bits += y * dst_stride + x * byte_per_pixel;

    for (j = y; j < y + h; j++) {
        memcpy(dst_bits, src_bits, w * byte_per_pixel);
        src_bits += src_stride;
        dst_bits += dst_stride;
    }
}

Bool
glamor_upload_sub_pixmap_to_texture(PixmapPtr pixmap, int x, int y, int w,
                                    int h, int stride, void *bits, int pbo)
{
    GLenum format, type;
    int no_alpha, revert, swap_rb;
    glamor_pixmap_private *pixmap_priv;
    Bool force_clip;

    if (glamor_get_tex_format_type_from_pixmap(pixmap,
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
    force_clip = pixmap_priv->base.glamor_priv->gl_flavor != GLAMOR_GL_DESKTOP
        && !glamor_check_fbo_size(pixmap_priv->base.glamor_priv, w, h);

    if (pixmap_priv->type == GLAMOR_TEXTURE_LARGE || force_clip) {
        RegionRec region;
        BoxRec box;
        int n_region;
        glamor_pixmap_clipped_regions *clipped_regions;
        void *sub_bits;
        int i, j;

        sub_bits = malloc(h * stride);
        if (sub_bits == NULL)
            return FALSE;
        box.x1 = x;
        box.y1 = y;
        box.x2 = x + w;
        box.y2 = y + h;
        RegionInitBoxes(&region, &box, 1);
        if (!force_clip)
            clipped_regions =
                glamor_compute_clipped_regions(pixmap_priv, &region, &n_region,
                                               0, 0, 0);
        else
            clipped_regions =
                glamor_compute_clipped_regions_ext(pixmap_priv, &region,
                                                   &n_region,
                                                   pixmap_priv->large.block_w,
                                                   pixmap_priv->large.block_h,
                                                   0,
                                                   0);
        DEBUGF("prepare upload %dx%d to a large pixmap %p\n", w, h, pixmap);
        for (i = 0; i < n_region; i++) {
            BoxPtr boxes;
            int nbox;
            int temp_stride;
            void *temp_bits;

            assert(pbo == 0);

            SET_PIXMAP_FBO_CURRENT(pixmap_priv, clipped_regions[i].block_idx);

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

enum glamor_pixmap_status
glamor_upload_pixmap_to_texture(PixmapPtr pixmap)
{
    glamor_pixmap_private *pixmap_priv;
    void *data;
    int pbo;
    int ret;

    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if ((pixmap_priv->base.fbo)
        && (pixmap_priv->base.fbo->pbo_valid)) {
        data = NULL;
        pbo = pixmap_priv->base.fbo->pbo;
    }
    else {
        data = pixmap->devPrivate.ptr;
        pbo = 0;
    }

    if (glamor_upload_sub_pixmap_to_texture(pixmap, 0, 0,
                                            pixmap->drawable.width,
                                            pixmap->drawable.height,
                                            pixmap->devKind, data, pbo))
        ret = GLAMOR_UPLOAD_DONE;
    else
        ret = GLAMOR_UPLOAD_FAILED;

    return ret;
}

void
glamor_restore_pixmap_to_texture(PixmapPtr pixmap)
{
    if (glamor_upload_pixmap_to_texture(pixmap) != GLAMOR_UPLOAD_DONE)
        LogMessage(X_WARNING, "Failed to restore pixmap to texture.\n");
}

/*
 * as gles2 only support a very small set of color format and
 * type when do glReadPixel,
 * Before we use glReadPixels to get back a textured pixmap,
 * Use shader to convert it to a supported format and thus
 * get a new temporary pixmap returned.
 * */

glamor_pixmap_fbo *
glamor_es2_pixmap_read_prepare(PixmapPtr source, int x, int y, int w, int h,
                               GLenum format, GLenum type, int no_alpha,
                               int revert, int swap_rb)
{
    glamor_pixmap_private *source_priv;
    glamor_screen_private *glamor_priv;
    ScreenPtr screen;
    glamor_pixmap_fbo *temp_fbo;
    float temp_xscale, temp_yscale, source_xscale, source_yscale;
    static float vertices[8];
    static float texcoords[8];

    screen = source->drawable.pScreen;

    glamor_priv = glamor_get_screen_private(screen);
    source_priv = glamor_get_pixmap_private(source);
    temp_fbo = glamor_create_fbo(glamor_priv, w, h, format, 0);
    if (temp_fbo == NULL)
        return NULL;

    glamor_make_current(glamor_priv);
    temp_xscale = 1.0 / w;
    temp_yscale = 1.0 / h;

    glamor_set_normalize_vcoords((struct glamor_pixmap_private *) NULL,
                                 temp_xscale, temp_yscale, 0, 0, w, h,
                                 glamor_priv->yInverted, vertices);

    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT, GL_FALSE,
                          2 * sizeof(float), vertices);
    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);

    pixmap_priv_get_scale(source_priv, &source_xscale, &source_yscale);
    glamor_set_normalize_tcoords(source_priv, source_xscale,
                                 source_yscale,
                                 x, y,
                                 x + w, y + h,
                                 glamor_priv->yInverted, texcoords);

    glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2, GL_FLOAT, GL_FALSE,
                          2 * sizeof(float), texcoords);
    glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_priv->base.fbo->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glamor_set_destination_pixmap_fbo(temp_fbo, 0, 0, w, h);
    glUseProgram(glamor_priv->finish_access_prog[no_alpha]);
    glUniform1i(glamor_priv->finish_access_revert[no_alpha], revert);
    glUniform1i(glamor_priv->finish_access_swap_rb[no_alpha], swap_rb);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    return temp_fbo;
}

/*
 * Download a sub region of pixmap to a specified memory region.
 * The pixmap must have a valid FBO, otherwise return a NULL.
 * */

static void *
_glamor_download_sub_pixmap_to_cpu(PixmapPtr pixmap, GLenum format,
                                   GLenum type, int no_alpha,
                                   int revert, int swap_rb,
                                   int x, int y, int w, int h,
                                   int stride, void *bits, int pbo,
                                   glamor_access_t access)
{
    glamor_pixmap_private *pixmap_priv;
    GLenum gl_access = 0, gl_usage = 0;
    void *data, *read;
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);
    glamor_pixmap_fbo *temp_fbo = NULL;
    int need_post_conversion = 0;
    int need_free_data = 0;
    int fbo_x_off, fbo_y_off;

    data = bits;
    pixmap_priv = glamor_get_pixmap_private(pixmap);
    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return NULL;

    switch (access) {
    case GLAMOR_ACCESS_RO:
        gl_access = GL_READ_ONLY;
        gl_usage = GL_STREAM_READ;
        break;
    case GLAMOR_ACCESS_RW:
        gl_access = GL_READ_WRITE;
        gl_usage = GL_DYNAMIC_DRAW;
        break;
    default:
        ErrorF("Glamor: Invalid access code. %d\n", access);
        assert(0);
    }

    glamor_make_current(glamor_priv);
    glamor_set_destination_pixmap_priv_nc(pixmap_priv);

    need_post_conversion = (revert > REVERT_NORMAL);
    if (need_post_conversion) {
        if (pixmap->drawable.depth == 1) {
            int temp_stride;

            temp_stride = (((w * 8 + 7) / 8) + 3) & ~3;
            data = malloc(temp_stride * h);
            if (data == NULL)
                return NULL;
            need_free_data = 1;
        }
    }

    pixmap_priv_get_fbo_off(pixmap_priv, &fbo_x_off, &fbo_y_off);

    if (glamor_priv->gl_flavor == GLAMOR_GL_ES2
        && !need_post_conversion
        && (swap_rb != SWAP_NONE_DOWNLOADING || revert != REVERT_NONE)) {
        if (!(temp_fbo = glamor_es2_pixmap_read_prepare(pixmap, x, y, w, h,
                                                        format, type, no_alpha,
                                                        revert, swap_rb))) {
            free(data);
            return NULL;
        }
        x = 0;
        y = 0;
        fbo_x_off = 0;
        fbo_y_off = 0;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    if (glamor_priv->has_pack_invert || glamor_priv->yInverted) {

        if (!glamor_priv->yInverted) {
            assert(glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP);
            glPixelStorei(GL_PACK_INVERT_MESA, 1);
        }

        if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP && data == NULL) {
            assert(pbo > 0);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
            glBufferData(GL_PIXEL_PACK_BUFFER, stride * h, NULL, gl_usage);
        }

        glReadPixels(x + fbo_x_off, y + fbo_y_off, w, h, format, type, data);

        if (!glamor_priv->yInverted) {
            assert(glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP);
            glPixelStorei(GL_PACK_INVERT_MESA, 0);
        }
        if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP && bits == NULL) {
            bits = glMapBuffer(GL_PIXEL_PACK_BUFFER, gl_access);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
    }
    else {
        unsigned int temp_pbo;
        int yy;

        glamor_make_current(glamor_priv);
        glGenBuffers(1, &temp_pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, temp_pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, stride * h, NULL, GL_STREAM_READ);
        glReadPixels(x + fbo_x_off, y + fbo_y_off, w, h, format, type, 0);
        read = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        for (yy = 0; yy < pixmap->drawable.height; yy++)
            memcpy((char *) data + yy * stride,
                   (char *) read + (h - yy - 1) * stride, stride);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glDeleteBuffers(1, &temp_pbo);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (need_post_conversion) {
        /* As OpenGL desktop version never enters here.
         * Don't need to consider if the pbo is valid.*/
        bits = glamor_color_convert_to_bits(data, bits,
                                            w, h,
                                            stride, no_alpha, revert, swap_rb);
    }

    if (temp_fbo != NULL)
        glamor_destroy_fbo(temp_fbo);
    if (need_free_data)
        free(data);

    return bits;
}

void *
glamor_download_sub_pixmap_to_cpu(PixmapPtr pixmap, int x, int y, int w, int h,
                                  int stride, void *bits, int pbo,
                                  glamor_access_t access)
{
    GLenum format, type;
    int no_alpha, revert, swap_rb;
    glamor_pixmap_private *pixmap_priv;
    Bool force_clip;

    if (glamor_get_tex_format_type_from_pixmap(pixmap,
                                               &format,
                                               &type,
                                               &no_alpha,
                                               &revert, &swap_rb, 0)) {
        glamor_fallback("Unknown pixmap depth %d.\n", pixmap->drawable.depth);
        return NULL;
    }

    pixmap_priv = glamor_get_pixmap_private(pixmap);
    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return NULL;

    force_clip = pixmap_priv->base.glamor_priv->gl_flavor != GLAMOR_GL_DESKTOP
        && !glamor_check_fbo_size(pixmap_priv->base.glamor_priv, w, h);

    if (pixmap_priv->type == GLAMOR_TEXTURE_LARGE || force_clip) {

        RegionRec region;
        BoxRec box;
        int n_region;
        glamor_pixmap_clipped_regions *clipped_regions;
        void *sub_bits;
        int i, j;

        sub_bits = malloc(h * stride);
        if (sub_bits == NULL)
            return FALSE;
        box.x1 = x;
        box.y1 = y;
        box.x2 = x + w;
        box.y2 = y + h;
        RegionInitBoxes(&region, &box, 1);

        if (!force_clip)
            clipped_regions =
                glamor_compute_clipped_regions(pixmap_priv, &region, &n_region,
                                               0, 0, 0);
        else
            clipped_regions =
                glamor_compute_clipped_regions_ext(pixmap_priv, &region,
                                                   &n_region,
                                                   pixmap_priv->large.block_w,
                                                   pixmap_priv->large.block_h,
                                                   0,
                                                   0);

        DEBUGF("start download large pixmap %p %dx%d \n", pixmap, w, h);
        for (i = 0; i < n_region; i++) {
            BoxPtr boxes;
            int nbox;
            int temp_stride;
            void *temp_bits;

            assert(pbo == 0);
            SET_PIXMAP_FBO_CURRENT(pixmap_priv, clipped_regions[i].block_idx);

            boxes = RegionRects(clipped_regions[i].region);
            nbox = RegionNumRects(clipped_regions[i].region);
            for (j = 0; j < nbox; j++) {
                temp_stride = PixmapBytePad(boxes[j].x2 - boxes[j].x1,
                                            pixmap->drawable.depth);

                if (boxes[j].x1 == x && temp_stride == stride) {
                    temp_bits = (char *) bits + (boxes[j].y1 - y) * stride;
                }
                else {
                    temp_bits = sub_bits;
                }
                DEBUGF("download x %d y %d w %d h %d temp stride %d \n",
                       boxes[j].x1, boxes[j].y1,
                       boxes[j].x2 - boxes[j].x1,
                       boxes[j].y2 - boxes[j].y1, temp_stride);

                /* For large pixmap, we don't support pbo currently. */
                assert(pbo == 0);
                if (_glamor_download_sub_pixmap_to_cpu
                    (pixmap, format, type, no_alpha, revert, swap_rb,
                     boxes[j].x1, boxes[j].y1, boxes[j].x2 - boxes[j].x1,
                     boxes[j].y2 - boxes[j].y1, temp_stride, temp_bits, pbo,
                     access) == FALSE) {
                    RegionUninit(&region);
                    free(sub_bits);
                    assert(0);
                    return NULL;
                }
                if (boxes[j].x1 != x || temp_stride != stride)
                    glamor_get_bits(bits, stride, temp_bits, temp_stride,
                                    pixmap->drawable.bitsPerPixel,
                                    boxes[j].x1 - x, boxes[j].y1 - y,
                                    boxes[j].x2 - boxes[j].x1,
                                    boxes[j].y2 - boxes[j].y1);
            }

            RegionDestroy(clipped_regions[i].region);
        }
        free(sub_bits);
        free(clipped_regions);
        RegionUninit(&region);
        return bits;
    }
    else
        return _glamor_download_sub_pixmap_to_cpu(pixmap, format, type,
                                                  no_alpha, revert, swap_rb, x,
                                                  y, w, h, stride, bits, pbo,
                                                  access);
}

/**
 * Move a pixmap to CPU memory.
 * The input data is the pixmap's fbo.
 * The output data is at pixmap->devPrivate.ptr. We always use pbo
 * to read the fbo and then map it to va. If possible, we will use
 * it directly as devPrivate.ptr.
 * If successfully download a fbo to cpu then return TRUE.
 * Otherwise return FALSE.
 **/
Bool
glamor_download_pixmap_to_cpu(PixmapPtr pixmap, glamor_access_t access)
{
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    unsigned int stride;
    void *data = NULL, *dst;
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(pixmap->drawable.pScreen);
    int pbo = 0;

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return TRUE;

    glamor_debug_output(GLAMOR_DEBUG_TEXTURE_DOWNLOAD,
                        "Downloading pixmap %p  %dx%d depth%d\n",
                        pixmap,
                        pixmap->drawable.width,
                        pixmap->drawable.height, pixmap->drawable.depth);

    stride = pixmap->devKind;

    if (glamor_priv->gl_flavor == GLAMOR_GL_ES2
        || (!glamor_priv->has_pack_invert && !glamor_priv->yInverted)
        || pixmap_priv->type == GLAMOR_TEXTURE_LARGE) {
        data = malloc(stride * pixmap->drawable.height);
    }
    else {
        glamor_make_current(glamor_priv);
        if (pixmap_priv->base.fbo->pbo == 0)
            glGenBuffers(1, &pixmap_priv->base.fbo->pbo);
        pbo = pixmap_priv->base.fbo->pbo;
    }

    if (pixmap_priv->type == GLAMOR_TEXTURE_DRM) {
        stride = PixmapBytePad(pixmap->drawable.width, pixmap->drawable.depth);
        pixmap_priv->base.drm_stride = pixmap->devKind;
        pixmap->devKind = stride;
    }

    dst = glamor_download_sub_pixmap_to_cpu(pixmap, 0, 0,
                                            pixmap->drawable.width,
                                            pixmap->drawable.height,
                                            pixmap->devKind, data, pbo, access);

    if (!dst) {
        if (data)
            free(data);
        return FALSE;
    }

    if (pbo != 0)
        pixmap_priv->base.fbo->pbo_valid = 1;

    pixmap_priv->base.gl_fbo = GLAMOR_FBO_DOWNLOADED;

    pixmap->devPrivate.ptr = dst;

    return TRUE;
}

/* fixup a fbo to the exact size as the pixmap. */
/* XXX LARGE pixmap? */
Bool
glamor_fixup_pixmap_priv(ScreenPtr screen, glamor_pixmap_private *pixmap_priv)
{
    glamor_pixmap_fbo *old_fbo;
    glamor_pixmap_fbo *new_fbo = NULL;
    PixmapPtr scratch = NULL;
    glamor_pixmap_private *scratch_priv;
    DrawablePtr drawable;
    GCPtr gc = NULL;
    int ret = FALSE;

    drawable = &pixmap_priv->base.pixmap->drawable;

    if (!GLAMOR_PIXMAP_FBO_NOT_EXACT_SIZE(pixmap_priv))
        return TRUE;

    old_fbo = pixmap_priv->base.fbo;

    if (!old_fbo)
        return FALSE;

    gc = GetScratchGC(drawable->depth, screen);
    if (!gc)
        goto fail;

    scratch = glamor_create_pixmap(screen, drawable->width, drawable->height,
                                   drawable->depth, GLAMOR_CREATE_PIXMAP_FIXUP);

    scratch_priv = glamor_get_pixmap_private(scratch);

    if (!scratch_priv->base.fbo)
        goto fail;

    ValidateGC(&scratch->drawable, gc);
    glamor_copy_area(drawable,
                     &scratch->drawable,
                     gc, 0, 0, drawable->width, drawable->height, 0, 0);
    old_fbo = glamor_pixmap_detach_fbo(pixmap_priv);
    new_fbo = glamor_pixmap_detach_fbo(scratch_priv);
    glamor_pixmap_attach_fbo(pixmap_priv->base.pixmap, new_fbo);
    glamor_pixmap_attach_fbo(scratch, old_fbo);

    DEBUGF("old %dx%d type %d\n",
           drawable->width, drawable->height, pixmap_priv->type);
    DEBUGF("copy tex %d  %dx%d to tex %d %dx%d \n",
           old_fbo->tex, old_fbo->width, old_fbo->height, new_fbo->tex,
           new_fbo->width, new_fbo->height);
    ret = TRUE;
 fail:
    if (gc)
        FreeScratchGC(gc);
    if (scratch)
        glamor_destroy_pixmap(scratch);

    return ret;
}

/*
 * We may use this function to reduce a large pixmap to a small sub
 * pixmap. Two scenarios currently:
 * 1. When fallback a large textured pixmap to CPU but we do need to
 * do rendering within a small sub region, then we can just get a
 * sub region.
 *
 * 2. When uploading a large pixmap to texture but we only need to
 * use part of the source/mask picture. As glTexImage2D will be more
 * efficient to upload a contingent region rather than a sub block
 * in a large buffer. We use this function to gather the sub region
 * to a contingent sub pixmap.
 *
 * The sub-pixmap must have the same format as the source pixmap.
 *
 * */
PixmapPtr
glamor_get_sub_pixmap(PixmapPtr pixmap, int x, int y, int w, int h,
                      glamor_access_t access)
{
    glamor_screen_private *glamor_priv;
    PixmapPtr sub_pixmap;
    glamor_pixmap_private *sub_pixmap_priv, *pixmap_priv;
    void *data;
    int pbo;
    int flag;

    if (x < 0 || y < 0)
        return NULL;
    w = (x + w) > pixmap->drawable.width ? (pixmap->drawable.width - x) : w;
    h = (y + h) > pixmap->drawable.height ? (pixmap->drawable.height - y) : h;

    glamor_priv = glamor_get_screen_private(pixmap->drawable.pScreen);
    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return NULL;
    if (glamor_priv->gl_flavor == GLAMOR_GL_ES2 ||
        pixmap_priv->type == GLAMOR_TEXTURE_LARGE)
        flag = GLAMOR_CREATE_PIXMAP_CPU;
    else
        flag = GLAMOR_CREATE_PIXMAP_MAP;

    sub_pixmap = glamor_create_pixmap(pixmap->drawable.pScreen, w, h,
                                      pixmap->drawable.depth, flag);

    if (sub_pixmap == NULL)
        return NULL;

    sub_pixmap_priv = glamor_get_pixmap_private(sub_pixmap);
    pbo =
        sub_pixmap_priv ? (sub_pixmap_priv->base.fbo ? sub_pixmap_priv->base.
                           fbo->pbo : 0) : 0;

    if (pixmap_priv->base.is_picture) {
        sub_pixmap_priv->base.picture = pixmap_priv->base.picture;
        sub_pixmap_priv->base.is_picture = pixmap_priv->base.is_picture;
    }

    if (pbo)
        data = NULL;
    else
        data = sub_pixmap->devPrivate.ptr;

    data =
        glamor_download_sub_pixmap_to_cpu(pixmap, x, y, w, h,
                                          sub_pixmap->devKind, data, pbo,
                                          access);
    if (data == NULL) {
        fbDestroyPixmap(sub_pixmap);
        return NULL;
    }
    if (pbo) {
        assert(sub_pixmap->devPrivate.ptr == NULL);
        sub_pixmap->devPrivate.ptr = data;
        sub_pixmap_priv->base.fbo->pbo_valid = 1;
    }
#if 0
    struct pixman_box16 box;
    PixmapPtr new_sub_pixmap;
    int dx, dy;

    box.x1 = 0;
    box.y1 = 0;
    box.x2 = w;
    box.y2 = h;

    dx = x;
    dy = y;

    new_sub_pixmap = glamor_create_pixmap(pixmap->drawable.pScreen, w, h,
                                          pixmap->drawable.depth,
                                          GLAMOR_CREATE_PIXMAP_CPU);
    glamor_copy_n_to_n(&pixmap->drawable, &new_sub_pixmap->drawable, NULL, &box,
                       1, dx, dy, 0, 0, 0, NULL);
    glamor_compare_pixmaps(new_sub_pixmap, sub_pixmap, 0, 0, w, h, 1, 1);
#endif

    return sub_pixmap;
}

void
glamor_put_sub_pixmap(PixmapPtr sub_pixmap, PixmapPtr pixmap, int x, int y,
                      int w, int h, glamor_access_t access)
{
    void *bits;
    int pbo;
    glamor_pixmap_private *sub_pixmap_priv;

    if (access != GLAMOR_ACCESS_RO) {
        sub_pixmap_priv = glamor_get_pixmap_private(sub_pixmap);
        if (sub_pixmap_priv->base.fbo && sub_pixmap_priv->base.fbo->pbo_valid) {
            bits = NULL;
            pbo = sub_pixmap_priv->base.fbo->pbo;
        }
        else {
            bits = sub_pixmap->devPrivate.ptr;
            pbo = 0;
        }

        assert(x >= 0 && y >= 0);
        w = (w > sub_pixmap->drawable.width) ? sub_pixmap->drawable.width : w;
        h = (h > sub_pixmap->drawable.height) ? sub_pixmap->drawable.height : h;
        glamor_upload_sub_pixmap_to_texture(pixmap, x, y, w, h,
                                            sub_pixmap->devKind, bits, pbo);
    }
    glamor_destroy_pixmap(sub_pixmap);
}
