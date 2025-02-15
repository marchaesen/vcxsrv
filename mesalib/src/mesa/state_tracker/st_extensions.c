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

#include "compiler/nir/nir.h"


#include "main/context.h"
#include "main/macros.h"
#include "main/spirv_extensions.h"
#include "main/version.h"
#include "nir/nir_to_tgsi.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "tgsi/tgsi_from_mesa.h"
#include "util/u_math.h"
#include "util/u_memory.h"

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

static float _minf(float a, float b)
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

static unsigned mesa_to_gl_stages(unsigned stages)
{
   unsigned ret = 0;

   if (stages & BITFIELD_BIT(MESA_SHADER_VERTEX))
      ret |= GL_VERTEX_SHADER_BIT;

   if (stages & BITFIELD_BIT(MESA_SHADER_TESS_CTRL))
      ret |= GL_TESS_CONTROL_SHADER_BIT;

   if (stages & BITFIELD_BIT(MESA_SHADER_TESS_EVAL))
      ret |= GL_TESS_EVALUATION_SHADER_BIT;

   if (stages & BITFIELD_BIT(MESA_SHADER_GEOMETRY))
      ret |= GL_GEOMETRY_SHADER_BIT;

   if (stages & BITFIELD_BIT(MESA_SHADER_FRAGMENT))
      ret |= GL_FRAGMENT_SHADER_BIT;

   if (stages & BITFIELD_BIT(MESA_SHADER_COMPUTE))
      ret |= GL_COMPUTE_SHADER_BIT;

   return ret;
}

/**
 * Query driver to get implementation limits.
 * Note that we have to limit/clamp against Mesa's internal limits too.
 */
