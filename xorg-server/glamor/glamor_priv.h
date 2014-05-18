/*
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
 *
 */
#ifndef GLAMOR_PRIV_H
#define GLAMOR_PRIV_H

#include "dix-config.h"

#include <xorg-server.h>
#include "glamor.h"

#include <epoxy/gl.h>
#if GLAMOR_HAS_GBM
#define MESA_EGL_NO_X11_HEADERS
#include <epoxy/egl.h>
#endif

#define GLAMOR_DEFAULT_PRECISION  \
    "#ifdef GL_ES\n"              \
    "precision mediump float;\n"  \
    "#endif\n"

#ifdef RENDER
#include "glyphstr.h"
#endif

#include "glamor_debug.h"
#include "glamor_context.h"
#include "glamor_program.h"

#include <list.h>

struct glamor_pixmap_private;

typedef struct glamor_composite_shader {
    GLuint prog;
    GLint dest_to_dest_uniform_location;
    GLint dest_to_source_uniform_location;
    GLint dest_to_mask_uniform_location;
    GLint source_uniform_location;
    GLint mask_uniform_location;
    GLint source_wh;
    GLint mask_wh;
    GLint source_repeat_mode;
    GLint mask_repeat_mode;
    union {
        float source_solid_color[4];
        struct {
            struct glamor_pixmap_private *source_priv;
            PicturePtr source;
        };
    };

    union {
        float mask_solid_color[4];
        struct {
            struct glamor_pixmap_private *mask_priv;
            PicturePtr mask;
        };
    };
} glamor_composite_shader;

enum shader_source {
    SHADER_SOURCE_SOLID,
    SHADER_SOURCE_TEXTURE,
    SHADER_SOURCE_TEXTURE_ALPHA,
    SHADER_SOURCE_COUNT,
};

enum shader_mask {
    SHADER_MASK_NONE,
    SHADER_MASK_SOLID,
    SHADER_MASK_TEXTURE,
    SHADER_MASK_TEXTURE_ALPHA,
    SHADER_MASK_COUNT,
};

enum shader_in {
    SHADER_IN_SOURCE_ONLY,
    SHADER_IN_NORMAL,
    SHADER_IN_CA_SOURCE,
    SHADER_IN_CA_ALPHA,
    SHADER_IN_COUNT,
};

struct shader_key {
    enum shader_source source;
    enum shader_mask mask;
    enum shader_in in;
};

struct blendinfo {
    Bool dest_alpha;
    Bool source_alpha;
    GLenum source_blend;
    GLenum dest_blend;
};

typedef struct {
    INT16 x_src;
    INT16 y_src;
    INT16 x_mask;
    INT16 y_mask;
    INT16 x_dst;
    INT16 y_dst;
    INT16 width;
    INT16 height;
} glamor_composite_rect_t;

enum glamor_vertex_type {
    GLAMOR_VERTEX_POS,
    GLAMOR_VERTEX_SOURCE,
    GLAMOR_VERTEX_MASK
};

enum gradient_shader {
    SHADER_GRADIENT_LINEAR,
    SHADER_GRADIENT_RADIAL,
    SHADER_GRADIENT_CONICAL,
    SHADER_GRADIENT_COUNT,
};

struct glamor_screen_private;
struct glamor_pixmap_private;

enum glamor_gl_flavor {
    GLAMOR_GL_DESKTOP,          // OPENGL API
    GLAMOR_GL_ES2               // OPENGL ES2.0 API
};

#define GLAMOR_NUM_GLYPH_CACHE_FORMATS 2

#define GLAMOR_COMPOSITE_VBO_VERT_CNT (64*1024)

typedef struct {
    PicturePtr picture;         /* Where the glyphs of the cache are stored */
    GlyphPtr *glyphs;
    uint16_t count;
    uint16_t evict;
} glamor_glyph_cache_t;

struct glamor_saved_procs {
    CloseScreenProcPtr close_screen;
    CreateScreenResourcesProcPtr create_screen_resources;
    CreateGCProcPtr create_gc;
    CreatePixmapProcPtr create_pixmap;
    DestroyPixmapProcPtr destroy_pixmap;
    GetSpansProcPtr get_spans;
    GetImageProcPtr get_image;
    CompositeProcPtr composite;
    CompositeRectsProcPtr composite_rects;
    TrapezoidsProcPtr trapezoids;
    GlyphsProcPtr glyphs;
    ChangeWindowAttributesProcPtr change_window_attributes;
    CopyWindowProcPtr copy_window;
    BitmapToRegionProcPtr bitmap_to_region;
    TrianglesProcPtr triangles;
    AddTrapsProcPtr addtraps;
    CreatePictureProcPtr create_picture;
    DestroyPictureProcPtr destroy_picture;
    UnrealizeGlyphProcPtr unrealize_glyph;
    SetWindowPixmapProcPtr set_window_pixmap;
};

