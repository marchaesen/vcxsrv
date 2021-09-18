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
#elif !defined(_WIN32)
#include <sys/sysmacros.h>
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
#include "util/timespec.h"
#include "util/u_atomic.h"
#include "winsys/null/radv_null_winsys_public.h"
#include "ac_llvm_util.h"
#include "git_sha1.h"
#include "sid.h"
#include "vk_format.h"
#include "vulkan/vk_icd.h"

/* The number of IBs per submit isn't infinite, it depends on the ring type
 * (ie. some initial setup needed for a submit) and the number of IBs (4 DW).
 * This limit is arbitrary but should be safe for now.  Ideally, we should get
 * this limit from the KMD.
 */
#define RADV_MAX_IBS_PER_SUBMIT 192

/* The "RAW" clocks on Linux are called "FAST" on FreeBSD */
#if !defined(CLOCK_MONOTONIC_RAW) && defined(CLOCK_MONOTONIC_FAST)
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC_FAST
#endif

static struct radv_timeline_point *
radv_timeline_find_point_at_least_locked(struct radv_device *device, struct radv_timeline *timeline,
                                         uint64_t p);

static struct radv_timeline_point *radv_timeline_add_point_locked(struct radv_device *device,
                                                                  struct radv_timeline *timeline,
                                                                  uint64_t p);

static void radv_timeline_trigger_waiters_locked(struct radv_timeline *timeline,
                                                 struct list_head *processing_list);

static void radv_destroy_semaphore_part(struct radv_device *device,
                                        struct radv_semaphore_part *part);

uint64_t
radv_get_current_time(void)
{
   return os_time_get_nano();
}

static uint64_t
radv_get_absolute_timeout(uint64_t timeout)
{
   if (timeout == UINT64_MAX) {
      return timeout;
   } else {
      uint64_t current_time = radv_get_current_time();

      timeout = MIN2(UINT64_MAX - current_time, timeout);

      return current_time + timeout;
   }
}

static int
radv_device_get_cache_uuid(enum radeon_family family, void *uuid)
{
   struct mesa_sha1 ctx;
   unsigned char sha1[20];
   unsigned ptr_size = sizeof(void *);

   memset(uuid, 0, VK_UUID_SIZE);
   _mesa_sha1_init(&ctx);

   if (!disk_cache_get_function_identifier(radv_device_get_cache_uuid, &ctx) ||
       !disk_cache_get_function_identifier(LLVMInitializeAMDGPUTargetInfo, &ctx))
      return -1;

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
      return MIN2(device->rad_info.vram_size, (uint64_t)ov << 20);
   return device->rad_info.vram_size;
}

static uint64_t
radv_get_visible_vram_size(struct radv_physical_device *device)
{
   return MIN2(radv_get_adjusted_vram_size(device), device->rad_info.vram_vis_size);
}

static uint64_t
radv_get_vram_size(struct radv_physical_device *device)
{
   uint64_t total_size = radv_get_adjusted_vram_size(device);
   return total_size - MIN2(total_size, device->rad_info.vram_vis_size);
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
   uint64_t gtt_size = device->rad_info.gart_size;
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

         if ((mem_type.propertyFlags &
              (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) ||
             mem_type.propertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {

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

   return " (LLVM " MESA_LLVM_VERSION_STRING ")";
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

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR) ||                    \
   defined(VK_USE_PLATFORM_XLIB_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR)
#define RADV_USE_WSI_PLATFORM
#endif

#ifdef ANDROID
#define RADV_API_VERSION VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION)
#else
#define RADV_API_VERSION VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION)
#endif

VkResult
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
      .KHR_acceleration_structure = (device->instance->perftest_flags & RADV_PERFTEST_RT) &&
                                    device->rad_info.chip_class >= GFX10_3,
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
      .KHR_external_fence = true,
      .KHR_external_fence_fd = true,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = true,
      .KHR_external_semaphore_fd = true,
      .KHR_fragment_shading_rate = device->rad_info.chip_class >= GFX10_3,
      .KHR_get_memory_requirements2 = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
#ifdef RADV_USE_WSI_PLATFORM
      .KHR_incremental_present = true,
#endif
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_multiview = true,
      .KHR_pipeline_executable_properties = true,
      .KHR_push_descriptor = true,
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
      .KHR_timeline_semaphore = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_variable_pointers = true,
      .KHR_vulkan_memory_model = true,
      .KHR_workgroup_memory_explicit_layout = true,
      .KHR_zero_initialize_workgroup_memory = true,
      .EXT_4444_formats = true,
      .EXT_buffer_device_address = true,
      .EXT_calibrated_timestamps = RADV_SUPPORT_CALIBRATED_TIMESTAMPS,
      .EXT_color_write_enable = true,
      .EXT_conditional_rendering = true,
      .EXT_conservative_rasterization = device->rad_info.chip_class >= GFX9,
      .EXT_custom_border_color = true,
      .EXT_debug_marker = radv_thread_trace_enabled(),
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
      .EXT_host_query_reset = true,
      .EXT_image_drm_format_modifier = device->rad_info.chip_class >= GFX9,
      .EXT_image_robustness = true,
      .EXT_index_type_uint8 = device->rad_info.chip_class >= GFX8,
      .EXT_inline_uniform_block = true,
      .EXT_line_rasterization = true,
      .EXT_memory_budget = true,
      .EXT_memory_priority = true,
      .EXT_multi_draw = true,
      .EXT_pci_bus_info = true,
#ifndef _WIN32
      .EXT_physical_device_drm = true,
#endif
      .EXT_pipeline_creation_cache_control = true,
      .EXT_pipeline_creation_feedback = true,
      .EXT_post_depth_coverage = device->rad_info.chip_class >= GFX10,
      .EXT_primitive_topology_list_restart = true,
      .EXT_private_data = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_robustness2 = true,
      .EXT_sample_locations = device->rad_info.chip_class < GFX10,
      .EXT_sampler_filter_minmax = true,
      .EXT_scalar_block_layout = device->rad_info.chip_class >= GFX7,
      .EXT_shader_atomic_float = true,
      .EXT_shader_atomic_float2 = !device->use_llvm || LLVM_VERSION_MAJOR >= 14,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_image_atomic_int64 = true,
      .EXT_shader_stencil_export = true,
      .EXT_shader_subgroup_ballot = true,
      .EXT_shader_subgroup_vote = true,
      .EXT_shader_viewport_index_layer = true,
      .EXT_subgroup_size_control = true,
      .EXT_texel_buffer_alignment = true,
      .EXT_transform_feedback = true,
      .EXT_vertex_attribute_divisor = true,
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
      .AMD_shader_fragment_mask = true,
      .AMD_shader_image_load_store_lod = true,
      .AMD_shader_info = true,
      .AMD_shader_trinary_minmax = true,
      .AMD_texture_gather_bias_lod = true,
#ifdef ANDROID
      .ANDROID_external_memory_android_hardware_buffer = RADV_SUPPORT_ANDROID_HARDWARE_BUFFER,
      .ANDROID_native_buffer = true,
#endif
      .GOOGLE_decorate_string = true,
      .GOOGLE_hlsl_functionality1 = true,
      .GOOGLE_user_type = true,
      .NV_compute_shader_derivatives = true,
      .VALVE_mutable_descriptor_type = true,
   };
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
         if (instance->debug_flags & RADV_DEBUG_STARTUP)
            radv_logi("Could not open device '%s'", path);

         return vk_error(instance, VK_ERROR_INCOMPATIBLE_DRIVER);
      }

      version = drmGetVersion(fd);
      if (!version) {
         close(fd);

         if (instance->debug_flags & RADV_DEBUG_STARTUP)
            radv_logi("Could not get the kernel driver version for device '%s'", path);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER, "failed to get version %s: %m",
                          path);
      }

      if (strcmp(version->name, "amdgpu")) {
         drmFreeVersion(version);
         close(fd);

         if (instance->debug_flags & RADV_DEBUG_STARTUP)
            radv_logi("Device '%s' is not using the amdgpu kernel driver.", path);

         return VK_ERROR_INCOMPATIBLE_DRIVER;
      }
      drmFreeVersion(version);

      if (instance->debug_flags & RADV_DEBUG_STARTUP)
         radv_logi("Found compatible device '%s'.", path);
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

   result = vk_physical_device_init(&device->vk, &instance->vk, NULL, &dispatch_table);
   if (result != VK_SUCCESS) {
      goto fail_alloc;
   }

   device->instance = instance;

#ifdef _WIN32
   device->ws = radv_null_winsys_create();
#else
   if (drm_device) {
      device->ws = radv_amdgpu_winsys_create(fd, instance->debug_flags, instance->perftest_flags, false);
   } else {
      device->ws = radv_null_winsys_create();
   }
#endif

   if (!device->ws) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "failed to initialize winsys");
      goto fail_base;
   }

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

   snprintf(device->name, sizeof(device->name), "AMD RADV %s%s", device->rad_info.name,
            radv_get_compiler_string(device));

#ifdef ENABLE_SHADER_CACHE
   if (radv_device_get_cache_uuid(device->rad_info.family, device->cache_uuid)) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "cannot generate UUID");
      goto fail_wsi;
   }

   /* These flags affect shader compilation. */
   uint64_t shader_env_flags = (device->use_llvm ? 0 : 0x2);

   /* The gpu id is already embedded in the uuid so we just pass "radv"
    * when creating the cache.
    */
   char buf[VK_UUID_SIZE * 2 + 1];
   disk_cache_format_hex_id(buf, device->cache_uuid, VK_UUID_SIZE * 2);
   device->disk_cache = disk_cache_create(device->name, buf, shader_env_flags);
#endif

   if (device->rad_info.chip_class < GFX8 || device->rad_info.chip_class > GFX10)
      vk_warn_non_conformant_implementation("radv");

   radv_get_driver_uuid(&device->driver_uuid);
   radv_get_device_uuid(&device->rad_info, &device->device_uuid);

   device->out_of_order_rast_allowed =
      device->rad_info.has_out_of_order_rast &&
      !(device->instance->debug_flags & RADV_DEBUG_NO_OUT_OF_ORDER);

   device->dcc_msaa_allowed = (device->instance->perftest_flags & RADV_PERFTEST_DCC_MSAA);

   device->use_ngg = device->rad_info.chip_class >= GFX10 &&
                     device->rad_info.family != CHIP_NAVI14 &&
                     !(device->instance->debug_flags & RADV_DEBUG_NO_NGG);

   device->use_ngg_streamout = false;

   /* Determine the number of threads per wave for all stages. */
   device->cs_wave_size = 64;
   device->ps_wave_size = 64;
   device->ge_wave_size = 64;

   if (device->rad_info.chip_class >= GFX10) {
      if (device->instance->perftest_flags & RADV_PERFTEST_CS_WAVE_32)
         device->cs_wave_size = 32;

      /* For pixel shaders, wave64 is recommanded. */
      if (device->instance->perftest_flags & RADV_PERFTEST_PS_WAVE_32)
         device->ps_wave_size = 32;

      if (device->instance->perftest_flags & RADV_PERFTEST_GE_WAVE_32)
         device->ge_wave_size = 32;
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
         goto fail_disk_cache;
      }
      device->primary_devid = primary_stat.st_rdev;

      if ((drm_device->available_nodes & (1 << DRM_NODE_RENDER)) &&
          stat(drm_device->nodes[DRM_NODE_RENDER], &render_stat) != 0) {
         result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                            "failed to stat DRM render node %s",
                            drm_device->nodes[DRM_NODE_RENDER]);
         goto fail_disk_cache;
      }
      device->render_devid = render_stat.st_rdev;
   }
#endif

   if ((device->instance->debug_flags & RADV_DEBUG_INFO))
      ac_print_gpu_info(&device->rad_info, stdout);

   /* The WSI is structured as a layer on top of the driver, so this has
    * to be the last part of initialization (at least until we get other
    * semi-layers).
    */
   result = radv_init_wsi(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail_disk_cache;
   }

   *device_out = device;

   return VK_SUCCESS;

fail_disk_cache:
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
radv_physical_device_destroy(struct radv_physical_device *device)
{
   radv_finish_wsi(device);
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
   {"errors", RADV_DEBUG_ERRORS},
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
   {"nodisplaydcc", RADV_DEBUG_NO_DISPLAY_DCC},
   {"notccompatcmask", RADV_DEBUG_NO_TC_COMPAT_CMASK},
   {"novrsflatshading", RADV_DEBUG_NO_VRS_FLAT_SHADING},
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
      DRI_CONF_RADV_DISABLE_TC_COMPAT_HTILE_GENERAL(false)
      DRI_CONF_RADV_DISABLE_DCC(false)
      DRI_CONF_RADV_REPORT_APU_AS_DGPU(false)
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

   if (driQueryOptionb(&instance->dri_options, "radv_zero_vram"))
      instance->debug_flags |= RADV_DEBUG_ZERO_VRAM;

   if (driQueryOptionb(&instance->dri_options, "radv_lower_discard_to_demote"))
      instance->debug_flags |= RADV_DEBUG_DISCARD_TO_DEMOTE;

   if (driQueryOptionb(&instance->dri_options, "radv_invariant_geom"))
      instance->debug_flags |= RADV_DEBUG_INVARIANT_GEOM;

   if (driQueryOptionb(&instance->dri_options, "radv_disable_dcc"))
      instance->debug_flags |= RADV_DEBUG_NO_DCC;

   instance->report_apu_as_dgpu =
      driQueryOptionb(&instance->dri_options, "radv_report_apu_as_dgpu");
}

VkResult
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
   result = vk_instance_init(&instance->vk, &radv_instance_extensions_supported, &dispatch_table,
                             pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(instance, result);
   }

   instance->debug_flags = parse_debug_string(getenv("RADV_DEBUG"), radv_debug_options);
   instance->perftest_flags = parse_debug_string(getenv("RADV_PERFTEST"), radv_perftest_options);

   if (instance->debug_flags & RADV_DEBUG_STARTUP)
      radv_logi("Created an instance");

   instance->physical_devices_enumerated = false;
   list_inithead(&instance->physical_devices);

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   radv_init_dri_options(instance);

   *pInstance = radv_instance_to_handle(instance);

   return VK_SUCCESS;
}

void
radv_DestroyInstance(VkInstance _instance, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_instance, instance, _instance);

   if (!instance)
      return;

   list_for_each_entry_safe(struct radv_physical_device, pdevice, &instance->physical_devices, link)
   {
      radv_physical_device_destroy(pdevice);
   }

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   driDestroyOptionCache(&instance->dri_options);
   driDestroyOptionInfo(&instance->available_dri_options);

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

static VkResult
radv_enumerate_physical_devices(struct radv_instance *instance)
{
   if (instance->physical_devices_enumerated)
      return VK_SUCCESS;

   instance->physical_devices_enumerated = true;

   VkResult result = VK_SUCCESS;

   if (getenv("RADV_FORCE_FAMILY")) {
      /* When RADV_FORCE_FAMILY is set, the driver creates a nul
       * device that allows to test the compiler without having an
       * AMDGPU instance.
       */
      struct radv_physical_device *pdevice;

      result = radv_physical_device_try_create(instance, NULL, &pdevice);
      if (result != VK_SUCCESS)
         return result;

      list_addtail(&pdevice->link, &instance->physical_devices);
      return VK_SUCCESS;
   }

#ifndef _WIN32
   /* TODO: Check for more devices ? */
   drmDevicePtr devices[8];
   int max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));

   if (instance->debug_flags & RADV_DEBUG_STARTUP)
      radv_logi("Found %d drm nodes", max_devices);

   if (max_devices < 1)
      return vk_error(instance, VK_SUCCESS);

   for (unsigned i = 0; i < (unsigned)max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PCI &&
          devices[i]->deviceinfo.pci->vendor_id == ATI_VENDOR_ID) {

         struct radv_physical_device *pdevice;
         result = radv_physical_device_try_create(instance, devices[i], &pdevice);
         /* Incompatible DRM device, skip. */
         if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
            result = VK_SUCCESS;
            continue;
         }

         /* Error creating the physical device, report the error. */
         if (result != VK_SUCCESS)
            break;

         list_addtail(&pdevice->link, &instance->physical_devices);
      }
   }
   drmFreeDevices(devices, max_devices);
#endif

   /* If we successfully enumerated any devices, call it success */
   return result;
}

VkResult
radv_EnumeratePhysicalDevices(VkInstance _instance, uint32_t *pPhysicalDeviceCount,
                              VkPhysicalDevice *pPhysicalDevices)
{
   RADV_FROM_HANDLE(radv_instance, instance, _instance);
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDevice, out, pPhysicalDevices, pPhysicalDeviceCount);

   VkResult result = radv_enumerate_physical_devices(instance);
   if (result != VK_SUCCESS)
      return result;

   list_for_each_entry(struct radv_physical_device, pdevice, &instance->physical_devices, link)
   {
      vk_outarray_append_typed(VkPhysicalDevice, &out, i)
      {
         *i = radv_physical_device_to_handle(pdevice);
      }
   }

   return vk_outarray_status(&out);
}

VkResult
radv_EnumeratePhysicalDeviceGroups(VkInstance _instance, uint32_t *pPhysicalDeviceGroupCount,
                                   VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties)
{
   RADV_FROM_HANDLE(radv_instance, instance, _instance);
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceGroupProperties, out, pPhysicalDeviceGroupProperties,
                          pPhysicalDeviceGroupCount);

   VkResult result = radv_enumerate_physical_devices(instance);
   if (result != VK_SUCCESS)
      return result;

   list_for_each_entry(struct radv_physical_device, pdevice, &instance->physical_devices, link)
   {
      vk_outarray_append_typed(VkPhysicalDeviceGroupProperties, &out, p)
      {
         p->physicalDeviceCount = 1;
         memset(p->physicalDevices, 0, sizeof(p->physicalDevices));
         p->physicalDevices[0] = radv_physical_device_to_handle(pdevice);
         p->subsetAllocation = false;
      }
   }

   return vk_outarray_status(&out);
}

void
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
      .textureCompressionETC2 = radv_device_supports_etc(pdevice),
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
   f->scalarBlockLayout = pdevice->rad_info.chip_class >= GFX7;
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

void
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

