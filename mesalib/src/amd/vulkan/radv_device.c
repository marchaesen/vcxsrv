/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
 */

#include <fcntl.h>
#include <stdbool.h>
#include <string.h>

#ifdef __FreeBSD__
#include <sys/types.h>
#endif
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

#ifdef __linux__
#include <sys/inotify.h>
#endif

#include "util/debug.h"
#include "util/disk_cache.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "vk_util.h"
#ifdef _WIN32
typedef void *drmDevicePtr;
#include <io.h>
#else
#include <amdgpu.h>
#include <xf86drm.h>
#include "drm-uapi/amdgpu_drm.h"
#include "winsys/amdgpu/radv_amdgpu_winsys_public.h"
#endif
#include "util/build_id.h"
#include "util/debug.h"
#include "util/driconf.h"
#include "util/mesa-sha1.h"
#include "util/os_time.h"
#include "util/timespec.h"
#include "util/u_atomic.h"
#include "winsys/null/radv_null_winsys_public.h"
#include "git_sha1.h"
#include "sid.h"
#include "vk_format.h"
#include "vk_sync.h"
#include "vk_sync_dummy.h"
#include "vulkan/vk_icd.h"

#ifdef LLVM_AVAILABLE
#include "ac_llvm_util.h"
#endif

/* The number of IBs per submit isn't infinite, it depends on the IP type
 * (ie. some initial setup needed for a submit) and the number of IBs (4 DW).
 * This limit is arbitrary but should be safe for now.  Ideally, we should get
 * this limit from the KMD.
 */
#define RADV_MAX_IBS_PER_SUBMIT 192

/* The "RAW" clocks on Linux are called "FAST" on FreeBSD */
#if !defined(CLOCK_MONOTONIC_RAW) && defined(CLOCK_MONOTONIC_FAST)
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC_FAST
#endif

static VkResult radv_queue_submit(struct vk_queue *vqueue, struct vk_queue_submit *submission);

static void
parse_hex(char *out, const char *in, unsigned length)
{
   for (unsigned i = 0; i < length; ++i)
      out[i] = 0;

   for (unsigned i = 0; i < 2 * length; ++i) {
      unsigned v =
         in[i] <= '9' ? in[i] - '0' : (in[i] >= 'a' ? (in[i] - 'a' + 10) : (in[i] - 'A' + 10));
      out[i / 2] |= v << (4 * (1 - i % 2));
   }
}

static int
radv_device_get_cache_uuid(struct radv_physical_device *pdevice, void *uuid)
{
   enum radeon_family family = pdevice->rad_info.family;
   struct mesa_sha1 ctx;
   unsigned char sha1[20];
   unsigned ptr_size = sizeof(void *);

   memset(uuid, 0, VK_UUID_SIZE);
   _mesa_sha1_init(&ctx);

#ifdef RADV_BUILD_ID_OVERRIDE
   {
      char data[strlen(RADV_BUILD_ID_OVERRIDE) / 2];
      parse_hex(data, RADV_BUILD_ID_OVERRIDE, ARRAY_SIZE(data));
      _mesa_sha1_update(&ctx, data, ARRAY_SIZE(data));
   }
#else
   if (!disk_cache_get_function_identifier(radv_device_get_cache_uuid, &ctx))
      return -1;
#endif

#ifdef LLVM_AVAILABLE
   if (pdevice->use_llvm &&
       !disk_cache_get_function_identifier(LLVMInitializeAMDGPUTargetInfo, &ctx))
      return -1;
#endif

   _mesa_sha1_update(&ctx, &family, sizeof(family));
   _mesa_sha1_update(&ctx, &ptr_size, sizeof(ptr_size));
   _mesa_sha1_final(&ctx, sha1);

   memcpy(uuid, sha1, VK_UUID_SIZE);
   return 0;
}

static void
radv_get_driver_uuid(void *uuid)
{
   ac_compute_driver_uuid(uuid, VK_UUID_SIZE);
}

static void
radv_get_device_uuid(struct radeon_info *info, void *uuid)
{
   ac_compute_device_uuid(info, uuid, VK_UUID_SIZE);
}

static uint64_t
radv_get_adjusted_vram_size(struct radv_physical_device *device)
{
   int ov = driQueryOptioni(&device->instance->dri_options, "override_vram_size");
   if (ov >= 0)
      return MIN2((uint64_t)device->rad_info.vram_size_kb * 1024, (uint64_t)ov << 20);
   return (uint64_t)device->rad_info.vram_size_kb * 1024;
}

static uint64_t
radv_get_visible_vram_size(struct radv_physical_device *device)
{
   return MIN2(radv_get_adjusted_vram_size(device), (uint64_t)device->rad_info.vram_vis_size_kb * 1024);
}

static uint64_t
radv_get_vram_size(struct radv_physical_device *device)
{
   uint64_t total_size = radv_get_adjusted_vram_size(device);
   return total_size - MIN2(total_size, (uint64_t)device->rad_info.vram_vis_size_kb * 1024);
}

enum radv_heap {
   RADV_HEAP_VRAM = 1 << 0,
   RADV_HEAP_GTT = 1 << 1,
   RADV_HEAP_VRAM_VIS = 1 << 2,
   RADV_HEAP_MAX = 1 << 3,
};

static void
radv_physical_device_init_mem_types(struct radv_physical_device *device)
{
   uint64_t visible_vram_size = radv_get_visible_vram_size(device);
   uint64_t vram_size = radv_get_vram_size(device);
   uint64_t gtt_size = (uint64_t)device->rad_info.gart_size_kb * 1024;
   int vram_index = -1, visible_vram_index = -1, gart_index = -1;

   device->memory_properties.memoryHeapCount = 0;
   device->heaps = 0;

   if (!device->rad_info.has_dedicated_vram) {
      /* On APUs, the carveout is usually too small for games that request a minimum VRAM size
       * greater than it. To workaround this, we compute the total available memory size (GTT +
       * visible VRAM size) and report 2/3 as VRAM and 1/3 as GTT.
       */
      const uint64_t total_size = gtt_size + visible_vram_size;
      visible_vram_size = align64((total_size * 2) / 3, device->rad_info.gart_page_size);
      gtt_size = total_size - visible_vram_size;
      vram_size = 0;
   }

   /* Only get a VRAM heap if it is significant, not if it is a 16 MiB
    * remainder above visible VRAM. */
   if (vram_size > 0 && vram_size * 9 >= visible_vram_size) {
      vram_index = device->memory_properties.memoryHeapCount++;
      device->heaps |= RADV_HEAP_VRAM;
      device->memory_properties.memoryHeaps[vram_index] = (VkMemoryHeap){
         .size = vram_size,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      };
   }

   if (gtt_size > 0) {
      gart_index = device->memory_properties.memoryHeapCount++;
      device->heaps |= RADV_HEAP_GTT;
      device->memory_properties.memoryHeaps[gart_index] = (VkMemoryHeap){
         .size = gtt_size,
         .flags = 0,
      };
   }

   if (visible_vram_size) {
      visible_vram_index = device->memory_properties.memoryHeapCount++;
      device->heaps |= RADV_HEAP_VRAM_VIS;
      device->memory_properties.memoryHeaps[visible_vram_index] = (VkMemoryHeap){
         .size = visible_vram_size,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      };
   }

   unsigned type_count = 0;

   if (vram_index >= 0 || visible_vram_index >= 0) {
      device->memory_domains[type_count] = RADEON_DOMAIN_VRAM;
      device->memory_flags[type_count] = RADEON_FLAG_NO_CPU_ACCESS;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
         .heapIndex = vram_index >= 0 ? vram_index : visible_vram_index,
      };

      device->memory_domains[type_count] = RADEON_DOMAIN_VRAM;
      device->memory_flags[type_count] = RADEON_FLAG_NO_CPU_ACCESS | RADEON_FLAG_32BIT;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
         .heapIndex = vram_index >= 0 ? vram_index : visible_vram_index,
      };
   }

   if (gart_index >= 0) {
      device->memory_domains[type_count] = RADEON_DOMAIN_GTT;
      device->memory_flags[type_count] = RADEON_FLAG_GTT_WC | RADEON_FLAG_CPU_ACCESS;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         .heapIndex = gart_index,
      };
   }
   if (visible_vram_index >= 0) {
      device->memory_domains[type_count] = RADEON_DOMAIN_VRAM;
      device->memory_flags[type_count] = RADEON_FLAG_CPU_ACCESS;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         .heapIndex = visible_vram_index,
      };
   }

   if (gart_index >= 0) {
      device->memory_domains[type_count] = RADEON_DOMAIN_GTT;
      device->memory_flags[type_count] = RADEON_FLAG_CPU_ACCESS;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
         .heapIndex = gart_index,
      };
   }
   device->memory_properties.memoryTypeCount = type_count;

   if (device->rad_info.has_l2_uncached) {
      for (int i = 0; i < device->memory_properties.memoryTypeCount; i++) {
         VkMemoryType mem_type = device->memory_properties.memoryTypes[i];

         if (((mem_type.propertyFlags &
               (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) ||
              mem_type.propertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
             !(device->memory_flags[i] & RADEON_FLAG_32BIT)) {

            VkMemoryPropertyFlags property_flags = mem_type.propertyFlags |
                                                   VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                                                   VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD;

            device->memory_domains[type_count] = device->memory_domains[i];
            device->memory_flags[type_count] = device->memory_flags[i] | RADEON_FLAG_VA_UNCACHED;
            device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
               .propertyFlags = property_flags,
               .heapIndex = mem_type.heapIndex,
            };
         }
      }
      device->memory_properties.memoryTypeCount = type_count;
   }

   for (unsigned i = 0; i < type_count; ++i) {
      if (device->memory_flags[i] & RADEON_FLAG_32BIT)
         device->memory_types_32bit |= BITFIELD_BIT(i);
   }
}

static const char *
radv_get_compiler_string(struct radv_physical_device *pdevice)
{
   if (!pdevice->use_llvm) {
      /* Some games like SotTR apply shader workarounds if the LLVM
       * version is too old or if the LLVM version string is
       * missing. This gives 2-5% performance with SotTR and ACO.
       */
      if (driQueryOptionb(&pdevice->instance->dri_options, "radv_report_llvm9_version_string")) {
         return " (LLVM 9.0.1)";
      }

      return "";
   }

#ifdef LLVM_AVAILABLE
   return " (LLVM " MESA_LLVM_VERSION_STRING ")";
#else
   unreachable("LLVM is not available");
#endif
}

int
radv_get_int_debug_option(const char *name, int default_value)
{
   const char *str;
   int result;

   str = getenv(name);
   if (!str) {
      result = default_value;
   } else {
      char *endptr;

      result = strtol(str, &endptr, 0);
      if (str == endptr) {
         /* No digits founs. */
         result = default_value;
      }
   }

   return result;
}

static bool
radv_thread_trace_enabled()
{
   return radv_get_int_debug_option("RADV_THREAD_TRACE", -1) >= 0 ||
          getenv("RADV_THREAD_TRACE_TRIGGER");
}

static bool
radv_spm_trace_enabled()
{
   return radv_thread_trace_enabled() &&
          debug_get_bool_option("RADV_THREAD_TRACE_CACHE_COUNTERS", false);
}

static bool
radv_perf_query_supported(const struct radv_physical_device *pdev)
{
   /* SQTT / SPM interfere with the register states for perf counters, and
    * the code has only been tested on GFX10.3 */
   return pdev->rad_info.gfx_level == GFX10_3 && !radv_thread_trace_enabled();
}

static bool
radv_taskmesh_enabled(const struct radv_physical_device *pdevice)
{
   return pdevice->use_ngg && !pdevice->use_llvm && pdevice->rad_info.gfx_level >= GFX10_3 &&
          !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE) &&
          pdevice->rad_info.has_scheduled_fence_dependency;
}

static bool
radv_NV_device_generated_commands_enabled(const struct radv_physical_device *device)
{
   return device->rad_info.gfx_level >= GFX7 &&
          !(device->instance->debug_flags & RADV_DEBUG_NO_IBS) &&
          driQueryOptionb(&device->instance->dri_options, "radv_dgc");
}

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR) ||                    \
   defined(VK_USE_PLATFORM_XLIB_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR)
#define RADV_USE_WSI_PLATFORM
#endif

#ifdef ANDROID
#define RADV_API_VERSION VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION)
#else
#define RADV_API_VERSION VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION)
#endif

VKAPI_ATTR VkResult VKAPI_CALL
radv_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   *pApiVersion = RADV_API_VERSION;
   return VK_SUCCESS;
}

static const struct vk_instance_extension_table radv_instance_extensions_supported = {
   .KHR_device_group_creation = true,
   .KHR_external_fence_capabilities = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
   .EXT_debug_report = true,
   .EXT_debug_utils = true,

#ifdef RADV_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2 = true,
   .KHR_surface = true,
   .KHR_surface_protected_capabilities = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   .EXT_acquire_xlib_display = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display = true,
   .KHR_get_display_properties2 = true,
   .EXT_direct_mode_display = true,
   .EXT_display_surface_counter = true,
   .EXT_acquire_drm_display = true,
#endif
};

static void
radv_physical_device_get_supported_extensions(const struct radv_physical_device *device,
                                              struct vk_device_extension_table *ext)
{
   *ext = (struct vk_device_extension_table){
      .KHR_8bit_storage = true,
      .KHR_16bit_storage = true,
      .KHR_acceleration_structure = radv_enable_rt(device, false),
      .KHR_bind_memory2 = true,
      .KHR_buffer_device_address = true,
      .KHR_copy_commands2 = true,
      .KHR_create_renderpass2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_deferred_host_operations = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_descriptor_update_template = true,
      .KHR_device_group = true,
      .KHR_draw_indirect_count = true,
      .KHR_driver_properties = true,
      .KHR_dynamic_rendering = true,
      .KHR_external_fence = true,
      .KHR_external_fence_fd = true,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = true,
      .KHR_external_semaphore_fd = true,
      .KHR_format_feature_flags2 = true,
      .KHR_fragment_shading_rate = device->rad_info.gfx_level >= GFX10_3,
      .KHR_get_memory_requirements2 = true,
      .KHR_global_priority = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
#ifdef RADV_USE_WSI_PLATFORM
      .KHR_incremental_present = true,
#endif
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_maintenance4 = true,
      .KHR_multiview = true,
      .KHR_performance_query = radv_perf_query_supported(device),
      .KHR_pipeline_executable_properties = true,
      .KHR_pipeline_library = !device->use_llvm,
      .KHR_push_descriptor = true,
      .KHR_ray_query = radv_enable_rt(device, false),
      .KHR_ray_tracing_maintenance1 = radv_enable_rt(device, false),
      .KHR_ray_tracing_pipeline = radv_enable_rt(device, true),
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_sampler_ycbcr_conversion = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_atomic_int64 = true,
      .KHR_shader_clock = true,
      .KHR_shader_draw_parameters = true,
      .KHR_shader_float16_int8 = true,
      .KHR_shader_float_controls = true,
      .KHR_shader_integer_dot_product = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_shader_subgroup_extended_types = true,
      .KHR_shader_subgroup_uniform_control_flow = true,
      .KHR_shader_terminate_invocation = true,
      .KHR_spirv_1_4 = true,
      .KHR_storage_buffer_storage_class = true,
#ifdef RADV_USE_WSI_PLATFORM
      .KHR_swapchain = true,
      .KHR_swapchain_mutable_format = true,
#endif
      .KHR_synchronization2 = true,
      .KHR_timeline_semaphore = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_variable_pointers = true,
      .KHR_vulkan_memory_model = true,
      .KHR_workgroup_memory_explicit_layout = true,
      .KHR_zero_initialize_workgroup_memory = true,
      .EXT_4444_formats = true,
      .EXT_attachment_feedback_loop_layout = true,
      .EXT_border_color_swizzle = device->rad_info.gfx_level >= GFX10,
      .EXT_buffer_device_address = true,
      .EXT_calibrated_timestamps = RADV_SUPPORT_CALIBRATED_TIMESTAMPS,
      .EXT_color_write_enable = true,
      .EXT_conditional_rendering = true,
      .EXT_conservative_rasterization = device->rad_info.gfx_level >= GFX9,
      .EXT_custom_border_color = true,
      .EXT_debug_marker = radv_thread_trace_enabled(),
      .EXT_depth_clip_control = true,
      .EXT_depth_clip_enable = true,
      .EXT_depth_range_unrestricted = true,
      .EXT_descriptor_indexing = true,
      .EXT_discard_rectangles = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
      .EXT_display_control = true,
#endif
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_external_memory_dma_buf = true,
      .EXT_external_memory_host = device->rad_info.has_userptr,
      .EXT_global_priority = true,
      .EXT_global_priority_query = true,
      .EXT_graphics_pipeline_library = !device->use_llvm &&
                                       !!(device->instance->perftest_flags & RADV_PERFTEST_GPL),
      .EXT_host_query_reset = true,
      .EXT_image_2d_view_of_3d = true,
      .EXT_image_drm_format_modifier = device->rad_info.gfx_level >= GFX9,
      .EXT_image_robustness = true,
      .EXT_image_view_min_lod = true,
      .EXT_index_type_uint8 = device->rad_info.gfx_level >= GFX8,
      .EXT_inline_uniform_block = true,
      .EXT_line_rasterization = true,
      .EXT_load_store_op_none = true,
      .EXT_memory_budget = true,
      .EXT_memory_priority = true,
      .EXT_mesh_shader =
         radv_taskmesh_enabled(device) && device->instance->perftest_flags & RADV_PERFTEST_EXT_MS,
      .EXT_multi_draw = true,
      .EXT_mutable_descriptor_type = true, /* Trivial promotion from VALVE. */
      .EXT_non_seamless_cube_map = true,
      .EXT_pci_bus_info = true,
#ifndef _WIN32
      .EXT_physical_device_drm = true,
#endif
      .EXT_pipeline_creation_cache_control = true,
      .EXT_pipeline_creation_feedback = true,
      .EXT_post_depth_coverage = device->rad_info.gfx_level >= GFX10,
      .EXT_primitive_topology_list_restart = true,
      .EXT_primitives_generated_query = true,
      .EXT_private_data = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_robustness2 = true,
      .EXT_sample_locations = device->rad_info.gfx_level < GFX10,
      .EXT_sampler_filter_minmax = true,
      .EXT_scalar_block_layout = device->rad_info.gfx_level >= GFX7,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_atomic_float = true,
#ifdef LLVM_AVAILABLE
      .EXT_shader_atomic_float2 = !device->use_llvm || LLVM_VERSION_MAJOR >= 14,
#else
      .EXT_shader_atomic_float2 = true,
#endif
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_image_atomic_int64 = true,
      .EXT_shader_module_identifier = true,
      .EXT_shader_stencil_export = true,
      .EXT_shader_subgroup_ballot = true,
      .EXT_shader_subgroup_vote = true,
      .EXT_shader_viewport_index_layer = true,
      .EXT_subgroup_size_control = true,
      .EXT_texel_buffer_alignment = true,
      .EXT_transform_feedback = device->rad_info.gfx_level < GFX11,
      .EXT_vertex_attribute_divisor = true,
      .EXT_vertex_input_dynamic_state = !device->use_llvm &&
                                        !radv_NV_device_generated_commands_enabled(device),
      .EXT_ycbcr_image_arrays = true,
      .AMD_buffer_marker = true,
      .AMD_device_coherent_memory = true,
      .AMD_draw_indirect_count = true,
      .AMD_gcn_shader = true,
      .AMD_gpu_shader_half_float = device->rad_info.has_packed_math_16bit,
      .AMD_gpu_shader_int16 = device->rad_info.has_packed_math_16bit,
      .AMD_memory_overallocation_behavior = true,
      .AMD_mixed_attachment_samples = true,
      .AMD_rasterization_order = device->rad_info.has_out_of_order_rast,
      .AMD_shader_ballot = true,
      .AMD_shader_core_properties = true,
      .AMD_shader_core_properties2 = true,
      .AMD_shader_explicit_vertex_parameter = true,
      .AMD_shader_fragment_mask = device->rad_info.gfx_level < GFX11,
      .AMD_shader_image_load_store_lod = true,
      .AMD_shader_trinary_minmax = true,
      .AMD_texture_gather_bias_lod = true,
#ifdef ANDROID
      .ANDROID_external_memory_android_hardware_buffer = RADV_SUPPORT_ANDROID_HARDWARE_BUFFER,
      .ANDROID_native_buffer = true,
#endif
      .GOOGLE_decorate_string = true,
      .GOOGLE_hlsl_functionality1 = true,
      .GOOGLE_user_type = true,
      .INTEL_shader_integer_functions2 = true,
      .NV_compute_shader_derivatives = true,
      .NV_device_generated_commands = radv_NV_device_generated_commands_enabled(device),
      .NV_mesh_shader =
         radv_taskmesh_enabled(device) && device->instance->perftest_flags & RADV_PERFTEST_NV_MS,
      /* Undocumented extension purely for vkd3d-proton. This check is to prevent anyone else from
       * using it.
       */
      .VALVE_descriptor_set_host_mapping =
         device->vk.instance->app_info.engine_name &&
         strcmp(device->vk.instance->app_info.engine_name, "vkd3d") == 0,
      .VALVE_mutable_descriptor_type = true,
   };
}

static bool
radv_is_conformant(const struct radv_physical_device *pdevice)
{
   return pdevice->rad_info.gfx_level >= GFX8;
}

static void
radv_physical_device_init_queue_table(struct radv_physical_device *pdevice)
{
   int idx = 0;
   pdevice->vk_queue_to_radv[idx] = RADV_QUEUE_GENERAL;
   idx++;

   for (unsigned i = 1; i < RADV_MAX_QUEUE_FAMILIES; i++)
      pdevice->vk_queue_to_radv[i] = RADV_MAX_QUEUE_FAMILIES + 1;

   if (pdevice->rad_info.ip[AMD_IP_COMPUTE].num_queues > 0 &&
       !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE)) {
      pdevice->vk_queue_to_radv[idx] = RADV_QUEUE_COMPUTE;
      idx++;
   }
   pdevice->num_queues = idx;
}

static void
radv_get_binning_settings(const struct radv_physical_device *pdevice,
                          struct radv_binning_settings *settings)
{
   if (pdevice->rad_info.has_dedicated_vram) {
      if (pdevice->rad_info.max_render_backends > 4) {
         settings->context_states_per_bin = 1;
         settings->persistent_states_per_bin = 1;
      } else {
         settings->context_states_per_bin = 3;
         settings->persistent_states_per_bin = 8;
      }
      settings->fpovs_per_batch = 63;
   } else {
      /* The context states are affected by the scissor bug. */
      settings->context_states_per_bin = 6;
      /* 32 causes hangs for RAVEN. */
      settings->persistent_states_per_bin = 16;
      settings->fpovs_per_batch = 63;
   }

   if (pdevice->rad_info.has_gfx9_scissor_bug)
      settings->context_states_per_bin = 1;
}

static VkResult
radv_physical_device_try_create(struct radv_instance *instance, drmDevicePtr drm_device,
                                struct radv_physical_device **device_out)
{
   VkResult result;
   int fd = -1;
   int master_fd = -1;

#ifdef _WIN32
   assert(drm_device == NULL);
#else
   if (drm_device) {
      const char *path = drm_device->nodes[DRM_NODE_RENDER];
      drmVersionPtr version;

      fd = open(path, O_RDWR | O_CLOEXEC);
      if (fd < 0) {
         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Could not open device %s: %m", path);
      }

      version = drmGetVersion(fd);
      if (!version) {
         close(fd);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Could not get the kernel driver version for device %s: %m", path);
      }

      if (strcmp(version->name, "amdgpu")) {
         drmFreeVersion(version);
         close(fd);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Device '%s' is not using the AMDGPU kernel driver: %m", path);
      }
      drmFreeVersion(version);

      if (instance->debug_flags & RADV_DEBUG_STARTUP)
         fprintf(stderr, "radv: info: Found compatible device '%s'.\n", path);
   }
#endif

   struct radv_physical_device *device = vk_zalloc2(&instance->vk.alloc, NULL, sizeof(*device), 8,
                                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_fd;
   }

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &radv_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk, NULL, &dispatch_table);
   if (result != VK_SUCCESS) {
      goto fail_alloc;
   }

   device->instance = instance;

#ifdef _WIN32
   device->ws = radv_null_winsys_create();
#else
   if (drm_device) {
      bool reserve_vmid = radv_thread_trace_enabled();

      device->ws = radv_amdgpu_winsys_create(fd, instance->debug_flags, instance->perftest_flags,
                                             reserve_vmid);
   } else {
      device->ws = radv_null_winsys_create();
   }
#endif

   if (!device->ws) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "failed to initialize winsys");
      goto fail_base;
   }

   device->vk.supported_sync_types = device->ws->get_sync_types(device->ws);

#ifndef _WIN32
   if (drm_device && instance->vk.enabled_extensions.KHR_display) {
      master_fd = open(drm_device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
      if (master_fd >= 0) {
         uint32_t accel_working = 0;
         struct drm_amdgpu_info request = {.return_pointer = (uintptr_t)&accel_working,
                                           .return_size = sizeof(accel_working),
                                           .query = AMDGPU_INFO_ACCEL_WORKING};

         if (drmCommandWrite(master_fd, DRM_AMDGPU_INFO, &request, sizeof(struct drm_amdgpu_info)) <
                0 ||
             !accel_working) {
            close(master_fd);
            master_fd = -1;
         }
      }
   }
#endif

   device->master_fd = master_fd;
   device->local_fd = fd;
   device->ws->query_info(device->ws, &device->rad_info);

   device->use_llvm = instance->debug_flags & RADV_DEBUG_LLVM;
#ifndef LLVM_AVAILABLE
   if (device->use_llvm) {
      fprintf(stderr, "ERROR: LLVM compiler backend selected for radv, but LLVM support was not "
                      "enabled at build time.\n");
      abort();
   }
#endif

#ifdef ANDROID
   device->emulate_etc2 = !radv_device_supports_etc(device);
#else
   device->emulate_etc2 = !radv_device_supports_etc(device) &&
                          driQueryOptionb(&device->instance->dri_options, "radv_require_etc2");
#endif

   snprintf(device->name, sizeof(device->name), "AMD RADV %s%s", device->rad_info.name,
            radv_get_compiler_string(device));

   const char *marketing_name = device->ws->get_chip_name(device->ws);
   snprintf(device->marketing_name, sizeof(device->name), "%s (RADV %s%s)",
            marketing_name, device->rad_info.name, radv_get_compiler_string(device));

#ifdef ENABLE_SHADER_CACHE
   if (radv_device_get_cache_uuid(device, device->cache_uuid)) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "cannot generate UUID");
      goto fail_wsi;
   }

   /* The gpu id is already embedded in the uuid so we just pass "radv"
    * when creating the cache.
    */
   char buf[VK_UUID_SIZE * 2 + 1];
   disk_cache_format_hex_id(buf, device->cache_uuid, VK_UUID_SIZE * 2);
   device->disk_cache = disk_cache_create(device->name, buf, 0);
#endif

   if (!radv_is_conformant(device))
      vk_warn_non_conformant_implementation("radv");

   radv_get_driver_uuid(&device->driver_uuid);
   radv_get_device_uuid(&device->rad_info, &device->device_uuid);

   device->out_of_order_rast_allowed =
      device->rad_info.has_out_of_order_rast &&
      !(device->instance->debug_flags & RADV_DEBUG_NO_OUT_OF_ORDER);

   device->dcc_msaa_allowed = (device->instance->perftest_flags & RADV_PERFTEST_DCC_MSAA);

   device->use_ngg = (device->rad_info.gfx_level >= GFX10 &&
                     device->rad_info.family != CHIP_NAVI14 &&
                     !(device->instance->debug_flags & RADV_DEBUG_NO_NGG)) ||
                     device->rad_info.gfx_level >= GFX11;

   device->use_ngg_culling = device->use_ngg && device->rad_info.max_render_backends > 1 &&
                             (device->rad_info.gfx_level >= GFX10_3 ||
                              (device->instance->perftest_flags & RADV_PERFTEST_NGGC)) &&
                             !(device->instance->debug_flags & RADV_DEBUG_NO_NGGC);

   device->use_ngg_streamout = false;

   /* Determine the number of threads per wave for all stages. */
   device->cs_wave_size = 64;
   device->ps_wave_size = 64;
   device->ge_wave_size = 64;
   device->rt_wave_size = 64;

   if (device->rad_info.gfx_level >= GFX10) {
      if (device->instance->perftest_flags & RADV_PERFTEST_CS_WAVE_32)
         device->cs_wave_size = 32;

      /* For pixel shaders, wave64 is recommanded. */
      if (device->instance->perftest_flags & RADV_PERFTEST_PS_WAVE_32)
         device->ps_wave_size = 32;

      if (device->instance->perftest_flags & RADV_PERFTEST_GE_WAVE_32)
         device->ge_wave_size = 32;

      if (!(device->instance->perftest_flags & RADV_PERFTEST_RT_WAVE_64))
         device->rt_wave_size = 32;
   }

   radv_physical_device_init_mem_types(device);

   radv_physical_device_get_supported_extensions(device, &device->vk.supported_extensions);

   radv_get_nir_options(device);

#ifndef _WIN32
   if (drm_device) {
      struct stat primary_stat = {0}, render_stat = {0};

      device->available_nodes = drm_device->available_nodes;
      device->bus_info = *drm_device->businfo.pci;

      if ((drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) &&
          stat(drm_device->nodes[DRM_NODE_PRIMARY], &primary_stat) != 0) {
         result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                            "failed to stat DRM primary node %s",
                            drm_device->nodes[DRM_NODE_PRIMARY]);
         goto fail_perfcounters;
      }
      device->primary_devid = primary_stat.st_rdev;

      if ((drm_device->available_nodes & (1 << DRM_NODE_RENDER)) &&
          stat(drm_device->nodes[DRM_NODE_RENDER], &render_stat) != 0) {
         result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                            "failed to stat DRM render node %s",
                            drm_device->nodes[DRM_NODE_RENDER]);
         goto fail_perfcounters;
      }
      device->render_devid = render_stat.st_rdev;
   }
#endif

   if ((device->instance->debug_flags & RADV_DEBUG_INFO))
      ac_print_gpu_info(&device->rad_info, stdout);

   radv_physical_device_init_queue_table(device);

   /* We don't check the error code, but later check if it is initialized. */
   ac_init_perfcounters(&device->rad_info, false, false, &device->ac_perfcounters);

   /* The WSI is structured as a layer on top of the driver, so this has
    * to be the last part of initialization (at least until we get other
    * semi-layers).
    */
   result = radv_init_wsi(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail_perfcounters;
   }

   device->gs_table_depth =
      ac_get_gs_table_depth(device->rad_info.gfx_level, device->rad_info.family);

   ac_get_hs_info(&device->rad_info, &device->hs);
   ac_get_task_info(&device->rad_info, &device->task_info);
   radv_get_binning_settings(device, &device->binning_settings);

   *device_out = device;

   return VK_SUCCESS;

fail_perfcounters:
   ac_destroy_perfcounters(&device->ac_perfcounters);
   disk_cache_destroy(device->disk_cache);
#ifdef ENABLE_SHADER_CACHE
fail_wsi:
#endif
   device->ws->destroy(device->ws);
fail_base:
   vk_physical_device_finish(&device->vk);
fail_alloc:
   vk_free(&instance->vk.alloc, device);
fail_fd:
   if (fd != -1)
      close(fd);
   if (master_fd != -1)
      close(master_fd);
   return result;
}

static void
radv_physical_device_destroy(struct vk_physical_device *vk_device)
{
   struct radv_physical_device *device = container_of(vk_device, struct radv_physical_device, vk);

   radv_finish_wsi(device);
   ac_destroy_perfcounters(&device->ac_perfcounters);
   device->ws->destroy(device->ws);
   disk_cache_destroy(device->disk_cache);
   if (device->local_fd != -1)
      close(device->local_fd);
   if (device->master_fd != -1)
      close(device->master_fd);
   vk_physical_device_finish(&device->vk);
   vk_free(&device->instance->vk.alloc, device);
}

static const struct debug_control radv_debug_options[] = {
   {"nofastclears", RADV_DEBUG_NO_FAST_CLEARS},
   {"nodcc", RADV_DEBUG_NO_DCC},
   {"shaders", RADV_DEBUG_DUMP_SHADERS},
   {"nocache", RADV_DEBUG_NO_CACHE},
   {"shaderstats", RADV_DEBUG_DUMP_SHADER_STATS},
   {"nohiz", RADV_DEBUG_NO_HIZ},
   {"nocompute", RADV_DEBUG_NO_COMPUTE_QUEUE},
   {"allbos", RADV_DEBUG_ALL_BOS},
   {"noibs", RADV_DEBUG_NO_IBS},
   {"spirv", RADV_DEBUG_DUMP_SPIRV},
   {"vmfaults", RADV_DEBUG_VM_FAULTS},
   {"zerovram", RADV_DEBUG_ZERO_VRAM},
   {"syncshaders", RADV_DEBUG_SYNC_SHADERS},
   {"preoptir", RADV_DEBUG_PREOPTIR},
   {"nodynamicbounds", RADV_DEBUG_NO_DYNAMIC_BOUNDS},
   {"nooutoforder", RADV_DEBUG_NO_OUT_OF_ORDER},
   {"info", RADV_DEBUG_INFO},
   {"startup", RADV_DEBUG_STARTUP},
   {"checkir", RADV_DEBUG_CHECKIR},
   {"nobinning", RADV_DEBUG_NOBINNING},
   {"nongg", RADV_DEBUG_NO_NGG},
   {"metashaders", RADV_DEBUG_DUMP_META_SHADERS},
   {"nomemorycache", RADV_DEBUG_NO_MEMORY_CACHE},
   {"discardtodemote", RADV_DEBUG_DISCARD_TO_DEMOTE},
   {"llvm", RADV_DEBUG_LLVM},
   {"forcecompress", RADV_DEBUG_FORCE_COMPRESS},
   {"hang", RADV_DEBUG_HANG},
   {"img", RADV_DEBUG_IMG},
   {"noumr", RADV_DEBUG_NO_UMR},
   {"invariantgeom", RADV_DEBUG_INVARIANT_GEOM},
   {"splitfma", RADV_DEBUG_SPLIT_FMA},
   {"nodisplaydcc", RADV_DEBUG_NO_DISPLAY_DCC},
   {"notccompatcmask", RADV_DEBUG_NO_TC_COMPAT_CMASK},
   {"novrsflatshading", RADV_DEBUG_NO_VRS_FLAT_SHADING},
   {"noatocdithering", RADV_DEBUG_NO_ATOC_DITHERING},
   {"nonggc", RADV_DEBUG_NO_NGGC},
   {"prologs", RADV_DEBUG_DUMP_PROLOGS},
   {"nodma", RADV_DEBUG_NO_DMA_BLIT},
   {"epilogs", RADV_DEBUG_DUMP_EPILOGS},
   {NULL, 0}};

