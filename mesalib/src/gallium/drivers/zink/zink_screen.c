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
#include "zink_descriptors.h"
#include "zink_fence.h"
#include "zink_format.h"
#include "zink_framebuffer.h"
#include "zink_instance.h"
#include "zink_program.h"
#include "zink_public.h"
#include "zink_resource.h"
#include "nir_to_spirv/nir_to_spirv.h" // for SPIRV_VERSION

#include "os/os_process.h"
#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "util/hash_table.h"
#include "util/os_file.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"
#include "util/u_transfer_helper.h"
#include "util/xmlconfig.h"

#include "util/u_cpu_detect.h"

#include "frontend/sw_winsys.h"

#if DETECT_OS_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

#if defined(__APPLE__)
// Source of MVK_VERSION
#include "MoltenVK/vk_mvk_moltenvk.h"
#endif

static const struct debug_named_value
zink_debug_options[] = {
   { "nir", ZINK_DEBUG_NIR, "Dump NIR during program compile" },
   { "spirv", ZINK_DEBUG_SPIRV, "Dump SPIR-V during program compile" },
   { "tgsi", ZINK_DEBUG_TGSI, "Dump TGSI during program compile" },
   { "validation", ZINK_DEBUG_VALIDATION, "Dump Validation layer output" },
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(zink_debug, "ZINK_DEBUG", zink_debug_options, 0)

uint32_t
zink_debug;


static const struct debug_named_value
zink_descriptor_options[] = {
   { "auto", ZINK_DESCRIPTOR_MODE_AUTO, "Automatically detect best mode" },
   { "lazy", ZINK_DESCRIPTOR_MODE_LAZY, "Don't cache, do least amount of updates" },
   { "nofallback", ZINK_DESCRIPTOR_MODE_NOFALLBACK, "Cache, never use lazy fallback" },
   { "notemplates", ZINK_DESCRIPTOR_MODE_NOTEMPLATES, "Cache, but disable templated updates" },
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(zink_descriptor_mode, "ZINK_DESCRIPTORS", zink_descriptor_options, ZINK_DESCRIPTOR_MODE_AUTO)

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

static uint32_t
hash_framebuffer_state(const void *key)
{
   struct zink_framebuffer_state* s = (struct zink_framebuffer_state*)key;
   return _mesa_hash_data(key, offsetof(struct zink_framebuffer_state, attachments) + sizeof(s->attachments[0]) * s->num_attachments);
}

static bool
equals_framebuffer_state(const void *a, const void *b)
{
   struct zink_framebuffer_state *s = (struct zink_framebuffer_state*)a;
   return memcmp(a, b, offsetof(struct zink_framebuffer_state, attachments) + sizeof(s->attachments[0]) * s->num_attachments) == 0;
}

static VkDeviceSize
get_video_mem(struct zink_screen *screen)
{
   VkDeviceSize size = 0;
   for (uint32_t i = 0; i < screen->info.mem_props.memoryHeapCount; ++i) {
      if (screen->info.mem_props.memoryHeaps[i].flags &
          VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
         size += screen->info.mem_props.memoryHeaps[i].size;
   }
   return size;
}

static void
disk_cache_init(struct zink_screen *screen)
{
#ifdef ENABLE_SHADER_CACHE
   static char buf[1000];
   snprintf(buf, sizeof(buf), "zink_%x04x", screen->info.props.vendorID);

   screen->disk_cache = disk_cache_create(buf, screen->info.props.deviceName, 0);
   if (screen->disk_cache) {
      util_queue_init(&screen->cache_put_thread, "zcq", 8, 1, UTIL_QUEUE_INIT_RESIZE_IF_FULL, screen);
      util_queue_init(&screen->cache_get_thread, "zcfq", 8, 4, UTIL_QUEUE_INIT_RESIZE_IF_FULL, screen);
   }
#endif
}


static void
cache_put_job(void *data, void *gdata, int thread_index)
{
   struct zink_program *pg = data;
   struct zink_screen *screen = gdata;
   size_t size = 0;
   if (VKSCR(GetPipelineCacheData)(screen->dev, pg->pipeline_cache, &size, NULL) != VK_SUCCESS)
      return;
   if (pg->pipeline_cache_size == size)
      return;
   void *pipeline_data = malloc(size);
   if (!pipeline_data)
      return;
   if (VKSCR(GetPipelineCacheData)(screen->dev, pg->pipeline_cache, &size, pipeline_data) == VK_SUCCESS) {
      pg->pipeline_cache_size = size;

      cache_key key;
      disk_cache_compute_key(screen->disk_cache, pg->sha1, sizeof(pg->sha1), key);
      disk_cache_put_nocopy(screen->disk_cache, key, pipeline_data, size, NULL);
   }
}

void
zink_screen_update_pipeline_cache(struct zink_screen *screen, struct zink_program *pg)
{
   util_queue_fence_init(&pg->cache_fence);
   if (!screen->disk_cache)
      return;

   util_queue_add_job(&screen->cache_put_thread, pg, NULL, cache_put_job, NULL, 0);
}

static void
cache_get_job(void *data, void *gdata, int thread_index)
{
   struct zink_program *pg = data;
   struct zink_screen *screen = gdata;

   VkPipelineCacheCreateInfo pcci;
   pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
   pcci.pNext = NULL;
   pcci.flags = screen->info.have_EXT_pipeline_creation_cache_control ? VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT : 0;
   pcci.initialDataSize = 0;
   pcci.pInitialData = NULL;

   cache_key key;
   disk_cache_compute_key(screen->disk_cache, pg->sha1, sizeof(pg->sha1), key);
   pcci.pInitialData = disk_cache_get(screen->disk_cache, key, &pg->pipeline_cache_size);
   pcci.initialDataSize = pg->pipeline_cache_size;
   VKSCR(CreatePipelineCache)(screen->dev, &pcci, NULL, &pg->pipeline_cache);
   free((void*)pcci.pInitialData);
}

void
zink_screen_get_pipeline_cache(struct zink_screen *screen, struct zink_program *pg)
{
   util_queue_fence_init(&pg->cache_fence);
   if (!screen->disk_cache)
      return;

   util_queue_add_job(&screen->cache_get_thread, pg, &pg->cache_fence, cache_get_job, NULL, 0);
}

static int
zink_get_compute_param(struct pipe_screen *pscreen, enum pipe_shader_ir ir_type,
                       enum pipe_compute_cap param, void *ret)
{
   struct zink_screen *screen = zink_screen(pscreen);
#define RET(x) do {                  \
   if (ret)                          \
      memcpy(ret, x, sizeof(x));     \
   return sizeof(x);                 \
} while (0)

   switch (param) {
   case PIPE_COMPUTE_CAP_ADDRESS_BITS:
      RET((uint32_t []){ 32 });

   case PIPE_COMPUTE_CAP_IR_TARGET:
      if (ret)
         strcpy(ret, "nir");
      return 4;

   case PIPE_COMPUTE_CAP_GRID_DIMENSION:
      RET((uint64_t []) { 3 });

   case PIPE_COMPUTE_CAP_MAX_GRID_SIZE:
      RET(((uint64_t []) { screen->info.props.limits.maxComputeWorkGroupCount[0],
                           screen->info.props.limits.maxComputeWorkGroupCount[1],
                           screen->info.props.limits.maxComputeWorkGroupCount[2] }));

   case PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE:
      /* MaxComputeWorkGroupSize[0..2] */
      RET(((uint64_t []) {screen->info.props.limits.maxComputeWorkGroupSize[0],
                          screen->info.props.limits.maxComputeWorkGroupSize[1],
                          screen->info.props.limits.maxComputeWorkGroupSize[2]}));

   case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
   case PIPE_COMPUTE_CAP_MAX_VARIABLE_THREADS_PER_BLOCK:
      RET((uint64_t []) { screen->info.props.limits.maxComputeWorkGroupInvocations });

   case PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE:
      RET((uint64_t []) { screen->info.props.limits.maxComputeSharedMemorySize });

   case PIPE_COMPUTE_CAP_IMAGES_SUPPORTED:
      RET((uint32_t []) { 1 });

   case PIPE_COMPUTE_CAP_SUBGROUP_SIZE:
      RET((uint32_t []) { screen->info.props11.subgroupSize });

   case PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE:
   case PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY:
   case PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS:
   case PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE:
   case PIPE_COMPUTE_CAP_MAX_PRIVATE_SIZE:
   case PIPE_COMPUTE_CAP_MAX_INPUT_SIZE:
      // XXX: I think these are for Clover...
      return 0;

   default:
      unreachable("unknown compute param");
   }
}

static int
zink_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   struct zink_screen *screen = zink_screen(pscreen);

   switch (param) {
   case PIPE_CAP_ANISOTROPIC_FILTER:
      return screen->info.feats.features.samplerAnisotropy;
   case PIPE_CAP_EMULATE_NONFIXED_PRIMITIVE_RESTART:
      return 1;
   case PIPE_CAP_SUPPORTED_PRIM_MODES_WITH_RESTART: {
      uint32_t modes = BITFIELD_BIT(PIPE_PRIM_LINE_STRIP) |
                       BITFIELD_BIT(PIPE_PRIM_TRIANGLE_STRIP) |
                       BITFIELD_BIT(PIPE_PRIM_LINE_STRIP_ADJACENCY) |
                       BITFIELD_BIT(PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY);
      if (screen->have_triangle_fans)
         modes |= BITFIELD_BIT(PIPE_PRIM_TRIANGLE_FAN);
      if (screen->info.have_EXT_primitive_topology_list_restart) {
         modes |= BITFIELD_BIT(PIPE_PRIM_POINTS) |
                  BITFIELD_BIT(PIPE_PRIM_LINES) |
                  BITFIELD_BIT(PIPE_PRIM_TRIANGLES) |
                  BITFIELD_BIT(PIPE_PRIM_TRIANGLES_ADJACENCY);
         if (screen->info.list_restart_feats.primitiveTopologyPatchListRestart)
            modes |= BITFIELD_BIT(PIPE_PRIM_PATCHES);
      }
      return modes;
   }
   case PIPE_CAP_SUPPORTED_PRIM_MODES: {
      uint32_t modes = BITFIELD_MASK(PIPE_PRIM_MAX);
      modes &= ~BITFIELD_BIT(PIPE_PRIM_QUADS);
      modes &= ~BITFIELD_BIT(PIPE_PRIM_QUAD_STRIP);
      modes &= ~BITFIELD_BIT(PIPE_PRIM_POLYGON);
      modes &= ~BITFIELD_BIT(PIPE_PRIM_LINE_LOOP);
      if (!screen->have_triangle_fans)
         modes &= ~BITFIELD_BIT(PIPE_PRIM_TRIANGLE_FAN);
      return modes;
   }

   case PIPE_CAP_FBFETCH:
      return 1;

   case PIPE_CAP_QUERY_MEMORY_INFO:
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_TGSI_TEXCOORD:
   case PIPE_CAP_DRAW_INDIRECT:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_GLSL_TESS_LEVELS_AS_INPUTS:
   case PIPE_CAP_CLEAR_TEXTURE:
   case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
   case PIPE_CAP_FORCE_PERSAMPLE_INTERP:
   case PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT:
   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
   case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
   case PIPE_CAP_QUERY_BUFFER_OBJECT:
   case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
   case PIPE_CAP_CLIP_HALFZ:
   case PIPE_CAP_TGSI_TXQS:
   case PIPE_CAP_TEXTURE_BARRIER:
   case PIPE_CAP_QUERY_SO_OVERFLOW:
   case PIPE_CAP_GL_SPIRV:
   case PIPE_CAP_CLEAR_SCISSORED:
   case PIPE_CAP_INVALIDATE_BUFFER:
   case PIPE_CAP_PREFER_REAL_BUFFER_IN_CONSTBUF0:
   case PIPE_CAP_PACKED_UNIFORMS:
   case PIPE_CAP_TGSI_PACK_HALF_FLOAT:
      return 1;

   case PIPE_CAP_SURFACE_SAMPLE_COUNT:
      return screen->vk_version >= VK_MAKE_VERSION(1,2,0);

   case PIPE_CAP_DRAW_PARAMETERS:
      return screen->info.feats11.shaderDrawParameters || screen->info.have_KHR_shader_draw_parameters;

   case PIPE_CAP_TGSI_VOTE:
      return screen->spirv_version >= SPIRV_VERSION(1, 3);

   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
      return screen->info.have_EXT_provoking_vertex;

   case PIPE_CAP_TEXTURE_MIRROR_CLAMP_TO_EDGE:
      return screen->info.have_KHR_sampler_mirror_clamp_to_edge;

   case PIPE_CAP_POLYGON_OFFSET_CLAMP:
      return screen->info.feats.features.depthBiasClamp;

   case PIPE_CAP_QUERY_PIPELINE_STATISTICS_SINGLE:
      return screen->info.feats.features.pipelineStatisticsQuery;

   case PIPE_CAP_ROBUST_BUFFER_ACCESS_BEHAVIOR:
      return screen->info.feats.features.robustBufferAccess;

   case PIPE_CAP_MULTI_DRAW_INDIRECT:
      return screen->info.feats.features.multiDrawIndirect;

   case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
      return screen->info.have_KHR_draw_indirect_count;

   case PIPE_CAP_START_INSTANCE:
      return (screen->info.have_vulkan12 && screen->info.feats11.shaderDrawParameters) ||
              screen->info.have_KHR_shader_draw_parameters;

   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
      return screen->info.have_EXT_vertex_attribute_divisor;

   case PIPE_CAP_MAX_VERTEX_STREAMS:
      return screen->info.tf_props.maxTransformFeedbackStreams;

   case PIPE_CAP_INT64:
   case PIPE_CAP_INT64_DIVMOD:
   case PIPE_CAP_DOUBLES:
      return 1;

   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      if (!screen->info.feats.features.dualSrcBlend)
         return 0;
      return screen->info.props.limits.maxFragmentDualSrcAttachments;

   case PIPE_CAP_MAX_RENDER_TARGETS:
      return screen->info.props.limits.maxColorAttachments;

   case PIPE_CAP_OCCLUSION_QUERY:
      return screen->info.feats.features.occlusionQueryPrecise;

   case PIPE_CAP_PROGRAMMABLE_SAMPLE_LOCATIONS:
      return screen->info.have_EXT_sample_locations && screen->info.have_EXT_extended_dynamic_state;

   case PIPE_CAP_QUERY_TIME_ELAPSED:
      return screen->timestamp_valid_bits > 0;

   case PIPE_CAP_TEXTURE_MULTISAMPLE:
      return 1;

   case PIPE_CAP_FRAGMENT_SHADER_INTERLOCK:
      return screen->info.have_EXT_fragment_shader_interlock;

   case PIPE_CAP_TGSI_CLOCK:
      return screen->info.have_KHR_shader_clock;

   case PIPE_CAP_POINT_SPRITE:
      return 1;

   case PIPE_CAP_TGSI_BALLOT:
      return screen->vk_version >= VK_MAKE_VERSION(1,2,0) && screen->info.props11.subgroupSize <= 64;

   case PIPE_CAP_SAMPLE_SHADING:
      return screen->info.feats.features.sampleRateShading;

   case PIPE_CAP_TEXTURE_SWIZZLE:
      return 1;

   case PIPE_CAP_GL_CLAMP:
      return 0;

   case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
      /* This is also broken on the other AMD drivers for old HW, but
       * there's no obvious way to test for that.
       */
      if (screen->info.driver_props.driverID == VK_DRIVER_ID_MESA_RADV ||
          screen->info.driver_props.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
         return PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_NV50;
      return 0;

   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
      return screen->info.props.limits.maxImageDimension2D;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return 1 + util_logbase2(screen->info.props.limits.maxImageDimension3D);
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return 1 + util_logbase2(screen->info.props.limits.maxImageDimensionCube);

   case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
   case PIPE_CAP_FRAGMENT_SHADER_DERIVATIVES:
   case PIPE_CAP_VERTEX_SHADER_SATURATE:
      return 1;

   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
      return screen->info.feats.features.independentBlend;

   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return screen->info.have_EXT_transform_feedback ? screen->info.tf_props.maxTransformFeedbackBuffers : 0;
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
   case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
      return screen->info.have_EXT_transform_feedback;

   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return screen->info.props.limits.maxImageArrayLayers;

   case PIPE_CAP_DEPTH_CLIP_DISABLE:
      return screen->info.feats.features.depthClamp;

   case PIPE_CAP_SHADER_STENCIL_EXPORT:
      return screen->info.have_EXT_shader_stencil_export;

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
     return 1;

   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
   case PIPE_CAP_GLSL_FEATURE_LEVEL:
      return 460;

   case PIPE_CAP_COMPUTE:
      return 1;

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

   case PIPE_CAP_BINDLESS_TEXTURE:
      return screen->info.have_EXT_descriptor_indexing &&
             /* push, 4 types, bindless */
             screen->info.props.limits.maxBoundDescriptorSets >= 6;

   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
      return screen->info.props.limits.minTexelBufferOffsetAlignment;

   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return 1;

   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
      return screen->info.props.limits.maxTexelBufferElements;

   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_NATIVE; /* unsure */

   case PIPE_CAP_MAX_VIEWPORTS:
      return MIN2(screen->info.props.limits.maxViewports, PIPE_MAX_VIEWPORTS);

   case PIPE_CAP_IMAGE_LOAD_FORMATTED:
      return screen->info.feats.features.shaderStorageImageReadWithoutFormat;

   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
      return 1;

   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
      return screen->info.props.limits.maxGeometryOutputVertices;
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return screen->info.props.limits.maxGeometryTotalOutputComponents;

   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
      return 4;

   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
      return screen->info.props.limits.minTexelGatherOffset;
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
      return screen->info.props.limits.maxTexelGatherOffset;

   case PIPE_CAP_SAMPLER_REDUCTION_MINMAX_ARB:
      return screen->vk_version >= VK_MAKE_VERSION(1,2,0) || screen->info.have_EXT_sampler_filter_minmax;

   case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
      return 1;

   case PIPE_CAP_VENDOR_ID:
      return screen->info.props.vendorID;
   case PIPE_CAP_DEVICE_ID:
      return screen->info.props.deviceID;

   case PIPE_CAP_ACCELERATED:
      return 1;
   case PIPE_CAP_VIDEO_MEMORY:
      return get_video_mem(screen) >> 20;
   case PIPE_CAP_UMA:
      return screen->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return screen->info.props.limits.maxVertexInputBindingStride;

   case PIPE_CAP_SAMPLER_VIEW_TARGET:
      return 1;

   case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
   case PIPE_CAP_TGSI_TES_LAYER_VIEWPORT:
      return screen->info.have_EXT_shader_viewport_index_layer ||
             (screen->spirv_version >= SPIRV_VERSION(1, 5) &&
              screen->info.feats12.shaderOutputLayer &&
              screen->info.feats12.shaderOutputViewportIndex);

   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
      return 1;

   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
      return screen->info.props.limits.minStorageBufferOffsetAlignment;

   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
      return 0; /* TODO: figure these out */

   case PIPE_CAP_CULL_DISTANCE:
      return screen->info.feats.features.shaderCullDistance;

   case PIPE_CAP_SPARSE_BUFFER_PAGE_SIZE:
      
      return screen->info.feats.features.sparseBinding ? ZINK_SPARSE_BUFFER_PAGE_SIZE : 0;

   case PIPE_CAP_VIEWPORT_SUBPIXEL_BITS:
      return screen->info.props.limits.viewportSubPixelBits;

   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
      return 0; /* not sure */

   case PIPE_CAP_MAX_GS_INVOCATIONS:
      return screen->info.props.limits.maxGeometryShaderInvocations;

   case PIPE_CAP_MAX_COMBINED_SHADER_BUFFERS:
      /* gallium handles this automatically */
      return 0;

   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE:
      /* 1<<27 is required by VK spec */
      assert(screen->info.props.limits.maxStorageBufferRange >= 1 << 27);
      /* but Gallium can't handle values that are too big, so clamp to VK spec minimum */
      return 1 << 27;

   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
      return 1;

   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
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
      return screen->info.have_KHR_external_memory_fd && screen->info.have_EXT_external_memory_dma_buf && screen->info.have_EXT_queue_family_foreign;

   case PIPE_CAP_DEPTH_BOUNDS_TEST:
      return screen->info.feats.features.depthBounds;

   case PIPE_CAP_POST_DEPTH_COVERAGE:
      return screen->info.have_EXT_post_depth_coverage;

   case PIPE_CAP_STRING_MARKER:
      return screen->instance_info.have_EXT_debug_utils;

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
      if (!screen->info.feats.features.wideLines)
         return 1.0f;
      return screen->info.props.limits.lineWidthRange[1];

   case PIPE_CAPF_MAX_POINT_WIDTH:
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      if (!screen->info.feats.features.largePoints)
         return 1.0f;
      return screen->info.props.limits.pointSizeRange[1];

   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      if (!screen->info.feats.features.samplerAnisotropy)
         return 1.0f;
      return screen->info.props.limits.maxSamplerAnisotropy;

   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return screen->info.props.limits.maxSamplerLodBias;

   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0f; /* not implemented */
   }

   /* should only get here on unhandled cases */
   return 0.0f;
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
             screen->info.have_KHR_maintenance2)
            return INT_MAX;
         break;

      case PIPE_SHADER_GEOMETRY:
         if (screen->info.feats.features.geometryShader)
            return INT_MAX;
         break;

      case PIPE_SHADER_COMPUTE:
         return INT_MAX;
      default:
         break;
      }
      return 0;
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      return INT_MAX;

   case PIPE_SHADER_CAP_MAX_INPUTS: {
      uint32_t max = 0;
      switch (shader) {
      case PIPE_SHADER_VERTEX:
         max = MIN2(screen->info.props.limits.maxVertexInputAttributes, PIPE_MAX_ATTRIBS);
         break;
      case PIPE_SHADER_TESS_CTRL:
         max = screen->info.props.limits.maxTessellationControlPerVertexInputComponents / 4;
         break;
      case PIPE_SHADER_TESS_EVAL:
         max = screen->info.props.limits.maxTessellationEvaluationInputComponents / 4;
         break;
      case PIPE_SHADER_GEOMETRY:
         max = screen->info.props.limits.maxGeometryInputComponents;
         break;
      case PIPE_SHADER_FRAGMENT:
         /* intel drivers report fewer components, but it's a value that's compatible
          * with what we need for GL, so we can still force a conformant value here
          */
         if (screen->info.driver_props.driverID == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA_KHR ||
             screen->info.driver_props.driverID == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS_KHR)
            return 32;
         max = screen->info.props.limits.maxFragmentInputComponents / 4;
         break;
      default:
         return 0; /* unsupported stage */
      }
      return MIN2(max, 64); // prevent overflowing struct shader_info::inputs_read
   }

   case PIPE_SHADER_CAP_MAX_OUTPUTS: {
      uint32_t max = 0;
      switch (shader) {
      case PIPE_SHADER_VERTEX:
         max = screen->info.props.limits.maxVertexOutputComponents / 4;
         break;
      case PIPE_SHADER_TESS_CTRL:
         max = screen->info.props.limits.maxTessellationControlPerVertexOutputComponents / 4;
         break;
      case PIPE_SHADER_TESS_EVAL:
         max = screen->info.props.limits.maxTessellationEvaluationOutputComponents / 4;
         break;
      case PIPE_SHADER_GEOMETRY:
         max = screen->info.props.limits.maxGeometryOutputComponents / 4;
         break;
      case PIPE_SHADER_FRAGMENT:
         max = screen->info.props.limits.maxColorAttachments;
         break;
      default:
         return 0; /* unsupported stage */
      }
      return MIN2(max, 64); // prevent overflowing struct shader_info::outputs_read/written
   }

   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      /* At least 16384 is guaranteed by VK spec */
      assert(screen->info.props.limits.maxUniformBufferRange >= 16384);
      /* but Gallium can't handle values that are too big */
      return MIN2(screen->info.props.limits.maxUniformBufferRange, 1 << 31);

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
   case PIPE_SHADER_CAP_GLSL_16BIT_CONSTS:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_FP16_CONST_BUFFERS:
      return screen->info.feats11.uniformAndStorageBuffer16BitAccess ||
             (screen->info.have_KHR_16bit_storage && screen->info.storage_16bit_feats.uniformAndStorageBuffer16BitAccess);
   case PIPE_SHADER_CAP_FP16_DERIVATIVES:
      return 0; //spirv requires 32bit derivative srcs and dests
   case PIPE_SHADER_CAP_FP16:
      return screen->info.feats12.shaderFloat16 ||
             (screen->info.have_KHR_shader_float16_int8 &&
              screen->info.shader_float16_int8_feats.shaderFloat16);

   case PIPE_SHADER_CAP_INT16:
      return screen->info.feats.features.shaderInt16;

   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_NIR;

   case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      return MIN2(MIN2(screen->info.props.limits.maxPerStageDescriptorSamplers,
                       screen->info.props.limits.maxPerStageDescriptorSampledImages),
                  PIPE_MAX_SAMPLERS);

   case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      return 0; /* no idea */

   case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
      return 0;

   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      switch (shader) {
      case PIPE_SHADER_VERTEX:
      case PIPE_SHADER_TESS_CTRL:
      case PIPE_SHADER_TESS_EVAL:
      case PIPE_SHADER_GEOMETRY:
         if (!screen->info.feats.features.vertexPipelineStoresAndAtomics)
            return 0;
         break;

      case PIPE_SHADER_FRAGMENT:
         if (!screen->info.feats.features.fragmentStoresAndAtomics)
            return 0;
         break;

      default:
         break;
      }

      /* TODO: this limitation is dumb, and will need some fixes in mesa */
      return MIN2(screen->info.props.limits.maxPerStageDescriptorStorageBuffers, PIPE_MAX_SHADER_BUFFERS);

   case PIPE_SHADER_CAP_SUPPORTED_IRS:
      return (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);

   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      if (screen->info.feats.features.shaderStorageImageExtendedFormats &&
          screen->info.feats.features.shaderStorageImageWriteWithoutFormat)
         return MIN2(screen->info.props.limits.maxPerStageDescriptorStorageImages,
                     PIPE_MAX_SHADER_IMAGES);
      return 0;

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

   if (bind & PIPE_BIND_INDEX_BUFFER) {
      if (format == PIPE_FORMAT_R8_UINT &&
          !screen->info.have_EXT_index_type_uint8)
         return false;
      if (format != PIPE_FORMAT_R8_UINT &&
          format != PIPE_FORMAT_R16_UINT &&
          format != PIPE_FORMAT_R32_UINT)
         return false;
   }

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
      if (bind & PIPE_BIND_SHADER_IMAGE) {
          if (!(screen->info.props.limits.storageImageSampleCounts & sample_mask))
             return false;
      }
   }

   VkFormatProperties props = screen->format_props[format];

   if (target == PIPE_BUFFER) {
      if (bind & PIPE_BIND_VERTEX_BUFFER) {
         if (!(props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) {
            enum pipe_format new_format = zink_decompose_vertex_format(format);
            if (!new_format)
               return false;
            if (!(screen->format_props[new_format].bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
               return false;
         }
      }

      if (bind & PIPE_BIND_SAMPLER_VIEW &&
         !(props.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT))
            return false;

      if (bind & PIPE_BIND_SHADER_IMAGE &&
          !(props.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))
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

      if (bind & PIPE_BIND_SAMPLER_REDUCTION_MINMAX &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT))
         return false;

      if ((bind & PIPE_BIND_SAMPLER_VIEW) || (bind & PIPE_BIND_RENDER_TARGET)) {
         /* if this is a 3-component texture, force gallium to give us 4 components by rejecting this one */
         const struct util_format_description *desc = util_format_description(format);
         if (desc->nr_channels == 3 &&
             (desc->block.bits == 24 || desc->block.bits == 48 || desc->block.bits == 96))
            return false;
      }

      if (bind & PIPE_BIND_DEPTH_STENCIL &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
         return false;

      if (bind & PIPE_BIND_SHADER_IMAGE &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
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
      VKSCR(DestroyDebugUtilsMessengerEXT)(screen->instance, screen->debugUtilsCallbackHandle, NULL);
   }

   if (!screen->info.have_KHR_imageless_framebuffer) {
      hash_table_foreach(&screen->framebuffer_cache, entry) {
         struct zink_framebuffer* fb = (struct zink_framebuffer*)entry->data;
         zink_destroy_framebuffer(screen, fb);
      }
      simple_mtx_destroy(&screen->framebuffer_mtx);
   }

   u_transfer_helper_destroy(pscreen->transfer_helper);
#ifdef ENABLE_SHADER_CACHE
   if (screen->disk_cache) {
      util_queue_finish(&screen->cache_put_thread);
      util_queue_finish(&screen->cache_get_thread);
      disk_cache_wait_for_idle(screen->disk_cache);
      util_queue_destroy(&screen->cache_put_thread);
      util_queue_destroy(&screen->cache_get_thread);
   }
#endif
   disk_cache_destroy(screen->disk_cache);
   zink_bo_deinit(screen);
   util_live_shader_cache_deinit(&screen->shaders);

   if (screen->sem)
      VKSCR(DestroySemaphore)(screen->dev, screen->sem, NULL);
   if (screen->prev_sem)
      VKSCR(DestroySemaphore)(screen->dev, screen->prev_sem, NULL);

   if (screen->threaded)
      util_queue_destroy(&screen->flush_queue);

   VKSCR(DestroyDevice)(screen->dev, NULL);
   vkDestroyInstance(screen->instance, NULL);
   util_idalloc_mt_fini(&screen->buffer_ids);

   if (screen->drm_fd != -1)
      close(screen->drm_fd);

   slab_destroy_parent(&screen->transfer_pool);
   ralloc_free(screen);
}

static void
choose_pdev(struct zink_screen *screen)
{
   uint32_t i, pdev_count;
   VkPhysicalDevice *pdevs;
   VkResult result = vkEnumeratePhysicalDevices(screen->instance, &pdev_count, NULL);
   if (result != VK_SUCCESS)
      return;

   assert(pdev_count > 0);

   pdevs = malloc(sizeof(*pdevs) * pdev_count);
   result = vkEnumeratePhysicalDevices(screen->instance, &pdev_count, pdevs);
   assert(result == VK_SUCCESS);
   assert(pdev_count > 0);

   VkPhysicalDeviceProperties *props = &screen->info.props;
   for (i = 0; i < pdev_count; ++i) {
      vkGetPhysicalDeviceProperties(pdevs[i], props);

#ifdef ZINK_WITH_SWRAST_VK
      char *use_lavapipe = getenv("ZINK_USE_LAVAPIPE");
      if (use_lavapipe) {
         if (props->deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            screen->pdev = pdevs[i];
            screen->info.device_version = props->apiVersion;
            break;
         }
         continue;
      }
#endif
      if (props->deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
         screen->pdev = pdevs[i];
         screen->info.device_version = props->apiVersion;
         break;
      }
   }
   free(pdevs);

   /* runtime version is the lesser of the instance version and device version */
   screen->vk_version = MIN2(screen->info.device_version, screen->instance_info.loader_version);

   /* calculate SPIR-V version based on VK version */
   if (screen->vk_version >= VK_MAKE_VERSION(1, 2, 0))
      screen->spirv_version = SPIRV_VERSION(1, 5);
   else if (screen->vk_version >= VK_MAKE_VERSION(1, 1, 0))
      screen->spirv_version = SPIRV_VERSION(1, 3);
   else
      screen->spirv_version = SPIRV_VERSION(1, 0);
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
         screen->max_queues = props[i].queueCount;
         screen->timestamp_valid_bits = props[i].timestampValidBits;
         break;
      }
   }
   free(props);
}

static void
init_queue(struct zink_screen *screen)
{
   vkGetDeviceQueue(screen->dev, screen->gfx_queue, 0, &screen->queue);
   if (screen->threaded && screen->max_queues > 1)
      vkGetDeviceQueue(screen->dev, screen->gfx_queue, 1, &screen->thread_queue);
   else
      screen->thread_queue = screen->queue;
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
      struct pipe_transfer *transfer = NULL;
      void *res_map = pipe_texture_map(pcontext, pres, level, layer, PIPE_MAP_READ, 0, 0,
                                        u_minify(pres->width0, level),
                                        u_minify(pres->height0, level),
                                        &transfer);
      if (res_map) {
         util_copy_rect((ubyte*)map, pres->format, res->dt_stride, 0, 0,
                        transfer->box.width, transfer->box.height,
                        (const ubyte*)res_map, transfer->stride, 0, 0);
         pipe_texture_unmap(pcontext, transfer);
      }
      winsys->displaytarget_unmap(winsys, res->dt);
   }

   winsys->displaytarget_display(winsys, res->dt, winsys_drawable_handle, sub_box);
}