#define CORE_FEATURE(major, minor, feature) features->feature = core_##major##_##minor.feature

   vk_foreach_struct(ext, pFeatures->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
         VkPhysicalDeviceVariablePointersFeatures *features = (void *)ext;
         CORE_FEATURE(1, 1, variablePointersStorageBuffer);
         CORE_FEATURE(1, 1, variablePointers);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
         VkPhysicalDeviceMultiviewFeatures *features = (VkPhysicalDeviceMultiviewFeatures *)ext;
         CORE_FEATURE(1, 1, multiview);
         CORE_FEATURE(1, 1, multiviewGeometryShader);
         CORE_FEATURE(1, 1, multiviewTessellationShader);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: {
         VkPhysicalDeviceShaderDrawParametersFeatures *features =
            (VkPhysicalDeviceShaderDrawParametersFeatures *)ext;
         CORE_FEATURE(1, 1, shaderDrawParameters);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES: {
         VkPhysicalDeviceProtectedMemoryFeatures *features =
            (VkPhysicalDeviceProtectedMemoryFeatures *)ext;
         CORE_FEATURE(1, 1, protectedMemory);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
         VkPhysicalDevice16BitStorageFeatures *features =
            (VkPhysicalDevice16BitStorageFeatures *)ext;
         CORE_FEATURE(1, 1, storageBuffer16BitAccess);
         CORE_FEATURE(1, 1, uniformAndStorageBuffer16BitAccess);
         CORE_FEATURE(1, 1, storagePushConstant16);
         CORE_FEATURE(1, 1, storageInputOutput16);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
         VkPhysicalDeviceSamplerYcbcrConversionFeatures *features =
            (VkPhysicalDeviceSamplerYcbcrConversionFeatures *)ext;
         CORE_FEATURE(1, 1, samplerYcbcrConversion);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: {
         VkPhysicalDeviceDescriptorIndexingFeatures *features =
            (VkPhysicalDeviceDescriptorIndexingFeatures *)ext;
         CORE_FEATURE(1, 2, shaderInputAttachmentArrayDynamicIndexing);
         CORE_FEATURE(1, 2, shaderUniformTexelBufferArrayDynamicIndexing);
         CORE_FEATURE(1, 2, shaderStorageTexelBufferArrayDynamicIndexing);
         CORE_FEATURE(1, 2, shaderUniformBufferArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderSampledImageArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderStorageBufferArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderStorageImageArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderInputAttachmentArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderUniformTexelBufferArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderStorageTexelBufferArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, descriptorBindingUniformBufferUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingSampledImageUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingStorageImageUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingStorageBufferUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingUniformTexelBufferUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingStorageTexelBufferUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingUpdateUnusedWhilePending);
         CORE_FEATURE(1, 2, descriptorBindingPartiallyBound);
         CORE_FEATURE(1, 2, descriptorBindingVariableDescriptorCount);
         CORE_FEATURE(1, 2, runtimeDescriptorArray);
         break;
      }
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
         features->transformFeedback = true;
         features->geometryStreams = !pdevice->use_ngg_streamout;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: {
         VkPhysicalDeviceBufferDeviceAddressFeatures *features =
            (VkPhysicalDeviceBufferDeviceAddressFeatures *)ext;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES: {
         VkPhysicalDeviceHostQueryResetFeatures *features =
            (VkPhysicalDeviceHostQueryResetFeatures *)ext;
         CORE_FEATURE(1, 2, hostQueryReset);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: {
         VkPhysicalDevice8BitStorageFeatures *features = (VkPhysicalDevice8BitStorageFeatures *)ext;
         CORE_FEATURE(1, 2, storageBuffer8BitAccess);
         CORE_FEATURE(1, 2, uniformAndStorageBuffer8BitAccess);
         CORE_FEATURE(1, 2, storagePushConstant8);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: {
         VkPhysicalDeviceShaderFloat16Int8Features *features =
            (VkPhysicalDeviceShaderFloat16Int8Features *)ext;
         CORE_FEATURE(1, 2, shaderFloat16);
         CORE_FEATURE(1, 2, shaderInt8);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES: {
         VkPhysicalDeviceShaderAtomicInt64Features *features =
            (VkPhysicalDeviceShaderAtomicInt64Features *)ext;
         CORE_FEATURE(1, 2, shaderBufferInt64Atomics);
         CORE_FEATURE(1, 2, shaderSharedInt64Atomics);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT: {
         VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT *features =
            (VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT *)ext;
         features->shaderDemoteToHelperInvocation = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES_EXT: {
         VkPhysicalDeviceInlineUniformBlockFeaturesEXT *features =
            (VkPhysicalDeviceInlineUniformBlockFeaturesEXT *)ext;

         features->inlineUniformBlock = true;
         features->descriptorBindingInlineUniformBlockUpdateAfterBind = true;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES: {
         VkPhysicalDeviceUniformBufferStandardLayoutFeatures *features =
            (VkPhysicalDeviceUniformBufferStandardLayoutFeatures *)ext;
         CORE_FEATURE(1, 2, uniformBufferStandardLayout);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT: {
         VkPhysicalDeviceIndexTypeUint8FeaturesEXT *features =
            (VkPhysicalDeviceIndexTypeUint8FeaturesEXT *)ext;
         features->indexTypeUint8 = pdevice->rad_info.chip_class >= GFX8;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES: {
         VkPhysicalDeviceImagelessFramebufferFeatures *features =
            (VkPhysicalDeviceImagelessFramebufferFeatures *)ext;
         CORE_FEATURE(1, 2, imagelessFramebuffer);
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
         features->shaderDeviceClock = pdevice->rad_info.chip_class >= GFX8;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: {
         VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *features =
            (VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *)ext;
         features->texelBufferAlignment = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: {
         VkPhysicalDeviceTimelineSemaphoreFeatures *features =
            (VkPhysicalDeviceTimelineSemaphoreFeatures *)ext;
         CORE_FEATURE(1, 2, timelineSemaphore);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT: {
         VkPhysicalDeviceSubgroupSizeControlFeaturesEXT *features =
            (VkPhysicalDeviceSubgroupSizeControlFeaturesEXT *)ext;
         features->subgroupSizeControl = true;
         features->computeFullSubgroups = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD: {
         VkPhysicalDeviceCoherentMemoryFeaturesAMD *features =
            (VkPhysicalDeviceCoherentMemoryFeaturesAMD *)ext;
         features->deviceCoherentMemory = pdevice->rad_info.has_l2_uncached;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES: {
         VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *features =
            (VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *)ext;
         CORE_FEATURE(1, 2, shaderSubgroupExtendedTypes);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES_KHR: {
         VkPhysicalDeviceSeparateDepthStencilLayoutsFeaturesKHR *features =
            (VkPhysicalDeviceSeparateDepthStencilLayoutsFeaturesKHR *)ext;
         CORE_FEATURE(1, 2, separateDepthStencilLayouts);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
         radv_get_physical_device_features_1_1(pdevice, (void *)ext);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
         radv_get_physical_device_features_1_2(pdevice, (void *)ext);
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
         features->stippledBresenhamLines = pdevice->rad_info.chip_class != GFX9;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES_EXT: {
         VkPhysicalDevicePrivateDataFeaturesEXT *features =
            (VkPhysicalDevicePrivateDataFeaturesEXT *)ext;
         features->privateData = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT: {
         VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT *features =
            (VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT *)ext;
         features->pipelineCreationCacheControl = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES_KHR: {
         VkPhysicalDeviceVulkanMemoryModelFeaturesKHR *features =
            (VkPhysicalDeviceVulkanMemoryModelFeaturesKHR *)ext;
         CORE_FEATURE(1, 2, vulkanMemoryModel);
         CORE_FEATURE(1, 2, vulkanMemoryModelDeviceScope);
         CORE_FEATURE(1, 2, vulkanMemoryModelAvailabilityVisibilityChains);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *features =
            (VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *)ext;
         features->extendedDynamicState = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES_EXT: {
         VkPhysicalDeviceImageRobustnessFeaturesEXT *features =
            (VkPhysicalDeviceImageRobustnessFeaturesEXT *)ext;
         features->robustImageAccess = true;
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
         features->shaderSharedFloat32AtomicAdd = pdevice->rad_info.chip_class >= GFX8;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES_KHR: {
         VkPhysicalDeviceShaderTerminateInvocationFeaturesKHR *features =
            (VkPhysicalDeviceShaderTerminateInvocationFeaturesKHR *)ext;
         features->shaderTerminateInvocation = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT: {
         VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *features =
            (VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *)ext;
         features->shaderImageInt64Atomics = true;
         features->sparseImageInt64Atomics = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_VALVE: {
         VkPhysicalDeviceMutableDescriptorTypeFeaturesVALVE *features =
            (VkPhysicalDeviceMutableDescriptorTypeFeaturesVALVE *)ext;
         features->mutableDescriptorType = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR: {
         VkPhysicalDeviceFragmentShadingRateFeaturesKHR *features =
            (VkPhysicalDeviceFragmentShadingRateFeaturesKHR *)ext;
         features->pipelineFragmentShadingRate = true;
         features->primitiveFragmentShadingRate = true;
         features->attachmentFragmentShadingRate = true;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES_KHR: {
         VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeaturesKHR *features =
            (VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeaturesKHR *)ext;
         features->shaderZeroInitializeWorkgroupMemory = true;
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
         features->extendedDynamicState2PatchControlPoints = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES_EXT: {
         VkPhysicalDeviceGlobalPriorityQueryFeaturesEXT *features =
            (VkPhysicalDeviceGlobalPriorityQueryFeaturesEXT *)ext;
         features->globalPriorityQuery = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR: {
         VkPhysicalDeviceAccelerationStructureFeaturesKHR *features =
            (VkPhysicalDeviceAccelerationStructureFeaturesKHR *)ext;
         features->accelerationStructure = true;
         features->accelerationStructureCaptureReplay = false;
         features->accelerationStructureIndirectBuild = false;
         features->accelerationStructureHostCommands = true;
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
         bool has_shader_buffer_float_minmax = ((pdevice->rad_info.chip_class == GFX6 ||
                                                 pdevice->rad_info.chip_class == GFX7) &&
                                                !pdevice->use_llvm) ||
                                               pdevice->rad_info.chip_class >= GFX10;
         bool has_shader_image_float_minmax = pdevice->rad_info.chip_class != GFX8 &&
                                              pdevice->rad_info.chip_class != GFX9;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR: {
         VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR *features =
            (VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR *)ext;
         features->shaderIntegerDotProduct = true;
         break;
      }
      default:
         break;
      }
   }
#undef CORE_FEATURE
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

void
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
      .maxFragmentCombinedOutputResources = 8,
      .maxComputeSharedMemorySize = pdevice->rad_info.chip_class >= GFX7 ? 65536 : 32768,
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
      .maxFramebufferWidth = (1 << 14),
      .maxFramebufferHeight = (1 << 14),
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
      .optimalBufferCopyOffsetAlignment = 128,
      .optimalBufferCopyRowPitchAlignment = 128,
      .nonCoherentAtomSize = 64,
   };

   VkPhysicalDeviceType device_type;

   if (pdevice->rad_info.has_dedicated_vram || pdevice->instance->report_apu_as_dgpu) {
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
         },
   };

   strcpy(pProperties->deviceName, pdevice->name);
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
   p->conformanceVersion = (VkConformanceVersion){
      .major = 1,
      .minor = 2,
      .subminor = 3,
      .patch = 0,
   };

   /* On AMD hardware, denormals and rounding modes for fp16/fp64 are
    * controlled by the same config register.
    */
   if (pdevice->rad_info.has_packed_math_16bit) {
      p->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY_KHR;
      p->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY_KHR;
   } else {
      p->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL_KHR;
      p->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL_KHR;
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

   p->shaderDenormFlushToZeroFloat64 = pdevice->rad_info.chip_class >= GFX8 && !pdevice->use_llvm;
   p->shaderDenormPreserveFloat64 = pdevice->rad_info.chip_class >= GFX8;
   p->shaderRoundingModeRTEFloat64 = pdevice->rad_info.chip_class >= GFX8;
   p->shaderRoundingModeRTZFloat64 = pdevice->rad_info.chip_class >= GFX8 && !pdevice->use_llvm;
   p->shaderSignedZeroInfNanPreserveFloat64 = pdevice->rad_info.chip_class >= GFX8;

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
   p->supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR |
                                   VK_RESOLVE_MODE_AVERAGE_BIT_KHR | VK_RESOLVE_MODE_MIN_BIT_KHR |
                                   VK_RESOLVE_MODE_MAX_BIT_KHR;

   /* Average doesn't make sense for stencil so we don't support that */
   p->supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR |
                                     VK_RESOLVE_MODE_MIN_BIT_KHR | VK_RESOLVE_MODE_MAX_BIT_KHR;

   p->independentResolveNone = true;
   p->independentResolve = true;

   /* GFX6-8 only support single channel min/max filter. */
   p->filterMinmaxImageComponentMapping = pdevice->rad_info.chip_class >= GFX9;
   p->filterMinmaxSingleComponentFormats = true;

   p->maxTimelineSemaphoreValueDifference = UINT64_MAX;

   p->framebufferIntegerColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
}

void
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

#define CORE_RENAMED_PROPERTY(major, minor, ext_property, core_property)                           \
   memcpy(&properties->ext_property, &core_##major##_##minor.core_property,                        \
          sizeof(core_##major##_##minor.core_property))

#define CORE_PROPERTY(major, minor, property)                                                      \
   CORE_RENAMED_PROPERTY(major, minor, property, property)

   vk_foreach_struct(ext, pProperties->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *)ext;
         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
         VkPhysicalDeviceIDProperties *properties = (VkPhysicalDeviceIDProperties *)ext;
         CORE_PROPERTY(1, 1, deviceUUID);
         CORE_PROPERTY(1, 1, driverUUID);
         CORE_PROPERTY(1, 1, deviceLUID);
         CORE_PROPERTY(1, 1, deviceLUIDValid);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: {
         VkPhysicalDeviceMultiviewProperties *properties =
            (VkPhysicalDeviceMultiviewProperties *)ext;
         CORE_PROPERTY(1, 1, maxMultiviewViewCount);
         CORE_PROPERTY(1, 1, maxMultiviewInstanceIndex);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: {
         VkPhysicalDevicePointClippingProperties *properties =
            (VkPhysicalDevicePointClippingProperties *)ext;
         CORE_PROPERTY(1, 1, pointClippingBehavior);
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
         VkPhysicalDeviceSubgroupProperties *properties = (VkPhysicalDeviceSubgroupProperties *)ext;
         CORE_PROPERTY(1, 1, subgroupSize);
         CORE_RENAMED_PROPERTY(1, 1, supportedStages, subgroupSupportedStages);
         CORE_RENAMED_PROPERTY(1, 1, supportedOperations, subgroupSupportedOperations);
         CORE_RENAMED_PROPERTY(1, 1, quadOperationsInAllStages, subgroupQuadOperationsInAllStages);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
         VkPhysicalDeviceMaintenance3Properties *properties =
            (VkPhysicalDeviceMaintenance3Properties *)ext;
         CORE_PROPERTY(1, 1, maxPerSetDescriptors);
         CORE_PROPERTY(1, 1, maxMemoryAllocationSize);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES: {
         VkPhysicalDeviceSamplerFilterMinmaxProperties *properties =
            (VkPhysicalDeviceSamplerFilterMinmaxProperties *)ext;
         CORE_PROPERTY(1, 2, filterMinmaxImageComponentMapping);
         CORE_PROPERTY(1, 2, filterMinmaxSingleComponentFormats);
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
         properties->activeComputeUnitCount = pdevice->rad_info.num_good_compute_units;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *properties =
            (VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)ext;
         properties->maxVertexAttribDivisor = UINT32_MAX;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES: {
         VkPhysicalDeviceDescriptorIndexingProperties *properties =
            (VkPhysicalDeviceDescriptorIndexingProperties *)ext;
         CORE_PROPERTY(1, 2, maxUpdateAfterBindDescriptorsInAllPools);
         CORE_PROPERTY(1, 2, shaderUniformBufferArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, shaderSampledImageArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, shaderStorageBufferArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, shaderStorageImageArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, shaderInputAttachmentArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, robustBufferAccessUpdateAfterBind);
         CORE_PROPERTY(1, 2, quadDivergentImplicitLod);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindSamplers);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindUniformBuffers);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindStorageBuffers);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindSampledImages);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindStorageImages);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindInputAttachments);
         CORE_PROPERTY(1, 2, maxPerStageUpdateAfterBindResources);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindSamplers);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindUniformBuffers);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindUniformBuffersDynamic);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindStorageBuffers);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindStorageBuffersDynamic);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindSampledImages);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindStorageImages);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindInputAttachments);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES: {
         VkPhysicalDeviceProtectedMemoryProperties *properties =
            (VkPhysicalDeviceProtectedMemoryProperties *)ext;
         CORE_PROPERTY(1, 1, protectedNoFault);
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: {
         VkPhysicalDeviceDriverProperties *properties = (VkPhysicalDeviceDriverProperties *)ext;
         CORE_PROPERTY(1, 2, driverID);
         CORE_PROPERTY(1, 2, driverName);
         CORE_PROPERTY(1, 2, driverInfo);
         CORE_PROPERTY(1, 2, conformanceVersion);
         break;
      }
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES_EXT: {
         VkPhysicalDeviceInlineUniformBlockPropertiesEXT *props =
            (VkPhysicalDeviceInlineUniformBlockPropertiesEXT *)ext;

         props->maxInlineUniformBlockSize = MAX_INLINE_UNIFORM_BLOCK_SIZE;
         props->maxPerStageDescriptorInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_SETS;
         props->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
            MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_SETS;
         props->maxDescriptorSetInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_COUNT;
         props->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_COUNT;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES: {
         VkPhysicalDeviceDepthStencilResolveProperties *properties =
            (VkPhysicalDeviceDepthStencilResolveProperties *)ext;
         CORE_PROPERTY(1, 2, supportedDepthResolveModes);
         CORE_PROPERTY(1, 2, supportedStencilResolveModes);
         CORE_PROPERTY(1, 2, independentResolveNone);
         CORE_PROPERTY(1, 2, independentResolve);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES_EXT: {
         VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *properties =
            (VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *)ext;
         properties->storageTexelBufferOffsetAlignmentBytes = 4;
         properties->storageTexelBufferOffsetSingleTexelAlignment = true;
         properties->uniformTexelBufferOffsetAlignmentBytes = 4;
         properties->uniformTexelBufferOffsetSingleTexelAlignment = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES: {
         VkPhysicalDeviceFloatControlsProperties *properties =
            (VkPhysicalDeviceFloatControlsProperties *)ext;
         CORE_PROPERTY(1, 2, denormBehaviorIndependence);
         CORE_PROPERTY(1, 2, roundingModeIndependence);
         CORE_PROPERTY(1, 2, shaderDenormFlushToZeroFloat16);
         CORE_PROPERTY(1, 2, shaderDenormPreserveFloat16);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTEFloat16);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTZFloat16);
         CORE_PROPERTY(1, 2, shaderSignedZeroInfNanPreserveFloat16);
         CORE_PROPERTY(1, 2, shaderDenormFlushToZeroFloat32);
         CORE_PROPERTY(1, 2, shaderDenormPreserveFloat32);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTEFloat32);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTZFloat32);
         CORE_PROPERTY(1, 2, shaderSignedZeroInfNanPreserveFloat32);
         CORE_PROPERTY(1, 2, shaderDenormFlushToZeroFloat64);
         CORE_PROPERTY(1, 2, shaderDenormPreserveFloat64);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTEFloat64);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTZFloat64);
         CORE_PROPERTY(1, 2, shaderSignedZeroInfNanPreserveFloat64);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES: {
         VkPhysicalDeviceTimelineSemaphoreProperties *properties =
            (VkPhysicalDeviceTimelineSemaphoreProperties *)ext;
         CORE_PROPERTY(1, 2, maxTimelineSemaphoreValueDifference);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT: {
         VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *props =
            (VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *)ext;
         props->minSubgroupSize = 64;
         props->maxSubgroupSize = 64;
         props->maxComputeWorkgroupSubgroups = UINT32_MAX;
         props->requiredSubgroupSizeStages = 0;

         if (pdevice->rad_info.chip_class >= GFX10) {
            /* Only GFX10+ supports wave32. */
            props->minSubgroupSize = 32;
            props->requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
         }
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES:
         radv_get_physical_device_properties_1_1(pdevice, (void *)ext);
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
         radv_get_physical_device_properties_1_2(pdevice, (void *)ext);
         break;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES_KHR: {
         VkPhysicalDeviceShaderIntegerDotProductPropertiesKHR *props =
            (VkPhysicalDeviceShaderIntegerDotProductPropertiesKHR *)ext;

         bool accel = pdevice->rad_info.has_accelerated_dot_product;

         props->integerDotProduct8BitUnsignedAccelerated = accel;
         props->integerDotProduct8BitSignedAccelerated = accel;
         props->integerDotProduct8BitMixedSignednessAccelerated = false;
         props->integerDotProduct4x8BitPackedUnsignedAccelerated = accel;
         props->integerDotProduct4x8BitPackedSignedAccelerated = accel;
         props->integerDotProduct4x8BitPackedMixedSignednessAccelerated = false;
         props->integerDotProduct16BitUnsignedAccelerated = accel;
         props->integerDotProduct16BitSignedAccelerated = accel;
         props->integerDotProduct16BitMixedSignednessAccelerated = false;
         props->integerDotProduct32BitUnsignedAccelerated = false;
         props->integerDotProduct32BitSignedAccelerated = false;
         props->integerDotProduct32BitMixedSignednessAccelerated = false;
         props->integerDotProduct64BitUnsignedAccelerated = false;
         props->integerDotProduct64BitSignedAccelerated = false;
         props->integerDotProduct64BitMixedSignednessAccelerated = false;
         props->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated = accel;
         props->integerDotProductAccumulatingSaturating8BitSignedAccelerated = accel;
         props->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated = false;
         props->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = accel;
         props->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = accel;
         props->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated =
            false;
         props->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = accel;
         props->integerDotProductAccumulatingSaturating16BitSignedAccelerated = accel;
         props->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = false;
         props->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = false;
         props->integerDotProductAccumulatingSaturating32BitSignedAccelerated = false;
         props->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = false;
         props->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = false;
         props->integerDotProductAccumulatingSaturating64BitSignedAccelerated = false;
         props->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = false;
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
   if (pdevice->rad_info.num_rings[RING_COMPUTE] > 0 &&
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

   if (pdevice->rad_info.num_rings[RING_COMPUTE] > 0 &&
       !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE)) {
      if (*pCount > idx) {
         *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
            .queueFlags =
               VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT,
            .queueCount = pdevice->rad_info.num_rings[RING_COMPUTE],
            .timestampValidBits = 64,
            .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
         };
         idx++;
      }
   }
   *pCount = idx;
}

void
radv_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                            VkQueueFamilyProperties *pQueueFamilyProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   if (!pQueueFamilyProperties) {
      radv_get_physical_device_queue_family_properties(pdevice, pCount, NULL);
      return;
   }
   VkQueueFamilyProperties *properties[] = {
      pQueueFamilyProperties + 0,
      pQueueFamilyProperties + 1,
      pQueueFamilyProperties + 2,
   };
   radv_get_physical_device_queue_family_properties(pdevice, pCount, properties);
   assert(*pCount <= 3);
}

static const VkQueueGlobalPriorityEXT radv_global_queue_priorities[] = {
   VK_QUEUE_GLOBAL_PRIORITY_LOW_EXT,
   VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_EXT,
   VK_QUEUE_GLOBAL_PRIORITY_HIGH_EXT,
   VK_QUEUE_GLOBAL_PRIORITY_REALTIME_EXT,
};

void
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
         case VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_EXT: {
            VkQueueFamilyGlobalPriorityPropertiesEXT *prop =
               (VkQueueFamilyGlobalPriorityPropertiesEXT *)ext;
            STATIC_ASSERT(ARRAY_SIZE(radv_global_queue_priorities) <= VK_MAX_GLOBAL_PRIORITY_SIZE_EXT);
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

void
radv_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                       VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);

   *pMemoryProperties = physical_device->memory_properties;
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

void
radv_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                        VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   radv_GetPhysicalDeviceMemoryProperties(physicalDevice, &pMemoryProperties->memoryProperties);

   VkPhysicalDeviceMemoryBudgetPropertiesEXT *memory_budget =
      vk_find_struct(pMemoryProperties->pNext, PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT);
   if (memory_budget)
      radv_get_memory_budget_properties(physicalDevice, memory_budget);
}

VkResult
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
radv_get_queue_global_priority(const VkDeviceQueueGlobalPriorityCreateInfoEXT *pObj)
{
   /* Default to MEDIUM when a specific global priority isn't requested */
   if (!pObj)
      return RADEON_CTX_PRIORITY_MEDIUM;

   switch (pObj->globalPriority) {
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_EXT:
      return RADEON_CTX_PRIORITY_REALTIME;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_EXT:
      return RADEON_CTX_PRIORITY_HIGH;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_EXT:
      return RADEON_CTX_PRIORITY_MEDIUM;
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_EXT:
      return RADEON_CTX_PRIORITY_LOW;
   default:
      unreachable("Illegal global priority value");
      return RADEON_CTX_PRIORITY_INVALID;
   }
}

static int
radv_queue_init(struct radv_device *device, struct radv_queue *queue, uint32_t queue_family_index,
                int idx, VkDeviceQueueCreateFlags flags,
                const VkDeviceQueueGlobalPriorityCreateInfoEXT *global_priority)
{
   queue->device = device;
   queue->queue_family_index = queue_family_index;
   queue->queue_idx = idx;
   queue->priority = radv_get_queue_global_priority(global_priority);
   queue->flags = flags;
   queue->hw_ctx = device->hw_ctx[queue->priority];

   vk_object_base_init(&device->vk, &queue->base, VK_OBJECT_TYPE_QUEUE);

   list_inithead(&queue->pending_submissions);
   mtx_init(&queue->pending_mutex, mtx_plain);

   mtx_init(&queue->thread_mutex, mtx_plain);
   if (u_cnd_monotonic_init(&queue->thread_cond)) {
      vk_object_base_finish(&queue->base);
      return vk_error(device->instance, VK_ERROR_INITIALIZATION_FAILED);
   }
   queue->cond_created = true;

   return VK_SUCCESS;
}

static void
radv_queue_finish(struct radv_queue *queue)
{
   if (queue->hw_ctx) {
      if (queue->cond_created) {
         if (queue->thread_running) {
            p_atomic_set(&queue->thread_exit, true);
            u_cnd_monotonic_broadcast(&queue->thread_cond);
            thrd_join(queue->submission_thread, NULL);
         }

         u_cnd_monotonic_destroy(&queue->thread_cond);
      }

      mtx_destroy(&queue->pending_mutex);
      mtx_destroy(&queue->thread_mutex);
   }

   if (queue->initial_full_flush_preamble_cs)
      queue->device->ws->cs_destroy(queue->initial_full_flush_preamble_cs);
   if (queue->initial_preamble_cs)
      queue->device->ws->cs_destroy(queue->initial_preamble_cs);
   if (queue->continue_preamble_cs)
      queue->device->ws->cs_destroy(queue->continue_preamble_cs);
   if (queue->descriptor_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, queue->descriptor_bo);
   if (queue->scratch_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, queue->scratch_bo);
   if (queue->esgs_ring_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, queue->esgs_ring_bo);
   if (queue->gsvs_ring_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, queue->gsvs_ring_bo);
   if (queue->tess_rings_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, queue->tess_rings_bo);
   if (queue->gds_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, queue->gds_bo);
   if (queue->gds_oa_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, queue->gds_oa_bo);
   if (queue->compute_scratch_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, queue->compute_scratch_bo);

   vk_object_base_finish(&queue->base);
}

static void
radv_device_init_gs_info(struct radv_device *device)
{
   device->gs_table_depth = ac_get_gs_table_depth(device->physical_device->rad_info.chip_class,
                                                  device->physical_device->rad_info.family);
}

static VkResult
check_physical_device_features(VkPhysicalDevice physicalDevice,
                               const VkPhysicalDeviceFeatures *features)
{
   RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);
   VkPhysicalDeviceFeatures supported_features;
   radv_GetPhysicalDeviceFeatures(physicalDevice, &supported_features);
   VkBool32 *supported_feature = (VkBool32 *)&supported_features;
   VkBool32 *enabled_feature = (VkBool32 *)features;
   unsigned num_features = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
   for (uint32_t i = 0; i < num_features; i++) {
      if (enabled_feature[i] && !supported_feature[i])
         return vk_error(physical_device->instance, VK_ERROR_FEATURE_NOT_PRESENT);
   }

   return VK_SUCCESS;
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
      return vk_error(device->physical_device->instance, result);

   result = device->ws->buffer_make_resident(device->ws, device->border_color_data.bo, true);
   if (result != VK_SUCCESS)
      return vk_error(device->physical_device->instance, result);

   device->border_color_data.colors_gpu_ptr = device->ws->buffer_map(device->border_color_data.bo);
   if (!device->border_color_data.colors_gpu_ptr)
      return vk_error(device->physical_device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
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
   radv_FreeMemory(radv_device_to_handle(device), radv_device_memory_to_handle(device->vrs.mem),
                   &device->meta_state.alloc);
   radv_DestroyBuffer(radv_device_to_handle(device), radv_buffer_to_handle(device->vrs.buffer),
                     &device->meta_state.alloc);
   radv_DestroyImage(radv_device_to_handle(device), radv_image_to_handle(device->vrs.image),
                     &device->meta_state.alloc);
}

VkResult
_radv_device_set_lost(struct radv_device *device, const char *file, int line, const char *msg, ...)
{
   VkResult err;
   va_list ap;

   p_atomic_inc(&device->lost);

   va_start(ap, msg);
   err =
      __vk_errorv(device->physical_device->instance, device, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT,
                  VK_ERROR_DEVICE_LOST, file, line, msg, ap);
   va_end(ap);

   return err;
}

VkResult
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
   bool vrs_enabled = false;
   bool attachment_vrs_enabled = false;
   bool image_float32_atomics = false;

   /* Check enabled features */
   if (pCreateInfo->pEnabledFeatures) {
      result = check_physical_device_features(physicalDevice, pCreateInfo->pEnabledFeatures);
      if (result != VK_SUCCESS)
         return result;

      if (pCreateInfo->pEnabledFeatures->robustBufferAccess)
         robust_buffer_access = true;
   }

   vk_foreach_struct_const(ext, pCreateInfo->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: {
         const VkPhysicalDeviceFeatures2 *features = (const void *)ext;
         result = check_physical_device_features(physicalDevice, &features->features);
         if (result != VK_SUCCESS)
            return result;

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
         vrs_enabled = vrs->pipelineFragmentShadingRate || vrs->primitiveFragmentShadingRate ||
                       attachment_vrs_enabled;
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
      default:
         break;
      }
   }

   device = vk_zalloc2(&physical_device->instance->vk.alloc, pAllocator, sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(physical_device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;

   if (radv_thread_trace_enabled()) {
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &sqtt_device_entrypoints, true);
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &radv_device_entrypoints, false);
   } else {
      vk_device_dispatch_table_from_entrypoints(&dispatch_table, &radv_device_entrypoints, true);
   }

   result =
      vk_device_init(&device->vk, &physical_device->vk, &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device);
      return result;
   }

   device->instance = physical_device->instance;
   device->physical_device = physical_device;

   device->ws = physical_device->ws;

   keep_shader_info = device->vk.enabled_extensions.AMD_shader_info;

   /* With update after bind we can't attach bo's to the command buffer
    * from the descriptor set anymore, so we have to use a global BO list.
    */
   device->use_global_bo_list = (device->instance->perftest_flags & RADV_PERFTEST_BO_LIST) ||
                                device->vk.enabled_extensions.EXT_descriptor_indexing ||
                                device->vk.enabled_extensions.EXT_buffer_device_address ||
                                device->vk.enabled_extensions.KHR_buffer_device_address ||
                                device->vk.enabled_extensions.KHR_ray_tracing_pipeline ||
                                device->vk.enabled_extensions.KHR_acceleration_structure;

   device->robust_buffer_access = robust_buffer_access || robust_buffer_access2;
   device->robust_buffer_access2 = robust_buffer_access2;

   device->adjust_frag_coord_z =
      (vrs_enabled || device->vk.enabled_extensions.KHR_fragment_shading_rate ||
       device->force_vrs != RADV_FORCE_VRS_NONE) &&
      (device->physical_device->rad_info.family == CHIP_SIENNA_CICHLID ||
       device->physical_device->rad_info.family == CHIP_NAVY_FLOUNDER ||
       device->physical_device->rad_info.family == CHIP_VANGOGH);
   device->attachment_vrs_enabled = attachment_vrs_enabled;

   device->image_float32_atomics = image_float32_atomics;

   mtx_init(&device->shader_slab_mutex, mtx_plain);
   list_inithead(&device->shader_slabs);

   device->overallocation_disallowed = overallocation_disallowed;
   mtx_init(&device->overallocation_mutex, mtx_plain);

   /* Create one context per queue priority. */
   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create = &pCreateInfo->pQueueCreateInfos[i];
      const VkDeviceQueueGlobalPriorityCreateInfoEXT *global_priority =
         vk_find_struct_const(queue_create->pNext, DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_EXT);
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
      const VkDeviceQueueGlobalPriorityCreateInfoEXT *global_priority =
         vk_find_struct_const(queue_create->pNext, DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_EXT);

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
         result = radv_queue_init(device, &device->queues[qfi][q], qfi, q, queue_create->flags,
                                  global_priority);
         if (result != VK_SUCCESS)
            goto fail;
      }
   }

   device->pbb_allowed = device->physical_device->rad_info.chip_class >= GFX9 &&
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
      MAX2(32 * physical_device->rad_info.num_good_compute_units, max_threads_per_block / 64);

   device->dispatch_initiator = S_00B800_COMPUTE_SHADER_EN(1);

   if (device->physical_device->rad_info.chip_class >= GFX7) {
      /* If the KMD allows it (there is a KMD hw register for it),
       * allow launching waves out-of-order.
       */
      device->dispatch_initiator |= S_00B800_ORDER_MODE(1);
   }

   radv_device_init_gs_info(device);

   device->tess_offchip_block_dw_size =
      device->physical_device->rad_info.family == CHIP_HAWAII ? 4096 : 8192;

   if (getenv("RADV_TRACE_FILE")) {
      fprintf(
         stderr,
         "***********************************************************************************\n");
      fprintf(
         stderr,
         "* WARNING: RADV_TRACE_FILE=<file> is deprecated and replaced by RADV_DEBUG=hang *\n");
      fprintf(
         stderr,
         "***********************************************************************************\n");
      abort();
   }

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
      fprintf(stderr, "*************************************************\n");
      fprintf(stderr, "* WARNING: Thread trace support is experimental *\n");
      fprintf(stderr, "*************************************************\n");

      if (device->physical_device->rad_info.chip_class < GFX8 ||
          device->physical_device->rad_info.chip_class > GFX10_3) {
         fprintf(stderr, "GPU hardware not supported: refer to "
                         "the RGP documentation for the list of "
                         "supported GPUs!\n");
         abort();
      }

      if (!radv_thread_trace_init(device))
         goto fail;
   }

   if (getenv("RADV_TRAP_HANDLER")) {
      /* TODO: Add support for more hardware. */
      assert(device->physical_device->rad_info.chip_class == GFX8);

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

   if (getenv("RADV_FORCE_VRS")) {
      const char *vrs_rates = getenv("RADV_FORCE_VRS");

      if (device->physical_device->rad_info.chip_class < GFX10_3)
         fprintf(stderr, "radv: VRS is only supported on RDNA2+\n");
      else if (!strcmp(vrs_rates, "2x2"))
         device->force_vrs = RADV_FORCE_VRS_2x2;
      else if (!strcmp(vrs_rates, "2x1"))
         device->force_vrs = RADV_FORCE_VRS_2x1;
      else if (!strcmp(vrs_rates, "1x2"))
         device->force_vrs = RADV_FORCE_VRS_1x2;
      else
         fprintf(stderr, "radv: Invalid VRS rates specified "
                         "(valid values are 2x2, 2x1 and 1x2)\n");
   }

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

   for (int family = 0; family < RADV_MAX_QUEUE_FAMILIES; ++family) {
      device->empty_cs[family] = device->ws->cs_create(device->ws, family);
      if (!device->empty_cs[family])
         goto fail;

      switch (family) {
      case RADV_QUEUE_GENERAL:
         radeon_emit(device->empty_cs[family], PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
         radeon_emit(device->empty_cs[family], CC0_UPDATE_LOAD_ENABLES(1));
         radeon_emit(device->empty_cs[family], CC1_UPDATE_SHADOW_ENABLES(1));
         break;
      case RADV_QUEUE_COMPUTE:
         radeon_emit(device->empty_cs[family], PKT3(PKT3_NOP, 0, 0));
         radeon_emit(device->empty_cs[family], 0);
         break;
      }

      result = device->ws->cs_finalize(device->empty_cs[family]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (device->physical_device->rad_info.chip_class >= GFX7)
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

   if (u_cnd_monotonic_init(&device->timeline_cond)) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto fail_mem_cache;
   }

   device->force_aniso = MIN2(16, radv_get_int_debug_option("RADV_TEX_ANISO", -1));
   if (device->force_aniso >= 0) {
      fprintf(stderr, "radv: Forcing anisotropy filter to %ix\n",
              1 << util_logbase2(device->force_aniso));
   }

   *pDevice = radv_device_to_handle(device);
   return VK_SUCCESS;

fail_mem_cache:
   radv_DestroyPipelineCache(radv_device_to_handle(device), pc, NULL);
fail_meta:
   radv_device_finish_meta(device);
fail:
   radv_thread_trace_finish(device);
   free(device->thread_trace.trigger_file);

   radv_trap_handler_finish(device);
   radv_finish_trace(device);

   if (device->gfx_init)
      device->ws->buffer_destroy(device->ws, device->gfx_init);

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

   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
   return result;
}

void
radv_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   if (!device)
      return;

   if (device->gfx_init)
      device->ws->buffer_destroy(device->ws, device->gfx_init);

   radv_device_finish_border_color(device);
   radv_device_finish_vrs_image(device);

   for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++)
         radv_queue_finish(&device->queues[i][q]);
      if (device->queue_count[i])
         vk_free(&device->vk.alloc, device->queues[i]);
      if (device->empty_cs[i])
         device->ws->cs_destroy(device->empty_cs[i]);
   }

   for (unsigned i = 0; i < RADV_NUM_HW_CTX; i++) {
      if (device->hw_ctx[i])
         device->ws->ctx_destroy(device->hw_ctx[i]);
   }

   radv_device_finish_meta(device);

   VkPipelineCache pc = radv_pipeline_cache_to_handle(device->mem_cache);
   radv_DestroyPipelineCache(radv_device_to_handle(device), pc, NULL);

   radv_trap_handler_finish(device);
   radv_finish_trace(device);

   radv_destroy_shader_slabs(device);

   u_cnd_monotonic_destroy(&device->timeline_cond);

   free(device->thread_trace.trigger_file);
   radv_thread_trace_finish(device);

   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VkResult
radv_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount, VkLayerProperties *pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VkResult
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

void
radv_GetDeviceQueue2(VkDevice _device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_queue *queue;

   queue = &device->queues[pQueueInfo->queueFamilyIndex][pQueueInfo->queueIndex];
   if (pQueueInfo->flags != queue->flags) {
      /* From the Vulkan 1.1.70 spec:
       *
       * "The queue returned by vkGetDeviceQueue2 must have the same
       * flags value from this structure as that used at device
       * creation time in a VkDeviceQueueCreateInfo instance. If no
       * matching flags were specified at device creation time then
       * pQueue will return VK_NULL_HANDLE."
       */
      *pQueue = VK_NULL_HANDLE;
      return;
   }

   *pQueue = radv_queue_to_handle(queue);
}

static void
fill_geom_tess_rings(struct radv_queue *queue, uint32_t *map, bool add_sample_positions,
                     uint32_t esgs_ring_size, struct radeon_winsys_bo *esgs_ring_bo,
                     uint32_t gsvs_ring_size, struct radeon_winsys_bo *gsvs_ring_bo,
                     uint32_t tess_factor_ring_size, uint32_t tess_offchip_ring_offset,
                     uint32_t tess_offchip_ring_size, struct radeon_winsys_bo *tess_rings_bo)
{
   uint32_t *desc = &map[4];

   if (esgs_ring_bo) {
      uint64_t esgs_va = radv_buffer_get_va(esgs_ring_bo);

      /* stride 0, num records - size, add tid, swizzle, elsize4,
         index stride 64 */
      desc[0] = esgs_va;
      desc[1] = S_008F04_BASE_ADDRESS_HI(esgs_va >> 32) | S_008F04_SWIZZLE_ENABLE(true);
      desc[2] = esgs_ring_size;
      desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
                S_008F0C_INDEX_STRIDE(3) | S_008F0C_ADD_TID_ENABLE(1);

      if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
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

      if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
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

      if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
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
      desc[5] = S_008F04_BASE_ADDRESS_HI(gsvs_va >> 32) | S_008F04_SWIZZLE_ENABLE(1);
      desc[6] = 0;
      desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
                S_008F0C_INDEX_STRIDE(1) | S_008F0C_ADD_TID_ENABLE(true);

      if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
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
      uint64_t tess_offchip_va = tess_va + tess_offchip_ring_offset;

      desc[0] = tess_va;
      desc[1] = S_008F04_BASE_ADDRESS_HI(tess_va >> 32);
      desc[2] = tess_factor_ring_size;
      desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
         desc[3] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc[3] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
      }

      desc[4] = tess_offchip_va;
      desc[5] = S_008F04_BASE_ADDRESS_HI(tess_offchip_va >> 32);
      desc[6] = tess_offchip_ring_size;
      desc[7] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

      if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
         desc[7] |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                    S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc[7] |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                    S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
      }
   }

   desc += 8;

   if (add_sample_positions) {
      /* add sample positions after all rings */
      memcpy(desc, queue->device->sample_locations_1x, 8);
      desc += 2;
      memcpy(desc, queue->device->sample_locations_2x, 16);
      desc += 4;
      memcpy(desc, queue->device->sample_locations_4x, 32);
      desc += 8;
      memcpy(desc, queue->device->sample_locations_8x, 64);
   }
}

static unsigned
radv_get_hs_offchip_param(struct radv_device *device, uint32_t *max_offchip_buffers_p)
{
   bool double_offchip_buffers = device->physical_device->rad_info.chip_class >= GFX7 &&
                                 device->physical_device->rad_info.family != CHIP_CARRIZO &&
                                 device->physical_device->rad_info.family != CHIP_STONEY;
   unsigned max_offchip_buffers_per_se = double_offchip_buffers ? 128 : 64;
   unsigned max_offchip_buffers;
   unsigned offchip_granularity;
   unsigned hs_offchip_param;

   /*
    * Per RadeonSI:
    * This must be one less than the maximum number due to a hw limitation.
    * Various hardware bugs need thGFX7
    *
    * Per AMDVLK:
    * Vega10 should limit max_offchip_buffers to 508 (4 * 127).
    * Gfx7 should limit max_offchip_buffers to 508
    * Gfx6 should limit max_offchip_buffers to 126 (2 * 63)
    *
    * Follow AMDVLK here.
    */
   if (device->physical_device->rad_info.chip_class >= GFX10) {
      max_offchip_buffers_per_se = 128;
   } else if (device->physical_device->rad_info.family == CHIP_VEGA10 ||
              device->physical_device->rad_info.chip_class == GFX7 ||
              device->physical_device->rad_info.chip_class == GFX6)
      --max_offchip_buffers_per_se;

   max_offchip_buffers = max_offchip_buffers_per_se * device->physical_device->rad_info.max_se;

   /* Hawaii has a bug with offchip buffers > 256 that can be worked
    * around by setting 4K granularity.
    */
   if (device->tess_offchip_block_dw_size == 4096) {
      assert(device->physical_device->rad_info.family == CHIP_HAWAII);
      offchip_granularity = V_03093C_X_4K_DWORDS;
   } else {
      assert(device->tess_offchip_block_dw_size == 8192);
      offchip_granularity = V_03093C_X_8K_DWORDS;
   }

   switch (device->physical_device->rad_info.chip_class) {
   case GFX6:
      max_offchip_buffers = MIN2(max_offchip_buffers, 126);
      break;
   case GFX7:
   case GFX8:
   case GFX9:
      max_offchip_buffers = MIN2(max_offchip_buffers, 508);
      break;
   case GFX10:
      break;
   default:
      break;
   }

   *max_offchip_buffers_p = max_offchip_buffers;
   if (device->physical_device->rad_info.chip_class >= GFX10_3) {
      hs_offchip_param = S_03093C_OFFCHIP_BUFFERING_GFX103(max_offchip_buffers - 1) |
                         S_03093C_OFFCHIP_GRANULARITY_GFX103(offchip_granularity);
   } else if (device->physical_device->rad_info.chip_class >= GFX7) {
      if (device->physical_device->rad_info.chip_class >= GFX8)
         --max_offchip_buffers;
      hs_offchip_param = S_03093C_OFFCHIP_BUFFERING_GFX7(max_offchip_buffers) |
                         S_03093C_OFFCHIP_GRANULARITY_GFX7(offchip_granularity);
   } else {
      hs_offchip_param = S_0089B0_OFFCHIP_BUFFERING(max_offchip_buffers);
   }
   return hs_offchip_param;
}

static void
radv_emit_gs_ring_sizes(struct radv_queue *queue, struct radeon_cmdbuf *cs,
                        struct radeon_winsys_bo *esgs_ring_bo, uint32_t esgs_ring_size,
                        struct radeon_winsys_bo *gsvs_ring_bo, uint32_t gsvs_ring_size)
{
   if (!esgs_ring_bo && !gsvs_ring_bo)
      return;

   if (esgs_ring_bo)
      radv_cs_add_buffer(queue->device->ws, cs, esgs_ring_bo);

   if (gsvs_ring_bo)
      radv_cs_add_buffer(queue->device->ws, cs, gsvs_ring_bo);

   if (queue->device->physical_device->rad_info.chip_class >= GFX7) {
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
radv_emit_tess_factor_ring(struct radv_queue *queue, struct radeon_cmdbuf *cs,
                           unsigned hs_offchip_param, unsigned tf_ring_size,
                           struct radeon_winsys_bo *tess_rings_bo)
{
   uint64_t tf_va;

   if (!tess_rings_bo)
      return;

   tf_va = radv_buffer_get_va(tess_rings_bo);

   radv_cs_add_buffer(queue->device->ws, cs, tess_rings_bo);

   if (queue->device->physical_device->rad_info.chip_class >= GFX7) {
      radeon_set_uconfig_reg(cs, R_030938_VGT_TF_RING_SIZE, S_030938_SIZE(tf_ring_size / 4));
      radeon_set_uconfig_reg(cs, R_030940_VGT_TF_MEMORY_BASE, tf_va >> 8);

      if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
         radeon_set_uconfig_reg(cs, R_030984_VGT_TF_MEMORY_BASE_HI_UMD,
                                S_030984_BASE_HI(tf_va >> 40));
      } else if (queue->device->physical_device->rad_info.chip_class == GFX9) {
         radeon_set_uconfig_reg(cs, R_030944_VGT_TF_MEMORY_BASE_HI, S_030944_BASE_HI(tf_va >> 40));
      }
      radeon_set_uconfig_reg(cs, R_03093C_VGT_HS_OFFCHIP_PARAM, hs_offchip_param);
   } else {
      radeon_set_config_reg(cs, R_008988_VGT_TF_RING_SIZE, S_008988_SIZE(tf_ring_size / 4));
      radeon_set_config_reg(cs, R_0089B8_VGT_TF_MEMORY_BASE, tf_va >> 8);
      radeon_set_config_reg(cs, R_0089B0_VGT_HS_OFFCHIP_PARAM, hs_offchip_param);
   }
}

static void
radv_emit_graphics_scratch(struct radv_queue *queue, struct radeon_cmdbuf *cs,
                           uint32_t size_per_wave, uint32_t waves,
                           struct radeon_winsys_bo *scratch_bo)
{
   if (queue->queue_family_index != RADV_QUEUE_GENERAL)
      return;

   if (!scratch_bo)
      return;

   radv_cs_add_buffer(queue->device->ws, cs, scratch_bo);

   radeon_set_context_reg(
      cs, R_0286E8_SPI_TMPRING_SIZE,
      S_0286E8_WAVES(waves) | S_0286E8_WAVESIZE(round_up_u32(size_per_wave, 1024)));
}

static void
radv_emit_compute_scratch(struct radv_queue *queue, struct radeon_cmdbuf *cs,
                          uint32_t size_per_wave, uint32_t waves,
                          struct radeon_winsys_bo *compute_scratch_bo)
{
   uint64_t scratch_va;

   if (!compute_scratch_bo)
      return;

   scratch_va = radv_buffer_get_va(compute_scratch_bo);

   radv_cs_add_buffer(queue->device->ws, cs, compute_scratch_bo);

   radeon_set_sh_reg_seq(cs, R_00B900_COMPUTE_USER_DATA_0, 2);
   radeon_emit(cs, scratch_va);
   radeon_emit(cs, S_008F04_BASE_ADDRESS_HI(scratch_va >> 32) | S_008F04_SWIZZLE_ENABLE(1));

   radeon_set_sh_reg(cs, R_00B860_COMPUTE_TMPRING_SIZE,
                     S_00B860_WAVES(waves) | S_00B860_WAVESIZE(round_up_u32(size_per_wave, 1024)));
}

static void
radv_emit_global_shader_pointers(struct radv_queue *queue, struct radeon_cmdbuf *cs,
                                 struct radeon_winsys_bo *descriptor_bo)
{
   uint64_t va;

   if (!descriptor_bo)
      return;

   va = radv_buffer_get_va(descriptor_bo);

   radv_cs_add_buffer(queue->device->ws, cs, descriptor_bo);

   if (queue->device->physical_device->rad_info.chip_class >= GFX10) {
      uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0, R_00B130_SPI_SHADER_USER_DATA_VS_0,
                         R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS,
                         R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS};

      for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
         radv_emit_shader_pointer(queue->device, cs, regs[i], va, true);
      }
   } else if (queue->device->physical_device->rad_info.chip_class == GFX9) {
      uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0, R_00B130_SPI_SHADER_USER_DATA_VS_0,
                         R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS,
                         R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS};

      for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
         radv_emit_shader_pointer(queue->device, cs, regs[i], va, true);
      }
   } else {
      uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0, R_00B130_SPI_SHADER_USER_DATA_VS_0,
                         R_00B230_SPI_SHADER_USER_DATA_GS_0, R_00B330_SPI_SHADER_USER_DATA_ES_0,
                         R_00B430_SPI_SHADER_USER_DATA_HS_0, R_00B530_SPI_SHADER_USER_DATA_LS_0};

      for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
         radv_emit_shader_pointer(queue->device, cs, regs[i], va, true);
      }
   }
}

static void
radv_init_graphics_state(struct radeon_cmdbuf *cs, struct radv_queue *queue)
{
   struct radv_device *device = queue->device;

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
radv_init_compute_state(struct radeon_cmdbuf *cs, struct radv_queue *queue)
{
   si_emit_compute(queue->device, cs);
}

static VkResult
radv_get_preamble_cs(struct radv_queue *queue, uint32_t scratch_size_per_wave,
                     uint32_t scratch_waves, uint32_t compute_scratch_size_per_wave,
                     uint32_t compute_scratch_waves, uint32_t esgs_ring_size,
                     uint32_t gsvs_ring_size, bool needs_tess_rings, bool needs_gds,
                     bool needs_gds_oa, bool needs_sample_positions,
                     struct radeon_cmdbuf **initial_full_flush_preamble_cs,
                     struct radeon_cmdbuf **initial_preamble_cs,
                     struct radeon_cmdbuf **continue_preamble_cs)
{
   struct radeon_winsys_bo *scratch_bo = NULL;
   struct radeon_winsys_bo *descriptor_bo = NULL;
   struct radeon_winsys_bo *compute_scratch_bo = NULL;
   struct radeon_winsys_bo *esgs_ring_bo = NULL;
   struct radeon_winsys_bo *gsvs_ring_bo = NULL;
   struct radeon_winsys_bo *tess_rings_bo = NULL;
   struct radeon_winsys_bo *gds_bo = NULL;
   struct radeon_winsys_bo *gds_oa_bo = NULL;
   struct radeon_cmdbuf *dest_cs[3] = {0};
   bool add_tess_rings = false, add_gds = false, add_gds_oa = false, add_sample_positions = false;
   unsigned tess_factor_ring_size = 0, tess_offchip_ring_size = 0;
   unsigned max_offchip_buffers;
   unsigned hs_offchip_param = 0;
   unsigned tess_offchip_ring_offset;
   uint32_t ring_bo_flags = RADEON_FLAG_NO_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING;
   VkResult result = VK_SUCCESS;
   if (!queue->has_tess_rings) {
      if (needs_tess_rings)
         add_tess_rings = true;
   }
   if (!queue->has_gds) {
      if (needs_gds)
         add_gds = true;
   }
   if (!queue->has_gds_oa) {
      if (needs_gds_oa)
         add_gds_oa = true;
   }
   if (!queue->has_sample_positions) {
      if (needs_sample_positions)
         add_sample_positions = true;
   }
   tess_factor_ring_size = 32768 * queue->device->physical_device->rad_info.max_se;
   hs_offchip_param = radv_get_hs_offchip_param(queue->device, &max_offchip_buffers);
   tess_offchip_ring_offset = align(tess_factor_ring_size, 64 * 1024);
   tess_offchip_ring_size = max_offchip_buffers * queue->device->tess_offchip_block_dw_size * 4;

   scratch_size_per_wave = MAX2(scratch_size_per_wave, queue->scratch_size_per_wave);
   if (scratch_size_per_wave)
      scratch_waves = MIN2(scratch_waves, UINT32_MAX / scratch_size_per_wave);
   else
      scratch_waves = 0;

   compute_scratch_size_per_wave =
      MAX2(compute_scratch_size_per_wave, queue->compute_scratch_size_per_wave);
   if (compute_scratch_size_per_wave)
      compute_scratch_waves =
         MIN2(compute_scratch_waves, UINT32_MAX / compute_scratch_size_per_wave);
   else
      compute_scratch_waves = 0;

   if (scratch_size_per_wave <= queue->scratch_size_per_wave &&
       scratch_waves <= queue->scratch_waves &&
       compute_scratch_size_per_wave <= queue->compute_scratch_size_per_wave &&
       compute_scratch_waves <= queue->compute_scratch_waves &&
       esgs_ring_size <= queue->esgs_ring_size && gsvs_ring_size <= queue->gsvs_ring_size &&
       !add_tess_rings && !add_gds && !add_gds_oa && !add_sample_positions &&
       queue->initial_preamble_cs) {
      *initial_full_flush_preamble_cs = queue->initial_full_flush_preamble_cs;
      *initial_preamble_cs = queue->initial_preamble_cs;
      *continue_preamble_cs = queue->continue_preamble_cs;
      if (!scratch_size_per_wave && !compute_scratch_size_per_wave && !esgs_ring_size &&
          !gsvs_ring_size && !needs_tess_rings && !needs_gds && !needs_gds_oa &&
          !needs_sample_positions)
         *continue_preamble_cs = NULL;
      return VK_SUCCESS;
   }

   uint32_t scratch_size = scratch_size_per_wave * scratch_waves;
   uint32_t queue_scratch_size = queue->scratch_size_per_wave * queue->scratch_waves;
   if (scratch_size > queue_scratch_size) {
      result =
         queue->device->ws->buffer_create(queue->device->ws, scratch_size, 4096, RADEON_DOMAIN_VRAM,
                                          ring_bo_flags, RADV_BO_PRIORITY_SCRATCH, 0, &scratch_bo);
      if (result != VK_SUCCESS)
         goto fail;
   } else
      scratch_bo = queue->scratch_bo;

   uint32_t compute_scratch_size = compute_scratch_size_per_wave * compute_scratch_waves;
   uint32_t compute_queue_scratch_size =
      queue->compute_scratch_size_per_wave * queue->compute_scratch_waves;
   if (compute_scratch_size > compute_queue_scratch_size) {
      result = queue->device->ws->buffer_create(queue->device->ws, compute_scratch_size, 4096,
                                                RADEON_DOMAIN_VRAM, ring_bo_flags,
                                                RADV_BO_PRIORITY_SCRATCH, 0, &compute_scratch_bo);
      if (result != VK_SUCCESS)
         goto fail;

   } else
      compute_scratch_bo = queue->compute_scratch_bo;

   if (esgs_ring_size > queue->esgs_ring_size) {
      result = queue->device->ws->buffer_create(queue->device->ws, esgs_ring_size, 4096,
                                                RADEON_DOMAIN_VRAM, ring_bo_flags,
                                                RADV_BO_PRIORITY_SCRATCH, 0, &esgs_ring_bo);
      if (result != VK_SUCCESS)
         goto fail;
   } else {
      esgs_ring_bo = queue->esgs_ring_bo;
      esgs_ring_size = queue->esgs_ring_size;
   }

   if (gsvs_ring_size > queue->gsvs_ring_size) {
      result = queue->device->ws->buffer_create(queue->device->ws, gsvs_ring_size, 4096,
                                                RADEON_DOMAIN_VRAM, ring_bo_flags,
                                                RADV_BO_PRIORITY_SCRATCH, 0, &gsvs_ring_bo);
      if (result != VK_SUCCESS)
         goto fail;
   } else {
      gsvs_ring_bo = queue->gsvs_ring_bo;
      gsvs_ring_size = queue->gsvs_ring_size;
   }

   if (add_tess_rings) {
      result = queue->device->ws->buffer_create(
         queue->device->ws, tess_offchip_ring_offset + tess_offchip_ring_size, 256,
         RADEON_DOMAIN_VRAM, ring_bo_flags, RADV_BO_PRIORITY_SCRATCH, 0, &tess_rings_bo);
      if (result != VK_SUCCESS)
         goto fail;
   } else {
      tess_rings_bo = queue->tess_rings_bo;
   }

   if (add_gds) {
      assert(queue->device->physical_device->rad_info.chip_class >= GFX10);

      /* 4 streamout GDS counters.
       * We need 256B (64 dw) of GDS, otherwise streamout hangs.
       */
      result =
         queue->device->ws->buffer_create(queue->device->ws, 256, 4, RADEON_DOMAIN_GDS,
                                          ring_bo_flags, RADV_BO_PRIORITY_SCRATCH, 0, &gds_bo);
      if (result != VK_SUCCESS)
         goto fail;
   } else {
      gds_bo = queue->gds_bo;
   }

   if (add_gds_oa) {
      assert(queue->device->physical_device->rad_info.chip_class >= GFX10);

      result =
         queue->device->ws->buffer_create(queue->device->ws, 4, 1, RADEON_DOMAIN_OA, ring_bo_flags,
                                          RADV_BO_PRIORITY_SCRATCH, 0, &gds_oa_bo);
      if (result != VK_SUCCESS)
         goto fail;
   } else {
      gds_oa_bo = queue->gds_oa_bo;
   }

   if (scratch_bo != queue->scratch_bo || esgs_ring_bo != queue->esgs_ring_bo ||
       gsvs_ring_bo != queue->gsvs_ring_bo || tess_rings_bo != queue->tess_rings_bo ||
       add_sample_positions) {
      uint32_t size = 0;
      if (gsvs_ring_bo || esgs_ring_bo || tess_rings_bo || add_sample_positions) {
         size = 112; /* 2 dword + 2 padding + 4 dword * 6 */
         if (add_sample_positions)
            size += 128; /* 64+32+16+8 = 120 bytes */
      } else if (scratch_bo)
         size = 8; /* 2 dword */

      result = queue->device->ws->buffer_create(
         queue->device->ws, size, 4096, RADEON_DOMAIN_VRAM,
         RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY,
         RADV_BO_PRIORITY_DESCRIPTOR, 0, &descriptor_bo);
      if (result != VK_SUCCESS)
         goto fail;
   } else
      descriptor_bo = queue->descriptor_bo;

   if (descriptor_bo != queue->descriptor_bo) {
      uint32_t *map = (uint32_t *)queue->device->ws->buffer_map(descriptor_bo);
      if (!map)
         goto fail;

      if (scratch_bo) {
         uint64_t scratch_va = radv_buffer_get_va(scratch_bo);
         uint32_t rsrc1 = S_008F04_BASE_ADDRESS_HI(scratch_va >> 32) | S_008F04_SWIZZLE_ENABLE(1);
         map[0] = scratch_va;
         map[1] = rsrc1;
      }

      if (esgs_ring_bo || gsvs_ring_bo || tess_rings_bo || add_sample_positions)
         fill_geom_tess_rings(queue, map, add_sample_positions, esgs_ring_size, esgs_ring_bo,
                              gsvs_ring_size, gsvs_ring_bo, tess_factor_ring_size,
                              tess_offchip_ring_offset, tess_offchip_ring_size, tess_rings_bo);

      queue->device->ws->buffer_unmap(descriptor_bo);
   }

   for (int i = 0; i < 3; ++i) {
      enum rgp_flush_bits sqtt_flush_bits = 0;
      struct radeon_cmdbuf *cs = NULL;
      cs = queue->device->ws->cs_create(queue->device->ws,
                                        queue->queue_family_index ? RING_COMPUTE : RING_GFX);
      if (!cs) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      dest_cs[i] = cs;

      if (scratch_bo)
         radv_cs_add_buffer(queue->device->ws, cs, scratch_bo);

      /* Emit initial configuration. */
      switch (queue->queue_family_index) {
      case RADV_QUEUE_GENERAL:
         radv_init_graphics_state(cs, queue);
         break;
      case RADV_QUEUE_COMPUTE:
         radv_init_compute_state(cs, queue);
         break;
      case RADV_QUEUE_TRANSFER:
         break;
      }

      if (esgs_ring_bo || gsvs_ring_bo || tess_rings_bo) {
         radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
         radeon_emit(cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));

         radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
         radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
      }

      radv_emit_gs_ring_sizes(queue, cs, esgs_ring_bo, esgs_ring_size, gsvs_ring_bo,
                              gsvs_ring_size);
      radv_emit_tess_factor_ring(queue, cs, hs_offchip_param, tess_factor_ring_size, tess_rings_bo);
      radv_emit_global_shader_pointers(queue, cs, descriptor_bo);
      radv_emit_compute_scratch(queue, cs, compute_scratch_size_per_wave, compute_scratch_waves,
                                compute_scratch_bo);
      radv_emit_graphics_scratch(queue, cs, scratch_size_per_wave, scratch_waves, scratch_bo);

      if (gds_bo)
         radv_cs_add_buffer(queue->device->ws, cs, gds_bo);
      if (gds_oa_bo)
         radv_cs_add_buffer(queue->device->ws, cs, gds_oa_bo);

      if (i == 0) {
         si_cs_emit_cache_flush(
            cs, queue->device->physical_device->rad_info.chip_class, NULL, 0,
            queue->queue_family_index == RING_COMPUTE &&
               queue->device->physical_device->rad_info.chip_class >= GFX7,
            (queue->queue_family_index == RADV_QUEUE_COMPUTE
                ? RADV_CMD_FLAG_CS_PARTIAL_FLUSH
                : (RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH)) |
               RADV_CMD_FLAG_INV_ICACHE | RADV_CMD_FLAG_INV_SCACHE | RADV_CMD_FLAG_INV_VCACHE |
               RADV_CMD_FLAG_INV_L2 | RADV_CMD_FLAG_START_PIPELINE_STATS,
            &sqtt_flush_bits, 0);
      } else if (i == 1) {
         si_cs_emit_cache_flush(cs, queue->device->physical_device->rad_info.chip_class, NULL, 0,
                                queue->queue_family_index == RING_COMPUTE &&
                                   queue->device->physical_device->rad_info.chip_class >= GFX7,
                                RADV_CMD_FLAG_INV_ICACHE | RADV_CMD_FLAG_INV_SCACHE |
                                   RADV_CMD_FLAG_INV_VCACHE | RADV_CMD_FLAG_INV_L2 |
                                   RADV_CMD_FLAG_START_PIPELINE_STATS,
                                &sqtt_flush_bits, 0);
      }

      result = queue->device->ws->cs_finalize(cs);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (queue->initial_full_flush_preamble_cs)
      queue->device->ws->cs_destroy(queue->initial_full_flush_preamble_cs);

   if (queue->initial_preamble_cs)
      queue->device->ws->cs_destroy(queue->initial_preamble_cs);

   if (queue->continue_preamble_cs)
      queue->device->ws->cs_destroy(queue->continue_preamble_cs);

   queue->initial_full_flush_preamble_cs = dest_cs[0];
   queue->initial_preamble_cs = dest_cs[1];
   queue->continue_preamble_cs = dest_cs[2];

   if (scratch_bo != queue->scratch_bo) {
      if (queue->scratch_bo)
         queue->device->ws->buffer_destroy(queue->device->ws, queue->scratch_bo);
      queue->scratch_bo = scratch_bo;
   }
   queue->scratch_size_per_wave = scratch_size_per_wave;
   queue->scratch_waves = scratch_waves;

   if (compute_scratch_bo != queue->compute_scratch_bo) {
      if (queue->compute_scratch_bo)
         queue->device->ws->buffer_destroy(queue->device->ws, queue->compute_scratch_bo);
      queue->compute_scratch_bo = compute_scratch_bo;
   }
   queue->compute_scratch_size_per_wave = compute_scratch_size_per_wave;
   queue->compute_scratch_waves = compute_scratch_waves;

   if (esgs_ring_bo != queue->esgs_ring_bo) {
      if (queue->esgs_ring_bo)
         queue->device->ws->buffer_destroy(queue->device->ws, queue->esgs_ring_bo);
      queue->esgs_ring_bo = esgs_ring_bo;
      queue->esgs_ring_size = esgs_ring_size;
   }

   if (gsvs_ring_bo != queue->gsvs_ring_bo) {
      if (queue->gsvs_ring_bo)
         queue->device->ws->buffer_destroy(queue->device->ws, queue->gsvs_ring_bo);
      queue->gsvs_ring_bo = gsvs_ring_bo;
      queue->gsvs_ring_size = gsvs_ring_size;
   }

   if (tess_rings_bo != queue->tess_rings_bo) {
      queue->tess_rings_bo = tess_rings_bo;
      queue->has_tess_rings = true;
   }

   if (gds_bo != queue->gds_bo) {
      queue->gds_bo = gds_bo;
      queue->has_gds = true;
   }

   if (gds_oa_bo != queue->gds_oa_bo) {
      queue->gds_oa_bo = gds_oa_bo;
      queue->has_gds_oa = true;
   }

   if (descriptor_bo != queue->descriptor_bo) {
      if (queue->descriptor_bo)
         queue->device->ws->buffer_destroy(queue->device->ws, queue->descriptor_bo);

      queue->descriptor_bo = descriptor_bo;
   }

   if (add_sample_positions)
      queue->has_sample_positions = true;

   *initial_full_flush_preamble_cs = queue->initial_full_flush_preamble_cs;
   *initial_preamble_cs = queue->initial_preamble_cs;
   *continue_preamble_cs = queue->continue_preamble_cs;
   if (!scratch_size && !compute_scratch_size && !esgs_ring_size && !gsvs_ring_size)
      *continue_preamble_cs = NULL;
   return VK_SUCCESS;
fail:
   for (int i = 0; i < ARRAY_SIZE(dest_cs); ++i)
      if (dest_cs[i])
         queue->device->ws->cs_destroy(dest_cs[i]);
   if (descriptor_bo && descriptor_bo != queue->descriptor_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, descriptor_bo);
   if (scratch_bo && scratch_bo != queue->scratch_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, scratch_bo);
   if (compute_scratch_bo && compute_scratch_bo != queue->compute_scratch_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, compute_scratch_bo);
   if (esgs_ring_bo && esgs_ring_bo != queue->esgs_ring_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, esgs_ring_bo);
   if (gsvs_ring_bo && gsvs_ring_bo != queue->gsvs_ring_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, gsvs_ring_bo);
   if (tess_rings_bo && tess_rings_bo != queue->tess_rings_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, tess_rings_bo);
   if (gds_bo && gds_bo != queue->gds_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, gds_bo);
   if (gds_oa_bo && gds_oa_bo != queue->gds_oa_bo)
      queue->device->ws->buffer_destroy(queue->device->ws, gds_oa_bo);

   return vk_error(queue->device->instance, result);
}

static VkResult
radv_alloc_sem_counts(struct radv_device *device, struct radv_winsys_sem_counts *counts,
                      int num_sems, struct radv_semaphore_part **sems,
                      const uint64_t *timeline_values, VkFence _fence, bool is_signal)
{
   int syncobj_idx = 0, non_reset_idx = 0, timeline_idx = 0;

   if (num_sems == 0 && _fence == VK_NULL_HANDLE)
      return VK_SUCCESS;

   for (uint32_t i = 0; i < num_sems; i++) {
      switch (sems[i]->kind) {
      case RADV_SEMAPHORE_SYNCOBJ:
         counts->syncobj_count++;
         counts->syncobj_reset_count++;
         break;
      case RADV_SEMAPHORE_NONE:
         break;
      case RADV_SEMAPHORE_TIMELINE:
         counts->syncobj_count++;
         break;
      case RADV_SEMAPHORE_TIMELINE_SYNCOBJ:
         counts->timeline_syncobj_count++;
         break;
      }
   }

   if (_fence != VK_NULL_HANDLE)
      counts->syncobj_count++;

   if (counts->syncobj_count || counts->timeline_syncobj_count) {
      counts->points = (uint64_t *)malloc(sizeof(*counts->syncobj) * counts->syncobj_count +
                                          (sizeof(*counts->syncobj) + sizeof(*counts->points)) *
                                             counts->timeline_syncobj_count);
      if (!counts->points)
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      counts->syncobj = (uint32_t *)(counts->points + counts->timeline_syncobj_count);
   }

   non_reset_idx = counts->syncobj_reset_count;

   for (uint32_t i = 0; i < num_sems; i++) {
      switch (sems[i]->kind) {
      case RADV_SEMAPHORE_NONE:
         unreachable("Empty semaphore");
         break;
      case RADV_SEMAPHORE_SYNCOBJ:
         counts->syncobj[syncobj_idx++] = sems[i]->syncobj;
         break;
      case RADV_SEMAPHORE_TIMELINE: {
         mtx_lock(&sems[i]->timeline.mutex);
         struct radv_timeline_point *point = NULL;
         if (is_signal) {
            point = radv_timeline_add_point_locked(device, &sems[i]->timeline, timeline_values[i]);
         } else {
            point = radv_timeline_find_point_at_least_locked(device, &sems[i]->timeline,
                                                             timeline_values[i]);
         }

         mtx_unlock(&sems[i]->timeline.mutex);

         if (point) {
            counts->syncobj[non_reset_idx++] = point->syncobj;
         } else {
            /* Explicitly remove the semaphore so we might not find
             * a point later post-submit. */
            sems[i] = NULL;
         }
         break;
      }
      case RADV_SEMAPHORE_TIMELINE_SYNCOBJ:
         counts->syncobj[counts->syncobj_count + timeline_idx] = sems[i]->syncobj;
         counts->points[timeline_idx] = timeline_values[i];
         ++timeline_idx;
         break;
      }
   }

   if (_fence != VK_NULL_HANDLE) {
      RADV_FROM_HANDLE(radv_fence, fence, _fence);

      struct radv_fence_part *part =
         fence->temporary.kind != RADV_FENCE_NONE ? &fence->temporary : &fence->permanent;
      counts->syncobj[non_reset_idx++] = part->syncobj;
   }

   assert(MAX2(syncobj_idx, non_reset_idx) <= counts->syncobj_count);
   counts->syncobj_count = MAX2(syncobj_idx, non_reset_idx);

   return VK_SUCCESS;
}

static void
radv_free_sem_info(struct radv_winsys_sem_info *sem_info)
{
   free(sem_info->wait.points);
   free(sem_info->signal.points);
}

static void
radv_free_temp_syncobjs(struct radv_device *device, int num_sems, struct radv_semaphore_part *sems)
{
   for (uint32_t i = 0; i < num_sems; i++) {
      radv_destroy_semaphore_part(device, sems + i);
   }
}

static VkResult
radv_alloc_sem_info(struct radv_device *device, struct radv_winsys_sem_info *sem_info,
                    int num_wait_sems, struct radv_semaphore_part **wait_sems,
                    const uint64_t *wait_values, int num_signal_sems,
                    struct radv_semaphore_part **signal_sems, const uint64_t *signal_values,
                    VkFence fence)
{
   VkResult ret;

   ret = radv_alloc_sem_counts(device, &sem_info->wait, num_wait_sems, wait_sems, wait_values,
                               VK_NULL_HANDLE, false);
   if (ret)
      return ret;
   ret = radv_alloc_sem_counts(device, &sem_info->signal, num_signal_sems, signal_sems,
                               signal_values, fence, true);
   if (ret)
      radv_free_sem_info(sem_info);

   /* caller can override these */
   sem_info->cs_emit_wait = true;
   sem_info->cs_emit_signal = true;
   return ret;
}

static void
radv_finalize_timelines(struct radv_device *device, uint32_t num_wait_sems,
                        struct radv_semaphore_part **wait_sems, const uint64_t *wait_values,
                        uint32_t num_signal_sems, struct radv_semaphore_part **signal_sems,
                        const uint64_t *signal_values, struct list_head *processing_list)
{
   for (uint32_t i = 0; i < num_wait_sems; ++i) {
      if (wait_sems[i] && wait_sems[i]->kind == RADV_SEMAPHORE_TIMELINE) {
         mtx_lock(&wait_sems[i]->timeline.mutex);
         struct radv_timeline_point *point = radv_timeline_find_point_at_least_locked(
            device, &wait_sems[i]->timeline, wait_values[i]);
         point->wait_count -= 2;
         mtx_unlock(&wait_sems[i]->timeline.mutex);
      }
   }
   for (uint32_t i = 0; i < num_signal_sems; ++i) {
      if (signal_sems[i] && signal_sems[i]->kind == RADV_SEMAPHORE_TIMELINE) {
         mtx_lock(&signal_sems[i]->timeline.mutex);
         struct radv_timeline_point *point = radv_timeline_find_point_at_least_locked(
            device, &signal_sems[i]->timeline, signal_values[i]);
         signal_sems[i]->timeline.highest_submitted =
            MAX2(signal_sems[i]->timeline.highest_submitted, point->value);
         point->wait_count -= 2;
         radv_timeline_trigger_waiters_locked(&signal_sems[i]->timeline, processing_list);
         mtx_unlock(&signal_sems[i]->timeline.mutex);
      } else if (signal_sems[i] && signal_sems[i]->kind == RADV_SEMAPHORE_TIMELINE_SYNCOBJ) {
         signal_sems[i]->timeline_syncobj.max_point =
            MAX2(signal_sems[i]->timeline_syncobj.max_point, signal_values[i]);
      }
   }
}

static VkResult
radv_sparse_buffer_bind_memory(struct radv_device *device, const VkSparseBufferMemoryBindInfo *bind)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, bind->buffer);
   VkResult result;

   for (uint32_t i = 0; i < bind->bindCount; ++i) {
      struct radv_device_memory *mem = NULL;

      if (bind->pBinds[i].memory != VK_NULL_HANDLE)
         mem = radv_device_memory_from_handle(bind->pBinds[i].memory);

      result = device->ws->buffer_virtual_bind(device->ws, buffer->bo,
                                               bind->pBinds[i].resourceOffset, bind->pBinds[i].size,
                                               mem ? mem->bo : NULL, bind->pBinds[i].memoryOffset);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
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

      result = device->ws->buffer_virtual_bind(device->ws, image->bo,
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
   uint32_t bs = vk_format_get_blocksize(image->vk_format);
   VkResult result;

   for (uint32_t i = 0; i < bind->bindCount; ++i) {
      struct radv_device_memory *mem = NULL;
      uint32_t offset, pitch;
      uint32_t mem_offset = bind->pBinds[i].memoryOffset;
      const uint32_t layer = bind->pBinds[i].subresource.arrayLayer;
      const uint32_t level = bind->pBinds[i].subresource.mipLevel;

      VkExtent3D bind_extent = bind->pBinds[i].extent;
      bind_extent.width =
         DIV_ROUND_UP(bind_extent.width, vk_format_get_blockwidth(image->vk_format));
      bind_extent.height =
         DIV_ROUND_UP(bind_extent.height, vk_format_get_blockheight(image->vk_format));

      VkOffset3D bind_offset = bind->pBinds[i].offset;
      bind_offset.x /= vk_format_get_blockwidth(image->vk_format);
      bind_offset.y /= vk_format_get_blockheight(image->vk_format);

      if (bind->pBinds[i].memory != VK_NULL_HANDLE)
         mem = radv_device_memory_from_handle(bind->pBinds[i].memory);

      if (device->physical_device->rad_info.chip_class >= GFX9) {
         offset = surface->u.gfx9.surf_slice_size * layer + surface->u.gfx9.prt_level_offset[level];
         pitch = surface->u.gfx9.prt_level_pitch[level];
      } else {
         offset = (uint64_t)surface->u.legacy.level[level].offset_256B * 256 +
                  surface->u.legacy.level[level].slice_size_dw * 4 * layer;
         pitch = surface->u.legacy.level[level].nblk_x;
      }

      offset += (bind_offset.y * pitch * bs) + (bind_offset.x * surface->prt_tile_height * bs);

      uint32_t aligned_extent_width = ALIGN(bind_extent.width, surface->prt_tile_width);

      bool whole_subres = bind_offset.x == 0 && aligned_extent_width == pitch;

      if (whole_subres) {
         uint32_t aligned_extent_height = ALIGN(bind_extent.height, surface->prt_tile_height);

         uint32_t size = aligned_extent_width * aligned_extent_height * bs;
         result = device->ws->buffer_virtual_bind(device->ws, image->bo, offset, size,
                                                  mem ? mem->bo : NULL, mem_offset);
         if (result != VK_SUCCESS)
            return result;
      } else {
         uint32_t img_increment = pitch * bs;
         uint32_t mem_increment = aligned_extent_width * bs;
         uint32_t size = mem_increment * surface->prt_tile_height;
         for (unsigned y = 0; y < bind_extent.height; y += surface->prt_tile_height) {
            result = device->ws->buffer_virtual_bind(
               device->ws, image->bo, offset + img_increment * y, size, mem ? mem->bo : NULL,
               mem_offset + mem_increment * y);
            if (result != VK_SUCCESS)
               return result;
         }
      }
   }

   return VK_SUCCESS;
}

static VkResult
radv_get_preambles(struct radv_queue *queue, const VkCommandBuffer *cmd_buffers,
                   uint32_t cmd_buffer_count, struct radeon_cmdbuf **initial_full_flush_preamble_cs,
                   struct radeon_cmdbuf **initial_preamble_cs,
                   struct radeon_cmdbuf **continue_preamble_cs)
{
   uint32_t scratch_size_per_wave = 0, waves_wanted = 0;
   uint32_t compute_scratch_size_per_wave = 0, compute_waves_wanted = 0;
   uint32_t esgs_ring_size = 0, gsvs_ring_size = 0;
   bool tess_rings_needed = false;
   bool gds_needed = false;
   bool gds_oa_needed = false;
   bool sample_positions_needed = false;

   for (uint32_t j = 0; j < cmd_buffer_count; j++) {
      RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, cmd_buffers[j]);

      scratch_size_per_wave = MAX2(scratch_size_per_wave, cmd_buffer->scratch_size_per_wave_needed);
      waves_wanted = MAX2(waves_wanted, cmd_buffer->scratch_waves_wanted);
      compute_scratch_size_per_wave =
         MAX2(compute_scratch_size_per_wave, cmd_buffer->compute_scratch_size_per_wave_needed);
      compute_waves_wanted = MAX2(compute_waves_wanted, cmd_buffer->compute_scratch_waves_wanted);
      esgs_ring_size = MAX2(esgs_ring_size, cmd_buffer->esgs_ring_size_needed);
      gsvs_ring_size = MAX2(gsvs_ring_size, cmd_buffer->gsvs_ring_size_needed);
      tess_rings_needed |= cmd_buffer->tess_rings_needed;
      gds_needed |= cmd_buffer->gds_needed;
      gds_oa_needed |= cmd_buffer->gds_oa_needed;
      sample_positions_needed |= cmd_buffer->sample_positions_needed;
   }

   return radv_get_preamble_cs(queue, scratch_size_per_wave, waves_wanted,
                               compute_scratch_size_per_wave, compute_waves_wanted, esgs_ring_size,
                               gsvs_ring_size, tess_rings_needed, gds_needed, gds_oa_needed,
                               sample_positions_needed, initial_full_flush_preamble_cs,
                               initial_preamble_cs, continue_preamble_cs);
}

struct radv_deferred_queue_submission {
   struct radv_queue *queue;
   VkCommandBuffer *cmd_buffers;
   uint32_t cmd_buffer_count;

   /* Sparse bindings that happen on a queue. */
   VkSparseBufferMemoryBindInfo *buffer_binds;
   uint32_t buffer_bind_count;
   VkSparseImageOpaqueMemoryBindInfo *image_opaque_binds;
   uint32_t image_opaque_bind_count;
   VkSparseImageMemoryBindInfo *image_binds;
   uint32_t image_bind_count;

   bool flush_caches;
   VkShaderStageFlags wait_dst_stage_mask;
   struct radv_semaphore_part **wait_semaphores;
   uint32_t wait_semaphore_count;
   struct radv_semaphore_part **signal_semaphores;
   uint32_t signal_semaphore_count;
   VkFence fence;

   uint64_t *wait_values;
   uint64_t *signal_values;

   struct radv_semaphore_part *temporary_semaphore_parts;
   uint32_t temporary_semaphore_part_count;

   struct list_head queue_pending_list;
   uint32_t submission_wait_count;
   struct radv_timeline_waiter *wait_nodes;

   struct list_head processing_list;
};

struct radv_queue_submission {
   const VkCommandBuffer *cmd_buffers;
   uint32_t cmd_buffer_count;

   /* Sparse bindings that happen on a queue. */
   const VkSparseBufferMemoryBindInfo *buffer_binds;
   uint32_t buffer_bind_count;
   const VkSparseImageOpaqueMemoryBindInfo *image_opaque_binds;
   uint32_t image_opaque_bind_count;
   const VkSparseImageMemoryBindInfo *image_binds;
   uint32_t image_bind_count;

   bool flush_caches;
   VkPipelineStageFlags wait_dst_stage_mask;
   const VkSemaphore *wait_semaphores;
   uint32_t wait_semaphore_count;
   const VkSemaphore *signal_semaphores;
   uint32_t signal_semaphore_count;
   VkFence fence;

   const uint64_t *wait_values;
   uint32_t wait_value_count;
   const uint64_t *signal_values;
   uint32_t signal_value_count;
};

static VkResult radv_queue_trigger_submission(struct radv_deferred_queue_submission *submission,
                                              uint32_t decrement,
                                              struct list_head *processing_list);

static VkResult
radv_create_deferred_submission(struct radv_queue *queue,
                                const struct radv_queue_submission *submission,
                                struct radv_deferred_queue_submission **out)
{
   struct radv_deferred_queue_submission *deferred = NULL;
   size_t size = sizeof(struct radv_deferred_queue_submission);

   uint32_t temporary_count = 0;
   for (uint32_t i = 0; i < submission->wait_semaphore_count; ++i) {
      RADV_FROM_HANDLE(radv_semaphore, semaphore, submission->wait_semaphores[i]);
      if (semaphore->temporary.kind != RADV_SEMAPHORE_NONE)
         ++temporary_count;
   }

   size += submission->cmd_buffer_count * sizeof(VkCommandBuffer);
   size += submission->buffer_bind_count * sizeof(VkSparseBufferMemoryBindInfo);
   size += submission->image_opaque_bind_count * sizeof(VkSparseImageOpaqueMemoryBindInfo);
   size += submission->image_bind_count * sizeof(VkSparseImageMemoryBindInfo);

   for (uint32_t i = 0; i < submission->image_bind_count; ++i)
      size += submission->image_binds[i].bindCount * sizeof(VkSparseImageMemoryBind);

   size += submission->wait_semaphore_count * sizeof(struct radv_semaphore_part *);
   size += temporary_count * sizeof(struct radv_semaphore_part);
   size += submission->signal_semaphore_count * sizeof(struct radv_semaphore_part *);
   size += submission->wait_value_count * sizeof(uint64_t);
   size += submission->signal_value_count * sizeof(uint64_t);
   size += submission->wait_semaphore_count * sizeof(struct radv_timeline_waiter);

   deferred = calloc(1, size);
   if (!deferred)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   deferred->queue = queue;

   deferred->cmd_buffers = (void *)(deferred + 1);
   deferred->cmd_buffer_count = submission->cmd_buffer_count;
   if (submission->cmd_buffer_count) {
      memcpy(deferred->cmd_buffers, submission->cmd_buffers,
             submission->cmd_buffer_count * sizeof(*deferred->cmd_buffers));
   }

   deferred->buffer_binds = (void *)(deferred->cmd_buffers + submission->cmd_buffer_count);
   deferred->buffer_bind_count = submission->buffer_bind_count;
   if (submission->buffer_bind_count) {
      memcpy(deferred->buffer_binds, submission->buffer_binds,
             submission->buffer_bind_count * sizeof(*deferred->buffer_binds));
   }

   deferred->image_opaque_binds = (void *)(deferred->buffer_binds + submission->buffer_bind_count);
   deferred->image_opaque_bind_count = submission->image_opaque_bind_count;
   if (submission->image_opaque_bind_count) {
      memcpy(deferred->image_opaque_binds, submission->image_opaque_binds,
             submission->image_opaque_bind_count * sizeof(*deferred->image_opaque_binds));
   }

   deferred->image_binds =
      (void *)(deferred->image_opaque_binds + deferred->image_opaque_bind_count);
   deferred->image_bind_count = submission->image_bind_count;

   VkSparseImageMemoryBind *sparse_image_binds =
      (void *)(deferred->image_binds + deferred->image_bind_count);
   for (uint32_t i = 0; i < deferred->image_bind_count; ++i) {
      deferred->image_binds[i] = submission->image_binds[i];
      deferred->image_binds[i].pBinds = sparse_image_binds;

      for (uint32_t j = 0; j < deferred->image_binds[i].bindCount; ++j)
         *sparse_image_binds++ = submission->image_binds[i].pBinds[j];
   }

   deferred->flush_caches = submission->flush_caches;
   deferred->wait_dst_stage_mask = submission->wait_dst_stage_mask;

   deferred->wait_semaphores = (void *)sparse_image_binds;
   deferred->wait_semaphore_count = submission->wait_semaphore_count;

   deferred->signal_semaphores =
      (void *)(deferred->wait_semaphores + deferred->wait_semaphore_count);
   deferred->signal_semaphore_count = submission->signal_semaphore_count;

   deferred->fence = submission->fence;

   deferred->temporary_semaphore_parts =
      (void *)(deferred->signal_semaphores + deferred->signal_semaphore_count);
   deferred->temporary_semaphore_part_count = temporary_count;

   uint32_t temporary_idx = 0;
   for (uint32_t i = 0; i < submission->wait_semaphore_count; ++i) {
      RADV_FROM_HANDLE(radv_semaphore, semaphore, submission->wait_semaphores[i]);
      if (semaphore->temporary.kind != RADV_SEMAPHORE_NONE) {
         deferred->wait_semaphores[i] = &deferred->temporary_semaphore_parts[temporary_idx];
         deferred->temporary_semaphore_parts[temporary_idx] = semaphore->temporary;
         semaphore->temporary.kind = RADV_SEMAPHORE_NONE;
         ++temporary_idx;
      } else
         deferred->wait_semaphores[i] = &semaphore->permanent;
   }

   for (uint32_t i = 0; i < submission->signal_semaphore_count; ++i) {
      RADV_FROM_HANDLE(radv_semaphore, semaphore, submission->signal_semaphores[i]);
      if (semaphore->temporary.kind != RADV_SEMAPHORE_NONE) {
         deferred->signal_semaphores[i] = &semaphore->temporary;
      } else {
         deferred->signal_semaphores[i] = &semaphore->permanent;
      }
   }

   deferred->wait_values = (void *)(deferred->temporary_semaphore_parts + temporary_count);
   if (submission->wait_value_count) {
      memcpy(deferred->wait_values, submission->wait_values,
             submission->wait_value_count * sizeof(uint64_t));
   }
   deferred->signal_values = deferred->wait_values + submission->wait_value_count;
   if (submission->signal_value_count) {
      memcpy(deferred->signal_values, submission->signal_values,
             submission->signal_value_count * sizeof(uint64_t));
   }

   deferred->wait_nodes = (void *)(deferred->signal_values + submission->signal_value_count);
   /* This is worst-case. radv_queue_enqueue_submission will fill in further, but this
    * ensure the submission is not accidentally triggered early when adding wait timelines. */
   deferred->submission_wait_count = 1 + submission->wait_semaphore_count;

   *out = deferred;
   return VK_SUCCESS;
}

static VkResult
radv_queue_enqueue_submission(struct radv_deferred_queue_submission *submission,
                              struct list_head *processing_list)
{
   uint32_t wait_cnt = 0;
   struct radv_timeline_waiter *waiter = submission->wait_nodes;
   for (uint32_t i = 0; i < submission->wait_semaphore_count; ++i) {
      if (submission->wait_semaphores[i]->kind == RADV_SEMAPHORE_TIMELINE) {
         mtx_lock(&submission->wait_semaphores[i]->timeline.mutex);
         if (submission->wait_semaphores[i]->timeline.highest_submitted <
             submission->wait_values[i]) {
            ++wait_cnt;
            waiter->value = submission->wait_values[i];
            waiter->submission = submission;
            list_addtail(&waiter->list, &submission->wait_semaphores[i]->timeline.waiters);
            ++waiter;
         }
         mtx_unlock(&submission->wait_semaphores[i]->timeline.mutex);
      }
   }

   mtx_lock(&submission->queue->pending_mutex);

   bool is_first = list_is_empty(&submission->queue->pending_submissions);
   list_addtail(&submission->queue_pending_list, &submission->queue->pending_submissions);

   mtx_unlock(&submission->queue->pending_mutex);

   /* If there is already a submission in the queue, that will decrement the counter by 1 when
    * submitted, but if the queue was empty, we decrement ourselves as there is no previous
    * submission. */
   uint32_t decrement = submission->wait_semaphore_count - wait_cnt + (is_first ? 1 : 0);

   /* if decrement is zero, then we don't have a refcounted reference to the
    * submission anymore, so it is not safe to access the submission. */
   if (!decrement)
      return VK_SUCCESS;

   return radv_queue_trigger_submission(submission, decrement, processing_list);
}

static void
radv_queue_submission_update_queue(struct radv_deferred_queue_submission *submission,
                                   struct list_head *processing_list)
{
   mtx_lock(&submission->queue->pending_mutex);
   list_del(&submission->queue_pending_list);

   /* trigger the next submission in the queue. */
   if (!list_is_empty(&submission->queue->pending_submissions)) {
      struct radv_deferred_queue_submission *next_submission =
         list_first_entry(&submission->queue->pending_submissions,
                          struct radv_deferred_queue_submission, queue_pending_list);
      radv_queue_trigger_submission(next_submission, 1, processing_list);
   }
   mtx_unlock(&submission->queue->pending_mutex);

   u_cnd_monotonic_broadcast(&submission->queue->device->timeline_cond);
}

static VkResult
radv_queue_submit_deferred(struct radv_deferred_queue_submission *submission,
                           struct list_head *processing_list)
{
   struct radv_queue *queue = submission->queue;
   struct radeon_winsys_ctx *ctx = queue->hw_ctx;
   uint32_t max_cs_submission = queue->device->trace_bo ? 1 : RADV_MAX_IBS_PER_SUBMIT;
   bool do_flush = submission->flush_caches || submission->wait_dst_stage_mask;
   bool can_patch = true;
   uint32_t advance;
   struct radv_winsys_sem_info sem_info = {0};
   VkResult result;
   struct radeon_cmdbuf *initial_preamble_cs = NULL;
   struct radeon_cmdbuf *initial_flush_preamble_cs = NULL;
   struct radeon_cmdbuf *continue_preamble_cs = NULL;

   result =
      radv_get_preambles(queue, submission->cmd_buffers, submission->cmd_buffer_count,
                         &initial_preamble_cs, &initial_flush_preamble_cs, &continue_preamble_cs);
   if (result != VK_SUCCESS)
      goto fail;

   result = radv_alloc_sem_info(queue->device, &sem_info, submission->wait_semaphore_count,
                                submission->wait_semaphores, submission->wait_values,
                                submission->signal_semaphore_count, submission->signal_semaphores,
                                submission->signal_values, submission->fence);
   if (result != VK_SUCCESS)
      goto fail;

   for (uint32_t i = 0; i < submission->buffer_bind_count; ++i) {
      result = radv_sparse_buffer_bind_memory(queue->device, submission->buffer_binds + i);
      if (result != VK_SUCCESS)
         goto fail;
   }

   for (uint32_t i = 0; i < submission->image_opaque_bind_count; ++i) {
      result =
         radv_sparse_image_opaque_bind_memory(queue->device, submission->image_opaque_binds + i);
      if (result != VK_SUCCESS)
         goto fail;
   }

   for (uint32_t i = 0; i < submission->image_bind_count; ++i) {
      result = radv_sparse_image_bind_memory(queue->device, submission->image_binds + i);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (!submission->cmd_buffer_count) {
      result = queue->device->ws->cs_submit(ctx, queue->queue_idx,
                                            &queue->device->empty_cs[queue->queue_family_index], 1,
                                            NULL, NULL, &sem_info, false);
      if (result != VK_SUCCESS)
         goto fail;
   } else {
      struct radeon_cmdbuf **cs_array =
         malloc(sizeof(struct radeon_cmdbuf *) * (submission->cmd_buffer_count));

      for (uint32_t j = 0; j < submission->cmd_buffer_count; j++) {
         RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, submission->cmd_buffers[j]);
         assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

         cs_array[j] = cmd_buffer->cs;
         if ((cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))
            can_patch = false;

         cmd_buffer->status = RADV_CMD_BUFFER_STATUS_PENDING;
      }

      for (uint32_t j = 0; j < submission->cmd_buffer_count; j += advance) {
         struct radeon_cmdbuf *initial_preamble =
            (do_flush && !j) ? initial_flush_preamble_cs : initial_preamble_cs;
         advance = MIN2(max_cs_submission, submission->cmd_buffer_count - j);

         if (queue->device->trace_bo)
            *queue->device->trace_id_ptr = 0;

         sem_info.cs_emit_wait = j == 0;
         sem_info.cs_emit_signal = j + advance == submission->cmd_buffer_count;

         result = queue->device->ws->cs_submit(ctx, queue->queue_idx, cs_array + j, advance,
                                               initial_preamble, continue_preamble_cs, &sem_info,
                                               can_patch);
         if (result != VK_SUCCESS) {
            free(cs_array);
            goto fail;
         }

         if (queue->device->trace_bo) {
            radv_check_gpu_hangs(queue, cs_array[j]);
         }

         if (queue->device->tma_bo) {
            radv_check_trap_handler(queue);
         }
      }

      free(cs_array);
   }

   radv_finalize_timelines(queue->device, submission->wait_semaphore_count,
                           submission->wait_semaphores, submission->wait_values,
                           submission->signal_semaphore_count, submission->signal_semaphores,
                           submission->signal_values, processing_list);
   /* Has to happen after timeline finalization to make sure the
    * condition variable is only triggered when timelines and queue have
    * been updated. */
   radv_queue_submission_update_queue(submission, processing_list);

fail:
   if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) {
      /* When something bad happened during the submission, such as
       * an out of memory issue, it might be hard to recover from
       * this inconsistent state. To avoid this sort of problem, we
       * assume that we are in a really bad situation and return
       * VK_ERROR_DEVICE_LOST to ensure the clients do not attempt
       * to submit the same job again to this device.
       */
      result = radv_device_set_lost(queue->device, "vkQueueSubmit() failed");
   }

   radv_free_temp_syncobjs(queue->device, submission->temporary_semaphore_part_count,
                           submission->temporary_semaphore_parts);
   radv_free_sem_info(&sem_info);
   free(submission);
   return result;
}

static VkResult
radv_process_submissions(struct list_head *processing_list)
{
   while (!list_is_empty(processing_list)) {
      struct radv_deferred_queue_submission *submission =
         list_first_entry(processing_list, struct radv_deferred_queue_submission, processing_list);
      list_del(&submission->processing_list);

      VkResult result = radv_queue_submit_deferred(submission, processing_list);
      if (result != VK_SUCCESS)
         return result;
   }
   return VK_SUCCESS;
}

static VkResult
wait_for_submission_timelines_available(struct radv_deferred_queue_submission *submission,
                                        uint64_t timeout)
{
   struct radv_device *device = submission->queue->device;
   uint32_t syncobj_count = 0;
   uint32_t syncobj_idx = 0;

   for (uint32_t i = 0; i < submission->wait_semaphore_count; ++i) {
      if (submission->wait_semaphores[i]->kind != RADV_SEMAPHORE_TIMELINE_SYNCOBJ)
         continue;

      if (submission->wait_semaphores[i]->timeline_syncobj.max_point >= submission->wait_values[i])
         continue;
      ++syncobj_count;
   }

   if (!syncobj_count)
      return VK_SUCCESS;

   uint64_t *points = malloc((sizeof(uint64_t) + sizeof(uint32_t)) * syncobj_count);
   if (!points)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t *syncobj = (uint32_t *)(points + syncobj_count);

   for (uint32_t i = 0; i < submission->wait_semaphore_count; ++i) {
      if (submission->wait_semaphores[i]->kind != RADV_SEMAPHORE_TIMELINE_SYNCOBJ)
         continue;

      if (submission->wait_semaphores[i]->timeline_syncobj.max_point >= submission->wait_values[i])
         continue;

      syncobj[syncobj_idx] = submission->wait_semaphores[i]->syncobj;
      points[syncobj_idx] = submission->wait_values[i];
      ++syncobj_idx;
   }
   bool success = device->ws->wait_timeline_syncobj(device->ws, syncobj, points, syncobj_idx, true,
                                                    true, timeout);

   free(points);
   return success ? VK_SUCCESS : VK_TIMEOUT;
}

static int
radv_queue_submission_thread_run(void *q)
{
   struct radv_queue *queue = q;

   mtx_lock(&queue->thread_mutex);
   while (!p_atomic_read(&queue->thread_exit)) {
      struct radv_deferred_queue_submission *submission = queue->thread_submission;
      struct list_head processing_list;
      VkResult result = VK_SUCCESS;
      if (!submission) {
         u_cnd_monotonic_wait(&queue->thread_cond, &queue->thread_mutex);
         continue;
      }
      mtx_unlock(&queue->thread_mutex);

      /* Wait at most 5 seconds so we have a chance to notice shutdown when
       * a semaphore never gets signaled. If it takes longer we just retry
       * the wait next iteration. */
      result =
         wait_for_submission_timelines_available(submission, radv_get_absolute_timeout(5000000000));
      if (result != VK_SUCCESS) {
         mtx_lock(&queue->thread_mutex);
         continue;
      }

      /* The lock isn't held but nobody will add one until we finish
       * the current submission. */
      p_atomic_set(&queue->thread_submission, NULL);

      list_inithead(&processing_list);
      list_addtail(&submission->processing_list, &processing_list);
      result = radv_process_submissions(&processing_list);

      mtx_lock(&queue->thread_mutex);
   }
   mtx_unlock(&queue->thread_mutex);
   return 0;
}

static VkResult
radv_queue_trigger_submission(struct radv_deferred_queue_submission *submission, uint32_t decrement,
                              struct list_head *processing_list)
{
   struct radv_queue *queue = submission->queue;
   int ret;
   if (p_atomic_add_return(&submission->submission_wait_count, -decrement))
      return VK_SUCCESS;

   if (wait_for_submission_timelines_available(submission, radv_get_absolute_timeout(0)) ==
       VK_SUCCESS) {
      list_addtail(&submission->processing_list, processing_list);
      return VK_SUCCESS;
   }

   mtx_lock(&queue->thread_mutex);

   /* A submission can only be ready for the thread if it doesn't have
    * any predecessors in the same queue, so there can only be one such
    * submission at a time. */
   assert(queue->thread_submission == NULL);

   /* Only start the thread on demand to save resources for the many games
    * which only use binary semaphores. */
   if (!queue->thread_running) {
      ret = thrd_create(&queue->submission_thread, radv_queue_submission_thread_run, queue);
      if (ret) {
         mtx_unlock(&queue->thread_mutex);
         return vk_errorf(queue->device->instance, VK_ERROR_DEVICE_LOST,
                          "Failed to start submission thread");
      }
      queue->thread_running = true;
   }

   queue->thread_submission = submission;
   mtx_unlock(&queue->thread_mutex);

   u_cnd_monotonic_signal(&queue->thread_cond);
   return VK_SUCCESS;
}

static VkResult
radv_queue_submit(struct radv_queue *queue, const struct radv_queue_submission *submission)
{
   struct radv_deferred_queue_submission *deferred = NULL;

   VkResult result = radv_create_deferred_submission(queue, submission, &deferred);
   if (result != VK_SUCCESS)
      return result;

   struct list_head processing_list;
   list_inithead(&processing_list);

   result = radv_queue_enqueue_submission(deferred, &processing_list);
   if (result != VK_SUCCESS) {
      /* If anything is in the list we leak. */
      assert(list_is_empty(&processing_list));
      return result;
   }
   return radv_process_submissions(&processing_list);
}

bool
radv_queue_internal_submit(struct radv_queue *queue, struct radeon_cmdbuf *cs)
{
   struct radeon_winsys_ctx *ctx = queue->hw_ctx;
   struct radv_winsys_sem_info sem_info = {0};
   VkResult result;

   result = radv_alloc_sem_info(queue->device, &sem_info, 0, NULL, 0, 0, 0, NULL, VK_NULL_HANDLE);
   if (result != VK_SUCCESS)
      return false;

   result =
      queue->device->ws->cs_submit(ctx, queue->queue_idx, &cs, 1, NULL, NULL, &sem_info, false);
   radv_free_sem_info(&sem_info);
   if (result != VK_SUCCESS)
      return false;

   return true;
}

/* Signals fence as soon as all the work currently put on queue is done. */
static VkResult
radv_signal_fence(struct radv_queue *queue, VkFence fence)
{
   return radv_queue_submit(queue, &(struct radv_queue_submission){.fence = fence});
}

static bool
radv_submit_has_effects(const VkSubmitInfo *info)
{
   return info->commandBufferCount || info->waitSemaphoreCount || info->signalSemaphoreCount;
}

VkResult
radv_QueueSubmit(VkQueue _queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);
   VkResult result;
   uint32_t fence_idx = 0;
   bool flushed_caches = false;

   if (radv_device_is_lost(queue->device))
      return VK_ERROR_DEVICE_LOST;

   if (fence != VK_NULL_HANDLE) {
      for (uint32_t i = 0; i < submitCount; ++i)
         if (radv_submit_has_effects(pSubmits + i))
            fence_idx = i;
   } else
      fence_idx = UINT32_MAX;

   for (uint32_t i = 0; i < submitCount; i++) {
      if (!radv_submit_has_effects(pSubmits + i) && fence_idx != i)
         continue;

      VkPipelineStageFlags wait_dst_stage_mask = 0;
      for (unsigned j = 0; j < pSubmits[i].waitSemaphoreCount; ++j) {
         wait_dst_stage_mask |= pSubmits[i].pWaitDstStageMask[j];
      }

      const VkTimelineSemaphoreSubmitInfo *timeline_info =
         vk_find_struct_const(pSubmits[i].pNext, TIMELINE_SEMAPHORE_SUBMIT_INFO);

      result = radv_queue_submit(
         queue, &(struct radv_queue_submission){
                   .cmd_buffers = pSubmits[i].pCommandBuffers,
                   .cmd_buffer_count = pSubmits[i].commandBufferCount,
                   .wait_dst_stage_mask = wait_dst_stage_mask,
                   .flush_caches = !flushed_caches,
                   .wait_semaphores = pSubmits[i].pWaitSemaphores,
                   .wait_semaphore_count = pSubmits[i].waitSemaphoreCount,
                   .signal_semaphores = pSubmits[i].pSignalSemaphores,
                   .signal_semaphore_count = pSubmits[i].signalSemaphoreCount,
                   .fence = i == fence_idx ? fence : VK_NULL_HANDLE,
                   .wait_values = timeline_info ? timeline_info->pWaitSemaphoreValues : NULL,
                   .wait_value_count = timeline_info && timeline_info->pWaitSemaphoreValues
                                          ? timeline_info->waitSemaphoreValueCount
                                          : 0,
                   .signal_values = timeline_info ? timeline_info->pSignalSemaphoreValues : NULL,
                   .signal_value_count = timeline_info && timeline_info->pSignalSemaphoreValues
                                            ? timeline_info->signalSemaphoreValueCount
                                            : 0,
                });
      if (result != VK_SUCCESS)
         return result;

      flushed_caches = true;
   }

   if (fence != VK_NULL_HANDLE && !submitCount) {
      result = radv_signal_fence(queue, fence);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static const char *
radv_get_queue_family_name(struct radv_queue *queue)
{
   switch (queue->queue_family_index) {
   case RADV_QUEUE_GENERAL:
      return "graphics";
   case RADV_QUEUE_COMPUTE:
      return "compute";
   case RADV_QUEUE_TRANSFER:
      return "transfer";
   default:
      unreachable("Unknown queue family");
   }
}

VkResult
radv_QueueWaitIdle(VkQueue _queue)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);

   if (radv_device_is_lost(queue->device))
      return VK_ERROR_DEVICE_LOST;

   mtx_lock(&queue->pending_mutex);
   while (!list_is_empty(&queue->pending_submissions)) {
      u_cnd_monotonic_wait(&queue->device->timeline_cond, &queue->pending_mutex);
   }
   mtx_unlock(&queue->pending_mutex);

   if (!queue->device->ws->ctx_wait_idle(
          queue->hw_ctx, radv_queue_family_to_ring(queue->queue_family_index), queue->queue_idx)) {
      return radv_device_set_lost(queue->device,
                                  "Failed to wait for a '%s' queue "
                                  "to be idle. GPU hang ?",
                                  radv_get_queue_family_name(queue));
   }

   return VK_SUCCESS;
}

VkResult
radv_DeviceWaitIdle(VkDevice _device)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++) {
         VkResult result = radv_QueueWaitIdle(radv_queue_to_handle(&device->queues[i][q]));

         if (result != VK_SUCCESS)
            return result;
      }
   }
   return VK_SUCCESS;
}

