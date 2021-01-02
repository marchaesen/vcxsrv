/*
 * Copyright 2018 Collabora Ltd.
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

#include "zink_screen.h"

#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_device_info.h"
#include "zink_fence.h"
#include "zink_format.h"
#include "zink_instance.h"
#include "zink_public.h"
#include "zink_resource.h"

#include "os/os_process.h"
#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"

#include "frontend/sw_winsys.h"

static const struct debug_named_value
debug_options[] = {
   { "nir", ZINK_DEBUG_NIR, "Dump NIR during program compile" },
   { "spirv", ZINK_DEBUG_SPIRV, "Dump SPIR-V during program compile" },
   { "tgsi", ZINK_DEBUG_TGSI, "Dump TGSI during program compile" },
   { "validation", ZINK_DEBUG_VALIDATION, "Dump Validation layer output" },
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(zink_debug, "ZINK_DEBUG", debug_options, 0)

uint32_t
zink_debug;

static const char *
zink_get_vendor(struct pipe_screen *pscreen)
{
   return "Collabora Ltd";
}

static const char *
zink_get_device_vendor(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);
   static char buf[1000];
   snprintf(buf, sizeof(buf), "Unknown (vendor-id: 0x%04x)", screen->info.props.vendorID);
   return buf;
}

static const char *
zink_get_name(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);
   static char buf[1000];
   snprintf(buf, sizeof(buf), "zink (%s)", screen->info.props.deviceName);
   return buf;
}

static int
get_video_mem(struct zink_screen *screen)
{
   VkDeviceSize size = 0;
   for (uint32_t i = 0; i < screen->info.mem_props.memoryHeapCount; ++i) {
      if (screen->info.mem_props.memoryHeaps[i].flags &
          VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
         size += screen->info.mem_props.memoryHeaps[i].size;
   }
   return (int)(size >> 20);
}

static int
zink_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   struct zink_screen *screen = zink_screen(pscreen);

   switch (param) {
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_TGSI_TEXCOORD:
   case PIPE_CAP_DRAW_INDIRECT:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_GLSL_TESS_LEVELS_AS_INPUTS:
      return 1;

   case PIPE_CAP_MULTI_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
      return screen->vk_CmdDrawIndirectCount &&
             screen->vk_CmdDrawIndexedIndirectCount;

   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
      return screen->info.have_EXT_vertex_attribute_divisor;

   case PIPE_CAP_INT64:
   case PIPE_CAP_INT64_DIVMOD:
   case PIPE_CAP_DOUBLES:
      return 1;

   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      if (!screen->info.feats.features.dualSrcBlend)
         return 0;
      return screen->info.props.limits.maxFragmentDualSrcAttachments;

   case PIPE_CAP_POINT_SPRITE:
      return 1;

   case PIPE_CAP_MAX_RENDER_TARGETS:
      return screen->info.props.limits.maxColorAttachments;

   case PIPE_CAP_OCCLUSION_QUERY:
      return 1;

   case PIPE_CAP_QUERY_TIME_ELAPSED:
      return screen->timestamp_valid_bits > 0;

   case PIPE_CAP_TEXTURE_MULTISAMPLE:
   case PIPE_CAP_SAMPLE_SHADING:
      return 1;

   case PIPE_CAP_TEXTURE_SWIZZLE:
      return 1;

   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
      return screen->info.props.limits.maxImageDimension2D;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return 1 + util_logbase2(screen->info.props.limits.maxImageDimension3D);
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return 1 + util_logbase2(screen->info.props.limits.maxImageDimensionCube);

   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
   case PIPE_CAP_FRAGMENT_SHADER_DERIVATIVES:
   case PIPE_CAP_VERTEX_SHADER_SATURATE:
      return 1;

   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
      return 1;

   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return screen->info.have_EXT_transform_feedback ? screen->info.tf_props.maxTransformFeedbackBuffers : 0;
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
   case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
      return 1;

   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return screen->info.props.limits.maxImageArrayLayers;

   case PIPE_CAP_DEPTH_CLIP_DISABLE:
      return screen->info.feats.features.depthClamp;

   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
      return 1;

   case PIPE_CAP_MIN_TEXEL_OFFSET:
      return screen->info.props.limits.minTexelOffset;
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return screen->info.props.limits.maxTexelOffset;

   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
      return 1;

   case PIPE_CAP_CONDITIONAL_RENDER:
     return screen->info.have_EXT_conditional_rendering;

   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
      return 130;
   case PIPE_CAP_GLSL_FEATURE_LEVEL:
      return 330;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_COMPUTE:
      return 1;
#endif

   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return screen->info.props.limits.minUniformBufferOffsetAlignment;

   case PIPE_CAP_QUERY_TIMESTAMP:
      return screen->info.have_EXT_calibrated_timestamps &&
             screen->timestamp_valid_bits > 0;

   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return screen->info.props.limits.minMemoryMapAlignment;

   case PIPE_CAP_CUBE_MAP_ARRAY:
      return screen->info.feats.features.imageCubeArray;

   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
   case PIPE_CAP_PRIMITIVE_RESTART:
      return 1;

   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
      return screen->info.props.limits.minTexelBufferOffsetAlignment;

   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return 0; /* unsure */

   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
      return screen->info.props.limits.maxTexelBufferElements;

   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_NATIVE; /* unsure */

   case PIPE_CAP_MAX_VIEWPORTS:
      return 1; /* TODO: When GS is supported, use screen->info.props.limits.maxViewports */

   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
      return 1;

   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
      return screen->info.props.limits.maxGeometryOutputVertices;
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return screen->info.props.limits.maxGeometryTotalOutputComponents;

