/*
 * Copyright © 2016 Intel Corporation
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
 */

#ifndef SHADER_INFO_H
#define SHADER_INFO_H

#include "shader_enums.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spirv_supported_capabilities {
   bool float64;
   bool image_ms_array;
   bool tessellation;
   bool device_group;
   bool draw_parameters;
   bool image_read_without_format;
   bool image_write_without_format;
   bool int64;
   bool multiview;
   bool variable_pointers;
   bool storage_16bit;
   bool int16;
   bool shader_viewport_index_layer;
   bool subgroup_arithmetic;
   bool subgroup_ballot;
   bool subgroup_basic;
   bool subgroup_quad;
   bool subgroup_shuffle;
   bool subgroup_vote;
   bool gcn_shader;
   bool trinary_minmax;
   bool descriptor_array_dynamic_indexing;
   bool runtime_descriptor_array;
};

typedef struct shader_info {
   const char *name;

   /* Descriptive name provided by the client; may be NULL */
   const char *label;

   /** The shader stage, such as MESA_SHADER_VERTEX. */
   gl_shader_stage stage;

   /** The shader stage in a non SSO linked program that follows this stage,
     * such as MESA_SHADER_FRAGMENT.
     */
   gl_shader_stage next_stage;

   /* Number of textures used by this shader */
   unsigned num_textures;
   /* Number of uniform buffers used by this shader */
   unsigned num_ubos;
   /* Number of atomic buffers used by this shader */
   unsigned num_abos;
   /* Number of shader storage buffers used by this shader */
   unsigned num_ssbos;
   /* Number of images used by this shader */
   unsigned num_images;

   /* Which inputs are actually read */
   uint64_t inputs_read;
   /* Which outputs are actually written */
   uint64_t outputs_written;
   /* Which outputs are actually read */
   uint64_t outputs_read;
   /* Which system values are actually read */
   uint64_t system_values_read;

   /* Which patch inputs are actually read */
   uint32_t patch_inputs_read;
   /* Which patch outputs are actually written */
   uint32_t patch_outputs_written;
   /* Which patch outputs are read */
   uint32_t patch_outputs_read;

   /* Whether or not this shader ever uses textureGather() */
   bool uses_texture_gather;

   /** Bitfield of which textures are used by texelFetch() */
   uint32_t textures_used_by_txf;

   /**
    * True if this shader uses the fddx/fddy opcodes.
    *
    * Note that this does not include the "fine" and "coarse" variants.
    */
   bool uses_fddx_fddy;

   /* The size of the gl_ClipDistance[] array, if declared. */
   unsigned clip_distance_array_size;

   /* The size of the gl_CullDistance[] array, if declared. */
   unsigned cull_distance_array_size;

   /* Whether or not separate shader objects were used */
   bool separate_shader;

   /** Was this shader linked with any transform feedback varyings? */
   bool has_transform_feedback_varyings;

   union {
      struct {
         /* Which inputs are doubles */
         uint64_t double_inputs;

         /* Which inputs are actually read and are double */
         uint64_t double_inputs_read;
      } vs;

      struct {
         /** The number of vertices recieves per input primitive */
         unsigned vertices_in;

         /** The output primitive type (GL enum value) */
         unsigned output_primitive;

         /** The input primitive type (GL enum value) */
         unsigned input_primitive;

         /** The maximum number of vertices the geometry shader might write. */
         unsigned vertices_out;

         /** 1 .. MAX_GEOMETRY_SHADER_INVOCATIONS */
         unsigned invocations;

         /** Whether or not this shader uses EndPrimitive */
         bool uses_end_primitive;

         /** Whether or not this shader uses non-zero streams */
         bool uses_streams;
      } gs;

      struct {
         bool uses_discard;

         /**
          * Whether any inputs are declared with the "sample" qualifier.
          */
         bool uses_sample_qualifier;

         /**
          * Whether early fragment tests are enabled as defined by
          * ARB_shader_image_load_store.
          */
         bool early_fragment_tests;

         /**
          * Defined by INTEL_conservative_rasterization.
          */
         bool inner_coverage;

         bool post_depth_coverage;

         bool pixel_center_integer;

         bool pixel_interlock_ordered;
         bool pixel_interlock_unordered;
         bool sample_interlock_ordered;
         bool sample_interlock_unordered;

         /** gl_FragDepth layout for ARB_conservative_depth. */
         enum gl_frag_depth_layout depth_layout;
      } fs;

      struct {
         unsigned local_size[3];

         bool local_size_variable;

         /**
          * Size of shared variables accessed by the compute shader.
          */
         unsigned shared_size;
      } cs;

      /* Applies to both TCS and TES. */
      struct {
         /** The number of vertices in the TCS output patch. */
         unsigned tcs_vertices_out;

         uint32_t primitive_mode; /* GL_TRIANGLES, GL_QUADS or GL_ISOLINES */
         enum gl_tess_spacing spacing;
         /** Is the vertex order counterclockwise? */
         bool ccw;
         bool point_mode;
      } tess;
   };
} shader_info;

#ifdef __cplusplus
}
#endif

#endif /* SHADER_INFO_H */