const char *
radv_get_debug_option_name(int id)
{
   assert(id < ARRAY_SIZE(radv_debug_options) - 1);
   return radv_debug_options[id].string;
}

static const struct debug_control radv_perftest_options[] = {{"localbos", RADV_PERFTEST_LOCAL_BOS},
                                                             {"dccmsaa", RADV_PERFTEST_DCC_MSAA},
                                                             {"bolist", RADV_PERFTEST_BO_LIST},
                                                             {"cswave32", RADV_PERFTEST_CS_WAVE_32},
                                                             {"pswave32", RADV_PERFTEST_PS_WAVE_32},
                                                             {"gewave32", RADV_PERFTEST_GE_WAVE_32},
                                                             {"nosam", RADV_PERFTEST_NO_SAM},
                                                             {"sam", RADV_PERFTEST_SAM},
                                                             {"rt", RADV_PERFTEST_RT},
                                                             {"nggc", RADV_PERFTEST_NGGC},
                                                             {"emulate_rt", RADV_PERFTEST_EMULATE_RT},
                                                             {"nv_ms", RADV_PERFTEST_NV_MS},
                                                             {"rtwave64", RADV_PERFTEST_RT_WAVE_64},
                                                             {"gpl", RADV_PERFTEST_GPL},
                                                             {"ext_ms", RADV_PERFTEST_EXT_MS},
                                                             {NULL, 0}};

const char *
radv_get_perftest_option_name(int id)
{
   assert(id < ARRAY_SIZE(radv_perftest_options) - 1);
   return radv_perftest_options[id].string;
}

// clang-format off
static const driOptionDescription radv_dri_options[] = {
   DRI_CONF_SECTION_PERFORMANCE
      DRI_CONF_ADAPTIVE_SYNC(true)
      DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
      DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(false)
      DRI_CONF_VK_X11_ENSURE_MIN_IMAGE_COUNT(false)
      DRI_CONF_VK_XWAYLAND_WAIT_READY(true)
      DRI_CONF_RADV_REPORT_LLVM9_VERSION_STRING(false)
      DRI_CONF_RADV_ENABLE_MRT_OUTPUT_NAN_FIXUP(false)
      DRI_CONF_RADV_DISABLE_SHRINK_IMAGE_STORE(false)
      DRI_CONF_RADV_NO_DYNAMIC_BOUNDS(false)
      DRI_CONF_RADV_ABSOLUTE_DEPTH_BIAS(false)
      DRI_CONF_RADV_OVERRIDE_UNIFORM_OFFSET_ALIGNMENT(0)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_DEBUG
      DRI_CONF_OVERRIDE_VRAM_SIZE()
      DRI_CONF_VK_WSI_FORCE_BGRA8_UNORM_FIRST(false)
      DRI_CONF_RADV_ZERO_VRAM(false)
      DRI_CONF_RADV_LOWER_DISCARD_TO_DEMOTE(false)
      DRI_CONF_RADV_INVARIANT_GEOM(false)
      DRI_CONF_RADV_SPLIT_FMA(false)
      DRI_CONF_RADV_DISABLE_TC_COMPAT_HTILE_GENERAL(false)
      DRI_CONF_RADV_DISABLE_DCC(false)
      DRI_CONF_RADV_REQUIRE_ETC2(false)
      DRI_CONF_RADV_DISABLE_ANISO_SINGLE_LEVEL(false)
      DRI_CONF_RADV_DISABLE_SINKING_LOAD_INPUT_FS(false)
      DRI_CONF_RADV_DGC(false)
      DRI_CONF_RADV_FLUSH_BEFORE_QUERY_COPY(false)
   DRI_CONF_SECTION_END
};
// clang-format on

static void
radv_init_dri_options(struct radv_instance *instance)
{
   driParseOptionInfo(&instance->available_dri_options, radv_dri_options,
                      ARRAY_SIZE(radv_dri_options));
   driParseConfigFiles(&instance->dri_options, &instance->available_dri_options, 0, "radv", NULL, NULL,
                       instance->vk.app_info.app_name, instance->vk.app_info.app_version,
                       instance->vk.app_info.engine_name, instance->vk.app_info.engine_version);

   instance->enable_mrt_output_nan_fixup =
      driQueryOptionb(&instance->dri_options, "radv_enable_mrt_output_nan_fixup");

   instance->disable_shrink_image_store =
      driQueryOptionb(&instance->dri_options, "radv_disable_shrink_image_store");

   instance->absolute_depth_bias =
      driQueryOptionb(&instance->dri_options, "radv_absolute_depth_bias");

   instance->disable_tc_compat_htile_in_general =
      driQueryOptionb(&instance->dri_options, "radv_disable_tc_compat_htile_general");

   if (driQueryOptionb(&instance->dri_options, "radv_no_dynamic_bounds"))
      instance->debug_flags |= RADV_DEBUG_NO_DYNAMIC_BOUNDS;

   if (driQueryOptionb(&instance->dri_options, "radv_lower_discard_to_demote"))
      instance->debug_flags |= RADV_DEBUG_DISCARD_TO_DEMOTE;

   if (driQueryOptionb(&instance->dri_options, "radv_invariant_geom"))
      instance->debug_flags |= RADV_DEBUG_INVARIANT_GEOM;

   if (driQueryOptionb(&instance->dri_options, "radv_split_fma"))
      instance->debug_flags |= RADV_DEBUG_SPLIT_FMA;

   if (driQueryOptionb(&instance->dri_options, "radv_disable_dcc"))
      instance->debug_flags |= RADV_DEBUG_NO_DCC;

   instance->zero_vram =
      driQueryOptionb(&instance->dri_options, "radv_zero_vram");

   instance->disable_aniso_single_level =
      driQueryOptionb(&instance->dri_options, "radv_disable_aniso_single_level");

   instance->disable_sinking_load_input_fs =
      driQueryOptionb(&instance->dri_options, "radv_disable_sinking_load_input_fs");

   instance->flush_before_query_copy =
      driQueryOptionb(&instance->dri_options, "radv_flush_before_query_copy");
}

static VkResult create_null_physical_device(struct vk_instance *vk_instance);

static VkResult create_drm_physical_device(struct vk_instance *vk_instance,
                                           struct _drmDevice *device,
                                           struct vk_physical_device **out);

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
   struct radv_instance *instance;
   VkResult result;

   if (!pAllocator)
      pAllocator = vk_default_allocator();

   instance = vk_zalloc(pAllocator, sizeof(*instance), 8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table, &radv_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table, &wsi_instance_entrypoints, false);
   struct vk_instance_extension_table extensions_supported = radv_instance_extensions_supported;

   result = vk_instance_init(&instance->vk, &extensions_supported, &dispatch_table,
                             pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(instance, result);
   }

   instance->debug_flags = parse_debug_string(getenv("RADV_DEBUG"), radv_debug_options);
   instance->perftest_flags = parse_debug_string(getenv("RADV_PERFTEST"), radv_perftest_options);

   /* When RADV_FORCE_FAMILY is set, the driver creates a null
    * device that allows to test the compiler without having an
    * AMDGPU instance.
    */
   if (getenv("RADV_FORCE_FAMILY"))
      instance->vk.physical_devices.enumerate = create_null_physical_device;
   else
      instance->vk.physical_devices.try_create_for_drm = create_drm_physical_device;

   instance->vk.physical_devices.destroy = radv_physical_device_destroy;

   if (instance->debug_flags & RADV_DEBUG_STARTUP)
      fprintf(stderr, "radv: info: Created an instance.\n");

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   radv_init_dri_options(instance);

   *pInstance = radv_instance_to_handle(instance);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyInstance(VkInstance _instance, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_instance, instance, _instance);

   if (!instance)
      return;

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   driDestroyOptionCache(&instance->dri_options);
   driDestroyOptionInfo(&instance->available_dri_options);

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

static VkResult
create_null_physical_device(struct vk_instance *vk_instance)
{
   struct radv_instance *instance = container_of(vk_instance, struct radv_instance, vk);
   struct radv_physical_device *pdevice;

   VkResult result = radv_physical_device_try_create(instance, NULL, &pdevice);
   if (result != VK_SUCCESS)
      return result;

   list_addtail(&pdevice->vk.link, &instance->vk.physical_devices.list);
   return VK_SUCCESS;
}

static VkResult
create_drm_physical_device(struct vk_instance *vk_instance, struct _drmDevice *device,
                           struct vk_physical_device **out)
{
#ifndef _WIN32
   if (!(device->available_nodes & (1 << DRM_NODE_RENDER)) ||
       device->bustype != DRM_BUS_PCI ||
       device->deviceinfo.pci->vendor_id != ATI_VENDOR_ID)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   return radv_physical_device_try_create((struct radv_instance *)vk_instance, device,
                                          (struct radv_physical_device **)out);
#else
   return VK_SUCCESS;
#endif
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   memset(pFeatures, 0, sizeof(*pFeatures));

   *pFeatures = (VkPhysicalDeviceFeatures){
      .robustBufferAccess = true,
      .fullDrawIndexUint32 = true,
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = true,
      .tessellationShader = true,
      .sampleRateShading = true,
      .dualSrcBlend = true,
      .logicOp = true,
      .multiDrawIndirect = true,
      .drawIndirectFirstInstance = true,
      .depthClamp = true,
      .depthBiasClamp = true,
      .fillModeNonSolid = true,
      .depthBounds = true,
      .wideLines = true,
      .largePoints = true,
      .alphaToOne = false,
      .multiViewport = true,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = radv_device_supports_etc(pdevice) || pdevice->emulate_etc2,
      .textureCompressionASTC_LDR = false,
      .textureCompressionBC = true,
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = true,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = true,
      .shaderImageGatherExtended = true,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageMultisample = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      .shaderFloat64 = true,
      .shaderInt64 = true,
      .shaderInt16 = true,
      .sparseBinding = true,
      .sparseResidencyBuffer = pdevice->rad_info.family >= CHIP_POLARIS10,
      .sparseResidencyImage2D = pdevice->rad_info.family >= CHIP_POLARIS10,
      .sparseResidencyImage3D = pdevice->rad_info.gfx_level >= GFX9,
      .sparseResidencyAliased = pdevice->rad_info.family >= CHIP_POLARIS10,
      .variableMultisampleRate = true,
      .shaderResourceMinLod = true,
      .shaderResourceResidency = true,
      .inheritedQueries = true,
   };
}

static void
radv_get_physical_device_features_1_1(struct radv_physical_device *pdevice,
                                      VkPhysicalDeviceVulkan11Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);

   f->storageBuffer16BitAccess = true;
   f->uniformAndStorageBuffer16BitAccess = true;
   f->storagePushConstant16 = true;
   f->storageInputOutput16 = pdevice->rad_info.has_packed_math_16bit;
   f->multiview = true;
   f->multiviewGeometryShader = true;
   f->multiviewTessellationShader = true;
   f->variablePointersStorageBuffer = true;
   f->variablePointers = true;
   f->protectedMemory = false;
   f->samplerYcbcrConversion = true;
   f->shaderDrawParameters = true;
}

static void
radv_get_physical_device_features_1_2(struct radv_physical_device *pdevice,
                                      VkPhysicalDeviceVulkan12Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);

   f->samplerMirrorClampToEdge = true;
   f->drawIndirectCount = true;
   f->storageBuffer8BitAccess = true;
   f->uniformAndStorageBuffer8BitAccess = true;
   f->storagePushConstant8 = true;
   f->shaderBufferInt64Atomics = true;
   f->shaderSharedInt64Atomics = true;
   f->shaderFloat16 = pdevice->rad_info.has_packed_math_16bit;
   f->shaderInt8 = true;

   f->descriptorIndexing = true;
   f->shaderInputAttachmentArrayDynamicIndexing = true;
   f->shaderUniformTexelBufferArrayDynamicIndexing = true;
   f->shaderStorageTexelBufferArrayDynamicIndexing = true;
   f->shaderUniformBufferArrayNonUniformIndexing = true;
   f->shaderSampledImageArrayNonUniformIndexing = true;
   f->shaderStorageBufferArrayNonUniformIndexing = true;
   f->shaderStorageImageArrayNonUniformIndexing = true;
   f->shaderInputAttachmentArrayNonUniformIndexing = true;
   f->shaderUniformTexelBufferArrayNonUniformIndexing = true;
   f->shaderStorageTexelBufferArrayNonUniformIndexing = true;
   f->descriptorBindingUniformBufferUpdateAfterBind = true;
   f->descriptorBindingSampledImageUpdateAfterBind = true;
   f->descriptorBindingStorageImageUpdateAfterBind = true;
   f->descriptorBindingStorageBufferUpdateAfterBind = true;
   f->descriptorBindingUniformTexelBufferUpdateAfterBind = true;
   f->descriptorBindingStorageTexelBufferUpdateAfterBind = true;
   f->descriptorBindingUpdateUnusedWhilePending = true;
   f->descriptorBindingPartiallyBound = true;
   f->descriptorBindingVariableDescriptorCount = true;
   f->runtimeDescriptorArray = true;

   f->samplerFilterMinmax = true;
   f->scalarBlockLayout = pdevice->rad_info.gfx_level >= GFX7;
   f->imagelessFramebuffer = true;
   f->uniformBufferStandardLayout = true;
   f->shaderSubgroupExtendedTypes = true;
   f->separateDepthStencilLayouts = true;
   f->hostQueryReset = true;
   f->timelineSemaphore = true, f->bufferDeviceAddress = true;
   f->bufferDeviceAddressCaptureReplay = true;
   f->bufferDeviceAddressMultiDevice = false;
   f->vulkanMemoryModel = true;
   f->vulkanMemoryModelDeviceScope = true;
   f->vulkanMemoryModelAvailabilityVisibilityChains = false;
   f->shaderOutputViewportIndex = true;
   f->shaderOutputLayer = true;
   f->subgroupBroadcastDynamicId = true;
}

static void
radv_get_physical_device_features_1_3(struct radv_physical_device *pdevice,
                                      VkPhysicalDeviceVulkan13Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);

   f->robustImageAccess = true;
   f->inlineUniformBlock = true;
   f->descriptorBindingInlineUniformBlockUpdateAfterBind = true;
   f->pipelineCreationCacheControl = true;
   f->privateData = true;
   f->shaderDemoteToHelperInvocation = true;
   f->shaderTerminateInvocation = true;
   f->subgroupSizeControl = true;
   f->computeFullSubgroups = true;
   f->synchronization2 = true;
   f->textureCompressionASTC_HDR = false;
   f->shaderZeroInitializeWorkgroupMemory = true;
   f->dynamicRendering = true;
   f->shaderIntegerDotProduct = true;
   f->maintenance4 = true;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                VkPhysicalDeviceFeatures2 *pFeatures)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   radv_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

   VkPhysicalDeviceVulkan11Features core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
   };
   radv_get_physical_device_features_1_1(pdevice, &core_1_1);

   VkPhysicalDeviceVulkan12Features core_1_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
   radv_get_physical_device_features_1_2(pdevice, &core_1_2);

   VkPhysicalDeviceVulkan13Features core_1_3 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
   };
   radv_get_physical_device_features_1_3(pdevice, &core_1_3);