bool
zink_is_depth_format_supported(struct zink_screen *screen, VkFormat format)
{
   VkFormatProperties props;
   VKSCR(GetPhysicalDeviceFormatProperties)(screen->pdev, format, &props);
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

   case PIPE_FORMAT_R8G8B8X8_SINT:
      return PIPE_FORMAT_R8G8B8A8_SINT;
   case PIPE_FORMAT_R8G8B8X8_SNORM:
      return PIPE_FORMAT_R8G8B8A8_SNORM;
   case PIPE_FORMAT_R8G8B8X8_UNORM:
      return PIPE_FORMAT_R8G8B8A8_UNORM;

   case PIPE_FORMAT_R16G16B16X16_FLOAT:
      return PIPE_FORMAT_R16G16B16A16_FLOAT;
   case PIPE_FORMAT_R16G16B16X16_SINT:
      return PIPE_FORMAT_R16G16B16A16_SINT;
   case PIPE_FORMAT_R16G16B16X16_SNORM:
      return PIPE_FORMAT_R16G16B16A16_SNORM;
   case PIPE_FORMAT_R16G16B16X16_UNORM:
      return PIPE_FORMAT_R16G16B16A16_UNORM;

   default:
      return format;
   }
}

VkFormat
zink_get_format(struct zink_screen *screen, enum pipe_format format)
{
   VkFormat ret = zink_pipe_format_to_vk_format(emulate_x8(format));

   if (format == PIPE_FORMAT_X32_S8X24_UINT)
      return VK_FORMAT_D32_SFLOAT_S8_UINT;

   if (format == PIPE_FORMAT_X24S8_UINT)
      /* valid when using aspects to extract stencil,
       * fails format test because it's emulated */
      ret = VK_FORMAT_D24_UNORM_S8_UINT;

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

void
zink_screen_init_descriptor_funcs(struct zink_screen *screen, bool fallback)
{
   if (screen->info.have_KHR_descriptor_update_template &&
       !fallback &&
       screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_LAZY) {
#define LAZY(FUNC) screen->FUNC = zink_##FUNC##_lazy
      LAZY(descriptor_program_init);
      LAZY(descriptor_program_deinit);
      LAZY(context_invalidate_descriptor_state);
      LAZY(batch_descriptor_init);
      LAZY(batch_descriptor_reset);
      LAZY(batch_descriptor_deinit);
      LAZY(descriptors_init);
      LAZY(descriptors_deinit);
      LAZY(descriptors_update);
#undef LAZY
   } else {
#define DEFAULT(FUNC) screen->FUNC = zink_##FUNC
      DEFAULT(descriptor_program_init);
      DEFAULT(descriptor_program_deinit);
      DEFAULT(context_invalidate_descriptor_state);
      DEFAULT(batch_descriptor_init);
      DEFAULT(batch_descriptor_reset);
      DEFAULT(batch_descriptor_deinit);
      DEFAULT(descriptors_init);
      DEFAULT(descriptors_deinit);
      DEFAULT(descriptors_update);
#undef DEFAULT
   }
}

static bool
check_have_device_time(struct zink_screen *screen)
{
   uint32_t num_domains = 0;
   VkTimeDomainEXT domains[8]; //current max is 4
   VKSCR(GetPhysicalDeviceCalibrateableTimeDomainsEXT)(screen->pdev, &num_domains, NULL);
   assert(num_domains > 0);
   assert(num_domains < ARRAY_SIZE(domains));

   VKSCR(GetPhysicalDeviceCalibrateableTimeDomainsEXT)(screen->pdev, &num_domains, domains);

   /* VK_TIME_DOMAIN_DEVICE_EXT is used for the ctx->get_timestamp hook and is the only one we really need */
   for (unsigned i = 0; i < num_domains; i++) {
      if (domains[i] == VK_TIME_DOMAIN_DEVICE_EXT) {
         return true;
      }
   }

   return false;
}

static void
zink_error(const char *msg)
{
   fprintf(stderr, "zink ERR: '%s'\n", msg);
}

static void
zink_warn(const char *msg)
{
   fprintf(stderr, "zink WRN: '%s'\n", msg);
}

static void
zink_info(const char *msg)
{
   fprintf(stderr, "zink NFO: '%s'\n", msg);
}

static void
zink_msg(const char *msg)
{
   fprintf(stderr, "zink MSG: '%s'\n", msg);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
zink_debug_util_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageType,
    const VkDebugUtilsMessengerCallbackDataEXT      *pCallbackData,
    void                                            *pUserData)
{
   // Pick message prefix and color to use.
   // Only MacOS and Linux have been tested for color support
   if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
      zink_error(pCallbackData->pMessage);
   } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
      zink_warn(pCallbackData->pMessage);
   } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
      zink_info(pCallbackData->pMessage);
   } else
      zink_msg(pCallbackData->pMessage);

   return VK_FALSE;
}