#define CACHE_FORMAT_COUNT 3

#define CACHE_BUCKET_WCOUNT 4
#define CACHE_BUCKET_HCOUNT 4

#define GLAMOR_TICK_AFTER(t0, t1) 	\
	(((int)(t1) - (int)(t0)) < 0)

#define IDLE_STATE 0
#define RENDER_STATE 1
#define BLIT_STATE 2
#define RENDER_IDEL_MAX 32

typedef struct glamor_screen_private {
    Bool yInverted;
    unsigned int tick;
    enum glamor_gl_flavor gl_flavor;
    int glsl_version;
    int has_pack_invert;
    int has_fbo_blit;
    int has_map_buffer_range;
    int has_buffer_storage;
    int has_khr_debug;
    int max_fbo_size;

    struct xorg_list
        fbo_cache[CACHE_FORMAT_COUNT][CACHE_BUCKET_WCOUNT][CACHE_BUCKET_HCOUNT];
    unsigned long fbo_cache_watermark;

    /* glamor_solid */
    GLint solid_prog;
    GLint solid_color_uniform_location;

    /* glamor point shader */
    glamor_program point_prog;

    /* glamor spans shaders */
    glamor_program_fill fill_spans_program;

    /* glamor rect shaders */
    glamor_program_fill poly_fill_rect_program;

    /* glamor glyphblt shaders */
    glamor_program_fill poly_glyph_blt_progs;

    /* glamor text shaders */
    glamor_program_fill poly_text_progs;
    glamor_program      te_text_prog;
    glamor_program      image_text_prog;

    /* vertext/elment_index buffer object for render */
    GLuint vbo, ebo;
    /** Next offset within the VBO that glamor_get_vbo_space() will use. */
    int vbo_offset;
    int vbo_size;
    /**
     * Pointer to glamor_get_vbo_space()'s current VBO mapping.
     *
     * Note that this is not necessarily equal to the pointer returned
     * by glamor_get_vbo_space(), so it can't be used in place of that.
     */
    char *vb;
    int vb_stride;
    Bool has_source_coords, has_mask_coords;
    int render_nr_verts;
    glamor_composite_shader composite_shader[SHADER_SOURCE_COUNT]
        [SHADER_MASK_COUNT]
        [SHADER_IN_COUNT];
    glamor_glyph_cache_t glyphCaches[GLAMOR_NUM_GLYPH_CACHE_FORMATS];
    Bool glyph_cache_initialized;

    /* shaders to restore a texture to another texture. */
    GLint finish_access_prog[2];
    GLint finish_access_revert[2];
    GLint finish_access_swap_rb[2];

    /* glamor_tile */
    GLint tile_prog;
    GLint tile_wh;

    /* glamor gradient, 0 for small nstops, 1 for
       large nstops and 2 for dynamic generate. */
    GLint gradient_prog[SHADER_GRADIENT_COUNT][3];
    int linear_max_nstops;
    int radial_max_nstops;

    /* glamor trapezoid shader. */
    GLint trapezoid_prog;

    PixmapPtr *back_pixmap;
    int screen_fbo;
    struct glamor_saved_procs saved_procs;
    char delayed_fallback_string[GLAMOR_DELAYED_STRING_MAX + 1];
    int delayed_fallback_pending;
    int flags;
    int state;
    unsigned int render_idle_cnt;
    ScreenPtr screen;
    int dri3_enabled;

    /* xv */
    GLint xv_prog;

    struct glamor_context ctx;
} glamor_screen_private;

typedef enum glamor_access {
    GLAMOR_ACCESS_RO,
    GLAMOR_ACCESS_RW,
} glamor_access_t;

enum glamor_fbo_state {
    /** There is no storage attached to the pixmap. */
    GLAMOR_FBO_UNATTACHED,
    /**
     * The pixmap has FBO storage attached, but devPrivate.ptr doesn't
     * point at anything.
     */
    GLAMOR_FBO_NORMAL,
    /**
     * The FBO is present and can be accessed as a linear memory
     * mapping through devPrivate.ptr.
     */
    GLAMOR_FBO_DOWNLOADED,
};

/* glamor_pixmap_fbo:
 * @list:    to be used to link to the cache pool list.
 * @expire:  when push to cache pool list, set a expire count.
 * 	     will be freed when glamor_priv->tick is equal or
 * 	     larger than this expire count in block handler.
 * @pbo_valid: The pbo has a valid copy of the pixmap's data.
 * @tex:     attached texture.
 * @fb:      attached fbo.
 * @pbo:     attached pbo.
 * @width:   width of this fbo.
 * @height:  height of this fbo.
 * @format:  internal format of this fbo's texture.
 * @type:    internal type of this fbo's texture.
 * @glamor_priv: point to glamor private data.
 */
