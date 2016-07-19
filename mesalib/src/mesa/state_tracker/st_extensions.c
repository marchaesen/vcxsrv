/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * Copyright (c) 2008 VMware, Inc.
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

#include "main/imports.h"
#include "main/context.h"
#include "main/macros.h"
#include "main/version.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "util/u_math.h"

#include "st_context.h"
#include "st_debug.h"
#include "st_extensions.h"
#include "st_format.h"


/*
 * Note: we use these function rather than the MIN2, MAX2, CLAMP macros to
 * avoid evaluating arguments (which are often function calls) more than once.
 */

static unsigned _min(unsigned a, unsigned b)
{
   return (a < b) ? a : b;
}

static float _maxf(float a, float b)
{
   return (a > b) ? a : b;
}

static int _clamp(int a, int min, int max)
{
   if (a < min)
      return min;
   else if (a > max)
      return max;
   else
      return a;
}


/**
 * Query driver to get implementation limits.
 * Note that we have to limit/clamp against Mesa's internal limits too.
 */
void st_init_limits(struct pipe_screen *screen,
                    struct gl_constants *c, struct gl_extensions *extensions)
{
   int supported_irs;
   unsigned sh;
   boolean can_ubo = TRUE;

   c->MaxTextureLevels
      = _min(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_2D_LEVELS),
            MAX_TEXTURE_LEVELS);

   c->Max3DTextureLevels
      = _min(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_3D_LEVELS),
            MAX_3D_TEXTURE_LEVELS);

   c->MaxCubeTextureLevels
      = _min(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS),
            MAX_CUBE_TEXTURE_LEVELS);

   c->MaxTextureRectSize
      = _min(1 << (c->MaxTextureLevels - 1), MAX_TEXTURE_RECT_SIZE);

   c->MaxArrayTextureLayers
      = screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS);

   /* Define max viewport size and max renderbuffer size in terms of
    * max texture size (note: max tex RECT size = max tex 2D size).
    * If this isn't true for some hardware we'll need new PIPE_CAP_ queries.
    */
   c->MaxViewportWidth =
   c->MaxViewportHeight =
   c->MaxRenderbufferSize = c->MaxTextureRectSize;

   c->MaxDrawBuffers = c->MaxColorAttachments =
      _clamp(screen->get_param(screen, PIPE_CAP_MAX_RENDER_TARGETS),
             1, MAX_DRAW_BUFFERS);

   c->MaxDualSourceDrawBuffers =
      _clamp(screen->get_param(screen,
                               PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS),
             0, MAX_DRAW_BUFFERS);

   c->MaxLineWidth =
      _maxf(1.0f, screen->get_paramf(screen, PIPE_CAPF_MAX_LINE_WIDTH));
   c->MaxLineWidthAA =
      _maxf(1.0f, screen->get_paramf(screen, PIPE_CAPF_MAX_LINE_WIDTH_AA));

   c->MaxPointSize =
      _maxf(1.0f, screen->get_paramf(screen, PIPE_CAPF_MAX_POINT_WIDTH));
   c->MaxPointSizeAA =
      _maxf(1.0f, screen->get_paramf(screen, PIPE_CAPF_MAX_POINT_WIDTH_AA));

   /* these are not queryable. Note that GL basically mandates a 1.0 minimum
    * for non-aa sizes, but we can go down to 0.0 for aa points.
    */
   c->MinPointSize = 1.0f;
   c->MinPointSizeAA = 0.0f;

   c->MaxTextureMaxAnisotropy =
      _maxf(2.0f,
            screen->get_paramf(screen, PIPE_CAPF_MAX_TEXTURE_ANISOTROPY));

   c->MaxTextureLodBias =
      screen->get_paramf(screen, PIPE_CAPF_MAX_TEXTURE_LOD_BIAS);

   c->QuadsFollowProvokingVertexConvention =
      screen->get_param(screen,
                        PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION);

   c->MaxUniformBlockSize =
      screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                               PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE);
   if (c->MaxUniformBlockSize < 16384) {
      can_ubo = FALSE;
   }

   for (sh = 0; sh < PIPE_SHADER_TYPES; ++sh) {
      struct gl_shader_compiler_options *options;
      struct gl_program_constants *pc;

      switch (sh) {
      case PIPE_SHADER_FRAGMENT:
         pc = &c->Program[MESA_SHADER_FRAGMENT];
         options = &c->ShaderCompilerOptions[MESA_SHADER_FRAGMENT];
         break;
      case PIPE_SHADER_VERTEX:
         pc = &c->Program[MESA_SHADER_VERTEX];
         options = &c->ShaderCompilerOptions[MESA_SHADER_VERTEX];
         break;
      case PIPE_SHADER_GEOMETRY:
         pc = &c->Program[MESA_SHADER_GEOMETRY];
         options = &c->ShaderCompilerOptions[MESA_SHADER_GEOMETRY];
         break;
      case PIPE_SHADER_TESS_CTRL:
         pc = &c->Program[MESA_SHADER_TESS_CTRL];
         options = &c->ShaderCompilerOptions[MESA_SHADER_TESS_CTRL];
         break;
      case PIPE_SHADER_TESS_EVAL:
         pc = &c->Program[MESA_SHADER_TESS_EVAL];
         options = &c->ShaderCompilerOptions[MESA_SHADER_TESS_EVAL];
         break;
      case PIPE_SHADER_COMPUTE:
         pc = &c->Program[MESA_SHADER_COMPUTE];
         options = &c->ShaderCompilerOptions[MESA_SHADER_COMPUTE];

         if (!screen->get_param(screen, PIPE_CAP_COMPUTE))
            continue;
         supported_irs =
            screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_SUPPORTED_IRS);
         if (!(supported_irs & (1 << PIPE_SHADER_IR_TGSI)))
            continue;
         break;
      default:
         assert(0);
      }

      pc->MaxTextureImageUnits =
         _min(screen->get_shader_param(screen, sh,
                                       PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS),
              MAX_TEXTURE_IMAGE_UNITS);

      pc->MaxInstructions =
      pc->MaxNativeInstructions =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_INSTRUCTIONS);
      pc->MaxAluInstructions =
      pc->MaxNativeAluInstructions =
         screen->get_shader_param(screen, sh,
                                  PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS);
      pc->MaxTexInstructions =
      pc->MaxNativeTexInstructions =
         screen->get_shader_param(screen, sh,
                                  PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS);
      pc->MaxTexIndirections =
      pc->MaxNativeTexIndirections =
         screen->get_shader_param(screen, sh,
                                  PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS);
      pc->MaxAttribs =
      pc->MaxNativeAttribs =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_INPUTS);
      pc->MaxTemps =
      pc->MaxNativeTemps =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_TEMPS);
      pc->MaxAddressRegs =
      pc->MaxNativeAddressRegs = sh == PIPE_SHADER_VERTEX ? 1 : 0;
      pc->MaxParameters =
      pc->MaxNativeParameters =
         screen->get_shader_param(screen, sh,
                   PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE) / sizeof(float[4]);
      pc->MaxInputComponents =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_INPUTS) * 4;
      pc->MaxOutputComponents =
         screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_MAX_OUTPUTS) * 4;

      pc->MaxUniformComponents =
         4 * MIN2(pc->MaxNativeParameters, MAX_UNIFORMS);

      pc->MaxUniformBlocks =
         screen->get_shader_param(screen, sh,
                                  PIPE_SHADER_CAP_MAX_CONST_BUFFERS);
      if (pc->MaxUniformBlocks)
         pc->MaxUniformBlocks -= 1; /* The first one is for ordinary uniforms. */
      pc->MaxUniformBlocks = _min(pc->MaxUniformBlocks, MAX_UNIFORM_BUFFERS);

      pc->MaxCombinedUniformComponents = (pc->MaxUniformComponents +
                                          c->MaxUniformBlockSize / 4 *
                                          pc->MaxUniformBlocks);

      pc->MaxAtomicCounters = MAX_ATOMIC_COUNTERS;
      pc->MaxAtomicBuffers = screen->get_shader_param(
            screen, sh, PIPE_SHADER_CAP_MAX_SHADER_BUFFERS) / 2;
      pc->MaxShaderStorageBlocks = pc->MaxAtomicBuffers;

      pc->MaxImageUniforms = screen->get_shader_param(
            screen, sh, PIPE_SHADER_CAP_MAX_SHADER_IMAGES);

      /* Gallium doesn't really care about local vs. env parameters so use the
       * same limits.
       */
      pc->MaxLocalParams = MIN2(pc->MaxParameters, MAX_PROGRAM_LOCAL_PARAMS);
      pc->MaxEnvParams = MIN2(pc->MaxParameters, MAX_PROGRAM_ENV_PARAMS);

      if (screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_INTEGERS)) {
         pc->LowInt.RangeMin = 31;
         pc->LowInt.RangeMax = 30;
         pc->LowInt.Precision = 0;
         pc->MediumInt = pc->HighInt = pc->LowInt;
      }

      options->EmitNoNoise = TRUE;

      /* TODO: make these more fine-grained if anyone needs it */
      options->MaxIfDepth =
         screen->get_shader_param(screen, sh,
                                  PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH);
      options->EmitNoLoops =
         !screen->get_shader_param(screen, sh,
                                   PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH);
      options->EmitNoFunctions =
         !screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_SUBROUTINES);
      options->EmitNoMainReturn =
         !screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_SUBROUTINES);

      options->EmitNoCont =
         !screen->get_shader_param(screen, sh,
                                   PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED);

      options->EmitNoIndirectInput =
         !screen->get_shader_param(screen, sh,
                                   PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR);
      options->EmitNoIndirectOutput =
         !screen->get_shader_param(screen, sh,
                                   PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR);
      options->EmitNoIndirectTemp =
         !screen->get_shader_param(screen, sh,
                                   PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR);
      options->EmitNoIndirectUniform =
         !screen->get_shader_param(screen, sh,
                                   PIPE_SHADER_CAP_INDIRECT_CONST_ADDR);

      if (pc->MaxNativeInstructions &&
          (options->EmitNoIndirectUniform || pc->MaxUniformBlocks < 12)) {
         can_ubo = FALSE;
      }

      if (options->EmitNoLoops)
         options->MaxUnrollIterations =
            MIN2(screen->get_shader_param(screen, sh,
                                          PIPE_SHADER_CAP_MAX_INSTRUCTIONS),
                 65536);
      else
         options->MaxUnrollIterations =
            screen->get_shader_param(screen, sh,
                                  PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT);

      options->LowerCombinedClipCullDistance = true;
      options->LowerBufferInterfaceBlocks = true;

      if (sh == PIPE_SHADER_COMPUTE)
         options->LowerShaderSharedVariables = true;
   }

   c->LowerTessLevel = true;
   c->LowerCsDerivedVariables = true;
   c->PrimitiveRestartForPatches =
      screen->get_param(screen, PIPE_CAP_PRIMITIVE_RESTART_FOR_PATCHES);

   c->MaxCombinedTextureImageUnits =
         _min(c->Program[MESA_SHADER_VERTEX].MaxTextureImageUnits +
              c->Program[MESA_SHADER_TESS_CTRL].MaxTextureImageUnits +
              c->Program[MESA_SHADER_TESS_EVAL].MaxTextureImageUnits +
              c->Program[MESA_SHADER_GEOMETRY].MaxTextureImageUnits +
              c->Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits +
              c->Program[MESA_SHADER_COMPUTE].MaxTextureImageUnits,
              MAX_COMBINED_TEXTURE_IMAGE_UNITS);

   /* This depends on program constants. */
   c->MaxTextureCoordUnits
      = _min(c->Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits,
             MAX_TEXTURE_COORD_UNITS);

   c->MaxTextureUnits =
      _min(c->Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits,
           c->MaxTextureCoordUnits);

   c->Program[MESA_SHADER_VERTEX].MaxAttribs =
      MIN2(c->Program[MESA_SHADER_VERTEX].MaxAttribs, 16);

   /* PIPE_SHADER_CAP_MAX_INPUTS for the FS specifies the maximum number
    * of inputs. It's always 2 colors + N generic inputs. */
   c->MaxVarying = screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                            PIPE_SHADER_CAP_MAX_INPUTS);
   c->MaxVarying = MIN2(c->MaxVarying, MAX_VARYING);
   c->MaxGeometryOutputVertices =
      screen->get_param(screen, PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES);
   c->MaxGeometryTotalOutputComponents =
      screen->get_param(screen, PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS);
   c->MaxTessPatchComponents =
      MAX2(screen->get_param(screen, PIPE_CAP_MAX_SHADER_PATCH_VARYINGS),
           MAX_VARYING) * 4;

   c->MinProgramTexelOffset =
      screen->get_param(screen, PIPE_CAP_MIN_TEXEL_OFFSET);
   c->MaxProgramTexelOffset =
      screen->get_param(screen, PIPE_CAP_MAX_TEXEL_OFFSET);

   c->MaxProgramTextureGatherComponents =
      screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS);
   c->MinProgramTextureGatherOffset =
      screen->get_param(screen, PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET);
   c->MaxProgramTextureGatherOffset =
      screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET);

   c->MaxTransformFeedbackBuffers =
      screen->get_param(screen, PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS);
   c->MaxTransformFeedbackBuffers = MIN2(c->MaxTransformFeedbackBuffers,
                                         MAX_FEEDBACK_BUFFERS);
   c->MaxTransformFeedbackSeparateComponents =
      screen->get_param(screen, PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS);
   c->MaxTransformFeedbackInterleavedComponents =
      screen->get_param(screen,
                        PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS);
   c->MaxVertexStreams =
      MAX2(1, screen->get_param(screen, PIPE_CAP_MAX_VERTEX_STREAMS));

   /* The vertex stream must fit into pipe_stream_output_info::stream */
   assert(c->MaxVertexStreams <= 4);

   c->MaxVertexAttribStride
      = screen->get_param(screen, PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE);

   c->StripTextureBorder = GL_TRUE;

   c->GLSLSkipStrictMaxUniformLimitCheck =
      screen->get_param(screen, PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS);

   c->UniformBufferOffsetAlignment =
      screen->get_param(screen, PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT);

   if (can_ubo) {
      extensions->ARB_uniform_buffer_object = GL_TRUE;
      c->MaxCombinedUniformBlocks = c->MaxUniformBufferBindings =
         c->Program[MESA_SHADER_VERTEX].MaxUniformBlocks +
         c->Program[MESA_SHADER_TESS_CTRL].MaxUniformBlocks +
         c->Program[MESA_SHADER_TESS_EVAL].MaxUniformBlocks +
         c->Program[MESA_SHADER_GEOMETRY].MaxUniformBlocks +
         c->Program[MESA_SHADER_FRAGMENT].MaxUniformBlocks +
         c->Program[MESA_SHADER_COMPUTE].MaxUniformBlocks;
      assert(c->MaxCombinedUniformBlocks <= MAX_COMBINED_UNIFORM_BUFFERS);
   }

   c->GLSLFragCoordIsSysVal =
      screen->get_param(screen, PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL);
   c->GLSLFrontFacingIsSysVal =
      screen->get_param(screen, PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL);

   c->MaxAtomicBufferBindings =
         c->Program[MESA_SHADER_FRAGMENT].MaxAtomicBuffers;
   c->MaxCombinedAtomicBuffers =
         c->Program[MESA_SHADER_VERTEX].MaxAtomicBuffers +
         c->Program[MESA_SHADER_TESS_CTRL].MaxAtomicBuffers +
         c->Program[MESA_SHADER_TESS_EVAL].MaxAtomicBuffers +
         c->Program[MESA_SHADER_GEOMETRY].MaxAtomicBuffers +
         c->Program[MESA_SHADER_FRAGMENT].MaxAtomicBuffers;
   assert(c->MaxCombinedAtomicBuffers <= MAX_COMBINED_ATOMIC_BUFFERS);

   if (c->MaxCombinedAtomicBuffers > 0) {
      extensions->ARB_shader_atomic_counters = GL_TRUE;
      extensions->ARB_shader_atomic_counter_ops = GL_TRUE;
   }

   c->MaxCombinedShaderOutputResources = c->MaxDrawBuffers;
   c->ShaderStorageBufferOffsetAlignment =
      screen->get_param(screen, PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT);
   if (c->ShaderStorageBufferOffsetAlignment) {
      c->MaxCombinedShaderStorageBlocks = c->MaxShaderStorageBufferBindings =
         c->MaxCombinedAtomicBuffers;
      c->MaxCombinedShaderOutputResources +=
         c->MaxCombinedShaderStorageBlocks;
      c->MaxShaderStorageBlockSize = 1 << 27;
      extensions->ARB_shader_storage_buffer_object = GL_TRUE;
   }

   c->MaxCombinedImageUniforms =
         c->Program[MESA_SHADER_VERTEX].MaxImageUniforms +
         c->Program[MESA_SHADER_TESS_CTRL].MaxImageUniforms +
         c->Program[MESA_SHADER_TESS_EVAL].MaxImageUniforms +
         c->Program[MESA_SHADER_GEOMETRY].MaxImageUniforms +
         c->Program[MESA_SHADER_FRAGMENT].MaxImageUniforms +
         c->Program[MESA_SHADER_COMPUTE].MaxImageUniforms;
   c->MaxCombinedShaderOutputResources += c->MaxCombinedImageUniforms;
   c->MaxImageUnits = MAX_IMAGE_UNITS;
   if (c->MaxCombinedImageUniforms) {
      extensions->ARB_shader_image_load_store = GL_TRUE;
      extensions->ARB_shader_image_size = GL_TRUE;
   }

   /* ARB_framebuffer_no_attachments */
   c->MaxFramebufferWidth   = c->MaxViewportWidth;
   c->MaxFramebufferHeight  = c->MaxViewportHeight;
   /* NOTE: we cheat here a little by assuming that
    * PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS has the same
    * number of layers as we need, although we technically
    * could have more the generality is not really useful
    * in practicality.
    */
   c->MaxFramebufferLayers =
      screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS);

   c->MaxWindowRectangles =
      screen->get_param(screen, PIPE_CAP_MAX_WINDOW_RECTANGLES);
}


