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
#include "vn_image.h"

#define COUNT_PRESENT_SRC(atts, att_count, initial_count, final_count)       \
   do {                                                                      \
      *initial_count = 0;                                                    \
      *final_count = 0;                                                      \
      for (uint32_t i = 0; i < att_count; i++) {                             \
         if (atts[i].initialLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)       \
            (*initial_count)++;                                              \
         if (atts[i].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)         \
            (*final_count)++;                                                \
      }                                                                      \
   } while (false)

#define REPLACE_PRESENT_SRC(pass, atts, att_count, out_atts)                 \
   do {                                                                      \
      struct vn_present_src_attachment *_acquire_atts =                      \
         pass->present_src_attachments;                                      \
      struct vn_present_src_attachment *_release_atts =                      \
         _acquire_atts + pass->acquire_count;                                \
                                                                             \
      memcpy(out_atts, atts, sizeof(*atts) * att_count);                     \
      for (uint32_t i = 0; i < att_count; i++) {                             \
         if (out_atts[i].initialLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) { \
            out_atts[i].initialLayout = VN_PRESENT_SRC_INTERNAL_LAYOUT;      \
            _acquire_atts->acquire = true;                                   \
            _acquire_atts->index = i;                                        \
            _acquire_atts++;                                                 \
         }                                                                   \
         if (out_atts[i].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {   \
            out_atts[i].finalLayout = VN_PRESENT_SRC_INTERNAL_LAYOUT;        \
            _release_atts->acquire = false;                                  \
            _release_atts->index = i;                                        \
            _release_atts++;                                                 \
         }                                                                   \
      }                                                                      \
   } while (false)

static void
vn_render_pass_count_present_src(const VkRenderPassCreateInfo *create_info,
                                 uint32_t *initial_count,
                                 uint32_t *final_count)
{
   COUNT_PRESENT_SRC(create_info->pAttachments, create_info->attachmentCount,
                     initial_count, final_count);
}

static void
vn_render_pass_count_present_src2(const VkRenderPassCreateInfo2 *create_info,
                                  uint32_t *initial_count,
                                  uint32_t *final_count)
{
   COUNT_PRESENT_SRC(create_info->pAttachments, create_info->attachmentCount,
                     initial_count, final_count);
}

static void
vn_render_pass_replace_present_src(struct vn_render_pass *pass,
                                   const VkRenderPassCreateInfo *create_info,
                                   VkAttachmentDescription *out_atts)
{
   REPLACE_PRESENT_SRC(pass, create_info->pAttachments,
                       create_info->attachmentCount, out_atts);
}

static void
vn_render_pass_replace_present_src2(struct vn_render_pass *pass,
                                    const VkRenderPassCreateInfo2 *create_info,
                                    VkAttachmentDescription2 *out_atts)
{
   REPLACE_PRESENT_SRC(pass, create_info->pAttachments,
                       create_info->attachmentCount, out_atts);
}

static void
vn_render_pass_setup_present_src_barriers(struct vn_render_pass *pass)
{
   /* TODO parse VkSubpassDependency for more accurate barriers */
   for (uint32_t i = 0; i < pass->present_src_count; i++) {
      struct vn_present_src_attachment *att =
         &pass->present_src_attachments[i];

      if (att->acquire) {
         att->src_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
         att->src_access_mask = 0;

         att->dst_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
         att->dst_access_mask =
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      } else {
         att->src_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
         att->src_access_mask = VK_ACCESS_MEMORY_WRITE_BIT;

         att->dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
         att->dst_access_mask = 0;
      }
   }
}

static struct vn_render_pass *
vn_render_pass_create(struct vn_device *dev,
                      uint32_t acquire_count,
                      uint32_t release_count,
                      const VkAllocationCallbacks *alloc)
{
   const uint32_t total_count = acquire_count + release_count;
   struct vn_render_pass *pass = vk_zalloc(
      alloc,
      sizeof(*pass) + sizeof(pass->present_src_attachments[0]) * total_count,
      VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pass)
      return NULL;

   vn_object_base_init(&pass->base, VK_OBJECT_TYPE_RENDER_PASS, &dev->base);

   pass->acquire_count = acquire_count;
   pass->release_count = release_count;
   pass->present_src_count = total_count;

   return pass;
}

/* render pass commands */

VkResult
vn_CreateRenderPass(VkDevice device,
                    const VkRenderPassCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkRenderPass *pRenderPass)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   uint32_t acquire_count;
   uint32_t release_count;
   vn_render_pass_count_present_src(pCreateInfo, &acquire_count,
                                    &release_count);

   struct vn_render_pass *pass =
      vn_render_pass_create(dev, acquire_count, release_count, alloc);
   if (!pass)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkRenderPassCreateInfo local_pass_info;
   if (pass->present_src_count) {
      VkAttachmentDescription *temp_atts =
         vk_alloc(alloc, sizeof(*temp_atts) * pCreateInfo->attachmentCount,
                  VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!temp_atts) {
         vk_free(alloc, pass);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      vn_render_pass_replace_present_src(pass, pCreateInfo, temp_atts);
      vn_render_pass_setup_present_src_barriers(pass);

      local_pass_info = *pCreateInfo;
      local_pass_info.pAttachments = temp_atts;
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

VkResult
vn_CreateRenderPass2(VkDevice device,
                     const VkRenderPassCreateInfo2 *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkRenderPass *pRenderPass)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   uint32_t acquire_count;
   uint32_t release_count;
   vn_render_pass_count_present_src2(pCreateInfo, &acquire_count,
                                     &release_count);

   struct vn_render_pass *pass =
      vn_render_pass_create(dev, acquire_count, release_count, alloc);
   if (!pass)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkRenderPassCreateInfo2 local_pass_info;
   if (pass->present_src_count) {
      VkAttachmentDescription2 *temp_atts =
         vk_alloc(alloc, sizeof(*temp_atts) * pCreateInfo->attachmentCount,
                  VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!temp_atts) {
         vk_free(alloc, pass);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      vn_render_pass_replace_present_src2(pass, pCreateInfo, temp_atts);
      vn_render_pass_setup_present_src_barriers(pass);

      local_pass_info = *pCreateInfo;
      local_pass_info.pAttachments = temp_atts;
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

   /* Two render passes differ only in attachment image layouts are considered
    * compatible.  We must not use pCreateInfo->renderPass here.
    */
   const bool imageless =
      pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
   const uint32_t view_count = imageless ? 0 : pCreateInfo->attachmentCount;

   struct vn_framebuffer *fb =
      vk_zalloc(alloc, sizeof(*fb) + sizeof(*fb->image_views) * view_count,
                VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!fb)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&fb->base, VK_OBJECT_TYPE_FRAMEBUFFER, &dev->base);

   fb->image_view_count = view_count;
   memcpy(fb->image_views, pCreateInfo->pAttachments,
          sizeof(*pCreateInfo->pAttachments) * view_count);

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