void st_init_limits(struct pipe_screen *screen,
                    struct gl_constants *c, struct gl_extensions *extensions,
                    gl_api api)
{
   unsigned sh;
   bool can_ubo = true;
   int temp;

   c->MaxTextureSize = screen->caps.max_texture_2d_size;
   c->MaxTextureSize = MIN2(c->MaxTextureSize, 1 << (MAX_TEXTURE_LEVELS - 1));
   c->MaxTextureMbytes = MAX2(c->MaxTextureMbytes,
                              screen->caps.max_texture_mb);

   c->Max3DTextureLevels
      = _min(screen->caps.max_texture_3d_levels,
            MAX_TEXTURE_LEVELS);
   extensions->OES_texture_3D = c->Max3DTextureLevels != 0;

   c->MaxCubeTextureLevels
      = _min(screen->caps.max_texture_cube_levels,
            MAX_TEXTURE_LEVELS);

   c->MaxTextureRectSize = _min(c->MaxTextureSize, MAX_TEXTURE_RECT_SIZE);

   c->MaxArrayTextureLayers
      = screen->caps.max_texture_array_layers;

   /* Define max viewport size and max renderbuffer size in terms of
    * max texture size (note: max tex RECT size = max tex 2D size).
    * If this isn't true for some hardware we'll need new pipe_caps. queries.
    */
   c->MaxViewportWidth =
   c->MaxViewportHeight =
   c->MaxRenderbufferSize = c->MaxTextureRectSize;

   c->SubPixelBits =
      screen->caps.rasterizer_subpixel_bits;
   c->ViewportSubpixelBits =
      screen->caps.viewport_subpixel_bits;

   c->MaxDrawBuffers = c->MaxColorAttachments =
      _clamp(screen->caps.max_render_targets,
             1, MAX_DRAW_BUFFERS);

   c->MaxDualSourceDrawBuffers =
      _clamp(screen->caps.max_dual_source_render_targets,
             0, MAX_DRAW_BUFFERS);

   c->MaxLineWidth =
      _maxf(1.0f, screen->caps.max_line_width);
   c->MaxLineWidthAA =
      _maxf(1.0f, screen->caps.max_line_width_aa);

   c->MinLineWidth = screen->caps.min_line_width;
   c->MinLineWidthAA = screen->caps.min_line_width_aa;
   c->LineWidthGranularity = screen->caps.line_width_granularity;

   c->MaxPointSize =
      _maxf(1.0f, screen->caps.max_point_size);
   c->MaxPointSizeAA =
      _maxf(1.0f, screen->caps.max_point_size_aa);

   c->MinPointSize = MAX2(screen->caps.min_point_size, 0.01);
   c->MinPointSizeAA = MAX2(screen->caps.min_point_size_aa, 0.01);
   c->PointSizeGranularity = screen->caps.point_size_granularity;

   c->MaxTextureMaxAnisotropy =
      _maxf(2.0f,
            screen->caps.max_texture_anisotropy);

   c->MaxTextureLodBias =
      _minf(31.0f, screen->caps.max_texture_lod_bias);

   c->QuadsFollowProvokingVertexConvention =
      screen->caps.quads_follow_provoking_vertex_convention;

   c->MaxUniformBlockSize =
      screen->caps.max_constant_buffer_size;

   if (c->MaxUniformBlockSize < 16384) {
      can_ubo = false;
   }

   /* Round down to a multiple of 4 to make piglit happy. Bytes are not
    * addressible by UBOs anyway.
    */
   c->MaxUniformBlockSize &= ~3;

   c->HasFBFetch = screen->caps.fbfetch;

   c->PointSizeFixed = screen->caps.point_size_fixed != PIPE_POINT_SIZE_LOWER_ALWAYS;

   for (sh = 0; sh < PIPE_SHADER_TYPES; ++sh) {
      const gl_shader_stage stage = tgsi_processor_to_shader_stage(sh);
      struct gl_shader_compiler_options *options =
         &c->ShaderCompilerOptions[stage];
      struct gl_program_constants *pc = &c->Program[stage];

      if (screen->get_compiler_options)
         options->NirOptions = screen->get_compiler_options(screen, PIPE_SHADER_IR_NIR, sh);

      if (!options->NirOptions) {
         options->NirOptions =
            nir_to_tgsi_get_compiler_options(screen, PIPE_SHADER_IR_NIR, sh);
      }

      if (sh == PIPE_SHADER_COMPUTE) {
         if (!screen->caps.compute)
            continue;
      }

      pc->MaxTextureImageUnits =
         _min(screen->shader_caps[sh].max_texture_samplers,
              MAX_TEXTURE_IMAGE_UNITS);

      pc->MaxInstructions =
         screen->shader_caps[sh].max_instructions;
      pc->MaxAluInstructions =
         screen->shader_caps[sh].max_alu_instructions;
      pc->MaxTexInstructions =
         screen->shader_caps[sh].max_tex_instructions;
      pc->MaxTexIndirections =
         screen->shader_caps[sh].max_tex_indirections;
      pc->MaxAttribs =
         screen->shader_caps[sh].max_inputs;
      pc->MaxTemps =
         screen->shader_caps[sh].max_temps;

      pc->MaxUniformComponents =
         screen->shader_caps[sh].max_const_buffer0_size / 4;

      /* reserve space in the default-uniform for lowered state */
      if (sh == PIPE_SHADER_VERTEX ||
          sh == PIPE_SHADER_TESS_EVAL ||
          sh == PIPE_SHADER_GEOMETRY) {

         if (!screen->caps.clip_planes)
            pc->MaxUniformComponents -= 4 * MAX_CLIP_PLANES;

         if (!screen->caps.point_size_fixed)
            pc->MaxUniformComponents -= 4;
      } else if (sh == PIPE_SHADER_FRAGMENT) {
         if (!screen->caps.alpha_test)
            pc->MaxUniformComponents -= 4;
      }


      pc->MaxUniformComponents = MIN2(pc->MaxUniformComponents,
                                      MAX_UNIFORMS * 4);

      /* For ARB programs, prog_src_register::Index is a signed 13-bit number.
       * This gives us a limit of 4096 values - but we may need to generate
       * internal values in addition to what the source program uses.  So, we
       * drop the limit one step lower, to 2048, to be safe.
       */
      pc->MaxParameters = MIN2(pc->MaxUniformComponents / 4, 2048);
      pc->MaxInputComponents =
         screen->shader_caps[sh].max_inputs * 4;
      pc->MaxOutputComponents =
         screen->shader_caps[sh].max_outputs * 4;


      pc->MaxUniformBlocks =
         screen->shader_caps[sh].max_const_buffers;
      if (pc->MaxUniformBlocks)
         pc->MaxUniformBlocks -= 1; /* The first one is for ordinary uniforms. */
      pc->MaxUniformBlocks = _min(pc->MaxUniformBlocks, MAX_UNIFORM_BUFFERS);

      pc->MaxCombinedUniformComponents =
         pc->MaxUniformComponents +
         (uint64_t)c->MaxUniformBlockSize / 4 * pc->MaxUniformBlocks;

      pc->MaxShaderStorageBlocks =
         screen->shader_caps[sh].max_shader_buffers;

      temp = screen->shader_caps[sh].max_hw_atomic_counters;
      if (temp) {
         /*
          * for separate atomic counters get the actual hw limits
          * per stage on atomic counters and buffers
          */
         pc->MaxAtomicCounters = temp;
         pc->MaxAtomicBuffers = screen->shader_caps[sh].max_hw_atomic_counter_buffers;
      } else if (pc->MaxShaderStorageBlocks) {
         pc->MaxAtomicCounters = MAX_ATOMIC_COUNTERS;
         /*
          * without separate atomic counters, reserve half of the available
          * SSBOs for atomic buffers, and the other half for normal SSBOs.
          */
         pc->MaxAtomicBuffers = pc->MaxShaderStorageBlocks / 2;
         pc->MaxShaderStorageBlocks -= pc->MaxAtomicBuffers;
      }
      pc->MaxImageUniforms =
         _min(screen->shader_caps[sh].max_shader_images,
              MAX_IMAGE_UNIFORMS);

      /* Gallium doesn't really care about local vs. env parameters so use the
       * same limits.
       */
      pc->MaxLocalParams = MIN2(pc->MaxParameters, MAX_PROGRAM_LOCAL_PARAMS);
      pc->MaxEnvParams = MIN2(pc->MaxParameters, MAX_PROGRAM_ENV_PARAMS);

      if (screen->shader_caps[sh].integers) {
         pc->LowInt.RangeMin = 31;
         pc->LowInt.RangeMax = 30;
         pc->LowInt.Precision = 0;
         pc->MediumInt = pc->HighInt = pc->LowInt;

         if (screen->shader_caps[sh].int16) {
            pc->LowInt.RangeMin = 15;
            pc->LowInt.RangeMax = 14;
            pc->MediumInt = pc->LowInt;
         }
      }

      if (screen->shader_caps[sh].fp16) {
         pc->LowFloat.RangeMin = 15;
         pc->LowFloat.RangeMax = 15;
         pc->LowFloat.Precision = 10;
         pc->MediumFloat = pc->LowFloat;
      }

      /* TODO: make these more fine-grained if anyone needs it */
      options->MaxIfDepth =
         screen->shader_caps[sh].max_control_flow_depth;

      options->EmitNoMainReturn =
         !screen->shader_caps[sh].subroutines;

      options->EmitNoCont =
         !screen->shader_caps[sh].cont_supported;

      options->EmitNoIndirectTemp =
         !screen->shader_caps[sh].indirect_temp_addr;
      options->EmitNoIndirectUniform =
         !screen->shader_caps[sh].indirect_const_addr;

      if (pc->MaxInstructions &&
          (options->EmitNoIndirectUniform || pc->MaxUniformBlocks < 12)) {
         can_ubo = false;
      }

      if (sh == PIPE_SHADER_VERTEX || sh == PIPE_SHADER_GEOMETRY) {
         if (screen->caps.viewport_transform_lowered)
            options->LowerBuiltinVariablesXfb |= VARYING_BIT_POS;
         if (screen->caps.psiz_clamped)
            options->LowerBuiltinVariablesXfb |= VARYING_BIT_PSIZ;
      }

      options->LowerPrecisionFloat16 =
         screen->shader_caps[sh].fp16;
      options->LowerPrecisionDerivatives =
         screen->shader_caps[sh].fp16_derivatives;
      options->LowerPrecisionInt16 =
         screen->shader_caps[sh].int16;
      options->LowerPrecisionConstants =
         screen->shader_caps[sh].glsl_16bit_consts;
      options->LowerPrecisionFloat16Uniforms =
         screen->shader_caps[sh].fp16_const_buffers;
   }

   c->MaxUserAssignableUniformLocations =
      c->Program[MESA_SHADER_VERTEX].MaxUniformComponents +
      c->Program[MESA_SHADER_TESS_CTRL].MaxUniformComponents +
      c->Program[MESA_SHADER_TESS_EVAL].MaxUniformComponents +
      c->Program[MESA_SHADER_GEOMETRY].MaxUniformComponents +
      c->Program[MESA_SHADER_FRAGMENT].MaxUniformComponents;

   c->GLSLLowerConstArrays =
      screen->caps.prefer_imm_arrays_as_constbuf;
   c->GLSLTessLevelsAsInputs =
      screen->caps.glsl_tess_levels_as_inputs;
   c->PrimitiveRestartForPatches = false;

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

   c->MaxVarying = screen->caps.max_varyings;
   c->MaxVarying = MIN2(c->MaxVarying, MAX_VARYING);
   c->MaxGeometryOutputVertices =
      screen->caps.max_geometry_output_vertices;
   c->MaxGeometryTotalOutputComponents =
      screen->caps.max_geometry_total_output_components;
   c->MaxGeometryShaderInvocations =
      screen->caps.max_gs_invocations;
   c->MaxTessPatchComponents =
      MIN2(screen->caps.max_shader_patch_varyings,
           MAX_VARYING) * 4;

   c->MinProgramTexelOffset =
      screen->caps.min_texel_offset;
   c->MaxProgramTexelOffset =
      screen->caps.max_texel_offset;

   c->MaxProgramTextureGatherComponents =
      screen->caps.max_texture_gather_components;
   c->MinProgramTextureGatherOffset =
      screen->caps.min_texture_gather_offset;
   c->MaxProgramTextureGatherOffset =
      screen->caps.max_texture_gather_offset;

   c->MaxTransformFeedbackBuffers =
      screen->caps.max_stream_output_buffers;
   c->MaxTransformFeedbackBuffers = MIN2(c->MaxTransformFeedbackBuffers,
                                         MAX_FEEDBACK_BUFFERS);
   c->MaxTransformFeedbackSeparateComponents =
      screen->caps.max_stream_output_separate_components;
   c->MaxTransformFeedbackInterleavedComponents =
      screen->caps.max_stream_output_interleaved_components;
   c->MaxVertexStreams =
      MAX2(1, screen->caps.max_vertex_streams);

   /* The vertex stream must fit into pipe_stream_output_info::stream */
   assert(c->MaxVertexStreams <= 4);

   c->MaxVertexAttribStride
      = screen->caps.max_vertex_attrib_stride;

   /* The value cannot be larger than that since pipe_vertex_buffer::src_offset
    * is only 16 bits.
    */
   temp = screen->caps.max_vertex_element_src_offset;
   c->MaxVertexAttribRelativeOffset = MIN2(0xffff, temp);

   c->GLSLSkipStrictMaxUniformLimitCheck =
      screen->caps.tgsi_can_compact_constants;

   c->UniformBufferOffsetAlignment =
      screen->caps.constant_buffer_offset_alignment;

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
      screen->caps.fs_position_is_sysval;
   c->GLSLPointCoordIsSysVal =
      screen->caps.fs_point_is_sysval;
   c->GLSLFrontFacingIsSysVal =
      screen->caps.fs_face_is_integer_sysval;

   /* GL_ARB_get_program_binary */
   if (screen->get_disk_shader_cache && screen->get_disk_shader_cache(screen))
      c->NumProgramBinaryFormats = 1;
   /* GL_ARB_gl_spirv */
   if (screen->caps.gl_spirv &&
       (api == API_OPENGL_CORE || api == API_OPENGL_COMPAT))
      c->NumShaderBinaryFormats = 1;

   c->MaxAtomicBufferBindings =
      MAX2(c->Program[MESA_SHADER_FRAGMENT].MaxAtomicBuffers,
           c->Program[MESA_SHADER_COMPUTE].MaxAtomicBuffers);
   c->MaxAtomicBufferSize = ATOMIC_COUNTER_SIZE *
      MAX2(c->Program[MESA_SHADER_FRAGMENT].MaxAtomicCounters,
           c->Program[MESA_SHADER_COMPUTE].MaxAtomicCounters);

   c->MaxCombinedAtomicBuffers =
      MIN2(screen->caps.max_combined_hw_atomic_counter_buffers,
           MAX_COMBINED_ATOMIC_BUFFERS);
   if (!c->MaxCombinedAtomicBuffers) {
      c->MaxCombinedAtomicBuffers = MAX2(
         c->Program[MESA_SHADER_VERTEX].MaxAtomicBuffers +
         c->Program[MESA_SHADER_TESS_CTRL].MaxAtomicBuffers +
         c->Program[MESA_SHADER_TESS_EVAL].MaxAtomicBuffers +
         c->Program[MESA_SHADER_GEOMETRY].MaxAtomicBuffers +
         c->Program[MESA_SHADER_FRAGMENT].MaxAtomicBuffers,
         c->Program[MESA_SHADER_COMPUTE].MaxAtomicBuffers);
      assert(c->MaxCombinedAtomicBuffers <= MAX_COMBINED_ATOMIC_BUFFERS);
   }

   c->MaxCombinedAtomicCounters =
      screen->caps.max_combined_hw_atomic_counters;
   if (!c->MaxCombinedAtomicCounters)
      c->MaxCombinedAtomicCounters = MAX_ATOMIC_COUNTERS;

   if (c->Program[MESA_SHADER_FRAGMENT].MaxAtomicBuffers) {
      extensions->ARB_shader_atomic_counters = GL_TRUE;
      extensions->ARB_shader_atomic_counter_ops = GL_TRUE;
   }

   c->MaxCombinedShaderOutputResources = c->MaxDrawBuffers;
   c->ShaderStorageBufferOffsetAlignment =
      screen->caps.shader_buffer_offset_alignment;
   if (c->ShaderStorageBufferOffsetAlignment) {
      c->MaxCombinedShaderStorageBlocks =
         MIN2(screen->caps.max_combined_shader_buffers,
              MAX_COMBINED_SHADER_STORAGE_BUFFERS);
      if (!c->MaxCombinedShaderStorageBlocks) {
         c->MaxCombinedShaderStorageBlocks = MAX2(
            c->Program[MESA_SHADER_VERTEX].MaxShaderStorageBlocks +
            c->Program[MESA_SHADER_TESS_CTRL].MaxShaderStorageBlocks +
            c->Program[MESA_SHADER_TESS_EVAL].MaxShaderStorageBlocks +
            c->Program[MESA_SHADER_GEOMETRY].MaxShaderStorageBlocks +
            c->Program[MESA_SHADER_FRAGMENT].MaxShaderStorageBlocks,
            c->Program[MESA_SHADER_COMPUTE].MaxShaderStorageBlocks);
         assert(c->MaxCombinedShaderStorageBlocks < MAX_COMBINED_SHADER_STORAGE_BUFFERS);
      }
      c->MaxShaderStorageBufferBindings = c->MaxCombinedShaderStorageBlocks;

      c->MaxCombinedShaderOutputResources +=
         c->MaxCombinedShaderStorageBlocks;
      c->MaxShaderStorageBlockSize =
         screen->caps.max_shader_buffer_size;
      if (c->Program[MESA_SHADER_FRAGMENT].MaxShaderStorageBlocks)
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
   if (c->Program[MESA_SHADER_FRAGMENT].MaxImageUniforms &&
       screen->caps.image_store_formatted) {
      extensions->ARB_shader_image_load_store = GL_TRUE;
      extensions->ARB_shader_image_size = GL_TRUE;
   }

   /* ARB_framebuffer_no_attachments */
   c->MaxFramebufferWidth   = c->MaxViewportWidth;
   c->MaxFramebufferHeight  = c->MaxViewportHeight;
   /* NOTE: we cheat here a little by assuming that
    * pipe_caps.max_texture_array_layers has the same
    * number of layers as we need, although we technically
    * could have more the generality is not really useful
    * in practicality.
    */
   c->MaxFramebufferLayers =
      screen->caps.max_texture_array_layers;

   c->MaxWindowRectangles =
      screen->caps.max_window_rectangles;

   c->SparseBufferPageSize =
      screen->caps.sparse_buffer_page_size;

   c->AllowMappedBuffersDuringExecution =
      screen->caps.allow_mapped_buffers_during_execution;

   c->UseSTD430AsDefaultPacking =
      screen->caps.load_constbuf;

   c->MaxSubpixelPrecisionBiasBits =
      screen->caps.max_conservative_raster_subpixel_precision_bias;

   c->ConservativeRasterDilateRange[0] =
      screen->caps.min_conservative_raster_dilate;
   c->ConservativeRasterDilateRange[1] =
      screen->caps.max_conservative_raster_dilate;
   c->ConservativeRasterDilateGranularity =
      screen->caps.conservative_raster_dilate_granularity;

   /* limit the max combined shader output resources to a driver limit */
   temp = screen->caps.max_combined_shader_output_resources;
   if (temp > 0 && c->MaxCombinedShaderOutputResources > temp)
      c->MaxCombinedShaderOutputResources = temp;

   c->VertexBufferOffsetIsInt32 =
      screen->caps.signed_vertex_buffer_offset;

   c->UseVAOFastPath =
         screen->caps.allow_dynamic_vao_fastpath;

   c->glBeginEndBufferSize =
      screen->caps.gl_begin_end_buffer_size;

   c->MaxSparseTextureSize =
      screen->caps.max_sparse_texture_size;
   c->MaxSparse3DTextureSize =
      screen->caps.max_sparse_3d_texture_size;
   c->MaxSparseArrayTextureLayers =
      screen->caps.max_sparse_array_texture_layers;
   c->SparseTextureFullArrayCubeMipmaps =
      screen->caps.sparse_texture_full_array_cube_mipmaps;

   c->HardwareAcceleratedSelect =
      screen->caps.hardware_gl_select;

   c->AllowGLThreadBufferSubDataOpt =
      screen->caps.allow_glthread_buffer_subdata_opt;

   c->HasDrawVertexState =
      screen->caps.draw_vertex_state;

   c->ShaderSubgroupSize =
      screen->caps.shader_subgroup_size;
   c->ShaderSubgroupSupportedStages =
      mesa_to_gl_stages(screen->caps.shader_subgroup_supported_stages);
   c->ShaderSubgroupSupportedFeatures =
      screen->caps.shader_subgroup_supported_features;
   c->ShaderSubgroupQuadAllStages =
      screen->caps.shader_subgroup_quad_all_stages;
}


/**
 * Given a member \c x of struct gl_extensions, return offset of
 * \c x in bytes.
 */
#define o(x) offsetof(struct gl_extensions, x)

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
                                         target, 0, 0, bind_flags)) {
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
                            const enum pipe_format *formats,
                            unsigned max_samples,
                            unsigned bind)
{
   unsigned i, f;

   for (i = max_samples; i > 0; --i) {
      for (f = 0; f < num_formats; f++) {
         if (screen->is_format_supported(screen, formats[f],
                                         PIPE_TEXTURE_2D, i, i, bind)) {
            return i;
         }
      }
   }
   return 0;
}

