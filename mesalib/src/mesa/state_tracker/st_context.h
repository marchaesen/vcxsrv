/**************************************************************************
 * 
 * Copyright 2003 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#ifndef ST_CONTEXT_H
#define ST_CONTEXT_H

#include "main/mtypes.h"
#include "state_tracker/st_api.h"
#include "main/fbobject.h"
#include "state_tracker/st_atom.h"
#include "util/u_inlines.h"
#include "util/list.h"
#include "vbo/vbo.h"


#ifdef __cplusplus
extern "C" {
#endif


struct dd_function_table;
struct draw_context;
struct draw_stage;
struct gen_mipmap_state;
struct st_context;
struct st_fragment_program;
struct st_perf_monitor_group;
struct u_upload_mgr;


/** For drawing quads for glClear, glDraw/CopyPixels, glBitmap, etc. */
struct st_util_vertex
{
   float x, y, z;
   float r, g, b, a;
   float s, t;
};

struct st_bitmap_cache
{
   /** Window pos to render the cached image */
   GLint xpos, ypos;
   /** Bounds of region used in window coords */
   GLint xmin, ymin, xmax, ymax;

   GLfloat color[4];

   /** Bitmap's Z position */
   GLfloat zpos;

   struct pipe_resource *texture;
   struct pipe_transfer *trans;

   GLboolean empty;

   /** An I8 texture image: */
   ubyte *buffer;
};

struct st_bound_handles
{
   unsigned num_handles;
   uint64_t *handles;
};


#define NUM_DRAWPIX_CACHE_ENTRIES 4

struct drawpix_cache_entry
{
   GLsizei width, height;
   GLenum format, type;
   const void *user_pointer;  /**< Last user 'pixels' pointer */
   void *image;               /**< Copy of the glDrawPixels image data */
   struct pipe_resource *texture;
   unsigned age;
};


struct st_context
{
   struct st_context_iface iface;

   struct gl_context *ctx;

   struct pipe_context *pipe;

   struct draw_context *draw;  /**< For selection/feedback/rastpos only */
   struct draw_stage *feedback_stage;  /**< For GL_FEEDBACK rendermode */
   struct draw_stage *selection_stage;  /**< For GL_SELECT rendermode */
   struct draw_stage *rastpos_stage;  /**< For glRasterPos */
   GLboolean clamp_frag_color_in_shader;
   GLboolean clamp_vert_color_in_shader;
   boolean has_stencil_export; /**< can do shader stencil export? */
   boolean has_time_elapsed;
   boolean has_shader_model3;
   boolean has_etc1;
   boolean has_etc2;
   boolean prefer_blit_based_texture_transfer;
   boolean force_persample_in_shader;
   boolean has_shareable_shaders;
   boolean has_half_float_packing;
   boolean has_multi_draw_indirect;
   boolean can_bind_const_buffer_as_vertex;

   /**
    * If a shader can be created when we get its source.
    * This means it has only 1 variant, not counting glBitmap and
    * glDrawPixels.
    */
   boolean shader_has_one_variant[MESA_SHADER_STAGES];

   boolean needs_texcoord_semantic;
   boolean apply_texture_swizzle_to_border_color;

   /* On old libGL's for linux we need to invalidate the drawables
    * on glViewpport calls, this is set via a option.
    */
   boolean invalidate_on_gl_viewport;
   boolean draw_needs_minmax_index;
   boolean vertex_array_out_of_memory;
   boolean has_hw_atomics;

   /* Some state is contained in constant objects.
    * Other state is just parameter values.
    */
   struct {
      struct pipe_blend_state               blend;
      struct pipe_depth_stencil_alpha_state depth_stencil;
      struct pipe_rasterizer_state          rasterizer;
      struct pipe_sampler_state frag_samplers[PIPE_MAX_SAMPLERS];
      GLuint num_frag_samplers;
      struct pipe_sampler_view *frag_sampler_views[PIPE_MAX_SAMPLERS];
      GLuint num_sampler_views[PIPE_SHADER_TYPES];
      struct pipe_clip_state clip;
      struct {
         void *ptr;
         unsigned size;
      } constants[PIPE_SHADER_TYPES];
      unsigned fb_width;
      unsigned fb_height;
      unsigned fb_num_samples;
      unsigned fb_num_layers;
      unsigned fb_num_cb;
      unsigned num_viewports;
      struct pipe_scissor_state scissor[PIPE_MAX_VIEWPORTS];
      struct pipe_viewport_state viewport[PIPE_MAX_VIEWPORTS];
      struct {
         unsigned num;
         boolean include;
         struct pipe_scissor_state rects[PIPE_MAX_WINDOW_RECTANGLES];
      } window_rects;

      GLuint poly_stipple[32];  /**< In OpenGL's bottom-to-top order */

      GLuint fb_orientation;
   } state;

   uint64_t dirty; /**< dirty states */

   /** This masks out unused shader resources. Only valid in draw calls. */
   uint64_t active_states;

   /* If true, further analysis of states is required to know if something
    * has changed. Used mainly for shaders.
    */
   bool gfx_shaders_may_be_dirty;
   bool compute_shader_may_be_dirty;

   GLboolean vertdata_edgeflags;
   GLboolean edgeflag_culls_prims;

   struct st_vertex_program *vp;    /**< Currently bound vertex program */
   struct st_fragment_program *fp;  /**< Currently bound fragment program */
   struct st_common_program *gp;  /**< Currently bound geometry program */
   struct st_common_program *tcp; /**< Currently bound tess control program */
   struct st_common_program *tep; /**< Currently bound tess eval program */
   struct st_compute_program *cp;   /**< Currently bound compute program */