VkResult
radv_EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(&radv_instance_extensions_supported,
                                                     pPropertyCount, pProperties);
}

PFN_vkVoidFunction
radv_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   RADV_FROM_HANDLE(radv_instance, instance, _instance);

   /* The Vulkan 1.0 spec for vkGetInstanceProcAddr has a table of exactly
    * when we have to return valid function pointers, NULL, or it's left
    * undefined.  See the table for exact details.
    */
   if (pName == NULL)
      return NULL;

#define LOOKUP_RADV_ENTRYPOINT(entrypoint)                                                         \
   if (strcmp(pName, "vk" #entrypoint) == 0)                                                       \
   return (PFN_vkVoidFunction)radv_##entrypoint

   LOOKUP_RADV_ENTRYPOINT(EnumerateInstanceExtensionProperties);
   LOOKUP_RADV_ENTRYPOINT(EnumerateInstanceLayerProperties);
   LOOKUP_RADV_ENTRYPOINT(EnumerateInstanceVersion);
   LOOKUP_RADV_ENTRYPOINT(CreateInstance);

   /* GetInstanceProcAddr() can also be called with a NULL instance.
    * See https://gitlab.khronos.org/vulkan/vulkan/issues/2057
    */
   LOOKUP_RADV_ENTRYPOINT(GetInstanceProcAddr);

#undef LOOKUP_RADV_ENTRYPOINT

   if (instance == NULL)
      return NULL;

   return vk_instance_get_proc_addr(&instance->vk, &radv_instance_entrypoints, pName);
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
   if (memory->image && memory->image->offset == 0) {
      struct radeon_bo_metadata metadata;
      radv_init_metadata(device, memory->image, &metadata);
      device->ws->buffer_set_metadata(device->ws, memory->bo, &metadata);
   }

   return device->ws->buffer_get_fd(device->ws, memory->bo, pFD);
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

   vk_object_base_finish(&mem->base);
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
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*mem), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY);

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
   mem->bo = NULL;

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
          !vk_format_is_depth_or_stencil(mem->image->vk_format) && mem->image->info.samples == 1 &&
          mem->image->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
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