#if 0 /* TODO: Enable me. Enables ARB_texture_gather */
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
      return 4;
#endif

   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
      return screen->info.props.limits.minTexelGatherOffset;
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
      return screen->info.props.limits.maxTexelGatherOffset;

   case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
      return 1;

   case PIPE_CAP_VENDOR_ID:
      return screen->info.props.vendorID;
   case PIPE_CAP_DEVICE_ID:
      return screen->info.props.deviceID;

   case PIPE_CAP_ACCELERATED:
      return 1;
   case PIPE_CAP_VIDEO_MEMORY:
      return get_video_mem(screen);
   case PIPE_CAP_UMA:
      return screen->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return screen->info.props.limits.maxVertexInputBindingStride;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_SAMPLER_VIEW_TARGET:
      return 1;
#endif

#if 0 /* TODO: Enable me */
   case PIPE_CAP_CLIP_HALFZ:
      return 1;
#endif

#if 0 /* TODO: Enable me */
   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
      return 1;
#endif

   case PIPE_CAP_SHAREABLE_SHADERS:
      return 1;

#if 0 /* TODO: Enable me. Enables GL_ARB_shader_storage_buffer_object */
   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
      return screen->info.props.limits.minStorageBufferOffsetAlignment;
#endif

   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
      return 0; /* TODO: figure these out */

   case PIPE_CAP_CULL_DISTANCE:
      return screen->info.feats.features.shaderCullDistance;

   case PIPE_CAP_VIEWPORT_SUBPIXEL_BITS:
      return screen->info.props.limits.viewportSubPixelBits;

   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
      return 0; /* not sure */

   case PIPE_CAP_MAX_GS_INVOCATIONS:
      return screen->info.props.limits.maxGeometryShaderInvocations;

   case PIPE_CAP_MAX_COMBINED_SHADER_BUFFERS:
      return screen->info.props.limits.maxDescriptorSetStorageBuffers;

   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE:
      return 65536;

   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
      return 1;

   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
      return 0;

   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
      return 0;

   case PIPE_CAP_NIR_COMPACT_ARRAYS:
      return 1;

   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
      return 1;

   case PIPE_CAP_VIEWPORT_TRANSFORM_LOWERED:
      return 1;

   case PIPE_CAP_FLATSHADE:
   case PIPE_CAP_ALPHA_TEST:
   case PIPE_CAP_CLIP_PLANES:
   case PIPE_CAP_POINT_SIZE_FIXED:
   case PIPE_CAP_TWO_SIDED_COLOR:
      return 0;

   case PIPE_CAP_MAX_SHADER_PATCH_VARYINGS:
      return screen->info.props.limits.maxTessellationControlPerVertexOutputComponents / 4;
   case PIPE_CAP_MAX_VARYINGS:
      /* need to reserve up to 60 of our varying components and 16 slots for streamout */
      return MIN2(screen->info.props.limits.maxVertexOutputComponents / 4 / 2, 16);

   case PIPE_CAP_DMABUF:
      return screen->info.have_KHR_external_memory_fd;

   default:
      return u_pipe_screen_get_param_defaults(pscreen, param);
   }
}

static float
zink_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
   struct zink_screen *screen = zink_screen(pscreen);

   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return screen->info.props.limits.lineWidthRange[1];

   case PIPE_CAPF_MAX_POINT_WIDTH:
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return screen->info.props.limits.pointSizeRange[1];

   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return screen->info.props.limits.maxSamplerAnisotropy;

   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return screen->info.props.limits.maxSamplerLodBias;

   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0f; /* not implemented */
   }

   /* should only get here on unhandled cases */
   return 0.0;
}