typedef struct glamor_pixmap_fbo {
    struct xorg_list list;
    unsigned int expire;
    unsigned char pbo_valid;
    GLuint tex;
    GLuint fb;
    GLuint pbo;
    int width;
    int height;
    GLenum format;
    GLenum type;
    glamor_screen_private *glamor_priv;
} glamor_pixmap_fbo;

/*
 * glamor_pixmap_private - glamor pixmap's private structure.
 * @gl_tex:  The pixmap is in a gl texture originally.
 * @is_picture: The drawable is attached to a picture.
 * @pict_format: the corresponding picture's format.
 * @pixmap: The corresponding pixmap's pointer.
 *
 * For GLAMOR_TEXTURE_LARGE, nbox should larger than 1.
 * And the box and fbo will both have nbox elements.
 * and box[i] store the relatively coords in this pixmap
 * of the fbo[i]. The reason why use boxes not region to
 * represent this structure is we may need to use overlapped
 * boxes for one pixmap for some special reason.
 *
 * pixmap
 * ******************
 * *  fbo0 * fbo1   *
 * *       *        *
 * ******************
 * *  fbo2 * fbo3   *
 * *       *        *
 * ******************
 *
 * Let's assume the texture has size of 1024x1024
 * box[0] = {0,0,1024,1024}
 * box[1] = {1024,0,2048,2048}
 * ...
 *
 * For GLAMOR_TEXTURE_ATLAS nbox should be 1. And box
 * and fbo both has one elements, and the box store
 * the relatively coords in the fbo of this pixmap:
 *
 * fbo
 * ******************
 * *   pixmap       *
 * *   *********    *
 * *   *       *    *
 * *   *********    *
 * *                *
 * ******************
 *
 * Assume the pixmap is at the (100,100) relatively to
 * the fbo's origin.
 * box[0]={100, 100, 1124, 1124};
 *
 * Considering large pixmap is not a normal case, to keep
 * it simple, I designe it as the following way.
 * When deal with a large pixmap, it split the working
 * rectangle into serval boxes, and each box fit into a
 * corresponding fbo. And then the rendering function will
 * loop from the left-top box to the right-bottom box,
 * each time, we will set current box and current fbo
 * to the box and fbo elements. Thus the inner routines
 * can handle it as normal, only the coords calculation need
 * to aware of it's large pixmap.
 *
 * Currently, we haven't implemented the atlas pixmap.
 *
 **/

typedef struct glamor_pixmap_clipped_regions {
    int block_idx;
    RegionPtr region;
} glamor_pixmap_clipped_regions;

#define SET_PIXMAP_FBO_CURRENT(priv, idx) 				\
  do {									\
	if (priv->type == GLAMOR_TEXTURE_LARGE) {			\
		(priv)->large.base.fbo = priv->large.fbo_array[idx]; 	\
		(priv)->large.box = priv->large.box_array[idx]; 	\
	}								\
  } while(0)

typedef struct glamor_pixmap_private_base {
    glamor_pixmap_type_t type;
    enum glamor_fbo_state gl_fbo;
    /**
     * If devPrivate.ptr is non-NULL (meaning we're within
     * glamor_prepare_access), determies whether we should re-upload
     * that data on glamor_finish_access().
     */
    glamor_access_t map_access;
    unsigned char is_picture:1;
    unsigned char gl_tex:1;
    glamor_pixmap_fbo *fbo;
    PixmapPtr pixmap;
    BoxRec box;
    int drm_stride;
    glamor_screen_private *glamor_priv;
    PicturePtr picture;
#if GLAMOR_HAS_GBM
    EGLImageKHR image;
#endif
} glamor_pixmap_private_base_t;

/*
 * @base.fbo: current fbo.
 * @box: current fbo's coords in the whole pixmap.
 * @block_w: block width of this large pixmap.
 * @block_h: block height of this large pixmap.
 * @block_wcnt: block count in one block row.
 * @block_hcnt: block count in one block column.
 * @nbox: total block count.
 * @box_array: contains each block's corresponding box.
 * @fbo_array: contains each block's fbo pointer.
 *
 **/
typedef struct glamor_pixmap_private_large {
    union {
        glamor_pixmap_type_t type;
        glamor_pixmap_private_base_t base;
    };
    BoxRec box;
    int block_w;
    int block_h;
    int block_wcnt;
    int block_hcnt;
    int nbox;
    BoxPtr box_array;
    glamor_pixmap_fbo **fbo_array;
} glamor_pixmap_private_large_t;

/*
 * @box: the relative coords in the corresponding fbo.
 */
typedef struct glamor_pixmap_private_atlas {
    union {
        glamor_pixmap_type_t type;
        glamor_pixmap_private_base_t base;
    };
    BoxRec box;
} glamor_pixmap_private_atlas_t;