VkResult
radv_AllocateMemory(VkDevice _device, const VkMemoryAllocateInfo *pAllocateInfo,
                    const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMem)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   return radv_alloc_memory(device, pAllocateInfo, pAllocator, pMem);
}

void
radv_FreeMemory(VkDevice _device, VkDeviceMemory _mem, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, mem, _mem);

   radv_free_memory(device, pAllocator, mem);
}

VkResult
radv_MapMemory(VkDevice _device, VkDeviceMemory _memory, VkDeviceSize offset, VkDeviceSize size,
               VkMemoryMapFlags flags, void **ppData)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, mem, _memory);

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   if (mem->user_ptr)
      *ppData = mem->user_ptr;
   else
      *ppData = device->ws->buffer_map(mem->bo);

   if (*ppData) {
      *ppData = (uint8_t *)*ppData + offset;
      return VK_SUCCESS;
   }

   return vk_error(device->instance, VK_ERROR_MEMORY_MAP_FAILED);
}

void
radv_UnmapMemory(VkDevice _device, VkDeviceMemory _memory)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, mem, _memory);

   if (mem == NULL)
      return;

   if (mem->user_ptr == NULL)
      device->ws->buffer_unmap(mem->bo);
}

VkResult
radv_FlushMappedMemoryRanges(VkDevice _device, uint32_t memoryRangeCount,
                             const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VkResult
radv_InvalidateMappedMemoryRanges(VkDevice _device, uint32_t memoryRangeCount,
                                  const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

void
radv_GetBufferMemoryRequirements2(VkDevice _device, const VkBufferMemoryRequirementsInfo2 *pInfo,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);

   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      (1u << device->physical_device->memory_properties.memoryTypeCount) - 1;

   if (buffer->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
      pMemoryRequirements->memoryRequirements.alignment = 4096;
   else
      pMemoryRequirements->memoryRequirements.alignment = 16;

   pMemoryRequirements->memoryRequirements.size =
      align64(buffer->size, pMemoryRequirements->memoryRequirements.alignment);

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

void
radv_GetImageMemoryRequirements2(VkDevice _device, const VkImageMemoryRequirementsInfo2 *pInfo,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_image, image, pInfo->image);

   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      (1u << device->physical_device->memory_properties.memoryTypeCount) - 1;

   pMemoryRequirements->memoryRequirements.size = image->size;
   pMemoryRequirements->memoryRequirements.alignment = image->alignment;

   vk_foreach_struct(ext, pMemoryRequirements->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req = (VkMemoryDedicatedRequirements *)ext;
         req->requiresDedicatedAllocation =
            image->shareable && image->tiling != VK_IMAGE_TILING_LINEAR;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

void
radv_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                               VkDeviceSize *pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

VkResult
radv_BindBufferMemory2(VkDevice _device, uint32_t bindInfoCount,
                       const VkBindBufferMemoryInfo *pBindInfos)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      RADV_FROM_HANDLE(radv_device_memory, mem, pBindInfos[i].memory);
      RADV_FROM_HANDLE(radv_buffer, buffer, pBindInfos[i].buffer);

      if (mem) {
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
               return vk_errorf(device->instance, VK_ERROR_UNKNOWN,
                                "Device memory object too small for the buffer.\n");
            }
         }

         buffer->bo = mem->bo;
         buffer->offset = pBindInfos[i].memoryOffset;
      } else {
         buffer->bo = NULL;
      }
   }
   return VK_SUCCESS;
}