static int
zink_get_shader_param(struct pipe_screen *pscreen,
                       enum pipe_shader_type shader,
                       enum pipe_shader_cap param)
{
   struct zink_screen *screen = zink_screen(pscreen);

   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
      switch (shader) {
      case PIPE_SHADER_FRAGMENT:
      case PIPE_SHADER_VERTEX:
         return INT_MAX;
      case PIPE_SHADER_TESS_CTRL:
      case PIPE_SHADER_TESS_EVAL:
         if (screen->info.feats.features.tessellationShader &&
             (screen->instance_info.have_KHR_maintenance2 ||
              VK_MAKE_VERSION(1,1,0) <= screen->loader_version))
            return INT_MAX;
         break;

      case PIPE_SHADER_GEOMETRY:
         if (screen->info.feats.features.geometryShader)
            return INT_MAX;
         break;

      default:
         break;
      }
      return 0;
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      if (shader == PIPE_SHADER_VERTEX ||
          shader == PIPE_SHADER_FRAGMENT)
         return INT_MAX;
      return 0;

   case PIPE_SHADER_CAP_MAX_INPUTS:
      switch (shader) {
      case PIPE_SHADER_VERTEX:
         return MIN2(screen->info.props.limits.maxVertexInputAttributes,
                     PIPE_MAX_SHADER_INPUTS);
      case PIPE_SHADER_TESS_CTRL:
         return MIN2(screen->info.props.limits.maxTessellationControlPerVertexInputComponents / 4,
                     PIPE_MAX_SHADER_INPUTS);
      case PIPE_SHADER_TESS_EVAL:
         return MIN2(screen->info.props.limits.maxTessellationEvaluationInputComponents / 4,
                     PIPE_MAX_SHADER_INPUTS);
      case PIPE_SHADER_GEOMETRY:
         return MIN2(screen->info.props.limits.maxGeometryInputComponents,
                     PIPE_MAX_SHADER_INPUTS);
      case PIPE_SHADER_FRAGMENT:
         return MIN2(screen->info.props.limits.maxFragmentInputComponents / 4,
                     PIPE_MAX_SHADER_INPUTS);
      default:
         return 0; /* unsupported stage */
      }

   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      switch (shader) {
      case PIPE_SHADER_VERTEX:
         return MIN2(screen->info.props.limits.maxVertexOutputComponents / 4,
                     PIPE_MAX_SHADER_OUTPUTS);
      case PIPE_SHADER_TESS_CTRL:
         return MIN2(screen->info.props.limits.maxTessellationControlPerVertexOutputComponents / 4,
                     PIPE_MAX_SHADER_OUTPUTS);
      case PIPE_SHADER_TESS_EVAL:
         return MIN2(screen->info.props.limits.maxTessellationEvaluationOutputComponents / 4,
                     PIPE_MAX_SHADER_OUTPUTS);
      case PIPE_SHADER_GEOMETRY:
         return MIN2(screen->info.props.limits.maxGeometryOutputComponents / 4,
                     PIPE_MAX_SHADER_OUTPUTS);
      case PIPE_SHADER_FRAGMENT:
         return MIN2(screen->info.props.limits.maxColorAttachments,
                PIPE_MAX_SHADER_OUTPUTS);
      default:
         return 0; /* unsupported stage */
      }

   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      switch (shader) {
      case PIPE_SHADER_VERTEX:
      case PIPE_SHADER_FRAGMENT:
      case PIPE_SHADER_GEOMETRY:
      case PIPE_SHADER_TESS_CTRL:
      case PIPE_SHADER_TESS_EVAL:
         /* this might be a bit simplistic... */
         return MIN2(screen->info.props.limits.maxPerStageDescriptorSamplers,
                     PIPE_MAX_SAMPLERS);
      default:
         return 0; /* unsupported stage */
      }

   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      return 65536;

   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return  MIN2(screen->info.props.limits.maxPerStageDescriptorUniformBuffers,
                   PIPE_MAX_CONSTANT_BUFFERS);

   case PIPE_SHADER_CAP_MAX_TEMPS:
      return INT_MAX;

   case PIPE_SHADER_CAP_INTEGERS:
      return 1;

   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      return 1;

   case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
   case PIPE_SHADER_CAP_SUBROUTINES:
   case PIPE_SHADER_CAP_INT64_ATOMICS:
   case PIPE_SHADER_CAP_FP16:
   case PIPE_SHADER_CAP_FP16_DERIVATIVES:
   case PIPE_SHADER_CAP_INT16:
   case PIPE_SHADER_CAP_GLSL_16BIT_CONSTS:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_NIR;

   case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      return MIN2(screen->info.props.limits.maxPerStageDescriptorSampledImages,
                  PIPE_MAX_SHADER_SAMPLER_VIEWS);