#define CORE_FEATURE(major, minor, feature) features->feature = core_##major##_##minor.feature

   vk_foreach_struct(ext, pFeatures->pNext)
   {
      if (vk_get_physical_device_core_1_1_feature_ext(ext, &core_1_1))
         continue;
      if (vk_get_physical_device_core_1_2_feature_ext(ext, &core_1_2))
         continue;
      if (vk_get_physical_device_core_1_3_feature_ext(ext, &core_1_3))
         continue;

      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT: {
         VkPhysicalDeviceConditionalRenderingFeaturesEXT *features =
            (VkPhysicalDeviceConditionalRenderingFeaturesEXT *)ext;
         features->conditionalRendering = true;
         features->inheritedConditionalRendering = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *features =
            (VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *)ext;
         features->vertexAttributeInstanceRateDivisor = true;
         features->vertexAttributeInstanceRateZeroDivisor = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: {
         VkPhysicalDeviceTransformFeedbackFeaturesEXT *features =
            (VkPhysicalDeviceTransformFeedbackFeaturesEXT *)ext;
         features->transformFeedback = pdevice->rad_info.gfx_level < GFX11;
         features->geometryStreams = !pdevice->use_ngg_streamout && pdevice->rad_info.gfx_level < GFX11;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES: {
         VkPhysicalDeviceScalarBlockLayoutFeatures *features =
            (VkPhysicalDeviceScalarBlockLayoutFeatures *)ext;
         CORE_FEATURE(1, 2, scalarBlockLayout);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT: {
         VkPhysicalDeviceMemoryPriorityFeaturesEXT *features =
            (VkPhysicalDeviceMemoryPriorityFeaturesEXT *)ext;
         features->memoryPriority = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT: {
         VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *features =
            (VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *)ext;
         CORE_FEATURE(1, 2, bufferDeviceAddress);
         CORE_FEATURE(1, 2, bufferDeviceAddressCaptureReplay);
         CORE_FEATURE(1, 2, bufferDeviceAddressMultiDevice);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: {
         VkPhysicalDeviceDepthClipEnableFeaturesEXT *features =
            (VkPhysicalDeviceDepthClipEnableFeaturesEXT *)ext;
         features->depthClipEnable = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV: {
         VkPhysicalDeviceComputeShaderDerivativesFeaturesNV *features =
            (VkPhysicalDeviceComputeShaderDerivativesFeaturesNV *)ext;
         features->computeDerivativeGroupQuads = false;
         features->computeDerivativeGroupLinear = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT: {
         VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *features =
            (VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *)ext;
         features->ycbcrImageArrays = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT: {
         VkPhysicalDeviceIndexTypeUint8FeaturesEXT *features =
            (VkPhysicalDeviceIndexTypeUint8FeaturesEXT *)ext;
         features->indexTypeUint8 = pdevice->rad_info.gfx_level >= GFX8;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: {
         VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *features =
            (VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *)ext;
         features->pipelineExecutableInfo = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR: {
         VkPhysicalDeviceShaderClockFeaturesKHR *features =
            (VkPhysicalDeviceShaderClockFeaturesKHR *)ext;
         features->shaderSubgroupClock = true;
         features->shaderDeviceClock = pdevice->rad_info.gfx_level >= GFX8;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: {
         VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *features =
            (VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *)ext;
         features->texelBufferAlignment = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD: {
         VkPhysicalDeviceCoherentMemoryFeaturesAMD *features =
            (VkPhysicalDeviceCoherentMemoryFeaturesAMD *)ext;
         features->deviceCoherentMemory = pdevice->rad_info.has_l2_uncached;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT: {
         VkPhysicalDeviceLineRasterizationFeaturesEXT *features =
            (VkPhysicalDeviceLineRasterizationFeaturesEXT *)ext;
         features->rectangularLines = false;
         features->bresenhamLines = true;
         features->smoothLines = false;
         features->stippledRectangularLines = false;
         /* FIXME: Some stippled Bresenham CTS fails on Vega10
          * but work on Raven.
          */
         features->stippledBresenhamLines = pdevice->rad_info.gfx_level != GFX9;
         features->stippledSmoothLines = false;
         break;
      }
      case VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD: {
         VkDeviceMemoryOverallocationCreateInfoAMD *features =
            (VkDeviceMemoryOverallocationCreateInfoAMD *)ext;
         features->overallocationBehavior = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT: {
         VkPhysicalDeviceRobustness2FeaturesEXT *features =
            (VkPhysicalDeviceRobustness2FeaturesEXT *)ext;
         features->robustBufferAccess2 = true;
         features->robustImageAccess2 = true;
         features->nullDescriptor = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
         VkPhysicalDeviceCustomBorderColorFeaturesEXT *features =
            (VkPhysicalDeviceCustomBorderColorFeaturesEXT *)ext;
         features->customBorderColors = true;
         features->customBorderColorWithoutFormat = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *features =
            (VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *)ext;
         features->extendedDynamicState = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT: {
         VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *features =
            (VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *)ext;
         features->shaderBufferFloat32Atomics = true;
         features->shaderBufferFloat32AtomicAdd = false;
         features->shaderBufferFloat64Atomics = true;
         features->shaderBufferFloat64AtomicAdd = false;
         features->shaderSharedFloat32Atomics = true;
         features->shaderSharedFloat32AtomicAdd = pdevice->rad_info.gfx_level >= GFX8;
         features->shaderSharedFloat64Atomics = true;
         features->shaderSharedFloat64AtomicAdd = false;
         features->shaderImageFloat32Atomics = true;
         features->shaderImageFloat32AtomicAdd = false;
         features->sparseImageFloat32Atomics = true;
         features->sparseImageFloat32AtomicAdd = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: {
         VkPhysicalDevice4444FormatsFeaturesEXT *features =
            (VkPhysicalDevice4444FormatsFeaturesEXT *)ext;
         features->formatA4R4G4B4 = true;
         features->formatA4B4G4R4 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT: {
         VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *features =
            (VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *)ext;
         features->shaderImageInt64Atomics = true;
         features->sparseImageInt64Atomics = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT: {
         VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *features =
            (VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *)ext;
         features->mutableDescriptorType = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR: {
         VkPhysicalDeviceFragmentShadingRateFeaturesKHR *features =
            (VkPhysicalDeviceFragmentShadingRateFeaturesKHR *)ext;
         features->pipelineFragmentShadingRate = true;
         features->primitiveFragmentShadingRate = true;
         features->attachmentFragmentShadingRate =
            !(pdevice->instance->debug_flags & RADV_DEBUG_NO_HIZ) &&
            pdevice->rad_info.gfx_level < GFX11; /* TODO: VRS no longer uses HTILE. */
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR: {
         VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *features =
            (VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *)ext;
         features->workgroupMemoryExplicitLayout = true;
         features->workgroupMemoryExplicitLayoutScalarBlockLayout = true;
         features->workgroupMemoryExplicitLayout8BitAccess = true;
         features->workgroupMemoryExplicitLayout16BitAccess = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT: {
         VkPhysicalDeviceProvokingVertexFeaturesEXT *features =
            (VkPhysicalDeviceProvokingVertexFeaturesEXT *)ext;
         features->provokingVertexLast = true;
         features->transformFeedbackPreservesProvokingVertex = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *features =
            (VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *)ext;
         features->extendedDynamicState2 = true;
         features->extendedDynamicState2LogicOp = true;
         features->extendedDynamicState2PatchControlPoints = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES_KHR: {
         VkPhysicalDeviceGlobalPriorityQueryFeaturesKHR *features =
            (VkPhysicalDeviceGlobalPriorityQueryFeaturesKHR *)ext;
         features->globalPriorityQuery = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR: {
         VkPhysicalDeviceAccelerationStructureFeaturesKHR *features =
            (VkPhysicalDeviceAccelerationStructureFeaturesKHR *)ext;
         features->accelerationStructure = true;
         features->accelerationStructureCaptureReplay = false;
         features->accelerationStructureIndirectBuild = false;
         features->accelerationStructureHostCommands = false;
         features->descriptorBindingAccelerationStructureUpdateAfterBind = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR: {
         VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *features =
            (VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *)ext;
         features->shaderSubgroupUniformControlFlow = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT: {
         VkPhysicalDeviceMultiDrawFeaturesEXT *features = (VkPhysicalDeviceMultiDrawFeaturesEXT *)ext;
         features->multiDraw = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
         VkPhysicalDeviceColorWriteEnableFeaturesEXT *features =
            (VkPhysicalDeviceColorWriteEnableFeaturesEXT *)ext;
         features->colorWriteEnable = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT: {
         VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *features =
            (VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *)ext;
         bool has_shader_buffer_float_minmax = radv_has_shader_buffer_float_minmax(pdevice);
         bool has_shader_image_float_minmax =
            pdevice->rad_info.gfx_level != GFX8 && pdevice->rad_info.gfx_level != GFX9;
         features->shaderBufferFloat16Atomics = false;
         features->shaderBufferFloat16AtomicAdd = false;
         features->shaderBufferFloat16AtomicMinMax = false;
         features->shaderBufferFloat32AtomicMinMax = has_shader_buffer_float_minmax;
         features->shaderBufferFloat64AtomicMinMax = has_shader_buffer_float_minmax;
         features->shaderSharedFloat16Atomics = false;
         features->shaderSharedFloat16AtomicAdd = false;
         features->shaderSharedFloat16AtomicMinMax = false;
         features->shaderSharedFloat32AtomicMinMax = true;
         features->shaderSharedFloat64AtomicMinMax = true;
         features->shaderImageFloat32AtomicMinMax = has_shader_image_float_minmax;
         features->sparseImageFloat32AtomicMinMax = has_shader_image_float_minmax;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT: {
         VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *features =
            (VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *)ext;
         features->primitiveTopologyListRestart = true;
         features->primitiveTopologyPatchListRestart = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR: {
         VkPhysicalDeviceRayQueryFeaturesKHR *features =
            (VkPhysicalDeviceRayQueryFeaturesKHR *)ext;
         features->rayQuery = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR: {
         VkPhysicalDeviceRayTracingPipelineFeaturesKHR *features =
            (VkPhysicalDeviceRayTracingPipelineFeaturesKHR *)ext;
         features->rayTracingPipeline = true;
         features->rayTracingPipelineShaderGroupHandleCaptureReplay = false;
         features->rayTracingPipelineShaderGroupHandleCaptureReplayMixed = false;
         features->rayTracingPipelineTraceRaysIndirect = true;
         features->rayTraversalPrimitiveCulling = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR: {
         VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *features =
            (VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *)ext;
         features->rayTracingMaintenance1 = true;
         features->rayTracingPipelineTraceRaysIndirect2 = radv_enable_rt(pdevice, true);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: {
         VkPhysicalDeviceMaintenance4Features *features =
            (VkPhysicalDeviceMaintenance4Features *)ext;
         features->maintenance4 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT: {
         VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *features =
            (VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *)ext;
         features->vertexInputDynamicState = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT: {
         VkPhysicalDeviceImageViewMinLodFeaturesEXT *features =
            (VkPhysicalDeviceImageViewMinLodFeaturesEXT *)ext;
         features->minLod = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES: {
         VkPhysicalDeviceSynchronization2Features *features =
            (VkPhysicalDeviceSynchronization2Features *)ext;
         features->synchronization2 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES: {
         VkPhysicalDeviceDynamicRenderingFeatures *features =
            (VkPhysicalDeviceDynamicRenderingFeatures *)ext;
         features->dynamicRendering = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV: {
         VkPhysicalDeviceMeshShaderFeaturesNV *features =
            (VkPhysicalDeviceMeshShaderFeaturesNV *)ext;
         features->taskShader = features->meshShader = radv_taskmesh_enabled(pdevice);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
         VkPhysicalDeviceMeshShaderFeaturesEXT *features =
            (VkPhysicalDeviceMeshShaderFeaturesEXT *)ext;
         bool taskmesh_en = radv_taskmesh_enabled(pdevice);
         features->meshShader = taskmesh_en;
         features->taskShader = taskmesh_en;
         features->multiviewMeshShader = taskmesh_en;
         features->primitiveFragmentShadingRateMeshShader = taskmesh_en;
         features->meshShaderQueries = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES: {
         VkPhysicalDeviceTextureCompressionASTCHDRFeatures *features =
            (VkPhysicalDeviceTextureCompressionASTCHDRFeatures *)ext;
         features->textureCompressionASTC_HDR = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_SET_HOST_MAPPING_FEATURES_VALVE: {
         VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *features =
            (VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *)ext;
         features->descriptorSetHostMapping = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT: {
         VkPhysicalDeviceDepthClipControlFeaturesEXT *features =
            (VkPhysicalDeviceDepthClipControlFeaturesEXT *)ext;
         features->depthClipControl = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT: {
         VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *features =
            (VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *)ext;
         features->image2DViewOf3D = true;
         features->sampler2DViewOf3D = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_FUNCTIONS_2_FEATURES_INTEL: {
         VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *features =
            (VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *)ext;
         features->shaderIntegerFunctions2 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT: {
         VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *features =
            (VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *)ext;
         features->primitivesGeneratedQuery = true;
         features->primitivesGeneratedQueryWithRasterizerDiscard = true;
         features->primitivesGeneratedQueryWithNonZeroStreams = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT : {
         VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *features =
            (VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *)ext;
         features->nonSeamlessCubeMap = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT: {
         VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *features =
            (VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *)ext;
         features->borderColorSwizzle = true;
         features->borderColorSwizzleFromImage = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT: {
         VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *features =
            (VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *)ext;
         features->shaderModuleIdentifier = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR: {
         VkPhysicalDevicePerformanceQueryFeaturesKHR *features =
            (VkPhysicalDevicePerformanceQueryFeaturesKHR *)ext;
         features->performanceCounterQueryPools = radv_perf_query_supported(pdevice);
         features->performanceCounterMultipleQueryPools = features->performanceCounterQueryPools;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV: {
         VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *features =
            (VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *)ext;
         features->deviceGeneratedCommands = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT: {
         VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *features =
            (VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *)ext;
         features->attachmentFeedbackLoopLayout = true;
	 break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT: {
         VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *features =
            (VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *)ext;
         features->graphicsPipelineLibrary = true;
         break;
      }
      default:
         break;
      }
   }
}

static size_t
radv_max_descriptor_set_size()
{
   /* make sure that the entire descriptor set is addressable with a signed
    * 32-bit int. So the sum of all limits scaled by descriptor size has to
    * be at most 2 GiB. the combined image & samples object count as one of
    * both. This limit is for the pipeline layout, not for the set layout, but
    * there is no set limit, so we just set a pipeline limit. I don't think
    * any app is going to hit this soon. */
   return ((1ull << 31) - 16 * MAX_DYNAMIC_BUFFERS -
           MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_INLINE_UNIFORM_BLOCK_COUNT) /
          (32 /* uniform buffer, 32 due to potential space wasted on alignment */ +
           32 /* storage buffer, 32 due to potential space wasted on alignment */ +
           32 /* sampler, largest when combined with image */ + 64 /* sampled image */ +
           64 /* storage image */);
}

static uint32_t
radv_uniform_buffer_offset_alignment(const struct radv_physical_device *pdevice)
{
   uint32_t uniform_offset_alignment =
      driQueryOptioni(&pdevice->instance->dri_options, "radv_override_uniform_offset_alignment");
   if (!util_is_power_of_two_or_zero(uniform_offset_alignment)) {
      fprintf(stderr,
              "ERROR: invalid radv_override_uniform_offset_alignment setting %d:"
              "not a power of two\n",
              uniform_offset_alignment);
      uniform_offset_alignment = 0;
   }

   /* Take at least the hardware limit. */
   return MAX2(uniform_offset_alignment, 4);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceProperties *pProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   VkSampleCountFlags sample_counts = 0xf;

   size_t max_descriptor_set_size = radv_max_descriptor_set_size();

   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D = (1 << 14),
      .maxImageDimension2D = (1 << 14),
      .maxImageDimension3D = (1 << 11),
      .maxImageDimensionCube = (1 << 14),
      .maxImageArrayLayers = (1 << 11),
      .maxTexelBufferElements = UINT32_MAX,
      .maxUniformBufferRange = UINT32_MAX,
      .maxStorageBufferRange = UINT32_MAX,
      .maxPushConstantsSize = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount = UINT32_MAX,
      .maxSamplerAllocationCount = 64 * 1024,
      .bufferImageGranularity = 1,
      .sparseAddressSpaceSize = RADV_MAX_MEMORY_ALLOCATION_SIZE, /* buffer max size */
      .maxBoundDescriptorSets = MAX_SETS,
      .maxPerStageDescriptorSamplers = max_descriptor_set_size,
      .maxPerStageDescriptorUniformBuffers = max_descriptor_set_size,
      .maxPerStageDescriptorStorageBuffers = max_descriptor_set_size,
      .maxPerStageDescriptorSampledImages = max_descriptor_set_size,
      .maxPerStageDescriptorStorageImages = max_descriptor_set_size,
      .maxPerStageDescriptorInputAttachments = max_descriptor_set_size,
      .maxPerStageResources = max_descriptor_set_size,
      .maxDescriptorSetSamplers = max_descriptor_set_size,
      .maxDescriptorSetUniformBuffers = max_descriptor_set_size,
      .maxDescriptorSetUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS,
      .maxDescriptorSetStorageBuffers = max_descriptor_set_size,
      .maxDescriptorSetStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS,
      .maxDescriptorSetSampledImages = max_descriptor_set_size,
      .maxDescriptorSetStorageImages = max_descriptor_set_size,
      .maxDescriptorSetInputAttachments = max_descriptor_set_size,
      .maxVertexInputAttributes = MAX_VERTEX_ATTRIBS,
      .maxVertexInputBindings = MAX_VBS,
      .maxVertexInputAttributeOffset = UINT32_MAX,
      .maxVertexInputBindingStride = 2048,
      .maxVertexOutputComponents = 128,
      .maxTessellationGenerationLevel = 64,
      .maxTessellationPatchSize = 32,
      .maxTessellationControlPerVertexInputComponents = 128,
      .maxTessellationControlPerVertexOutputComponents = 128,
      .maxTessellationControlPerPatchOutputComponents = 120,
      .maxTessellationControlTotalOutputComponents = 4096,
      .maxTessellationEvaluationInputComponents = 128,
      .maxTessellationEvaluationOutputComponents = 128,
      .maxGeometryShaderInvocations = 127,
      .maxGeometryInputComponents = 64,
      .maxGeometryOutputComponents = 128,
      .maxGeometryOutputVertices = 256,
      .maxGeometryTotalOutputComponents = 1024,
      .maxFragmentInputComponents = 128,
      .maxFragmentOutputAttachments = 8,
      .maxFragmentDualSrcAttachments = 1,
      .maxFragmentCombinedOutputResources = max_descriptor_set_size,
      .maxComputeSharedMemorySize = pdevice->rad_info.gfx_level >= GFX7 ? 65536 : 32768,
      .maxComputeWorkGroupCount = {65535, 65535, 65535},
      .maxComputeWorkGroupInvocations = 1024,
      .maxComputeWorkGroupSize = {1024, 1024, 1024},
      .subPixelPrecisionBits = 8,
      .subTexelPrecisionBits = 8,
      .mipmapPrecisionBits = 8,
      .maxDrawIndexedIndexValue = UINT32_MAX,
      .maxDrawIndirectCount = UINT32_MAX,
      .maxSamplerLodBias = 16,
      .maxSamplerAnisotropy = 16,
      .maxViewports = MAX_VIEWPORTS,
      .maxViewportDimensions = {(1 << 14), (1 << 14)},
      .viewportBoundsRange = {INT16_MIN, INT16_MAX},
      .viewportSubPixelBits = 8,
      .minMemoryMapAlignment = 4096, /* A page */
      .minTexelBufferOffsetAlignment = 4,
      .minUniformBufferOffsetAlignment = radv_uniform_buffer_offset_alignment(pdevice),
      .minStorageBufferOffsetAlignment = 4,
      .minTexelOffset = -32,
      .maxTexelOffset = 31,
      .minTexelGatherOffset = -32,
      .maxTexelGatherOffset = 31,
      .minInterpolationOffset = -2,
      .maxInterpolationOffset = 2,
      .subPixelInterpolationOffsetBits = 8,
      .maxFramebufferWidth = MAX_FRAMEBUFFER_WIDTH,
      .maxFramebufferHeight = MAX_FRAMEBUFFER_HEIGHT,
      .maxFramebufferLayers = (1 << 10),
      .framebufferColorSampleCounts = sample_counts,
      .framebufferDepthSampleCounts = sample_counts,
      .framebufferStencilSampleCounts = sample_counts,
      .framebufferNoAttachmentsSampleCounts = sample_counts,
      .maxColorAttachments = MAX_RTS,
      .sampledImageColorSampleCounts = sample_counts,
      .sampledImageIntegerSampleCounts = sample_counts,
      .sampledImageDepthSampleCounts = sample_counts,
      .sampledImageStencilSampleCounts = sample_counts,
      .storageImageSampleCounts = sample_counts,
      .maxSampleMaskWords = 1,
      .timestampComputeAndGraphics = true,
      .timestampPeriod = 1000000.0 / pdevice->rad_info.clock_crystal_freq,
      .maxClipDistances = 8,
      .maxCullDistances = 8,
      .maxCombinedClipAndCullDistances = 8,
      .discreteQueuePriorities = 2,
      .pointSizeRange = {0.0, 8191.875},
      .lineWidthRange = {0.0, 8191.875},
      .pointSizeGranularity = (1.0 / 8.0),
      .lineWidthGranularity = (1.0 / 8.0),
      .strictLines = false, /* FINISHME */
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = 1,
      .optimalBufferCopyRowPitchAlignment = 1,
      .nonCoherentAtomSize = 64,
   };

   VkPhysicalDeviceType device_type;

   if (pdevice->rad_info.has_dedicated_vram) {
      device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
   } else {
      device_type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
   }

   *pProperties = (VkPhysicalDeviceProperties){
      .apiVersion = RADV_API_VERSION,
      .driverVersion = vk_get_driver_version(),
      .vendorID = ATI_VENDOR_ID,
      .deviceID = pdevice->rad_info.pci_id,
      .deviceType = device_type,
      .limits = limits,
      .sparseProperties =
         {
            .residencyNonResidentStrict = pdevice->rad_info.family >= CHIP_POLARIS10,
            .residencyStandard2DBlockShape = pdevice->rad_info.family >= CHIP_POLARIS10,
            .residencyStandard3DBlockShape = pdevice->rad_info.gfx_level >= GFX9,
         },
   };

   strcpy(pProperties->deviceName, pdevice->marketing_name);
   memcpy(pProperties->pipelineCacheUUID, pdevice->cache_uuid, VK_UUID_SIZE);
}

static void
radv_get_physical_device_properties_1_1(struct radv_physical_device *pdevice,
                                        VkPhysicalDeviceVulkan11Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);

   memcpy(p->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
   memcpy(p->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
   memset(p->deviceLUID, 0, VK_LUID_SIZE);
   /* The LUID is for Windows. */
   p->deviceLUIDValid = false;
   p->deviceNodeMask = 0;

   p->subgroupSize = RADV_SUBGROUP_SIZE;
   p->subgroupSupportedStages = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
   if (radv_taskmesh_enabled(pdevice))
      p->subgroupSupportedStages |= VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

   if (radv_enable_rt(pdevice, true))
      p->subgroupSupportedStages |= RADV_RT_STAGE_BITS;
   p->subgroupSupportedOperations =
      VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
      VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
      VK_SUBGROUP_FEATURE_CLUSTERED_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT |
      VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT;
   p->subgroupQuadOperationsInAllStages = true;

   p->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
   p->maxMultiviewViewCount = MAX_VIEWS;
   p->maxMultiviewInstanceIndex = INT_MAX;
   p->protectedNoFault = false;
   p->maxPerSetDescriptors = RADV_MAX_PER_SET_DESCRIPTORS;
   p->maxMemoryAllocationSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
}

static void
radv_get_physical_device_properties_1_2(struct radv_physical_device *pdevice,
                                        VkPhysicalDeviceVulkan12Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);

   p->driverID = VK_DRIVER_ID_MESA_RADV;
   snprintf(p->driverName, VK_MAX_DRIVER_NAME_SIZE, "radv");
   snprintf(p->driverInfo, VK_MAX_DRIVER_INFO_SIZE, "Mesa " PACKAGE_VERSION MESA_GIT_SHA1 "%s",
            radv_get_compiler_string(pdevice));

   if (radv_is_conformant(pdevice)) {
      if (pdevice->rad_info.gfx_level >= GFX10_3) {
         p->conformanceVersion = (VkConformanceVersion){
            .major = 1,
            .minor = 3,
            .subminor = 0,
            .patch = 0,
         };
      } else {
         p->conformanceVersion = (VkConformanceVersion){
            .major = 1,
            .minor = 2,
            .subminor = 7,
            .patch = 1,
         };
      }
   } else {
      p->conformanceVersion = (VkConformanceVersion){
         .major = 0,
         .minor = 0,
         .subminor = 0,
         .patch = 0,
      };
   }

   /* On AMD hardware, denormals and rounding modes for fp16/fp64 are
    * controlled by the same config register.
    */
   if (pdevice->rad_info.has_packed_math_16bit) {
      p->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY;
      p->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY;
   } else {
      p->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
      p->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
   }

   /* With LLVM, do not allow both preserving and flushing denorms because
    * different shaders in the same pipeline can have different settings and
    * this won't work for merged shaders. To make it work, this requires LLVM
    * support for changing the register. The same logic applies for the
    * rounding modes because they are configured with the same config
    * register.
    */
   p->shaderDenormFlushToZeroFloat32 = true;
   p->shaderDenormPreserveFloat32 = !pdevice->use_llvm;
   p->shaderRoundingModeRTEFloat32 = true;
   p->shaderRoundingModeRTZFloat32 = !pdevice->use_llvm;
   p->shaderSignedZeroInfNanPreserveFloat32 = true;

   p->shaderDenormFlushToZeroFloat16 =
      pdevice->rad_info.has_packed_math_16bit && !pdevice->use_llvm;
   p->shaderDenormPreserveFloat16 = pdevice->rad_info.has_packed_math_16bit;
   p->shaderRoundingModeRTEFloat16 = pdevice->rad_info.has_packed_math_16bit;
   p->shaderRoundingModeRTZFloat16 = pdevice->rad_info.has_packed_math_16bit && !pdevice->use_llvm;
   p->shaderSignedZeroInfNanPreserveFloat16 = pdevice->rad_info.has_packed_math_16bit;

   p->shaderDenormFlushToZeroFloat64 = pdevice->rad_info.gfx_level >= GFX8 && !pdevice->use_llvm;
   p->shaderDenormPreserveFloat64 = pdevice->rad_info.gfx_level >= GFX8;
   p->shaderRoundingModeRTEFloat64 = pdevice->rad_info.gfx_level >= GFX8;
   p->shaderRoundingModeRTZFloat64 = pdevice->rad_info.gfx_level >= GFX8 && !pdevice->use_llvm;
   p->shaderSignedZeroInfNanPreserveFloat64 = pdevice->rad_info.gfx_level >= GFX8;

   p->maxUpdateAfterBindDescriptorsInAllPools = UINT32_MAX / 64;
   p->shaderUniformBufferArrayNonUniformIndexingNative = false;
   p->shaderSampledImageArrayNonUniformIndexingNative = false;
   p->shaderStorageBufferArrayNonUniformIndexingNative = false;
   p->shaderStorageImageArrayNonUniformIndexingNative = false;
   p->shaderInputAttachmentArrayNonUniformIndexingNative = false;
   p->robustBufferAccessUpdateAfterBind = true;
   p->quadDivergentImplicitLod = false;

   size_t max_descriptor_set_size =
      ((1ull << 31) - 16 * MAX_DYNAMIC_BUFFERS -
       MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_INLINE_UNIFORM_BLOCK_COUNT) /
      (32 /* uniform buffer, 32 due to potential space wasted on alignment */ +
       32 /* storage buffer, 32 due to potential space wasted on alignment */ +
       32 /* sampler, largest when combined with image */ + 64 /* sampled image */ +
       64 /* storage image */);
   p->maxPerStageDescriptorUpdateAfterBindSamplers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindUniformBuffers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindStorageBuffers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindSampledImages = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindStorageImages = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindInputAttachments = max_descriptor_set_size;
   p->maxPerStageUpdateAfterBindResources = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindSamplers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindUniformBuffers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS;
   p->maxDescriptorSetUpdateAfterBindStorageBuffers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS;
   p->maxDescriptorSetUpdateAfterBindSampledImages = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindStorageImages = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindInputAttachments = max_descriptor_set_size;

   /* We support all of the depth resolve modes */
   p->supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                   VK_RESOLVE_MODE_AVERAGE_BIT | VK_RESOLVE_MODE_MIN_BIT |
                                   VK_RESOLVE_MODE_MAX_BIT;

   /* Average doesn't make sense for stencil so we don't support that */
   p->supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                     VK_RESOLVE_MODE_MIN_BIT | VK_RESOLVE_MODE_MAX_BIT;

   p->independentResolveNone = true;
   p->independentResolve = true;

   /* GFX6-8 only support single channel min/max filter. */
   p->filterMinmaxImageComponentMapping = pdevice->rad_info.gfx_level >= GFX9;
   p->filterMinmaxSingleComponentFormats = true;

   p->maxTimelineSemaphoreValueDifference = UINT64_MAX;

   p->framebufferIntegerColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
}

static void
radv_get_physical_device_properties_1_3(struct radv_physical_device *pdevice,
                                        VkPhysicalDeviceVulkan13Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES);

   p->minSubgroupSize = 64;
   p->maxSubgroupSize = 64;
   p->maxComputeWorkgroupSubgroups = UINT32_MAX;
   p->requiredSubgroupSizeStages = 0;
   if (pdevice->rad_info.gfx_level >= GFX10) {
      /* Only GFX10+ supports wave32. */
      p->minSubgroupSize = 32;
      p->requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
   }

   p->maxInlineUniformBlockSize = MAX_INLINE_UNIFORM_BLOCK_SIZE;
   p->maxPerStageDescriptorInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_SETS;
   p->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_SETS;
   p->maxDescriptorSetInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_COUNT;
   p->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_COUNT;
   p->maxInlineUniformTotalSize = UINT16_MAX;

   bool accel = pdevice->rad_info.has_accelerated_dot_product;
   p->integerDotProduct8BitUnsignedAccelerated = accel;
   p->integerDotProduct8BitSignedAccelerated = accel;
   p->integerDotProduct8BitMixedSignednessAccelerated = false;
   p->integerDotProduct4x8BitPackedUnsignedAccelerated = accel;
   p->integerDotProduct4x8BitPackedSignedAccelerated = accel;
   p->integerDotProduct4x8BitPackedMixedSignednessAccelerated = false;
   p->integerDotProduct16BitUnsignedAccelerated = accel;
   p->integerDotProduct16BitSignedAccelerated = accel;
   p->integerDotProduct16BitMixedSignednessAccelerated = false;
   p->integerDotProduct32BitUnsignedAccelerated = false;
   p->integerDotProduct32BitSignedAccelerated = false;
   p->integerDotProduct32BitMixedSignednessAccelerated = false;
   p->integerDotProduct64BitUnsignedAccelerated = false;
   p->integerDotProduct64BitSignedAccelerated = false;
   p->integerDotProduct64BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating8BitSignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating16BitSignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = false;

   p->storageTexelBufferOffsetAlignmentBytes = 4;
   p->storageTexelBufferOffsetSingleTexelAlignment = true;
   p->uniformTexelBufferOffsetAlignmentBytes = 4;
   p->uniformTexelBufferOffsetSingleTexelAlignment = true;

   p->maxBufferSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceProperties2 *pProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   radv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   VkPhysicalDeviceVulkan11Properties core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
   };
   radv_get_physical_device_properties_1_1(pdevice, &core_1_1);

   VkPhysicalDeviceVulkan12Properties core_1_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
   };
   radv_get_physical_device_properties_1_2(pdevice, &core_1_2);

   VkPhysicalDeviceVulkan13Properties core_1_3 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES,
   };
   radv_get_physical_device_properties_1_3(pdevice, &core_1_3);

   vk_foreach_struct(ext, pProperties->pNext)
   {
      if (vk_get_physical_device_core_1_1_property_ext(ext, &core_1_1))
         continue;
      if (vk_get_physical_device_core_1_2_property_ext(ext, &core_1_2))
         continue;
      if (vk_get_physical_device_core_1_3_property_ext(ext, &core_1_3))
         continue;

      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *)ext;
         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISCARD_RECTANGLE_PROPERTIES_EXT: {
         VkPhysicalDeviceDiscardRectanglePropertiesEXT *properties =
            (VkPhysicalDeviceDiscardRectanglePropertiesEXT *)ext;
         properties->maxDiscardRectangles = MAX_DISCARD_RECTANGLES;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT: {
         VkPhysicalDeviceExternalMemoryHostPropertiesEXT *properties =
            (VkPhysicalDeviceExternalMemoryHostPropertiesEXT *)ext;
         properties->minImportedHostPointerAlignment = 4096;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD: {
         VkPhysicalDeviceShaderCorePropertiesAMD *properties =
            (VkPhysicalDeviceShaderCorePropertiesAMD *)ext;

         /* Shader engines. */
         properties->shaderEngineCount = pdevice->rad_info.max_se;
         properties->shaderArraysPerEngineCount = pdevice->rad_info.max_sa_per_se;
         properties->computeUnitsPerShaderArray = pdevice->rad_info.min_good_cu_per_sa;
         properties->simdPerComputeUnit = pdevice->rad_info.num_simd_per_compute_unit;
         properties->wavefrontsPerSimd = pdevice->rad_info.max_wave64_per_simd;
         properties->wavefrontSize = 64;

         /* SGPR. */
         properties->sgprsPerSimd = pdevice->rad_info.num_physical_sgprs_per_simd;
         properties->minSgprAllocation = pdevice->rad_info.min_sgpr_alloc;
         properties->maxSgprAllocation = pdevice->rad_info.max_sgpr_alloc;
         properties->sgprAllocationGranularity = pdevice->rad_info.sgpr_alloc_granularity;

         /* VGPR. */
         properties->vgprsPerSimd = pdevice->rad_info.num_physical_wave64_vgprs_per_simd;
         properties->minVgprAllocation = pdevice->rad_info.min_wave64_vgpr_alloc;
         properties->maxVgprAllocation = pdevice->rad_info.max_vgpr_alloc;
         properties->vgprAllocationGranularity = pdevice->rad_info.wave64_vgpr_alloc_granularity;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD: {
         VkPhysicalDeviceShaderCoreProperties2AMD *properties =
            (VkPhysicalDeviceShaderCoreProperties2AMD *)ext;

         properties->shaderCoreFeatures = 0;
         properties->activeComputeUnitCount = pdevice->rad_info.num_cu;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *properties =
            (VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)ext;
         properties->maxVertexAttribDivisor = UINT32_MAX;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceConservativeRasterizationPropertiesEXT *properties =
            (VkPhysicalDeviceConservativeRasterizationPropertiesEXT *)ext;
         properties->primitiveOverestimationSize = 0;
         properties->maxExtraPrimitiveOverestimationSize = 0;
         properties->extraPrimitiveOverestimationSizeGranularity = 0;
         properties->primitiveUnderestimation = false;
         properties->conservativePointAndLineRasterization = false;
         properties->degenerateTrianglesRasterized = true;
         properties->degenerateLinesRasterized = false;
         properties->fullyCoveredFragmentShaderInputVariable = false;
         properties->conservativeRasterizationPostDepthCoverage = false;
         break;
      }
#ifndef _WIN32
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT: {
         VkPhysicalDevicePCIBusInfoPropertiesEXT *properties =
            (VkPhysicalDevicePCIBusInfoPropertiesEXT *)ext;
         properties->pciDomain = pdevice->bus_info.domain;
         properties->pciBus = pdevice->bus_info.bus;
         properties->pciDevice = pdevice->bus_info.dev;
         properties->pciFunction = pdevice->bus_info.func;
         break;
      }
#endif
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: {
         VkPhysicalDeviceTransformFeedbackPropertiesEXT *properties =
            (VkPhysicalDeviceTransformFeedbackPropertiesEXT *)ext;
         properties->maxTransformFeedbackStreams = MAX_SO_STREAMS;
         properties->maxTransformFeedbackBuffers = MAX_SO_BUFFERS;
         properties->maxTransformFeedbackBufferSize = UINT32_MAX;
         properties->maxTransformFeedbackStreamDataSize = 512;
         properties->maxTransformFeedbackBufferDataSize = 512;
         properties->maxTransformFeedbackBufferDataStride = 512;
         properties->transformFeedbackQueries = !pdevice->use_ngg_streamout;
         properties->transformFeedbackStreamsLinesTriangles = !pdevice->use_ngg_streamout;
         properties->transformFeedbackRasterizationStreamSelect = false;
         properties->transformFeedbackDraw = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT: {
         VkPhysicalDeviceSampleLocationsPropertiesEXT *properties =
            (VkPhysicalDeviceSampleLocationsPropertiesEXT *)ext;
         properties->sampleLocationSampleCounts = VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
                                                  VK_SAMPLE_COUNT_8_BIT;
         properties->maxSampleLocationGridSize = (VkExtent2D){2, 2};
         properties->sampleLocationCoordinateRange[0] = 0.0f;
         properties->sampleLocationCoordinateRange[1] = 0.9375f;
         properties->sampleLocationSubPixelBits = 4;
         properties->variableSampleLocations = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceLineRasterizationPropertiesEXT *props =
            (VkPhysicalDeviceLineRasterizationPropertiesEXT *)ext;
         props->lineSubPixelPrecisionBits = 4;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT: {
         VkPhysicalDeviceRobustness2PropertiesEXT *properties =
            (VkPhysicalDeviceRobustness2PropertiesEXT *)ext;
         properties->robustStorageBufferAccessSizeAlignment = 4;
         properties->robustUniformBufferAccessSizeAlignment = 4;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT: {
         VkPhysicalDeviceCustomBorderColorPropertiesEXT *props =
            (VkPhysicalDeviceCustomBorderColorPropertiesEXT *)ext;
         props->maxCustomBorderColorSamplers = RADV_BORDER_COLOR_COUNT;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR: {
         VkPhysicalDeviceFragmentShadingRatePropertiesKHR *props =
            (VkPhysicalDeviceFragmentShadingRatePropertiesKHR *)ext;
         props->minFragmentShadingRateAttachmentTexelSize = (VkExtent2D){8, 8};
         props->maxFragmentShadingRateAttachmentTexelSize = (VkExtent2D){8, 8};
         props->maxFragmentShadingRateAttachmentTexelSizeAspectRatio = 1;
         props->primitiveFragmentShadingRateWithMultipleViewports = true;
         props->layeredShadingRateAttachments = false; /* TODO */
         props->fragmentShadingRateNonTrivialCombinerOps = true;
         props->maxFragmentSize = (VkExtent2D){2, 2};
         props->maxFragmentSizeAspectRatio = 2;
         props->maxFragmentShadingRateCoverageSamples = 32;
         props->maxFragmentShadingRateRasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
         props->fragmentShadingRateWithShaderDepthStencilWrites = false;
         props->fragmentShadingRateWithSampleMask = true;
         props->fragmentShadingRateWithShaderSampleMask = false;
         props->fragmentShadingRateWithConservativeRasterization = true;
         props->fragmentShadingRateWithFragmentShaderInterlock = false;
         props->fragmentShadingRateWithCustomSampleLocations = false;
         props->fragmentShadingRateStrictMultiplyCombiner = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT: {
         VkPhysicalDeviceProvokingVertexPropertiesEXT *props =
            (VkPhysicalDeviceProvokingVertexPropertiesEXT *)ext;
         props->provokingVertexModePerPipeline = true;
         props->transformFeedbackPreservesTriangleFanProvokingVertex = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR: {
         VkPhysicalDeviceAccelerationStructurePropertiesKHR *props =
            (VkPhysicalDeviceAccelerationStructurePropertiesKHR *)ext;
         props->maxGeometryCount = (1 << 24) - 1;
         props->maxInstanceCount = (1 << 24) - 1;
         props->maxPrimitiveCount = (1 << 29) - 1;
         props->maxPerStageDescriptorAccelerationStructures =
            pProperties->properties.limits.maxPerStageDescriptorStorageBuffers;
         props->maxPerStageDescriptorUpdateAfterBindAccelerationStructures =
            pProperties->properties.limits.maxPerStageDescriptorStorageBuffers;
         props->maxDescriptorSetAccelerationStructures =
            pProperties->properties.limits.maxDescriptorSetStorageBuffers;
         props->maxDescriptorSetUpdateAfterBindAccelerationStructures =
            pProperties->properties.limits.maxDescriptorSetStorageBuffers;
         props->minAccelerationStructureScratchOffsetAlignment = 128;
         break;
      }
#ifndef _WIN32
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT: {
         VkPhysicalDeviceDrmPropertiesEXT *props = (VkPhysicalDeviceDrmPropertiesEXT *)ext;
         if (pdevice->available_nodes & (1 << DRM_NODE_PRIMARY)) {
            props->hasPrimary = true;
            props->primaryMajor = (int64_t)major(pdevice->primary_devid);
            props->primaryMinor = (int64_t)minor(pdevice->primary_devid);
         } else {
            props->hasPrimary = false;
         }
         if (pdevice->available_nodes & (1 << DRM_NODE_RENDER)) {
            props->hasRender = true;
            props->renderMajor = (int64_t)major(pdevice->render_devid);
            props->renderMinor = (int64_t)minor(pdevice->render_devid);
         } else {
            props->hasRender = false;
         }
         break;
      }
#endif
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT: {
         VkPhysicalDeviceMultiDrawPropertiesEXT *props = (VkPhysicalDeviceMultiDrawPropertiesEXT *)ext;
         props->maxMultiDrawCount = 2048;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR: {
         VkPhysicalDeviceRayTracingPipelinePropertiesKHR *props =
            (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)ext;
         props->shaderGroupHandleSize = RADV_RT_HANDLE_SIZE;
         props->maxRayRecursionDepth = 31;    /* Minimum allowed for DXR. */
         props->maxShaderGroupStride = 16384; /* dummy */
         props->shaderGroupBaseAlignment = 16;
         props->shaderGroupHandleCaptureReplaySize = 16;
         props->maxRayDispatchInvocationCount = 1024 * 1024 * 64;
         props->shaderGroupHandleAlignment = 16;
         props->maxRayHitAttributeSize = RADV_MAX_HIT_ATTRIB_SIZE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES: {
         VkPhysicalDeviceMaintenance4Properties *properties =
            (VkPhysicalDeviceMaintenance4Properties *)ext;
         properties->maxBufferSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_NV: {
         VkPhysicalDeviceMeshShaderPropertiesNV *properties =
            (VkPhysicalDeviceMeshShaderPropertiesNV *)ext;

         /* Task shader limitations:
          * Same as compute, because TS are compiled to CS.
          */
         properties->maxDrawMeshTasksCount = 65535;
         properties->maxTaskTotalMemorySize = 65536;
         properties->maxTaskWorkGroupInvocations = 1024;
         properties->maxTaskWorkGroupSize[0] = 1024;
         properties->maxTaskWorkGroupSize[1] = 1024;
         properties->maxTaskWorkGroupSize[2] = 1024;
         properties->maxTaskOutputCount = 65535;

         /* Mesh shader limitations:
          * Same as NGG, because MS are compiled to NGG.
          */
         properties->maxMeshMultiviewViewCount = MAX_VIEWS;
         properties->maxMeshOutputPrimitives = 256;
         properties->maxMeshOutputVertices = 256;
         properties->maxMeshTotalMemorySize = 31 * 1024; /* Reserve 1K for prim indices, etc. */
         properties->maxMeshWorkGroupInvocations = 256;
         properties->maxMeshWorkGroupSize[0] = 256;
         properties->maxMeshWorkGroupSize[1] = 256;
         properties->maxMeshWorkGroupSize[2] = 256;
         properties->meshOutputPerPrimitiveGranularity = 1;
         properties->meshOutputPerVertexGranularity = 1;

         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_PROPERTIES_EXT: {
         VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *properties =
            (VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *)ext;
         STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
                       sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
         memcpy(properties->shaderModuleIdentifierAlgorithmUUID,
                vk_shaderModuleIdentifierAlgorithmUUID,
                sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_PROPERTIES_KHR: {
         VkPhysicalDevicePerformanceQueryPropertiesKHR *properties =
            (VkPhysicalDevicePerformanceQueryPropertiesKHR *)ext;
         properties->allowCommandBufferQueryCopies = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_NV: {
         VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *properties =
            (VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *)ext;
         properties->maxIndirectCommandsStreamCount = 1;
         properties->maxIndirectCommandsStreamStride = UINT32_MAX;
         properties->maxIndirectCommandsTokenCount = UINT32_MAX;
         properties->maxIndirectCommandsTokenOffset = UINT16_MAX;
         properties->minIndirectCommandsBufferOffsetAlignment = 4;
         properties->minSequencesCountBufferOffsetAlignment = 4;
         properties->minSequencesIndexBufferOffsetAlignment = 4;

         /* Don't support even a shader group count = 1 until we support shader
          * overrides during pipeline creation. */
         properties->maxGraphicsShaderGroupCount = 0;

         properties->maxIndirectSequenceCount = UINT32_MAX;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT: {
         VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *props =
            (VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *)ext;
         props->graphicsPipelineLibraryFastLinking = false;
         props->graphicsPipelineLibraryIndependentInterpolationDecoration = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
         VkPhysicalDeviceMeshShaderPropertiesEXT *properties =
            (VkPhysicalDeviceMeshShaderPropertiesEXT *)ext;

         properties->maxTaskWorkGroupTotalCount = 4194304; /* 2^22 min required */
         properties->maxTaskWorkGroupCount[0] = 65535;
         properties->maxTaskWorkGroupCount[1] = 65535;
         properties->maxTaskWorkGroupCount[2] = 65535;
         properties->maxTaskWorkGroupInvocations = 1024;
         properties->maxTaskWorkGroupSize[0] = 1024;
         properties->maxTaskWorkGroupSize[1] = 1024;
         properties->maxTaskWorkGroupSize[2] = 1024;
         properties->maxTaskPayloadSize = 16384; /* 16K min required */
         properties->maxTaskSharedMemorySize = 65536;
         properties->maxTaskPayloadAndSharedMemorySize = 65536;

         properties->maxMeshWorkGroupTotalCount = 4194304; /* 2^22 min required */
         properties->maxMeshWorkGroupCount[0] = 65535;
         properties->maxMeshWorkGroupCount[1] = 65535;
         properties->maxMeshWorkGroupCount[2] = 65535;
         properties->maxMeshWorkGroupInvocations = 256; /* Max NGG HW limit */
         properties->maxMeshWorkGroupSize[0] = 256;
         properties->maxMeshWorkGroupSize[1] = 256;
         properties->maxMeshWorkGroupSize[2] = 256;
         properties->maxMeshOutputMemorySize = 32 * 1024; /* 32K min required */
         properties->maxMeshSharedMemorySize = 28672;     /* 28K min required */
         properties->maxMeshPayloadAndSharedMemorySize =
            properties->maxTaskPayloadSize +
            properties->maxMeshSharedMemorySize; /* 28K min required */
         properties->maxMeshPayloadAndOutputMemorySize =
            properties->maxTaskPayloadSize +
            properties->maxMeshOutputMemorySize;    /* 47K min required */
         properties->maxMeshOutputComponents = 128; /* 32x vec4 min required */
         properties->maxMeshOutputVertices = 256;
         properties->maxMeshOutputPrimitives = 256;
         properties->maxMeshOutputLayers = 8;
         properties->maxMeshMultiviewViewCount = MAX_VIEWS;
         properties->meshOutputPerVertexGranularity = 1;
         properties->meshOutputPerPrimitiveGranularity = 1;

         properties->maxPreferredTaskWorkGroupInvocations = 1024;
         properties->maxPreferredMeshWorkGroupInvocations = 128;
         properties->prefersLocalInvocationVertexOutput = true;
         properties->prefersLocalInvocationPrimitiveOutput = true;
         properties->prefersCompactVertexOutput = true;
         properties->prefersCompactPrimitiveOutput = false;

         break;
      }
      default:
         break;
      }
   }
}

static void
radv_get_physical_device_queue_family_properties(struct radv_physical_device *pdevice,
                                                 uint32_t *pCount,
                                                 VkQueueFamilyProperties **pQueueFamilyProperties)
{
   int num_queue_families = 1;
   int idx;
   if (pdevice->rad_info.ip[AMD_IP_COMPUTE].num_queues > 0 &&
       !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE))
      num_queue_families++;

   if (pQueueFamilyProperties == NULL) {
      *pCount = num_queue_families;
      return;
   }

   if (!*pCount)
      return;

   idx = 0;
   if (*pCount >= 1) {
      *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
         .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT |
                       VK_QUEUE_SPARSE_BINDING_BIT,
         .queueCount = 1,
         .timestampValidBits = 64,
         .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
      };
      idx++;
   }

   if (pdevice->rad_info.ip[AMD_IP_COMPUTE].num_queues > 0 &&
       !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE)) {
      if (*pCount > idx) {
         *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
            .queueFlags =
               VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT,
            .queueCount = pdevice->rad_info.ip[AMD_IP_COMPUTE].num_queues,
            .timestampValidBits = 64,
            .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
         };
         idx++;
      }
   }
   *pCount = idx;
}

static const VkQueueGlobalPriorityKHR radv_global_queue_priorities[] = {
   VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR,
   VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
   VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR,
   VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR,
};

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                             VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   if (!pQueueFamilyProperties) {
      radv_get_physical_device_queue_family_properties(pdevice, pCount, NULL);
      return;
   }
   VkQueueFamilyProperties *properties[] = {
      &pQueueFamilyProperties[0].queueFamilyProperties,
      &pQueueFamilyProperties[1].queueFamilyProperties,
      &pQueueFamilyProperties[2].queueFamilyProperties,
   };
   radv_get_physical_device_queue_family_properties(pdevice, pCount, properties);
   assert(*pCount <= 3);

   for (uint32_t i = 0; i < *pCount; i++) {
      vk_foreach_struct(ext, pQueueFamilyProperties[i].pNext)
      {
         switch (ext->sType) {
         case VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR: {
            VkQueueFamilyGlobalPriorityPropertiesKHR *prop =
               (VkQueueFamilyGlobalPriorityPropertiesKHR *)ext;
            STATIC_ASSERT(ARRAY_SIZE(radv_global_queue_priorities) <= VK_MAX_GLOBAL_PRIORITY_SIZE_KHR);
            prop->priorityCount = ARRAY_SIZE(radv_global_queue_priorities);
            memcpy(&prop->priorities, radv_global_queue_priorities, sizeof(radv_global_queue_priorities));
            break;
         }
         default:
            break;
         }
      }
   }
}

static void
radv_get_memory_budget_properties(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceMemoryBudgetPropertiesEXT *memoryBudget)
{
   RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
   VkPhysicalDeviceMemoryProperties *memory_properties = &device->memory_properties;

   /* For all memory heaps, the computation of budget is as follow:
    *	heap_budget = heap_size - global_heap_usage + app_heap_usage
    *
    * The Vulkan spec 1.1.97 says that the budget should include any
    * currently allocated device memory.
    *
    * Note that the application heap usages are not really accurate (eg.
    * in presence of shared buffers).
    */
   if (!device->rad_info.has_dedicated_vram) {
      /* On APUs, the driver exposes fake heaps to the application because usually the carveout is
       * too small for games but the budgets need to be redistributed accordingly.
       */

      assert(device->heaps == (RADV_HEAP_GTT | RADV_HEAP_VRAM_VIS));
      assert(device->memory_properties.memoryHeaps[0].flags == 0); /* GTT */
      assert(device->memory_properties.memoryHeaps[1].flags == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
      uint8_t gtt_heap_idx = 0, vram_vis_heap_idx = 1;

      /* Get the visible VRAM/GTT heap sizes and internal usages. */
      uint64_t gtt_heap_size = device->memory_properties.memoryHeaps[gtt_heap_idx].size;
      uint64_t vram_vis_heap_size = device->memory_properties.memoryHeaps[vram_vis_heap_idx].size;

      uint64_t vram_vis_internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM_VIS) +
                                         device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM);
      uint64_t gtt_internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_GTT);

      /* Compute the total heap size, internal and system usage. */
      uint64_t total_heap_size = vram_vis_heap_size + gtt_heap_size;
      uint64_t total_internal_usage = vram_vis_internal_usage + gtt_internal_usage;
      uint64_t total_system_usage = device->ws->query_value(device->ws, RADEON_VRAM_VIS_USAGE) +
                                    device->ws->query_value(device->ws, RADEON_GTT_USAGE);

      uint64_t total_usage = MAX2(total_internal_usage, total_system_usage);

      /* Compute the total free space that can be allocated for this process accross all heaps. */
      uint64_t total_free_space = total_heap_size - MIN2(total_heap_size, total_usage);

      /* Compute the remaining visible VRAM size for this process. */
      uint64_t vram_vis_free_space = vram_vis_heap_size - MIN2(vram_vis_heap_size, vram_vis_internal_usage);

      /* Distribute the total free space (2/3rd as VRAM and 1/3rd as GTT) to match the heap sizes,
       * and align down to the page size to be conservative.
       */
      vram_vis_free_space = ROUND_DOWN_TO(MIN2((total_free_space * 2) / 3, vram_vis_free_space),
                                          device->rad_info.gart_page_size);
      uint64_t gtt_free_space = total_free_space - vram_vis_free_space;

      memoryBudget->heapBudget[vram_vis_heap_idx] = vram_vis_free_space + vram_vis_internal_usage;
      memoryBudget->heapUsage[vram_vis_heap_idx] = vram_vis_internal_usage;
      memoryBudget->heapBudget[gtt_heap_idx] = gtt_free_space + gtt_internal_usage;
      memoryBudget->heapUsage[gtt_heap_idx] = gtt_internal_usage;
   } else {
      unsigned mask = device->heaps;
      unsigned heap = 0;
      while (mask) {
         uint64_t internal_usage = 0, system_usage = 0;
         unsigned type = 1u << u_bit_scan(&mask);

         switch (type) {
         case RADV_HEAP_VRAM:
            internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM);
            system_usage = device->ws->query_value(device->ws, RADEON_VRAM_USAGE);
            break;
         case RADV_HEAP_VRAM_VIS:
            internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM_VIS);
            if (!(device->heaps & RADV_HEAP_VRAM))
               internal_usage += device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM);
            system_usage = device->ws->query_value(device->ws, RADEON_VRAM_VIS_USAGE);
            break;
         case RADV_HEAP_GTT:
            internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_GTT);
            system_usage = device->ws->query_value(device->ws, RADEON_GTT_USAGE);
            break;
         }

         uint64_t total_usage = MAX2(internal_usage, system_usage);

         uint64_t free_space = device->memory_properties.memoryHeaps[heap].size -
                               MIN2(device->memory_properties.memoryHeaps[heap].size, total_usage);
         memoryBudget->heapBudget[heap] = free_space + internal_usage;
         memoryBudget->heapUsage[heap] = internal_usage;
         ++heap;
      }

      assert(heap == memory_properties->memoryHeapCount);
   }

   /* The heapBudget and heapUsage values must be zero for array elements
    * greater than or equal to
    * VkPhysicalDeviceMemoryProperties::memoryHeapCount.
    */
   for (uint32_t i = memory_properties->memoryHeapCount; i < VK_MAX_MEMORY_HEAPS; i++) {
      memoryBudget->heapBudget[i] = 0;
      memoryBudget->heapUsage[i] = 0;
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                        VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);

   pMemoryProperties->memoryProperties = pdevice->memory_properties;

   VkPhysicalDeviceMemoryBudgetPropertiesEXT *memory_budget =
      vk_find_struct(pMemoryProperties->pNext, PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT);
   if (memory_budget)
      radv_get_memory_budget_properties(physicalDevice, memory_budget);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetMemoryHostPointerPropertiesEXT(
   VkDevice _device, VkExternalMemoryHandleTypeFlagBits handleType, const void *pHostPointer,
   VkMemoryHostPointerPropertiesEXT *pMemoryHostPointerProperties)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT: {
      const struct radv_physical_device *physical_device = device->physical_device;
      uint32_t memoryTypeBits = 0;
      for (int i = 0; i < physical_device->memory_properties.memoryTypeCount; i++) {
         if (physical_device->memory_domains[i] == RADEON_DOMAIN_GTT &&
             !(physical_device->memory_flags[i] & RADEON_FLAG_GTT_WC)) {
            memoryTypeBits = (1 << i);
            break;
         }
      }
      pMemoryHostPointerProperties->memoryTypeBits = memoryTypeBits;
      return VK_SUCCESS;
   }
   default:
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
}

static enum radeon_ctx_priority
radv_get_queue_global_priority(const VkDeviceQueueGlobalPriorityCreateInfoKHR *pObj)
{
   /* Default to MEDIUM when a specific global priority isn't requested */
   if (!pObj)
      return RADEON_CTX_PRIORITY_MEDIUM;

   switch (pObj->globalPriority) {
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR:
      return RADEON_CTX_PRIORITY_REALTIME;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:
      return RADEON_CTX_PRIORITY_HIGH;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:
      return RADEON_CTX_PRIORITY_MEDIUM;
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR:
      return RADEON_CTX_PRIORITY_LOW;
   default:
      unreachable("Illegal global priority value");
      return RADEON_CTX_PRIORITY_INVALID;
   }
}

int
radv_queue_init(struct radv_device *device, struct radv_queue *queue, int idx,
                const VkDeviceQueueCreateInfo *create_info,
                const VkDeviceQueueGlobalPriorityCreateInfoKHR *global_priority)
{
   queue->device = device;
   queue->priority = radv_get_queue_global_priority(global_priority);
   queue->hw_ctx = device->hw_ctx[queue->priority];
   queue->state.qf = vk_queue_to_radv(device->physical_device, create_info->queueFamilyIndex);

   VkResult result = vk_queue_init(&queue->vk, &device->vk, create_info, idx);
   if (result != VK_SUCCESS)
      return result;

   queue->vk.driver_submit = radv_queue_submit;

   return VK_SUCCESS;
}

static void
radv_queue_state_finish(struct radv_queue_state *queue, struct radeon_winsys *ws)
{
   if (queue->initial_full_flush_preamble_cs)
      ws->cs_destroy(queue->initial_full_flush_preamble_cs);
   if (queue->initial_preamble_cs)
      ws->cs_destroy(queue->initial_preamble_cs);
   if (queue->continue_preamble_cs)
      ws->cs_destroy(queue->continue_preamble_cs);
   if (queue->descriptor_bo)
      ws->buffer_destroy(ws, queue->descriptor_bo);
   if (queue->scratch_bo)
      ws->buffer_destroy(ws, queue->scratch_bo);
   if (queue->esgs_ring_bo)
      ws->buffer_destroy(ws, queue->esgs_ring_bo);
   if (queue->gsvs_ring_bo)
      ws->buffer_destroy(ws, queue->gsvs_ring_bo);
   if (queue->tess_rings_bo)
      ws->buffer_destroy(ws, queue->tess_rings_bo);
   if (queue->task_rings_bo)
      ws->buffer_destroy(ws, queue->task_rings_bo);
   if (queue->gds_bo)
      ws->buffer_destroy(ws, queue->gds_bo);
   if (queue->gds_oa_bo)
      ws->buffer_destroy(ws, queue->gds_oa_bo);
   if (queue->compute_scratch_bo)
      ws->buffer_destroy(ws, queue->compute_scratch_bo);
}

static void
radv_queue_finish(struct radv_queue *queue)
{
   if (queue->ace_internal_state) {
      /* Prevent double free */
      queue->ace_internal_state->task_rings_bo = NULL;

      /* Clean up the internal ACE queue state. */
      radv_queue_state_finish(queue->ace_internal_state, queue->device->ws);
      free(queue->ace_internal_state);
   }

   radv_queue_state_finish(&queue->state, queue->device->ws);
   vk_queue_finish(&queue->vk);
}

static bool
radv_queue_init_ace_internal_state(struct radv_queue *queue)
{
   if (queue->ace_internal_state)
      return true;

   queue->ace_internal_state = calloc(1, sizeof(struct radv_queue_state));
   if (!queue->ace_internal_state)
      return false;

   queue->ace_internal_state->qf = RADV_QUEUE_COMPUTE;
   return true;
}

static VkResult
radv_device_init_border_color(struct radv_device *device)
{
   VkResult result;

   result = device->ws->buffer_create(
      device->ws, RADV_BORDER_COLOR_BUFFER_SIZE, 4096, RADEON_DOMAIN_VRAM,
      RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_READ_ONLY | RADEON_FLAG_NO_INTERPROCESS_SHARING,
      RADV_BO_PRIORITY_SHADER, 0, &device->border_color_data.bo);

   if (result != VK_SUCCESS)
      return vk_error(device, result);

   result = device->ws->buffer_make_resident(device->ws, device->border_color_data.bo, true);
   if (result != VK_SUCCESS)
      return vk_error(device, result);

   device->border_color_data.colors_gpu_ptr = device->ws->buffer_map(device->border_color_data.bo);
   if (!device->border_color_data.colors_gpu_ptr)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   mtx_init(&device->border_color_data.mutex, mtx_plain);

   return VK_SUCCESS;
}

static void
radv_device_finish_border_color(struct radv_device *device)
{
   if (device->border_color_data.bo) {
      device->ws->buffer_make_resident(device->ws, device->border_color_data.bo, false);
      device->ws->buffer_destroy(device->ws, device->border_color_data.bo);

      mtx_destroy(&device->border_color_data.mutex);
   }
}

static VkResult
radv_device_init_vs_prologs(struct radv_device *device)
{
   u_rwlock_init(&device->vs_prologs_lock);
   device->vs_prologs = _mesa_hash_table_create(NULL, &radv_hash_vs_prolog, &radv_cmp_vs_prolog);
   if (!device->vs_prologs)
      return vk_error(device->physical_device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* don't pre-compile prologs if we want to print them */
   if (device->instance->debug_flags & RADV_DEBUG_DUMP_PROLOGS)
      return VK_SUCCESS;

   struct radv_vs_input_state state;
   state.nontrivial_divisors = 0;
   memset(state.offsets, 0, sizeof(state.offsets));
   state.alpha_adjust_lo = 0;
   state.alpha_adjust_hi = 0;
   memset(state.formats, 0, sizeof(state.formats));

   struct radv_vs_prolog_key key;
   key.state = &state;
   key.misaligned_mask = 0;
   key.as_ls = false;
   key.is_ngg = device->physical_device->use_ngg;
   key.next_stage = MESA_SHADER_VERTEX;
   key.wave32 = device->physical_device->ge_wave_size == 32;

   for (unsigned i = 1; i <= MAX_VERTEX_ATTRIBS; i++) {
      state.attribute_mask = BITFIELD_MASK(i);
      state.instance_rate_inputs = 0;

      key.num_attributes = i;

      device->simple_vs_prologs[i - 1] = radv_create_vs_prolog(device, &key);
      if (!device->simple_vs_prologs[i - 1])
         return vk_error(device->physical_device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   unsigned idx = 0;
   for (unsigned num_attributes = 1; num_attributes <= 16; num_attributes++) {
      state.attribute_mask = BITFIELD_MASK(num_attributes);

      for (unsigned i = 0; i < num_attributes; i++)
         state.divisors[i] = 1;

      for (unsigned count = 1; count <= num_attributes; count++) {
         for (unsigned start = 0; start <= (num_attributes - count); start++) {
            state.instance_rate_inputs = u_bit_consecutive(start, count);

            key.num_attributes = num_attributes;

            struct radv_shader_part *prolog = radv_create_vs_prolog(device, &key);
            if (!prolog)
               return vk_error(device->physical_device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);

            assert(idx ==
                   radv_instance_rate_prolog_index(num_attributes, state.instance_rate_inputs));
            device->instance_rate_vs_prologs[idx++] = prolog;
         }
      }
   }
   assert(idx == ARRAY_SIZE(device->instance_rate_vs_prologs));

   return VK_SUCCESS;
}

static void
radv_device_finish_vs_prologs(struct radv_device *device)
{
   if (device->vs_prologs) {
      hash_table_foreach(device->vs_prologs, entry)
      {
         free((void *)entry->key);
         radv_shader_part_unref(device, entry->data);
      }
      _mesa_hash_table_destroy(device->vs_prologs, NULL);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(device->simple_vs_prologs); i++) {
      if (!device->simple_vs_prologs[i])
         continue;

      radv_shader_part_unref(device, device->simple_vs_prologs[i]);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(device->instance_rate_vs_prologs); i++) {
      if (!device->instance_rate_vs_prologs[i])
         continue;

      radv_shader_part_unref(device, device->instance_rate_vs_prologs[i]);
   }
}

VkResult
radv_device_init_vrs_state(struct radv_device *device)
{
   /* FIXME: 4k depth buffers should be large enough for now but we might want to adjust this
    * dynamically at some point.
    */
   uint32_t width = 4096, height = 4096;
   VkDeviceMemory mem;
   VkBuffer buffer;
   VkResult result;
   VkImage image;

   VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_D16_UNORM,
      .extent = {width, height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = NULL,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   result = radv_CreateImage(radv_device_to_handle(device), &image_create_info,
                             &device->meta_state.alloc, &image);
   if (result != VK_SUCCESS)
      return result;

   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = radv_image_from_handle(image)->planes[0].surface.meta_size,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };

   result = radv_CreateBuffer(radv_device_to_handle(device), &buffer_create_info,
                              &device->meta_state.alloc, &buffer);
   if (result != VK_SUCCESS)
      goto fail_create;

   VkBufferMemoryRequirementsInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
      .buffer = buffer,
   };
   VkMemoryRequirements2 mem_req = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };
   radv_GetBufferMemoryRequirements2(radv_device_to_handle(device), &info, &mem_req);

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_req.memoryRequirements.size,
   };

   result = radv_AllocateMemory(radv_device_to_handle(device), &alloc_info,
                                &device->meta_state.alloc, &mem);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   VkBindBufferMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
      .buffer = buffer,
      .memory = mem,
      .memoryOffset = 0
   };

   result = radv_BindBufferMemory2(radv_device_to_handle(device), 1, &bind_info);
   if (result != VK_SUCCESS)
      goto fail_bind;

   device->vrs.image = radv_image_from_handle(image);
   device->vrs.buffer = radv_buffer_from_handle(buffer);
   device->vrs.mem = radv_device_memory_from_handle(mem);

   return VK_SUCCESS;

fail_bind:
   radv_FreeMemory(radv_device_to_handle(device), mem, &device->meta_state.alloc);
fail_alloc:
   radv_DestroyBuffer(radv_device_to_handle(device), buffer, &device->meta_state.alloc);
fail_create:
   radv_DestroyImage(radv_device_to_handle(device), image, &device->meta_state.alloc);

   return result;
}

static void
radv_device_finish_vrs_image(struct radv_device *device)
{
   if (!device->vrs.image)
      return;

   radv_FreeMemory(radv_device_to_handle(device), radv_device_memory_to_handle(device->vrs.mem),
                   &device->meta_state.alloc);
   radv_DestroyBuffer(radv_device_to_handle(device), radv_buffer_to_handle(device->vrs.buffer),
                     &device->meta_state.alloc);
   radv_DestroyImage(radv_device_to_handle(device), radv_image_to_handle(device->vrs.image),
                     &device->meta_state.alloc);
}

static enum radv_force_vrs
radv_parse_vrs_rates(const char *str)
{
   if (!strcmp(str, "2x2")) {
      return RADV_FORCE_VRS_2x2;
   } else if (!strcmp(str, "2x1")) {
      return RADV_FORCE_VRS_2x1;
   } else if (!strcmp(str, "1x2")) {
      return RADV_FORCE_VRS_1x2;
   } else if (!strcmp(str, "1x1")) {
      return RADV_FORCE_VRS_1x1;
   }

   fprintf(stderr, "radv: Invalid VRS rates specified (valid values are 2x2, 2x1, 1x2 and 1x1)\n");
   return RADV_FORCE_VRS_1x1;
}

static const char *
radv_get_force_vrs_config_file(void)
{
   return getenv("RADV_FORCE_VRS_CONFIG_FILE");
}

static enum radv_force_vrs
radv_parse_force_vrs_config_file(const char *config_file)
{
   enum radv_force_vrs force_vrs = RADV_FORCE_VRS_1x1;
   char buf[4];
   FILE *f;

   f = fopen(config_file, "r");
   if (!f) {
      fprintf(stderr, "radv: Can't open file: '%s'.\n", config_file);
      return force_vrs;
   }

   if (fread(buf, sizeof(buf), 1, f) == 1) {
      buf[3] = '\0';
      force_vrs = radv_parse_vrs_rates(buf);
   }

   fclose(f);
   return force_vrs;
}

#ifdef __linux__

#define BUF_LEN ((10 * (sizeof(struct inotify_event) + NAME_MAX + 1)))

static int
radv_notifier_thread_run(void *data)
{
   struct radv_device *device = data;
   struct radv_notifier *notifier = &device->notifier;
   char buf[BUF_LEN];

   while (!notifier->quit) {
      const char *file = radv_get_force_vrs_config_file();
      struct timespec tm = { .tv_nsec = 100000000 }; /* 1OOms */
      int length, i = 0;

      length = read(notifier->fd, buf, BUF_LEN);
      while (i < length) {
         struct inotify_event *event = (struct inotify_event *)&buf[i];

         i += sizeof(struct inotify_event) + event->len;
         if (event->mask & IN_MODIFY || event->mask & IN_DELETE_SELF) {
            /* Sleep 100ms for editors that use a temporary file and delete the original. */
            thrd_sleep(&tm, NULL);
            device->force_vrs = radv_parse_force_vrs_config_file(file);

            fprintf(stderr, "radv: Updated the per-vertex VRS rate to '%d'.\n", device->force_vrs);

            if (event->mask & IN_DELETE_SELF) {
               inotify_rm_watch(notifier->fd, notifier->watch);
               notifier->watch = inotify_add_watch(notifier->fd, file, IN_MODIFY | IN_DELETE_SELF);
            }
         }
      }

      thrd_sleep(&tm, NULL);
   }

   return 0;
}

#endif

static int
radv_device_init_notifier(struct radv_device *device)
{
#ifndef __linux__
   return true;
#else
   struct radv_notifier *notifier = &device->notifier;
   const char *file = radv_get_force_vrs_config_file();
   int ret;

   notifier->fd = inotify_init1(IN_NONBLOCK);
   if (notifier->fd < 0)
      return false;

   notifier->watch = inotify_add_watch(notifier->fd, file, IN_MODIFY | IN_DELETE_SELF);
   if (notifier->watch < 0)
      goto fail_watch;

   ret = thrd_create(&notifier->thread, radv_notifier_thread_run, device);
   if (ret)
      goto fail_thread;

   return true;

fail_thread:
   inotify_rm_watch(notifier->fd, notifier->watch);
fail_watch:
   close(notifier->fd);

   return false;
#endif
}

static void
radv_device_finish_notifier(struct radv_device *device)
{
#ifdef __linux__
   struct radv_notifier *notifier = &device->notifier;

   if (!notifier->thread)
      return;

   notifier->quit = true;
   thrd_join(notifier->thread, NULL);
   inotify_rm_watch(notifier->fd, notifier->watch);
   close(notifier->fd);
#endif
}

static void
radv_device_finish_perf_counter_lock_cs(struct radv_device *device)
{
   if (!device->perf_counter_lock_cs)
      return;

   for (unsigned i = 0; i < 2 * PERF_CTR_MAX_PASSES; ++i) {
      if (device->perf_counter_lock_cs[i])
         device->ws->cs_destroy(device->perf_counter_lock_cs[i]);
   }

   free(device->perf_counter_lock_cs);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);
   VkResult result;
   struct radv_device *device;

   bool keep_shader_info = false;
   bool robust_buffer_access = false;
   bool robust_buffer_access2 = false;
   bool overallocation_disallowed = false;
   bool custom_border_colors = false;
   bool attachment_vrs_enabled = false;
   bool image_float32_atomics = false;
   bool vs_prologs = false;
   bool global_bo_list = false;
   bool image_2d_view_of_3d = false;
   bool primitives_generated_query = false;
   bool use_perf_counters = false;
   bool use_dgc = false;

   /* Check enabled features */
   if (pCreateInfo->pEnabledFeatures) {
      if (pCreateInfo->pEnabledFeatures->robustBufferAccess)
         robust_buffer_access = true;
   }

   vk_foreach_struct_const(ext, pCreateInfo->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: {
         const VkPhysicalDeviceFeatures2 *features = (const void *)ext;
         if (features->features.robustBufferAccess)
            robust_buffer_access = true;
         break;
      }
      case VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD: {
         const VkDeviceMemoryOverallocationCreateInfoAMD *overallocation = (const void *)ext;
         if (overallocation->overallocationBehavior ==
             VK_MEMORY_OVERALLOCATION_BEHAVIOR_DISALLOWED_AMD)
            overallocation_disallowed = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
         const VkPhysicalDeviceCustomBorderColorFeaturesEXT *border_color_features =
            (const void *)ext;
         custom_border_colors = border_color_features->customBorderColors;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR: {
         const VkPhysicalDeviceFragmentShadingRateFeaturesKHR *vrs = (const void *)ext;
         attachment_vrs_enabled = vrs->attachmentFragmentShadingRate;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT: {
         const VkPhysicalDeviceRobustness2FeaturesEXT *features = (const void *)ext;
         if (features->robustBufferAccess2)
            robust_buffer_access2 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT: {
         const VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *features = (const void *)ext;
         if (features->shaderImageFloat32Atomics ||
             features->sparseImageFloat32Atomics)
            image_float32_atomics = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT: {
         const VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *features = (const void *)ext;
         if (features->shaderImageFloat32AtomicMinMax ||
             features->sparseImageFloat32AtomicMinMax)
            image_float32_atomics = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT: {
         const VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *features = (const void *)ext;
         if (features->vertexInputDynamicState)
            vs_prologs = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
         const VkPhysicalDeviceVulkan12Features *features = (const void *)ext;
         if (features->bufferDeviceAddress || features->descriptorIndexing)
            global_bo_list = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT: {
         const VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *features = (const void *)ext;
         if (features->image2DViewOf3D)
            image_2d_view_of_3d = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT: {
         const VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *features = (const void *)ext;
         if (features->primitivesGeneratedQuery ||
             features->primitivesGeneratedQueryWithRasterizerDiscard ||
             features->primitivesGeneratedQueryWithNonZeroStreams)
            primitives_generated_query = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR: {
         const VkPhysicalDevicePerformanceQueryFeaturesKHR *features = (const void *)ext;
         if (features->performanceCounterQueryPools)
            use_perf_counters = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV: {
         const VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *features = (const void *)ext;
         if (features->deviceGeneratedCommands)
            use_dgc = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT: {
         const VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *features = (const void *)ext;
         if (features->graphicsPipelineLibrary)
            vs_prologs = true;
         break;
      }
      default:
         break;
      }
   }

   device = vk_zalloc2(&physical_device->instance->vk.alloc, pAllocator, sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(physical_device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;

   if (physical_device->instance->vk.app_info.app_name &&
       !strcmp(physical_device->instance->vk.app_info.app_name, "metroexodus")) {
      /* Metro Exodus (Linux native) calls vkGetSemaphoreCounterValue() with a NULL semaphore and it
       * crashes sometimes.  Workaround this game bug by enabling an internal layer. Remove this
       * when the game is fixed.
       */
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &metro_exodus_device_entrypoints, true);
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &radv_device_entrypoints, false);
   } else if (radv_thread_trace_enabled()) {
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &sqtt_device_entrypoints, true);
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &radv_device_entrypoints, false);
   } else if (radv_rra_trace_enabled() && radv_enable_rt(physical_device, false)) {
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &rra_device_entrypoints, true);
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &radv_device_entrypoints, false);
   } else {
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &radv_device_entrypoints, true);
   }
   vk_device_dispatch_table_from_entrypoints(&dispatch_table, &wsi_device_entrypoints, false);

   result =
      vk_device_init(&device->vk, &physical_device->vk, &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device);
      return result;
   }

   device->vk.command_buffer_ops = &radv_cmd_buffer_ops;

   device->instance = physical_device->instance;
   device->physical_device = physical_device;
   simple_mtx_init(&device->trace_mtx, mtx_plain);
   simple_mtx_init(&device->pstate_mtx, mtx_plain);

   device->ws = physical_device->ws;
   vk_device_set_drm_fd(&device->vk, device->ws->get_fd(device->ws));

   /* With update after bind we can't attach bo's to the command buffer
    * from the descriptor set anymore, so we have to use a global BO list.
    */
   device->use_global_bo_list = global_bo_list ||
                                (device->instance->perftest_flags & RADV_PERFTEST_BO_LIST) ||
                                device->vk.enabled_extensions.EXT_descriptor_indexing ||
                                device->vk.enabled_extensions.EXT_buffer_device_address ||
                                device->vk.enabled_extensions.KHR_buffer_device_address ||
                                device->vk.enabled_extensions.KHR_ray_tracing_pipeline ||
                                device->vk.enabled_extensions.KHR_acceleration_structure ||
                                device->vk.enabled_extensions.VALVE_descriptor_set_host_mapping;

   device->robust_buffer_access = robust_buffer_access || robust_buffer_access2;
   device->robust_buffer_access2 = robust_buffer_access2;

   device->attachment_vrs_enabled = attachment_vrs_enabled;

   device->image_float32_atomics = image_float32_atomics;

   device->image_2d_view_of_3d = image_2d_view_of_3d;

   device->primitives_generated_query = primitives_generated_query;
   device->uses_device_generated_commands = use_dgc;

   radv_init_shader_arenas(device);

   device->overallocation_disallowed = overallocation_disallowed;
   mtx_init(&device->overallocation_mutex, mtx_plain);

   /* Create one context per queue priority. */
   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create = &pCreateInfo->pQueueCreateInfos[i];
      const VkDeviceQueueGlobalPriorityCreateInfoKHR *global_priority =
         vk_find_struct_const(queue_create->pNext, DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);
      enum radeon_ctx_priority priority = radv_get_queue_global_priority(global_priority);

      if (device->hw_ctx[priority])
         continue;

      result = device->ws->ctx_create(device->ws, priority, &device->hw_ctx[priority]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create = &pCreateInfo->pQueueCreateInfos[i];
      uint32_t qfi = queue_create->queueFamilyIndex;
      const VkDeviceQueueGlobalPriorityCreateInfoKHR *global_priority =
         vk_find_struct_const(queue_create->pNext, DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);

      device->queues[qfi] =
         vk_alloc(&device->vk.alloc, queue_create->queueCount * sizeof(struct radv_queue), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!device->queues[qfi]) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      memset(device->queues[qfi], 0, queue_create->queueCount * sizeof(struct radv_queue));

      device->queue_count[qfi] = queue_create->queueCount;

      for (unsigned q = 0; q < queue_create->queueCount; q++) {
         result = radv_queue_init(device, &device->queues[qfi][q], q, queue_create, global_priority);
         if (result != VK_SUCCESS)
            goto fail;
      }
   }
   device->private_sdma_queue = VK_NULL_HANDLE;

   device->pbb_allowed = device->physical_device->rad_info.gfx_level >= GFX9 &&
                         !(device->instance->debug_flags & RADV_DEBUG_NOBINNING);

   /* The maximum number of scratch waves. Scratch space isn't divided
    * evenly between CUs. The number is only a function of the number of CUs.
    * We can decrease the constant to decrease the scratch buffer size.
    *
    * sctx->scratch_waves must be >= the maximum possible size of
    * 1 threadgroup, so that the hw doesn't hang from being unable
    * to start any.
    *
    * The recommended value is 4 per CU at most. Higher numbers don't
    * bring much benefit, but they still occupy chip resources (think
    * async compute). I've seen ~2% performance difference between 4 and 32.
    */
   uint32_t max_threads_per_block = 2048;
   device->scratch_waves =
      MAX2(32 * physical_device->rad_info.num_cu, max_threads_per_block / 64);

   device->dispatch_initiator = S_00B800_COMPUTE_SHADER_EN(1);

   if (device->physical_device->rad_info.gfx_level >= GFX7) {
      /* If the KMD allows it (there is a KMD hw register for it),
       * allow launching waves out-of-order.
       */
      device->dispatch_initiator |= S_00B800_ORDER_MODE(1);
   }

   /* Disable partial preemption for task shaders.
    * The kernel may not support preemption, but PAL always sets this bit,
    * so let's also set it here for consistency.
    */
   device->dispatch_initiator_task =
      device->dispatch_initiator | S_00B800_DISABLE_DISP_PREMPT_EN(1);

   if (device->instance->debug_flags & RADV_DEBUG_HANG) {
      /* Enable GPU hangs detection and dump logs if a GPU hang is
       * detected.
       */
      keep_shader_info = true;

      if (!radv_init_trace(device))
         goto fail;

      fprintf(stderr,
              "*****************************************************************************\n");
      fprintf(stderr,
              "* WARNING: RADV_DEBUG=hang is costly and should only be used for debugging! *\n");
      fprintf(stderr,
              "*****************************************************************************\n");

      /* Wait for idle after every draw/dispatch to identify the
       * first bad call.
       */
      device->instance->debug_flags |= RADV_DEBUG_SYNC_SHADERS;

      radv_dump_enabled_options(device, stderr);
   }

   if (radv_thread_trace_enabled()) {
      if (device->physical_device->rad_info.gfx_level < GFX8 ||
          device->physical_device->rad_info.gfx_level > GFX10_3) {
         fprintf(stderr, "GPU hardware not supported: refer to "
                         "the RGP documentation for the list of "
                         "supported GPUs!\n");
         abort();
      }

      if (!radv_thread_trace_init(device))
         goto fail;

      fprintf(stderr, "radv: Thread trace support is enabled (initial buffer size: %u MiB, "
                      "instruction timing: %s, cache counters: %s).\n",
              device->thread_trace.buffer_size / (1024 * 1024),
              radv_is_instruction_timing_enabled() ? "enabled" : "disabled",
              radv_spm_trace_enabled() ? "enabled" : "disabled");

      if (radv_spm_trace_enabled()) {
         if (device->physical_device->rad_info.gfx_level >= GFX10) {
            if (!radv_spm_init(device))
               goto fail;
         } else {
            fprintf(stderr, "radv: SPM isn't supported for this GPU (%s)!\n",
                    device->physical_device->name);
         }
      }
   }

   if (getenv("RADV_TRAP_HANDLER")) {
      /* TODO: Add support for more hardware. */
      assert(device->physical_device->rad_info.gfx_level == GFX8);

      fprintf(stderr, "**********************************************************************\n");
      fprintf(stderr, "* WARNING: RADV_TRAP_HANDLER is experimental and only for debugging! *\n");
      fprintf(stderr, "**********************************************************************\n");

      /* To get the disassembly of the faulty shaders, we have to
       * keep some shader info around.
       */
      keep_shader_info = true;

      if (!radv_trap_handler_init(device))
         goto fail;
   }

   if (device->physical_device->rad_info.gfx_level >= GFX10_3) {
      if (getenv("RADV_FORCE_VRS_CONFIG_FILE")) {
         const char *file = radv_get_force_vrs_config_file();

         device->force_vrs = radv_parse_force_vrs_config_file(file);

         if (radv_device_init_notifier(device)) {
            device->force_vrs_enabled = true;
         } else {
            fprintf(stderr, "radv: Failed to initialize the notifier for RADV_FORCE_VRS_CONFIG_FILE!\n");
         }
      } else if (getenv("RADV_FORCE_VRS")) {
         const char *vrs_rates = getenv("RADV_FORCE_VRS");

         device->force_vrs = radv_parse_vrs_rates(vrs_rates);
         device->force_vrs_enabled = device->force_vrs != RADV_FORCE_VRS_1x1;
      }
   }

   /* PKT3_LOAD_SH_REG_INDEX is supported on GFX8+, but it hangs with compute queues until GFX10.3. */
   device->load_grid_size_from_user_sgpr = device->physical_device->rad_info.gfx_level >= GFX10_3;

   device->keep_shader_info = keep_shader_info;
   result = radv_device_init_meta(device);
   if (result != VK_SUCCESS)
      goto fail;

   radv_device_init_msaa(device);

   /* If the border color extension is enabled, let's create the buffer we need. */
   if (custom_border_colors) {
      result = radv_device_init_border_color(device);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (vs_prologs) {
      result = radv_device_init_vs_prologs(device);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (device->physical_device->rad_info.gfx_level >= GFX7)
      cik_create_gfx_config(device);

   VkPipelineCacheCreateInfo ci;
   ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
   ci.pNext = NULL;
   ci.flags = 0;
   ci.pInitialData = NULL;
   ci.initialDataSize = 0;
   VkPipelineCache pc;
   result = radv_CreatePipelineCache(radv_device_to_handle(device), &ci, NULL, &pc);
   if (result != VK_SUCCESS)
      goto fail_meta;

   device->mem_cache = radv_pipeline_cache_from_handle(pc);

   device->force_aniso = MIN2(16, radv_get_int_debug_option("RADV_TEX_ANISO", -1));
   if (device->force_aniso >= 0) {
      fprintf(stderr, "radv: Forcing anisotropy filter to %ix\n",
              1 << util_logbase2(device->force_aniso));
   }

   if (use_perf_counters) {
      size_t bo_size = PERF_CTR_BO_PASS_OFFSET + sizeof(uint64_t) * PERF_CTR_MAX_PASSES;
      result =
         device->ws->buffer_create(device->ws, bo_size, 4096, RADEON_DOMAIN_GTT,
                                   RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING,
                                   RADV_BO_PRIORITY_UPLOAD_BUFFER, 0, &device->perf_counter_bo);
      if (result != VK_SUCCESS)
         goto fail_cache;

      device->perf_counter_lock_cs =
         calloc(sizeof(struct radeon_winsys_cs *), 2 * PERF_CTR_MAX_PASSES);
      if (!device->perf_counter_lock_cs) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail_cache;
      }

      if (!device->physical_device->ac_perfcounters.blocks) {
         result = VK_ERROR_INITIALIZATION_FAILED;
         goto fail_cache;
      }
   }

   if (radv_rra_trace_enabled() && radv_enable_rt(physical_device, false)) {
      radv_rra_trace_init(device);
   }

   *pDevice = radv_device_to_handle(device);
   return VK_SUCCESS;

fail_cache:
   radv_DestroyPipelineCache(radv_device_to_handle(device), pc, NULL);
fail_meta:
   radv_device_finish_meta(device);
fail:
   radv_thread_trace_finish(device);

   radv_spm_finish(device);

   radv_trap_handler_finish(device);
   radv_finish_trace(device);

   radv_device_finish_perf_counter_lock_cs(device);
   if (device->perf_counter_bo)
      device->ws->buffer_destroy(device->ws, device->perf_counter_bo);
   if (device->gfx_init)
      device->ws->buffer_destroy(device->ws, device->gfx_init);

   radv_device_finish_notifier(device);
   radv_device_finish_vs_prologs(device);
   radv_device_finish_border_color(device);

   for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++)
         radv_queue_finish(&device->queues[i][q]);
      if (device->queue_count[i])
         vk_free(&device->vk.alloc, device->queues[i]);
   }

   for (unsigned i = 0; i < RADV_NUM_HW_CTX; i++) {
      if (device->hw_ctx[i])
         device->ws->ctx_destroy(device->hw_ctx[i]);
   }

   simple_mtx_destroy(&device->pstate_mtx);
   simple_mtx_destroy(&device->trace_mtx);
   mtx_destroy(&device->overallocation_mutex);

   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   if (!device)
      return;

   radv_device_finish_perf_counter_lock_cs(device);
   if (device->perf_counter_bo)
      device->ws->buffer_destroy(device->ws, device->perf_counter_bo);

   if (device->gfx_init)
      device->ws->buffer_destroy(device->ws, device->gfx_init);

   radv_device_finish_notifier(device);
   radv_device_finish_vs_prologs(device);
   radv_device_finish_border_color(device);
   radv_device_finish_vrs_image(device);

   for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++)
         radv_queue_finish(&device->queues[i][q]);
      if (device->queue_count[i])
         vk_free(&device->vk.alloc, device->queues[i]);
   }
   if (device->private_sdma_queue != VK_NULL_HANDLE) {
      radv_queue_finish(device->private_sdma_queue);
      vk_free(&device->vk.alloc, device->private_sdma_queue);
   }

   for (unsigned i = 0; i < RADV_NUM_HW_CTX; i++) {
      if (device->hw_ctx[i])
         device->ws->ctx_destroy(device->hw_ctx[i]);
   }

   mtx_destroy(&device->overallocation_mutex);
   simple_mtx_destroy(&device->pstate_mtx);
   simple_mtx_destroy(&device->trace_mtx);

   radv_device_finish_meta(device);

   VkPipelineCache pc = radv_pipeline_cache_to_handle(device->mem_cache);
   radv_DestroyPipelineCache(radv_device_to_handle(device), pc, NULL);

   radv_trap_handler_finish(device);
   radv_finish_trace(device);

   radv_destroy_shader_arenas(device);

   radv_thread_trace_finish(device);

   radv_rra_trace_finish(_device, &device->rra_trace);

   radv_spm_finish(device);

   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount, VkLayerProperties *pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                    VkLayerProperties *pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

static void
radv_fill_shader_rings(struct radv_device *device, uint32_t *map, bool add_sample_positions,
                       uint32_t esgs_ring_size, struct radeon_winsys_bo *esgs_ring_bo,
                       uint32_t gsvs_ring_size, struct radeon_winsys_bo *gsvs_ring_bo,
                       struct radeon_winsys_bo *tess_rings_bo,
                       struct radeon_winsys_bo *task_rings_bo,
                       struct radeon_winsys_bo *mesh_scratch_ring_bo)
{
   uint32_t *desc = &map[4];

   if (esgs_ring_bo) {
      uint64_t esgs_va = radv_buffer_get_va(esgs_ring_bo);

      /* stride 0, num records - size, add tid, swizzle, elsize4,
         index stride 64 */
      desc[0] = esgs_va;
      desc[1] = S_008F04_BASE_ADDRESS_HI(esgs_va >> 32);
      desc[2] = esgs_ring_size;
      desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
                S_008F0C_INDEX_STRIDE(3) | S_008F0C_ADD_TID_ENABLE(1);

      if (device->physical_device->rad_info.gfx_level >= GFX11)
         desc[1] |= S_008F04_SWIZZLE_ENABLE_GFX11(1);
      else
         desc[1] |= S_008F04_SWIZZLE_ENABLE_GFX6(1);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED);
      } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc[3] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) | S_008F0C_ELEMENT_SIZE(1);
      }

      /* GS entry for ES->GS ring */
      /* stride 0, num records - size, elsize0,
         index stride 0 */
      desc[4] = esgs_va;
      desc[5] = S_008F04_BASE_ADDRESS_HI(esgs_va >> 32);
      desc[6] = esgs_ring_size;
      desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED);
      } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc[7] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
      }
   }

   desc += 8;

   if (gsvs_ring_bo) {
      uint64_t gsvs_va = radv_buffer_get_va(gsvs_ring_bo);

      /* VS entry for GS->VS ring */
      /* stride 0, num records - size, elsize0,
         index stride 0 */
      desc[0] = gsvs_va;
      desc[1] = S_008F04_BASE_ADDRESS_HI(gsvs_va >> 32);
      desc[2] = gsvs_ring_size;
      desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED);
      } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc[3] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
      }

      /* stride gsvs_itemsize, num records 64
         elsize 4, index stride 16 */
      /* shader will patch stride and desc[2] */
      desc[4] = gsvs_va;
      desc[5] = S_008F04_BASE_ADDRESS_HI(gsvs_va >> 32);
      desc[6] = 0;
      desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
                S_008F0C_INDEX_STRIDE(1) | S_008F0C_ADD_TID_ENABLE(true);

      if (device->physical_device->rad_info.gfx_level >= GFX11)
         desc[5] |= S_008F04_SWIZZLE_ENABLE_GFX11(1);
      else
         desc[5] |= S_008F04_SWIZZLE_ENABLE_GFX6(1);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED);
      } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc[7] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) | S_008F0C_ELEMENT_SIZE(1);
      }
   }

   desc += 8;

   if (tess_rings_bo) {
      uint64_t tess_va = radv_buffer_get_va(tess_rings_bo);
      uint64_t tess_offchip_va = tess_va + device->physical_device->hs.tess_offchip_ring_offset;

      desc[0] = tess_va;
      desc[1] = S_008F04_BASE_ADDRESS_HI(tess_va >> 32);
      desc[2] = device->physical_device->hs.tess_factor_ring_size;
      desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW);
      } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc[3] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
      }

      desc[4] = tess_offchip_va;
      desc[5] = S_008F04_BASE_ADDRESS_HI(tess_offchip_va >> 32);
      desc[6] = device->physical_device->hs.tess_offchip_ring_size;
      desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW);
      } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc[7] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
      }
   }

   desc += 8;

   if (task_rings_bo) {
      uint64_t task_va = radv_buffer_get_va(task_rings_bo);
      uint64_t task_draw_ring_va = task_va + device->physical_device->task_info.draw_ring_offset;
      uint64_t task_payload_ring_va = task_va + device->physical_device->task_info.payload_ring_offset;

      desc[0] = task_draw_ring_va;
      desc[1] = S_008F04_BASE_ADDRESS_HI(task_draw_ring_va >> 32);
      desc[2] = device->physical_device->task_info.num_entries * AC_TASK_DRAW_ENTRY_BYTES;
      desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_UINT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED);
      } else {
         assert(device->physical_device->rad_info.gfx_level >= GFX10_3);
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_UINT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      }

      desc[4] = task_payload_ring_va;
      desc[5] = S_008F04_BASE_ADDRESS_HI(task_payload_ring_va >> 32);
      desc[6] = device->physical_device->task_info.num_entries * AC_TASK_PAYLOAD_ENTRY_BYTES;
      desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_UINT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED);
      } else {
         assert(device->physical_device->rad_info.gfx_level >= GFX10_3);
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_UINT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      }
   }

   desc += 8;

   if (mesh_scratch_ring_bo) {
      uint64_t va = radv_buffer_get_va(mesh_scratch_ring_bo);

      desc[0] = va;
      desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
      desc[2] = RADV_MESH_SCRATCH_NUM_ENTRIES * RADV_MESH_SCRATCH_ENTRY_BYTES;
      desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX11_FORMAT_32_UINT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED);
      } else {
         assert(device->physical_device->rad_info.gfx_level >= GFX10_3);
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_UINT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      }
   }

   desc += 4;

   if (add_sample_positions) {
      /* add sample positions after all rings */
      memcpy(desc, device->sample_locations_1x, 8);
      desc += 2;
      memcpy(desc, device->sample_locations_2x, 16);
      desc += 4;
      memcpy(desc, device->sample_locations_4x, 32);
      desc += 8;
      memcpy(desc, device->sample_locations_8x, 64);
   }
}

static void
radv_emit_gs_ring_sizes(struct radv_device *device, struct radeon_cmdbuf *cs,
                        struct radeon_winsys_bo *esgs_ring_bo, uint32_t esgs_ring_size,
                        struct radeon_winsys_bo *gsvs_ring_bo, uint32_t gsvs_ring_size)
{
   if (!esgs_ring_bo && !gsvs_ring_bo)
      return;

   if (esgs_ring_bo)
      radv_cs_add_buffer(device->ws, cs, esgs_ring_bo);

   if (gsvs_ring_bo)
      radv_cs_add_buffer(device->ws, cs, gsvs_ring_bo);

   if (device->physical_device->rad_info.gfx_level >= GFX7) {
      radeon_set_uconfig_reg_seq(cs, R_030900_VGT_ESGS_RING_SIZE, 2);
      radeon_emit(cs, esgs_ring_size >> 8);
      radeon_emit(cs, gsvs_ring_size >> 8);
   } else {
      radeon_set_config_reg_seq(cs, R_0088C8_VGT_ESGS_RING_SIZE, 2);
      radeon_emit(cs, esgs_ring_size >> 8);
      radeon_emit(cs, gsvs_ring_size >> 8);
   }
}

static void
radv_emit_tess_factor_ring(struct radv_device *device, struct radeon_cmdbuf *cs,
                           struct radeon_winsys_bo *tess_rings_bo)
{
   uint64_t tf_va;
   uint32_t tf_ring_size;
   if (!tess_rings_bo)
      return;

   tf_ring_size = device->physical_device->hs.tess_factor_ring_size / 4;
   tf_va = radv_buffer_get_va(tess_rings_bo);

   radv_cs_add_buffer(device->ws, cs, tess_rings_bo);

   if (device->physical_device->rad_info.gfx_level >= GFX7) {
      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         /* TF_RING_SIZE is per SE on GFX11. */
         tf_ring_size /= device->physical_device->rad_info.max_se;
      }

      radeon_set_uconfig_reg(cs, R_030938_VGT_TF_RING_SIZE, S_030938_SIZE(tf_ring_size));
      radeon_set_uconfig_reg(cs, R_030940_VGT_TF_MEMORY_BASE, tf_va >> 8);

      if (device->physical_device->rad_info.gfx_level >= GFX10) {
         radeon_set_uconfig_reg(cs, R_030984_VGT_TF_MEMORY_BASE_HI,
                                S_030984_BASE_HI(tf_va >> 40));
      } else if (device->physical_device->rad_info.gfx_level == GFX9) {
         radeon_set_uconfig_reg(cs, R_030944_VGT_TF_MEMORY_BASE_HI, S_030944_BASE_HI(tf_va >> 40));
      }

      radeon_set_uconfig_reg(cs, R_03093C_VGT_HS_OFFCHIP_PARAM, device->physical_device->hs.hs_offchip_param);
   } else {
      radeon_set_config_reg(cs, R_008988_VGT_TF_RING_SIZE, S_008988_SIZE(tf_ring_size));
      radeon_set_config_reg(cs, R_0089B8_VGT_TF_MEMORY_BASE, tf_va >> 8);
      radeon_set_config_reg(cs, R_0089B0_VGT_HS_OFFCHIP_PARAM, device->physical_device->hs.hs_offchip_param);
   }
}

static VkResult
radv_initialise_task_control_buffer(struct radv_device *device,
                                    struct radeon_winsys_bo *task_rings_bo)
{
   uint32_t *ptr = (uint32_t *)device->ws->buffer_map(task_rings_bo);
   if (!ptr)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   const uint32_t num_entries = device->physical_device->task_info.num_entries;
   const uint64_t task_va = radv_buffer_get_va(task_rings_bo);
   const uint64_t task_draw_ring_va = task_va + device->physical_device->task_info.draw_ring_offset;
   assert((task_draw_ring_va & 0xFFFFFF00) == (task_draw_ring_va & 0xFFFFFFFF));

   /* 64-bit write_ptr */
   ptr[0] = num_entries;
   ptr[1] = 0;
   /* 64-bit read_ptr */
   ptr[2] = num_entries;
   ptr[3] = 0;
   /* 64-bit dealloc_ptr */
   ptr[4] = num_entries;
   ptr[5] = 0;
   /* num_entries */
   ptr[6] = num_entries;
   /* 64-bit draw ring address */
   ptr[7] = task_draw_ring_va;
   ptr[8] = task_draw_ring_va >> 32;

   device->ws->buffer_unmap(task_rings_bo);
   return VK_SUCCESS;
}

static void
radv_emit_task_rings(struct radv_device *device, struct radeon_cmdbuf *cs,
                     struct radeon_winsys_bo *task_rings_bo, bool compute)
{
   if (!task_rings_bo)
      return;

   const uint64_t task_ctrlbuf_va = radv_buffer_get_va(task_rings_bo);
   assert(radv_is_aligned(task_ctrlbuf_va, 256));
   radv_cs_add_buffer(device->ws, cs, task_rings_bo);

   /* Tell the GPU where the task control buffer is. */
   radeon_emit(cs, PKT3(PKT3_DISPATCH_TASK_STATE_INIT, 1, 0) | PKT3_SHADER_TYPE_S(!!compute));
   /* bits [31:8]: control buffer address lo, bits[7:0]: reserved (set to zero) */
   radeon_emit(cs, task_ctrlbuf_va & 0xFFFFFF00);
   /* bits [31:0]: control buffer address hi */
   radeon_emit(cs, task_ctrlbuf_va >> 32);
}

static void
radv_emit_graphics_scratch(struct radv_device *device, struct radeon_cmdbuf *cs,
                           uint32_t size_per_wave, uint32_t waves,
                           struct radeon_winsys_bo *scratch_bo)
{
   struct radeon_info *info = &device->physical_device->rad_info;

   if (!scratch_bo)
      return;

   radv_cs_add_buffer(device->ws, cs, scratch_bo);

   if (info->gfx_level >= GFX11) {
      uint64_t va = radv_buffer_get_va(scratch_bo);

      /* WAVES is per SE for SPI_TMPRING_SIZE. */
      waves /= info->num_se;

      radeon_set_context_reg_seq(cs, R_0286E8_SPI_TMPRING_SIZE, 3);
      radeon_emit(cs, S_0286E8_WAVES(waves) | S_0286E8_WAVESIZE(round_up_u32(size_per_wave, 256)));
      radeon_emit(cs, va >> 8);  /* SPI_GFX_SCRATCH_BASE_LO */
      radeon_emit(cs, va >> 40); /* SPI_GFX_SCRATCH_BASE_HI */
   } else {
      radeon_set_context_reg(
         cs, R_0286E8_SPI_TMPRING_SIZE,
         S_0286E8_WAVES(waves) | S_0286E8_WAVESIZE(round_up_u32(size_per_wave, 1024)));
   }
}

static void
radv_emit_compute_scratch(struct radv_device *device, struct radeon_cmdbuf *cs,
                          uint32_t size_per_wave, uint32_t waves,
                          struct radeon_winsys_bo *compute_scratch_bo)
{
   struct radeon_info *info = &device->physical_device->rad_info;
   uint64_t scratch_va;
   uint32_t rsrc1;

   if (!compute_scratch_bo)
      return;

   scratch_va = radv_buffer_get_va(compute_scratch_bo);
   rsrc1 = S_008F04_BASE_ADDRESS_HI(scratch_va >> 32);

   if (device->physical_device->rad_info.gfx_level >= GFX11)
      rsrc1 |= S_008F04_SWIZZLE_ENABLE_GFX11(1);
   else
      rsrc1 |= S_008F04_SWIZZLE_ENABLE_GFX6(1);

   radv_cs_add_buffer(device->ws, cs, compute_scratch_bo);

   if (info->gfx_level >= GFX11) {
      radeon_set_sh_reg_seq(cs, R_00B840_COMPUTE_DISPATCH_SCRATCH_BASE_LO, 4);
      radeon_emit(cs, scratch_va >> 8);
      radeon_emit(cs, scratch_va >> 40);
   } else {
      radeon_set_sh_reg_seq(cs, R_00B900_COMPUTE_USER_DATA_0, 2);
   }

   radeon_emit(cs, scratch_va);
   radeon_emit(cs, rsrc1);

   radeon_set_sh_reg(cs, R_00B860_COMPUTE_TMPRING_SIZE,
                     S_00B860_WAVES(waves) |
                     S_00B860_WAVESIZE(round_up_u32(size_per_wave, info->gfx_level >= GFX11 ? 256 : 1024)));
}

static void
radv_emit_compute_shader_pointers(struct radv_device *device, struct radeon_cmdbuf *cs,
                                  struct radeon_winsys_bo *descriptor_bo)
{
   if (!descriptor_bo)
      return;

   uint64_t va = radv_buffer_get_va(descriptor_bo);
   radv_cs_add_buffer(device->ws, cs, descriptor_bo);

   /* Compute shader user data 0-1 have the scratch pointer (unlike GFX shaders),
    * so emit the descriptor pointer to user data 2-3 instead (task_ring_offsets arg).
    */
   radv_emit_shader_pointer(device, cs, R_00B908_COMPUTE_USER_DATA_2, va, true);
}

static void
radv_emit_graphics_shader_pointers(struct radv_device *device, struct radeon_cmdbuf *cs,
                                   struct radeon_winsys_bo *descriptor_bo)
{
   uint64_t va;

   if (!descriptor_bo)
      return;

   va = radv_buffer_get_va(descriptor_bo);

   radv_cs_add_buffer(device->ws, cs, descriptor_bo);

   if (device->physical_device->rad_info.gfx_level >= GFX11) {
      uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0,
                         R_00B420_SPI_SHADER_PGM_LO_HS,
                         R_00B220_SPI_SHADER_PGM_LO_GS};

      for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
         radv_emit_shader_pointer(device, cs, regs[i], va, true);
      }
   } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
      uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0, R_00B130_SPI_SHADER_USER_DATA_VS_0,
                         R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS,
                         R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS};

      for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
         radv_emit_shader_pointer(device, cs, regs[i], va, true);
      }
   } else if (device->physical_device->rad_info.gfx_level == GFX9) {
      uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0, R_00B130_SPI_SHADER_USER_DATA_VS_0,
                         R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS,
                         R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS};

      for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
         radv_emit_shader_pointer(device, cs, regs[i], va, true);
      }
   } else {
      uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0, R_00B130_SPI_SHADER_USER_DATA_VS_0,
                         R_00B230_SPI_SHADER_USER_DATA_GS_0, R_00B330_SPI_SHADER_USER_DATA_ES_0,
                         R_00B430_SPI_SHADER_USER_DATA_HS_0, R_00B530_SPI_SHADER_USER_DATA_LS_0};

      for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
         radv_emit_shader_pointer(device, cs, regs[i], va, true);
      }
   }
}

static void
radv_init_graphics_state(struct radeon_cmdbuf *cs, struct radv_device *device)
{
   if (device->gfx_init) {
      uint64_t va = radv_buffer_get_va(device->gfx_init);

      radeon_emit(cs, PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0));
      radeon_emit(cs, va);
      radeon_emit(cs, va >> 32);
      radeon_emit(cs, device->gfx_init_size_dw & 0xffff);

      radv_cs_add_buffer(device->ws, cs, device->gfx_init);
   } else {
      si_emit_graphics(device, cs);
   }
}

static void
radv_init_compute_state(struct radeon_cmdbuf *cs, struct radv_device *device)
{
   si_emit_compute(device, cs);
}

static VkResult
radv_update_preamble_cs(struct radv_queue_state *queue, struct radv_device *device,
                        const struct radv_queue_ring_info *needs)
{
   struct radeon_winsys *ws = device->ws;
   struct radeon_winsys_bo *scratch_bo = queue->scratch_bo;
   struct radeon_winsys_bo *descriptor_bo = queue->descriptor_bo;
   struct radeon_winsys_bo *compute_scratch_bo = queue->compute_scratch_bo;
   struct radeon_winsys_bo *esgs_ring_bo = queue->esgs_ring_bo;
   struct radeon_winsys_bo *gsvs_ring_bo = queue->gsvs_ring_bo;
   struct radeon_winsys_bo *tess_rings_bo = queue->tess_rings_bo;
   struct radeon_winsys_bo *task_rings_bo = queue->task_rings_bo;
   struct radeon_winsys_bo *mesh_scratch_ring_bo = queue->mesh_scratch_ring_bo;
   struct radeon_winsys_bo *gds_bo = queue->gds_bo;
   struct radeon_winsys_bo *gds_oa_bo = queue->gds_oa_bo;
   struct radeon_cmdbuf *dest_cs[3] = {0};
   const uint32_t ring_bo_flags = RADEON_FLAG_NO_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING;
   VkResult result = VK_SUCCESS;

   const bool add_sample_positions = !queue->ring_info.sample_positions && needs->sample_positions;
   const uint32_t scratch_size = needs->scratch_size_per_wave * needs->scratch_waves;
   const uint32_t queue_scratch_size =
      queue->ring_info.scratch_size_per_wave * queue->ring_info.scratch_waves;

   if (scratch_size > queue_scratch_size) {
      result = ws->buffer_create(ws, scratch_size, 4096, RADEON_DOMAIN_VRAM, ring_bo_flags,
                                 RADV_BO_PRIORITY_SCRATCH, 0, &scratch_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   const uint32_t compute_scratch_size =
      needs->compute_scratch_size_per_wave * needs->compute_scratch_waves;
   const uint32_t compute_queue_scratch_size =
      queue->ring_info.compute_scratch_size_per_wave * queue->ring_info.compute_scratch_waves;
   if (compute_scratch_size > compute_queue_scratch_size) {
      result = ws->buffer_create(ws, compute_scratch_size, 4096, RADEON_DOMAIN_VRAM, ring_bo_flags,
                                 RADV_BO_PRIORITY_SCRATCH, 0, &compute_scratch_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (needs->esgs_ring_size > queue->ring_info.esgs_ring_size) {
      result = ws->buffer_create(ws, needs->esgs_ring_size, 4096, RADEON_DOMAIN_VRAM, ring_bo_flags,
                                 RADV_BO_PRIORITY_SCRATCH, 0, &esgs_ring_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (needs->gsvs_ring_size > queue->ring_info.gsvs_ring_size) {
      result = ws->buffer_create(ws, needs->gsvs_ring_size, 4096, RADEON_DOMAIN_VRAM, ring_bo_flags,
                                 RADV_BO_PRIORITY_SCRATCH, 0, &gsvs_ring_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (!queue->ring_info.tess_rings && needs->tess_rings) {
      result = ws->buffer_create(
         ws, device->physical_device->hs.tess_offchip_ring_offset + device->physical_device->hs.tess_offchip_ring_size, 256,
         RADEON_DOMAIN_VRAM, ring_bo_flags, RADV_BO_PRIORITY_SCRATCH, 0, &tess_rings_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (!queue->ring_info.task_rings && needs->task_rings) {
      assert(device->physical_device->rad_info.gfx_level >= GFX10_3);

      /* We write the control buffer from the CPU, so need to grant CPU access to the BO.
       * The draw ring needs to be zero-initialized otherwise the ready bits will be incorrect.
       */
      uint32_t task_rings_bo_flags =
         RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_ZERO_VRAM;

      result = ws->buffer_create(ws, device->physical_device->task_info.bo_size_bytes, 256,
                                 RADEON_DOMAIN_VRAM, task_rings_bo_flags, RADV_BO_PRIORITY_SCRATCH,
                                 0, &task_rings_bo);
      if (result != VK_SUCCESS)
         goto fail;

      result = radv_initialise_task_control_buffer(device, task_rings_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (!queue->ring_info.mesh_scratch_ring && needs->mesh_scratch_ring) {
      assert(device->physical_device->rad_info.gfx_level >= GFX10_3);
      result =
         ws->buffer_create(ws, RADV_MESH_SCRATCH_NUM_ENTRIES * RADV_MESH_SCRATCH_ENTRY_BYTES, 256,
                           RADEON_DOMAIN_VRAM, ring_bo_flags, RADV_BO_PRIORITY_SCRATCH, 0, &mesh_scratch_ring_bo);

      if (result != VK_SUCCESS)
         goto fail;
   }

   if (!queue->ring_info.gds && needs->gds) {
      assert(device->physical_device->rad_info.gfx_level >= GFX10);

      /* 4 streamout GDS counters.
       * We need 256B (64 dw) of GDS, otherwise streamout hangs.
       */
      result = ws->buffer_create(ws, 256, 4, RADEON_DOMAIN_GDS, ring_bo_flags,
                                 RADV_BO_PRIORITY_SCRATCH, 0, &gds_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (!queue->ring_info.gds_oa && needs->gds_oa) {
      assert(device->physical_device->rad_info.gfx_level >= GFX10);

      result = ws->buffer_create(ws, 4, 1, RADEON_DOMAIN_OA, ring_bo_flags,
                                 RADV_BO_PRIORITY_SCRATCH, 0, &gds_oa_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   /* Re-initialize the descriptor BO when any ring BOs changed.
    *
    * Additionally, make sure to create the descriptor BO for the compute queue
    * when it uses the task shader rings. The task rings BO is shared between the
    * GFX and compute queues and already initialized here.
    */
   if ((queue->qf == RADV_QUEUE_COMPUTE && !descriptor_bo && task_rings_bo) ||
       scratch_bo != queue->scratch_bo || esgs_ring_bo != queue->esgs_ring_bo ||
       gsvs_ring_bo != queue->gsvs_ring_bo || tess_rings_bo != queue->tess_rings_bo ||
       task_rings_bo != queue->task_rings_bo || mesh_scratch_ring_bo != queue->mesh_scratch_ring_bo ||
       add_sample_positions) {
      uint32_t size = 0;
      if (gsvs_ring_bo || esgs_ring_bo || tess_rings_bo || task_rings_bo || mesh_scratch_ring_bo || add_sample_positions) {
         size = 160; /* 2 dword + 2 padding + 4 dword * 9 */
         if (add_sample_positions)
            size += 128; /* 64+32+16+8 = 120 bytes */
      } else if (scratch_bo) {
         size = 8; /* 2 dword */
      }

      result = ws->buffer_create(
         ws, size, 4096, RADEON_DOMAIN_VRAM,
         RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY,
         RADV_BO_PRIORITY_DESCRIPTOR, 0, &descriptor_bo);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (descriptor_bo != queue->descriptor_bo) {
      uint32_t *map = (uint32_t *)ws->buffer_map(descriptor_bo);
      if (!map)
         goto fail;

      if (scratch_bo) {
         uint64_t scratch_va = radv_buffer_get_va(scratch_bo);
         uint32_t rsrc1 = S_008F04_BASE_ADDRESS_HI(scratch_va >> 32);

         if (device->physical_device->rad_info.gfx_level >= GFX11)
            rsrc1 |= S_008F04_SWIZZLE_ENABLE_GFX11(1);
         else
            rsrc1 |= S_008F04_SWIZZLE_ENABLE_GFX6(1);

         map[0] = scratch_va;
         map[1] = rsrc1;
      }

      if (esgs_ring_bo || gsvs_ring_bo || tess_rings_bo || task_rings_bo || mesh_scratch_ring_bo || add_sample_positions)
         radv_fill_shader_rings(device, map, add_sample_positions, needs->esgs_ring_size,
                                esgs_ring_bo, needs->gsvs_ring_size, gsvs_ring_bo, tess_rings_bo,
                                task_rings_bo, mesh_scratch_ring_bo);

      ws->buffer_unmap(descriptor_bo);
   }

   for (int i = 0; i < 3; ++i) {
      /* Don't create continue preamble when it's not necessary. */
      if (i == 2) {
         /* We only need the continue preamble when we can't use indirect buffers. */
         if (!(device->instance->debug_flags & RADV_DEBUG_NO_IBS) &&
             device->physical_device->rad_info.gfx_level >= GFX7)
            continue;
         /* Continue preamble is unnecessary when no shader rings are used. */
         if (!needs->scratch_size_per_wave && !needs->compute_scratch_size_per_wave &&
             !needs->esgs_ring_size && !needs->gsvs_ring_size && !needs->tess_rings &&
             !needs->task_rings && !needs->mesh_scratch_ring && !needs->gds && !needs->gds_oa && !needs->sample_positions)
            continue;
      }

      enum rgp_flush_bits sqtt_flush_bits = 0;
      struct radeon_cmdbuf *cs = NULL;
      cs = ws->cs_create(ws, radv_queue_family_to_ring(device->physical_device, queue->qf));
      if (!cs) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      dest_cs[i] = cs;

      if (scratch_bo)
         radv_cs_add_buffer(ws, cs, scratch_bo);

      /* Emit initial configuration. */
      switch (queue->qf) {
      case RADV_QUEUE_GENERAL:
         radv_init_graphics_state(cs, device);

         if (esgs_ring_bo || gsvs_ring_bo || tess_rings_bo || task_rings_bo) {
            radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
            radeon_emit(cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));

            radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
            radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
         }

         radv_emit_gs_ring_sizes(device, cs, esgs_ring_bo, needs->esgs_ring_size, gsvs_ring_bo,
                                 needs->gsvs_ring_size);
         radv_emit_tess_factor_ring(device, cs, tess_rings_bo);
         radv_emit_task_rings(device, cs, task_rings_bo, false);
         radv_emit_graphics_shader_pointers(device, cs, descriptor_bo);
         radv_emit_compute_scratch(device, cs, needs->compute_scratch_size_per_wave,
                                   needs->compute_scratch_waves, compute_scratch_bo);
         radv_emit_graphics_scratch(device, cs, needs->scratch_size_per_wave, needs->scratch_waves,
                                    scratch_bo);
         break;
      case RADV_QUEUE_COMPUTE:
         radv_init_compute_state(cs, device);

         if (task_rings_bo) {
            radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
            radeon_emit(cs, EVENT_TYPE(V_028A90_CS_PARTIAL_FLUSH) | EVENT_INDEX(4));
         }

         radv_emit_task_rings(device, cs, task_rings_bo, true);
         radv_emit_compute_shader_pointers(device, cs, descriptor_bo);
         radv_emit_compute_scratch(device, cs, needs->compute_scratch_size_per_wave,
                                   needs->compute_scratch_waves, compute_scratch_bo);
         break;
      default:
         break;
      }

      if (gds_bo)
         radv_cs_add_buffer(ws, cs, gds_bo);
      if (gds_oa_bo)
         radv_cs_add_buffer(ws, cs, gds_oa_bo);

      if (i < 2) {
         /* The two initial preambles have a cache flush at the beginning. */
         const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
         const bool is_mec = queue->qf == RADV_QUEUE_COMPUTE && gfx_level >= GFX7;
         enum radv_cmd_flush_bits flush_bits = RADV_CMD_FLAG_INV_ICACHE | RADV_CMD_FLAG_INV_SCACHE |
                                               RADV_CMD_FLAG_INV_VCACHE | RADV_CMD_FLAG_INV_L2 |
                                               RADV_CMD_FLAG_START_PIPELINE_STATS;

         if (i == 0) {
            /* The full flush preamble should also wait for previous shader work to finish. */
            flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH;
            if (queue->qf == RADV_QUEUE_GENERAL)
               flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH;
         }

         si_cs_emit_cache_flush(cs, gfx_level, NULL, 0, is_mec, flush_bits, &sqtt_flush_bits, 0);
      }

      result = ws->cs_finalize(cs);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (queue->initial_full_flush_preamble_cs)
      ws->cs_destroy(queue->initial_full_flush_preamble_cs);

   if (queue->initial_preamble_cs)
      ws->cs_destroy(queue->initial_preamble_cs);

   if (queue->continue_preamble_cs)
      ws->cs_destroy(queue->continue_preamble_cs);

   queue->initial_full_flush_preamble_cs = dest_cs[0];
   queue->initial_preamble_cs = dest_cs[1];
   queue->continue_preamble_cs = dest_cs[2];

   if (scratch_bo != queue->scratch_bo) {
      if (queue->scratch_bo)
         ws->buffer_destroy(ws, queue->scratch_bo);
      queue->scratch_bo = scratch_bo;
   }

   if (compute_scratch_bo != queue->compute_scratch_bo) {
      if (queue->compute_scratch_bo)
         ws->buffer_destroy(ws, queue->compute_scratch_bo);
      queue->compute_scratch_bo = compute_scratch_bo;
   }

   if (esgs_ring_bo != queue->esgs_ring_bo) {
      if (queue->esgs_ring_bo)
         ws->buffer_destroy(ws, queue->esgs_ring_bo);
      queue->esgs_ring_bo = esgs_ring_bo;
   }

   if (gsvs_ring_bo != queue->gsvs_ring_bo) {
      if (queue->gsvs_ring_bo)
         ws->buffer_destroy(ws, queue->gsvs_ring_bo);
      queue->gsvs_ring_bo = gsvs_ring_bo;
   }

   if (descriptor_bo != queue->descriptor_bo) {
      if (queue->descriptor_bo)
         ws->buffer_destroy(ws, queue->descriptor_bo);
      queue->descriptor_bo = descriptor_bo;
   }

   queue->tess_rings_bo = tess_rings_bo;
   queue->task_rings_bo = task_rings_bo;
   queue->mesh_scratch_ring_bo = mesh_scratch_ring_bo;
   queue->gds_bo = gds_bo;
   queue->gds_oa_bo = gds_oa_bo;
   queue->ring_info = *needs;
   return VK_SUCCESS;
fail:
   for (int i = 0; i < ARRAY_SIZE(dest_cs); ++i)
      if (dest_cs[i])
         ws->cs_destroy(dest_cs[i]);
   if (descriptor_bo && descriptor_bo != queue->descriptor_bo)
      ws->buffer_destroy(ws, descriptor_bo);
   if (scratch_bo && scratch_bo != queue->scratch_bo)
      ws->buffer_destroy(ws, scratch_bo);
   if (compute_scratch_bo && compute_scratch_bo != queue->compute_scratch_bo)
      ws->buffer_destroy(ws, compute_scratch_bo);
   if (esgs_ring_bo && esgs_ring_bo != queue->esgs_ring_bo)
      ws->buffer_destroy(ws, esgs_ring_bo);
   if (gsvs_ring_bo && gsvs_ring_bo != queue->gsvs_ring_bo)
      ws->buffer_destroy(ws, gsvs_ring_bo);
   if (tess_rings_bo && tess_rings_bo != queue->tess_rings_bo)
      ws->buffer_destroy(ws, tess_rings_bo);
   if (task_rings_bo && task_rings_bo != queue->task_rings_bo)
      ws->buffer_destroy(ws, task_rings_bo);
   if (gds_bo && gds_bo != queue->gds_bo)
      ws->buffer_destroy(ws, gds_bo);
   if (gds_oa_bo && gds_oa_bo != queue->gds_oa_bo)
      ws->buffer_destroy(ws, gds_oa_bo);

   return vk_error(queue, result);
}

static struct radeon_cmdbuf *
radv_create_perf_counter_lock_cs(struct radv_device *device, unsigned pass, bool unlock)
{
   struct radeon_cmdbuf **cs_ref = &device->perf_counter_lock_cs[pass * 2 + (unlock ? 1 : 0)];
   struct radeon_cmdbuf *cs;

   if (*cs_ref)
      return *cs_ref;

   cs = device->ws->cs_create(device->ws, AMD_IP_GFX);
   if (!cs)
      return NULL;

   ASSERTED unsigned cdw = radeon_check_space(device->ws, cs, 21);

   if (!unlock) {
      uint64_t mutex_va = radv_buffer_get_va(device->perf_counter_bo) + PERF_CTR_BO_LOCK_OFFSET;
      radeon_emit(cs, PKT3(PKT3_ATOMIC_MEM, 7, 0));
      radeon_emit(cs, ATOMIC_OP(TC_OP_ATOMIC_CMPSWAP_32) | ATOMIC_COMMAND(ATOMIC_COMMAND_LOOP));
      radeon_emit(cs, mutex_va);       /* addr lo */
      radeon_emit(cs, mutex_va >> 32); /* addr hi */
      radeon_emit(cs, 1);              /* data lo */
      radeon_emit(cs, 0);              /* data hi */
      radeon_emit(cs, 0);              /* compare data lo */
      radeon_emit(cs, 0);              /* compare data hi */
      radeon_emit(cs, 10);             /* loop interval */
   }

   uint64_t va = radv_buffer_get_va(device->perf_counter_bo) + PERF_CTR_BO_PASS_OFFSET;
   uint64_t unset_va = va + (unlock ? 8 * pass : 0);
   uint64_t set_va = va + (unlock ? 0 : 8 * pass);

   radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
   radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                      COPY_DATA_COUNT_SEL | COPY_DATA_WR_CONFIRM);
   radeon_emit(cs, 0); /* immediate */
   radeon_emit(cs, 0);
   radeon_emit(cs, unset_va);
   radeon_emit(cs, unset_va >> 32);

   radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
   radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                      COPY_DATA_COUNT_SEL | COPY_DATA_WR_CONFIRM);
   radeon_emit(cs, 1); /* immediate */
   radeon_emit(cs, 0);
   radeon_emit(cs, set_va);
   radeon_emit(cs, set_va >> 32);

   if (unlock) {
      uint64_t mutex_va = radv_buffer_get_va(device->perf_counter_bo) + PERF_CTR_BO_LOCK_OFFSET;

      radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
      radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                         COPY_DATA_COUNT_SEL | COPY_DATA_WR_CONFIRM);
      radeon_emit(cs, 0); /* immediate */
      radeon_emit(cs, 0);
      radeon_emit(cs, mutex_va);
      radeon_emit(cs, mutex_va >> 32);
   }

   assert(cs->cdw <= cdw);

   VkResult result = device->ws->cs_finalize(cs);
   if (result != VK_SUCCESS) {
      device->ws->cs_destroy(cs);
      return NULL;
   }

   /* All the casts are to avoid MSVC errors around pointer truncation in a non-taken
    * alternative.
    */
   if (p_atomic_cmpxchg((uintptr_t*)cs_ref, 0, (uintptr_t)cs) != 0) {
      device->ws->cs_destroy(cs);
   }

   return *cs_ref;
}

static VkResult
radv_sparse_buffer_bind_memory(struct radv_device *device, const VkSparseBufferMemoryBindInfo *bind)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, bind->buffer);
   VkResult result = VK_SUCCESS;

   struct radv_device_memory *mem = NULL;
   VkDeviceSize resourceOffset = 0;
   VkDeviceSize size = 0;
   VkDeviceSize memoryOffset = 0;
   for (uint32_t i = 0; i < bind->bindCount; ++i) {
      struct radv_device_memory *cur_mem = NULL;

      if (bind->pBinds[i].memory != VK_NULL_HANDLE)
         cur_mem = radv_device_memory_from_handle(bind->pBinds[i].memory);
      if (i && mem == cur_mem) {
         if (mem) {
            if (bind->pBinds[i].resourceOffset == resourceOffset + size &&
                bind->pBinds[i].memoryOffset == memoryOffset + size) {
               size += bind->pBinds[i].size;
               continue;
            }
         } else {
            if (bind->pBinds[i].resourceOffset == resourceOffset + size) {
               size += bind->pBinds[i].size;
               continue;
            }
         }
      }
      if (size) {
         result = device->ws->buffer_virtual_bind(device->ws, buffer->bo,
                                                  resourceOffset, size,
                                                  mem ? mem->bo : NULL, memoryOffset);
         if (result != VK_SUCCESS)
            return result;
      }
      mem = cur_mem;
      resourceOffset = bind->pBinds[i].resourceOffset;
      size = bind->pBinds[i].size;
      memoryOffset = bind->pBinds[i].memoryOffset;
   }
   if (size) {
      result = device->ws->buffer_virtual_bind(device->ws, buffer->bo,
                                               resourceOffset, size,
                                               mem ? mem->bo : NULL, memoryOffset);
   }

   return result;
}

static VkResult
radv_sparse_image_opaque_bind_memory(struct radv_device *device,
                                     const VkSparseImageOpaqueMemoryBindInfo *bind)
{
   RADV_FROM_HANDLE(radv_image, image, bind->image);
   VkResult result;

   for (uint32_t i = 0; i < bind->bindCount; ++i) {
      struct radv_device_memory *mem = NULL;

      if (bind->pBinds[i].memory != VK_NULL_HANDLE)
         mem = radv_device_memory_from_handle(bind->pBinds[i].memory);

      result = device->ws->buffer_virtual_bind(device->ws, image->bindings[0].bo,
                                               bind->pBinds[i].resourceOffset, bind->pBinds[i].size,
                                               mem ? mem->bo : NULL, bind->pBinds[i].memoryOffset);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static VkResult
radv_sparse_image_bind_memory(struct radv_device *device, const VkSparseImageMemoryBindInfo *bind)
{
   RADV_FROM_HANDLE(radv_image, image, bind->image);
   struct radeon_surf *surface = &image->planes[0].surface;
   uint32_t bs = vk_format_get_blocksize(image->vk.format);
   VkResult result;

   for (uint32_t i = 0; i < bind->bindCount; ++i) {
      struct radv_device_memory *mem = NULL;
      uint32_t offset, pitch, depth_pitch;
      uint32_t mem_offset = bind->pBinds[i].memoryOffset;
      const uint32_t layer = bind->pBinds[i].subresource.arrayLayer;
      const uint32_t level = bind->pBinds[i].subresource.mipLevel;

      VkExtent3D bind_extent = bind->pBinds[i].extent;
      bind_extent.width =
         DIV_ROUND_UP(bind_extent.width, vk_format_get_blockwidth(image->vk.format));
      bind_extent.height =
         DIV_ROUND_UP(bind_extent.height, vk_format_get_blockheight(image->vk.format));

      VkOffset3D bind_offset = bind->pBinds[i].offset;
      bind_offset.x /= vk_format_get_blockwidth(image->vk.format);
      bind_offset.y /= vk_format_get_blockheight(image->vk.format);

      if (bind->pBinds[i].memory != VK_NULL_HANDLE)
         mem = radv_device_memory_from_handle(bind->pBinds[i].memory);

      if (device->physical_device->rad_info.gfx_level >= GFX9) {
         offset = surface->u.gfx9.surf_slice_size * layer + surface->u.gfx9.prt_level_offset[level];
         pitch = surface->u.gfx9.prt_level_pitch[level];
         depth_pitch = surface->u.gfx9.surf_slice_size;
      } else {
         depth_pitch = surface->u.legacy.level[level].slice_size_dw * 4;
         offset = (uint64_t)surface->u.legacy.level[level].offset_256B * 256 + depth_pitch * layer;
         pitch = surface->u.legacy.level[level].nblk_x;
      }

      offset += bind_offset.z * depth_pitch +
                (bind_offset.y * pitch * surface->prt_tile_depth +
                 bind_offset.x * surface->prt_tile_height * surface->prt_tile_depth) *
                   bs;

      uint32_t aligned_extent_width = ALIGN(bind_extent.width, surface->prt_tile_width);
      uint32_t aligned_extent_height = ALIGN(bind_extent.height, surface->prt_tile_height);
      uint32_t aligned_extent_depth = ALIGN(bind_extent.depth, surface->prt_tile_depth);

      bool whole_subres =
         (bind_extent.height <= surface->prt_tile_height || aligned_extent_width == pitch) &&
         (bind_extent.depth <= surface->prt_tile_depth ||
          aligned_extent_width * aligned_extent_height * bs == depth_pitch);

      if (whole_subres) {
         uint32_t size = aligned_extent_width * aligned_extent_height * aligned_extent_depth * bs;
         result = device->ws->buffer_virtual_bind(device->ws, image->bindings[0].bo, offset, size,
                                                  mem ? mem->bo : NULL, mem_offset);
         if (result != VK_SUCCESS)
            return result;
      } else {
         uint32_t img_y_increment = pitch * bs * surface->prt_tile_depth;
         uint32_t mem_y_increment = aligned_extent_width * bs * surface->prt_tile_depth;
         uint32_t mem_z_increment = aligned_extent_width * aligned_extent_height * bs;
         uint32_t size = mem_y_increment * surface->prt_tile_height;
         for (unsigned z = 0; z < bind_extent.depth;
              z += surface->prt_tile_depth, offset += depth_pitch * surface->prt_tile_depth) {
            for (unsigned y = 0; y < bind_extent.height; y += surface->prt_tile_height) {
               result = device->ws->buffer_virtual_bind(
                  device->ws, image->bindings[0].bo, offset + img_y_increment * y, size,
                  mem ? mem->bo : NULL, mem_offset + mem_y_increment * y + mem_z_increment * z);
               if (result != VK_SUCCESS)
                  return result;
            }
         }
      }
   }

   return VK_SUCCESS;
}

static VkResult
radv_update_preambles(struct radv_queue_state *queue, struct radv_device *device,
                      struct vk_command_buffer *const *cmd_buffers, uint32_t cmd_buffer_count,
                      bool *uses_perf_counters)
{
   if (queue->qf == RADV_QUEUE_TRANSFER)
      return VK_SUCCESS;

   /* Figure out the needs of the current submission.
    * Start by copying the queue's current info.
    * This is done because we only allow two possible behaviours for these buffers:
    * - Grow when the newly needed amount is larger than what we had
    * - Allocate the max size and reuse it, but don't free it until the queue is destroyed
    */
   struct radv_queue_ring_info needs = queue->ring_info;
   *uses_perf_counters = false;
   for (uint32_t j = 0; j < cmd_buffer_count; j++) {
      struct radv_cmd_buffer *cmd_buffer = container_of(cmd_buffers[j], struct radv_cmd_buffer, vk);

      needs.scratch_size_per_wave =
         MAX2(needs.scratch_size_per_wave, cmd_buffer->scratch_size_per_wave_needed);
      needs.scratch_waves = MAX2(needs.scratch_waves, cmd_buffer->scratch_waves_wanted);
      needs.compute_scratch_size_per_wave = MAX2(needs.compute_scratch_size_per_wave,
                                                 cmd_buffer->compute_scratch_size_per_wave_needed);
      needs.compute_scratch_waves =
         MAX2(needs.compute_scratch_waves, cmd_buffer->compute_scratch_waves_wanted);
      needs.esgs_ring_size = MAX2(needs.esgs_ring_size, cmd_buffer->esgs_ring_size_needed);
      needs.gsvs_ring_size = MAX2(needs.gsvs_ring_size, cmd_buffer->gsvs_ring_size_needed);
      needs.tess_rings |= cmd_buffer->tess_rings_needed;
      needs.task_rings |= cmd_buffer->task_rings_needed;
      needs.mesh_scratch_ring |= cmd_buffer->mesh_scratch_ring_needed;
      needs.gds |= cmd_buffer->gds_needed;
      needs.gds_oa |= cmd_buffer->gds_oa_needed;
      needs.sample_positions |= cmd_buffer->sample_positions_needed;
      *uses_perf_counters |= cmd_buffer->state.uses_perf_counters;
   }

   /* Sanitize scratch size information. */
   needs.scratch_waves = needs.scratch_size_per_wave
                            ? MIN2(needs.scratch_waves, UINT32_MAX / needs.scratch_size_per_wave)
                            : 0;
   needs.compute_scratch_waves =
      needs.compute_scratch_size_per_wave
         ? MIN2(needs.compute_scratch_waves, UINT32_MAX / needs.compute_scratch_size_per_wave)
         : 0;

   /* Return early if we already match these needs.
    * Note that it's not possible for any of the needed values to be less
    * than what the queue already had, because we only ever increase the allocated size.
    */
   if (queue->initial_full_flush_preamble_cs &&
       queue->ring_info.scratch_size_per_wave == needs.scratch_size_per_wave &&
       queue->ring_info.scratch_waves == needs.scratch_waves &&
       queue->ring_info.compute_scratch_size_per_wave == needs.compute_scratch_size_per_wave &&
       queue->ring_info.compute_scratch_waves == needs.compute_scratch_waves &&
       queue->ring_info.esgs_ring_size == needs.esgs_ring_size &&
       queue->ring_info.gsvs_ring_size == needs.gsvs_ring_size &&
       queue->ring_info.tess_rings == needs.tess_rings &&
       queue->ring_info.task_rings == needs.task_rings &&
       queue->ring_info.mesh_scratch_ring == needs.mesh_scratch_ring &&
       queue->ring_info.gds == needs.gds &&
       queue->ring_info.gds_oa == needs.gds_oa &&
       queue->ring_info.sample_positions == needs.sample_positions)
      return VK_SUCCESS;

   return radv_update_preamble_cs(queue, device, &needs);
}

static VkResult
radv_update_ace_preambles(struct radv_queue *queue)
{
   if (!radv_queue_init_ace_internal_state(queue))
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* Copy task rings state.
    * Task shaders that are submitted on the ACE queue need to share
    * their ring buffers with the mesh shaders on the GFX queue.
    */
   queue->ace_internal_state->ring_info.task_rings = queue->state.ring_info.task_rings;
   queue->ace_internal_state->task_rings_bo = queue->state.task_rings_bo;

   /* Copy some needed states from the parent queue state.
    * These can only increase so it's okay to copy them as-is without checking.
    * Note, task shaders use the scratch size from their graphics pipeline.
    */
   struct radv_queue_ring_info needs = queue->ace_internal_state->ring_info;
   needs.compute_scratch_size_per_wave = queue->state.ring_info.scratch_size_per_wave;
   needs.compute_scratch_waves = queue->state.ring_info.scratch_waves;
   needs.task_rings = queue->state.ring_info.task_rings;

   return radv_update_preamble_cs(queue->ace_internal_state, queue->device, &needs);
}

static bool
radv_cmd_buffer_needs_ace(const struct radv_cmd_buffer *cmd_buffer)
{
   return cmd_buffer->ace_internal.cs && cmd_buffer->task_rings_needed;
}

static VkResult
radv_queue_submit_bind_sparse_memory(struct radv_device *device, struct vk_queue_submit *submission)
{
   for (uint32_t i = 0; i < submission->buffer_bind_count; ++i) {
      VkResult result = radv_sparse_buffer_bind_memory(device, submission->buffer_binds + i);
      if (result != VK_SUCCESS)
         return result;
   }

   for (uint32_t i = 0; i < submission->image_opaque_bind_count; ++i) {
      VkResult result =
         radv_sparse_image_opaque_bind_memory(device, submission->image_opaque_binds + i);
      if (result != VK_SUCCESS)
         return result;
   }

   for (uint32_t i = 0; i < submission->image_bind_count; ++i) {
      VkResult result = radv_sparse_image_bind_memory(device, submission->image_binds + i);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static VkResult
radv_queue_submit_empty(struct radv_queue *queue, struct vk_queue_submit *submission)
{
   struct radeon_winsys_ctx *ctx = queue->hw_ctx;
   struct radv_winsys_submit_info submit = {
      .ip_type = radv_queue_ring(queue),
      .queue_index = queue->vk.index_in_family,
   };

   return queue->device->ws->cs_submit(ctx, 1, &submit, submission->wait_count, submission->waits,
                                       submission->signal_count, submission->signals, false);
}

static VkResult
radv_queue_submit_with_ace(struct radv_queue *queue, struct vk_queue_submit *submission,
                           struct radeon_cmdbuf **cs_array, unsigned cs_count, unsigned cs_offset,
                           bool can_patch)
{
   /* Submits command buffers that may have an internal ACE cmdbuf
    * using scheduled dependencies. This guarantees that the GFX cmdbuf
    * is only scheduled after ACE.
    *
    * TODO: Unfortunately this is prone to a deadlock, so is considered a
    *       temporary solution until gang submit is merged in the upstream kernel.
    */
   struct radeon_winsys_ctx *ctx = queue->hw_ctx;
   const uint32_t max_cs_submission = queue->device->trace_bo ? 1 : RADV_MAX_IBS_PER_SUBMIT;
   const bool need_wait = submission->wait_count > 0;
   VkResult result = VK_SUCCESS;

   struct radeon_cmdbuf **ace_cs_array = calloc(max_cs_submission, sizeof(struct radeon_cmdbuf *));
   if (!ace_cs_array) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto finish;
   }

   result = radv_update_ace_preambles(queue);
   if (result != VK_SUCCESS)
      goto finish;

   struct radv_winsys_submit_info submit[2] = {
      {
         .ip_type = AMD_IP_COMPUTE,
         .cs_array = ace_cs_array,
         .cs_count = 0,
         .initial_preamble_cs = need_wait
                                   ? queue->ace_internal_state->initial_full_flush_preamble_cs
                                   : queue->ace_internal_state->initial_preamble_cs,
      },
      {
         .ip_type = radv_queue_ring(queue),
         .queue_index = queue->vk.index_in_family,
         .cs_array = cs_array,
         .cs_count = 0,
         .initial_preamble_cs = need_wait ? queue->state.initial_full_flush_preamble_cs
                                          : queue->state.initial_preamble_cs,
      }};

   for (uint32_t advance, j = 0; j < cs_count; j += advance) {
      advance = MIN2(max_cs_submission, cs_count - j);
      bool last_submit = j + advance == cs_count;

      if (queue->device->trace_bo)
         *queue->device->trace_id_ptr = 0;

      for (unsigned c = 0; c < advance; ++c) {
         const struct radv_cmd_buffer *cmd_buffer =
            (struct radv_cmd_buffer *)submission->command_buffers[j + c + cs_offset];
         if (!radv_cmd_buffer_needs_ace(cmd_buffer))
            continue;

         submit[0].cs_array[submit[0].cs_count++] = cmd_buffer->ace_internal.cs;
      }

      const uint32_t submit_count = 1 + !!submit[0].cs_count;
      const struct radv_winsys_submit_info *submit_ptr = submit + !submit[0].cs_count;
      submit[1].cs_count = advance;

      result = queue->device->ws->cs_submit(
         ctx, submit_count, submit_ptr, j == 0 ? submission->wait_count : 0, submission->waits,
         last_submit ? submission->signal_count : 0, submission->signals, can_patch);

      if (result != VK_SUCCESS)
         goto finish;

      if (queue->device->trace_bo) {
         radv_check_gpu_hangs(queue, cs_array[j]);
      }

      if (queue->device->tma_bo) {
         radv_check_trap_handler(queue);
      }

      submit[1].cs_array += submit[1].cs_count;
      submit[1].initial_preamble_cs = queue->state.initial_preamble_cs;
      submit[0].cs_count = 0;
      submit[0].initial_preamble_cs = queue->ace_internal_state->initial_preamble_cs;
   }

finish:
   free(ace_cs_array);
   return result;
}

static VkResult
radv_queue_submit_normal(struct radv_queue *queue, struct vk_queue_submit *submission)
{
   struct radeon_winsys_ctx *ctx = queue->hw_ctx;
   uint32_t max_cs_submission = queue->device->trace_bo ? 1 : RADV_MAX_IBS_PER_SUBMIT;
   bool can_patch = true;
   bool use_ace = false;
   uint32_t advance;
   VkResult result;
   bool uses_perf_counters = false;

   result = radv_update_preambles(&queue->state, queue->device, submission->command_buffers,
                                  submission->command_buffer_count, &uses_perf_counters);
   if (result != VK_SUCCESS)
      return result;

   if (queue->device->trace_bo)
      simple_mtx_lock(&queue->device->trace_mtx);

   const unsigned cs_offset = uses_perf_counters ? 1 : 0;
   const unsigned cmd_buffer_count =
      submission->command_buffer_count + (uses_perf_counters ? 2 : 0);

   struct radeon_cmdbuf **cs_array = malloc(sizeof(struct radeon_cmdbuf *) * cmd_buffer_count);
   if (!cs_array)
      goto fail;

   for (uint32_t j = 0; j < submission->command_buffer_count; j++) {
      struct radv_cmd_buffer *cmd_buffer = (struct radv_cmd_buffer *)submission->command_buffers[j];
      assert(cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

      cs_array[j + cs_offset] = cmd_buffer->cs;
      if ((cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))
         can_patch = false;

      cmd_buffer->status = RADV_CMD_BUFFER_STATUS_PENDING;
      use_ace |= radv_cmd_buffer_needs_ace(cmd_buffer);
   }

   if (uses_perf_counters) {
      cs_array[0] =
         radv_create_perf_counter_lock_cs(queue->device, submission->perf_pass_index, false);
      cs_array[cmd_buffer_count - 1] =
         radv_create_perf_counter_lock_cs(queue->device, submission->perf_pass_index, true);
      can_patch = false;
      if (!cs_array[0] || !cs_array[cmd_buffer_count - 1]) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }
   }

   if (use_ace) {
      result = radv_queue_submit_with_ace(queue, submission, cs_array, cmd_buffer_count, cs_offset,
                                          can_patch);
      goto fail;
   }

   /* For fences on the same queue/vm amdgpu doesn't wait till all processing is finished
    * before starting the next cmdbuffer, so we need to do it here. */
   bool need_wait = submission->wait_count > 0;

   struct radv_winsys_submit_info submit = {
      .ip_type = radv_queue_ring(queue),
      .queue_index = queue->vk.index_in_family,
      .cs_array = cs_array,
      .cs_count = 0,
      .initial_preamble_cs =
         need_wait ? queue->state.initial_full_flush_preamble_cs : queue->state.initial_preamble_cs,
      .continue_preamble_cs = queue->state.continue_preamble_cs,
   };

   for (uint32_t j = 0; j < cmd_buffer_count; j += advance) {
      advance = MIN2(max_cs_submission, cmd_buffer_count - j);
      bool last_submit = j + advance == cmd_buffer_count;

      if (queue->device->trace_bo)
         *queue->device->trace_id_ptr = 0;

      submit.cs_count = advance;

      result = queue->device->ws->cs_submit(
         ctx, 1, &submit, j == 0 ? submission->wait_count : 0, submission->waits,
         last_submit ? submission->signal_count : 0, submission->signals, can_patch);

      if (result != VK_SUCCESS)
         goto fail;

      if (queue->device->trace_bo) {
         radv_check_gpu_hangs(queue, cs_array[j]);
      }

      if (queue->device->tma_bo) {
         radv_check_trap_handler(queue);
      }

      submit.cs_array += advance;
      submit.initial_preamble_cs = queue->state.initial_preamble_cs;
   }

fail:
   free(cs_array);
   if (queue->device->trace_bo)
      simple_mtx_unlock(&queue->device->trace_mtx);

   return result;
}

static VkResult
radv_queue_submit(struct vk_queue *vqueue, struct vk_queue_submit *submission)
{
   struct radv_queue *queue = (struct radv_queue *)vqueue;
   VkResult result;

   result = radv_queue_submit_bind_sparse_memory(queue->device, submission);
   if (result != VK_SUCCESS)
      goto fail;

   if (!submission->command_buffer_count && !submission->wait_count && !submission->signal_count)
      return VK_SUCCESS;

   if (!submission->command_buffer_count) {
      result = radv_queue_submit_empty(queue, submission);
   } else {
      result = radv_queue_submit_normal(queue, submission);
   }

fail:
   if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) {
      /* When something bad happened during the submission, such as
       * an out of memory issue, it might be hard to recover from
       * this inconsistent state. To avoid this sort of problem, we
       * assume that we are in a really bad situation and return
       * VK_ERROR_DEVICE_LOST to ensure the clients do not attempt
       * to submit the same job again to this device.
       */
      result = vk_device_set_lost(&queue->device->vk, "vkQueueSubmit() failed");
   }
   return result;
}

bool
radv_queue_internal_submit(struct radv_queue *queue, struct radeon_cmdbuf *cs)
{
   struct radeon_winsys_ctx *ctx = queue->hw_ctx;
   struct radv_winsys_submit_info submit = {
      .ip_type = radv_queue_ring(queue),
      .queue_index = queue->vk.index_in_family,
      .cs_array = &cs,
      .cs_count = 1,
   };

   VkResult result = queue->device->ws->cs_submit(ctx, 1, &submit, 0, NULL, 0, NULL, false);
   if (result != VK_SUCCESS)
      return false;

   return true;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(&radv_instance_extensions_supported,
                                                     pPropertyCount, pProperties);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
radv_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   RADV_FROM_HANDLE(vk_instance, instance, _instance);
   return vk_instance_get_proc_addr(instance, &radv_instance_entrypoints, pName);
}

/* Windows will use a dll definition file to avoid build errors. */
#ifdef _WIN32
#undef PUBLIC
#define PUBLIC
#endif

/* The loader wants us to expose a second GetInstanceProcAddr function
 * to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return radv_GetInstanceProcAddr(instance, pName);
}

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance _instance, const char *pName)
{
   RADV_FROM_HANDLE(radv_instance, instance, _instance);
   return vk_instance_get_physical_device_proc_addr(&instance->vk, pName);
}

bool
radv_get_memory_fd(struct radv_device *device, struct radv_device_memory *memory, int *pFD)
{
   /* Only set BO metadata for the first plane */
   if (memory->image && memory->image->bindings[0].offset == 0) {
      struct radeon_bo_metadata metadata;
      radv_init_metadata(device, memory->image, &metadata);
      device->ws->buffer_set_metadata(device->ws, memory->bo, &metadata);
   }

   return device->ws->buffer_get_fd(device->ws, memory->bo, pFD);
}

void
radv_device_memory_init(struct radv_device_memory *mem, struct radv_device *device,
                        struct radeon_winsys_bo *bo)
{
   memset(mem, 0, sizeof(*mem));
   vk_object_base_init(&device->vk, &mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY);

   mem->bo = bo;
}

void
radv_device_memory_finish(struct radv_device_memory *mem)
{
   vk_object_base_finish(&mem->base);
}

void
radv_free_memory(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                 struct radv_device_memory *mem)
{
   if (mem == NULL)
      return;

#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
   if (mem->android_hardware_buffer)
      AHardwareBuffer_release(mem->android_hardware_buffer);
#endif

   if (mem->bo) {
      if (device->overallocation_disallowed) {
         mtx_lock(&device->overallocation_mutex);
         device->allocated_memory_size[mem->heap_index] -= mem->alloc_size;
         mtx_unlock(&device->overallocation_mutex);
      }

      if (device->use_global_bo_list)
         device->ws->buffer_make_resident(device->ws, mem->bo, false);
      device->ws->buffer_destroy(device->ws, mem->bo);
      mem->bo = NULL;
   }

   radv_device_memory_finish(mem);
   vk_free2(&device->vk.alloc, pAllocator, mem);
}

static VkResult
radv_alloc_memory(struct radv_device *device, const VkMemoryAllocateInfo *pAllocateInfo,
                  const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMem)
{
   struct radv_device_memory *mem;
   VkResult result;
   enum radeon_bo_domain domain;
   uint32_t flags = 0;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   const VkImportMemoryFdInfoKHR *import_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);
   const VkMemoryDedicatedAllocateInfo *dedicate_info =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
   const VkExportMemoryAllocateInfo *export_info =
      vk_find_struct_const(pAllocateInfo->pNext, EXPORT_MEMORY_ALLOCATE_INFO);
   const struct VkImportAndroidHardwareBufferInfoANDROID *ahb_import_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID);
   const VkImportMemoryHostPointerInfoEXT *host_ptr_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_HOST_POINTER_INFO_EXT);

   const struct wsi_memory_allocate_info *wsi_info =
      vk_find_struct_const(pAllocateInfo->pNext, WSI_MEMORY_ALLOCATE_INFO_MESA);

   if (pAllocateInfo->allocationSize == 0 && !ahb_import_info &&
       !(export_info && (export_info->handleTypes &
                         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID))) {
      /* Apparently, this is allowed */
      *pMem = VK_NULL_HANDLE;
      return VK_SUCCESS;
   }

   mem =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*mem), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_device_memory_init(mem, device, NULL);

   if (wsi_info) {
      if(wsi_info->implicit_sync)
         flags |= RADEON_FLAG_IMPLICIT_SYNC;

      /* In case of prime, linear buffer is allocated in default heap which is VRAM.
       * Due to this when display is connected to iGPU and render on dGPU, ddx
       * function amdgpu_present_check_flip() fails due to which there is blit
       * instead of flip. Setting the flag RADEON_FLAG_GTT_WC allows kernel to
       * allocate GTT memory in supported hardware where GTT can be directly scanout.
       * Using wsi_info variable check to set the flag RADEON_FLAG_GTT_WC so that
       * only for memory allocated by driver this flag is set.
       */
      flags |= RADEON_FLAG_GTT_WC;
   }

   if (dedicate_info) {
      mem->image = radv_image_from_handle(dedicate_info->image);
      mem->buffer = radv_buffer_from_handle(dedicate_info->buffer);
   } else {
      mem->image = NULL;
      mem->buffer = NULL;
   }

   if (wsi_info && wsi_info->implicit_sync && mem->buffer) {
      /* Mark the linear prime buffer (aka the destination of the prime blit
       * as uncached.
       */
      flags |= RADEON_FLAG_VA_UNCACHED;
   }

   float priority_float = 0.5;
   const struct VkMemoryPriorityAllocateInfoEXT *priority_ext =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_PRIORITY_ALLOCATE_INFO_EXT);
   if (priority_ext)
      priority_float = priority_ext->priority;

   uint64_t replay_address = 0;
   const VkMemoryOpaqueCaptureAddressAllocateInfo *replay_info =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO);
   if (replay_info && replay_info->opaqueCaptureAddress)
      replay_address = replay_info->opaqueCaptureAddress;

   unsigned priority = MIN2(RADV_BO_PRIORITY_APPLICATION_MAX - 1,
                            (int)(priority_float * RADV_BO_PRIORITY_APPLICATION_MAX));

   mem->user_ptr = NULL;

#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
   mem->android_hardware_buffer = NULL;
#endif

   if (ahb_import_info) {
      result = radv_import_ahb_memory(device, mem, priority, ahb_import_info);
      if (result != VK_SUCCESS)
         goto fail;
   } else if (export_info && (export_info->handleTypes &
                              VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)) {
      result = radv_create_ahb_memory(device, mem, priority, pAllocateInfo);
      if (result != VK_SUCCESS)
         goto fail;
   } else if (import_info) {
      assert(import_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
             import_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
      result = device->ws->buffer_from_fd(device->ws, import_info->fd, priority, &mem->bo, NULL);
      if (result != VK_SUCCESS) {
         goto fail;
      } else {
         close(import_info->fd);
      }

      if (mem->image && mem->image->plane_count == 1 &&
          !vk_format_is_depth_or_stencil(mem->image->vk.format) && mem->image->info.samples == 1 &&
          mem->image->vk.tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
         struct radeon_bo_metadata metadata;
         device->ws->buffer_get_metadata(device->ws, mem->bo, &metadata);

         struct radv_image_create_info create_info = {.no_metadata_planes = true,
                                                      .bo_metadata = &metadata};

         /* This gives a basic ability to import radeonsi images
          * that don't have DCC. This is not guaranteed by any
          * spec and can be removed after we support modifiers. */
         result = radv_image_create_layout(device, create_info, NULL, mem->image);
         if (result != VK_SUCCESS) {
            device->ws->buffer_destroy(device->ws, mem->bo);
            goto fail;
         }
      }
   } else if (host_ptr_info) {
      assert(host_ptr_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);
      result = device->ws->buffer_from_ptr(device->ws, host_ptr_info->pHostPointer,
                                           pAllocateInfo->allocationSize, priority, &mem->bo);
      if (result != VK_SUCCESS) {
         goto fail;
      } else {
         mem->user_ptr = host_ptr_info->pHostPointer;
      }
   } else {
      uint64_t alloc_size = align_u64(pAllocateInfo->allocationSize, 4096);
      uint32_t heap_index;

      heap_index =
         device->physical_device->memory_properties.memoryTypes[pAllocateInfo->memoryTypeIndex]
            .heapIndex;
      domain = device->physical_device->memory_domains[pAllocateInfo->memoryTypeIndex];
      flags |= device->physical_device->memory_flags[pAllocateInfo->memoryTypeIndex];

      if (!import_info && (!export_info || !export_info->handleTypes)) {
         flags |= RADEON_FLAG_NO_INTERPROCESS_SHARING;
         if (device->use_global_bo_list) {
            flags |= RADEON_FLAG_PREFER_LOCAL_BO;
         }
      }

      const VkMemoryAllocateFlagsInfo *flags_info = vk_find_struct_const(pAllocateInfo->pNext, MEMORY_ALLOCATE_FLAGS_INFO);
      if (flags_info && flags_info->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)
         flags |= RADEON_FLAG_REPLAYABLE;

      if (device->instance->zero_vram)
         flags |= RADEON_FLAG_ZERO_VRAM;

      if (device->overallocation_disallowed) {
         uint64_t total_size =
            device->physical_device->memory_properties.memoryHeaps[heap_index].size;

         mtx_lock(&device->overallocation_mutex);
         if (device->allocated_memory_size[heap_index] + alloc_size > total_size) {
            mtx_unlock(&device->overallocation_mutex);
            result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
            goto fail;
         }
         device->allocated_memory_size[heap_index] += alloc_size;
         mtx_unlock(&device->overallocation_mutex);
      }

      result = device->ws->buffer_create(device->ws, alloc_size,
                                         device->physical_device->rad_info.max_alignment, domain,
                                         flags, priority, replay_address, &mem->bo);

      if (result != VK_SUCCESS) {
         if (device->overallocation_disallowed) {
            mtx_lock(&device->overallocation_mutex);
            device->allocated_memory_size[heap_index] -= alloc_size;
            mtx_unlock(&device->overallocation_mutex);
         }
         goto fail;
      }

      mem->heap_index = heap_index;
      mem->alloc_size = alloc_size;
   }

   if (!wsi_info) {
      if (device->use_global_bo_list) {
         result = device->ws->buffer_make_resident(device->ws, mem->bo, true);
         if (result != VK_SUCCESS)
            goto fail;
      }
   }

   *pMem = radv_device_memory_to_handle(mem);

   return VK_SUCCESS;

fail:
   radv_free_memory(device, pAllocator, mem);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_AllocateMemory(VkDevice _device, const VkMemoryAllocateInfo *pAllocateInfo,
                    const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMem)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   return radv_alloc_memory(device, pAllocateInfo, pAllocator, pMem);
}

VKAPI_ATTR void VKAPI_CALL
radv_FreeMemory(VkDevice _device, VkDeviceMemory _mem, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, mem, _mem);

   radv_free_memory(device, pAllocator, mem);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_MapMemory(VkDevice _device, VkDeviceMemory _memory, VkDeviceSize offset, VkDeviceSize size,
               VkMemoryMapFlags flags, void **ppData)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, mem, _memory);

   if (mem->user_ptr)
      *ppData = mem->user_ptr;
   else
      *ppData = device->ws->buffer_map(mem->bo);

   if (*ppData) {
      *ppData = (uint8_t *)*ppData + offset;
      return VK_SUCCESS;
   }

   return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);
}

VKAPI_ATTR void VKAPI_CALL
radv_UnmapMemory(VkDevice _device, VkDeviceMemory _memory)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, mem, _memory);

   if (mem->user_ptr == NULL)
      device->ws->buffer_unmap(mem->bo);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_FlushMappedMemoryRanges(VkDevice _device, uint32_t memoryRangeCount,
                             const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_InvalidateMappedMemoryRanges(VkDevice _device, uint32_t memoryRangeCount,
                                  const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

static void
radv_get_buffer_memory_requirements(struct radv_device *device, VkDeviceSize size,
                                    VkBufferCreateFlags flags, VkBufferCreateFlags usage,
                                    VkMemoryRequirements2 *pMemoryRequirements)
{
   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      ((1u << device->physical_device->memory_properties.memoryTypeCount) - 1u) &
      ~device->physical_device->memory_types_32bit;

   /* Allow 32-bit address-space for DGC usage, as this buffer will contain
    * cmd buffer upload buffers, and those get passed to shaders through 32-bit
    * pointers.
    *
    * We only allow it with this usage set, to "protect" the 32-bit address space
    * from being overused. The actual requirement is done as part of
    * vkGetGeneratedCommandsMemoryRequirementsNV. (we have to make sure their
    * intersection is non-zero at least)
    */
   if ((usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) && device->uses_device_generated_commands)
      pMemoryRequirements->memoryRequirements.memoryTypeBits |=
         device->physical_device->memory_types_32bit;

   if (flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
      pMemoryRequirements->memoryRequirements.alignment = 4096;
   else
      pMemoryRequirements->memoryRequirements.alignment = 16;

   /* Top level acceleration structures need the bottom 6 bits to store
    * the root ids of instances. The hardware also needs bvh nodes to
    * be 64 byte aligned.
    */
   if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
      pMemoryRequirements->memoryRequirements.alignment =
         MAX2(pMemoryRequirements->memoryRequirements.alignment, 64);

   pMemoryRequirements->memoryRequirements.size =
      align64(size, pMemoryRequirements->memoryRequirements.alignment);

   vk_foreach_struct(ext, pMemoryRequirements->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req = (VkMemoryDedicatedRequirements *)ext;
         req->requiresDedicatedAllocation = false;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_GetBufferMemoryRequirements2(VkDevice _device, const VkBufferMemoryRequirementsInfo2 *pInfo,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);

   radv_get_buffer_memory_requirements(device, buffer->vk.size, buffer->vk.create_flags,
                                       buffer->vk.usage, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceBufferMemoryRequirements(VkDevice _device,
                                       const VkDeviceBufferMemoryRequirements *pInfo,
                                       VkMemoryRequirements2 *pMemoryRequirements)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   radv_get_buffer_memory_requirements(device, pInfo->pCreateInfo->size, pInfo->pCreateInfo->flags,
                                       pInfo->pCreateInfo->usage, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetImageMemoryRequirements2(VkDevice _device, const VkImageMemoryRequirementsInfo2 *pInfo,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_image, image, pInfo->image);

   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      ((1u << device->physical_device->memory_properties.memoryTypeCount) - 1u) &
      ~device->physical_device->memory_types_32bit;

   pMemoryRequirements->memoryRequirements.size = image->size;
   pMemoryRequirements->memoryRequirements.alignment = image->alignment;

   vk_foreach_struct(ext, pMemoryRequirements->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req = (VkMemoryDedicatedRequirements *)ext;
         req->requiresDedicatedAllocation =
            image->shareable && image->vk.tiling != VK_IMAGE_TILING_LINEAR;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceImageMemoryRequirements(VkDevice device,
                                      const VkDeviceImageMemoryRequirements *pInfo,
                                      VkMemoryRequirements2 *pMemoryRequirements)
{
   UNUSED VkResult result;
   VkImage image;

   /* Determining the image size/alignment require to create a surface, which is complicated without
    * creating an image.
    * TODO: Avoid creating an image.
    */
   result = radv_CreateImage(device, pInfo->pCreateInfo, NULL, &image);
   assert(result == VK_SUCCESS);

   VkImageMemoryRequirementsInfo2 info2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image,
   };

   radv_GetImageMemoryRequirements2(device, &info2, pMemoryRequirements);

   radv_DestroyImage(device, image, NULL);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                               VkDeviceSize *pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_BindBufferMemory2(VkDevice _device, uint32_t bindInfoCount,
                       const VkBindBufferMemoryInfo *pBindInfos)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      RADV_FROM_HANDLE(radv_device_memory, mem, pBindInfos[i].memory);
      RADV_FROM_HANDLE(radv_buffer, buffer, pBindInfos[i].buffer);

      if (mem->alloc_size) {
         VkBufferMemoryRequirementsInfo2 info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
            .buffer = pBindInfos[i].buffer,
         };
         VkMemoryRequirements2 reqs = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
         };

         radv_GetBufferMemoryRequirements2(_device, &info, &reqs);

         if (pBindInfos[i].memoryOffset + reqs.memoryRequirements.size > mem->alloc_size) {
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "Device memory object too small for the buffer.\n");
         }
      }

      buffer->bo = mem->bo;
      buffer->offset = pBindInfos[i].memoryOffset;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_BindImageMemory2(VkDevice _device, uint32_t bindInfoCount,
                      const VkBindImageMemoryInfo *pBindInfos)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      RADV_FROM_HANDLE(radv_device_memory, mem, pBindInfos[i].memory);
      RADV_FROM_HANDLE(radv_image, image, pBindInfos[i].image);

      if (mem->alloc_size) {
         VkImageMemoryRequirementsInfo2 info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .image = pBindInfos[i].image,
         };
         VkMemoryRequirements2 reqs = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
         };

         radv_GetImageMemoryRequirements2(_device, &info, &reqs);

         if (pBindInfos[i].memoryOffset + reqs.memoryRequirements.size > mem->alloc_size) {
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "Device memory object too small for the image.\n");
         }
      }

      if (image->disjoint) {
         const VkBindImagePlaneMemoryInfo *plane_info =
            vk_find_struct_const(pBindInfos[i].pNext, BIND_IMAGE_PLANE_MEMORY_INFO);

         switch (plane_info->planeAspect) {
            case VK_IMAGE_ASPECT_PLANE_0_BIT:
               image->bindings[0].bo = mem->bo;
               image->bindings[0].offset = pBindInfos[i].memoryOffset;
               break;
            case VK_IMAGE_ASPECT_PLANE_1_BIT:
               image->bindings[1].bo = mem->bo;
               image->bindings[1].offset = pBindInfos[i].memoryOffset;
               break;
            case VK_IMAGE_ASPECT_PLANE_2_BIT:
               image->bindings[2].bo = mem->bo;
               image->bindings[2].offset = pBindInfos[i].memoryOffset;
               break;
            default:
               break;
         }
      } else {
         image->bindings[0].bo = mem->bo;
         image->bindings[0].offset = pBindInfos[i].memoryOffset;
      }
   }
   return VK_SUCCESS;
}

static void
radv_destroy_event(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                   struct radv_event *event)
{
   if (event->bo)
      device->ws->buffer_destroy(device->ws, event->bo);

   vk_object_base_finish(&event->base);
   vk_free2(&device->vk.alloc, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateEvent(VkDevice _device, const VkEventCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   enum radeon_bo_domain bo_domain;
   enum radeon_bo_flag bo_flags;
   struct radv_event *event;
   VkResult result;

   event = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*event), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!event)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &event->base, VK_OBJECT_TYPE_EVENT);

   if (pCreateInfo->flags & VK_EVENT_CREATE_DEVICE_ONLY_BIT) {
      bo_domain = RADEON_DOMAIN_VRAM;
      bo_flags = RADEON_FLAG_NO_CPU_ACCESS;
   } else {
      bo_domain = RADEON_DOMAIN_GTT;
      bo_flags = RADEON_FLAG_CPU_ACCESS;
   }

   result = device->ws->buffer_create(
      device->ws, 8, 8, bo_domain,
      RADEON_FLAG_VA_UNCACHED | RADEON_FLAG_NO_INTERPROCESS_SHARING | bo_flags,
      RADV_BO_PRIORITY_FENCE, 0, &event->bo);
   if (result != VK_SUCCESS) {
      radv_destroy_event(device, pAllocator, event);
      return vk_error(device, result);
   }

   if (!(pCreateInfo->flags & VK_EVENT_CREATE_DEVICE_ONLY_BIT)) {
      event->map = (uint64_t *)device->ws->buffer_map(event->bo);
      if (!event->map) {
         radv_destroy_event(device, pAllocator, event);
         return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }
   }

   *pEvent = radv_event_to_handle(event);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyEvent(VkDevice _device, VkEvent _event, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_event, event, _event);

   if (!event)
      return;

   radv_destroy_event(device, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetEventStatus(VkDevice _device, VkEvent _event)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_event, event, _event);

   if (vk_device_is_lost(&device->vk))
      return VK_ERROR_DEVICE_LOST;

   if (*event->map == 1)
      return VK_EVENT_SET;
   return VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_SetEvent(VkDevice _device, VkEvent _event)
{
   RADV_FROM_HANDLE(radv_event, event, _event);
   *event->map = 1;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_ResetEvent(VkDevice _device, VkEvent _event)
{
   RADV_FROM_HANDLE(radv_event, event, _event);
   *event->map = 0;

   return VK_SUCCESS;
}

void
radv_buffer_init(struct radv_buffer *buffer, struct radv_device *device,
                 struct radeon_winsys_bo *bo, uint64_t size,
                 uint64_t offset)
{
   VkBufferCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
   };

   vk_buffer_init(&device->vk, &buffer->vk, &createInfo);

   buffer->bo = bo;
   buffer->offset = offset;
}

void
radv_buffer_finish(struct radv_buffer *buffer)
{
   vk_buffer_finish(&buffer->vk);
}

static void
radv_destroy_buffer(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                    struct radv_buffer *buffer)
{
   if ((buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) && buffer->bo)
      device->ws->buffer_destroy(device->ws, buffer->bo);

   radv_buffer_finish(buffer);
   vk_free2(&device->vk.alloc, pAllocator, buffer);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateBuffer(VkDevice _device, const VkBufferCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

   buffer = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*buffer), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_buffer_init(&device->vk, &buffer->vk, pCreateInfo);
   buffer->bo = NULL;
   buffer->offset = 0;

   if (pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      enum radeon_bo_flag flags = RADEON_FLAG_VIRTUAL;
      if (pCreateInfo->flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)
         flags |= RADEON_FLAG_REPLAYABLE;

      uint64_t replay_address = 0;
      const VkBufferOpaqueCaptureAddressCreateInfo *replay_info =
         vk_find_struct_const(pCreateInfo->pNext, BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO);
      if (replay_info && replay_info->opaqueCaptureAddress)
         replay_address = replay_info->opaqueCaptureAddress;

      VkResult result =
         device->ws->buffer_create(device->ws, align64(buffer->vk.size, 4096), 4096, 0, flags,
                                   RADV_BO_PRIORITY_VIRTUAL, replay_address, &buffer->bo);
      if (result != VK_SUCCESS) {
         radv_destroy_buffer(device, pAllocator, buffer);
         return vk_error(device, result);
      }
   }

   *pBuffer = radv_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyBuffer(VkDevice _device, VkBuffer _buffer, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);

   if (!buffer)
      return;

   radv_destroy_buffer(device, pAllocator, buffer);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
radv_GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);
   return radv_buffer_get_va(buffer->bo) + buffer->offset;
}

VKAPI_ATTR uint64_t VKAPI_CALL
radv_GetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);
   return buffer->bo ? radv_buffer_get_va(buffer->bo) + buffer->offset : 0;
}

VKAPI_ATTR uint64_t VKAPI_CALL
radv_GetDeviceMemoryOpaqueCaptureAddress(VkDevice device,
                                         const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
   RADV_FROM_HANDLE(radv_device_memory, mem, pInfo->memory);
   return radv_buffer_get_va(mem->bo);
}

static inline unsigned
si_tile_mode_index(const struct radv_image_plane *plane, unsigned level, bool stencil)
{
   if (stencil)
      return plane->surface.u.legacy.zs.stencil_tiling_index[level];
   else
      return plane->surface.u.legacy.tiling_index[level];
}

static uint32_t
radv_surface_max_layer_count(struct radv_image_view *iview)
{
   return iview->vk.view_type == VK_IMAGE_VIEW_TYPE_3D ? iview->extent.depth
                                                       : (iview->vk.base_array_layer + iview->vk.layer_count);
}

static unsigned
get_dcc_max_uncompressed_block_size(const struct radv_device *device,
                                    const struct radv_image_view *iview)
{
   if (device->physical_device->rad_info.gfx_level < GFX10 && iview->image->info.samples > 1) {
      if (iview->image->planes[0].surface.bpe == 1)
         return V_028C78_MAX_BLOCK_SIZE_64B;
      else if (iview->image->planes[0].surface.bpe == 2)
         return V_028C78_MAX_BLOCK_SIZE_128B;
   }

   return V_028C78_MAX_BLOCK_SIZE_256B;
}

static unsigned
get_dcc_min_compressed_block_size(const struct radv_device *device)
{
   if (!device->physical_device->rad_info.has_dedicated_vram) {
      /* amdvlk: [min-compressed-block-size] should be set to 32 for
       * dGPU and 64 for APU because all of our APUs to date use
       * DIMMs which have a request granularity size of 64B while all
       * other chips have a 32B request size.
       */
      return V_028C78_MIN_BLOCK_SIZE_64B;
   }

   return V_028C78_MIN_BLOCK_SIZE_32B;
}

static uint32_t
radv_init_dcc_control_reg(struct radv_device *device, struct radv_image_view *iview)
{
   unsigned max_uncompressed_block_size = get_dcc_max_uncompressed_block_size(device, iview);
   unsigned min_compressed_block_size = get_dcc_min_compressed_block_size(device);
   unsigned max_compressed_block_size;
   unsigned independent_128b_blocks;
   unsigned independent_64b_blocks;

   if (!radv_dcc_enabled(iview->image, iview->vk.base_mip_level))
      return 0;

   /* For GFX9+ ac_surface computes values for us (except min_compressed
    * and max_uncompressed) */
   if (device->physical_device->rad_info.gfx_level >= GFX9) {
      max_compressed_block_size =
         iview->image->planes[0].surface.u.gfx9.color.dcc.max_compressed_block_size;
      independent_128b_blocks = iview->image->planes[0].surface.u.gfx9.color.dcc.independent_128B_blocks;
      independent_64b_blocks = iview->image->planes[0].surface.u.gfx9.color.dcc.independent_64B_blocks;
   } else {
      independent_128b_blocks = 0;

      if (iview->image->vk.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
         /* If this DCC image is potentially going to be used in texture
          * fetches, we need some special settings.
          */
         independent_64b_blocks = 1;
         max_compressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
      } else {
         /* MAX_UNCOMPRESSED_BLOCK_SIZE must be >=
          * MAX_COMPRESSED_BLOCK_SIZE. Set MAX_COMPRESSED_BLOCK_SIZE as
          * big as possible for better compression state.
          */
         independent_64b_blocks = 0;
         max_compressed_block_size = max_uncompressed_block_size;
      }
   }

   uint32_t result = S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(max_uncompressed_block_size) |
                     S_028C78_MAX_COMPRESSED_BLOCK_SIZE(max_compressed_block_size) |
                     S_028C78_MIN_COMPRESSED_BLOCK_SIZE(min_compressed_block_size) |
                     S_028C78_INDEPENDENT_64B_BLOCKS(independent_64b_blocks);

   if (device->physical_device->rad_info.gfx_level >= GFX11) {
      result |= S_028C78_INDEPENDENT_128B_BLOCKS_GFX11(independent_128b_blocks) |
                S_028C78_DISABLE_CONSTANT_ENCODE_REG(1) |
                S_028C78_FDCC_ENABLE(radv_dcc_enabled(iview->image, iview->vk.base_mip_level));
   } else {
      result |= S_028C78_INDEPENDENT_128B_BLOCKS_GFX10(independent_128b_blocks);
   }

   return result;
}

void
radv_initialise_color_surface(struct radv_device *device, struct radv_color_buffer_info *cb,
                              struct radv_image_view *iview)
{
   const struct util_format_description *desc;
   unsigned ntype, format, swap, endian;
   unsigned blend_clamp = 0, blend_bypass = 0;
   uint64_t va;
   const struct radv_image_plane *plane = &iview->image->planes[iview->plane_id];
   const struct radeon_surf *surf = &plane->surface;

   desc = vk_format_description(iview->vk.format);

   memset(cb, 0, sizeof(*cb));

   /* Intensity is implemented as Red, so treat it that way. */
   if (device->physical_device->rad_info.gfx_level >= GFX11)
      cb->cb_color_attrib = S_028C74_FORCE_DST_ALPHA_1_GFX11(desc->swizzle[3] == PIPE_SWIZZLE_1);
   else
      cb->cb_color_attrib = S_028C74_FORCE_DST_ALPHA_1_GFX6(desc->swizzle[3] == PIPE_SWIZZLE_1);

   uint32_t plane_id = iview->image->disjoint ? iview->plane_id : 0;
   va = radv_buffer_get_va(iview->image->bindings[plane_id].bo) +
      iview->image->bindings[plane_id].offset;

   cb->cb_color_base = va >> 8;

   if (device->physical_device->rad_info.gfx_level >= GFX9) {
      if (device->physical_device->rad_info.gfx_level >= GFX11) {
         cb->cb_color_attrib3 |= S_028EE0_COLOR_SW_MODE(surf->u.gfx9.swizzle_mode) |
                                 S_028EE0_DCC_PIPE_ALIGNED(surf->u.gfx9.color.dcc.pipe_aligned);
      } else if (device->physical_device->rad_info.gfx_level >= GFX10) {
         cb->cb_color_attrib3 |= S_028EE0_COLOR_SW_MODE(surf->u.gfx9.swizzle_mode) |
                                 S_028EE0_FMASK_SW_MODE(surf->u.gfx9.color.fmask_swizzle_mode) |
                                 S_028EE0_CMASK_PIPE_ALIGNED(1) |
                                 S_028EE0_DCC_PIPE_ALIGNED(surf->u.gfx9.color.dcc.pipe_aligned);
      } else {
         struct gfx9_surf_meta_flags meta = {
            .rb_aligned = 1,
            .pipe_aligned = 1,
         };

         if (surf->meta_offset)
            meta = surf->u.gfx9.color.dcc;

         cb->cb_color_attrib |= S_028C74_COLOR_SW_MODE(surf->u.gfx9.swizzle_mode) |
                                S_028C74_FMASK_SW_MODE(surf->u.gfx9.color.fmask_swizzle_mode) |
                                S_028C74_RB_ALIGNED(meta.rb_aligned) |
                                S_028C74_PIPE_ALIGNED(meta.pipe_aligned);
         cb->cb_mrt_epitch = S_0287A0_EPITCH(surf->u.gfx9.epitch);
      }

      cb->cb_color_base += surf->u.gfx9.surf_offset >> 8;
      cb->cb_color_base |= surf->tile_swizzle;
   } else {
      const struct legacy_surf_level *level_info = &surf->u.legacy.level[iview->vk.base_mip_level];
      unsigned pitch_tile_max, slice_tile_max, tile_mode_index;

      cb->cb_color_base += level_info->offset_256B;
      if (level_info->mode == RADEON_SURF_MODE_2D)
         cb->cb_color_base |= surf->tile_swizzle;

      pitch_tile_max = level_info->nblk_x / 8 - 1;
      slice_tile_max = (level_info->nblk_x * level_info->nblk_y) / 64 - 1;
      tile_mode_index = si_tile_mode_index(plane, iview->vk.base_mip_level, false);

      cb->cb_color_pitch = S_028C64_TILE_MAX(pitch_tile_max);
      cb->cb_color_slice = S_028C68_TILE_MAX(slice_tile_max);
      cb->cb_color_cmask_slice = surf->u.legacy.color.cmask_slice_tile_max;

      cb->cb_color_attrib |= S_028C74_TILE_MODE_INDEX(tile_mode_index);

      if (radv_image_has_fmask(iview->image)) {
         if (device->physical_device->rad_info.gfx_level >= GFX7)
            cb->cb_color_pitch |=
               S_028C64_FMASK_TILE_MAX(surf->u.legacy.color.fmask.pitch_in_pixels / 8 - 1);
         cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(surf->u.legacy.color.fmask.tiling_index);
         cb->cb_color_fmask_slice = S_028C88_TILE_MAX(surf->u.legacy.color.fmask.slice_tile_max);
      } else {
         /* This must be set for fast clear to work without FMASK. */
         if (device->physical_device->rad_info.gfx_level >= GFX7)
            cb->cb_color_pitch |= S_028C64_FMASK_TILE_MAX(pitch_tile_max);
         cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(tile_mode_index);
         cb->cb_color_fmask_slice = S_028C88_TILE_MAX(slice_tile_max);
      }
   }

   /* CMASK variables */
   va = radv_buffer_get_va(iview->image->bindings[0].bo) + iview->image->bindings[0].offset;
   va += surf->cmask_offset;
   cb->cb_color_cmask = va >> 8;

   va = radv_buffer_get_va(iview->image->bindings[0].bo) + iview->image->bindings[0].offset;
   va += surf->meta_offset;

   if (radv_dcc_enabled(iview->image, iview->vk.base_mip_level) &&
       device->physical_device->rad_info.gfx_level <= GFX8)
      va += plane->surface.u.legacy.color.dcc_level[iview->vk.base_mip_level].dcc_offset;

   unsigned dcc_tile_swizzle = surf->tile_swizzle;
   dcc_tile_swizzle &= ((1 << surf->meta_alignment_log2) - 1) >> 8;

   cb->cb_dcc_base = va >> 8;
   cb->cb_dcc_base |= dcc_tile_swizzle;

   /* GFX10 field has the same base shift as the GFX6 field. */
   uint32_t max_slice = radv_surface_max_layer_count(iview) - 1;
   cb->cb_color_view =
      S_028C6C_SLICE_START(iview->vk.base_array_layer) | S_028C6C_SLICE_MAX_GFX10(max_slice);

   if (iview->image->info.samples > 1) {
      unsigned log_samples = util_logbase2(iview->image->info.samples);

      if (device->physical_device->rad_info.gfx_level >= GFX11)
         cb->cb_color_attrib |= S_028C74_NUM_FRAGMENTS_GFX11(log_samples);
      else
         cb->cb_color_attrib |=
            S_028C74_NUM_SAMPLES(log_samples) | S_028C74_NUM_FRAGMENTS_GFX6(log_samples);
   }

   if (radv_image_has_fmask(iview->image)) {
      va = radv_buffer_get_va(iview->image->bindings[0].bo) + iview->image->bindings[0].offset +
         surf->fmask_offset;
      cb->cb_color_fmask = va >> 8;
      cb->cb_color_fmask |= surf->fmask_tile_swizzle;
   } else {
      cb->cb_color_fmask = cb->cb_color_base;
   }

   ntype = radv_translate_color_numformat(iview->vk.format, desc,
                                          vk_format_get_first_non_void_channel(iview->vk.format));
   format = radv_translate_colorformat(iview->vk.format);
   assert(format != V_028C70_COLOR_INVALID);

   swap = radv_translate_colorswap(iview->vk.format, false);
   endian = radv_colorformat_endian_swap(format);

   /* blend clamp should be set for all NORM/SRGB types */
   if (ntype == V_028C70_NUMBER_UNORM || ntype == V_028C70_NUMBER_SNORM ||
       ntype == V_028C70_NUMBER_SRGB)
      blend_clamp = 1;

   /* set blend bypass according to docs if SINT/UINT or
      8/24 COLOR variants */
   if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT ||
       format == V_028C70_COLOR_8_24 || format == V_028C70_COLOR_24_8 ||
       format == V_028C70_COLOR_X24_8_32_FLOAT) {
      blend_clamp = 0;
      blend_bypass = 1;
   }
#if 0
	if ((ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT) &&
	    (format == V_028C70_COLOR_8 ||
	     format == V_028C70_COLOR_8_8 ||
	     format == V_028C70_COLOR_8_8_8_8))
		->color_is_int8 = true;
#endif
   cb->cb_color_info =
      S_028C70_COMP_SWAP(swap) | S_028C70_BLEND_CLAMP(blend_clamp) |
      S_028C70_BLEND_BYPASS(blend_bypass) | S_028C70_SIMPLE_FLOAT(1) |
      S_028C70_ROUND_MODE(ntype != V_028C70_NUMBER_UNORM && ntype != V_028C70_NUMBER_SNORM &&
                          ntype != V_028C70_NUMBER_SRGB && format != V_028C70_COLOR_8_24 &&
                          format != V_028C70_COLOR_24_8) |
      S_028C70_NUMBER_TYPE(ntype);

   if (device->physical_device->rad_info.gfx_level >= GFX11)
      cb->cb_color_info |= S_028C70_FORMAT_GFX11(format);
   else
      cb->cb_color_info |= S_028C70_FORMAT_GFX6(format) | S_028C70_ENDIAN(endian);

   if (radv_image_has_fmask(iview->image)) {
      cb->cb_color_info |= S_028C70_COMPRESSION(1);
      if (device->physical_device->rad_info.gfx_level == GFX6) {
         unsigned fmask_bankh = util_logbase2(surf->u.legacy.color.fmask.bankh);
         cb->cb_color_attrib |= S_028C74_FMASK_BANK_HEIGHT(fmask_bankh);
      }

      if (radv_image_is_tc_compat_cmask(iview->image)) {
         /* Allow the texture block to read FMASK directly
          * without decompressing it. This bit must be cleared
          * when performing FMASK_DECOMPRESS or DCC_COMPRESS,
          * otherwise the operation doesn't happen.
          */
         cb->cb_color_info |= S_028C70_FMASK_COMPRESS_1FRAG_ONLY(1);

         if (device->physical_device->rad_info.gfx_level == GFX8) {
            /* Set CMASK into a tiling format that allows
             * the texture block to read it.
             */
            cb->cb_color_info |= S_028C70_CMASK_ADDR_TYPE(2);
         }
      }
   }

   if (radv_image_has_cmask(iview->image) &&
       !(device->instance->debug_flags & RADV_DEBUG_NO_FAST_CLEARS))
      cb->cb_color_info |= S_028C70_FAST_CLEAR(1);

   if (radv_dcc_enabled(iview->image, iview->vk.base_mip_level) && !iview->disable_dcc_mrt &&
       device->physical_device->rad_info.gfx_level < GFX11)
      cb->cb_color_info |= S_028C70_DCC_ENABLE(1);

   cb->cb_dcc_control = radv_init_dcc_control_reg(device, iview);

   /* This must be set for fast clear to work without FMASK. */
   if (!radv_image_has_fmask(iview->image) && device->physical_device->rad_info.gfx_level == GFX6) {
      unsigned bankh = util_logbase2(surf->u.legacy.bankh);
      cb->cb_color_attrib |= S_028C74_FMASK_BANK_HEIGHT(bankh);
   }

   if (device->physical_device->rad_info.gfx_level >= GFX9) {
      unsigned mip0_depth = iview->image->vk.image_type == VK_IMAGE_TYPE_3D
                               ? (iview->extent.depth - 1)
                               : (iview->image->info.array_size - 1);
      unsigned width =
         vk_format_get_plane_width(iview->image->vk.format, iview->plane_id, iview->extent.width);
      unsigned height =
         vk_format_get_plane_height(iview->image->vk.format, iview->plane_id, iview->extent.height);

      if (device->physical_device->rad_info.gfx_level >= GFX10) {
         cb->cb_color_view |= S_028C6C_MIP_LEVEL_GFX10(iview->vk.base_mip_level);

         cb->cb_color_attrib3 |=
            S_028EE0_MIP0_DEPTH(mip0_depth) | S_028EE0_RESOURCE_TYPE(surf->u.gfx9.resource_type) |
            S_028EE0_RESOURCE_LEVEL(device->physical_device->rad_info.gfx_level >= GFX11 ? 0 : 1);
      } else {
         cb->cb_color_view |= S_028C6C_MIP_LEVEL_GFX9(iview->vk.base_mip_level);
         cb->cb_color_attrib |=
            S_028C74_MIP0_DEPTH(mip0_depth) | S_028C74_RESOURCE_TYPE(surf->u.gfx9.resource_type);
      }

      cb->cb_color_attrib2 = S_028C68_MIP0_WIDTH(width - 1) | S_028C68_MIP0_HEIGHT(height - 1) |
                             S_028C68_MAX_MIP(iview->image->info.levels - 1);
   }
}

static unsigned
radv_calc_decompress_on_z_planes(struct radv_device *device, struct radv_image_view *iview)
{
   unsigned max_zplanes = 0;

   assert(radv_image_is_tc_compat_htile(iview->image));

   if (device->physical_device->rad_info.gfx_level >= GFX9) {
      /* Default value for 32-bit depth surfaces. */
      max_zplanes = 4;

      if (iview->vk.format == VK_FORMAT_D16_UNORM && iview->image->info.samples > 1)
         max_zplanes = 2;

      /* Workaround for a DB hang when ITERATE_256 is set to 1. Only affects 4X MSAA D/S images. */
      if (device->physical_device->rad_info.has_two_planes_iterate256_bug &&
          radv_image_get_iterate256(device, iview->image) &&
          !radv_image_tile_stencil_disabled(device, iview->image) &&
          iview->image->info.samples == 4) {
         max_zplanes = 1;
      }

      max_zplanes = max_zplanes + 1;
   } else {
      if (iview->vk.format == VK_FORMAT_D16_UNORM) {
         /* Do not enable Z plane compression for 16-bit depth
          * surfaces because isn't supported on GFX8. Only
          * 32-bit depth surfaces are supported by the hardware.
          * This allows to maintain shader compatibility and to
          * reduce the number of depth decompressions.
          */
         max_zplanes = 1;
      } else {
         if (iview->image->info.samples <= 1)
            max_zplanes = 5;
         else if (iview->image->info.samples <= 4)
            max_zplanes = 3;
         else
            max_zplanes = 2;
      }
   }

   return max_zplanes;
}

void
radv_initialise_vrs_surface(struct radv_image *image, struct radv_buffer *htile_buffer,
                            struct radv_ds_buffer_info *ds)
{
   const struct radeon_surf *surf = &image->planes[0].surface;

   assert(image->vk.format == VK_FORMAT_D16_UNORM);
   memset(ds, 0, sizeof(*ds));

   ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-16);

   ds->db_z_info = S_028038_FORMAT(V_028040_Z_16) |
                   S_028038_SW_MODE(surf->u.gfx9.swizzle_mode) |
                   S_028038_ZRANGE_PRECISION(1) |
                   S_028038_TILE_SURFACE_ENABLE(1);
   ds->db_stencil_info = S_02803C_FORMAT(V_028044_STENCIL_INVALID);

   ds->db_depth_size = S_02801C_X_MAX(image->info.width - 1) |
                       S_02801C_Y_MAX(image->info.height - 1);

   ds->db_htile_data_base = radv_buffer_get_va(htile_buffer->bo) >> 8;
   ds->db_htile_surface = S_028ABC_FULL_CACHE(1) | S_028ABC_PIPE_ALIGNED(1) |
                          S_028ABC_VRS_HTILE_ENCODING(V_028ABC_VRS_HTILE_4BIT_ENCODING);
}

void
radv_initialise_ds_surface(struct radv_device *device, struct radv_ds_buffer_info *ds,
                           struct radv_image_view *iview)
{
   unsigned level = iview->vk.base_mip_level;
   unsigned format, stencil_format;
   uint64_t va, s_offs, z_offs;
   bool stencil_only = iview->image->vk.format == VK_FORMAT_S8_UINT;
   const struct radv_image_plane *plane = &iview->image->planes[0];
   const struct radeon_surf *surf = &plane->surface;

   assert(vk_format_get_plane_count(iview->image->vk.format) == 1);

   memset(ds, 0, sizeof(*ds));
   if (!device->instance->absolute_depth_bias) {
      switch (iview->image->vk.format) {
      case VK_FORMAT_D24_UNORM_S8_UINT:
      case VK_FORMAT_X8_D24_UNORM_PACK32:
         ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-24);
         break;
      case VK_FORMAT_D16_UNORM:
      case VK_FORMAT_D16_UNORM_S8_UINT:
         ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-16);
         break;
      case VK_FORMAT_D32_SFLOAT:
      case VK_FORMAT_D32_SFLOAT_S8_UINT:
         ds->pa_su_poly_offset_db_fmt_cntl =
            S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-23) | S_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT(1);
         break;
      default:
         break;
      }
   }

   format = radv_translate_dbformat(iview->image->vk.format);
   stencil_format = surf->has_stencil ? V_028044_STENCIL_8 : V_028044_STENCIL_INVALID;

   uint32_t max_slice = radv_surface_max_layer_count(iview) - 1;
   ds->db_depth_view = S_028008_SLICE_START(iview->vk.base_array_layer) |
                       S_028008_SLICE_MAX(max_slice);
   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      ds->db_depth_view |= S_028008_SLICE_START_HI(iview->vk.base_array_layer >> 11) |
                           S_028008_SLICE_MAX_HI(max_slice >> 11);
   }

   ds->db_htile_data_base = 0;
   ds->db_htile_surface = 0;

   va = radv_buffer_get_va(iview->image->bindings[0].bo) + iview->image->bindings[0].offset;
   s_offs = z_offs = va;

   if (device->physical_device->rad_info.gfx_level >= GFX9) {
      assert(surf->u.gfx9.surf_offset == 0);
      s_offs += surf->u.gfx9.zs.stencil_offset;

      ds->db_z_info = S_028038_FORMAT(format) |
                      S_028038_NUM_SAMPLES(util_logbase2(iview->image->info.samples)) |
                      S_028038_SW_MODE(surf->u.gfx9.swizzle_mode) |
                      S_028038_MAXMIP(iview->image->info.levels - 1) |
                      S_028038_ZRANGE_PRECISION(1) |
                      S_028040_ITERATE_256(device->physical_device->rad_info.gfx_level >= GFX11);
      ds->db_stencil_info = S_02803C_FORMAT(stencil_format) |
                            S_02803C_SW_MODE(surf->u.gfx9.zs.stencil_swizzle_mode) |
                            S_028044_ITERATE_256(device->physical_device->rad_info.gfx_level >= GFX11);

      if (device->physical_device->rad_info.gfx_level == GFX9) {
         ds->db_z_info2 = S_028068_EPITCH(surf->u.gfx9.epitch);
         ds->db_stencil_info2 = S_02806C_EPITCH(surf->u.gfx9.zs.stencil_epitch);
      }

      ds->db_depth_view |= S_028008_MIPID(level);
      ds->db_depth_size = S_02801C_X_MAX(iview->image->info.width - 1) |
                          S_02801C_Y_MAX(iview->image->info.height - 1);

      if (radv_htile_enabled(iview->image, level)) {
         ds->db_z_info |= S_028038_TILE_SURFACE_ENABLE(1);

         if (radv_image_is_tc_compat_htile(iview->image)) {
            unsigned max_zplanes = radv_calc_decompress_on_z_planes(device, iview);

            ds->db_z_info |= S_028038_DECOMPRESS_ON_N_ZPLANES(max_zplanes);

            if (device->physical_device->rad_info.gfx_level >= GFX10) {
               bool iterate256 = radv_image_get_iterate256(device, iview->image);

               ds->db_z_info |= S_028040_ITERATE_FLUSH(1);
               ds->db_stencil_info |= S_028044_ITERATE_FLUSH(1);
               ds->db_z_info |= S_028040_ITERATE_256(iterate256);
               ds->db_stencil_info |= S_028044_ITERATE_256(iterate256);
            } else {
               ds->db_z_info |= S_028038_ITERATE_FLUSH(1);
               ds->db_stencil_info |= S_02803C_ITERATE_FLUSH(1);
            }
         }

         if (radv_image_tile_stencil_disabled(device, iview->image)) {
            ds->db_stencil_info |= S_02803C_TILE_STENCIL_DISABLE(1);
         }

         va = radv_buffer_get_va(iview->image->bindings[0].bo) + iview->image->bindings[0].offset + 
            surf->meta_offset;
         ds->db_htile_data_base = va >> 8;
         ds->db_htile_surface = S_028ABC_FULL_CACHE(1) | S_028ABC_PIPE_ALIGNED(1);

         if (device->physical_device->rad_info.gfx_level == GFX9) {
            ds->db_htile_surface |= S_028ABC_RB_ALIGNED(1);
         }

         if (radv_image_has_vrs_htile(device, iview->image)) {
            ds->db_htile_surface |= S_028ABC_VRS_HTILE_ENCODING(V_028ABC_VRS_HTILE_4BIT_ENCODING);
         }
      }
   } else {
      const struct legacy_surf_level *level_info = &surf->u.legacy.level[level];

      if (stencil_only)
         level_info = &surf->u.legacy.zs.stencil_level[level];

      z_offs += (uint64_t)surf->u.legacy.level[level].offset_256B * 256;
      s_offs += (uint64_t)surf->u.legacy.zs.stencil_level[level].offset_256B * 256;

      ds->db_depth_info = S_02803C_ADDR5_SWIZZLE_MASK(!radv_image_is_tc_compat_htile(iview->image));
      ds->db_z_info = S_028040_FORMAT(format) | S_028040_ZRANGE_PRECISION(1);
      ds->db_stencil_info = S_028044_FORMAT(stencil_format);

      if (iview->image->info.samples > 1)
         ds->db_z_info |= S_028040_NUM_SAMPLES(util_logbase2(iview->image->info.samples));

      if (device->physical_device->rad_info.gfx_level >= GFX7) {
         struct radeon_info *info = &device->physical_device->rad_info;
         unsigned tiling_index = surf->u.legacy.tiling_index[level];
         unsigned stencil_index = surf->u.legacy.zs.stencil_tiling_index[level];
         unsigned macro_index = surf->u.legacy.macro_tile_index;
         unsigned tile_mode = info->si_tile_mode_array[tiling_index];
         unsigned stencil_tile_mode = info->si_tile_mode_array[stencil_index];
         unsigned macro_mode = info->cik_macrotile_mode_array[macro_index];

         if (stencil_only)
            tile_mode = stencil_tile_mode;

         ds->db_depth_info |= S_02803C_ARRAY_MODE(G_009910_ARRAY_MODE(tile_mode)) |
                              S_02803C_PIPE_CONFIG(G_009910_PIPE_CONFIG(tile_mode)) |
                              S_02803C_BANK_WIDTH(G_009990_BANK_WIDTH(macro_mode)) |
                              S_02803C_BANK_HEIGHT(G_009990_BANK_HEIGHT(macro_mode)) |
                              S_02803C_MACRO_TILE_ASPECT(G_009990_MACRO_TILE_ASPECT(macro_mode)) |
                              S_02803C_NUM_BANKS(G_009990_NUM_BANKS(macro_mode));
         ds->db_z_info |= S_028040_TILE_SPLIT(G_009910_TILE_SPLIT(tile_mode));
         ds->db_stencil_info |= S_028044_TILE_SPLIT(G_009910_TILE_SPLIT(stencil_tile_mode));
      } else {
         unsigned tile_mode_index = si_tile_mode_index(&iview->image->planes[0], level, false);
         ds->db_z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);
         tile_mode_index = si_tile_mode_index(&iview->image->planes[0], level, true);
         ds->db_stencil_info |= S_028044_TILE_MODE_INDEX(tile_mode_index);
         if (stencil_only)
            ds->db_z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);
      }

      ds->db_depth_size = S_028058_PITCH_TILE_MAX((level_info->nblk_x / 8) - 1) |
                          S_028058_HEIGHT_TILE_MAX((level_info->nblk_y / 8) - 1);
      ds->db_depth_slice =
         S_02805C_SLICE_TILE_MAX((level_info->nblk_x * level_info->nblk_y) / 64 - 1);

      if (radv_htile_enabled(iview->image, level)) {
         ds->db_z_info |= S_028040_TILE_SURFACE_ENABLE(1);

         if (radv_image_tile_stencil_disabled(device, iview->image)) {
            ds->db_stencil_info |= S_028044_TILE_STENCIL_DISABLE(1);
         }

         va = radv_buffer_get_va(iview->image->bindings[0].bo) + iview->image->bindings[0].offset +
            surf->meta_offset;
         ds->db_htile_data_base = va >> 8;
         ds->db_htile_surface = S_028ABC_FULL_CACHE(1);

         if (radv_image_is_tc_compat_htile(iview->image)) {
            unsigned max_zplanes = radv_calc_decompress_on_z_planes(device, iview);

            ds->db_htile_surface |= S_028ABC_TC_COMPATIBLE(1);
            ds->db_z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(max_zplanes);
         }
      }
   }

   ds->db_z_read_base = ds->db_z_write_base = z_offs >> 8;
   ds->db_stencil_read_base = ds->db_stencil_write_base = s_offs >> 8;
}

static unsigned
radv_tex_wrap(VkSamplerAddressMode address_mode)
{
   switch (address_mode) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      return V_008F30_SQ_TEX_WRAP;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      return V_008F30_SQ_TEX_MIRROR;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      return V_008F30_SQ_TEX_CLAMP_LAST_TEXEL;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      return V_008F30_SQ_TEX_CLAMP_BORDER;
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
      return V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL;
   default:
      unreachable("illegal tex wrap mode");
      break;
   }
}