VkResult
radv_BindImageMemory2(VkDevice _device, uint32_t bindInfoCount,
                      const VkBindImageMemoryInfo *pBindInfos)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      RADV_FROM_HANDLE(radv_device_memory, mem, pBindInfos[i].memory);
      RADV_FROM_HANDLE(radv_image, image, pBindInfos[i].image);

      if (mem) {
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
               return vk_errorf(device->instance, VK_ERROR_UNKNOWN,
                                "Device memory object too small for the image.\n");
            }
         }

         image->bo = mem->bo;
         image->offset = pBindInfos[i].memoryOffset;
      } else {
         image->bo = NULL;
         image->offset = 0;
      }
   }
   return VK_SUCCESS;
}

static bool
radv_sparse_bind_has_effects(const VkBindSparseInfo *info)
{
   return info->bufferBindCount || info->imageOpaqueBindCount || info->imageBindCount ||
          info->waitSemaphoreCount || info->signalSemaphoreCount;
}

VkResult
radv_QueueBindSparse(VkQueue _queue, uint32_t bindInfoCount, const VkBindSparseInfo *pBindInfo,
                     VkFence fence)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);
   uint32_t fence_idx = 0;

   if (radv_device_is_lost(queue->device))
      return VK_ERROR_DEVICE_LOST;

   if (fence != VK_NULL_HANDLE) {
      for (uint32_t i = 0; i < bindInfoCount; ++i)
         if (radv_sparse_bind_has_effects(pBindInfo + i))
            fence_idx = i;
   } else
      fence_idx = UINT32_MAX;

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      if (i != fence_idx && !radv_sparse_bind_has_effects(pBindInfo + i))
         continue;

      const VkTimelineSemaphoreSubmitInfo *timeline_info =
         vk_find_struct_const(pBindInfo[i].pNext, TIMELINE_SEMAPHORE_SUBMIT_INFO);

      VkResult result = radv_queue_submit(
         queue, &(struct radv_queue_submission){
                   .buffer_binds = pBindInfo[i].pBufferBinds,
                   .buffer_bind_count = pBindInfo[i].bufferBindCount,
                   .image_opaque_binds = pBindInfo[i].pImageOpaqueBinds,
                   .image_opaque_bind_count = pBindInfo[i].imageOpaqueBindCount,
                   .image_binds = pBindInfo[i].pImageBinds,
                   .image_bind_count = pBindInfo[i].imageBindCount,
                   .wait_semaphores = pBindInfo[i].pWaitSemaphores,
                   .wait_semaphore_count = pBindInfo[i].waitSemaphoreCount,
                   .signal_semaphores = pBindInfo[i].pSignalSemaphores,
                   .signal_semaphore_count = pBindInfo[i].signalSemaphoreCount,
                   .fence = i == fence_idx ? fence : VK_NULL_HANDLE,
                   .wait_values = timeline_info ? timeline_info->pWaitSemaphoreValues : NULL,
                   .wait_value_count = timeline_info && timeline_info->pWaitSemaphoreValues
                                          ? timeline_info->waitSemaphoreValueCount
                                          : 0,
                   .signal_values = timeline_info ? timeline_info->pSignalSemaphoreValues : NULL,
                   .signal_value_count = timeline_info && timeline_info->pSignalSemaphoreValues
                                            ? timeline_info->signalSemaphoreValueCount
                                            : 0,
                });

      if (result != VK_SUCCESS)
         return result;
   }

   if (fence != VK_NULL_HANDLE && !bindInfoCount) {
      VkResult result = radv_signal_fence(queue, fence);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static void
radv_destroy_fence_part(struct radv_device *device, struct radv_fence_part *part)
{
   if (part->kind != RADV_FENCE_NONE)
      device->ws->destroy_syncobj(device->ws, part->syncobj);
   part->kind = RADV_FENCE_NONE;
}

static void
radv_destroy_fence(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                   struct radv_fence *fence)
{
   radv_destroy_fence_part(device, &fence->temporary);
   radv_destroy_fence_part(device, &fence->permanent);

   vk_object_base_finish(&fence->base);
   vk_free2(&device->vk.alloc, pAllocator, fence);
}

VkResult
radv_CreateFence(VkDevice _device, const VkFenceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator, VkFence *pFence)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   bool create_signaled = false;
   struct radv_fence *fence;
   int ret;

   fence = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*fence), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!fence)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &fence->base, VK_OBJECT_TYPE_FENCE);

   fence->permanent.kind = RADV_FENCE_SYNCOBJ;

   if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT)
      create_signaled = true;

   ret = device->ws->create_syncobj(device->ws, create_signaled, &fence->permanent.syncobj);
   if (ret) {
      radv_destroy_fence(device, pAllocator, fence);
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   *pFence = radv_fence_to_handle(fence);

   return VK_SUCCESS;
}

