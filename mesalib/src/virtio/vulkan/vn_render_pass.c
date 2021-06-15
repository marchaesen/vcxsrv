/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_render_pass.h"

#include "venus-protocol/vn_protocol_driver_framebuffer.h"
#include "venus-protocol/vn_protocol_driver_render_pass.h"

#include "vn_device.h"

static bool
vn_render_pass_has_present_src(const VkRenderPassCreateInfo *create_info)
{
   /* XXX drop the #ifdef after fixing common wsi */
#ifdef ANDROID
   for (uint32_t i = 0; i < create_info->attachmentCount; i++) {
      const VkAttachmentDescription *att = &create_info->pAttachments[i];
      if (att->initialLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
          att->finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         return true;
   }
#endif

   return false;
}

static bool
vn_render_pass_has_present_src2(const VkRenderPassCreateInfo2 *create_info)
{
   /* XXX drop the #ifdef after fixing common wsi */
#ifdef ANDROID
   for (uint32_t i = 0; i < create_info->attachmentCount; i++) {
      const VkAttachmentDescription2 *att = &create_info->pAttachments[i];
      if (att->initialLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
          att->finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         return true;
   }
#endif

   return false;
}

/* render pass commands */

static const VkAttachmentDescription *
vn_get_intercepted_attachments(const VkAttachmentDescription *attachments,
                               uint32_t count,
                               const VkAllocationCallbacks *alloc)
{
   size_t size = sizeof(VkAttachmentDescription) * count;
   VkAttachmentDescription *out_attachments = vk_alloc(
      alloc, size, VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!out_attachments)
      return NULL;

   memcpy(out_attachments, attachments, size);
   for (uint32_t i = 0; i < count; i++) {
      if (out_attachments[i].initialLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         out_attachments[i].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
      if (out_attachments[i].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         out_attachments[i].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
   }
   return out_attachments;
}

VkResult
vn_CreateRenderPass(VkDevice device,
                    const VkRenderPassCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkRenderPass *pRenderPass)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_render_pass *pass =
      vk_zalloc(alloc, sizeof(*pass), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pass)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&pass->base, VK_OBJECT_TYPE_RENDER_PASS, &dev->base);

   VkRenderPassCreateInfo local_pass_info;
   if (vn_render_pass_has_present_src(pCreateInfo)) {
      local_pass_info = *pCreateInfo;
      local_pass_info.pAttachments = vn_get_intercepted_attachments(
         pCreateInfo->pAttachments, pCreateInfo->attachmentCount, alloc);
      if (!local_pass_info.pAttachments) {
         vk_free(alloc, pass);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
      pCreateInfo = &local_pass_info;
   }

   VkRenderPass pass_handle = vn_render_pass_to_handle(pass);
   vn_async_vkCreateRenderPass(dev->instance, device, pCreateInfo, NULL,
                               &pass_handle);

   if (pCreateInfo == &local_pass_info)
      vk_free(alloc, (void *)local_pass_info.pAttachments);

   *pRenderPass = pass_handle;

   return VK_SUCCESS;
}

static const VkAttachmentDescription2 *
vn_get_intercepted_attachments2(const VkAttachmentDescription2 *attachments,
                                uint32_t count,
                                const VkAllocationCallbacks *alloc)
{
   size_t size = sizeof(VkAttachmentDescription2) * count;
   VkAttachmentDescription2 *out_attachments = vk_alloc(
      alloc, size, VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!out_attachments)
      return NULL;

   memcpy(out_attachments, attachments, size);
   for (uint32_t i = 0; i < count; i++) {
      if (out_attachments[i].initialLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         out_attachments[i].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
      if (out_attachments[i].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         out_attachments[i].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
   }
   return out_attachments;
}

VkResult
vn_CreateRenderPass2(VkDevice device,
                     const VkRenderPassCreateInfo2 *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkRenderPass *pRenderPass)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_render_pass *pass =
      vk_zalloc(alloc, sizeof(*pass), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pass)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&pass->base, VK_OBJECT_TYPE_RENDER_PASS, &dev->base);

   VkRenderPassCreateInfo2 local_pass_info;
   if (vn_render_pass_has_present_src2(pCreateInfo)) {
      local_pass_info = *pCreateInfo;
      local_pass_info.pAttachments = vn_get_intercepted_attachments2(
         pCreateInfo->pAttachments, pCreateInfo->attachmentCount, alloc);
      if (!local_pass_info.pAttachments) {
         vk_free(alloc, pass);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
      pCreateInfo = &local_pass_info;
   }

   VkRenderPass pass_handle = vn_render_pass_to_handle(pass);
   vn_async_vkCreateRenderPass2(dev->instance, device, pCreateInfo, NULL,
                                &pass_handle);

   if (pCreateInfo == &local_pass_info)
      vk_free(alloc, (void *)local_pass_info.pAttachments);

   *pRenderPass = pass_handle;

   return VK_SUCCESS;
}

void
vn_DestroyRenderPass(VkDevice device,
                     VkRenderPass renderPass,
                     const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_render_pass *pass = vn_render_pass_from_handle(renderPass);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!pass)
      return;

   vn_async_vkDestroyRenderPass(dev->instance, device, renderPass, NULL);

   vn_object_base_fini(&pass->base);
   vk_free(alloc, pass);
}

void
vn_GetRenderAreaGranularity(VkDevice device,
                            VkRenderPass renderPass,
                            VkExtent2D *pGranularity)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_render_pass *pass = vn_render_pass_from_handle(renderPass);

   if (!pass->granularity.width) {
      vn_call_vkGetRenderAreaGranularity(dev->instance, device, renderPass,
                                         &pass->granularity);
   }

   *pGranularity = pass->granularity;
}

/* framebuffer commands */

VkResult
vn_CreateFramebuffer(VkDevice device,
                     const VkFramebufferCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkFramebuffer *pFramebuffer)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_framebuffer *fb = vk_zalloc(alloc, sizeof(*fb), VN_DEFAULT_ALIGN,
                                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!fb)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&fb->base, VK_OBJECT_TYPE_FRAMEBUFFER, &dev->base);

   VkFramebuffer fb_handle = vn_framebuffer_to_handle(fb);
   vn_async_vkCreateFramebuffer(dev->instance, device, pCreateInfo, NULL,
                                &fb_handle);

   *pFramebuffer = fb_handle;

   return VK_SUCCESS;
}

void
vn_DestroyFramebuffer(VkDevice device,
                      VkFramebuffer framebuffer,
                      const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_framebuffer *fb = vn_framebuffer_from_handle(framebuffer);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!fb)
      return;

   vn_async_vkDestroyFramebuffer(dev->instance, device, framebuffer, NULL);

   vn_object_base_fini(&fb->base);
   vk_free(alloc, fb);
}