static unsigned
radv_tex_compare(VkCompareOp op)
{
   switch (op) {
   case VK_COMPARE_OP_NEVER:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;
   case VK_COMPARE_OP_LESS:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_LESS;
   case VK_COMPARE_OP_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_EQUAL;
   case VK_COMPARE_OP_LESS_OR_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_LESSEQUAL;
   case VK_COMPARE_OP_GREATER:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATER;
   case VK_COMPARE_OP_NOT_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_NOTEQUAL;
   case VK_COMPARE_OP_GREATER_OR_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATEREQUAL;
   case VK_COMPARE_OP_ALWAYS:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_ALWAYS;
   default:
      unreachable("illegal compare mode");
      break;
   }
}

static unsigned
radv_tex_filter(VkFilter filter, unsigned max_ansio)
{
   switch (filter) {
   case VK_FILTER_NEAREST:
      return (max_ansio > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_POINT
                            : V_008F38_SQ_TEX_XY_FILTER_POINT);
   case VK_FILTER_LINEAR:
      return (max_ansio > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_BILINEAR
                            : V_008F38_SQ_TEX_XY_FILTER_BILINEAR);
   case VK_FILTER_CUBIC_EXT:
   default:
      fprintf(stderr, "illegal texture filter");
      return 0;
   }
}