   case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      return 0; /* no idea */

   case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
      return 32; /* arbitrary */

   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      return 0;

   case PIPE_SHADER_CAP_SUPPORTED_IRS:
      return (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);

   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
#if 0 /* TODO: needs compiler support */
      return MIN2(screen->info.props.limits.maxPerStageDescriptorStorageImages,
                  PIPE_MAX_SHADER_IMAGES);
#else
      return 0;
#endif

   case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
   case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
      return 0; /* unsure */

   case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
   case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      return 0; /* not implemented */
   }

   /* should only get here on unhandled cases */
   return 0;
}

static VkSampleCountFlagBits
vk_sample_count_flags(uint32_t sample_count)
{
   switch (sample_count) {
   case 1: return VK_SAMPLE_COUNT_1_BIT;
   case 2: return VK_SAMPLE_COUNT_2_BIT;
   case 4: return VK_SAMPLE_COUNT_4_BIT;
   case 8: return VK_SAMPLE_COUNT_8_BIT;
   case 16: return VK_SAMPLE_COUNT_16_BIT;
   case 32: return VK_SAMPLE_COUNT_32_BIT;
   case 64: return VK_SAMPLE_COUNT_64_BIT;
   default:
      return 0;
   }
}

static bool
zink_is_format_supported(struct pipe_screen *pscreen,
                         enum pipe_format format,
                         enum pipe_texture_target target,
                         unsigned sample_count,
                         unsigned storage_sample_count,
                         unsigned bind)
{
   struct zink_screen *screen = zink_screen(pscreen);

   if (format == PIPE_FORMAT_NONE)
      return screen->info.props.limits.framebufferNoAttachmentsSampleCounts &
             vk_sample_count_flags(sample_count);

   VkFormat vkformat = zink_get_format(screen, format);
   if (vkformat == VK_FORMAT_UNDEFINED)
      return false;

   if (sample_count >= 1) {
      VkSampleCountFlagBits sample_mask = vk_sample_count_flags(sample_count);
      if (!sample_mask)
         return false;
      const struct util_format_description *desc = util_format_description(format);
      if (util_format_is_depth_or_stencil(format)) {
         if (util_format_has_depth(desc)) {
            if (bind & PIPE_BIND_DEPTH_STENCIL &&
                (screen->info.props.limits.framebufferDepthSampleCounts & sample_mask) != sample_mask)
               return false;
            if (bind & PIPE_BIND_SAMPLER_VIEW &&
                (screen->info.props.limits.sampledImageDepthSampleCounts & sample_mask) != sample_mask)
               return false;
         }
         if (util_format_has_stencil(desc)) {
            if (bind & PIPE_BIND_DEPTH_STENCIL &&
                (screen->info.props.limits.framebufferStencilSampleCounts & sample_mask) != sample_mask)
               return false;
            if (bind & PIPE_BIND_SAMPLER_VIEW &&
                (screen->info.props.limits.sampledImageStencilSampleCounts & sample_mask) != sample_mask)
               return false;
         }
      } else if (util_format_is_pure_integer(format)) {
         if (bind & PIPE_BIND_RENDER_TARGET &&
             !(screen->info.props.limits.framebufferColorSampleCounts & sample_mask))
            return false;
         if (bind & PIPE_BIND_SAMPLER_VIEW &&
             !(screen->info.props.limits.sampledImageIntegerSampleCounts & sample_mask))
            return false;
      } else {
         if (bind & PIPE_BIND_RENDER_TARGET &&
             !(screen->info.props.limits.framebufferColorSampleCounts & sample_mask))
            return false;
         if (bind & PIPE_BIND_SAMPLER_VIEW &&
             !(screen->info.props.limits.sampledImageColorSampleCounts & sample_mask))
            return false;
      }
   }

   VkFormatProperties props;
   vkGetPhysicalDeviceFormatProperties(screen->pdev, vkformat, &props);

   if (target == PIPE_BUFFER) {
      if (bind & PIPE_BIND_VERTEX_BUFFER &&
          !(props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
         return false;
   } else {
      /* all other targets are texture-targets */
      if (bind & PIPE_BIND_RENDER_TARGET &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
         return false;

      if (bind & PIPE_BIND_BLENDABLE &&
         !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT))
        return false;

      if (bind & PIPE_BIND_SAMPLER_VIEW &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
         return false;

      if (bind & PIPE_BIND_DEPTH_STENCIL &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
         return false;
   }

   if (util_format_is_compressed(format)) {
      const struct util_format_description *desc = util_format_description(format);
      if (desc->layout == UTIL_FORMAT_LAYOUT_BPTC &&
          !screen->info.feats.features.textureCompressionBC)
         return false;
   }

   return true;
}

static void
zink_destroy_screen(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);

   if (VK_NULL_HANDLE != screen->debugUtilsCallbackHandle) {
      screen->vk_DestroyDebugUtilsMessengerEXT(screen->instance, screen->debugUtilsCallbackHandle, NULL);
   }

   slab_destroy_parent(&screen->transfer_pool);
   FREE(screen);
}

static VkPhysicalDevice
choose_pdev(const VkInstance instance)
{
   uint32_t i, pdev_count;
   VkPhysicalDevice *pdevs, pdev;
   vkEnumeratePhysicalDevices(instance, &pdev_count, NULL);
   assert(pdev_count > 0);

   pdevs = malloc(sizeof(*pdevs) * pdev_count);
   vkEnumeratePhysicalDevices(instance, &pdev_count, pdevs);
   assert(pdev_count > 0);

   pdev = pdevs[0];
   for (i = 0; i < pdev_count; ++i) {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(pdevs[i], &props);
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
         pdev = pdevs[i];
         break;
      }
   }
   free(pdevs);
   return pdev;
}

static void
update_queue_props(struct zink_screen *screen)
{
   uint32_t num_queues;
   vkGetPhysicalDeviceQueueFamilyProperties(screen->pdev, &num_queues, NULL);
   assert(num_queues > 0);

   VkQueueFamilyProperties *props = malloc(sizeof(*props) * num_queues);
   vkGetPhysicalDeviceQueueFamilyProperties(screen->pdev, &num_queues, props);

   for (uint32_t i = 0; i < num_queues; i++) {
      if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
         screen->gfx_queue = i;
         screen->timestamp_valid_bits = props[i].timestampValidBits;
         break;
      }
   }
   free(props);
}

static void
zink_flush_frontbuffer(struct pipe_screen *pscreen,
                       struct pipe_context *pcontext,
                       struct pipe_resource *pres,
                       unsigned level, unsigned layer,
                       void *winsys_drawable_handle,
                       struct pipe_box *sub_box)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct sw_winsys *winsys = screen->winsys;
   struct zink_resource *res = zink_resource(pres);

   if (!winsys)
     return;
   void *map = winsys->displaytarget_map(winsys, res->dt, 0);

   if (map) {
      VkImageSubresource isr = {};
      isr.aspectMask = res->aspect;
      isr.mipLevel = level;
      isr.arrayLayer = layer;
      VkSubresourceLayout layout;
      vkGetImageSubresourceLayout(screen->dev, res->image, &isr, &layout);

      void *ptr;
      VkResult result = vkMapMemory(screen->dev, res->mem, res->offset, res->size, 0, &ptr);
      if (result != VK_SUCCESS) {
         debug_printf("failed to map memory for display\n");
         return;
      }
      for (int i = 0; i < pres->height0; ++i) {
         uint8_t *src = (uint8_t *)ptr + i * layout.rowPitch;
         uint8_t *dst = (uint8_t *)map + i * res->dt_stride;
         memcpy(dst, src, res->dt_stride);
      }
      vkUnmapMemory(screen->dev, res->mem);
   }

   winsys->displaytarget_unmap(winsys, res->dt);

   assert(res->dt);
   if (res->dt)
      winsys->displaytarget_display(winsys, res->dt, winsys_drawable_handle, sub_box);
}