typedef struct glamor_pixmap_private {
    union {
        glamor_pixmap_type_t type;
        glamor_pixmap_private_base_t base;
        glamor_pixmap_private_large_t large;
        glamor_pixmap_private_atlas_t atlas;
    };
} glamor_pixmap_private;

static inline glamor_pixmap_fbo *
glamor_pixmap_fbo_at(glamor_pixmap_private *priv, int x, int y)
{
    if (priv->type == GLAMOR_TEXTURE_LARGE) {
        assert(x < priv->large.block_wcnt);
        assert(y < priv->large.block_hcnt);
        return priv->large.fbo_array[y * priv->large.block_wcnt + x];
    }
    assert (x == 0);
    assert (y == 0);
    return priv->base.fbo;
}

static inline BoxPtr
glamor_pixmap_box_at(glamor_pixmap_private *priv, int x, int y)
{
    if (priv->type == GLAMOR_TEXTURE_LARGE) {
        assert(x < priv->large.block_wcnt);
        assert(y < priv->large.block_hcnt);
        return &priv->large.box_array[y * priv->large.block_wcnt + x];
    }
    assert (x == 0);
    assert (y == 0);
    return &priv->base.box;
}

static inline int
glamor_pixmap_wcnt(glamor_pixmap_private *priv)
{
    if (priv->type == GLAMOR_TEXTURE_LARGE)
        return priv->large.block_wcnt;
    return 1;
}

static inline int
glamor_pixmap_hcnt(glamor_pixmap_private *priv)
{
    if (priv->type == GLAMOR_TEXTURE_LARGE)
        return priv->large.block_hcnt;
    return 1;
}

#define glamor_pixmap_loop(priv, x, y)                  \
    for (y = 0; y < glamor_pixmap_hcnt(priv); y++)      \
        for (x = 0; x < glamor_pixmap_wcnt(priv); x++)

/* 
 * Pixmap dynamic status, used by dynamic upload feature.
 *
 * GLAMOR_NONE:  initial status, don't need to do anything.
 * GLAMOR_UPLOAD_PENDING: marked as need to be uploaded to gl texture.
 * GLAMOR_UPLOAD_DONE: the pixmap has been uploaded successfully.
 * GLAMOR_UPLOAD_FAILED: fail to upload the pixmap.
 *
 * */
typedef enum glamor_pixmap_status {
    GLAMOR_NONE,
    GLAMOR_UPLOAD_PENDING,
    GLAMOR_UPLOAD_DONE,
    GLAMOR_UPLOAD_FAILED
} glamor_pixmap_status_t;

extern DevPrivateKey glamor_screen_private_key;
extern DevPrivateKey glamor_pixmap_private_key;
static inline glamor_screen_private *
glamor_get_screen_private(ScreenPtr screen)
{
    return (glamor_screen_private *)
        dixLookupPrivate(&screen->devPrivates, glamor_screen_private_key);
}

static inline void
glamor_set_screen_private(ScreenPtr screen, glamor_screen_private *priv)
{
    dixSetPrivate(&screen->devPrivates, glamor_screen_private_key, priv);
}

static inline glamor_pixmap_private *
glamor_get_pixmap_private(PixmapPtr pixmap)
{
    glamor_pixmap_private *priv;

    priv = dixLookupPrivate(&pixmap->devPrivates, glamor_pixmap_private_key);
    if (!priv) {
        glamor_set_pixmap_type(pixmap, GLAMOR_MEMORY);
        priv = dixLookupPrivate(&pixmap->devPrivates,
                                glamor_pixmap_private_key);
    }
    return priv;
}

void glamor_set_pixmap_private(PixmapPtr pixmap, glamor_pixmap_private *priv);

/**
 * Returns TRUE if the given planemask covers all the significant bits in the
 * pixel values for pDrawable.
 */
static inline Bool
glamor_pm_is_solid(DrawablePtr drawable, unsigned long planemask)
{
    return (planemask & FbFullMask(drawable->depth)) ==
        FbFullMask(drawable->depth);
}

extern int glamor_debug_level;

/* glamor.c */
PixmapPtr glamor_get_drawable_pixmap(DrawablePtr drawable);

glamor_pixmap_fbo *glamor_pixmap_detach_fbo(glamor_pixmap_private *
                                            pixmap_priv);
void glamor_pixmap_attach_fbo(PixmapPtr pixmap, glamor_pixmap_fbo *fbo);
glamor_pixmap_fbo *glamor_create_fbo_from_tex(glamor_screen_private *
                                              glamor_priv, int w, int h,
                                              GLenum format, GLint tex,
                                              int flag);
glamor_pixmap_fbo *glamor_create_fbo(glamor_screen_private *glamor_priv, int w,
                                     int h, GLenum format, int flag);
void glamor_destroy_fbo(glamor_pixmap_fbo *fbo);
void glamor_pixmap_destroy_fbo(glamor_pixmap_private *priv);
void glamor_purge_fbo(glamor_pixmap_fbo *fbo);

