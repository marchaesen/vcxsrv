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
#include "main/atifragshader.h"
#include "program/program.h"
#include "pipe/p_state.h"
#include "tgsi/tgsi_from_mesa.h"
#include "st_context.h"
#include "st_texture.h"
#include "st_glsl_to_tgsi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ST_DOUBLE_ATTRIB_PLACEHOLDER 0xff

struct st_external_sampler_key
{
   GLuint lower_nv12;             /**< bitmask of 2 plane YUV samplers */
   GLuint lower_iyuv;             /**< bitmask of 3 plane YUV samplers */
   GLuint lower_xy_uxvx;          /**< bitmask of 2 plane YUV samplers */
   GLuint lower_yx_xuxv;          /**< bitmask of 2 plane YUV samplers */
   GLuint lower_ayuv;
   GLuint lower_xyuv;
   GLuint lower_yuv;
};

static inline struct st_external_sampler_key
st_get_external_sampler_key(struct st_context *st, struct gl_program *prog)
{
   unsigned mask = prog->ExternalSamplersUsed;
   struct st_external_sampler_key key;

   memset(&key, 0, sizeof(key));

   while (unlikely(mask)) {
      unsigned unit = u_bit_scan(&mask);
      struct st_texture_object *stObj =
            st_get_texture_object(st->ctx, prog, unit);
      enum pipe_format format = st_get_view_format(stObj);

      /* if resource format matches then YUV wasn't lowered */
      if (format == stObj->pt->format)
         continue;

      switch (format) {
      case PIPE_FORMAT_NV12:
         if (stObj->pt->format == PIPE_FORMAT_R8_G8B8_420_UNORM) {
            key.lower_yuv |= (1 << unit);
            break;
         }
         /* fallthrough */
      case PIPE_FORMAT_P010:
      case PIPE_FORMAT_P012:
      case PIPE_FORMAT_P016:
         key.lower_nv12 |= (1 << unit);
         break;
      case PIPE_FORMAT_IYUV:
         key.lower_iyuv |= (1 << unit);
         break;
      case PIPE_FORMAT_YUYV:
         key.lower_yx_xuxv |= (1 << unit);
         break;
      case PIPE_FORMAT_UYVY:
         key.lower_xy_uxvx |= (1 << unit);
         break;
      case PIPE_FORMAT_AYUV:
         key.lower_ayuv |= (1 << unit);
         break;
      case PIPE_FORMAT_XYUV:
         key.lower_xyuv |= (1 << unit);
         break;
      default:
         printf("mesa: st_get_external_sampler_key: unhandled pipe format %u\n",
                format);
         break;
      }
   }

   return key;
}

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

   /** needed for ATI_fragment_shader */
   GLuint fog:2;

   /** for ARB_depth_clamp */
   GLuint lower_depth_clamp:1;

   /** for OpenGL 1.0 on modern hardware */
   GLuint lower_two_sided_color:1;

   GLuint lower_flatshade:1;
   unsigned lower_alpha_func:3;

   /** needed for ATI_fragment_shader */
   char texture_targets[MAX_NUM_FRAGMENT_REGISTERS_ATI];

   struct st_external_sampler_key external;
};

/**
 * Base class for shader variants.
 */
struct st_variant
{
   /** next in linked list */
   struct st_variant *next;

   /** st_context from the shader key */
   struct st_context *st;

   void *driver_shader;
};

/**
 * Variant of a fragment program.
 */
struct st_fp_variant
{
   struct st_variant base;

   /** Parameters which generated this version of fragment program */
   struct st_fp_variant_key key;

   /** For glBitmap variants */
   uint bitmap_sampler;

   /** For glDrawPixels variants */
   unsigned drawpix_sampler;
   unsigned pixelmap_sampler;
};


/** Shader key shared by other shaders */
struct st_common_variant_key
{
   struct st_context *st;          /**< variants are per-context */
   bool passthrough_edgeflags;

   /** for ARB_color_buffer_float */
   bool clamp_color;

   /** both for ARB_depth_clamp */
   bool lower_depth_clamp;
   bool clip_negative_one_to_one;