bool
zink_is_depth_format_supported(struct zink_screen *screen, VkFormat format)
{
   VkFormatProperties props;
   vkGetPhysicalDeviceFormatProperties(screen->pdev, format, &props);
   return (props.linearTilingFeatures | props.optimalTilingFeatures) &
          VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
}

static enum pipe_format
emulate_x8(enum pipe_format format)
{
   /* convert missing X8 variants to A8 */
   switch (format) {
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      return PIPE_FORMAT_B8G8R8A8_UNORM;

   case PIPE_FORMAT_B8G8R8X8_SRGB:
      return PIPE_FORMAT_B8G8R8A8_SRGB;

   default:
      return format;
   }
}

VkFormat
zink_get_format(struct zink_screen *screen, enum pipe_format format)
{
   VkFormat ret = zink_pipe_format_to_vk_format(emulate_x8(format));

   if (ret == VK_FORMAT_X8_D24_UNORM_PACK32 &&
       !screen->have_X8_D24_UNORM_PACK32) {
      assert(zink_is_depth_format_supported(screen, VK_FORMAT_D32_SFLOAT));
      return VK_FORMAT_D32_SFLOAT;
   }

   if (ret == VK_FORMAT_D24_UNORM_S8_UINT &&
       !screen->have_D24_UNORM_S8_UINT) {
      assert(zink_is_depth_format_supported(screen,
                                            VK_FORMAT_D32_SFLOAT_S8_UINT));
      return VK_FORMAT_D32_SFLOAT_S8_UINT;
   }

   if ((ret == VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT &&
        !screen->info.format_4444_feats.formatA4B4G4R4) ||
       (ret == VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT &&
        !screen->info.format_4444_feats.formatA4R4G4B4))
      return VK_FORMAT_UNDEFINED;

   return ret;
}

