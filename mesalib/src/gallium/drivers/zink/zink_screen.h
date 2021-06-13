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

#ifndef ZINK_SCREEN_H
#define ZINK_SCREEN_H

#include "zink_device_info.h"
#include "zink_instance.h"

#include "pipe/p_screen.h"
#include "util/slab.h"
#include "compiler/nir/nir.h"
#include "util/disk_cache.h"
#include "util/log.h"
#include "util/simple_mtx.h"

#include <vulkan/vulkan.h>

#if defined(__APPLE__)
// Source of MVK_VERSION
#include "MoltenVK/vk_mvk_moltenvk.h"
#endif

extern uint32_t zink_debug;
struct hash_table;

#define ZINK_DEBUG_NIR 0x1
#define ZINK_DEBUG_SPIRV 0x2
#define ZINK_DEBUG_TGSI 0x4
#define ZINK_DEBUG_VALIDATION 0x8

struct zink_screen {
   struct pipe_screen base;
   bool threaded;
   uint32_t curr_batch; //the current batch id
   uint32_t last_finished; //this is racy but ultimately doesn't matter
   VkSemaphore sem;
   VkSemaphore prev_sem;

   bool device_lost;
   struct sw_winsys *winsys;

   struct hash_table framebuffer_cache;
   simple_mtx_t framebuffer_mtx;
   struct hash_table surface_cache;
   simple_mtx_t surface_mtx;
   struct hash_table bufferview_cache;
   simple_mtx_t bufferview_mtx;

   struct slab_parent_pool transfer_pool;
   VkPipelineCache pipeline_cache;
   size_t pipeline_cache_size;
   struct disk_cache *disk_cache;
   cache_key disk_cache_key;

   simple_mtx_t mem_cache_mtx;
   struct hash_table *resource_mem_cache;

   unsigned shader_id;

   uint64_t total_video_mem;
   uint64_t total_mem;

   VkInstance instance;
   struct zink_instance_info instance_info;

   VkPhysicalDevice pdev;
   uint32_t vk_version;

   struct zink_device_info info;
   struct nir_shader_compiler_options nir_options;

   bool have_X8_D24_UNORM_PACK32;
   bool have_D24_UNORM_S8_UINT;
   bool have_triangle_fans;

   uint32_t gfx_queue;
   uint32_t max_queues;
   uint32_t timestamp_valid_bits;
   VkDevice dev;
   VkDebugUtilsMessengerEXT debugUtilsCallbackHandle;

   uint32_t cur_custom_border_color_samplers;

   bool needs_mesa_wsi;
   bool needs_mesa_flush_wsi;

   PFN_vkGetPhysicalDeviceFeatures2 vk_GetPhysicalDeviceFeatures2;
   PFN_vkGetPhysicalDeviceProperties2 vk_GetPhysicalDeviceProperties2;
   PFN_vkGetPhysicalDeviceFormatProperties2 vk_GetPhysicalDeviceFormatProperties2;
   PFN_vkGetPhysicalDeviceImageFormatProperties2 vk_GetPhysicalDeviceImageFormatProperties2;
   PFN_vkGetPhysicalDeviceMemoryProperties2 vk_GetPhysicalDeviceMemoryProperties2;

   PFN_vkCmdDrawIndirectCount vk_CmdDrawIndirectCount;
   PFN_vkCmdDrawIndexedIndirectCount vk_CmdDrawIndexedIndirectCount;

   PFN_vkWaitSemaphores vk_WaitSemaphores;

   PFN_vkGetMemoryFdKHR vk_GetMemoryFdKHR;
   PFN_vkCmdBeginConditionalRenderingEXT vk_CmdBeginConditionalRenderingEXT;
   PFN_vkCmdEndConditionalRenderingEXT vk_CmdEndConditionalRenderingEXT;

   PFN_vkCmdBindTransformFeedbackBuffersEXT vk_CmdBindTransformFeedbackBuffersEXT;
   PFN_vkCmdBeginTransformFeedbackEXT vk_CmdBeginTransformFeedbackEXT;
   PFN_vkCmdEndTransformFeedbackEXT vk_CmdEndTransformFeedbackEXT;
   PFN_vkCmdBeginQueryIndexedEXT vk_CmdBeginQueryIndexedEXT;
   PFN_vkCmdEndQueryIndexedEXT vk_CmdEndQueryIndexedEXT;
   PFN_vkCmdDrawIndirectByteCountEXT vk_CmdDrawIndirectByteCountEXT;

   PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT vk_GetPhysicalDeviceCalibrateableTimeDomainsEXT;
   PFN_vkGetCalibratedTimestampsEXT vk_GetCalibratedTimestampsEXT;

   PFN_vkCmdSetViewportWithCountEXT vk_CmdSetViewportWithCountEXT;
   PFN_vkCmdSetScissorWithCountEXT vk_CmdSetScissorWithCountEXT;
   PFN_vkCmdBindVertexBuffers2EXT vk_CmdBindVertexBuffers2EXT;