void
radv_DestroyFence(VkDevice _device, VkFence _fence, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_fence, fence, _fence);

   if (!fence)
      return;

   radv_destroy_fence(device, pAllocator, fence);
}

VkResult
radv_WaitForFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll,
                   uint64_t timeout)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   uint32_t *handles;

   if (radv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   timeout = radv_get_absolute_timeout(timeout);

   handles = malloc(sizeof(uint32_t) * fenceCount);
   if (!handles)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < fenceCount; ++i) {
      RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);

      struct radv_fence_part *part =
         fence->temporary.kind != RADV_FENCE_NONE ? &fence->temporary : &fence->permanent;

      assert(part->kind == RADV_FENCE_SYNCOBJ);
      handles[i] = part->syncobj;
   }

   bool success = device->ws->wait_syncobj(device->ws, handles, fenceCount, waitAll, timeout);
   free(handles);
   return success ? VK_SUCCESS : VK_TIMEOUT;
}

VkResult
radv_ResetFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   for (unsigned i = 0; i < fenceCount; ++i) {
      RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);

      /* From the Vulkan 1.0.53 spec:
       *
       *    "If any member of pFences currently has its payload
       *    imported with temporary permanence, that fence’s prior
       *    permanent payload is irst restored. The remaining
       *    operations described therefore operate on the restored
       *    payload."
       */
      if (fence->temporary.kind != RADV_FENCE_NONE)
         radv_destroy_fence_part(device, &fence->temporary);

      device->ws->reset_syncobj(device->ws, fence->permanent.syncobj);
   }

   return VK_SUCCESS;
}