void glamor_init_pixmap_fbo(ScreenPtr screen);
void glamor_fini_pixmap_fbo(ScreenPtr screen);
Bool glamor_pixmap_fbo_fixup(ScreenPtr screen, PixmapPtr pixmap);
void glamor_fbo_expire(glamor_screen_private *glamor_priv);

glamor_pixmap_fbo *glamor_create_fbo_array(glamor_screen_private *glamor_priv,
                                           int w, int h, GLenum format,
                                           int flag, int block_w, int block_h,
                                           glamor_pixmap_private *);

/* glamor_copyarea.c */
RegionPtr

glamor_copy_area(DrawablePtr src, DrawablePtr dst, GCPtr gc,
                 int srcx, int srcy, int width, int height, int dstx, int dsty);
void glamor_copy_n_to_n(DrawablePtr src, DrawablePtr dst, GCPtr gc,
                        BoxPtr box, int nbox, int dx, int dy, Bool reverse,
                        Bool upsidedown, Pixel bitplane, void *closure);

/* glamor_core.c */
Bool glamor_prepare_access(DrawablePtr drawable, glamor_access_t access);
void glamor_finish_access(DrawablePtr drawable);
Bool glamor_prepare_access_window(WindowPtr window);
void glamor_finish_access_window(WindowPtr window);
Bool glamor_prepare_access_gc(GCPtr gc);
void glamor_finish_access_gc(GCPtr gc);
void glamor_init_finish_access_shaders(ScreenPtr screen);
void glamor_fini_finish_access_shaders(ScreenPtr screen);
const Bool glamor_get_drawable_location(const DrawablePtr drawable);
void glamor_get_drawable_deltas(DrawablePtr drawable, PixmapPtr pixmap,
                                int *x, int *y);
Bool glamor_stipple(PixmapPtr pixmap, PixmapPtr stipple,
                    int x, int y, int width, int height,
                    unsigned char alu, unsigned long planemask,
                    unsigned long fg_pixel, unsigned long bg_pixel,
                    int stipple_x, int stipple_y);
GLint glamor_compile_glsl_prog(GLenum type, const char *source);
void glamor_link_glsl_prog(ScreenPtr screen, GLint prog,
                           const char *format, ...) _X_ATTRIBUTE_PRINTF(3,4);
void glamor_get_color_4f_from_pixel(PixmapPtr pixmap,
                                    unsigned long fg_pixel, GLfloat *color);

int glamor_set_destination_pixmap(PixmapPtr pixmap);
int glamor_set_destination_pixmap_priv(glamor_pixmap_private *pixmap_priv);
void glamor_set_destination_pixmap_fbo(glamor_pixmap_fbo *, int, int, int, int);

/* nc means no check. caller must ensure this pixmap has valid fbo.
 * usually use the GLAMOR_PIXMAP_PRIV_HAS_FBO firstly. 
 * */
void glamor_set_destination_pixmap_priv_nc(glamor_pixmap_private *pixmap_priv);

glamor_pixmap_fbo *glamor_es2_pixmap_read_prepare(PixmapPtr source, int x,
                                                  int y, int w, int h,
                                                  GLenum format, GLenum type,
                                                  int no_alpha, int revert,
                                                  int swap_rb);

Bool glamor_set_alu(ScreenPtr screen, unsigned char alu);
Bool glamor_set_planemask(PixmapPtr pixmap, unsigned long planemask);
RegionPtr glamor_bitmap_to_region(PixmapPtr pixmap);

/* glamor_fill.c */
Bool glamor_fill(DrawablePtr drawable,
                 GCPtr gc, int x, int y, int width, int height, Bool fallback);
Bool glamor_solid(PixmapPtr pixmap, int x, int y, int width, int height,
                  unsigned char alu, unsigned long planemask,
                  unsigned long fg_pixel);
Bool glamor_solid_boxes(PixmapPtr pixmap,
                        BoxPtr box, int nbox, unsigned long fg_pixel);

void glamor_init_solid_shader(ScreenPtr screen);
void glamor_fini_solid_shader(ScreenPtr screen);

/* glamor_glyphs.c */
Bool glamor_realize_glyph_caches(ScreenPtr screen);
void glamor_glyphs_fini(ScreenPtr screen);
void glamor_glyphs(CARD8 op,
                   PicturePtr pSrc,
                   PicturePtr pDst,
                   PictFormatPtr maskFormat,
                   INT16 xSrc,
                   INT16 ySrc, int nlist, GlyphListPtr list, GlyphPtr *glyphs);

/* glamor_polylines.c */
void glamor_poly_lines(DrawablePtr drawable, GCPtr gc, int mode, int n,
                       DDXPointPtr points);