static unsigned
radv_tex_mipfilter(VkSamplerMipmapMode mode)
{
   switch (mode) {
   case VK_SAMPLER_MIPMAP_MODE_NEAREST:
      return V_008F38_SQ_TEX_Z_FILTER_POINT;
   case VK_SAMPLER_MIPMAP_MODE_LINEAR:
      return V_008F38_SQ_TEX_Z_FILTER_LINEAR;
   default:
      return V_008F38_SQ_TEX_Z_FILTER_NONE;
   }
}

static unsigned
radv_tex_bordercolor(VkBorderColor bcolor)
{
   switch (bcolor) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      return V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_BLACK;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_WHITE;
   case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
   case VK_BORDER_COLOR_INT_CUSTOM_EXT:
      return V_008F3C_SQ_TEX_BORDER_COLOR_REGISTER;
   default:
      break;
   }
   return 0;
}

static unsigned
radv_tex_aniso_filter(unsigned filter)
{
   return MIN2(util_logbase2(filter), 4);
}

static unsigned
radv_tex_filter_mode(VkSamplerReductionMode mode)
{
   switch (mode) {
   case VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE:
      return V_008F30_SQ_IMG_FILTER_MODE_BLEND;
   case VK_SAMPLER_REDUCTION_MODE_MIN:
      return V_008F30_SQ_IMG_FILTER_MODE_MIN;
   case VK_SAMPLER_REDUCTION_MODE_MAX:
      return V_008F30_SQ_IMG_FILTER_MODE_MAX;
   default:
      break;
   }
   return 0;
}

