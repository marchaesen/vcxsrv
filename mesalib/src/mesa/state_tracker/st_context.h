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
#include "pipe/p_state.h"
#include "state_tracker/st_api.h"
#include "main/fbobject.h"


#ifdef __cplusplus
extern "C" {
#endif


struct bitmap_cache;
struct dd_function_table;
struct draw_context;
struct draw_stage;
struct gen_mipmap_state;
struct st_context;
struct st_fragment_program;
struct st_perf_monitor_group;
struct u_upload_mgr;


/* gap  */
#define ST_NEW_FRAGMENT_PROGRAM        (1 << 1)
#define ST_NEW_VERTEX_PROGRAM          (1 << 2)
#define ST_NEW_FRAMEBUFFER             (1 << 3)
#define ST_NEW_TESS_STATE              (1 << 4)
#define ST_NEW_GEOMETRY_PROGRAM        (1 << 5)
#define ST_NEW_VERTEX_ARRAYS           (1 << 6)
#define ST_NEW_RASTERIZER              (1 << 7)
#define ST_NEW_UNIFORM_BUFFER          (1 << 8)
#define ST_NEW_TESSCTRL_PROGRAM        (1 << 9)
#define ST_NEW_TESSEVAL_PROGRAM        (1 << 10)
#define ST_NEW_SAMPLER_VIEWS           (1 << 11)
#define ST_NEW_ATOMIC_BUFFER           (1 << 12)
#define ST_NEW_STORAGE_BUFFER          (1 << 13)
#define ST_NEW_COMPUTE_PROGRAM         (1 << 14)
#define ST_NEW_IMAGE_UNITS             (1 << 15)


struct st_state_flags {
   GLbitfield mesa;  /**< Mask of _NEW_x flags */
   uint64_t st;      /**< Mask of ST_NEW_x flags */
};

struct st_tracked_state {
   const char *name;
   struct st_state_flags dirty;
   void (*update)( struct st_context *st );
};


/**
 * Enumeration of state tracker pipelines.
 */
enum st_pipeline {
   ST_PIPELINE_RENDER,
   ST_PIPELINE_COMPUTE,
};


/** For drawing quads for glClear, glDraw/CopyPixels, glBitmap, etc. */
struct st_util_vertex
{
   float x, y, z;
   float r, g, b, a;
   float s, t;
};


struct st_context
{
   struct st_context_iface iface;

   struct gl_context *ctx;

   struct pipe_context *pipe;

   struct u_upload_mgr *uploader, *indexbuf_uploader, *constbuf_uploader;

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

   boolean vertex_array_out_of_memory;

   /* Some state is contained in constant objects.
    * Other state is just parameter values.
    */
   struct {
      struct pipe_blend_state               blend;
      struct pipe_depth_stencil_alpha_state depth_stencil;
      struct pipe_rasterizer_state          rasterizer;
      struct pipe_sampler_state samplers[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      GLuint num_samplers[PIPE_SHADER_TYPES];
      struct pipe_sampler_view *sampler_views[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      GLuint num_sampler_views[PIPE_SHADER_TYPES];
      struct pipe_clip_state clip;
      struct {
         void *ptr;
         unsigned size;
      } constants[PIPE_SHADER_TYPES];
      struct pipe_framebuffer_state framebuffer;
      struct pipe_scissor_state scissor[PIPE_MAX_VIEWPORTS];
      struct pipe_viewport_state viewport[PIPE_MAX_VIEWPORTS];
      struct {
         unsigned num;
         boolean include;
         struct pipe_scissor_state rects[PIPE_MAX_WINDOW_RECTANGLES];
      } window_rects;
      unsigned sample_mask;

      GLuint poly_stipple[32];  /**< In OpenGL's bottom-to-top order */

      GLuint fb_orientation;
   } state;

   char vendor[100];
   char renderer[100];

   struct st_state_flags dirty;
   struct st_state_flags dirty_cp;

   GLboolean vertdata_edgeflags;
   GLboolean edgeflag_culls_prims;

   /** Mapping from VARYING_SLOT_x to post-transformed vertex slot */
   const GLuint *vertex_result_to_slot;

   struct st_vertex_program *vp;    /**< Currently bound vertex program */
   struct st_fragment_program *fp;  /**< Currently bound fragment program */
   struct st_geometry_program *gp;  /**< Currently bound geometry program */
   struct st_tessctrl_program *tcp; /**< Currently bound tess control program */
   struct st_tesseval_program *tep; /**< Currently bound tess eval program */
   struct st_compute_program *cp;   /**< Currently bound compute program */

   struct st_vp_variant *vp_variant;
   struct st_fp_variant *fp_variant;
   struct st_basic_variant *gp_variant;
   struct st_basic_variant *tcp_variant;
   struct st_basic_variant *tep_variant;
   struct st_basic_variant *cp_variant;

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
      struct bitmap_cache *cache;
   } bitmap;

   /** for glDraw/CopyPixels */
   struct {
      void *zs_shaders[4];
      void *vert_shaders[2];   /**< ureg shaders */
   } drawpix;

   struct {
      GLsizei width, height;
      GLenum format, type;
      const void *user_pointer;  /**< Last user 'pixels' pointer */
      void *image;               /**< Copy of the glDrawPixels image data */
      struct pipe_resource *texture;
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
      void *upload_fs;
      void *download_fs[PIPE_MAX_TEXTURE_TYPES];
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
   void *Private;

   struct st_framebuffer_iface *iface;
   enum st_attachment_type statts[ST_ATTACHMENT_COUNT];
   unsigned num_statts;
   int32_t stamp;
   int32_t iface_stamp;
};


extern void st_init_driver_functions(struct pipe_screen *screen,
                                     struct dd_function_table *functions);

void st_invalidate_state(struct gl_context * ctx, GLbitfield new_state);

void st_invalidate_readpix_cache(struct st_context *st);


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


static inline unsigned
st_shader_stage_to_ptarget(gl_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
      return PIPE_SHADER_VERTEX;
   case MESA_SHADER_FRAGMENT:
      return PIPE_SHADER_FRAGMENT;
   case MESA_SHADER_GEOMETRY:
      return PIPE_SHADER_GEOMETRY;
   case MESA_SHADER_TESS_CTRL:
      return PIPE_SHADER_TESS_CTRL;
   case MESA_SHADER_TESS_EVAL:
      return PIPE_SHADER_TESS_EVAL;
   case MESA_SHADER_COMPUTE:
      return PIPE_SHADER_COMPUTE;
   }

   assert(!"should not be reached");
   return PIPE_SHADER_VERTEX;
}


/** clear-alloc a struct-sized object, with casting */
#define ST_CALLOC_STRUCT(T)   (struct T *) calloc(1, sizeof(struct T))


extern struct st_context *
st_create_context(gl_api api, struct pipe_context *pipe,
                  const struct gl_config *visual,
                  struct st_context *share,
                  const struct st_config_options *options);

extern void
st_destroy_context(struct st_context *st);


#ifdef __cplusplus
}
#endif

#endif