static bool
load_instance_extensions(struct zink_screen *screen)
{
   if (zink_debug & ZINK_DEBUG_VALIDATION) {
      printf("zink: Loader %d.%d.%d \n", VK_VERSION_MAJOR(screen->loader_version), VK_VERSION_MINOR(screen->loader_version), VK_VERSION_PATCH(screen->loader_version));
   }

   if (screen->instance_info.have_KHR_get_physical_device_properties2) {
      // Not Vk 1.1+ so if VK_KHR_get_physical_device_properties2 the use it
      GET_PROC_ADDR_INSTANCE_LOCAL(screen->instance, GetPhysicalDeviceFeatures2KHR);
      GET_PROC_ADDR_INSTANCE_LOCAL(screen->instance, GetPhysicalDeviceProperties2KHR);
      screen->vk_GetPhysicalDeviceFeatures2 = vk_GetPhysicalDeviceFeatures2KHR;
      screen->vk_GetPhysicalDeviceProperties2 = vk_GetPhysicalDeviceProperties2KHR;
   } else if (VK_MAKE_VERSION(1,1,0) <= screen->loader_version) {
      // Get Vk 1.1+ Instance functions
      GET_PROC_ADDR_INSTANCE(GetPhysicalDeviceFeatures2);
      GET_PROC_ADDR_INSTANCE(GetPhysicalDeviceProperties2);
   }

   if (screen->instance_info.have_KHR_draw_indirect_count) {
      GET_PROC_ADDR_INSTANCE_LOCAL(screen->instance, CmdDrawIndirectCountKHR);
      GET_PROC_ADDR_INSTANCE_LOCAL(screen->instance, CmdDrawIndexedIndirectCountKHR);
      screen->vk_CmdDrawIndirectCount = vk_CmdDrawIndirectCountKHR;
      screen->vk_CmdDrawIndexedIndirectCount = vk_CmdDrawIndexedIndirectCountKHR;
   } else if (VK_MAKE_VERSION(1,2,0) <= screen->loader_version) {
      // Get Vk 1.1+ Instance functions
      GET_PROC_ADDR_INSTANCE(CmdDrawIndirectCount);
      GET_PROC_ADDR_INSTANCE(CmdDrawIndexedIndirectCount);
   }

   return true;
}

static bool
load_device_extensions(struct zink_screen *screen)
{
   if (screen->info.have_EXT_transform_feedback) {
      GET_PROC_ADDR(CmdBindTransformFeedbackBuffersEXT);
      GET_PROC_ADDR(CmdBeginTransformFeedbackEXT);
      GET_PROC_ADDR(CmdEndTransformFeedbackEXT);
      GET_PROC_ADDR(CmdBeginQueryIndexedEXT);
      GET_PROC_ADDR(CmdEndQueryIndexedEXT);
      GET_PROC_ADDR(CmdDrawIndirectByteCountEXT);
   }
   if (screen->info.have_KHR_external_memory_fd)
      GET_PROC_ADDR(GetMemoryFdKHR);

   if (screen->info.have_EXT_conditional_rendering) {
      GET_PROC_ADDR(CmdBeginConditionalRenderingEXT);
      GET_PROC_ADDR(CmdEndConditionalRenderingEXT);
   }

   if (screen->info.have_EXT_calibrated_timestamps) {
      GET_PROC_ADDR_INSTANCE(GetPhysicalDeviceCalibrateableTimeDomainsEXT);
      GET_PROC_ADDR(GetCalibratedTimestampsEXT);

      uint32_t num_domains = 0;
      screen->vk_GetPhysicalDeviceCalibrateableTimeDomainsEXT(screen->pdev, &num_domains, NULL);
      assert(num_domains > 0);

      VkTimeDomainEXT *domains = malloc(sizeof(VkTimeDomainEXT) * num_domains);
      screen->vk_GetPhysicalDeviceCalibrateableTimeDomainsEXT(screen->pdev, &num_domains, domains);

      /* VK_TIME_DOMAIN_DEVICE_EXT is used for the ctx->get_timestamp hook and is the only one we really need */
      ASSERTED bool have_device_time = false;
      for (unsigned i = 0; i < num_domains; i++) {
         if (domains[i] == VK_TIME_DOMAIN_DEVICE_EXT) {
            have_device_time = true;
            break;
         }
      }
      assert(have_device_time);
      free(domains);
   }
   if (screen->info.have_EXT_extended_dynamic_state) {
      GET_PROC_ADDR(CmdSetViewportWithCountEXT);
      GET_PROC_ADDR(CmdSetScissorWithCountEXT);
   }

   screen->have_triangle_fans = true;
#if defined(VK_EXTX_PORTABILITY_SUBSET_EXTENSION_NAME)
   if (screen->info.have_EXTX_portability_subset) {
      screen->have_triangle_fans = (VK_TRUE == screen->info.portability_subset_extx_feats.triangleFans);
   }
#endif // VK_EXTX_PORTABILITY_SUBSET_EXTENSION_NAME

   return true;
}