static uint32_t
radv_get_max_anisotropy(struct radv_device *device, const VkSamplerCreateInfo *pCreateInfo)
{
   if (device->force_aniso >= 0)
      return device->force_aniso;

   if (pCreateInfo->anisotropyEnable && pCreateInfo->maxAnisotropy > 1.0f)
      return (uint32_t)pCreateInfo->maxAnisotropy;

   return 0;
}

static uint32_t
radv_register_border_color(struct radv_device *device, VkClearColorValue value)
{
   uint32_t slot;

   mtx_lock(&device->border_color_data.mutex);

   for (slot = 0; slot < RADV_BORDER_COLOR_COUNT; slot++) {
      if (!device->border_color_data.used[slot]) {
         /* Copy to the GPU wrt endian-ness. */
         util_memcpy_cpu_to_le32(&device->border_color_data.colors_gpu_ptr[slot], &value,
                                 sizeof(VkClearColorValue));

         device->border_color_data.used[slot] = true;
         break;
      }
   }

   mtx_unlock(&device->border_color_data.mutex);

   return slot;
}

static void
radv_unregister_border_color(struct radv_device *device, uint32_t slot)
{
   mtx_lock(&device->border_color_data.mutex);

   device->border_color_data.used[slot] = false;

   mtx_unlock(&device->border_color_data.mutex);
}