/* glamor_render.c */
Bool glamor_composite_clipped_region(CARD8 op,
                                     PicturePtr source,
                                     PicturePtr mask,
                                     PicturePtr dest,
                                     glamor_pixmap_private *soruce_pixmap_priv,
                                     glamor_pixmap_private *mask_pixmap_priv,
                                     glamor_pixmap_private *dest_pixmap_priv,
                                     RegionPtr region,
                                     int x_source,
                                     int y_source,
                                     int x_mask, int y_mask,
                                     int x_dest, int y_dest);

void glamor_composite(CARD8 op,
                      PicturePtr pSrc,
                      PicturePtr pMask,
                      PicturePtr pDst,
                      INT16 xSrc,
                      INT16 ySrc,
                      INT16 xMask,
                      INT16 yMask,
                      INT16 xDst, INT16 yDst, CARD16 width, CARD16 height);

void glamor_init_composite_shaders(ScreenPtr screen);
void glamor_fini_composite_shaders(ScreenPtr screen);
void glamor_composite_glyph_rects(CARD8 op,
                                  PicturePtr src, PicturePtr mask,
                                  PicturePtr dst, int nrect,
                                  glamor_composite_rect_t *rects);
void glamor_composite_rects(CARD8 op,
                            PicturePtr pDst,
                            xRenderColor *color, int nRect, xRectangle *rects);
void glamor_init_trapezoid_shader(ScreenPtr screen);
void glamor_fini_trapezoid_shader(ScreenPtr screen);
PicturePtr glamor_convert_gradient_picture(ScreenPtr screen,
                                           PicturePtr source,
                                           int x_source,
                                           int y_source, int width, int height);

Bool glamor_composite_choose_shader(CARD8 op,
                                    PicturePtr source,
                                    PicturePtr mask,
                                    PicturePtr dest,
                                    glamor_pixmap_private *source_pixmap_priv,
                                    glamor_pixmap_private *mask_pixmap_priv,
                                    glamor_pixmap_private *dest_pixmap_priv,
                                    struct shader_key *s_key,
                                    glamor_composite_shader ** shader,
                                    struct blendinfo *op_info,
                                    PictFormatShort *psaved_source_format);

void glamor_composite_set_shader_blend(glamor_pixmap_private *dest_priv,
                                       struct shader_key *key,
                                       glamor_composite_shader *shader,
                                       struct blendinfo *op_info);

void *glamor_setup_composite_vbo(ScreenPtr screen, int n_verts);

/* glamor_trapezoid.c */
void glamor_trapezoids(CARD8 op,
                       PicturePtr src, PicturePtr dst,
                       PictFormatPtr mask_format, INT16 x_src, INT16 y_src,
                       int ntrap, xTrapezoid *traps);

/* glamor_tile.c */
Bool glamor_tile(PixmapPtr pixmap, PixmapPtr tile,
                 int x, int y, int width, int height,
                 unsigned char alu, unsigned long planemask,
                 int tile_x, int tile_y);
void glamor_init_tile_shader(ScreenPtr screen);
void glamor_fini_tile_shader(ScreenPtr screen);

/* glamor_gradient.c */
void glamor_init_gradient_shader(ScreenPtr screen);
void glamor_fini_gradient_shader(ScreenPtr screen);
PicturePtr glamor_generate_linear_gradient_picture(ScreenPtr screen,
                                                   PicturePtr src_picture,
                                                   int x_source, int y_source,
                                                   int width, int height,
                                                   PictFormatShort format);
PicturePtr glamor_generate_radial_gradient_picture(ScreenPtr screen,
                                                   PicturePtr src_picture,
                                                   int x_source, int y_source,
                                                   int width, int height,
                                                   PictFormatShort format);

/* glamor_triangles.c */
void glamor_triangles(CARD8 op,
                      PicturePtr pSrc,
                      PicturePtr pDst,
                      PictFormatPtr maskFormat,
                      INT16 xSrc, INT16 ySrc, int ntris, xTriangle * tris);

/* glamor_pixmap.c */

void glamor_pixmap_init(ScreenPtr screen);
void glamor_pixmap_fini(ScreenPtr screen);

/* glamor_vbo.c */

void glamor_init_vbo(ScreenPtr screen);
void glamor_fini_vbo(ScreenPtr screen);

void *
glamor_get_vbo_space(ScreenPtr screen, unsigned size, char **vbo_offset);

void
glamor_put_vbo_space(ScreenPtr screen);

/** 
 * Download a pixmap's texture to cpu memory. If success,
 * One copy of current pixmap's texture will be put into
 * the pixmap->devPrivate.ptr. Will use pbo to map to 
 * the pointer if possible.
 * The pixmap must be a gl texture pixmap. gl_fbo must be GLAMOR_FBO_NORMAL and
 * gl_tex must be 1. Used by glamor_prepare_access.
 *
 */
Bool glamor_download_pixmap_to_cpu(PixmapPtr pixmap, glamor_access_t access);

