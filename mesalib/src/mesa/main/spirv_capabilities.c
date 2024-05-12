/*
 * Copyright 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * \file
 * \brief SPIRV-V capability handling.
 */

#include "spirv_capabilities.h"
#include "compiler/spirv/spirv_info.h"

void
_mesa_fill_supported_spirv_capabilities(struct spirv_capabilities *caps,
                                        struct gl_constants *consts,
                                        const struct gl_extensions *gl_exts)
{
   const struct spirv_supported_extensions *spirv_exts = consts->SpirVExtensions;

   *caps = (struct spirv_capabilities) {
      /* These come from the table in GL_ARB_gl_spirv */
      .Matrix                             = true,
      .Shader                             = true,
      .Geometry                           = true,
      .Tessellation                       = gl_exts->ARB_tessellation_shader,
      .Float64                            = gl_exts->ARB_gpu_shader_fp64,
      .AtomicStorage                      = gl_exts->ARB_shader_atomic_counters,
      .TessellationPointSize              = gl_exts->ARB_tessellation_shader,
      .GeometryPointSize                  = true,
      .ImageGatherExtended                = gl_exts->ARB_gpu_shader5,
      .StorageImageMultisample            = gl_exts->ARB_shader_image_load_store &&
                                            consts->MaxImageSamples > 1,
      .UniformBufferArrayDynamicIndexing  = gl_exts->ARB_gpu_shader5,
      .SampledImageArrayDynamicIndexing   = gl_exts->ARB_gpu_shader5,
      .StorageBufferArrayDynamicIndexing  = gl_exts->ARB_shader_storage_buffer_object,
      .StorageImageArrayDynamicIndexing   = gl_exts->ARB_shader_image_load_store,
      .ClipDistance                       = true,
      .CullDistance                       = gl_exts->ARB_cull_distance,
      .ImageCubeArray                     = gl_exts->ARB_texture_cube_map_array,
      .SampleRateShading                  = gl_exts->ARB_sample_shading,
      .ImageRect                          = true,
      .SampledRect                        = true,
      .Sampled1D                          = true,
      .Image1D                            = true,
      .SampledCubeArray                   = gl_exts->ARB_texture_cube_map_array,
      .SampledBuffer                      = true,
      .ImageBuffer                        = true,
      .ImageMSArray                       = true,
      .StorageImageExtendedFormats        = gl_exts->ARB_shader_image_load_store,
      .ImageQuery                         = true,
      .DerivativeControl                  = gl_exts->ARB_derivative_control,
      .InterpolationFunction              = gl_exts->ARB_gpu_shader5,
      .TransformFeedback                  = true,
      .GeometryStreams                    = gl_exts->ARB_gpu_shader5,
      .StorageImageWriteWithoutFormat     = gl_exts->ARB_shader_image_load_store,
      .MultiViewport                      = gl_exts->ARB_viewport_array,

      /* These aren't in the main table for some reason */
      .Int64                              = gl_exts->ARB_gpu_shader_int64,
      .SparseResidency                    = gl_exts->ARB_sparse_texture2,
      .MinLod                             = gl_exts->ARB_sparse_texture_clamp,
      .StorageImageReadWithoutFormat      = gl_exts->EXT_shader_image_load_formatted,
      .Int64Atomics                       = gl_exts->NV_shader_atomic_int64,

      /* These come from their individual extension specs */
      .DemoteToHelperInvocation           = gl_exts->EXT_demote_to_helper_invocation,
      .DrawParameters                     = gl_exts->ARB_shader_draw_parameters &&
                                            spirv_exts->supported[SPV_KHR_shader_draw_parameters],
      .ComputeDerivativeGroupQuadsNV      = gl_exts->NV_compute_shader_derivatives,
      .ComputeDerivativeGroupLinearNV     = gl_exts->NV_compute_shader_derivatives,
      .SampleMaskPostDepthCoverage        = gl_exts->ARB_post_depth_coverage,
      .ShaderClockKHR                     = gl_exts->ARB_shader_clock,
      .ShaderViewportIndexLayerEXT        = gl_exts->ARB_shader_viewport_layer_array,
      .StencilExportEXT                   = gl_exts->ARB_shader_stencil_export,
      .SubgroupBallotKHR                  = gl_exts->ARB_shader_ballot &&
                                            spirv_exts->supported[SPV_KHR_shader_ballot],
      .SubgroupVoteKHR                    = gl_exts->ARB_shader_group_vote &&
                                            spirv_exts->supported[SPV_KHR_subgroup_vote],
      .TransformFeedback                  = gl_exts->ARB_transform_feedback3,
      .VariablePointers                   = spirv_exts->supported[SPV_KHR_variable_pointers],
      .IntegerFunctions2INTEL             = gl_exts->INTEL_shader_integer_functions2,
   };
}