static VkBool32 VKAPI_CALL
zink_debug_util_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageType,
    const VkDebugUtilsMessengerCallbackDataEXT      *pCallbackData,
    void                                            *pUserData)
{
   const char *severity = "MSG";

   // Pick message prefix and color to use.
   // Only MacOS and Linux have been tested for color support
   if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
      severity = "ERR";
   } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
      severity = "WRN";
   } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
      severity = "NFO";
   }

   fprintf(stderr, "zink DEBUG: %s: '%s'\n", severity, pCallbackData->pMessage);
   return VK_FALSE;
}

static bool
create_debug(struct zink_screen *screen)
{
   GET_PROC_ADDR_INSTANCE(CreateDebugUtilsMessengerEXT);
   GET_PROC_ADDR_INSTANCE(DestroyDebugUtilsMessengerEXT);

   if (!screen->vk_CreateDebugUtilsMessengerEXT || !screen->vk_DestroyDebugUtilsMessengerEXT)
      return false;

   VkDebugUtilsMessengerCreateInfoEXT vkDebugUtilsMessengerCreateInfoEXT = {
       VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
       NULL,
       0,  // flags
       VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
       VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
       VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
       zink_debug_util_callback,
       NULL
   };

   VkDebugUtilsMessengerEXT vkDebugUtilsCallbackEXT = VK_NULL_HANDLE;

   screen->vk_CreateDebugUtilsMessengerEXT(
       screen->instance,
       &vkDebugUtilsMessengerCreateInfoEXT,
       NULL,
       &vkDebugUtilsCallbackEXT
   );

   screen->debugUtilsCallbackHandle = vkDebugUtilsCallbackEXT;

   return true;
}

static bool
zink_internal_setup_moltenvk(struct zink_screen *screen)
{
#if defined(MVK_VERSION)
   if (!screen->instance_info.have_MVK_moltenvk)
      return true;

   GET_PROC_ADDR_INSTANCE(GetMoltenVKConfigurationMVK);
   GET_PROC_ADDR_INSTANCE(SetMoltenVKConfigurationMVK);

   GET_PROC_ADDR_INSTANCE(GetPhysicalDeviceMetalFeaturesMVK);
   GET_PROC_ADDR_INSTANCE(GetVersionStringsMVK);
   GET_PROC_ADDR_INSTANCE(UseIOSurfaceMVK);
   GET_PROC_ADDR_INSTANCE(GetIOSurfaceMVK);

   if (screen->vk_GetVersionStringsMVK) {
      char molten_version[64] = {0};
      char vulkan_version[64] = {0};

      (*screen->vk_GetVersionStringsMVK)(molten_version, sizeof(molten_version) - 1, vulkan_version, sizeof(vulkan_version) - 1);

      printf("zink: MoltenVK %s Vulkan %s \n", molten_version, vulkan_version);
   }

   if (screen->vk_GetMoltenVKConfigurationMVK && screen->vk_SetMoltenVKConfigurationMVK) {
      MVKConfiguration molten_config = {0};
      size_t molten_config_size = sizeof(molten_config);

      VkResult res = (*screen->vk_GetMoltenVKConfigurationMVK)(screen->instance, &molten_config, &molten_config_size);
      if (res == VK_SUCCESS || res == VK_INCOMPLETE) {
         // Needed to allow MoltenVK to accept VkImageView swizzles.
         // Encountered when using VK_FORMAT_R8G8_UNORM
         molten_config.fullImageViewSwizzle = VK_TRUE;
         (*screen->vk_SetMoltenVKConfigurationMVK)(screen->instance, &molten_config, &molten_config_size);
      }
   }
#endif // MVK_VERSION

   return true;
}

static void
check_device_needs_mesa_wsi(struct zink_screen *screen)
{
   /* Raspberry Pi 4 V3DV driver */
   if (screen->info.props.vendorID == 0x14E4 &&
       screen->info.props.deviceID == 42) {
      screen->needs_mesa_wsi = true;
   }
}