   PFN_vkCreateDebugUtilsMessengerEXT vk_CreateDebugUtilsMessengerEXT;
   PFN_vkDestroyDebugUtilsMessengerEXT vk_DestroyDebugUtilsMessengerEXT;

#if defined(MVK_VERSION)
   PFN_vkGetMoltenVKConfigurationMVK vk_GetMoltenVKConfigurationMVK;
   PFN_vkSetMoltenVKConfigurationMVK vk_SetMoltenVKConfigurationMVK;

   PFN_vkGetPhysicalDeviceMetalFeaturesMVK vk_GetPhysicalDeviceMetalFeaturesMVK;
   PFN_vkGetVersionStringsMVK vk_GetVersionStringsMVK;
   PFN_vkUseIOSurfaceMVK vk_UseIOSurfaceMVK;
   PFN_vkGetIOSurfaceMVK vk_GetIOSurfaceMVK;
#endif

   PFN_vkCreateSwapchainKHR vk_CreateSwapchainKHR;
   PFN_vkDestroySwapchainKHR vk_DestroySwapchainKHR;

   struct {
      bool dual_color_blend_by_location;
      bool inline_uniforms;
   } driconf;

   PFN_vkGetImageDrmFormatModifierPropertiesEXT vk_GetImageDrmFormatModifierPropertiesEXT;

   VkFormatProperties format_props[PIPE_FORMAT_COUNT];
   struct {
      uint32_t image_view;
      uint32_t buffer_view;
   } null_descriptor_hashes;

   PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT vk_GetPhysicalDeviceMultisamplePropertiesEXT;
   PFN_vkCmdSetSampleLocationsEXT vk_CmdSetSampleLocationsEXT;
};


/* update last_finished to account for batch_id wrapping */
static inline void
zink_screen_update_last_finished(struct zink_screen *screen, uint32_t batch_id)
{
   /* last_finished may have wrapped */
   if (screen->last_finished < UINT_MAX / 2) {
      /* last_finished has wrapped, batch_id has not */
      if (batch_id > UINT_MAX / 2)
         return;
   } else if (batch_id < UINT_MAX / 2) {
      /* batch_id has wrapped, last_finished has not */
      screen->last_finished = batch_id;
      return;
   }
   /* neither have wrapped */
   screen->last_finished = MAX2(batch_id, screen->last_finished);
}

/* check a batch_id against last_finished while accounting for wrapping */
static inline bool
zink_screen_check_last_finished(struct zink_screen *screen, uint32_t batch_id)
{
   /* last_finished may have wrapped */
   if (screen->last_finished < UINT_MAX / 2) {
      /* last_finished has wrapped, batch_id has not */
      if (batch_id > UINT_MAX / 2)
         return true;
   } else if (batch_id < UINT_MAX / 2) {
      /* batch_id has wrapped, last_finished has not */
      return false;
   }
   return screen->last_finished >= batch_id;
}

bool
zink_screen_init_semaphore(struct zink_screen *screen);

static inline bool
zink_screen_handle_vkresult(struct zink_screen *screen, VkResult ret)
{
   bool success = false;
   switch (ret) {
   case VK_SUCCESS:
      success = true;
      break;
   case VK_ERROR_DEVICE_LOST:
      screen->device_lost = true;
      FALLTHROUGH;
   default:
      success = false;
      break;
   }
   return success;
}

static inline struct zink_screen *
zink_screen(struct pipe_screen *pipe)
{
   return (struct zink_screen *)pipe;
}


struct mem_cache_entry {
   VkDeviceMemory mem;
   void *map;
};

VkFormat
zink_get_format(struct zink_screen *screen, enum pipe_format format);

bool
zink_is_depth_format_supported(struct zink_screen *screen, VkFormat format);

#define GET_PROC_ADDR(x) do {                                               \
      screen->vk_##x = (PFN_vk##x)vkGetDeviceProcAddr(screen->dev, "vk"#x); \
      if (!screen->vk_##x) {                                                \
         mesa_loge("ZINK: vkGetDeviceProcAddr failed: vk"#x"\n");           \
         return false;                                                      \
      } \
   } while (0)

#define GET_PROC_ADDR_KHR(x) do {                                               \
      screen->vk_##x = (PFN_vk##x)vkGetDeviceProcAddr(screen->dev, "vk"#x"KHR"); \
      if (!screen->vk_##x) {                                                \
         mesa_loge("ZINK: vkGetDeviceProcAddr failed: vk"#x"KHR\n");           \
         return false;                                                      \
      } \
   } while (0)

#define GET_PROC_ADDR_INSTANCE(x) do {                                          \
      screen->vk_##x = (PFN_vk##x)vkGetInstanceProcAddr(screen->instance, "vk"#x); \
      if (!screen->vk_##x) {                                                \
         mesa_loge("ZINK: GetInstanceProcAddr failed: vk"#x"\n");           \
         return false;                                                      \
      } \
   } while (0)

#define GET_PROC_ADDR_INSTANCE_LOCAL(instance, x) PFN_vk##x vk_##x = (PFN_vk##x)vkGetInstanceProcAddr(instance, "vk"#x)

void
zink_screen_update_pipeline_cache(struct zink_screen *screen);

#endif
