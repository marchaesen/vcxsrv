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

#include <vulkan/vulkan.h>

#if defined(__APPLE__)
// Source of MVK_VERSION
#include "MoltenVK/vk_mvk_moltenvk.h"
#endif

extern uint32_t zink_debug;

#define ZINK_DEBUG_NIR 0x1
#define ZINK_DEBUG_SPIRV 0x2
#define ZINK_DEBUG_TGSI 0x4
#define ZINK_DEBUG_VALIDATION 0x8

struct zink_screen {
   struct pipe_screen base;

   struct sw_winsys *winsys;

   struct slab_parent_pool transfer_pool;

   unsigned shader_id;

   VkInstance instance;
   struct zink_instance_info instance_info;

   VkPhysicalDevice pdev;

   struct zink_device_info info;

   bool have_X8_D24_UNORM_PACK32;
   bool have_D24_UNORM_S8_UINT;
   bool have_triangle_fans;

   uint32_t gfx_queue;
   uint32_t timestamp_valid_bits;
   VkDevice dev;
   VkDebugUtilsMessengerEXT debugUtilsCallbackHandle;

   uint32_t cur_custom_border_color_samplers;

   uint32_t loader_version;

   bool needs_mesa_wsi;

   PFN_vkGetPhysicalDeviceFeatures2 vk_GetPhysicalDeviceFeatures2;
   PFN_vkGetPhysicalDeviceProperties2 vk_GetPhysicalDeviceProperties2;

   PFN_vkCmdDrawIndirectCount vk_CmdDrawIndirectCount;
   PFN_vkCmdDrawIndexedIndirectCount vk_CmdDrawIndexedIndirectCount;

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
};

static inline struct zink_screen *
zink_screen(struct pipe_screen *pipe)
{
   return (struct zink_screen *)pipe;
}

VkFormat
zink_get_format(struct zink_screen *screen, enum pipe_format format);

bool
zink_is_depth_format_supported(struct zink_screen *screen, VkFormat format);

#define GET_PROC_ADDR(x) do {                                               \
      screen->vk_##x = (PFN_vk##x)vkGetDeviceProcAddr(screen->dev, "vk"#x); \
      if (!screen->vk_##x) {                                                \
         debug_printf("vkGetDeviceProcAddr failed: vk"#x"\n");              \
         return false;                                                      \
      } \
   } while (0)

#define GET_PROC_ADDR_INSTANCE(x) do {                                          \
      screen->vk_##x = (PFN_vk##x)vkGetInstanceProcAddr(screen->instance, "vk"#x); \
      if (!screen->vk_##x) {                                                \
         debug_printf("GetInstanceProcAddr failed: vk"#x"\n");        \
         return false;                                                      \
      } \
   } while (0)

#define GET_PROC_ADDR_INSTANCE_LOCAL(instance, x) PFN_vk##x vk_##x = (PFN_vk##x)vkGetInstanceProcAddr(instance, "vk"#x)

#endif