VkResult
radv_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_fence, fence, _fence);

   struct radv_fence_part *part =
      fence->temporary.kind != RADV_FENCE_NONE ? &fence->temporary : &fence->permanent;

   if (radv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   bool success = device->ws->wait_syncobj(device->ws, &part->syncobj, 1, true, 0);
   return success ? VK_SUCCESS : VK_NOT_READY;
}

// Queue semaphore functions

static void
radv_create_timeline(struct radv_timeline *timeline, uint64_t value)
{
   timeline->highest_signaled = value;
   timeline->highest_submitted = value;
   list_inithead(&timeline->points);
   list_inithead(&timeline->free_points);
   list_inithead(&timeline->waiters);
   mtx_init(&timeline->mutex, mtx_plain);
}

static void
radv_destroy_timeline(struct radv_device *device, struct radv_timeline *timeline)
{
   list_for_each_entry_safe(struct radv_timeline_point, point, &timeline->free_points, list)
   {
      list_del(&point->list);
      device->ws->destroy_syncobj(device->ws, point->syncobj);
      free(point);
   }
   list_for_each_entry_safe(struct radv_timeline_point, point, &timeline->points, list)
   {
      list_del(&point->list);
      device->ws->destroy_syncobj(device->ws, point->syncobj);
      free(point);
   }
   mtx_destroy(&timeline->mutex);
}

static void
radv_timeline_gc_locked(struct radv_device *device, struct radv_timeline *timeline)
{
   list_for_each_entry_safe(struct radv_timeline_point, point, &timeline->points, list)
   {
      if (point->wait_count || point->value > timeline->highest_submitted)
         return;

      if (device->ws->wait_syncobj(device->ws, &point->syncobj, 1, true, 0)) {
         timeline->highest_signaled = point->value;
         list_del(&point->list);
         list_add(&point->list, &timeline->free_points);
      }
   }
}

static struct radv_timeline_point *
radv_timeline_find_point_at_least_locked(struct radv_device *device, struct radv_timeline *timeline,
                                         uint64_t p)
{
   radv_timeline_gc_locked(device, timeline);

   if (p <= timeline->highest_signaled)
      return NULL;

   list_for_each_entry(struct radv_timeline_point, point, &timeline->points, list)
   {
      if (point->value >= p) {
         ++point->wait_count;
         return point;
      }
   }
   return NULL;
}

static struct radv_timeline_point *
radv_timeline_add_point_locked(struct radv_device *device, struct radv_timeline *timeline,
                               uint64_t p)
{
   radv_timeline_gc_locked(device, timeline);

   struct radv_timeline_point *ret = NULL;
   struct radv_timeline_point *prev = NULL;
   int r;

   if (p <= timeline->highest_signaled)
      return NULL;

   list_for_each_entry(struct radv_timeline_point, point, &timeline->points, list)
   {
      if (point->value == p) {
         return NULL;
      }

      if (point->value < p)
         prev = point;
   }

   if (list_is_empty(&timeline->free_points)) {
      ret = malloc(sizeof(struct radv_timeline_point));
      r = device->ws->create_syncobj(device->ws, false, &ret->syncobj);
      if (r) {
         free(ret);
         return NULL;
      }
   } else {
      ret = list_first_entry(&timeline->free_points, struct radv_timeline_point, list);
      list_del(&ret->list);

      device->ws->reset_syncobj(device->ws, ret->syncobj);
   }

   ret->value = p;
   ret->wait_count = 1;

   if (prev) {
      list_add(&ret->list, &prev->list);
   } else {
      list_addtail(&ret->list, &timeline->points);
   }
   return ret;
}

static VkResult
radv_timeline_wait(struct radv_device *device, struct radv_timeline *timeline, uint64_t value,
                   uint64_t abs_timeout)
{
   mtx_lock(&timeline->mutex);

   while (timeline->highest_submitted < value) {
      struct timespec abstime;
      timespec_from_nsec(&abstime, abs_timeout);

      u_cnd_monotonic_timedwait(&device->timeline_cond, &timeline->mutex, &abstime);

      if (radv_get_current_time() >= abs_timeout && timeline->highest_submitted < value) {
         mtx_unlock(&timeline->mutex);
         return VK_TIMEOUT;
      }
   }

   struct radv_timeline_point *point =
      radv_timeline_find_point_at_least_locked(device, timeline, value);
   mtx_unlock(&timeline->mutex);
   if (!point)
      return VK_SUCCESS;

   bool success = device->ws->wait_syncobj(device->ws, &point->syncobj, 1, true, abs_timeout);

   mtx_lock(&timeline->mutex);
   point->wait_count--;
   mtx_unlock(&timeline->mutex);
   return success ? VK_SUCCESS : VK_TIMEOUT;
}

static void
radv_timeline_trigger_waiters_locked(struct radv_timeline *timeline,
                                     struct list_head *processing_list)
{
   list_for_each_entry_safe(struct radv_timeline_waiter, waiter, &timeline->waiters, list)
   {
      if (waiter->value > timeline->highest_submitted)
         continue;

      radv_queue_trigger_submission(waiter->submission, 1, processing_list);
      list_del(&waiter->list);
   }
}

static void
radv_destroy_semaphore_part(struct radv_device *device, struct radv_semaphore_part *part)
{
   switch (part->kind) {
   case RADV_SEMAPHORE_NONE:
      break;
   case RADV_SEMAPHORE_TIMELINE:
      radv_destroy_timeline(device, &part->timeline);
      break;
   case RADV_SEMAPHORE_SYNCOBJ:
   case RADV_SEMAPHORE_TIMELINE_SYNCOBJ:
      device->ws->destroy_syncobj(device->ws, part->syncobj);
      break;
   }
   part->kind = RADV_SEMAPHORE_NONE;
}

static VkSemaphoreTypeKHR
radv_get_semaphore_type(const void *pNext, uint64_t *initial_value)
{
   const VkSemaphoreTypeCreateInfo *type_info =
      vk_find_struct_const(pNext, SEMAPHORE_TYPE_CREATE_INFO);

   if (!type_info)
      return VK_SEMAPHORE_TYPE_BINARY;

   if (initial_value)
      *initial_value = type_info->initialValue;
   return type_info->semaphoreType;
}

static void
radv_destroy_semaphore(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                       struct radv_semaphore *sem)
{
   radv_destroy_semaphore_part(device, &sem->temporary);
   radv_destroy_semaphore_part(device, &sem->permanent);
   vk_object_base_finish(&sem->base);
   vk_free2(&device->vk.alloc, pAllocator, sem);
}

VkResult
radv_CreateSemaphore(VkDevice _device, const VkSemaphoreCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   uint64_t initial_value = 0;
   VkSemaphoreTypeKHR type = radv_get_semaphore_type(pCreateInfo->pNext, &initial_value);

   struct radv_semaphore *sem =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*sem), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sem)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &sem->base, VK_OBJECT_TYPE_SEMAPHORE);

   sem->temporary.kind = RADV_SEMAPHORE_NONE;
   sem->permanent.kind = RADV_SEMAPHORE_NONE;

   if (type == VK_SEMAPHORE_TYPE_TIMELINE &&
       device->physical_device->rad_info.has_timeline_syncobj) {
      int ret = device->ws->create_syncobj(device->ws, false, &sem->permanent.syncobj);
      if (ret) {
         radv_destroy_semaphore(device, pAllocator, sem);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
      device->ws->signal_syncobj(device->ws, sem->permanent.syncobj, initial_value);
      sem->permanent.timeline_syncobj.max_point = initial_value;
      sem->permanent.kind = RADV_SEMAPHORE_TIMELINE_SYNCOBJ;
   } else if (type == VK_SEMAPHORE_TYPE_TIMELINE) {
      radv_create_timeline(&sem->permanent.timeline, initial_value);
      sem->permanent.kind = RADV_SEMAPHORE_TIMELINE;
   } else {
      int ret = device->ws->create_syncobj(device->ws, false, &sem->permanent.syncobj);
      if (ret) {
         radv_destroy_semaphore(device, pAllocator, sem);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
      sem->permanent.kind = RADV_SEMAPHORE_SYNCOBJ;
   }

   *pSemaphore = radv_semaphore_to_handle(sem);
   return VK_SUCCESS;
}

void
radv_DestroySemaphore(VkDevice _device, VkSemaphore _semaphore,
                      const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_semaphore, sem, _semaphore);
   if (!_semaphore)
      return;

   radv_destroy_semaphore(device, pAllocator, sem);
}

VkResult
radv_GetSemaphoreCounterValue(VkDevice _device, VkSemaphore _semaphore, uint64_t *pValue)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_semaphore, semaphore, _semaphore);

   if (radv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   struct radv_semaphore_part *part = semaphore->temporary.kind != RADV_SEMAPHORE_NONE
                                         ? &semaphore->temporary
                                         : &semaphore->permanent;

   switch (part->kind) {
   case RADV_SEMAPHORE_TIMELINE: {
      mtx_lock(&part->timeline.mutex);
      radv_timeline_gc_locked(device, &part->timeline);
      *pValue = part->timeline.highest_signaled;
      mtx_unlock(&part->timeline.mutex);
      return VK_SUCCESS;
   }
   case RADV_SEMAPHORE_TIMELINE_SYNCOBJ: {
      return device->ws->query_syncobj(device->ws, part->syncobj, pValue);
   }
   case RADV_SEMAPHORE_NONE:
   case RADV_SEMAPHORE_SYNCOBJ:
      unreachable("Invalid semaphore type");
   }
   unreachable("Unhandled semaphore type");
}

static VkResult
radv_wait_timelines(struct radv_device *device, const VkSemaphoreWaitInfo *pWaitInfo,
                    uint64_t abs_timeout)
{
   if ((pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT_KHR) && pWaitInfo->semaphoreCount > 1) {
      for (;;) {
         for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
            RADV_FROM_HANDLE(radv_semaphore, semaphore, pWaitInfo->pSemaphores[i]);
            VkResult result =
               radv_timeline_wait(device, &semaphore->permanent.timeline, pWaitInfo->pValues[i], 0);

            if (result == VK_SUCCESS)
               return VK_SUCCESS;
         }
         if (radv_get_current_time() > abs_timeout)
            return VK_TIMEOUT;
      }
   }

   for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
      RADV_FROM_HANDLE(radv_semaphore, semaphore, pWaitInfo->pSemaphores[i]);
      VkResult result = radv_timeline_wait(device, &semaphore->permanent.timeline,
                                           pWaitInfo->pValues[i], abs_timeout);

      if (result != VK_SUCCESS)
         return result;
   }
   return VK_SUCCESS;
}
VkResult
radv_WaitSemaphores(VkDevice _device, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t timeout)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   if (radv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   uint64_t abs_timeout = radv_get_absolute_timeout(timeout);

   if (radv_semaphore_from_handle(pWaitInfo->pSemaphores[0])->permanent.kind ==
       RADV_SEMAPHORE_TIMELINE)
      return radv_wait_timelines(device, pWaitInfo, abs_timeout);

   if (pWaitInfo->semaphoreCount > UINT32_MAX / sizeof(uint32_t))
      return vk_errorf(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "semaphoreCount integer overflow");

   bool wait_all = !(pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT_KHR);
   uint32_t *handles = malloc(sizeof(*handles) * pWaitInfo->semaphoreCount);
   if (!handles)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
      RADV_FROM_HANDLE(radv_semaphore, semaphore, pWaitInfo->pSemaphores[i]);
      handles[i] = semaphore->permanent.syncobj;
   }

   bool success =
      device->ws->wait_timeline_syncobj(device->ws, handles, pWaitInfo->pValues,
                                        pWaitInfo->semaphoreCount, wait_all, false, abs_timeout);
   free(handles);
   return success ? VK_SUCCESS : VK_TIMEOUT;
}

VkResult
radv_SignalSemaphore(VkDevice _device, const VkSemaphoreSignalInfo *pSignalInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_semaphore, semaphore, pSignalInfo->semaphore);

   struct radv_semaphore_part *part = semaphore->temporary.kind != RADV_SEMAPHORE_NONE
                                         ? &semaphore->temporary
                                         : &semaphore->permanent;

   switch (part->kind) {
   case RADV_SEMAPHORE_TIMELINE: {
      mtx_lock(&part->timeline.mutex);
      radv_timeline_gc_locked(device, &part->timeline);
      part->timeline.highest_submitted = MAX2(part->timeline.highest_submitted, pSignalInfo->value);
      part->timeline.highest_signaled = MAX2(part->timeline.highest_signaled, pSignalInfo->value);

      struct list_head processing_list;
      list_inithead(&processing_list);
      radv_timeline_trigger_waiters_locked(&part->timeline, &processing_list);
      mtx_unlock(&part->timeline.mutex);

      VkResult result = radv_process_submissions(&processing_list);

      /* This needs to happen after radv_process_submissions, so
       * that any submitted submissions that are now unblocked get
       * processed before we wake the application. This way we
       * ensure that any binary semaphores that are now unblocked
       * are usable by the application. */
      u_cnd_monotonic_broadcast(&device->timeline_cond);

      return result;
   }
   case RADV_SEMAPHORE_TIMELINE_SYNCOBJ: {
      part->timeline_syncobj.max_point = MAX2(part->timeline_syncobj.max_point, pSignalInfo->value);
      device->ws->signal_syncobj(device->ws, part->syncobj, pSignalInfo->value);
      break;
   }
   case RADV_SEMAPHORE_NONE:
   case RADV_SEMAPHORE_SYNCOBJ:
      unreachable("Invalid semaphore type");
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

VkResult
radv_CreateEvent(VkDevice _device, const VkEventCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_event *event = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*event), 8,
                                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!event)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &event->base, VK_OBJECT_TYPE_EVENT);

   VkResult result = device->ws->buffer_create(
      device->ws, 8, 8, RADEON_DOMAIN_GTT,
      RADEON_FLAG_VA_UNCACHED | RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING,
      RADV_BO_PRIORITY_FENCE, 0, &event->bo);
   if (result != VK_SUCCESS) {
      radv_destroy_event(device, pAllocator, event);
      return vk_error(device->instance, result);
   }

   event->map = (uint64_t *)device->ws->buffer_map(event->bo);
   if (!event->map) {
      radv_destroy_event(device, pAllocator, event);
      return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   *pEvent = radv_event_to_handle(event);

   return VK_SUCCESS;
}

void
radv_DestroyEvent(VkDevice _device, VkEvent _event, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_event, event, _event);

   if (!event)
      return;

   radv_destroy_event(device, pAllocator, event);
}

VkResult
radv_GetEventStatus(VkDevice _device, VkEvent _event)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_event, event, _event);

   if (radv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   if (*event->map == 1)
      return VK_EVENT_SET;
   return VK_EVENT_RESET;
}

VkResult
radv_SetEvent(VkDevice _device, VkEvent _event)
{
   RADV_FROM_HANDLE(radv_event, event, _event);
   *event->map = 1;

   return VK_SUCCESS;
}

VkResult
radv_ResetEvent(VkDevice _device, VkEvent _event)
{
   RADV_FROM_HANDLE(radv_event, event, _event);
   *event->map = 0;

   return VK_SUCCESS;
}

static void
radv_destroy_buffer(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                    struct radv_buffer *buffer)
{
   if ((buffer->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) && buffer->bo)
      device->ws->buffer_destroy(device->ws, buffer->bo);

   vk_object_base_finish(&buffer->base);
   vk_free2(&device->vk.alloc, pAllocator, buffer);
}

VkResult
radv_CreateBuffer(VkDevice _device, const VkBufferCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_buffer *buffer;

   if (pCreateInfo->size > RADV_MAX_MEMORY_ALLOCATION_SIZE)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

   buffer = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*buffer), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (buffer == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &buffer->base, VK_OBJECT_TYPE_BUFFER);

   buffer->size = pCreateInfo->size;
   buffer->usage = pCreateInfo->usage;
   buffer->bo = NULL;
   buffer->offset = 0;
   buffer->flags = pCreateInfo->flags;

   buffer->shareable =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_MEMORY_BUFFER_CREATE_INFO) != NULL;

   if (pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      enum radeon_bo_flag flags = RADEON_FLAG_VIRTUAL;
      if (pCreateInfo->flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)
         flags |= RADEON_FLAG_REPLAYABLE;

      uint64_t replay_address = 0;
      const VkBufferOpaqueCaptureAddressCreateInfo *replay_info =
         vk_find_struct_const(pCreateInfo->pNext, BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO);
      if (replay_info && replay_info->opaqueCaptureAddress)
         replay_address = replay_info->opaqueCaptureAddress;

      VkResult result = device->ws->buffer_create(device->ws, align64(buffer->size, 4096), 4096, 0,
                                                  flags, RADV_BO_PRIORITY_VIRTUAL,
                                                  replay_address, &buffer->bo);
      if (result != VK_SUCCESS) {
         radv_destroy_buffer(device, pAllocator, buffer);
         return vk_error(device->instance, result);
      }
   }

   *pBuffer = radv_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

void
radv_DestroyBuffer(VkDevice _device, VkBuffer _buffer, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);

   if (!buffer)
      return;

   radv_destroy_buffer(device, pAllocator, buffer);
}

VkDeviceAddress
radv_GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);
   return radv_buffer_get_va(buffer->bo) + buffer->offset;
}

uint64_t
radv_GetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, pInfo->buffer);
   return buffer->bo ? radv_buffer_get_va(buffer->bo) + buffer->offset : 0;
}

uint64_t
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
   return iview->type == VK_IMAGE_VIEW_TYPE_3D ? iview->extent.depth
                                               : (iview->base_layer + iview->layer_count);
}