static unsigned
get_max_samples_for_formats_advanced(struct pipe_screen *screen,
                                     unsigned num_formats,
                                     const enum pipe_format *formats,
                                     unsigned max_samples,
                                     unsigned num_storage_samples,
                                     unsigned bind)
{
   unsigned i, f;

   for (i = max_samples; i > 0; --i) {
      for (f = 0; f < num_formats; f++) {
         if (screen->is_format_supported(screen, formats[f], PIPE_TEXTURE_2D,
                                         i, num_storage_samples, bind)) {
            return i;
         }
      }
   }
   return 0;
}

/**
 * Use pipe_screen::get_param() to query pipe_caps. values to determine
 * which GL extensions are supported.
 * Quite a few extensions are always supported because they are standard
 * features or can be built on top of other gallium features.
 * Some fine tuning may still be needed.
 */
void st_init_extensions(struct pipe_screen *screen,
                        struct gl_constants *consts,
                        struct gl_extensions *extensions,
                        struct st_config_options *options,
                        gl_api api)
{
   unsigned i;

   /* Required: render target and sampler support */
   static const struct st_extension_format_mapping rendertarget_mapping[] = {
      { { o(ARB_texture_rgb10_a2ui) },
        { PIPE_FORMAT_R10G10B10A2_UINT,
          PIPE_FORMAT_B10G10R10A2_UINT },
         GL_TRUE }, /* at least one format must be supported */

      { { o(EXT_sRGB) },
        { PIPE_FORMAT_A8B8G8R8_SRGB,
          PIPE_FORMAT_B8G8R8A8_SRGB,
          PIPE_FORMAT_R8G8B8A8_SRGB },
         GL_TRUE }, /* at least one format must be supported */

      { { o(EXT_packed_float) },
        { PIPE_FORMAT_R11G11B10_FLOAT } },

      { { o(EXT_texture_integer) },
        { PIPE_FORMAT_R32G32B32A32_UINT,
          PIPE_FORMAT_R32G32B32A32_SINT } },

      { { o(ARB_texture_rg) },
        { PIPE_FORMAT_R8_UNORM,
          PIPE_FORMAT_R8G8_UNORM } },

      { { o(EXT_texture_norm16) },
        { PIPE_FORMAT_R16_UNORM,
          PIPE_FORMAT_R16G16_UNORM,
          PIPE_FORMAT_R16G16B16A16_UNORM } },

      { { o(EXT_render_snorm) },
        { PIPE_FORMAT_R8_SNORM,
          PIPE_FORMAT_R8G8_SNORM,
          PIPE_FORMAT_R8G8B8A8_SNORM,
          PIPE_FORMAT_R16_SNORM,
          PIPE_FORMAT_R16G16_SNORM,
          PIPE_FORMAT_R16G16B16A16_SNORM } },

      { { o(EXT_color_buffer_half_float) },
        { PIPE_FORMAT_R16_FLOAT,
          PIPE_FORMAT_R16G16_FLOAT,
          PIPE_FORMAT_R16G16B16A16_FLOAT } },

      { { o(EXT_color_buffer_float) },
        { PIPE_FORMAT_R16_FLOAT,
          PIPE_FORMAT_R16G16_FLOAT,
          PIPE_FORMAT_R16G16B16A16_FLOAT,
          PIPE_FORMAT_R32_FLOAT,
          PIPE_FORMAT_R32G32_FLOAT,
          PIPE_FORMAT_R32G32B32A32_FLOAT } },
   };

   /* Required: render target, sampler, and blending */
   static const struct st_extension_format_mapping rt_blendable[] = {
      { { o(EXT_float_blend) },
        { PIPE_FORMAT_R32G32B32A32_FLOAT } },
   };

   /* Required: depth stencil and sampler support */
   static const struct st_extension_format_mapping depthstencil_mapping[] = {
      { { o(ARB_depth_buffer_float) },
        { PIPE_FORMAT_Z32_FLOAT,
          PIPE_FORMAT_Z32_FLOAT_S8X24_UINT } },
   };

   /* Required: sampler support */
   static const struct st_extension_format_mapping texture_mapping[] = {
      { { o(OES_texture_float) },
        { PIPE_FORMAT_R32G32B32A32_FLOAT } },

      { { o(OES_texture_half_float) },
        { PIPE_FORMAT_R16G16B16A16_FLOAT } },

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

      { { o(EXT_texture_compression_s3tc_srgb) },
        { PIPE_FORMAT_DXT1_SRGB,
          PIPE_FORMAT_DXT1_SRGBA,
          PIPE_FORMAT_DXT3_SRGBA,
          PIPE_FORMAT_DXT5_SRGBA } },

      { { o(ARB_texture_compression_bptc) },
        { PIPE_FORMAT_BPTC_RGBA_UNORM,
          PIPE_FORMAT_BPTC_SRGBA,
          PIPE_FORMAT_BPTC_RGB_FLOAT,
          PIPE_FORMAT_BPTC_RGB_UFLOAT } },

      { { o(TDFX_texture_compression_FXT1) },
        { PIPE_FORMAT_FXT1_RGB,
          PIPE_FORMAT_FXT1_RGBA } },

      { { o(KHR_texture_compression_astc_ldr),
          o(KHR_texture_compression_astc_sliced_3d) },
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
	  PIPE_FORMAT_B8G8R8A8_SRGB,
	  PIPE_FORMAT_A8R8G8B8_SRGB,
	  PIPE_FORMAT_R8G8B8A8_SRGB},
        GL_TRUE }, /* at least one format must be supported */

      { { o(EXT_texture_sRGB_R8) },
        { PIPE_FORMAT_R8_SRGB }, },

      { { o(EXT_texture_sRGB_RG8) },
        { PIPE_FORMAT_R8G8_SRGB }, },

      { { o(EXT_texture_type_2_10_10_10_REV) },
        { PIPE_FORMAT_R10G10B10A2_UNORM,
          PIPE_FORMAT_B10G10R10A2_UNORM },
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

      { { o(AMD_compressed_ATC_texture) },
        { PIPE_FORMAT_ATC_RGB,
          PIPE_FORMAT_ATC_RGBA_EXPLICIT,
          PIPE_FORMAT_ATC_RGBA_INTERPOLATED } },
   };

   /* Required: sampler support */
   static const struct st_extension_format_mapping texture_mapping_compressed_fallback[] = {
      { { o(KHR_texture_compression_astc_ldr),
          o(KHR_texture_compression_astc_sliced_3d) },
        { PIPE_FORMAT_R8G8B8A8_UNORM,
          PIPE_FORMAT_R8G8B8A8_SRGB } },

      { { o(ARB_texture_compression_rgtc) },
        { PIPE_FORMAT_R8_UNORM,
          PIPE_FORMAT_R8_SNORM,
          PIPE_FORMAT_R8G8_UNORM,
          PIPE_FORMAT_R8G8_SNORM } },

      { { o(EXT_texture_compression_latc) },
        { PIPE_FORMAT_L8_UNORM,
          PIPE_FORMAT_L8_SNORM,
          PIPE_FORMAT_L8A8_UNORM,
          PIPE_FORMAT_L8A8_SNORM } },

      { { o(EXT_texture_compression_s3tc),
          o(ANGLE_texture_compression_dxt) },
        { PIPE_FORMAT_R8G8B8A8_UNORM } },

      { { o(EXT_texture_compression_s3tc_srgb) },
        { PIPE_FORMAT_R8G8B8A8_SRGB } },

      { { o(ARB_texture_compression_bptc) },
        { PIPE_FORMAT_R8G8B8A8_UNORM,
          PIPE_FORMAT_R8G8B8A8_SRGB,
          PIPE_FORMAT_R16G16B16X16_FLOAT } },

      { { o(ATI_texture_compression_3dc) },
        { PIPE_FORMAT_L8A8_UNORM } },
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

#define EXT_CAP(ext, cap) extensions->ext |= !!screen->caps.cap

   /* Expose the extensions which directly correspond to gallium caps. */
   EXT_CAP(ARB_base_instance,                start_instance);
   EXT_CAP(ARB_bindless_texture,             bindless_texture);
   EXT_CAP(ARB_buffer_storage,               buffer_map_persistent_coherent);
   EXT_CAP(ARB_clip_control,                 clip_halfz);
   EXT_CAP(ARB_color_buffer_float,           vertex_color_unclamped);
   EXT_CAP(ARB_conditional_render_inverted,  conditional_render_inverted);
   EXT_CAP(ARB_copy_image,                   copy_between_compressed_and_plain_formats);
   EXT_CAP(OES_copy_image,                   copy_between_compressed_and_plain_formats);
   EXT_CAP(ARB_cull_distance,                cull_distance);
   EXT_CAP(ARB_depth_clamp,                  depth_clip_disable);
   EXT_CAP(ARB_derivative_control,           fs_fine_derivative);
   EXT_CAP(ARB_draw_buffers_blend,           indep_blend_func);
   EXT_CAP(ARB_draw_indirect,                draw_indirect);
   EXT_CAP(ARB_draw_instanced,               vs_instanceid);
   EXT_CAP(ARB_fragment_program_shadow,      texture_shadow_map);
   EXT_CAP(ARB_framebuffer_object,           mixed_framebuffer_sizes);
   EXT_CAP(ARB_gpu_shader_int64,             int64);
   EXT_CAP(ARB_gl_spirv,                     gl_spirv);
   EXT_CAP(ARB_indirect_parameters,          multi_draw_indirect_params);
   EXT_CAP(ARB_instanced_arrays,             vertex_element_instance_divisor);
   EXT_CAP(ARB_occlusion_query2,             occlusion_query);
   EXT_CAP(ARB_pipeline_statistics_query,    query_pipeline_statistics);
   EXT_CAP(ARB_pipeline_statistics_query,    query_pipeline_statistics_single);
   EXT_CAP(ARB_polygon_offset_clamp,         polygon_offset_clamp);
   EXT_CAP(ARB_post_depth_coverage,          post_depth_coverage);
   EXT_CAP(ARB_query_buffer_object,          query_buffer_object);
   EXT_CAP(ARB_robust_buffer_access_behavior, robust_buffer_access_behavior);
   EXT_CAP(ARB_sample_shading,               sample_shading);
   EXT_CAP(ARB_sample_locations,             programmable_sample_locations);
   EXT_CAP(ARB_seamless_cube_map,            seamless_cube_map);
   EXT_CAP(ARB_shader_ballot,                shader_ballot);
   EXT_CAP(ARB_shader_clock,                 shader_clock);
   EXT_CAP(ARB_shader_draw_parameters,       draw_parameters);
   EXT_CAP(ARB_shader_group_vote,            shader_group_vote);
   EXT_CAP(EXT_shader_image_load_formatted,  image_load_formatted);
   EXT_CAP(EXT_shader_image_load_store,      image_atomic_inc_wrap);
   EXT_CAP(ARB_shader_stencil_export,        shader_stencil_export);
   EXT_CAP(ARB_shader_texture_image_samples, texture_query_samples);
   EXT_CAP(ARB_shader_texture_lod,           fragment_shader_texture_lod);
   EXT_CAP(ARB_shadow,                       texture_shadow_map);
   EXT_CAP(ARB_sparse_buffer,                sparse_buffer_page_size);
   EXT_CAP(ARB_sparse_texture,               max_sparse_texture_size);
   EXT_CAP(ARB_sparse_texture2,              query_sparse_texture_residency);
   EXT_CAP(ARB_sparse_texture_clamp,         clamp_sparse_texture_lod);
   EXT_CAP(ARB_spirv_extensions,             gl_spirv);
   EXT_CAP(ARB_texture_buffer_object,        texture_buffer_objects);
   EXT_CAP(ARB_texture_cube_map_array,       cube_map_array);
   EXT_CAP(ARB_texture_filter_minmax,        sampler_reduction_minmax_arb);
   EXT_CAP(ARB_texture_gather,               max_texture_gather_components);
   EXT_CAP(ARB_texture_mirror_clamp_to_edge, texture_mirror_clamp_to_edge);
   EXT_CAP(ARB_texture_multisample,          texture_multisample);
   EXT_CAP(ARB_texture_non_power_of_two,     npot_textures);
   EXT_CAP(ARB_texture_query_lod,            texture_query_lod);
   EXT_CAP(ARB_texture_view,                 sampler_view_target);
   EXT_CAP(ARB_timer_query,                  query_timestamp);
   EXT_CAP(ARB_transform_feedback2,          stream_output_pause_resume);
   EXT_CAP(ARB_transform_feedback3,          stream_output_interleave_buffers);
   EXT_CAP(ARB_transform_feedback_overflow_query, query_so_overflow);
   EXT_CAP(ARB_fragment_shader_interlock,    fragment_shader_interlock);

   EXT_CAP(EXT_blend_equation_separate,      blend_equation_separate);
   EXT_CAP(EXT_demote_to_helper_invocation,  demote_to_helper_invocation);
   EXT_CAP(EXT_depth_bounds_test,            depth_bounds_test);
   EXT_CAP(EXT_disjoint_timer_query,         query_timestamp);
   EXT_CAP(EXT_draw_buffers2,                indep_blend_enable);
   EXT_CAP(EXT_memory_object,                memobj);
#ifndef _WIN32
   EXT_CAP(EXT_memory_object_fd,             memobj);
#else
   EXT_CAP(EXT_memory_object_win32,          memobj);
#endif
   EXT_CAP(EXT_multisampled_render_to_texture, surface_sample_count);
   EXT_CAP(EXT_semaphore,                    fence_signal);
#ifndef _WIN32
   EXT_CAP(EXT_semaphore_fd,                 fence_signal);
#else
   EXT_CAP(EXT_semaphore_win32,              fence_signal);
#endif
   EXT_CAP(EXT_shader_samples_identical,     shader_samples_identical);
   EXT_CAP(EXT_texture_array,                max_texture_array_layers);
   EXT_CAP(EXT_texture_compression_astc_decode_mode, astc_decode_mode);
   EXT_CAP(EXT_texture_filter_anisotropic,   anisotropic_filter);
   EXT_CAP(EXT_texture_filter_minmax,        sampler_reduction_minmax);
   EXT_CAP(EXT_texture_mirror_clamp,         texture_mirror_clamp);
   EXT_CAP(EXT_texture_shadow_lod,           texture_shadow_lod);
   EXT_CAP(EXT_texture_swizzle,              texture_swizzle);
   EXT_CAP(EXT_transform_feedback,           max_stream_output_buffers);
   EXT_CAP(EXT_window_rectangles,            max_window_rectangles);

   EXT_CAP(KHR_shader_subgroup,              shader_subgroup_size);

   EXT_CAP(AMD_depth_clamp_separate,         depth_clip_disable_separate);
   EXT_CAP(AMD_framebuffer_multisample_advanced, framebuffer_msaa_constraints);
   EXT_CAP(AMD_gpu_shader_half_float,        fp16);
   EXT_CAP(AMD_performance_monitor,          performance_monitor);
   EXT_CAP(AMD_pinned_memory,                resource_from_user_memory);
   EXT_CAP(ATI_meminfo,                      query_memory_info);
   EXT_CAP(AMD_seamless_cubemap_per_texture, seamless_cube_map_per_texture);
   EXT_CAP(ATI_texture_mirror_once,          texture_mirror_clamp);
   EXT_CAP(INTEL_conservative_rasterization, conservative_raster_inner_coverage);
   EXT_CAP(INTEL_shader_atomic_float_minmax, atomic_float_minmax);
   EXT_CAP(MESA_tile_raster_order,           tile_raster_order);
   EXT_CAP(NV_alpha_to_coverage_dither_control, alpha_to_coverage_dither_control);
   EXT_CAP(NV_compute_shader_derivatives,    compute_shader_derivatives);
   EXT_CAP(NV_conditional_render,            conditional_render);
   EXT_CAP(NV_fill_rectangle,                polygon_mode_fill_rectangle);
   EXT_CAP(NV_primitive_restart,             primitive_restart);
   EXT_CAP(NV_shader_atomic_float,           image_atomic_float_add);
   EXT_CAP(NV_shader_atomic_int64,           shader_atomic_int64);
   EXT_CAP(NV_texture_barrier,               texture_barrier);
   EXT_CAP(NV_viewport_array2,               viewport_mask);
   EXT_CAP(NV_viewport_swizzle,              viewport_swizzle);
   EXT_CAP(NVX_gpu_memory_info,              query_memory_info);

   EXT_CAP(OES_standard_derivatives,         fragment_shader_derivatives);
   EXT_CAP(OES_texture_float_linear,         texture_float_linear);
   EXT_CAP(OES_texture_half_float_linear,    texture_half_float_linear);
   EXT_CAP(OES_texture_view,                 sampler_view_target);
   EXT_CAP(INTEL_blackhole_render,           frontend_noop);
   EXT_CAP(ARM_shader_framebuffer_fetch_depth_stencil, fbfetch_zs);
   EXT_CAP(MESA_texture_const_bandwidth,     has_const_bw);

#undef EXT_CAP

   /* MESA_texture_const_bandwidth depends on EXT_memory_object */
   if (!extensions->EXT_memory_object)
      extensions->MESA_texture_const_bandwidth = GL_FALSE;

   /* EXT implies ARB here */
   if (extensions->EXT_texture_filter_minmax)
      extensions->ARB_texture_filter_minmax = GL_TRUE;

   /* Expose the extensions which directly correspond to gallium formats. */
   init_format_extensions(screen, extensions, rendertarget_mapping,
                          ARRAY_SIZE(rendertarget_mapping), PIPE_TEXTURE_2D,
                          PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(screen, extensions, rt_blendable,
                          ARRAY_SIZE(rt_blendable), PIPE_TEXTURE_2D,
                          PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW |
                          PIPE_BIND_BLENDABLE);
   init_format_extensions(screen, extensions, depthstencil_mapping,
                          ARRAY_SIZE(depthstencil_mapping), PIPE_TEXTURE_2D,
                          PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(screen, extensions, texture_mapping,
                          ARRAY_SIZE(texture_mapping), PIPE_TEXTURE_2D,
                          PIPE_BIND_SAMPLER_VIEW);
   if (options->allow_compressed_fallback)
      init_format_extensions(screen, extensions,
                             texture_mapping_compressed_fallback,
                             ARRAY_SIZE(texture_mapping_compressed_fallback),
                             PIPE_TEXTURE_2D, PIPE_BIND_SAMPLER_VIEW);
   init_format_extensions(screen, extensions, vertex_mapping,
                          ARRAY_SIZE(vertex_mapping), PIPE_BUFFER,
                          PIPE_BIND_VERTEX_BUFFER);

   /* Figure out GLSL support and set GLSLVersion to it. */
   consts->GLSLVersion = screen->caps.glsl_feature_level;
   consts->GLSLVersionCompat =
      screen->caps.glsl_feature_level_compatibility;

   const unsigned ESSLVersion =
      screen->caps.essl_feature_level;
   const unsigned GLSLVersion =
      api == API_OPENGL_COMPAT ? consts->GLSLVersionCompat :
                                 consts->GLSLVersion;

   _mesa_override_glsl_version(consts);

   if (options->force_glsl_version > 0 &&
       options->force_glsl_version <= GLSLVersion) {
      consts->ForceGLSLVersion = options->force_glsl_version;
   }

   consts->ForceCompatShaders = options->force_compat_shaders;

   consts->AllowExtraPPTokens = options->allow_extra_pp_tokens;

   consts->AllowHigherCompatVersion = options->allow_higher_compat_version;
   consts->AllowGLSLCompatShaders = options->allow_glsl_compat_shaders;

   consts->ForceGLSLAbsSqrt = options->force_glsl_abs_sqrt;

   consts->AllowGLSLBuiltinVariableRedeclaration = options->allow_glsl_builtin_variable_redeclaration;

   consts->dri_config_options_sha1 = options->config_options_sha1;

   consts->AllowGLSLCrossStageInterpolationMismatch = options->allow_glsl_cross_stage_interpolation_mismatch;

   consts->DoDCEBeforeClipCullAnalysis = options->do_dce_before_clip_cull_analysis;

   consts->GLSLIgnoreWriteToReadonlyVar = options->glsl_ignore_write_to_readonly_var;

   consts->ForceMapBufferSynchronized = options->force_gl_map_buffer_synchronized;

   consts->PrimitiveRestartFixedIndex =
      screen->caps.primitive_restart_fixed_index;

   /* Technically we are turning on the EXT_gpu_shader5 extension,
    * ARB_gpu_shader5 does not exist in GLES, but this flag is what
    * switches on EXT_gpu_shader5:
    */
   if (api == API_OPENGLES2 && ESSLVersion >= 320)
      extensions->ARB_gpu_shader5 = GL_TRUE;

   if (GLSLVersion >= 400 && !options->disable_arb_gpu_shader5)
      extensions->ARB_gpu_shader5 = GL_TRUE;
   if (GLSLVersion >= 410)
      extensions->ARB_shader_precision = GL_TRUE;

   /* This extension needs full OpenGL 3.2, but we don't know if that's
    * supported at this point. Only check the GLSL version. */
   if (GLSLVersion >= 150 &&
       screen->caps.vs_layer_viewport) {
      extensions->AMD_vertex_shader_layer = GL_TRUE;
   }

   if (GLSLVersion >= 140) {
      /* Since GLSL 1.40 has support for all of the features of gpu_shader4,
       * we can always expose it if the driver can do 140. Supporting
       * gpu_shader4 on drivers without GLSL 1.40 is left for a future
       * pipe cap.
       */
      extensions->EXT_gpu_shader4 = GL_TRUE;
      extensions->EXT_texture_buffer_object = GL_TRUE;

      if (consts->MaxTransformFeedbackBuffers &&
          screen->caps.shader_array_components)
         extensions->ARB_enhanced_layouts = GL_TRUE;
   }

   if (GLSLVersion >= 130) {
      consts->NativeIntegers = GL_TRUE;
      consts->MaxClipPlanes = 8;

      uint32_t drv_clip_planes = screen->caps.clip_planes;
      /* only override for > 1 - 0 if none, 1 is MAX, >2 overrides MAX */
      if (drv_clip_planes > 1)
         consts->MaxClipPlanes = drv_clip_planes;

      /* Extensions that either depend on GLSL 1.30 or are a subset thereof. */
      extensions->ARB_conservative_depth = GL_TRUE;
      extensions->ARB_shading_language_packing = GL_TRUE;
      extensions->OES_depth_texture_cube_map = GL_TRUE;
      extensions->ARB_shading_language_420pack = GL_TRUE;
      extensions->ARB_texture_query_levels = GL_TRUE;

      extensions->ARB_shader_bit_encoding = GL_TRUE;

      extensions->EXT_shader_integer_mix = GL_TRUE;
      extensions->ARB_arrays_of_arrays = GL_TRUE;
      extensions->MESA_shader_integer_functions = GL_TRUE;

      switch (screen->caps.multiview) {
      case 1:
         extensions->OVR_multiview = GL_TRUE;
         break;
      case 2:
         extensions->OVR_multiview = GL_TRUE;
         extensions->OVR_multiview2 = GL_TRUE;
         break;
      }

      extensions->OVR_multiview_multisampled_render_to_texture = extensions->EXT_multisampled_render_to_texture &&
                                                                 extensions->OVR_multiview;

      if (screen->caps.opencl_integer_functions &&
          screen->caps.integer_multiply_32x16) {
         extensions->INTEL_shader_integer_functions2 = GL_TRUE;
      }
   } else {
      /* Optional integer support for GLSL 1.2. */
      if (screen->shader_caps[PIPE_SHADER_VERTEX].integers &&
          screen->shader_caps[PIPE_SHADER_FRAGMENT].integers) {
         consts->NativeIntegers = GL_TRUE;

         extensions->EXT_shader_integer_mix = GL_TRUE;
      }

      /* Integer textures make no sense before GLSL 1.30 */
      extensions->EXT_texture_integer = GL_FALSE;
      extensions->ARB_texture_rgb10_a2ui = GL_FALSE;
   }

   if (options->glsl_zero_init) {
      consts->GLSLZeroInit = 1;
   } else {
      consts->GLSLZeroInit = screen->caps.glsl_zero_init;
   }

   consts->ForceIntegerTexNearest = options->force_integer_tex_nearest;

   consts->VendorOverride = options->force_gl_vendor;
   consts->RendererOverride = options->force_gl_renderer;

   consts->UniformBooleanTrue = consts->NativeIntegers ? ~0U : fui(1.0f);

   /* Below are the cases which cannot be moved into tables easily. */

   /* The compatibility profile also requires GLSLVersionCompat >= 400. */
   if (screen->shader_caps[PIPE_SHADER_TESS_CTRL].max_instructions > 0 &&
       (api != API_OPENGL_COMPAT || consts->GLSLVersionCompat >= 400)) {
      extensions->ARB_tessellation_shader = GL_TRUE;
   }

   /* OES_geometry_shader requires instancing */
   if ((GLSLVersion >= 400 || ESSLVersion >= 310) &&
       screen->shader_caps[PIPE_SHADER_GEOMETRY].max_instructions > 0 &&
       consts->MaxGeometryShaderInvocations >= 32) {
      extensions->OES_geometry_shader = GL_TRUE;
   }

   /* Some hardware may not support indirect draws, but still wants ES
    * 3.1. This allows the extension to be enabled only in ES contexts to
    * avoid claiming hw support when there is none, and using a software
    * fallback for ES.
    */
   if (api == API_OPENGLES2 && ESSLVersion >= 310) {
      extensions->ARB_draw_indirect = GL_TRUE;
   }

   /* Needs pipe_caps.sample_shading + all the sample-related bits of
    * ARB_gpu_shader5. This enables all the per-sample shading ES extensions.
    */
   extensions->OES_sample_variables = extensions->ARB_sample_shading &&
      extensions->ARB_gpu_shader5;

   /* Maximum sample count. */
   {
      static const enum pipe_format color_formats[] = {
         PIPE_FORMAT_R8G8B8A8_UNORM,
         PIPE_FORMAT_B8G8R8A8_UNORM,
         PIPE_FORMAT_A8R8G8B8_UNORM,
         PIPE_FORMAT_A8B8G8R8_UNORM,
      };
      static const enum pipe_format depth_formats[] = {
         PIPE_FORMAT_Z16_UNORM,
         PIPE_FORMAT_Z24X8_UNORM,
         PIPE_FORMAT_X8Z24_UNORM,
         PIPE_FORMAT_Z32_UNORM,
         PIPE_FORMAT_Z32_FLOAT
      };
      static const enum pipe_format int_formats[] = {
         PIPE_FORMAT_R8G8B8A8_SINT
      };
      static const enum pipe_format void_formats[] = {
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

      if (extensions->AMD_framebuffer_multisample_advanced) {
         /* AMD_framebuffer_multisample_advanced */
         /* This can be greater than storage samples. */
         consts->MaxColorFramebufferSamples =
            get_max_samples_for_formats_advanced(screen,
                                                ARRAY_SIZE(color_formats),
                                                color_formats, 16,
                                                consts->MaxSamples,
                                                PIPE_BIND_RENDER_TARGET);

         /* If the driver supports N color samples, it means it supports
          * N samples and N storage samples. N samples >= N storage
          * samples.
          */
         consts->MaxColorFramebufferStorageSamples = consts->MaxSamples;
         consts->MaxDepthStencilFramebufferSamples =
            consts->MaxDepthTextureSamples;

         assert(consts->MaxColorFramebufferSamples >=
                consts->MaxDepthStencilFramebufferSamples);
         assert(consts->MaxDepthStencilFramebufferSamples >=
                consts->MaxColorFramebufferStorageSamples);

         consts->NumSupportedMultisampleModes = 0;

         unsigned depth_samples_supported = 0;

         for (unsigned samples = 2;
              samples <= consts->MaxDepthStencilFramebufferSamples;
              samples++) {
            if (screen->is_format_supported(screen, PIPE_FORMAT_Z32_FLOAT,
                                            PIPE_TEXTURE_2D, samples, samples,
                                            PIPE_BIND_DEPTH_STENCIL))
               depth_samples_supported |= 1 << samples;
         }

         for (unsigned samples = 2;
              samples <= consts->MaxColorFramebufferSamples;
              samples++) {
            for (unsigned depth_samples = 2;
                 depth_samples <= samples; depth_samples++) {
               if (!(depth_samples_supported & (1 << depth_samples)))
                  continue;

               for (unsigned storage_samples = 2;
                    storage_samples <= depth_samples; storage_samples++) {
                  if (screen->is_format_supported(screen,
                                                  PIPE_FORMAT_R8G8B8A8_UNORM,
                                                  PIPE_TEXTURE_2D,
                                                  samples,
                                                  storage_samples,
                                                  PIPE_BIND_RENDER_TARGET)) {
                     unsigned i = consts->NumSupportedMultisampleModes;

                     assert(i < ARRAY_SIZE(consts->SupportedMultisampleModes));
                     consts->SupportedMultisampleModes[i].NumColorSamples =
                        samples;
                     consts->SupportedMultisampleModes[i].NumColorStorageSamples =
                        storage_samples;
                     consts->SupportedMultisampleModes[i].NumDepthStencilSamples =
                        depth_samples;
                     consts->NumSupportedMultisampleModes++;
                  }
               }
            }
         }
      }
   }

   if (consts->MaxSamples >= 2) {
      /* Real MSAA support */
      extensions->EXT_framebuffer_multisample = GL_TRUE;
      extensions->EXT_framebuffer_multisample_blit_scaled = GL_TRUE;
   }
   else if (consts->MaxSamples > 0 &&
            screen->caps.fake_sw_msaa) {
      /* fake MSAA support */
      consts->FakeSWMSAA = GL_TRUE;
      extensions->EXT_framebuffer_multisample = GL_TRUE;
      extensions->EXT_framebuffer_multisample_blit_scaled = GL_TRUE;
      extensions->ARB_texture_multisample = GL_TRUE;
   }

   if (consts->MaxDualSourceDrawBuffers > 0 &&
       !options->disable_blend_func_extended)
      extensions->ARB_blend_func_extended = GL_TRUE;

   if (screen->caps.query_time_elapsed ||
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

   if (options->disable_uniform_array_resize)
      consts->DisableUniformArrayResize = 1;

   consts->AliasShaderExtension = options->alias_shader_extension;

   if (options->allow_vertex_texture_bias)
      consts->AllowVertexTextureBias = GL_TRUE;

   if (options->allow_glsl_extension_directive_midshader)
      consts->AllowGLSLExtensionDirectiveMidShader = GL_TRUE;

   if (options->allow_glsl_120_subset_in_110)
      consts->AllowGLSL120SubsetIn110 = GL_TRUE;

   if (options->allow_glsl_builtin_const_expression)
      consts->AllowGLSLBuiltinConstantExpression = GL_TRUE;

   if (options->allow_glsl_relaxed_es)
      consts->AllowGLSLRelaxedES = GL_TRUE;

   consts->MinMapBufferAlignment =
      screen->caps.min_map_buffer_alignment;

   /* The OpenGL Compatibility profile requires arbitrary buffer swizzling. */
   if (api == API_OPENGL_COMPAT &&
       screen->caps.buffer_sampler_view_rgba_only)
      extensions->ARB_texture_buffer_object = GL_FALSE;

   if (extensions->ARB_texture_buffer_object) {
      consts->MaxTextureBufferSize =
         screen->caps.max_texel_buffer_elements;
      consts->TextureBufferOffsetAlignment =
         screen->caps.texture_buffer_offset_alignment;

      if (consts->TextureBufferOffsetAlignment)
         extensions->ARB_texture_buffer_range = GL_TRUE;

      init_format_extensions(screen, extensions, tbo_rgb32,
                             ARRAY_SIZE(tbo_rgb32), PIPE_BUFFER,
                             PIPE_BIND_SAMPLER_VIEW);
   }

   extensions->OES_texture_buffer =
      consts->Program[MESA_SHADER_COMPUTE].MaxImageUniforms &&
      extensions->ARB_texture_buffer_object &&
      extensions->ARB_texture_buffer_range &&
      extensions->ARB_texture_buffer_object_rgb32;

   extensions->EXT_framebuffer_sRGB =
         screen->caps.dest_surface_srgb_control &&
         extensions->EXT_sRGB;

   /* Unpacking a varying in the fragment shader costs 1 texture indirection.
    * If the number of available texture indirections is very limited, then we
    * prefer to disable varying packing rather than run the risk of varying
    * packing preventing a shader from running.
    */
   if (screen->shader_caps[PIPE_SHADER_FRAGMENT].max_tex_indirections <= 8) {
      /* We can't disable varying packing if transform feedback is available,
       * because transform feedback code assumes a packed varying layout.
       */
      if (!extensions->EXT_transform_feedback)
         consts->DisableVaryingPacking = GL_TRUE;
   }

   if (!screen->caps.packed_stream_output)
      consts->DisableTransformFeedbackPacking = GL_TRUE;

   if (screen->caps.prefer_pot_aligned_varyings)
      consts->PreferPOTAlignedVaryings = GL_TRUE;

   unsigned max_fb_fetch_rts = screen->caps.fbfetch;
   bool coherent_fb_fetch = screen->caps.fbfetch_coherent;

   if (screen->caps.blend_equation_advanced)
      extensions->KHR_blend_equation_advanced = true;

   if (max_fb_fetch_rts > 0) {
      extensions->KHR_blend_equation_advanced = true;
      extensions->KHR_blend_equation_advanced_coherent = coherent_fb_fetch;

      if (max_fb_fetch_rts >=
          screen->caps.max_render_targets) {
         extensions->EXT_shader_framebuffer_fetch_non_coherent = true;
         extensions->EXT_shader_framebuffer_fetch = coherent_fb_fetch;
      }
   }

   consts->MaxViewports = screen->caps.max_viewports;
   if (consts->MaxViewports >= 16) {
      if (GLSLVersion >= 400) {
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

   if (extensions->AMD_vertex_shader_layer &&
       extensions->AMD_vertex_shader_viewport_index &&
       screen->caps.tes_layer_viewport)
      extensions->ARB_shader_viewport_layer_array = GL_TRUE;

   /* ARB_framebuffer_no_attachments */
   if (screen->caps.framebuffer_no_attachment &&
       ((consts->MaxSamples >= 4 && consts->MaxFramebufferLayers >= 2048) ||
        (consts->MaxFramebufferSamples >= consts->MaxSamples &&
         consts->MaxFramebufferLayers >= consts->MaxArrayTextureLayers)))
      extensions->ARB_framebuffer_no_attachments = GL_TRUE;

   /* GL_ARB_ES3_compatibility.
    * Check requirements for GLSL ES 3.00.
    */
   if (GLSLVersion >= 130 &&
       extensions->ARB_uniform_buffer_object &&
       (extensions->NV_primitive_restart ||
        consts->PrimitiveRestartFixedIndex) &&
       screen->shader_caps[PIPE_SHADER_VERTEX].max_texture_samplers >= 16 &&
       /* Requirements for ETC2 emulation. */
       screen->is_format_supported(screen, PIPE_FORMAT_R8G8B8A8_UNORM,
                                   PIPE_TEXTURE_2D, 0, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R8G8B8A8_SRGB,
                                   PIPE_TEXTURE_2D, 0, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R16_UNORM,
                                   PIPE_TEXTURE_2D, 0, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R16G16_UNORM,
                                   PIPE_TEXTURE_2D, 0, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R16_SNORM,
                                   PIPE_TEXTURE_2D, 0, 0,
                                   PIPE_BIND_SAMPLER_VIEW) &&
       screen->is_format_supported(screen, PIPE_FORMAT_R16G16_SNORM,
                                   PIPE_TEXTURE_2D, 0, 0,
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

   if (screen->caps.doubles) {
      extensions->ARB_gpu_shader_fp64 = GL_TRUE;
      extensions->ARB_vertex_attrib_64bit = GL_TRUE;
   }

   if ((ST_DEBUG & DEBUG_GREMEDY) &&
       screen->caps.string_marker)
      extensions->GREMEDY_string_marker = GL_TRUE;

   if (screen->caps.compute) {
      consts->MaxComputeWorkGroupInvocations = screen->compute_caps.max_threads_per_block;
      consts->MaxComputeSharedMemorySize = screen->compute_caps.max_local_size;

      for (i = 0; i < 3; i++) {
         /* There are tests that fail if we report more that INT_MAX - 1. */
         consts->MaxComputeWorkGroupCount[i] = MIN2(screen->compute_caps.max_grid_size[i], INT_MAX - 1);
         consts->MaxComputeWorkGroupSize[i] = screen->compute_caps.max_block_size[i];
      }

      extensions->ARB_compute_shader =
         screen->compute_caps.max_threads_per_block >= 1024 &&
         extensions->ARB_shader_image_load_store &&
         extensions->ARB_shader_atomic_counters;

      if (extensions->ARB_compute_shader) {
         unsigned max_variable_threads_per_block =
            screen->compute_caps.max_variable_threads_per_block;

         for (i = 0; i < 3; i++) {
            /* Clamp the values to avoid having a local work group size
               * greater than the maximum number of invocations.
               */
            consts->MaxComputeVariableGroupSize[i] =
               MIN2(consts->MaxComputeWorkGroupSize[i],
                     max_variable_threads_per_block);
         }
         consts->MaxComputeVariableGroupInvocations =
            max_variable_threads_per_block;

         extensions->ARB_compute_variable_group_size =
            max_variable_threads_per_block > 0;
      }
   }

   /* Technically speaking, there's no phrasing in the ARB_texture_float spec
    * that allows ARB_texture_float to be supported without also supporting
    * linear interpolation for them. However, being strict about this would
    * make us drop OpenGL 3.0 support for a lot of GPUs, which is bad.
    */
   extensions->ARB_texture_float =
      extensions->OES_texture_half_float &&
      extensions->OES_texture_float;

   if (extensions->EXT_texture_filter_anisotropic &&
       screen->caps.max_texture_anisotropy >= 16.0)
      extensions->ARB_texture_filter_anisotropic = GL_TRUE;

   extensions->KHR_robustness = extensions->ARB_robust_buffer_access_behavior;

   /* If we support ES 3.1, we support the ES3_1_compatibility ext. However
    * there's no clean way of telling whether we would support ES 3.1 from
    * here, so copy the condition from compute_version_es2 here. A lot of
    * these are redunant, but simpler to just have a (near-)exact copy here.
    */
   extensions->ARB_ES3_1_compatibility =
      consts->Program[MESA_SHADER_FRAGMENT].MaxImageUniforms &&
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

   extensions->OES_texture_cube_map_array =
      (extensions->ARB_ES3_1_compatibility || ESSLVersion >= 310) &&
      extensions->OES_geometry_shader &&
      extensions->ARB_texture_cube_map_array;

   extensions->OES_viewport_array =
      (extensions->ARB_ES3_1_compatibility || ESSLVersion >= 310) &&
      extensions->OES_geometry_shader &&
      extensions->ARB_viewport_array;

   extensions->OES_primitive_bounding_box =
      extensions->ARB_ES3_1_compatibility || ESSLVersion >= 310;

   consts->NoPrimitiveBoundingBoxOutput = true;

   extensions->ANDROID_extension_pack_es31a =
      consts->Program[MESA_SHADER_FRAGMENT].MaxImageUniforms &&
      extensions->KHR_texture_compression_astc_ldr &&
      extensions->KHR_blend_equation_advanced &&
      extensions->OES_sample_variables &&
      extensions->ARB_texture_stencil8 &&
      extensions->ARB_texture_multisample &&
      extensions->OES_copy_image &&
      extensions->ARB_draw_buffers_blend &&
      extensions->OES_geometry_shader &&
      extensions->ARB_gpu_shader5 &&
      extensions->OES_primitive_bounding_box &&
      extensions->ARB_tessellation_shader &&
      extensions->OES_texture_buffer &&
      extensions->OES_texture_cube_map_array &&
      extensions->EXT_texture_sRGB_decode;

   /* Same deal as for ARB_ES3_1_compatibility - this has to be computed
    * before overall versions are selected. Also it's actually a subset of ES
    * 3.2, since it doesn't require ASTC or advanced blending.
    */
   extensions->ARB_ES3_2_compatibility =
      extensions->ARB_ES3_1_compatibility &&
      extensions->KHR_robustness &&
      extensions->ARB_copy_image &&
      extensions->ARB_draw_buffers_blend &&
      extensions->ARB_draw_elements_base_vertex &&
      extensions->OES_geometry_shader &&
      extensions->ARB_gpu_shader5 &&
      extensions->ARB_sample_shading &&
      extensions->ARB_tessellation_shader &&
      extensions->OES_texture_buffer &&
      extensions->ARB_texture_cube_map_array &&
      extensions->ARB_texture_stencil8 &&
      extensions->ARB_texture_multisample;

   if (screen->caps.conservative_raster_post_snap_triangles &&
       screen->caps.conservative_raster_post_snap_points_lines &&
       screen->caps.conservative_raster_post_depth_coverage) {
      float max_dilate;
      bool pre_snap_triangles, pre_snap_points_lines;

      max_dilate = screen->caps.max_conservative_raster_dilate;

      pre_snap_triangles =
         screen->caps.conservative_raster_pre_snap_triangles;
      pre_snap_points_lines =
         screen->caps.conservative_raster_pre_snap_points_lines;

      extensions->NV_conservative_raster =
         screen->caps.max_conservative_raster_subpixel_precision_bias > 1;

      if (extensions->NV_conservative_raster) {
         extensions->NV_conservative_raster_dilate = max_dilate >= 0.75;
         extensions->NV_conservative_raster_pre_snap_triangles = pre_snap_triangles;
         extensions->NV_conservative_raster_pre_snap =
            pre_snap_triangles && pre_snap_points_lines;
      }
   }

   if (extensions->ARB_gl_spirv) {
      consts->SpirVExtensions = CALLOC_STRUCT(spirv_supported_extensions);
      consts->SpirVExtensions->supported[SPV_KHR_shader_draw_parameters] =
         extensions->ARB_shader_draw_parameters;
      consts->SpirVExtensions->supported[SPV_KHR_storage_buffer_storage_class] = true;
      consts->SpirVExtensions->supported[SPV_KHR_variable_pointers] =
         screen->caps.gl_spirv_variable_pointers;
      consts->SpirVExtensions->supported[SPV_KHR_shader_ballot] =
         extensions->ARB_shader_ballot;
      consts->SpirVExtensions->supported[SPV_KHR_subgroup_vote] =
         extensions->ARB_shader_group_vote;
   }

   consts->AllowDrawOutOfOrder =
      api == API_OPENGL_COMPAT &&
      options->allow_draw_out_of_order &&
      screen->caps.allow_draw_out_of_order;
   consts->GLThreadNopCheckFramebufferStatus = options->glthread_nop_check_framebuffer_status;

   const struct nir_shader_compiler_options *nir_options =
      consts->ShaderCompilerOptions[MESA_SHADER_FRAGMENT].NirOptions;

   if (screen->shader_caps[PIPE_SHADER_FRAGMENT].integers &&
       extensions->ARB_stencil_texturing &&
       screen->caps.doubles &&
       !(nir_options->lower_doubles_options & nir_lower_fp64_full_software))
      extensions->NV_copy_depth_to_color = true;
   if (screen->caps.device_protected_surface || screen->caps.device_protected_context)
      extensions->EXT_protected_textures = true;
}