void *glamor_download_sub_pixmap_to_cpu(PixmapPtr pixmap, int x, int y, int w,
                                        int h, int stride, void *bits, int pbo,
                                        glamor_access_t access);

/**
 * Restore a pixmap's data which is downloaded by 
 * glamor_download_pixmap_to_cpu to its original 
 * gl texture. Used by glamor_finish_access. 
 *
 * The pixmap must originally be a texture -- gl_fbo must be
 * GLAMOR_FBO_NORMAL.
 **/
void glamor_restore_pixmap_to_texture(PixmapPtr pixmap);

/**
 * According to the flag,
 * if the flag is GLAMOR_CREATE_FBO_NO_FBO then just ensure
 * the fbo has a valid texture. Otherwise, it will ensure
 * the fbo has valid texture and attach to a valid fb.
 * If the fbo already has a valid glfbo then do nothing.
 */
Bool glamor_pixmap_ensure_fbo(PixmapPtr pixmap, GLenum format, int flag);

/**
 * Upload a pixmap to gl texture. Used by dynamic pixmap
 * uploading feature. The pixmap must be a software pixmap.
 * This function will change current FBO and current shaders.
 */
enum glamor_pixmap_status glamor_upload_pixmap_to_texture(PixmapPtr pixmap);

Bool glamor_upload_sub_pixmap_to_texture(PixmapPtr pixmap, int x, int y, int w,
                                         int h, int stride, void *bits,
                                         int pbo);

PixmapPtr glamor_get_sub_pixmap(PixmapPtr pixmap, int x, int y,
                                int w, int h, glamor_access_t access);
void glamor_put_sub_pixmap(PixmapPtr sub_pixmap, PixmapPtr pixmap, int x, int y,
                           int w, int h, glamor_access_t access);

glamor_pixmap_clipped_regions *
glamor_compute_clipped_regions(glamor_pixmap_private *priv,
                               RegionPtr region, int *clipped_nbox,
                               int repeat_type, int reverse,
                               int upsidedown);

glamor_pixmap_clipped_regions *
glamor_compute_clipped_regions_ext(glamor_pixmap_private *pixmap_priv,
                                   RegionPtr region, int *n_region,
                                   int inner_block_w, int inner_block_h,
                                   int reverse, int upsidedown);

glamor_pixmap_clipped_regions *
glamor_compute_transform_clipped_regions(glamor_pixmap_private *priv,
                                         struct pixman_transform *transform,
                                         RegionPtr region,
                                         int *n_region, int dx, int dy,
                                         int repeat_type, int reverse,
                                         int upsidedown);

Bool glamor_composite_largepixmap_region(CARD8 op,
                                         PicturePtr source,
                                         PicturePtr mask,
                                         PicturePtr dest,
                                         glamor_pixmap_private *source_pixmap_priv,
                                         glamor_pixmap_private *mask_pixmap_priv,
                                         glamor_pixmap_private *dest_pixmap_priv,
                                         RegionPtr region, Bool force_clip,
                                         INT16 x_source,
                                         INT16 y_source,
                                         INT16 x_mask,
                                         INT16 y_mask,
                                         INT16 x_dest, INT16 y_dest,
                                         CARD16 width, CARD16 height);

Bool glamor_get_transform_block_size(struct pixman_transform *transform,
                                     int block_w, int block_h,
                                     int *transformed_block_w,
                                     int *transformed_block_h);

void glamor_get_transform_extent_from_box(struct pixman_box32 *temp_box,
                                          struct pixman_transform *transform);

/**
 * Upload a picture to gl texture. Similar to the
 * glamor_upload_pixmap_to_texture. Used in rendering.
 **/
enum glamor_pixmap_status glamor_upload_picture_to_texture(PicturePtr picture);

/**
 * Upload bits to a pixmap's texture. This function will
 * convert the bits to the specified format/type format
 * if the conversion is unavoidable.
 **/
Bool glamor_upload_bits_to_pixmap_texture(PixmapPtr pixmap, GLenum format,
                                          GLenum type, int no_alpha, int revert,
                                          int swap_rb, void *bits);

/**
 * Destroy all the resources allocated on the uploading
 * phase, includs the tex and fbo.
 **/
void glamor_destroy_upload_pixmap(PixmapPtr pixmap);

int glamor_create_picture(PicturePtr picture);

void glamor_set_window_pixmap(WindowPtr pWindow, PixmapPtr pPixmap);

Bool glamor_prepare_access_picture(PicturePtr picture, glamor_access_t access);

void glamor_finish_access_picture(PicturePtr picture);

void glamor_destroy_picture(PicturePtr picture);

/* fixup a fbo to the exact size as the pixmap. */
Bool glamor_fixup_pixmap_priv(ScreenPtr screen,
                              glamor_pixmap_private *pixmap_priv);