static unsigned
get_dcc_max_uncompressed_block_size(const struct radv_device *device,
                                    const struct radv_image_view *iview)
{
   if (device->physical_device->rad_info.chip_class < GFX10 && iview->image->info.samples > 1) {
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

   if (!radv_dcc_enabled(iview->image, iview->base_mip))
      return 0;

   /* For GFX9+ ac_surface computes values for us (except min_compressed
    * and max_uncompressed) */
   if (device->physical_device->rad_info.chip_class >= GFX9) {
      max_compressed_block_size =
         iview->image->planes[0].surface.u.gfx9.color.dcc.max_compressed_block_size;
      independent_128b_blocks = iview->image->planes[0].surface.u.gfx9.color.dcc.independent_128B_blocks;
      independent_64b_blocks = iview->image->planes[0].surface.u.gfx9.color.dcc.independent_64B_blocks;
   } else {
      independent_128b_blocks = 0;

      if (iview->image->usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
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

   return S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(max_uncompressed_block_size) |
          S_028C78_MAX_COMPRESSED_BLOCK_SIZE(max_compressed_block_size) |
          S_028C78_MIN_COMPRESSED_BLOCK_SIZE(min_compressed_block_size) |
          S_028C78_INDEPENDENT_64B_BLOCKS(independent_64b_blocks) |
          S_028C78_INDEPENDENT_128B_BLOCKS(independent_128b_blocks);
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

   desc = vk_format_description(iview->vk_format);

   memset(cb, 0, sizeof(*cb));

   /* Intensity is implemented as Red, so treat it that way. */
   cb->cb_color_attrib = S_028C74_FORCE_DST_ALPHA_1(desc->swizzle[3] == PIPE_SWIZZLE_1);

   va = radv_buffer_get_va(iview->image->bo) + iview->image->offset;

   cb->cb_color_base = va >> 8;

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      if (device->physical_device->rad_info.chip_class >= GFX10) {
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
      const struct legacy_surf_level *level_info = &surf->u.legacy.level[iview->base_mip];
      unsigned pitch_tile_max, slice_tile_max, tile_mode_index;

      cb->cb_color_base += level_info->offset_256B;
      if (level_info->mode == RADEON_SURF_MODE_2D)
         cb->cb_color_base |= surf->tile_swizzle;

      pitch_tile_max = level_info->nblk_x / 8 - 1;
      slice_tile_max = (level_info->nblk_x * level_info->nblk_y) / 64 - 1;
      tile_mode_index = si_tile_mode_index(plane, iview->base_mip, false);

      cb->cb_color_pitch = S_028C64_TILE_MAX(pitch_tile_max);
      cb->cb_color_slice = S_028C68_TILE_MAX(slice_tile_max);
      cb->cb_color_cmask_slice = surf->u.legacy.color.cmask_slice_tile_max;

      cb->cb_color_attrib |= S_028C74_TILE_MODE_INDEX(tile_mode_index);

      if (radv_image_has_fmask(iview->image)) {
         if (device->physical_device->rad_info.chip_class >= GFX7)
            cb->cb_color_pitch |=
               S_028C64_FMASK_TILE_MAX(surf->u.legacy.color.fmask.pitch_in_pixels / 8 - 1);
         cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(surf->u.legacy.color.fmask.tiling_index);
         cb->cb_color_fmask_slice = S_028C88_TILE_MAX(surf->u.legacy.color.fmask.slice_tile_max);
      } else {
         /* This must be set for fast clear to work without FMASK. */
         if (device->physical_device->rad_info.chip_class >= GFX7)
            cb->cb_color_pitch |= S_028C64_FMASK_TILE_MAX(pitch_tile_max);
         cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(tile_mode_index);
         cb->cb_color_fmask_slice = S_028C88_TILE_MAX(slice_tile_max);
      }
   }

   /* CMASK variables */
   va = radv_buffer_get_va(iview->image->bo) + iview->image->offset;
   va += surf->cmask_offset;
   cb->cb_color_cmask = va >> 8;

   va = radv_buffer_get_va(iview->image->bo) + iview->image->offset;
   va += surf->meta_offset;

   if (radv_dcc_enabled(iview->image, iview->base_mip) &&
       device->physical_device->rad_info.chip_class <= GFX8)
      va += plane->surface.u.legacy.color.dcc_level[iview->base_mip].dcc_offset;

   unsigned dcc_tile_swizzle = surf->tile_swizzle;
   dcc_tile_swizzle &= ((1 << surf->meta_alignment_log2) - 1) >> 8;

   cb->cb_dcc_base = va >> 8;
   cb->cb_dcc_base |= dcc_tile_swizzle;

   /* GFX10 field has the same base shift as the GFX6 field. */
   uint32_t max_slice = radv_surface_max_layer_count(iview) - 1;
   cb->cb_color_view =
      S_028C6C_SLICE_START(iview->base_layer) | S_028C6C_SLICE_MAX_GFX10(max_slice);

   if (iview->image->info.samples > 1) {
      unsigned log_samples = util_logbase2(iview->image->info.samples);

      cb->cb_color_attrib |=
         S_028C74_NUM_SAMPLES(log_samples) | S_028C74_NUM_FRAGMENTS(log_samples);
   }

   if (radv_image_has_fmask(iview->image)) {
      va = radv_buffer_get_va(iview->image->bo) + iview->image->offset + surf->fmask_offset;
      cb->cb_color_fmask = va >> 8;
      cb->cb_color_fmask |= surf->fmask_tile_swizzle;
   } else {
      cb->cb_color_fmask = cb->cb_color_base;
   }

   ntype = radv_translate_color_numformat(iview->vk_format, desc,
                                          vk_format_get_first_non_void_channel(iview->vk_format));
   format = radv_translate_colorformat(iview->vk_format);
   assert(format != V_028C70_COLOR_INVALID);

   swap = radv_translate_colorswap(iview->vk_format, false);
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
      S_028C70_FORMAT(format) | S_028C70_COMP_SWAP(swap) | S_028C70_BLEND_CLAMP(blend_clamp) |
      S_028C70_BLEND_BYPASS(blend_bypass) | S_028C70_SIMPLE_FLOAT(1) |
      S_028C70_ROUND_MODE(ntype != V_028C70_NUMBER_UNORM && ntype != V_028C70_NUMBER_SNORM &&
                          ntype != V_028C70_NUMBER_SRGB && format != V_028C70_COLOR_8_24 &&
                          format != V_028C70_COLOR_24_8) |
      S_028C70_NUMBER_TYPE(ntype) | S_028C70_ENDIAN(endian);
   if (radv_image_has_fmask(iview->image)) {
      cb->cb_color_info |= S_028C70_COMPRESSION(1);
      if (device->physical_device->rad_info.chip_class == GFX6) {
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

         if (device->physical_device->rad_info.chip_class == GFX8) {
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

   if (radv_dcc_enabled(iview->image, iview->base_mip))
      cb->cb_color_info |= S_028C70_DCC_ENABLE(1);

   cb->cb_dcc_control = radv_init_dcc_control_reg(device, iview);

   /* This must be set for fast clear to work without FMASK. */
   if (!radv_image_has_fmask(iview->image) &&
       device->physical_device->rad_info.chip_class == GFX6) {
      unsigned bankh = util_logbase2(surf->u.legacy.bankh);
      cb->cb_color_attrib |= S_028C74_FMASK_BANK_HEIGHT(bankh);
   }

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      unsigned mip0_depth = iview->image->type == VK_IMAGE_TYPE_3D
                               ? (iview->extent.depth - 1)
                               : (iview->image->info.array_size - 1);
      unsigned width =
         vk_format_get_plane_width(iview->image->vk_format, iview->plane_id, iview->extent.width);
      unsigned height =
         vk_format_get_plane_height(iview->image->vk_format, iview->plane_id, iview->extent.height);

      if (device->physical_device->rad_info.chip_class >= GFX10) {
         cb->cb_color_view |= S_028C6C_MIP_LEVEL_GFX10(iview->base_mip);

         cb->cb_color_attrib3 |= S_028EE0_MIP0_DEPTH(mip0_depth) |
                                 S_028EE0_RESOURCE_TYPE(surf->u.gfx9.resource_type) |
                                 S_028EE0_RESOURCE_LEVEL(1);
      } else {
         cb->cb_color_view |= S_028C6C_MIP_LEVEL_GFX9(iview->base_mip);
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

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      /* Default value for 32-bit depth surfaces. */
      max_zplanes = 4;

      if (iview->vk_format == VK_FORMAT_D16_UNORM && iview->image->info.samples > 1)
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
      if (iview->vk_format == VK_FORMAT_D16_UNORM) {
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

   assert(image->vk_format == VK_FORMAT_D16_UNORM);
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
   unsigned level = iview->base_mip;
   unsigned format, stencil_format;
   uint64_t va, s_offs, z_offs;
   bool stencil_only = iview->image->vk_format == VK_FORMAT_S8_UINT;
   const struct radv_image_plane *plane = &iview->image->planes[0];
   const struct radeon_surf *surf = &plane->surface;

   assert(vk_format_get_plane_count(iview->image->vk_format) == 1);

   memset(ds, 0, sizeof(*ds));
   if (!device->instance->absolute_depth_bias) {
      switch (iview->image->vk_format) {
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

   format = radv_translate_dbformat(iview->image->vk_format);
   stencil_format = surf->has_stencil ? V_028044_STENCIL_8 : V_028044_STENCIL_INVALID;

   uint32_t max_slice = radv_surface_max_layer_count(iview) - 1;
   ds->db_depth_view = S_028008_SLICE_START(iview->base_layer) | S_028008_SLICE_MAX(max_slice);
   if (device->physical_device->rad_info.chip_class >= GFX10) {
      ds->db_depth_view |=
         S_028008_SLICE_START_HI(iview->base_layer >> 11) | S_028008_SLICE_MAX_HI(max_slice >> 11);
   }

   ds->db_htile_data_base = 0;
   ds->db_htile_surface = 0;

   va = radv_buffer_get_va(iview->image->bo) + iview->image->offset;
   s_offs = z_offs = va;

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      assert(surf->u.gfx9.surf_offset == 0);
      s_offs += surf->u.gfx9.zs.stencil_offset;

      ds->db_z_info = S_028038_FORMAT(format) |
                      S_028038_NUM_SAMPLES(util_logbase2(iview->image->info.samples)) |
                      S_028038_SW_MODE(surf->u.gfx9.swizzle_mode) |
                      S_028038_MAXMIP(iview->image->info.levels - 1) | S_028038_ZRANGE_PRECISION(1);
      ds->db_stencil_info =
         S_02803C_FORMAT(stencil_format) | S_02803C_SW_MODE(surf->u.gfx9.zs.stencil_swizzle_mode);

      if (device->physical_device->rad_info.chip_class == GFX9) {
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

            if (device->physical_device->rad_info.chip_class >= GFX10) {
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

         va = radv_buffer_get_va(iview->image->bo) + iview->image->offset + surf->meta_offset;
         ds->db_htile_data_base = va >> 8;
         ds->db_htile_surface = S_028ABC_FULL_CACHE(1) | S_028ABC_PIPE_ALIGNED(1);

         if (device->physical_device->rad_info.chip_class == GFX9) {
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

      if (device->physical_device->rad_info.chip_class >= GFX7) {
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

         va = radv_buffer_get_va(iview->image->bo) + iview->image->offset + surf->meta_offset;
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

VkResult
radv_CreateFramebuffer(VkDevice _device, const VkFramebufferCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_framebuffer *framebuffer;
   const VkFramebufferAttachmentsCreateInfo *imageless_create_info =
      vk_find_struct_const(pCreateInfo->pNext, FRAMEBUFFER_ATTACHMENTS_CREATE_INFO);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   size_t size = sizeof(*framebuffer);
   if (!imageless_create_info)
      size += sizeof(struct radv_image_view *) * pCreateInfo->attachmentCount;
   framebuffer =
      vk_alloc2(&device->vk.alloc, pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (framebuffer == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &framebuffer->base, VK_OBJECT_TYPE_FRAMEBUFFER);

   framebuffer->attachment_count = pCreateInfo->attachmentCount;
   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;
   framebuffer->imageless = !!imageless_create_info;

   if (!imageless_create_info) {
      for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
         VkImageView _iview = pCreateInfo->pAttachments[i];
         struct radv_image_view *iview = radv_image_view_from_handle(_iview);
         framebuffer->attachments[i] = iview;
      }
   }

   *pFramebuffer = radv_framebuffer_to_handle(framebuffer);
   return VK_SUCCESS;
}

void
radv_DestroyFramebuffer(VkDevice _device, VkFramebuffer _fb,
                        const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_framebuffer, fb, _fb);

   if (!fb)
      return;
   vk_object_base_finish(&fb->base);
   vk_free2(&device->vk.alloc, pAllocator, fb);
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
   case VK_FILTER_CUBIC_IMG:
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
   if (filter < 2)
      return 0;
   if (filter < 4)
      return 1;
   if (filter < 8)
      return 2;
   if (filter < 16)
      return 3;
   return 4;
}

static unsigned
radv_tex_filter_mode(VkSamplerReductionMode mode)
{
   switch (mode) {
   case VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT:
      return V_008F30_SQ_IMG_FILTER_MODE_BLEND;
   case VK_SAMPLER_REDUCTION_MODE_MIN_EXT:
      return V_008F30_SQ_IMG_FILTER_MODE_MIN;
   case VK_SAMPLER_REDUCTION_MODE_MAX_EXT:
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

static inline int
S_FIXED(float value, unsigned frac_bits)
{
   return value * (1 << frac_bits);
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
   bool compat_mode = device->physical_device->rad_info.chip_class == GFX8 ||
                      device->physical_device->rad_info.chip_class == GFX9;
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
       S_008F30_DISABLE_CUBE_WRAP(0) | S_008F30_COMPAT_MODE(compat_mode) |
       S_008F30_FILTER_MODE(filter_mode) | S_008F30_TRUNC_COORD(trunc_coord));
   sampler->state[1] = (S_008F34_MIN_LOD(S_FIXED(CLAMP(pCreateInfo->minLod, 0, 15), 8)) |
                        S_008F34_MAX_LOD(S_FIXED(CLAMP(pCreateInfo->maxLod, 0, 15), 8)) |
                        S_008F34_PERF_MIP(max_aniso_ratio ? max_aniso_ratio + 6 : 0));
   sampler->state[2] = (S_008F38_LOD_BIAS(S_FIXED(CLAMP(pCreateInfo->mipLodBias, -16, 16), 8)) |
                        S_008F38_XY_MAG_FILTER(radv_tex_filter(pCreateInfo->magFilter, max_aniso)) |
                        S_008F38_XY_MIN_FILTER(radv_tex_filter(pCreateInfo->minFilter, max_aniso)) |
                        S_008F38_MIP_FILTER(radv_tex_mipfilter(pCreateInfo->mipmapMode)) |
                        S_008F38_MIP_POINT_PRECLAMP(0));
   sampler->state[3] = (S_008F3C_BORDER_COLOR_PTR(border_color_ptr) |
                        S_008F3C_BORDER_COLOR_TYPE(radv_tex_bordercolor(border_color)));

   if (device->physical_device->rad_info.chip_class >= GFX10) {
      sampler->state[2] |= S_008F38_ANISO_OVERRIDE_GFX10(1);
   } else {
      sampler->state[2] |=
         S_008F38_DISABLE_LSB_CEIL(device->physical_device->rad_info.chip_class <= GFX8) |
         S_008F38_FILTER_PREC_FIX(1) |
         S_008F38_ANISO_OVERRIDE_GFX8(device->physical_device->rad_info.chip_class >= GFX8);
   }
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &sampler->base, VK_OBJECT_TYPE_SAMPLER);

   radv_init_sampler(device, sampler, pCreateInfo);

   sampler->ycbcr_sampler =
      ycbcr_conversion ? radv_sampler_ycbcr_conversion_from_handle(ycbcr_conversion->conversion)
                       : NULL;
   *pSampler = radv_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

void
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
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 4u);
   return VK_SUCCESS;
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
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

   return bits;
}
VkResult
radv_GetMemoryFdPropertiesKHR(VkDevice _device, VkExternalMemoryHandleTypeFlagBits handleType,
                              int fd, VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT: {
      enum radeon_bo_domain domains;
      enum radeon_bo_flag flags;
      if (!device->ws->buffer_get_flags_from_fd(device->ws, fd, &domains, &flags))
         return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

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
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

static VkResult
radv_import_opaque_fd(struct radv_device *device, int fd, uint32_t *syncobj)
{
   uint32_t syncobj_handle = 0;
   int ret = device->ws->import_syncobj(device->ws, fd, &syncobj_handle);
   if (ret != 0)
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   if (*syncobj)
      device->ws->destroy_syncobj(device->ws, *syncobj);

   *syncobj = syncobj_handle;
   close(fd);

   return VK_SUCCESS;
}

static VkResult
radv_import_sync_fd(struct radv_device *device, int fd, uint32_t *syncobj)
{
   /* If we create a syncobj we do it locally so that if we have an error, we don't
    * leave a syncobj in an undetermined state in the fence. */
   uint32_t syncobj_handle = *syncobj;
   if (!syncobj_handle) {
      bool create_signaled = fd == -1 ? true : false;

      int ret = device->ws->create_syncobj(device->ws, create_signaled, &syncobj_handle);
      if (ret) {
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else {
      if (fd == -1)
         device->ws->signal_syncobj(device->ws, syncobj_handle, 0);
   }

   if (fd != -1) {
      int ret = device->ws->import_syncobj_from_sync_file(device->ws, syncobj_handle, fd);
      if (ret)
         return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
      close(fd);
   }

   *syncobj = syncobj_handle;

   return VK_SUCCESS;
}

VkResult
radv_ImportSemaphoreFdKHR(VkDevice _device,
                          const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_semaphore, sem, pImportSemaphoreFdInfo->semaphore);
   VkResult result;
   struct radv_semaphore_part *dst = NULL;
   bool timeline = sem->permanent.kind == RADV_SEMAPHORE_TIMELINE_SYNCOBJ;

   if (pImportSemaphoreFdInfo->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT) {
      assert(!timeline);
      dst = &sem->temporary;
   } else {
      dst = &sem->permanent;
   }

   uint32_t syncobj =
      (dst->kind == RADV_SEMAPHORE_SYNCOBJ || dst->kind == RADV_SEMAPHORE_TIMELINE_SYNCOBJ)
         ? dst->syncobj
         : 0;

   switch (pImportSemaphoreFdInfo->handleType) {
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT:
      result = radv_import_opaque_fd(device, pImportSemaphoreFdInfo->fd, &syncobj);
      break;
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT:
      assert(!timeline);
      result = radv_import_sync_fd(device, pImportSemaphoreFdInfo->fd, &syncobj);
      break;
   default:
      unreachable("Unhandled semaphore handle type");
   }

   if (result == VK_SUCCESS) {
      dst->syncobj = syncobj;
      dst->kind = RADV_SEMAPHORE_SYNCOBJ;
      if (timeline) {
         dst->kind = RADV_SEMAPHORE_TIMELINE_SYNCOBJ;
         dst->timeline_syncobj.max_point = 0;
      }
   }

   return result;
}

VkResult
radv_GetSemaphoreFdKHR(VkDevice _device, const VkSemaphoreGetFdInfoKHR *pGetFdInfo, int *pFd)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_semaphore, sem, pGetFdInfo->semaphore);
   int ret;
   uint32_t syncobj_handle;

   if (sem->temporary.kind != RADV_SEMAPHORE_NONE) {
      assert(sem->temporary.kind == RADV_SEMAPHORE_SYNCOBJ ||
             sem->temporary.kind == RADV_SEMAPHORE_TIMELINE_SYNCOBJ);
      syncobj_handle = sem->temporary.syncobj;
   } else {
      assert(sem->permanent.kind == RADV_SEMAPHORE_SYNCOBJ ||
             sem->permanent.kind == RADV_SEMAPHORE_TIMELINE_SYNCOBJ);
      syncobj_handle = sem->permanent.syncobj;
   }

   switch (pGetFdInfo->handleType) {
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT:
      ret = device->ws->export_syncobj(device->ws, syncobj_handle, pFd);
      if (ret)
         return vk_error(device->instance, VK_ERROR_TOO_MANY_OBJECTS);
      break;
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT:
      ret = device->ws->export_syncobj_to_sync_file(device->ws, syncobj_handle, pFd);
      if (ret)
         return vk_error(device->instance, VK_ERROR_TOO_MANY_OBJECTS);

      if (sem->temporary.kind != RADV_SEMAPHORE_NONE) {
         radv_destroy_semaphore_part(device, &sem->temporary);
      } else {
         device->ws->reset_syncobj(device->ws, syncobj_handle);
      }
      break;
   default:
      unreachable("Unhandled semaphore handle type");
   }

   return VK_SUCCESS;
}

void
radv_GetPhysicalDeviceExternalSemaphoreProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
   VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   VkSemaphoreTypeKHR type = radv_get_semaphore_type(pExternalSemaphoreInfo->pNext, NULL);

   if (type == VK_SEMAPHORE_TYPE_TIMELINE && pdevice->rad_info.has_timeline_syncobj &&
       pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT) {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
      pExternalSemaphoreProperties->compatibleHandleTypes =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
      pExternalSemaphoreProperties->externalSemaphoreFeatures =
         VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   } else if (type == VK_SEMAPHORE_TYPE_TIMELINE) {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
      pExternalSemaphoreProperties->compatibleHandleTypes = 0;
      pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
   } else if (pExternalSemaphoreInfo->handleType ==
                 VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT ||
              pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT) {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalSemaphoreProperties->compatibleHandleTypes =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalSemaphoreProperties->externalSemaphoreFeatures =
         VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   } else if (pExternalSemaphoreInfo->handleType ==
              VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT) {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
      pExternalSemaphoreProperties->compatibleHandleTypes =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
      pExternalSemaphoreProperties->externalSemaphoreFeatures =
         VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   } else {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
      pExternalSemaphoreProperties->compatibleHandleTypes = 0;
      pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
   }
}

VkResult
radv_ImportFenceFdKHR(VkDevice _device, const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_fence, fence, pImportFenceFdInfo->fence);
   struct radv_fence_part *dst = NULL;
   VkResult result;

   if (pImportFenceFdInfo->flags & VK_FENCE_IMPORT_TEMPORARY_BIT) {
      dst = &fence->temporary;
   } else {
      dst = &fence->permanent;
   }

   uint32_t syncobj = dst->kind == RADV_FENCE_SYNCOBJ ? dst->syncobj : 0;

   switch (pImportFenceFdInfo->handleType) {
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT:
      result = radv_import_opaque_fd(device, pImportFenceFdInfo->fd, &syncobj);
      break;
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT:
      result = radv_import_sync_fd(device, pImportFenceFdInfo->fd, &syncobj);
      break;
   default:
      unreachable("Unhandled fence handle type");
   }

   if (result == VK_SUCCESS) {
      dst->syncobj = syncobj;
      dst->kind = RADV_FENCE_SYNCOBJ;
   }

   return result;
}

VkResult
radv_GetFenceFdKHR(VkDevice _device, const VkFenceGetFdInfoKHR *pGetFdInfo, int *pFd)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_fence, fence, pGetFdInfo->fence);
   int ret;

   struct radv_fence_part *part =
      fence->temporary.kind != RADV_FENCE_NONE ? &fence->temporary : &fence->permanent;

   switch (pGetFdInfo->handleType) {
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT:
      ret = device->ws->export_syncobj(device->ws, part->syncobj, pFd);
      if (ret)
         return vk_error(device->instance, VK_ERROR_TOO_MANY_OBJECTS);
      break;
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT:
      ret = device->ws->export_syncobj_to_sync_file(device->ws, part->syncobj, pFd);
      if (ret)
         return vk_error(device->instance, VK_ERROR_TOO_MANY_OBJECTS);

      if (part == &fence->temporary) {
         radv_destroy_fence_part(device, part);
      } else {
         device->ws->reset_syncobj(device->ws, part->syncobj);
      }
      break;
   default:
      unreachable("Unhandled fence handle type");
   }

   return VK_SUCCESS;
}

void
radv_GetPhysicalDeviceExternalFenceProperties(
   VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
   VkExternalFenceProperties *pExternalFenceProperties)
{
   if (pExternalFenceInfo->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT ||
       pExternalFenceInfo->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT) {
      pExternalFenceProperties->exportFromImportedHandleTypes =
         VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalFenceProperties->compatibleHandleTypes =
         VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalFenceProperties->externalFenceFeatures =
         VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   } else {
      pExternalFenceProperties->exportFromImportedHandleTypes = 0;
      pExternalFenceProperties->compatibleHandleTypes = 0;
      pExternalFenceProperties->externalFenceFeatures = 0;
   }
}

void
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

VkResult
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
static uint64_t
radv_clock_gettime(clockid_t clock_id)
{
   struct timespec current;
   int ret;

   ret = clock_gettime(clock_id, &current);
#ifdef CLOCK_MONOTONIC_RAW
   if (ret < 0 && clock_id == CLOCK_MONOTONIC_RAW)
      ret = clock_gettime(CLOCK_MONOTONIC, &current);
#endif
   if (ret < 0)
      return 0;

   return (uint64_t)current.tv_sec * 1000000000ULL + current.tv_nsec;
}

VkResult
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
   begin = radv_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   begin = radv_clock_gettime(CLOCK_MONOTONIC);
#endif

   for (d = 0; d < timestampCount; d++) {
      switch (pTimestampInfos[d].timeDomain) {
      case VK_TIME_DOMAIN_DEVICE_EXT:
         pTimestamps[d] = device->ws->query_value(device->ws, RADEON_TIMESTAMP);
         uint64_t device_period = DIV_ROUND_UP(1000000, clock_crystal_freq);
         max_clock_period = MAX2(max_clock_period, device_period);
         break;
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT:
         pTimestamps[d] = radv_clock_gettime(CLOCK_MONOTONIC);
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
   end = radv_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   end = radv_clock_gettime(CLOCK_MONOTONIC);
#endif

   /*
    * The maximum deviation is the sum of the interval over which we
    * perform the sampling and the maximum period of any sampled
    * clock. That's because the maximum skew between any two sampled
    * clock edges is when the sampled clock with the largest period is
    * sampled at the end of that period but right at the beginning of the
    * sampling interval and some other clock is sampled right at the
    * begining of its sampling period and right at the end of the
    * sampling interval. Let's assume the GPU has the longest clock
    * period and that the application is sampling GPU and monotonic:
    *
    *                               s                 e
    *			 w x y z 0 1 2 3 4 5 6 7 8 9 a b c d e f
    *	Raw              -_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-
    *
    *                               g
    *		  0         1         2         3
    *	GPU       -----_____-----_____-----_____-----_____
    *
    *                                                m
    *					    x y z 0 1 2 3 4 5 6 7 8 9 a b c
    *	Monotonic                           -_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-
    *
    *	Interval                     <----------------->
    *	Deviation           <-------------------------->
    *
    *		s  = read(raw)       2
    *		g  = read(GPU)       1
    *		m  = read(monotonic) 2
    *		e  = read(raw)       b
    *
    * We round the sample interval up by one tick to cover sampling error
    * in the interval clock
    */

   uint64_t sample_interval = end - begin + 1;

   *pMaxDeviation = sample_interval + max_clock_period;

   return VK_SUCCESS;
}
#endif

void
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

VkResult
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