/**
 * Given a member \c x of struct gl_extensions, return offset of
 * \c x in bytes.
 */
#define o(x) offsetof(struct gl_extensions, x)


struct st_extension_cap_mapping {
   int extension_offset;
   int cap;
};

struct st_extension_format_mapping {
   int extension_offset[2];
   enum pipe_format format[32];

   /* If TRUE, at least one format must be supported for the extensions to be
    * advertised. If FALSE, all the formats must be supported. */
   GLboolean need_at_least_one;
};

/**
 * Enable extensions if certain pipe formats are supported by the driver.
 * What extensions will be enabled and what formats must be supported is
 * described by the array of st_extension_format_mapping.
 *
 * target and bind_flags are passed to is_format_supported.
 */
static void
init_format_extensions(struct pipe_screen *screen,
                       struct gl_extensions *extensions,
                       const struct st_extension_format_mapping *mapping,
                       unsigned num_mappings,
                       enum pipe_texture_target target,
                       unsigned bind_flags)
{
   GLboolean *extension_table = (GLboolean *) extensions;
   unsigned i;
   int j;
   int num_formats = ARRAY_SIZE(mapping->format);
   int num_ext = ARRAY_SIZE(mapping->extension_offset);

   for (i = 0; i < num_mappings; i++) {
      int num_supported = 0;

      /* Examine each format in the list. */
      for (j = 0; j < num_formats && mapping[i].format[j]; j++) {
         if (screen->is_format_supported(screen, mapping[i].format[j],
                                         target, 0, bind_flags)) {
            num_supported++;
         }
      }

      if (!num_supported ||
          (!mapping[i].need_at_least_one && num_supported != j)) {
         continue;
      }

      /* Enable all extensions in the list. */
      for (j = 0; j < num_ext && mapping[i].extension_offset[j]; j++)
         extension_table[mapping[i].extension_offset[j]] = GL_TRUE;
   }
}