static uint32_t
zink_get_loader_version(void)
{

   uint32_t loader_version = VK_API_VERSION_1_0;

   // Get the Loader version
   GET_PROC_ADDR_INSTANCE_LOCAL(NULL, EnumerateInstanceVersion);
   if (vk_EnumerateInstanceVersion) {
      uint32_t loader_version_temp = VK_API_VERSION_1_0;
      if (VK_SUCCESS == (*vk_EnumerateInstanceVersion)(&loader_version_temp)) {
         loader_version = loader_version_temp;
      }
   }

   return loader_version;
}

static VkDevice
zink_create_logical_device(struct zink_screen *screen)
{
   VkDevice dev = VK_NULL_HANDLE;

   VkDeviceQueueCreateInfo qci = {};
   float dummy = 0.0f;
   qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   qci.queueFamilyIndex = screen->gfx_queue;
   qci.queueCount = 1;
   qci.pQueuePriorities = &dummy;

   VkDeviceCreateInfo dci = {};
   dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   dci.queueCreateInfoCount = 1;
   dci.pQueueCreateInfos = &qci;
   /* extensions don't have bool members in pEnabledFeatures.
    * this requires us to pass the whole VkPhysicalDeviceFeatures2 struct
    */
   if (screen->info.feats.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) {
      dci.pNext = &screen->info.feats;
   } else {
      dci.pEnabledFeatures = &screen->info.feats.features;
   }

   dci.ppEnabledExtensionNames = screen->info.extensions;
   dci.enabledExtensionCount = screen->info.num_extensions;

   vkCreateDevice(screen->pdev, &dci, NULL, &dev);
   return dev;
}

static struct zink_screen *
zink_internal_create_screen(const struct pipe_screen_config *config)
{
   struct zink_screen *screen = CALLOC_STRUCT(zink_screen);
   if (!screen)
      return NULL;

   zink_debug = debug_get_option_zink_debug();

   screen->loader_version = zink_get_loader_version();
   screen->instance = zink_create_instance(screen);
   if (!screen->instance)
      goto fail;

   if (!load_instance_extensions(screen))
      goto fail;

   if (screen->instance_info.have_EXT_debug_utils && !create_debug(screen))
      debug_printf("ZINK: failed to setup debug utils\n");

   screen->pdev = choose_pdev(screen->instance);
   update_queue_props(screen);

   screen->have_X8_D24_UNORM_PACK32 = zink_is_depth_format_supported(screen,
                                              VK_FORMAT_X8_D24_UNORM_PACK32);
   screen->have_D24_UNORM_S8_UINT = zink_is_depth_format_supported(screen,
                                              VK_FORMAT_D24_UNORM_S8_UINT);

   if (!zink_get_physical_device_info(screen)) {
      debug_printf("ZINK: failed to detect features\n");
      goto fail;
   }

   /* Some Vulkan implementations have special requirements for WSI
    * allocations.
    */
   check_device_needs_mesa_wsi(screen);

   zink_internal_setup_moltenvk(screen);

   screen->dev = zink_create_logical_device(screen);
   if (!screen->dev)
      goto fail;

   if (!load_device_extensions(screen))
      goto fail;

   screen->base.get_name = zink_get_name;
   screen->base.get_vendor = zink_get_vendor;
   screen->base.get_device_vendor = zink_get_device_vendor;
   screen->base.get_param = zink_get_param;
   screen->base.get_paramf = zink_get_paramf;
   screen->base.get_shader_param = zink_get_shader_param;
   screen->base.get_compiler_options = zink_get_compiler_options;
   screen->base.is_format_supported = zink_is_format_supported;
   screen->base.context_create = zink_context_create;
   screen->base.flush_frontbuffer = zink_flush_frontbuffer;
   screen->base.destroy = zink_destroy_screen;

   zink_screen_resource_init(&screen->base);
   zink_screen_fence_init(&screen->base);

   slab_create_parent(&screen->transfer_pool, sizeof(struct zink_transfer), 16);

   return screen;

fail:
   FREE(screen);
   return NULL;
}

struct pipe_screen *
zink_create_screen(struct sw_winsys *winsys)
{
   struct zink_screen *ret = zink_internal_create_screen(NULL);

   if (ret)
      ret->winsys = winsys;

   return &ret->base;
}

struct pipe_screen *
zink_drm_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct zink_screen *ret = zink_internal_create_screen(config);

   if (ret && !ret->info.have_KHR_external_memory_fd) {
      debug_printf("ZINK: KHR_external_memory_fd required!\n");
      free(ret);
      return NULL;
   }

   return &ret->base;
}
