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

 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */
    

#ifndef ST_PROGRAM_H
#define ST_PROGRAM_H

#include "main/mtypes.h"
#include "program/program.h"
#include "pipe/p_state.h"
#include "st_context.h"
#include "st_glsl_to_tgsi.h"


#ifdef __cplusplus
extern "C" {
#endif

#define ST_DOUBLE_ATTRIB_PLACEHOLDER 0xffffffff

/** Fragment program variant key */
struct st_fp_variant_key
{
   struct st_context *st;         /**< variants are per-context */

   /** for glBitmap */
   GLuint bitmap:1;               /**< glBitmap variant? */

   /** for glDrawPixels */
   GLuint drawpixels:1;           /**< glDrawPixels variant */
   GLuint scaleAndBias:1;         /**< glDrawPixels w/ scale and/or bias? */
   GLuint pixelMaps:1;            /**< glDrawPixels w/ pixel lookup map? */

   /** for ARB_color_buffer_float */
   GLuint clamp_color:1;

   /** for ARB_sample_shading */
   GLuint persample_shading:1;
};


/**
 * Variant of a fragment program.
 */
struct st_fp_variant
{
   /** Parameters which generated this version of fragment program */
   struct st_fp_variant_key key;

   /** Driver's compiled shader */
   void *driver_shader;

   /** For glBitmap variants */
   uint bitmap_sampler;

   /** For glDrawPixels variants */
   unsigned drawpix_sampler;
   unsigned pixelmap_sampler;

   /** next in linked list */
   struct st_fp_variant *next;
};


/**
 * Derived from Mesa gl_fragment_program:
 */
struct st_fragment_program
{
   struct gl_fragment_program Base;
   struct pipe_shader_state tgsi;
   struct glsl_to_tgsi_visitor* glsl_to_tgsi;

   struct st_fp_variant *variants;
};



/** Vertex program variant key */
struct st_vp_variant_key
{
   struct st_context *st;          /**< variants are per-context */
   boolean passthrough_edgeflags;

   /** for ARB_color_buffer_float */
   boolean clamp_color;
};


/**
 * This represents a vertex program, especially translated to match
 * the inputs of a particular fragment shader.
 */
struct st_vp_variant
{
   /* Parameters which generated this translated version of a vertex
    * shader:
    */
   struct st_vp_variant_key key;

   /**
    * TGSI tokens (to later generate a 'draw' module shader for
    * selection/feedback/rasterpos)
    */
   struct pipe_shader_state tgsi;

   /** Driver's compiled shader */
   void *driver_shader;

   /** For using our private draw module (glRasterPos) */
   struct draw_vertex_shader *draw_shader;

   /** Next in linked list */
   struct st_vp_variant *next;  

   /** similar to that in st_vertex_program, but with edgeflags info too */
   GLuint num_inputs;
};


/**
 * Derived from Mesa gl_fragment_program:
 */
struct st_vertex_program
{
   struct gl_vertex_program Base;  /**< The Mesa vertex program */
   struct pipe_shader_state tgsi;
   struct glsl_to_tgsi_visitor* glsl_to_tgsi;

   /** maps a Mesa VERT_ATTRIB_x to a packed TGSI input index */
   /** maps a TGSI input index back to a Mesa VERT_ATTRIB_x */
   GLuint index_to_input[PIPE_MAX_SHADER_INPUTS];
   GLuint num_inputs;

   /** Maps VARYING_SLOT_x to slot */
   GLuint result_to_output[VARYING_SLOT_MAX];

   /** List of translated variants of this vertex program.
    */
   struct st_vp_variant *variants;
};



/** Key shared by all shaders except VP, FP */
struct st_basic_variant_key
{
   struct st_context *st;          /**< variants are per-context */
};


/**
 * Geometry program variant.
 */
struct st_basic_variant
{
   /* Parameters which generated this variant. */
   struct st_basic_variant_key key;

   void *driver_shader;

   struct st_basic_variant *next;
};


/**
 * Derived from Mesa gl_geometry_program:
 */
struct st_geometry_program
{
   struct gl_geometry_program Base;  /**< The Mesa geometry program */
   struct pipe_shader_state tgsi;
   struct glsl_to_tgsi_visitor* glsl_to_tgsi;

   struct st_basic_variant *variants;
};


/**
 * Derived from Mesa gl_tess_ctrl_program:
 */
struct st_tessctrl_program
{
   struct gl_tess_ctrl_program Base;  /**< The Mesa tess ctrl program */
   struct pipe_shader_state tgsi;
   struct glsl_to_tgsi_visitor* glsl_to_tgsi;

   struct st_basic_variant *variants;
};


/**
 * Derived from Mesa gl_tess_eval_program:
 */
struct st_tesseval_program
{
   struct gl_tess_eval_program Base;  /**< The Mesa tess eval program */
   struct pipe_shader_state tgsi;
   struct glsl_to_tgsi_visitor* glsl_to_tgsi;

   struct st_basic_variant *variants;
};


/**
 * Derived from Mesa gl_compute_program:
 */
struct st_compute_program
{
   struct gl_compute_program Base;  /**< The Mesa compute program */
   struct pipe_compute_state tgsi;
   struct glsl_to_tgsi_visitor* glsl_to_tgsi;

   struct st_basic_variant *variants;
};


static inline struct st_fragment_program *
st_fragment_program( struct gl_fragment_program *fp )
{
   return (struct st_fragment_program *)fp;
}


static inline struct st_vertex_program *
st_vertex_program( struct gl_vertex_program *vp )
{
   return (struct st_vertex_program *)vp;
}

static inline struct st_geometry_program *
st_geometry_program( struct gl_geometry_program *gp )
{
   return (struct st_geometry_program *)gp;
}

static inline struct st_tessctrl_program *
st_tessctrl_program( struct gl_tess_ctrl_program *tcp )
{
   return (struct st_tessctrl_program *)tcp;
}

static inline struct st_tesseval_program *
st_tesseval_program( struct gl_tess_eval_program *tep )
{
   return (struct st_tesseval_program *)tep;
}

static inline struct st_compute_program *
st_compute_program( struct gl_compute_program *cp )
{
   return (struct st_compute_program *)cp;
}

static inline void
st_reference_vertprog(struct st_context *st,
                      struct st_vertex_program **ptr,
                      struct st_vertex_program *prog)
{
   _mesa_reference_program(st->ctx,
                           (struct gl_program **) ptr,
                           (struct gl_program *) prog);
}

static inline void
st_reference_geomprog(struct st_context *st,
                      struct st_geometry_program **ptr,
                      struct st_geometry_program *prog)
{
   _mesa_reference_program(st->ctx,
                           (struct gl_program **) ptr,
                           (struct gl_program *) prog);
}

static inline void
st_reference_fragprog(struct st_context *st,
                      struct st_fragment_program **ptr,
                      struct st_fragment_program *prog)
{
   _mesa_reference_program(st->ctx,
                           (struct gl_program **) ptr,
                           (struct gl_program *) prog);
}

static inline void
st_reference_tesscprog(struct st_context *st,
                       struct st_tessctrl_program **ptr,
                       struct st_tessctrl_program *prog)
{
   _mesa_reference_program(st->ctx,
                           (struct gl_program **) ptr,
                           (struct gl_program *) prog);
}

static inline void
st_reference_tesseprog(struct st_context *st,
                       struct st_tesseval_program **ptr,
                       struct st_tesseval_program *prog)
{
   _mesa_reference_program(st->ctx,
                           (struct gl_program **) ptr,
                           (struct gl_program *) prog);
}

static inline void
st_reference_compprog(struct st_context *st,
                      struct st_compute_program **ptr,
                      struct st_compute_program *prog)
{
   _mesa_reference_program(st->ctx,
                           (struct gl_program **) ptr,
                           (struct gl_program *) prog);
}

/**
 * This defines mapping from Mesa VARYING_SLOTs to TGSI GENERIC slots.
 */
static inline unsigned
st_get_generic_varying_index(struct st_context *st, GLuint attr)
{
   if (attr >= VARYING_SLOT_VAR0) {
      if (st->needs_texcoord_semantic)
         return attr - VARYING_SLOT_VAR0;
      else
         return 9 + (attr - VARYING_SLOT_VAR0);
   }
   if (attr == VARYING_SLOT_PNTC) {
      assert(!st->needs_texcoord_semantic);
      return 8;
   }
   if (attr >= VARYING_SLOT_TEX0 && attr <= VARYING_SLOT_TEX7) {
      assert(!st->needs_texcoord_semantic);
      return attr - VARYING_SLOT_TEX0;
   }

   assert(0);
   return 0;
}


extern struct st_vp_variant *
st_get_vp_variant(struct st_context *st,
                  struct st_vertex_program *stvp,
                  const struct st_vp_variant_key *key);


extern struct st_fp_variant *
st_get_fp_variant(struct st_context *st,
                  struct st_fragment_program *stfp,
                  const struct st_fp_variant_key *key);

extern struct st_basic_variant *
st_get_cp_variant(struct st_context *st,
                  struct pipe_compute_state *tgsi,
                  struct st_basic_variant **variants);

extern struct st_basic_variant *
st_get_basic_variant(struct st_context *st,
                     unsigned pipe_shader,
                     struct pipe_shader_state *tgsi,
                     struct st_basic_variant **variants);

extern void
st_release_vp_variants( struct st_context *st,
                        struct st_vertex_program *stvp );

extern void
st_release_fp_variants( struct st_context *st,
                        struct st_fragment_program *stfp );

extern void
st_release_cp_variants(struct st_context *st,
                        struct st_compute_program *stcp);

extern void
st_release_basic_variants(struct st_context *st, GLenum target,
                          struct st_basic_variant **variants,
                          struct pipe_shader_state *tgsi);

extern void
st_destroy_program_variants(struct st_context *st);

extern bool
st_translate_vertex_program(struct st_context *st,
                            struct st_vertex_program *stvp);

extern bool
st_translate_fragment_program(struct st_context *st,
                              struct st_fragment_program *stfp);

extern bool
st_translate_geometry_program(struct st_context *st,
                              struct st_geometry_program *stgp);

extern bool
st_translate_tessctrl_program(struct st_context *st,
                              struct st_tessctrl_program *sttcp);

extern bool
st_translate_tesseval_program(struct st_context *st,
                              struct st_tesseval_program *sttep);

extern bool
st_translate_compute_program(struct st_context *st,
                             struct st_compute_program *stcp);

extern void
st_print_current_vertex_program(void);

extern void
st_precompile_shader_variant(struct st_context *st,
                             struct gl_program *prog);

#ifdef __cplusplus
}
#endif

#endif