static bool
create_debug(struct zink_screen *screen)
{
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

   VKSCR(CreateDebugUtilsMessengerEXT)(
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

   GET_PROC_ADDR_INSTANCE_LOCAL(screen->instance, GetMoltenVKConfigurationMVK);
   GET_PROC_ADDR_INSTANCE_LOCAL(screen->instance, SetMoltenVKConfigurationMVK);
   GET_PROC_ADDR_INSTANCE_LOCAL(screen->instance, GetVersionStringsMVK);

   if (vk_GetVersionStringsMVK) {
      char molten_version[64] = {0};
      char vulkan_version[64] = {0};

      vk_GetVersionStringsMVK(molten_version, sizeof(molten_version) - 1, vulkan_version, sizeof(vulkan_version) - 1);

      printf("zink: MoltenVK %s Vulkan %s \n", molten_version, vulkan_version);
   }

   if (vk_GetMoltenVKConfigurationMVK && vk_SetMoltenVKConfigurationMVK) {
      MVKConfiguration molten_config = {0};
      size_t molten_config_size = sizeof(molten_config);

      VkResult res = vk_GetMoltenVKConfigurationMVK(screen->instance, &molten_config, &molten_config_size);
      if (res == VK_SUCCESS || res == VK_INCOMPLETE) {
         // Needed to allow MoltenVK to accept VkImageView swizzles.
         // Encountered when using VK_FORMAT_R8G8_UNORM
         molten_config.fullImageViewSwizzle = VK_TRUE;
         vk_SetMoltenVKConfigurationMVK(screen->instance, &molten_config, &molten_config_size);
      }
   }
#endif // MVK_VERSION

   return true;
}

static void
check_device_needs_mesa_wsi(struct zink_screen *screen)
{
   if (
       /* Raspberry Pi 4 V3DV driver */
       (screen->info.props.vendorID == 0x14E4 &&
        screen->info.props.deviceID == 42) ||
       /* RADV */
       screen->info.driver_props.driverID == VK_DRIVER_ID_MESA_RADV_KHR
      ) {
      screen->needs_mesa_wsi = true;
   } else if (screen->info.driver_props.driverID == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA_KHR)
      screen->needs_mesa_flush_wsi = true;

}

static void
populate_format_props(struct zink_screen *screen)
{
   for (unsigned i = 0; i < PIPE_FORMAT_COUNT; i++) {
      VkFormat format = zink_get_format(screen, i);
      if (!format)
         continue;
      if (VKSCR(GetPhysicalDeviceFormatProperties2)) {
         VkFormatProperties2 props = {0};
         props.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;

         VkDrmFormatModifierPropertiesListEXT mod_props;
         VkDrmFormatModifierPropertiesEXT mods[128];
         if (screen->info.have_EXT_image_drm_format_modifier) {
            mod_props.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
            mod_props.pNext = NULL;
            mod_props.drmFormatModifierCount = ARRAY_SIZE(mods);
            mod_props.pDrmFormatModifierProperties = mods;
            props.pNext = &mod_props;
         }
         VKSCR(GetPhysicalDeviceFormatProperties2)(screen->pdev, format, &props);
         screen->format_props[i] = props.formatProperties;
         if (screen->info.have_EXT_image_drm_format_modifier && mod_props.drmFormatModifierCount) {
            screen->modifier_props[i].drmFormatModifierCount = mod_props.drmFormatModifierCount;
            screen->modifier_props[i].pDrmFormatModifierProperties = ralloc_array(screen, VkDrmFormatModifierPropertiesEXT, mod_props.drmFormatModifierCount);
            if (mod_props.pDrmFormatModifierProperties) {
               for (unsigned j = 0; j < mod_props.drmFormatModifierCount; j++)
                  screen->modifier_props[i].pDrmFormatModifierProperties[j] = mod_props.pDrmFormatModifierProperties[j];
            }
         }
      } else
         VKSCR(GetPhysicalDeviceFormatProperties)(screen->pdev, format, &screen->format_props[i]);
   }
}

bool
zink_screen_init_semaphore(struct zink_screen *screen)
{
   VkSemaphoreCreateInfo sci = {0};
   VkSemaphoreTypeCreateInfo tci = {0};
   VkSemaphore sem;
   sci.pNext = &tci;
   sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
   tci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
   tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

   if (VKSCR(CreateSemaphore)(screen->dev, &sci, NULL, &sem) == VK_SUCCESS) {
      /* semaphore signal values can never decrease,
       * so we need a new semaphore anytime we overflow
       */
      if (screen->prev_sem)
         VKSCR(DestroySemaphore)(screen->dev, screen->prev_sem, NULL);
      screen->prev_sem = screen->sem;
      screen->sem = sem;
      return true;
   }
   screen->info.have_KHR_timeline_semaphore = false;
   return false;
}

bool
zink_screen_timeline_wait(struct zink_screen *screen, uint32_t batch_id, uint64_t timeout)
{
   VkSemaphoreWaitInfo wi = {0};

   if (zink_screen_check_last_finished(screen, batch_id))
      return true;

   wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
   wi.semaphoreCount = 1;
   /* handle batch_id overflow */
   wi.pSemaphores = batch_id > screen->curr_batch ? &screen->prev_sem : &screen->sem;
   uint64_t batch_id64 = batch_id;
   wi.pValues = &batch_id64;
   bool success = false;
   if (screen->device_lost)
      return true;
   VkResult ret = VKSCR(WaitSemaphores)(screen->dev, &wi, timeout);
   success = zink_screen_handle_vkresult(screen, ret);

   if (success)
      zink_screen_update_last_finished(screen, batch_id);

   return success;
}

struct noop_submit_info {
   struct zink_screen *screen;
   VkFence fence;
};

static void
noop_submit(void *data, void *gdata, int thread_index)
{
   struct noop_submit_info *n = data;
   VkSubmitInfo si = {0};
   si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   if (n->VKSCR(QueueSubmit)(n->screen->threaded ? n->screen->thread_queue : n->screen->queue,
                     1, &si, n->fence) != VK_SUCCESS) {
      debug_printf("ZINK: vkQueueSubmit() failed\n");
      n->screen->device_lost = true;
   }
}

bool
zink_screen_batch_id_wait(struct zink_screen *screen, uint32_t batch_id, uint64_t timeout)
{
   if (zink_screen_check_last_finished(screen, batch_id))
      return true;

   if (screen->info.have_KHR_timeline_semaphore)
      return zink_screen_timeline_wait(screen, batch_id, timeout);

   if (!timeout)
      return false;

   uint32_t new_id = 0;
   while (!new_id)
      new_id = p_atomic_inc_return(&screen->curr_batch);
   VkResult ret;
   struct noop_submit_info n;
   uint64_t abs_timeout = os_time_get_absolute_timeout(timeout);
   uint64_t remaining = PIPE_TIMEOUT_INFINITE;
   VkFenceCreateInfo fci = {0};
   struct util_queue_fence fence;
   util_queue_fence_init(&fence);
   fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

   if (VKSCR(CreateFence)(screen->dev, &fci, NULL, &n.fence) != VK_SUCCESS)
      return false;

   n.screen = screen;
   if (screen->threaded) {
      /* must use thread dispatch for sanity */
      util_queue_add_job(&screen->flush_queue, &n, &fence, noop_submit, NULL, 0);
      util_queue_fence_wait(&fence);
   } else {
      noop_submit(&n, NULL, 0);
   }
   if (timeout != PIPE_TIMEOUT_INFINITE) {
      int64_t time_ns = os_time_get_nano();
      remaining = abs_timeout > time_ns ? abs_timeout - time_ns : 0;
   }

   if (remaining)
      ret = VKSCR(WaitForFences)(screen->dev, 1, &n.fence, VK_TRUE, remaining);
   else
      ret = VKSCR(GetFenceStatus)(screen->dev, n.fence);
   VKSCR(DestroyFence)(screen->dev, n.fence, NULL);
   bool success = zink_screen_handle_vkresult(screen, ret);

   if (success)
      zink_screen_update_last_finished(screen, new_id);

   return success;
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

static void
zink_query_memory_info(struct pipe_screen *pscreen, struct pipe_memory_info *info)
{
   struct zink_screen *screen = zink_screen(pscreen);
   memset(info, 0, sizeof(struct pipe_memory_info));
   if (screen->info.have_EXT_memory_budget && VKSCR(GetPhysicalDeviceMemoryProperties2)) {
      VkPhysicalDeviceMemoryProperties2 mem = {0};
      mem.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;

      VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {0};
      budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
      mem.pNext = &budget;
      VKSCR(GetPhysicalDeviceMemoryProperties2)(screen->pdev, &mem);

      for (unsigned i = 0; i < mem.memoryProperties.memoryHeapCount; i++) {
         if (mem.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            /* VRAM */
            info->total_device_memory += mem.memoryProperties.memoryHeaps[i].size / 1024;
            info->avail_device_memory += (budget.heapBudget[i] - budget.heapUsage[i]) / 1024;
         } else {
            /* GART */
            info->total_staging_memory += mem.memoryProperties.memoryHeaps[i].size / 1024;
            info->avail_staging_memory += (budget.heapBudget[i] - budget.heapUsage[i]) / 1024;
         }
      }
      /* evictions not yet supported in vulkan */
   } else {
      for (unsigned i = 0; i < screen->info.mem_props.memoryHeapCount; i++) {
         if (screen->info.mem_props.memoryHeaps[i].flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            /* VRAM */
            info->total_device_memory += screen->info.mem_props.memoryHeaps[i].size / 1024;
            /* free real estate! */
            info->avail_device_memory += info->total_device_memory;
         } else {
            /* GART */
            info->total_staging_memory += screen->info.mem_props.memoryHeaps[i].size / 1024;
            /* free real estate! */
            info->avail_staging_memory += info->total_staging_memory;
         }
      }
   }
}

static void
zink_query_dmabuf_modifiers(struct pipe_screen *pscreen, enum pipe_format format, int max, uint64_t *modifiers, unsigned int *external_only, int *count)
{
   struct zink_screen *screen = zink_screen(pscreen);
   *count = screen->modifier_props[format].drmFormatModifierCount;
   for (int i = 0; i < MIN2(max, *count); i++)
      modifiers[i] = screen->modifier_props[format].pDrmFormatModifierProperties[i].drmFormatModifier;
}

static bool
zink_is_dmabuf_modifier_supported(struct pipe_screen *pscreen, uint64_t modifier, enum pipe_format format, bool *external_only)
{
   struct zink_screen *screen = zink_screen(pscreen);
   for (unsigned i = 0; i < screen->modifier_props[format].drmFormatModifierCount; i++)
      if (screen->modifier_props[format].pDrmFormatModifierProperties[i].drmFormatModifier == modifier)
         return true;
   return false;
}

static unsigned
zink_get_dmabuf_modifier_planes(struct pipe_screen *pscreen, uint64_t modifier, enum pipe_format format)
{
   struct zink_screen *screen = zink_screen(pscreen);
   for (unsigned i = 0; i < screen->modifier_props[format].drmFormatModifierCount; i++)
      if (screen->modifier_props[format].pDrmFormatModifierProperties[i].drmFormatModifier == modifier)
         return screen->modifier_props[format].pDrmFormatModifierProperties[i].drmFormatModifierPlaneCount;
   return 0;
}

static VkDevice
zink_create_logical_device(struct zink_screen *screen)
{
   VkDevice dev = VK_NULL_HANDLE;

   VkDeviceQueueCreateInfo qci = {0};
   float dummy = 0.0f;
   qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   qci.queueFamilyIndex = screen->gfx_queue;
   qci.queueCount = screen->threaded && screen->max_queues > 1 ? 2 : 1;
   qci.pQueuePriorities = &dummy;

   VkDeviceCreateInfo dci = {0};
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

static void
pre_hash_descriptor_states(struct zink_screen *screen)
{
   VkImageViewCreateInfo null_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
   VkBufferViewCreateInfo null_binfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
   screen->null_descriptor_hashes.image_view = _mesa_hash_data(&null_info, sizeof(VkImageViewCreateInfo));
   screen->null_descriptor_hashes.buffer_view = _mesa_hash_data(&null_binfo, sizeof(VkBufferViewCreateInfo));
}

static void
check_base_requirements(struct zink_screen *screen)
{
   if (!screen->info.feats.features.logicOp ||
       !screen->info.feats.features.fillModeNonSolid ||
       !screen->info.feats.features.wideLines ||
       !screen->info.feats.features.largePoints ||
       !screen->info.feats.features.shaderClipDistance ||
       !(screen->info.feats12.scalarBlockLayout ||
         screen->info.have_EXT_scalar_block_layout) ||
       !screen->info.have_KHR_maintenance1 ||
       !screen->info.have_EXT_custom_border_color ||
       !screen->info.have_EXT_line_rasterization) {
      fprintf(stderr, "WARNING: Some incorrect rendering "
              "might occur because the selected Vulkan device (%s) doesn't support "
              "base Zink requirements: ", screen->info.props.deviceName);
#define CHECK_OR_PRINT(X) \
      if (!screen->info.X) \
         fprintf(stderr, "%s ", #X)
      CHECK_OR_PRINT(feats.features.logicOp);
      CHECK_OR_PRINT(feats.features.fillModeNonSolid);
      CHECK_OR_PRINT(feats.features.wideLines);
      CHECK_OR_PRINT(feats.features.largePoints);
      CHECK_OR_PRINT(feats.features.shaderClipDistance);
      if (!screen->info.feats12.scalarBlockLayout && !screen->info.have_EXT_scalar_block_layout)
         printf("scalarBlockLayout OR EXT_scalar_block_layout ");
      CHECK_OR_PRINT(have_KHR_maintenance1);
      CHECK_OR_PRINT(have_EXT_custom_border_color);
      CHECK_OR_PRINT(have_EXT_line_rasterization);
      fprintf(stderr, "\n");
   }
}

static void
zink_get_sample_pixel_grid(struct pipe_screen *pscreen, unsigned sample_count,
                           unsigned *width, unsigned *height)
{
   struct zink_screen *screen = zink_screen(pscreen);
   unsigned idx = util_logbase2_ceil(MAX2(sample_count, 1));
   assert(idx < ARRAY_SIZE(screen->maxSampleLocationGridSize));
   *width = screen->maxSampleLocationGridSize[idx].width;
   *height = screen->maxSampleLocationGridSize[idx].height;
}

static struct zink_screen *
zink_internal_create_screen(const struct pipe_screen_config *config)
{
   struct zink_screen *screen = rzalloc(NULL, struct zink_screen);
   if (!screen)
      return NULL;

   util_cpu_detect();
   screen->threaded = util_get_cpu_caps()->nr_cpus > 1 && debug_get_bool_option("GALLIUM_THREAD", util_get_cpu_caps()->nr_cpus > 1);
   if (screen->threaded)
      util_queue_init(&screen->flush_queue, "zfq", 8, 1, UTIL_QUEUE_INIT_RESIZE_IF_FULL, NULL);

   zink_debug = debug_get_option_zink_debug();
   screen->descriptor_mode = debug_get_option_zink_descriptor_mode();
   if (screen->descriptor_mode > ZINK_DESCRIPTOR_MODE_NOTEMPLATES) {
      printf("Specify exactly one descriptor mode.\n");
      abort();
   }

   screen->instance_info.loader_version = zink_get_loader_version();
   screen->instance = zink_create_instance(&screen->instance_info);

   if (!screen->instance)
      goto fail;

   vk_instance_dispatch_table_load(&screen->vk.instance, &vkGetInstanceProcAddr, screen->instance);
   vk_physical_device_dispatch_table_load(&screen->vk.physical_device, &vkGetInstanceProcAddr, screen->instance);

   zink_verify_instance_extensions(screen);

   if (screen->instance_info.have_EXT_debug_utils &&
      (zink_debug & ZINK_DEBUG_VALIDATION) && !create_debug(screen))
      debug_printf("ZINK: failed to setup debug utils\n");

   choose_pdev(screen);
   if (screen->pdev == VK_NULL_HANDLE)
      goto fail;

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

   init_queue(screen);
   if (screen->info.driver_props.driverID == VK_DRIVER_ID_MESA_RADV ||
       screen->info.driver_props.driverID == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
       screen->info.driver_props.driverID == VK_DRIVER_ID_AMD_PROPRIETARY)
      /* this has bad perf on AMD */
      screen->info.have_KHR_push_descriptor = false;

   vk_device_dispatch_table_load(&screen->vk.device, &vkGetDeviceProcAddr, screen->dev);

   zink_verify_device_extensions(screen);

   if (screen->info.have_EXT_calibrated_timestamps && !check_have_device_time(screen))
      goto fail;

   screen->have_triangle_fans = true;
#if defined(VK_EXTX_PORTABILITY_SUBSET_EXTENSION_NAME)
   if (screen->info.have_EXTX_portability_subset) {
      screen->have_triangle_fans = (VK_TRUE == screen->info.portability_subset_extx_feats.triangleFans);
   }
#endif // VK_EXTX_PORTABILITY_SUBSET_EXTENSION_NAME

   check_base_requirements(screen);
   util_live_shader_cache_init(&screen->shaders, zink_create_gfx_shader_state, zink_delete_shader_state);

   screen->base.get_name = zink_get_name;
   screen->base.get_vendor = zink_get_vendor;
   screen->base.get_device_vendor = zink_get_device_vendor;
   screen->base.get_compute_param = zink_get_compute_param;
   screen->base.query_memory_info = zink_query_memory_info;
   screen->base.get_param = zink_get_param;
   screen->base.get_paramf = zink_get_paramf;
   screen->base.get_shader_param = zink_get_shader_param;
   screen->base.get_compiler_options = zink_get_compiler_options;
   screen->base.get_sample_pixel_grid = zink_get_sample_pixel_grid;
   screen->base.is_format_supported = zink_is_format_supported;
   screen->base.query_dmabuf_modifiers = zink_query_dmabuf_modifiers;
   screen->base.is_dmabuf_modifier_supported = zink_is_dmabuf_modifier_supported;
   screen->base.get_dmabuf_modifier_planes = zink_get_dmabuf_modifier_planes;
   screen->base.context_create = zink_context_create;
   screen->base.flush_frontbuffer = zink_flush_frontbuffer;
   screen->base.destroy = zink_destroy_screen;
   screen->base.finalize_nir = zink_shader_finalize;

   if (screen->info.have_EXT_sample_locations) {
      VkMultisamplePropertiesEXT prop;
      prop.sType = VK_STRUCTURE_TYPE_MULTISAMPLE_PROPERTIES_EXT;
      prop.pNext = NULL;
      for (unsigned i = 0; i < ARRAY_SIZE(screen->maxSampleLocationGridSize); i++) {
         if (screen->info.sample_locations_props.sampleLocationSampleCounts & (1 << i)) {
            VKSCR(GetPhysicalDeviceMultisamplePropertiesEXT)(screen->pdev, 1 << i, &prop);
            screen->maxSampleLocationGridSize[i] = prop.maxSampleLocationGridSize;
         }
      }
   }

   if (!zink_screen_resource_init(&screen->base))
      goto fail;
   zink_bo_init(screen);
   zink_screen_fence_init(&screen->base);

   zink_screen_init_compiler(screen);
   disk_cache_init(screen);
   populate_format_props(screen);
   pre_hash_descriptor_states(screen);

   slab_create_parent(&screen->transfer_pool, sizeof(struct zink_transfer), 16);

#if WITH_XMLCONFIG
   if (config) {
      driParseConfigFiles(config->options, config->options_info, 0, "zink",
                          NULL, NULL, NULL, 0, NULL, 0);
      screen->driconf.dual_color_blend_by_location = driQueryOptionb(config->options, "dual_color_blend_by_location");
      //screen->driconf.inline_uniforms = driQueryOptionb(config->options, "radeonsi_inline_uniforms");
   }
#endif
   screen->driconf.inline_uniforms = debug_get_bool_option("ZINK_INLINE_UNIFORMS", false);

   screen->total_video_mem = get_video_mem(screen);
   screen->clamp_video_mem = screen->total_video_mem * 0.8;
   if (!os_get_total_physical_memory(&screen->total_mem))
      goto fail;

   if (debug_get_bool_option("ZINK_NO_TIMELINES", false))
      screen->info.have_KHR_timeline_semaphore = false;
   if (screen->info.have_KHR_timeline_semaphore)
      zink_screen_init_semaphore(screen);

   memset(&screen->heap_map, UINT8_MAX, sizeof(screen->heap_map));
   for (enum zink_heap i = 0; i < ZINK_HEAP_MAX; i++) {
      for (unsigned j = 0; j < screen->info.mem_props.memoryTypeCount; j++) {
         VkMemoryPropertyFlags domains = vk_domain_from_heap(i);
         if ((screen->info.mem_props.memoryTypes[j].propertyFlags & domains) == domains) {
            assert(screen->heap_map[i] == UINT8_MAX);
            screen->heap_map[i] = j;
            break;
         }
      }

      /* not found: use compatible heap */
      if (screen->heap_map[i] == UINT8_MAX) {
         /* only cached mem has a failure case for now */
         assert(i == ZINK_HEAP_HOST_VISIBLE_CACHED || i == ZINK_HEAP_DEVICE_LOCAL_LAZY);
         if (i == ZINK_HEAP_HOST_VISIBLE_CACHED)
            screen->heap_map[i] = screen->heap_map[ZINK_HEAP_HOST_VISIBLE_COHERENT];
         else
            screen->heap_map[i] = screen->heap_map[ZINK_HEAP_DEVICE_LOCAL];
      }
   }
   {
      unsigned vis_vram = screen->heap_map[ZINK_HEAP_DEVICE_LOCAL_VISIBLE];
      unsigned vram = screen->heap_map[ZINK_HEAP_DEVICE_LOCAL];
      /* determine if vis vram is roughly equal to total vram */
      if (screen->info.mem_props.memoryHeaps[screen->info.mem_props.memoryTypes[vis_vram].heapIndex].size >
          screen->info.mem_props.memoryHeaps[screen->info.mem_props.memoryTypes[vram].heapIndex].size * 0.9)
         screen->resizable_bar = true;
   }

   if (!screen->info.have_KHR_imageless_framebuffer) {
      simple_mtx_init(&screen->framebuffer_mtx, mtx_plain);
      _mesa_hash_table_init(&screen->framebuffer_cache, screen, hash_framebuffer_state, equals_framebuffer_state);
   }

   zink_screen_init_descriptor_funcs(screen, false);
   util_idalloc_mt_init_tc(&screen->buffer_ids);

   return screen;

fail:
   ralloc_free(screen);
   return NULL;
}

struct pipe_screen *
zink_create_screen(struct sw_winsys *winsys)
{
   struct zink_screen *ret = zink_internal_create_screen(NULL);
   if (ret) {
      ret->winsys = winsys;
      ret->drm_fd = -1;
   }

   return &ret->base;
}

struct pipe_screen *
zink_drm_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct zink_screen *ret = zink_internal_create_screen(config);

   if (ret)
      ret->drm_fd = os_dupfd_cloexec(fd);
   if (ret && !ret->info.have_KHR_external_memory_fd) {
      debug_printf("ZINK: KHR_external_memory_fd required!\n");
      zink_destroy_screen(&ret->base);
      return NULL;
   }

   return &ret->base;
}

void zink_stub_function_not_loaded()
{
   /* this will be used by the zink_verify_*_extensions() functions on a
    * release build
    */
   mesa_loge("ZINK: a Vulkan function was called without being loaded");
   abort();
}