   /** lower glPointSize to gl_PointSize */
   boolean lower_point_size;

   /* for user-defined clip-planes */
   uint8_t lower_ucp;

   /* Whether st_variant::driver_shader is for the draw module,
    * not for the driver.
    */
   bool is_draw_shader;
};


/**
 * Common shader variant.
 */
struct st_common_variant
{
   struct st_variant base;

   /* Parameters which generated this variant. */
   struct st_common_variant_key key;

   /* Bitfield of VERT_BIT_* bits matching vertex shader inputs,
    * but not include the high part of doubles.
    */
   GLbitfield vert_attrib_mask;
};


/**
 * Derived from Mesa gl_program:
 */
struct st_program
{
   struct gl_program Base;
   struct pipe_shader_state state;
   struct glsl_to_tgsi_visitor* glsl_to_tgsi;
   struct ati_fragment_shader *ati_fs;
   uint64_t affected_states; /**< ST_NEW_* flags to mark dirty when binding */

   void *serialized_nir;
   unsigned serialized_nir_size;

   /* used when bypassing glsl_to_tgsi: */
   struct gl_shader_program *shader_program;

   struct st_variant *variants;
};


struct st_vertex_program
{
   struct st_program Base;

   /** maps a TGSI input index back to a Mesa VERT_ATTRIB_x */
   ubyte index_to_input[PIPE_MAX_ATTRIBS];
   ubyte num_inputs;
   /** Reverse mapping of the above */
   ubyte input_to_index[VERT_ATTRIB_MAX];

   /** Maps VARYING_SLOT_x to slot */
   ubyte result_to_output[VARYING_SLOT_MAX];
};


static inline struct st_program *
st_program( struct gl_program *cp )
{
   return (struct st_program *)cp;
}

static inline void
st_reference_prog(struct st_context *st,
                  struct st_program **ptr,
                  struct st_program *prog)
{
   _mesa_reference_program(st->ctx,
                           (struct gl_program **) ptr,
                           (struct gl_program *) prog);
}

static inline struct st_common_variant *
st_common_variant(struct st_variant *v)
{
   return (struct st_common_variant*)v;
}

static inline struct st_fp_variant *
st_fp_variant(struct st_variant *v)
{
   return (struct st_fp_variant*)v;
}

/**
 * This defines mapping from Mesa VARYING_SLOTs to TGSI GENERIC slots.
 */
static inline unsigned
st_get_generic_varying_index(struct st_context *st, GLuint attr)
{
   return tgsi_get_generic_gl_varying_index((gl_varying_slot)attr,
                                            st->needs_texcoord_semantic);
}

extern void
st_set_prog_affected_state_flags(struct gl_program *prog);

extern struct st_common_variant *
st_get_vp_variant(struct st_context *st,
                  struct st_program *stvp,
                  const struct st_common_variant_key *key);


extern struct st_fp_variant *
st_get_fp_variant(struct st_context *st,
                  struct st_program *stfp,
                  const struct st_fp_variant_key *key);

extern struct st_variant *
st_get_common_variant(struct st_context *st,
                      struct st_program *p,
                      const struct st_common_variant_key *key);

extern void
st_release_variants(struct st_context *st, struct st_program *p);

extern void
st_release_program(struct st_context *st, struct st_program **p);

extern void
st_destroy_program_variants(struct st_context *st);

extern void
st_finalize_nir_before_variants(struct nir_shader *nir);

extern void
st_prepare_vertex_program(struct st_program *stvp);

extern void
st_translate_stream_output_info(struct gl_program *prog);

extern bool
st_translate_vertex_program(struct st_context *st,
                            struct st_program *stvp);

extern bool
st_translate_fragment_program(struct st_context *st,
                              struct st_program *stfp);

extern bool
st_translate_common_program(struct st_context *st,
                            struct st_program *stp);

extern void
st_serialize_nir(struct st_program *stp);

extern void
st_finalize_program(struct st_context *st, struct gl_program *prog);

#ifdef __cplusplus
}
#endif

#endif