void glamor_picture_format_fixup(PicturePtr picture,
                                 glamor_pixmap_private *pixmap_priv);

void glamor_add_traps(PicturePtr pPicture,
                      INT16 x_off, INT16 y_off, int ntrap, xTrap *traps);

RegionPtr glamor_copy_plane(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
                            int srcx, int srcy, int w, int h,
                            int dstx, int dsty,
                            unsigned long bitPlane);

/* glamor_text.c */
int glamor_poly_text8(DrawablePtr pDrawable, GCPtr pGC,
                      int x, int y, int count, char *chars);

int glamor_poly_text16(DrawablePtr pDrawable, GCPtr pGC,
                       int x, int y, int count, unsigned short *chars);

void glamor_image_text8(DrawablePtr pDrawable, GCPtr pGC,
                        int x, int y, int count, char *chars);

void glamor_image_text16(DrawablePtr pDrawable, GCPtr pGC,
                         int x, int y, int count, unsigned short *chars);

/* glamor_spans.c */
void
glamor_fill_spans(DrawablePtr drawable,
                  GCPtr gc,
                  int n, DDXPointPtr points, int *widths, int sorted);

void
glamor_get_spans(DrawablePtr drawable, int wmax,
                 DDXPointPtr points, int *widths, int count, char *dst);

void
glamor_set_spans(DrawablePtr drawable, GCPtr gc, char *src,
                 DDXPointPtr points, int *widths, int numPoints, int sorted);

/* glamor_rects.c */
void
glamor_poly_fill_rect(DrawablePtr drawable,
                      GCPtr gc, int nrect, xRectangle *prect);

/* glamor_image.c */
void
glamor_put_image(DrawablePtr drawable, GCPtr gc, int depth, int x, int y,
                 int w, int h, int leftPad, int format, char *bits);

void
glamor_get_image(DrawablePtr pDrawable, int x, int y, int w, int h,
                 unsigned int format, unsigned long planeMask, char *d);

/* glamor_glyphblt.c */
void glamor_image_glyph_blt(DrawablePtr pDrawable, GCPtr pGC,
                            int x, int y, unsigned int nglyph,
                            CharInfoPtr *ppci, void *pglyphBase);

void glamor_poly_glyph_blt(DrawablePtr pDrawable, GCPtr pGC,
                           int x, int y, unsigned int nglyph,
                           CharInfoPtr *ppci, void *pglyphBase);

void glamor_push_pixels(GCPtr pGC, PixmapPtr pBitmap,
                        DrawablePtr pDrawable, int w, int h, int x, int y);

void glamor_poly_point(DrawablePtr pDrawable, GCPtr pGC, int mode, int npt,
                       DDXPointPtr ppt);

void glamor_poly_segment(DrawablePtr pDrawable, GCPtr pGC, int nseg,
                         xSegment *pSeg);

void glamor_poly_line(DrawablePtr pDrawable, GCPtr pGC, int mode, int npt,
                      DDXPointPtr ppt);

void glamor_composite_rectangles(CARD8 op,
                                 PicturePtr dst,
                                 xRenderColor *color,
                                 int num_rects, xRectangle *rects);

/* glamor_xv */
typedef struct {
    uint32_t transform_index;
    uint32_t gamma;             /* gamma value x 1000 */
    int brightness;
    int saturation;
    int hue;
    int contrast;

    DrawablePtr pDraw;
    PixmapPtr pPixmap;
    uint32_t src_pitch;
    uint8_t *src_addr;
    int src_w, src_h, dst_w, dst_h;
    int src_x, src_y, drw_x, drw_y;
    int w, h;
    RegionRec clip;
    PixmapPtr src_pix[3];       /* y, u, v for planar */
    int src_pix_w, src_pix_h;
} glamor_port_private;

void glamor_init_xv_shader(ScreenPtr screen);
void glamor_fini_xv_shader(ScreenPtr screen);

#include"glamor_utils.h"

/* Dynamic pixmap upload to texture if needed. 
 * Sometimes, the target is a gl texture pixmap/picture,
 * but the source or mask is in cpu memory. In that case,
 * upload the source/mask to gl texture and then avoid 
 * fallback the whole process to cpu. Most of the time,
 * this will increase performance obviously. */

#define GLAMOR_PIXMAP_DYNAMIC_UPLOAD
#define GLAMOR_GRADIENT_SHADER
#define GLAMOR_TRAPEZOID_SHADER
#define GLAMOR_TEXTURED_LARGE_PIXMAP 1
#define WALKAROUND_LARGE_TEXTURE_MAP
#if 0
#define MAX_FBO_SIZE 32         /* For test purpose only. */
#endif
//#define GLYPHS_NO_EDEGEMAP_OVERLAP_CHECK
#define GLYPHS_EDEGE_OVERLAP_LOOSE_CHECK

#include "glamor_font.h"

#endif                          /* GLAMOR_PRIV_H */