/**
 * Given a list of formats and bind flags, return the maximum number
 * of samples supported by any of those formats.
 */
static unsigned
get_max_samples_for_formats(struct pipe_screen *screen,
                            unsigned num_formats,
                            enum pipe_format *formats,
                            unsigned max_samples,
                            unsigned bind)
{
   unsigned i, f;

   for (i = max_samples; i > 0; --i) {
      for (f = 0; f < num_formats; f++) {
         if (screen->is_format_supported(screen, formats[f],
                                         PIPE_TEXTURE_2D, i, bind)) {
            return i;
         }
      }
   }
   return 0;
}


/**
 * Use pipe_screen::get_param() to query PIPE_CAP_ values to determine
 * which GL extensions are supported.
 * Quite a few extensions are always supported because they are standard
 * features or can be built on top of other gallium features.
 * Some fine tuning may still be needed.
 */
void st_init_extensions(struct pipe_screen *screen,
                        struct gl_constants *consts,
                        struct gl_extensions *extensions,
                        struct st_config_options *options,
                        boolean has_lib_dxtc)
{
   unsigned i;
   GLboolean *extension_table = (GLboolean *) extensions;

   static const struct st_extension_cap_mapping cap_mapping[] = {
      { o(ARB_base_instance),                PIPE_CAP_START_INSTANCE                   },
      { o(ARB_buffer_storage),               PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT   },
      { o(ARB_clear_texture),                PIPE_CAP_CLEAR_TEXTURE                    },
      { o(ARB_clip_control),                 PIPE_CAP_CLIP_HALFZ                       },
      { o(ARB_color_buffer_float),           PIPE_CAP_VERTEX_COLOR_UNCLAMPED           },
      { o(ARB_conditional_render_inverted),  PIPE_CAP_CONDITIONAL_RENDER_INVERTED      },
      { o(ARB_copy_image),                   PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS },
      { o(ARB_cull_distance),                PIPE_CAP_CULL_DISTANCE                    },
      { o(ARB_depth_clamp),                  PIPE_CAP_DEPTH_CLIP_DISABLE               },
      { o(ARB_depth_texture),                PIPE_CAP_TEXTURE_SHADOW_MAP               },
      { o(ARB_derivative_control),           PIPE_CAP_TGSI_FS_FINE_DERIVATIVE          },
      { o(ARB_draw_buffers_blend),           PIPE_CAP_INDEP_BLEND_FUNC                 },
      { o(ARB_draw_indirect),                PIPE_CAP_DRAW_INDIRECT                    },
      { o(ARB_draw_instanced),               PIPE_CAP_TGSI_INSTANCEID                  },
      { o(ARB_fragment_program_shadow),      PIPE_CAP_TEXTURE_SHADOW_MAP               },
      { o(ARB_framebuffer_object),           PIPE_CAP_MIXED_FRAMEBUFFER_SIZES          },
      { o(ARB_indirect_parameters),          PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS       },
      { o(ARB_instanced_arrays),             PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR  },
      { o(ARB_occlusion_query),              PIPE_CAP_OCCLUSION_QUERY                  },
      { o(ARB_occlusion_query2),             PIPE_CAP_OCCLUSION_QUERY                  },
      { o(ARB_pipeline_statistics_query),    PIPE_CAP_QUERY_PIPELINE_STATISTICS        },
      { o(ARB_point_sprite),                 PIPE_CAP_POINT_SPRITE                     },
      { o(ARB_query_buffer_object),          PIPE_CAP_QUERY_BUFFER_OBJECT              },
      { o(ARB_robust_buffer_access_behavior), PIPE_CAP_ROBUST_BUFFER_ACCESS_BEHAVIOR   },
      { o(ARB_sample_shading),               PIPE_CAP_SAMPLE_SHADING                   },
      { o(ARB_seamless_cube_map),            PIPE_CAP_SEAMLESS_CUBE_MAP                },
      { o(ARB_shader_draw_parameters),       PIPE_CAP_DRAW_PARAMETERS                  },
      { o(ARB_shader_group_vote),            PIPE_CAP_TGSI_VOTE                        },
      { o(ARB_shader_stencil_export),        PIPE_CAP_SHADER_STENCIL_EXPORT            },
      { o(ARB_shader_texture_image_samples), PIPE_CAP_TGSI_TXQS                        },
      { o(ARB_shader_texture_lod),           PIPE_CAP_SM3                              },
      { o(ARB_shadow),                       PIPE_CAP_TEXTURE_SHADOW_MAP               },
      { o(ARB_texture_buffer_object),        PIPE_CAP_TEXTURE_BUFFER_OBJECTS           },
      { o(ARB_texture_cube_map_array),       PIPE_CAP_CUBE_MAP_ARRAY                   },
      { o(ARB_texture_gather),               PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS    },
      { o(ARB_texture_mirror_clamp_to_edge), PIPE_CAP_TEXTURE_MIRROR_CLAMP             },
      { o(ARB_texture_multisample),          PIPE_CAP_TEXTURE_MULTISAMPLE              },
      { o(ARB_texture_non_power_of_two),     PIPE_CAP_NPOT_TEXTURES                    },
      { o(ARB_texture_query_lod),            PIPE_CAP_TEXTURE_QUERY_LOD                },
      { o(ARB_texture_view),                 PIPE_CAP_SAMPLER_VIEW_TARGET              },
      { o(ARB_timer_query),                  PIPE_CAP_QUERY_TIMESTAMP                  },
      { o(ARB_transform_feedback2),          PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME       },
      { o(ARB_transform_feedback3),          PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME       },

      { o(EXT_blend_equation_separate),      PIPE_CAP_BLEND_EQUATION_SEPARATE          },
      { o(EXT_depth_bounds_test),            PIPE_CAP_DEPTH_BOUNDS_TEST                },
      { o(EXT_draw_buffers2),                PIPE_CAP_INDEP_BLEND_ENABLE               },
      { o(EXT_polygon_offset_clamp),         PIPE_CAP_POLYGON_OFFSET_CLAMP             },
      { o(EXT_stencil_two_side),             PIPE_CAP_TWO_SIDED_STENCIL                },
      { o(EXT_texture_array),                PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS         },
      { o(EXT_texture_filter_anisotropic),   PIPE_CAP_ANISOTROPIC_FILTER               },
      { o(EXT_texture_mirror_clamp),         PIPE_CAP_TEXTURE_MIRROR_CLAMP             },
      { o(EXT_texture_swizzle),              PIPE_CAP_TEXTURE_SWIZZLE                  },
      { o(EXT_transform_feedback),           PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS        },
      { o(EXT_window_rectangles),            PIPE_CAP_MAX_WINDOW_RECTANGLES            },

      { o(AMD_pinned_memory),                PIPE_CAP_RESOURCE_FROM_USER_MEMORY        },
      { o(ATI_meminfo),                      PIPE_CAP_QUERY_MEMORY_INFO                },
      { o(AMD_seamless_cubemap_per_texture), PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE    },
      { o(ATI_separate_stencil),             PIPE_CAP_TWO_SIDED_STENCIL                },
      { o(ATI_texture_mirror_once),          PIPE_CAP_TEXTURE_MIRROR_CLAMP             },
      { o(NV_conditional_render),            PIPE_CAP_CONDITIONAL_RENDER               },
      { o(NV_primitive_restart),             PIPE_CAP_PRIMITIVE_RESTART                },
      { o(NV_texture_barrier),               PIPE_CAP_TEXTURE_BARRIER                  },
      { o(NVX_gpu_memory_info),              PIPE_CAP_QUERY_MEMORY_INFO                },
      /* GL_NV_point_sprite is not supported by gallium because we don't
       * support the GL_POINT_SPRITE_R_MODE_NV option. */

      { o(OES_standard_derivatives),         PIPE_CAP_SM3                              },
      { o(OES_texture_float_linear),         PIPE_CAP_TEXTURE_FLOAT_LINEAR             },
      { o(OES_texture_half_float_linear),    PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR        },
   };

   /* Required: render target and sampler support */
   static const struct st_extension_format_mapping rendertarget_mapping[] = {
      { { o(ARB_texture_float) },
        { PIPE_FORMAT_R32G32B32A32_FLOAT,
          PIPE_FORMAT_R16G16B16A16_FLOAT } },

      { { o(OES_texture_float) },
        { PIPE_FORMAT_R32G32B32A32_FLOAT } },

      { { o(OES_texture_half_float) },
        { PIPE_FORMAT_R16G16B16A16_FLOAT } },

      { { o(ARB_texture_rgb10_a2ui) },
        { PIPE_FORMAT_R10G10B10A2_UINT,
          PIPE_FORMAT_B10G10R10A2_UINT },
         GL_TRUE }, /* at least one format must be supported */

      { { o(EXT_framebuffer_sRGB) },
        { PIPE_FORMAT_A8B8G8R8_SRGB,
          PIPE_FORMAT_B8G8R8A8_SRGB },
         GL_TRUE }, /* at least one format must be supported */

      { { o(EXT_packed_float) },
        { PIPE_FORMAT_R11G11B10_FLOAT } },

      { { o(EXT_texture_integer) },
        { PIPE_FORMAT_R32G32B32A32_UINT,
          PIPE_FORMAT_R32G32B32A32_SINT } },

      { { o(ARB_texture_rg) },
        { PIPE_FORMAT_R8_UNORM,
          PIPE_FORMAT_R8G8_UNORM } },
   };

   /* Required: depth stencil and sampler support */
   static const struct st_extension_format_mapping depthstencil_mapping[] = {
      { { o(ARB_depth_buffer_float) },
        { PIPE_FORMAT_Z32_FLOAT,
          PIPE_FORMAT_Z32_FLOAT_S8X24_UINT } },
   };

   /* Required: sampler support */
   static const struct st_extension_format_mapping texture_mapping[] = {
      { { o(ARB_texture_compression_rgtc) },
        { PIPE_FORMAT_RGTC1_UNORM,
          PIPE_FORMAT_RGTC1_SNORM,
          PIPE_FORMAT_RGTC2_UNORM,
          PIPE_FORMAT_RGTC2_SNORM } },

      { { o(EXT_texture_compression_latc) },
        { PIPE_FORMAT_LATC1_UNORM,
          PIPE_FORMAT_LATC1_SNORM,
          PIPE_FORMAT_LATC2_UNORM,
          PIPE_FORMAT_LATC2_SNORM } },

      { { o(EXT_texture_compression_s3tc),
          o(ANGLE_texture_compression_dxt) },
        { PIPE_FORMAT_DXT1_RGB,
          PIPE_FORMAT_DXT1_RGBA,
          PIPE_FORMAT_DXT3_RGBA,
          PIPE_FORMAT_DXT5_RGBA } },

      { { o(ARB_texture_compression_bptc) },
        { PIPE_FORMAT_BPTC_RGBA_UNORM,
          PIPE_FORMAT_BPTC_SRGBA,
          PIPE_FORMAT_BPTC_RGB_FLOAT,
          PIPE_FORMAT_BPTC_RGB_UFLOAT } },

      { { o(KHR_texture_compression_astc_ldr) },
        { PIPE_FORMAT_ASTC_4x4,
          PIPE_FORMAT_ASTC_5x4,
          PIPE_FORMAT_ASTC_5x5,
          PIPE_FORMAT_ASTC_6x5,
          PIPE_FORMAT_ASTC_6x6,
          PIPE_FORMAT_ASTC_8x5,
          PIPE_FORMAT_ASTC_8x6,
          PIPE_FORMAT_ASTC_8x8,
          PIPE_FORMAT_ASTC_10x5,
          PIPE_FORMAT_ASTC_10x6,
          PIPE_FORMAT_ASTC_10x8,
          PIPE_FORMAT_ASTC_10x10,
          PIPE_FORMAT_ASTC_12x10,
          PIPE_FORMAT_ASTC_12x12,
          PIPE_FORMAT_ASTC_4x4_SRGB,
          PIPE_FORMAT_ASTC_5x4_SRGB,
          PIPE_FORMAT_ASTC_5x5_SRGB,
          PIPE_FORMAT_ASTC_6x5_SRGB,
          PIPE_FORMAT_ASTC_6x6_SRGB,
          PIPE_FORMAT_ASTC_8x5_SRGB,
          PIPE_FORMAT_ASTC_8x6_SRGB,
          PIPE_FORMAT_ASTC_8x8_SRGB,
          PIPE_FORMAT_ASTC_10x5_SRGB,
          PIPE_FORMAT_ASTC_10x6_SRGB,
          PIPE_FORMAT_ASTC_10x8_SRGB,
          PIPE_FORMAT_ASTC_10x10_SRGB,
          PIPE_FORMAT_ASTC_12x10_SRGB,
          PIPE_FORMAT_ASTC_12x12_SRGB } },

      { { o(EXT_texture_shared_exponent) },
        { PIPE_FORMAT_R9G9B9E5_FLOAT } },

      { { o(EXT_texture_snorm) },
        { PIPE_FORMAT_R8G8B8A8_SNORM } },

      { { o(EXT_texture_sRGB),
          o(EXT_texture_sRGB_decode) },
        { PIPE_FORMAT_A8B8G8R8_SRGB,
          PIPE_FORMAT_B8G8R8A8_SRGB },
        GL_TRUE }, /* at least one format must be supported */

      { { o(ATI_texture_compression_3dc) },
        { PIPE_FORMAT_LATC2_UNORM } },

      { { o(MESA_ycbcr_texture) },
        { PIPE_FORMAT_UYVY,
          PIPE_FORMAT_YUYV },
        GL_TRUE }, /* at least one format must be supported */

      { { o(OES_compressed_ETC1_RGB8_texture) },
        { PIPE_FORMAT_ETC1_RGB8,
          PIPE_FORMAT_R8G8B8A8_UNORM },
        GL_TRUE }, /* at least one format must be supported */

      { { o(ARB_stencil_texturing),
          o(ARB_texture_stencil8) },
        { PIPE_FORMAT_X24S8_UINT,
          PIPE_FORMAT_S8X24_UINT },
        GL_TRUE }, /* at least one format must be supported */
   };

   /* Required: vertex fetch support. */
   static const struct st_extension_format_mapping vertex_mapping[] = {
      { { o(EXT_vertex_array_bgra) },
        { PIPE_FORMAT_B8G8R8A8_UNORM } },
      { { o(ARB_vertex_type_2_10_10_10_rev) },
        { PIPE_FORMAT_R10G10B10A2_UNORM,
          PIPE_FORMAT_B10G10R10A2_UNORM,
          PIPE_FORMAT_R10G10B10A2_SNORM,
          PIPE_FORMAT_B10G10R10A2_SNORM,
          PIPE_FORMAT_R10G10B10A2_USCALED,
          PIPE_FORMAT_B10G10R10A2_USCALED,
          PIPE_FORMAT_R10G10B10A2_SSCALED,
          PIPE_FORMAT_B10G10R10A2_SSCALED } },
      { { o(ARB_vertex_type_10f_11f_11f_rev) },
        { PIPE_FORMAT_R11G11B10_FLOAT } },
   };

   static const struct st_extension_format_mapping tbo_rgb32[] = {
      { {o(ARB_texture_buffer_object_rgb32) },
        { PIPE_FORMAT_R32G32B32_FLOAT,
          PIPE_FORMAT_R32G32B32_UINT,
          PIPE_FORMAT_R32G32B32_SINT,
        } },
   };

   /*
    * Extensions that are supported by all Gallium drivers:
    */
   extensions->ARB_ES2_compatibility = GL_TRUE;
   extensions->ARB_draw_elements_base_vertex = GL_TRUE;
   extensions->ARB_explicit_attrib_location = GL_TRUE;
   extensions->ARB_explicit_uniform_location = GL_TRUE;
   extensions->ARB_fragment_coord_conventions = GL_TRUE;
   extensions->ARB_fragment_program = GL_TRUE;
   extensions->ARB_fragment_shader = GL_TRUE;
   extensions->ARB_half_float_vertex = GL_TRUE;
   extensions->ARB_internalformat_query = GL_TRUE;
   extensions->ARB_internalformat_query2 = GL_TRUE;
   extensions->ARB_map_buffer_range = GL_TRUE;
   extensions->ARB_texture_border_clamp = GL_TRUE; /* XXX temp */
   extensions->ARB_texture_cube_map = GL_TRUE;
   extensions->ARB_texture_env_combine = GL_TRUE;
   extensions->ARB_texture_env_crossbar = GL_TRUE;
   extensions->ARB_texture_env_dot3 = GL_TRUE;
   extensions->ARB_vertex_program = GL_TRUE;
   extensions->ARB_vertex_shader = GL_TRUE;

   extensions->EXT_blend_color = GL_TRUE;
   extensions->EXT_blend_func_separate = GL_TRUE;
   extensions->EXT_blend_minmax = GL_TRUE;
   extensions->EXT_gpu_program_parameters = GL_TRUE;
   extensions->EXT_pixel_buffer_object = GL_TRUE;
   extensions->EXT_point_parameters = GL_TRUE;
   extensions->EXT_provoking_vertex = GL_TRUE;

   extensions->EXT_texture_env_dot3 = GL_TRUE;

   extensions->ATI_fragment_shader = GL_TRUE;
   extensions->ATI_texture_env_combine3 = GL_TRUE;

   extensions->MESA_pack_invert = GL_TRUE;

   extensions->NV_fog_distance = GL_TRUE;
   extensions->NV_texture_env_combine4 = GL_TRUE;
   extensions->NV_texture_rectangle = GL_TRUE;

   extensions->OES_EGL_image = GL_TRUE;
   extensions->OES_EGL_image_external = GL_TRUE;
   extensions->OES_draw_texture = GL_TRUE;

   /* Expose the extensions which directly correspond to gallium caps. */
   for (i = 0; i < ARRAY_SIZE(cap_mapping); i++) {
      if (screen->get_param(screen, cap_mapping[i].cap)) {
         extension_table[cap_mapping[i].extension_offset] = GL_TRUE;
      }
   }

   /* Expose the extensions which directly correspond to gallium formats. */
   init_format_extensions(screen, extensions, rendertarget_mapping,
                          ARRAY_SIZE(rendertarget_mapping), PIPE_TEXTURE_2D,
                          PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(screen, extensions, depthstencil_mapping,
                          ARRAY_SIZE(depthstencil_mapping), PIPE_TEXTURE_2D,
                          PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(screen, extensions, texture_mapping,
                          ARRAY_SIZE(texture_mapping), PIPE_TEXTURE_2D,
                          PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(screen, extensions, vertex_mapping,
                          ARRAY_SIZE(vertex_mapping), PIPE_BUFFER,
                          PIPE_BIND_VERTEX_BUFFER);

   /* Figure out GLSL support and set GLSLVersion to it. */
   consts->GLSLVersion = screen->get_param(screen, PIPE_CAP_GLSL_FEATURE_LEVEL);

   _mesa_override_glsl_version(consts);

   if (options->force_glsl_version > 0 &&
       options->force_glsl_version <= consts->GLSLVersion) {
      consts->ForceGLSLVersion = options->force_glsl_version;
   }

   if (consts->GLSLVersion >= 400)
      extensions->ARB_gpu_shader5 = GL_TRUE;
   if (consts->GLSLVersion >= 410)
      extensions->ARB_shader_precision = GL_TRUE;

   /* This extension needs full OpenGL 3.2, but we don't know if that's
    * supported at this point. Only check the GLSL version. */
   if (consts->GLSLVersion >= 150 &&
       screen->get_param(screen, PIPE_CAP_TGSI_VS_LAYER_VIEWPORT)) {
      extensions->AMD_vertex_shader_layer = GL_TRUE;
   }

   if (consts->GLSLVersion >= 130) {
      consts->NativeIntegers = GL_TRUE;
      consts->MaxClipPlanes = 8;

      if (screen->get_param(screen, PIPE_CAP_VERTEXID_NOBASE)) {
         consts->VertexID_is_zero_based = GL_TRUE;
      }

      /* Extensions that either depend on GLSL 1.30 or are a subset thereof. */
      extensions->ARB_conservative_depth = GL_TRUE;
      extensions->ARB_shading_language_packing = GL_TRUE;
      extensions->OES_depth_texture_cube_map = GL_TRUE;
      extensions->ARB_shading_language_420pack = GL_TRUE;
      extensions->ARB_texture_query_levels = GL_TRUE;
      extensions->ARB_shader_subroutine = GL_TRUE;

      if (!options->disable_shader_bit_encoding) {
         extensions->ARB_shader_bit_encoding = GL_TRUE;
      }

      extensions->EXT_shader_integer_mix = GL_TRUE;
      extensions->ARB_arrays_of_arrays = GL_TRUE;
   } else {
      /* Optional integer support for GLSL 1.2. */
      if (screen->get_shader_param(screen, PIPE_SHADER_VERTEX,
                                   PIPE_SHADER_CAP_INTEGERS) &&
          screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                   PIPE_SHADER_CAP_INTEGERS)) {
         consts->NativeIntegers = GL_TRUE;

         extensions->EXT_shader_integer_mix = GL_TRUE;
      }

      /* Integer textures make no sense before GLSL 1.30 */
      extensions->EXT_texture_integer = GL_FALSE;
   }

   consts->GLSLZeroInit = options->glsl_zero_init;

   consts->UniformBooleanTrue = consts->NativeIntegers ? ~0U : fui(1.0f);

   /* Below are the cases which cannot be moved into tables easily. */

   if (!has_lib_dxtc && !options->force_s3tc_enable) {
      extensions->EXT_texture_compression_s3tc = GL_FALSE;
      extensions->ANGLE_texture_compression_dxt = GL_FALSE;
   }

   if (screen->get_shader_param(screen, PIPE_SHADER_TESS_CTRL,
                                PIPE_SHADER_CAP_MAX_INSTRUCTIONS) > 0) {
      extensions->ARB_tessellation_shader = GL_TRUE;
   }

   if (screen->fence_finish) {
      extensions->ARB_sync = GL_TRUE;
   }

   /* Needs PIPE_CAP_SAMPLE_SHADING + all the sample-related bits of
    * ARB_gpu_shader5. This enables all the per-sample shading ES extensions.
    */
   extensions->OES_sample_variables = extensions->ARB_sample_shading &&
      extensions->ARB_gpu_shader5;

   /* If we don't have native ETC2 support, we don't keep track of the
    * original ETC2 data. This is necessary to be able to copy images between
    * compatible view classes.
    */
   if (extensions->ARB_copy_image && screen->is_format_supported(
             screen, PIPE_FORMAT_ETC2_RGB8,
             PIPE_TEXTURE_2D, 0,
             PIPE_BIND_SAMPLER_VIEW)) {
      extensions->OES_copy_image = GL_TRUE;
   }

   /* Maximum sample count. */
   {
      enum pipe_format color_formats[] = {
         PIPE_FORMAT_R8G8B8A8_UNORM,
         PIPE_FORMAT_B8G8R8A8_UNORM,
         PIPE_FORMAT_A8R8G8B8_UNORM,
         PIPE_FORMAT_A8B8G8R8_UNORM,
      };
      enum pipe_format depth_formats[] = {
         PIPE_FORMAT_Z16_UNORM,
         PIPE_FORMAT_Z24X8_UNORM,
         PIPE_FORMAT_X8Z24_UNORM,
         PIPE_FORMAT_Z32_UNORM,
         PIPE_FORMAT_Z32_FLOAT
      };
      enum pipe_format int_formats[] = {
         PIPE_FORMAT_R8G8B8A8_SINT
      };
      enum pipe_format void_formats[] = {
         PIPE_FORMAT_NONE
      };

      consts->MaxSamples =
         get_max_samples_for_formats(screen, ARRAY_SIZE(color_formats),
                                     color_formats, 16,
                                     PIPE_BIND_RENDER_TARGET);

      consts->MaxImageSamples =
         get_max_samples_for_formats(screen, ARRAY_SIZE(color_formats),
                                     color_formats, 16,
                                     PIPE_BIND_SHADER_IMAGE);

      consts->MaxColorTextureSamples =
         get_max_samples_for_formats(screen, ARRAY_SIZE(color_formats),
                                     color_formats, consts->MaxSamples,
                                     PIPE_BIND_SAMPLER_VIEW);

      consts->MaxDepthTextureSamples =
         get_max_samples_for_formats(screen, ARRAY_SIZE(depth_formats),
                                     depth_formats, consts->MaxSamples,
                                     PIPE_BIND_SAMPLER_VIEW);

      consts->MaxIntegerSamples =
         get_max_samples_for_formats(screen, ARRAY_SIZE(int_formats),
                                     int_formats, consts->MaxSamples,
                                     PIPE_BIND_SAMPLER_VIEW);

      /* ARB_framebuffer_no_attachments, assume max no. of samples 32 */
      consts->MaxFramebufferSamples =
         get_max_samples_for_formats(screen, ARRAY_SIZE(void_formats),
                                     void_formats, 32,
                                     PIPE_BIND_RENDER_TARGET);
   }
   if (consts->MaxSamples == 1) {
      /* one sample doesn't really make sense */
      consts->MaxSamples = 0;
   }
   else if (consts->MaxSamples >= 2) {
      extensions->EXT_framebuffer_multisample = GL_TRUE;
      extensions->EXT_framebuffer_multisample_blit_scaled = GL_TRUE;
   }

   if (consts->MaxSamples == 0 && screen->get_param(screen, PIPE_CAP_FAKE_SW_MSAA)) {
	consts->FakeSWMSAA = GL_TRUE;
        extensions->EXT_framebuffer_multisample = GL_TRUE;
        extensions->EXT_framebuffer_multisample_blit_scaled = GL_TRUE;
        extensions->ARB_texture_multisample = GL_TRUE;
   }

   if (consts->MaxDualSourceDrawBuffers > 0 &&
       !options->disable_blend_func_extended)
      extensions->ARB_blend_func_extended = GL_TRUE;

   if (screen->get_param(screen, PIPE_CAP_QUERY_TIME_ELAPSED) ||
       extensions->ARB_timer_query) {
      extensions->EXT_timer_query = GL_TRUE;
   }

   if (extensions->ARB_transform_feedback2 &&
       extensions->ARB_draw_instanced) {
      extensions->ARB_transform_feedback_instanced = GL_TRUE;
   }
   if (options->force_glsl_extensions_warn)
      consts->ForceGLSLExtensionsWarn = 1;

   if (options->disable_glsl_line_continuations)
      consts->DisableGLSLLineContinuations = 1;

   if (options->allow_glsl_extension_directive_midshader)
      consts->AllowGLSLExtensionDirectiveMidShader = GL_TRUE;

   consts->MinMapBufferAlignment =
      screen->get_param(screen, PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT);

   if (extensions->ARB_texture_buffer_object) {
      consts->MaxTextureBufferSize =
         _min(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE),
              (1u << 31) - 1);
      consts->TextureBufferOffsetAlignment =
         screen->get_param(screen, PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT);

      if (consts->TextureBufferOffsetAlignment)
         extensions->ARB_texture_buffer_range = GL_TRUE;

      init_format_extensions(screen, extensions, tbo_rgb32,
                             ARRAY_SIZE(tbo_rgb32), PIPE_BUFFER,
                             PIPE_BIND_SAMPLER_VIEW);
   }

   extensions->OES_texture_buffer =
      extensions->ARB_texture_buffer_object &&
      extensions->ARB_texture_buffer_range &&
      extensions->ARB_texture_buffer_object_rgb32 &&
      extensions->ARB_shader_image_load_store;

   /* Unpacking a varying in the fragment shader costs 1 texture indirection.
    * If the number of available texture indirections is very limited, then we
    * prefer to disable varying packing rather than run the risk of varying
    * packing preventing a shader from running.
    */
   if (screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS) <= 8) {
      /* We can't disable varying packing if transform feedback is available,
       * because transform feedback code assumes a packed varying layout.
       */
      if (!extensions->EXT_transform_feedback)
         consts->DisableVaryingPacking = GL_TRUE;
   }

   consts->MaxViewports = screen->get_param(screen, PIPE_CAP_MAX_VIEWPORTS);
   if (consts->MaxViewports >= 16) {
      if (consts->GLSLVersion >= 400) {
         consts->ViewportBounds.Min = -32768.0;
         consts->ViewportBounds.Max = 32767.0;
      } else {
         consts->ViewportBounds.Min = -16384.0;
         consts->ViewportBounds.Max = 16383.0;
      }
      extensions->ARB_viewport_array = GL_TRUE;
      extensions->ARB_fragment_layer_viewport = GL_TRUE;
      if (extensions->AMD_vertex_shader_layer)
         extensions->AMD_vertex_shader_viewport_index = GL_TRUE;
   }

   /* ARB_framebuffer_no_attachments */
   if (screen->get_param(screen, PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT) &&
       ((consts->MaxSamples >= 4 && consts->MaxFramebufferLayers >= 2048) ||
        (consts->MaxFramebufferSamples >= consts->MaxSamples &&
         consts->MaxFramebufferLayers >= consts->MaxArrayTextureLayers)))
      extensions->ARB_framebuffer_no_attachments = GL_TRUE;

   /* GL_ARB_ES3_compatibility.
    *
    * Assume that ES3 is supported if GLSL 3.30 is supported.
    * (OpenGL 3.3 is a requirement for that extension.)
    */
   if (consts->GLSLVersion >= 330 &&
       /* Requirements for ETC2 emulation. */
       screen->is_format_supported(screen, PIPE_FORMAT_R8G8B8A8_UNORM,
                                   PIPE_TEXTURE_2D, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_B8G8R8A8_SRGB,
                                   PIPE_TEXTURE_2D, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R16_UNORM,
                                   PIPE_TEXTURE_2D, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R16G16_UNORM,
                                   PIPE_TEXTURE_2D, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R16_SNORM,
                                   PIPE_TEXTURE_2D, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R16G16_SNORM,
                                   PIPE_TEXTURE_2D, 0,
                                   PIPE_BIND_SAMPLER_VIEW)) {
      extensions->ARB_ES3_compatibility = GL_TRUE;
   }

#ifdef HAVE_ST_VDPAU
   if (screen->get_video_param &&
       screen->get_video_param(screen, PIPE_VIDEO_PROFILE_UNKNOWN,
                               PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
                               PIPE_VIDEO_CAP_SUPPORTS_INTERLACED)) {
      extensions->NV_vdpau_interop = GL_TRUE;
   }
#endif

   if (screen->get_shader_param(screen, PIPE_SHADER_VERTEX,
                                PIPE_SHADER_CAP_DOUBLES) &&
       screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                PIPE_SHADER_CAP_DOUBLES)) {
      extensions->ARB_gpu_shader_fp64 = GL_TRUE;
      extensions->ARB_vertex_attrib_64bit = GL_TRUE;
   }

   if ((ST_DEBUG & DEBUG_GREMEDY) &&
       screen->get_param(screen, PIPE_CAP_STRING_MARKER))
      extensions->GREMEDY_string_marker = GL_TRUE;

   if (screen->get_param(screen, PIPE_CAP_COMPUTE)) {
      int compute_supported_irs =
         screen->get_shader_param(screen, PIPE_SHADER_COMPUTE,
                                  PIPE_SHADER_CAP_SUPPORTED_IRS);
      if (compute_supported_irs & (1 << PIPE_SHADER_IR_TGSI)) {
         uint64_t grid_size[3], block_size[3];
         uint64_t max_local_size, max_threads_per_block;

         screen->get_compute_param(screen, PIPE_SHADER_IR_TGSI,
                                   PIPE_COMPUTE_CAP_MAX_GRID_SIZE, grid_size);
         screen->get_compute_param(screen, PIPE_SHADER_IR_TGSI,
                                   PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE, block_size);
         screen->get_compute_param(screen, PIPE_SHADER_IR_TGSI,
                                   PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK,
                                   &max_threads_per_block);
         screen->get_compute_param(screen, PIPE_SHADER_IR_TGSI,
                                   PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE,
                                   &max_local_size);

         consts->MaxComputeWorkGroupInvocations = max_threads_per_block;
         consts->MaxComputeSharedMemorySize = max_local_size;

         for (i = 0; i < 3; i++) {
            consts->MaxComputeWorkGroupCount[i] = grid_size[i];
            consts->MaxComputeWorkGroupSize[i] = block_size[i];
         }

         extensions->ARB_compute_shader =
                                      extensions->ARB_shader_image_load_store &&
                                      extensions->ARB_shader_atomic_counters;
      }
   }

   /* If we support ES 3.1, we support the ES3_1_compatibility ext. However
    * there's no clean way of telling whether we would support ES 3.1 from
    * here, so copy the condition from compute_version_es2 here. A lot of
    * these are redunant, but simpler to just have a (near-)exact copy here.
    */
   extensions->ARB_ES3_1_compatibility =
      extensions->ARB_ES3_compatibility &&
      extensions->ARB_arrays_of_arrays &&
      extensions->ARB_compute_shader &&
      extensions->ARB_draw_indirect &&
      extensions->ARB_explicit_uniform_location &&
      extensions->ARB_framebuffer_no_attachments &&
      extensions->ARB_shader_atomic_counters &&
      extensions->ARB_shader_image_load_store &&
      extensions->ARB_shader_image_size &&
      extensions->ARB_shader_storage_buffer_object &&
      extensions->ARB_shading_language_packing &&
      extensions->ARB_stencil_texturing &&
      extensions->ARB_texture_multisample &&
      extensions->ARB_gpu_shader5 &&
      extensions->EXT_shader_integer_mix;

   /* And if we have enough for ES 3.1, we can also expose
    * OES_shader_io_blocks, which is only hidden due to the compiler not being
    * able to version-restrict things.
    */
   extensions->OES_shader_io_blocks = extensions->ARB_ES3_1_compatibility;
}