static void
radv_init_sampler(struct radv_device *device, struct radv_sampler *sampler,
                  const VkSamplerCreateInfo *pCreateInfo)
{
   uint32_t max_aniso = radv_get_max_anisotropy(device, pCreateInfo);
   uint32_t max_aniso_ratio = radv_tex_aniso_filter(max_aniso);
   bool compat_mode = device->physical_device->rad_info.gfx_level == GFX8 ||
                      device->physical_device->rad_info.gfx_level == GFX9;
   unsigned filter_mode = V_008F30_SQ_IMG_FILTER_MODE_BLEND;
   unsigned depth_compare_func = V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;
   bool trunc_coord =
      pCreateInfo->minFilter == VK_FILTER_NEAREST && pCreateInfo->magFilter == VK_FILTER_NEAREST;
   bool uses_border_color = pCreateInfo->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
                            pCreateInfo->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
                            pCreateInfo->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
   VkBorderColor border_color =
      uses_border_color ? pCreateInfo->borderColor : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
   uint32_t border_color_ptr;
   bool disable_cube_wrap = pCreateInfo->flags & VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT;

   const struct VkSamplerReductionModeCreateInfo *sampler_reduction =
      vk_find_struct_const(pCreateInfo->pNext, SAMPLER_REDUCTION_MODE_CREATE_INFO);
   if (sampler_reduction)
      filter_mode = radv_tex_filter_mode(sampler_reduction->reductionMode);

   if (pCreateInfo->compareEnable)
      depth_compare_func = radv_tex_compare(pCreateInfo->compareOp);

   sampler->border_color_slot = RADV_BORDER_COLOR_COUNT;

   if (border_color == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT ||
       border_color == VK_BORDER_COLOR_INT_CUSTOM_EXT) {
      const VkSamplerCustomBorderColorCreateInfoEXT *custom_border_color =
         vk_find_struct_const(pCreateInfo->pNext, SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);

      assert(custom_border_color);

      sampler->border_color_slot =
         radv_register_border_color(device, custom_border_color->customBorderColor);

      /* Did we fail to find a slot? */
      if (sampler->border_color_slot == RADV_BORDER_COLOR_COUNT) {
         fprintf(stderr, "WARNING: no free border color slots, defaulting to TRANS_BLACK.\n");
         border_color = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
      }
   }

   /* If we don't have a custom color, set the ptr to 0 */
   border_color_ptr =
      sampler->border_color_slot != RADV_BORDER_COLOR_COUNT ? sampler->border_color_slot : 0;

   sampler->state[0] =
      (S_008F30_CLAMP_X(radv_tex_wrap(pCreateInfo->addressModeU)) |
       S_008F30_CLAMP_Y(radv_tex_wrap(pCreateInfo->addressModeV)) |
       S_008F30_CLAMP_Z(radv_tex_wrap(pCreateInfo->addressModeW)) |
       S_008F30_MAX_ANISO_RATIO(max_aniso_ratio) | S_008F30_DEPTH_COMPARE_FUNC(depth_compare_func) |
       S_008F30_FORCE_UNNORMALIZED(pCreateInfo->unnormalizedCoordinates ? 1 : 0) |
       S_008F30_ANISO_THRESHOLD(max_aniso_ratio >> 1) | S_008F30_ANISO_BIAS(max_aniso_ratio) |
       S_008F30_DISABLE_CUBE_WRAP(disable_cube_wrap) | S_008F30_COMPAT_MODE(compat_mode) |
       S_008F30_FILTER_MODE(filter_mode) | S_008F30_TRUNC_COORD(trunc_coord));
   sampler->state[1] = (S_008F34_MIN_LOD(radv_float_to_ufixed(CLAMP(pCreateInfo->minLod, 0, 15), 8)) |
                        S_008F34_MAX_LOD(radv_float_to_ufixed(CLAMP(pCreateInfo->maxLod, 0, 15), 8)) |
                        S_008F34_PERF_MIP(max_aniso_ratio ? max_aniso_ratio + 6 : 0));
   sampler->state[2] = (S_008F38_LOD_BIAS(radv_float_to_sfixed(CLAMP(pCreateInfo->mipLodBias, -16, 16), 8)) |
                        S_008F38_XY_MAG_FILTER(radv_tex_filter(pCreateInfo->magFilter, max_aniso)) |
                        S_008F38_XY_MIN_FILTER(radv_tex_filter(pCreateInfo->minFilter, max_aniso)) |
                        S_008F38_MIP_FILTER(radv_tex_mipfilter(pCreateInfo->mipmapMode)));
   sampler->state[3] = S_008F3C_BORDER_COLOR_TYPE(radv_tex_bordercolor(border_color));

   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      sampler->state[2] |=
         S_008F38_ANISO_OVERRIDE_GFX10(device->instance->disable_aniso_single_level);
   } else {
      sampler->state[2] |=
         S_008F38_DISABLE_LSB_CEIL(device->physical_device->rad_info.gfx_level <= GFX8) |
         S_008F38_FILTER_PREC_FIX(1) |
         S_008F38_ANISO_OVERRIDE_GFX8(device->instance->disable_aniso_single_level &&
                                      device->physical_device->rad_info.gfx_level >= GFX8);
   }

   if (device->physical_device->rad_info.gfx_level >= GFX11) {
      sampler->state[3] |= S_008F3C_BORDER_COLOR_PTR_GFX11(border_color_ptr);
   } else {
      sampler->state[3] |= S_008F3C_BORDER_COLOR_PTR_GFX6(border_color_ptr);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateSampler(VkDevice _device, const VkSamplerCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_sampler *sampler;

   const struct VkSamplerYcbcrConversionInfo *ycbcr_conversion =
      vk_find_struct_const(pCreateInfo->pNext, SAMPLER_YCBCR_CONVERSION_INFO);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*sampler), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &sampler->base, VK_OBJECT_TYPE_SAMPLER);

   radv_init_sampler(device, sampler, pCreateInfo);

   sampler->ycbcr_sampler =
      ycbcr_conversion ? radv_sampler_ycbcr_conversion_from_handle(ycbcr_conversion->conversion)
                       : NULL;
   *pSampler = radv_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroySampler(VkDevice _device, VkSampler _sampler, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_sampler, sampler, _sampler);

   if (!sampler)
      return;

   if (sampler->border_color_slot != RADV_BORDER_COLOR_COUNT)
      radv_unregister_border_color(device, sampler->border_color_slot);

   vk_object_base_finish(&sampler->base);
   vk_free2(&device->vk.alloc, pAllocator, sampler);
}

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion)
{
   /* For the full details on loader interface versioning, see
    * <https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/loader/LoaderAndLayerInterface.md>.
    * What follows is a condensed summary, to help you navigate the large and
    * confusing official doc.
    *
    *   - Loader interface v0 is incompatible with later versions. We don't
    *     support it.
    *
    *   - In loader interface v1:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdGetInstanceProcAddr(). The ICD must statically expose this
    *         entrypoint.
    *       - The ICD must statically expose no other Vulkan symbol unless it is
    *         linked with -Bsymbolic.
    *       - Each dispatchable Vulkan handle created by the ICD must be
    *         a pointer to a struct whose first member is VK_LOADER_DATA. The
    *         ICD must initialize VK_LOADER_DATA.loadMagic to ICD_LOADER_MAGIC.
    *       - The loader implements vkCreate{PLATFORM}SurfaceKHR() and
    *         vkDestroySurfaceKHR(). The ICD must be capable of working with
    *         such loader-managed surfaces.
    *
    *    - Loader interface v2 differs from v1 in:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdNegotiateLoaderICDInterfaceVersion(). The ICD must
    *         statically expose this entrypoint.
    *
    *    - Loader interface v3 differs from v2 in:
    *        - The ICD must implement vkCreate{PLATFORM}SurfaceKHR(),
    *          vkDestroySurfaceKHR(), and other API which uses VKSurfaceKHR,
    *          because the loader no longer does so.
    *
    *    - Loader interface v4 differs from v3 in:
    *        - The ICD must implement vk_icdGetPhysicalDeviceProcAddr().
    *
    *    - Loader interface v5 differs from v4 in:
    *        - The ICD must support Vulkan API version 1.1 and must not return
    *          VK_ERROR_INCOMPATIBLE_DRIVER from vkCreateInstance() unless a
    *          Vulkan Loader with interface v4 or smaller is being used and the
    *          application provides an API version that is greater than 1.0.
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 5u);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetMemoryFdKHR(VkDevice _device, const VkMemoryGetFdInfoKHR *pGetFdInfo, int *pFD)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, memory, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   /* At the moment, we support only the below handle types. */
   assert(pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
          pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   bool ret = radv_get_memory_fd(device, memory, pFD);
   if (ret == false)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   return VK_SUCCESS;
}

static uint32_t
radv_compute_valid_memory_types_attempt(struct radv_physical_device *dev,
                                        enum radeon_bo_domain domains, enum radeon_bo_flag flags,
                                        enum radeon_bo_flag ignore_flags)
{
   /* Don't count GTT/CPU as relevant:
    *
    * - We're not fully consistent between the two.
    * - Sometimes VRAM gets VRAM|GTT.
    */
   const enum radeon_bo_domain relevant_domains =
      RADEON_DOMAIN_VRAM | RADEON_DOMAIN_GDS | RADEON_DOMAIN_OA;
   uint32_t bits = 0;
   for (unsigned i = 0; i < dev->memory_properties.memoryTypeCount; ++i) {
      if ((domains & relevant_domains) != (dev->memory_domains[i] & relevant_domains))
         continue;

      if ((flags & ~ignore_flags) != (dev->memory_flags[i] & ~ignore_flags))
         continue;

      bits |= 1u << i;
   }

   return bits;
}

static uint32_t
radv_compute_valid_memory_types(struct radv_physical_device *dev, enum radeon_bo_domain domains,
                                enum radeon_bo_flag flags)
{
   enum radeon_bo_flag ignore_flags = ~(RADEON_FLAG_NO_CPU_ACCESS | RADEON_FLAG_GTT_WC);
   uint32_t bits = radv_compute_valid_memory_types_attempt(dev, domains, flags, ignore_flags);

   if (!bits) {
      ignore_flags |= RADEON_FLAG_GTT_WC;
      bits = radv_compute_valid_memory_types_attempt(dev, domains, flags, ignore_flags);
   }

   if (!bits) {
      ignore_flags |= RADEON_FLAG_NO_CPU_ACCESS;
      bits = radv_compute_valid_memory_types_attempt(dev, domains, flags, ignore_flags);
   }

   /* Avoid 32-bit memory types for shared memory. */
   bits &= ~dev->memory_types_32bit;

   return bits;
}
VKAPI_ATTR VkResult VKAPI_CALL
radv_GetMemoryFdPropertiesKHR(VkDevice _device, VkExternalMemoryHandleTypeFlagBits handleType,
                              int fd, VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT: {
      enum radeon_bo_domain domains;
      enum radeon_bo_flag flags;
      if (!device->ws->buffer_get_flags_from_fd(device->ws, fd, &domains, &flags))
         return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);

      pMemoryFdProperties->memoryTypeBits =
         radv_compute_valid_memory_types(device->physical_device, domains, flags);
      return VK_SUCCESS;
   }
   default:
      /* The valid usage section for this function says:
       *
       *    "handleType must not be one of the handle types defined as
       *    opaque."
       *
       * So opaque handle types fall into the default "unsupported" case.
       */
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex,
                                      uint32_t localDeviceIndex, uint32_t remoteDeviceIndex,
                                      VkPeerMemoryFeatureFlags *pPeerMemoryFeatures)
{
   assert(localDeviceIndex == remoteDeviceIndex);

   *pPeerMemoryFeatures =
      VK_PEER_MEMORY_FEATURE_COPY_SRC_BIT | VK_PEER_MEMORY_FEATURE_COPY_DST_BIT |
      VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT | VK_PEER_MEMORY_FEATURE_GENERIC_DST_BIT;
}