   struct st_vp_variant *vp_variant;

   struct {
      struct pipe_resource *pixelmap_texture;
      struct pipe_sampler_view *pixelmap_sampler_view;
   } pixel_xfer;

   /** for glBitmap */
   struct {
      struct pipe_rasterizer_state rasterizer;
      struct pipe_sampler_state sampler;
      struct pipe_sampler_state atlas_sampler;
      enum pipe_format tex_format;
      void *vs;
      struct st_bitmap_cache cache;
   } bitmap;

   /** for glDraw/CopyPixels */
   struct {
      void *zs_shaders[4];
      void *vert_shaders[2];   /**< ureg shaders */
   } drawpix;

   /** Cache of glDrawPixels images */
   struct {
      struct drawpix_cache_entry entries[NUM_DRAWPIX_CACHE_ENTRIES];
      unsigned age;
   } drawpix_cache;

   /** for glReadPixels */
   struct {
      struct pipe_resource *src;
      struct pipe_resource *cache;
      enum pipe_format dst_format;
      unsigned level;
      unsigned layer;
      unsigned hits;
   } readpix_cache;

   /** for glClear */
   struct {
      struct pipe_rasterizer_state raster;
      struct pipe_viewport_state viewport;
      void *vs;
      void *fs;
      void *vs_layered;
      void *gs_layered;
   } clear;

   /* For gl(Compressed)Tex(Sub)Image */
   struct {
      struct pipe_rasterizer_state raster;
      struct pipe_blend_state upload_blend;
      void *vs;
      void *gs;
      void *upload_fs[3];
      void *download_fs[3][PIPE_MAX_TEXTURE_TYPES];
      bool upload_enabled;
      bool download_enabled;
      bool rgba_only;
      bool layers;
      bool use_gs;
   } pbo;

   /** for drawing with st_util_vertex */
   struct pipe_vertex_element util_velems[3];

   void *passthrough_fs;  /**< simple pass-through frag shader */

   enum pipe_texture_target internal_target;

   struct cso_context *cso_context;

   void *winsys_drawable_handle;

   /* The number of vertex buffers from the last call of validate_arrays. */
   unsigned last_num_vbuffers;

   int32_t draw_stamp;
   int32_t read_stamp;

   struct st_config_options options;

   struct st_perf_monitor_group *perfmon;

   enum pipe_reset_status reset_status;

   /* Array of bound texture/image handles which are resident in the context.
    */
   struct st_bound_handles bound_texture_handles[PIPE_SHADER_TYPES];
   struct st_bound_handles bound_image_handles[PIPE_SHADER_TYPES];

   /* Winsys buffers */
   struct list_head winsys_buffers;

   /* For the initial pushdown, keep the list of vbo inputs. */
   struct vbo_inputs draw_arrays;
};


/* Need this so that we can implement Mesa callbacks in this module.
 */
static inline struct st_context *st_context(struct gl_context *ctx)
{
   return ctx->st;
}


/**
 * Wrapper for struct gl_framebuffer.
 * This is an opaque type to the outside world.
 */
struct st_framebuffer
{
   struct gl_framebuffer Base;

   struct st_framebuffer_iface *iface;
   enum st_attachment_type statts[ST_ATTACHMENT_COUNT];
   unsigned num_statts;
   int32_t stamp;
   int32_t iface_stamp;
   uint32_t iface_ID;

   /* list of framebuffer objects */
   struct list_head head;
};


extern void st_init_driver_functions(struct pipe_screen *screen,
                                     struct dd_function_table *functions);

void
st_invalidate_buffers(struct st_context *st);

/* Invalidate the readpixels cache to ensure we don't read stale data.
 */
static inline void
st_invalidate_readpix_cache(struct st_context *st)
{
   if (unlikely(st->readpix_cache.src)) {
      pipe_resource_reference(&st->readpix_cache.src, NULL);
      pipe_resource_reference(&st->readpix_cache.cache, NULL);
   }
}


#define Y_0_TOP 1
#define Y_0_BOTTOM 2

static inline GLuint
st_fb_orientation(const struct gl_framebuffer *fb)
{
   if (fb && _mesa_is_winsys_fbo(fb)) {
      /* Drawing into a window (on-screen buffer).
       *
       * Negate Y scale to flip image vertically.
       * The NDC Y coords prior to viewport transformation are in the range
       * [y=-1=bottom, y=1=top]
       * Hardware window coords are in the range [y=0=top, y=H-1=bottom] where
       * H is the window height.
       * Use the viewport transformation to invert Y.
       */
      return Y_0_TOP;
   }
   else {
      /* Drawing into user-created FBO (very likely a texture).
       *
       * For textures, T=0=Bottom, so by extension Y=0=Bottom for rendering.
       */
      return Y_0_BOTTOM;
   }
}


static inline bool
st_user_clip_planes_enabled(struct gl_context *ctx)
{
   return (ctx->API == API_OPENGL_COMPAT ||
           ctx->API == API_OPENGLES) && /* only ES 1.x */
          ctx->Transform.ClipPlanesEnabled;
}

/** clear-alloc a struct-sized object, with casting */
#define ST_CALLOC_STRUCT(T)   (struct T *) calloc(1, sizeof(struct T))


extern struct st_context *
st_create_context(gl_api api, struct pipe_context *pipe,
                  const struct gl_config *visual,
                  struct st_context *share,
                  const struct st_config_options *options,
                  bool no_error);

extern void
st_destroy_context(struct st_context *st);

uint64_t
st_get_active_states(struct gl_context *ctx);


#ifdef __cplusplus
}
#endif

#endif