static const VkTimeDomainEXT radv_time_domains[] = {
   VK_TIME_DOMAIN_DEVICE_EXT,
   VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT,
#ifdef CLOCK_MONOTONIC_RAW
   VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT,
#endif
};

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPhysicalDeviceCalibrateableTimeDomainsEXT(VkPhysicalDevice physicalDevice,
                                                  uint32_t *pTimeDomainCount,
                                                  VkTimeDomainEXT *pTimeDomains)
{
   int d;
   VK_OUTARRAY_MAKE_TYPED(VkTimeDomainEXT, out, pTimeDomains, pTimeDomainCount);

   for (d = 0; d < ARRAY_SIZE(radv_time_domains); d++) {
      vk_outarray_append_typed(VkTimeDomainEXT, &out, i)
      {
         *i = radv_time_domains[d];
      }
   }

   return vk_outarray_status(&out);
}

#ifndef _WIN32
VKAPI_ATTR VkResult VKAPI_CALL
radv_GetCalibratedTimestampsEXT(VkDevice _device, uint32_t timestampCount,
                                const VkCalibratedTimestampInfoEXT *pTimestampInfos,
                                uint64_t *pTimestamps, uint64_t *pMaxDeviation)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   uint32_t clock_crystal_freq = device->physical_device->rad_info.clock_crystal_freq;
   int d;
   uint64_t begin, end;
   uint64_t max_clock_period = 0;

#ifdef CLOCK_MONOTONIC_RAW
   begin = vk_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   begin = vk_clock_gettime(CLOCK_MONOTONIC);
#endif

   for (d = 0; d < timestampCount; d++) {
      switch (pTimestampInfos[d].timeDomain) {
      case VK_TIME_DOMAIN_DEVICE_EXT:
         pTimestamps[d] = device->ws->query_value(device->ws, RADEON_TIMESTAMP);
         uint64_t device_period = DIV_ROUND_UP(1000000, clock_crystal_freq);
         max_clock_period = MAX2(max_clock_period, device_period);
         break;
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT:
         pTimestamps[d] = vk_clock_gettime(CLOCK_MONOTONIC);
         max_clock_period = MAX2(max_clock_period, 1);
         break;

#ifdef CLOCK_MONOTONIC_RAW
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT:
         pTimestamps[d] = begin;
         break;
#endif
      default:
         pTimestamps[d] = 0;
         break;
      }
   }

#ifdef CLOCK_MONOTONIC_RAW
   end = vk_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   end = vk_clock_gettime(CLOCK_MONOTONIC);
#endif

   *pMaxDeviation = vk_time_max_deviation(begin, end, max_clock_period);

   return VK_SUCCESS;
}
#endif

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceMultisamplePropertiesEXT(VkPhysicalDevice physicalDevice,
                                               VkSampleCountFlagBits samples,
                                               VkMultisamplePropertiesEXT *pMultisampleProperties)
{
   VkSampleCountFlagBits supported_samples = VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
                                             VK_SAMPLE_COUNT_8_BIT;

   if (samples & supported_samples) {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){2, 2};
   } else {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){0, 0};
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPhysicalDeviceFragmentShadingRatesKHR(
   VkPhysicalDevice physicalDevice, uint32_t *pFragmentShadingRateCount,
   VkPhysicalDeviceFragmentShadingRateKHR *pFragmentShadingRates)
{
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceFragmentShadingRateKHR, out, pFragmentShadingRates,
                          pFragmentShadingRateCount);

#define append_rate(w, h, s)                                                                       \
   {                                                                                               \
      VkPhysicalDeviceFragmentShadingRateKHR rate = {                                              \
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR,          \
         .sampleCounts = s,                                                                        \
         .fragmentSize = {.width = w, .height = h},                                                \
      };                                                                                           \
      vk_outarray_append_typed(VkPhysicalDeviceFragmentShadingRateKHR, &out, r) *r = rate;         \
   }

   for (uint32_t x = 2; x >= 1; x--) {
      for (uint32_t y = 2; y >= 1; y--) {
         VkSampleCountFlagBits samples;

         if (x == 1 && y == 1) {
            samples = ~0;
         } else {
            samples = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT |
                      VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
         }

         append_rate(x, y, samples);
      }
   }
#undef append_rate

   return vk_outarray_status(&out);
}

static bool
radv_thread_trace_set_pstate(struct radv_device *device, bool enable)
{
   struct radeon_winsys *ws = device->ws;
   enum radeon_ctx_pstate pstate = enable ? RADEON_CTX_PSTATE_PEAK : RADEON_CTX_PSTATE_NONE;

   if (device->physical_device->rad_info.has_stable_pstate) {
      /* pstate is per-device; setting it for one ctx is sufficient.
       * We pick the first initialized one below. */
      for (unsigned i = 0; i < RADV_NUM_HW_CTX; i++)
         if (device->hw_ctx[i])
            return ws->ctx_set_pstate(device->hw_ctx[i], pstate) >= 0;
   }

   return true;
}

bool
radv_device_acquire_performance_counters(struct radv_device *device)
{
   bool result = true;
   simple_mtx_lock(&device->pstate_mtx);

   if (device->pstate_cnt == 0) {
      result = radv_thread_trace_set_pstate(device, true);
      if (result)
         ++device->pstate_cnt;
   }

   simple_mtx_unlock(&device->pstate_mtx);
   return result;
}

void
radv_device_release_performance_counters(struct radv_device *device)
{
   simple_mtx_lock(&device->pstate_mtx);

   if (--device->pstate_cnt == 0)
      radv_thread_trace_set_pstate(device, false);

   simple_mtx_unlock(&device->pstate_mtx);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_AcquireProfilingLockKHR(VkDevice _device, const VkAcquireProfilingLockInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   bool result = radv_device_acquire_performance_counters(device);
   return result ? VK_SUCCESS : VK_ERROR_UNKNOWN;
}

VKAPI_ATTR void VKAPI_CALL
radv_ReleaseProfilingLockKHR(VkDevice _device)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   radv_device_release_performance_counters(device);
}
