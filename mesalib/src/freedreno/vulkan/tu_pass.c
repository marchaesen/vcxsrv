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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "tu_private.h"

#include "vk_util.h"

VkResult
tu_CreateRenderPass(VkDevice _device,
                    const VkRenderPassCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkRenderPass *pRenderPass)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_render_pass *pass;
   size_t size;
   size_t attachments_offset;
   VkRenderPassMultiviewCreateInfo *multiview_info = NULL;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);

   size = sizeof(*pass);
   size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
   attachments_offset = size;
   size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

   pass = vk_alloc2(&device->alloc, pAllocator, size, 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(pass, 0, size);
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = (void *) pass + attachments_offset;

   vk_foreach_struct(ext, pCreateInfo->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO:
         multiview_info = (VkRenderPassMultiviewCreateInfo *) ext;
         break;
      default:
         break;
      }
   }

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];

      att->format = pCreateInfo->pAttachments[i].format;
      att->samples = pCreateInfo->pAttachments[i].samples;
      att->load_op = pCreateInfo->pAttachments[i].loadOp;
      att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
      att->initial_layout = pCreateInfo->pAttachments[i].initialLayout;
      att->final_layout = pCreateInfo->pAttachments[i].finalLayout;
      // att->store_op = pCreateInfo->pAttachments[i].storeOp;
      // att->stencil_store_op = pCreateInfo->pAttachments[i].stencilStoreOp;
   }
   uint32_t subpass_attachment_count = 0;
   struct tu_subpass_attachment *p;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];

      subpass_attachment_count +=
         desc->inputAttachmentCount + desc->colorAttachmentCount +
         (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
         (desc->pDepthStencilAttachment != NULL);
   }

   if (subpass_attachment_count) {
      pass->subpass_attachments = vk_alloc2(
         &device->alloc, pAllocator,
         subpass_attachment_count * sizeof(struct tu_subpass_attachment), 8,
         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (pass->subpass_attachments == NULL) {
         vk_free2(&device->alloc, pAllocator, pass);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else
      pass->subpass_attachments = NULL;

   p = pass->subpass_attachments;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];
      uint32_t color_sample_count = 1, depth_sample_count = 1;
      struct tu_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      if (multiview_info)
         subpass->view_mask = multiview_info->pViewMasks[i];

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = p;
         p += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            subpass->input_attachments[j] = (struct tu_subpass_attachment) {
               .attachment = desc->pInputAttachments[j].attachment,
               .layout = desc->pInputAttachments[j].layout,
            };
            if (desc->pInputAttachments[j].attachment != VK_ATTACHMENT_UNUSED)
               pass->attachments[desc->pInputAttachments[j].attachment]
                  .view_mask |= subpass->view_mask;
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->color_attachments[j] = (struct tu_subpass_attachment) {
               .attachment = desc->pColorAttachments[j].attachment,
               .layout = desc->pColorAttachments[j].layout,
            };
            if (desc->pColorAttachments[j].attachment !=
                VK_ATTACHMENT_UNUSED) {
               pass->attachments[desc->pColorAttachments[j].attachment]
                  .view_mask |= subpass->view_mask;
               color_sample_count =
                  pCreateInfo
                     ->pAttachments[desc->pColorAttachments[j].attachment]
                     .samples;
            }
         }
      }

      subpass->has_resolve = false;
      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            uint32_t a = desc->pResolveAttachments[j].attachment;
            subpass->resolve_attachments[j] = (struct tu_subpass_attachment) {
               .attachment = desc->pResolveAttachments[j].attachment,
               .layout = desc->pResolveAttachments[j].layout,
            };
            if (a != VK_ATTACHMENT_UNUSED) {
               subpass->has_resolve = true;
               pass->attachments[desc->pResolveAttachments[j].attachment]
                  .view_mask |= subpass->view_mask;
            }
         }
      }

      if (desc->pDepthStencilAttachment) {
         subpass->depth_stencil_attachment = (struct tu_subpass_attachment) {
            .attachment = desc->pDepthStencilAttachment->attachment,
            .layout = desc->pDepthStencilAttachment->layout,
         };
         if (desc->pDepthStencilAttachment->attachment !=
             VK_ATTACHMENT_UNUSED) {
            pass->attachments[desc->pDepthStencilAttachment->attachment]
               .view_mask |= subpass->view_mask;
            depth_sample_count =
               pCreateInfo
                  ->pAttachments[desc->pDepthStencilAttachment->attachment]
                  .samples;
         }
      } else {
         subpass->depth_stencil_attachment.attachment = VK_ATTACHMENT_UNUSED;
      }

      subpass->max_sample_count =
         MAX2(color_sample_count, depth_sample_count);
   }

   for (unsigned i = 0; i < pCreateInfo->dependencyCount; ++i) {
      uint32_t dst = pCreateInfo->pDependencies[i].dstSubpass;
      if (dst == VK_SUBPASS_EXTERNAL) {
         pass->end_barrier.src_stage_mask =
            pCreateInfo->pDependencies[i].srcStageMask;
         pass->end_barrier.src_access_mask =
            pCreateInfo->pDependencies[i].srcAccessMask;
         pass->end_barrier.dst_access_mask =
            pCreateInfo->pDependencies[i].dstAccessMask;
      } else {
         pass->subpasses[dst].start_barrier.src_stage_mask =
            pCreateInfo->pDependencies[i].srcStageMask;
         pass->subpasses[dst].start_barrier.src_access_mask =
            pCreateInfo->pDependencies[i].srcAccessMask;
         pass->subpasses[dst].start_barrier.dst_access_mask =
            pCreateInfo->pDependencies[i].dstAccessMask;
      }
   }

   *pRenderPass = tu_render_pass_to_handle(pass);

   return VK_SUCCESS;
}

VkResult
tu_CreateRenderPass2KHR(VkDevice _device,
                        const VkRenderPassCreateInfo2KHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkRenderPass *pRenderPass)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_render_pass *pass;
   size_t size;
   size_t attachments_offset;

   assert(pCreateInfo->sType ==
          VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2_KHR);

   size = sizeof(*pass);
   size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
   attachments_offset = size;
   size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

   pass = vk_alloc2(&device->alloc, pAllocator, size, 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(pass, 0, size);
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = (void *) pass + attachments_offset;

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];

      att->format = pCreateInfo->pAttachments[i].format;
      att->samples = pCreateInfo->pAttachments[i].samples;
      att->load_op = pCreateInfo->pAttachments[i].loadOp;
      att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
      att->initial_layout = pCreateInfo->pAttachments[i].initialLayout;
      att->final_layout = pCreateInfo->pAttachments[i].finalLayout;
      // att->store_op = pCreateInfo->pAttachments[i].storeOp;
      // att->stencil_store_op = pCreateInfo->pAttachments[i].stencilStoreOp;
   }
   uint32_t subpass_attachment_count = 0;
   struct tu_subpass_attachment *p;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2KHR *desc = &pCreateInfo->pSubpasses[i];

      subpass_attachment_count +=
         desc->inputAttachmentCount + desc->colorAttachmentCount +
         (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
         (desc->pDepthStencilAttachment != NULL);
   }

   if (subpass_attachment_count) {
      pass->subpass_attachments = vk_alloc2(
         &device->alloc, pAllocator,
         subpass_attachment_count * sizeof(struct tu_subpass_attachment), 8,
         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (pass->subpass_attachments == NULL) {
         vk_free2(&device->alloc, pAllocator, pass);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else
      pass->subpass_attachments = NULL;

   p = pass->subpass_attachments;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2KHR *desc = &pCreateInfo->pSubpasses[i];
      uint32_t color_sample_count = 1, depth_sample_count = 1;
      struct tu_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->view_mask = desc->viewMask;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = p;
         p += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            subpass->input_attachments[j] = (struct tu_subpass_attachment) {
               .attachment = desc->pInputAttachments[j].attachment,
               .layout = desc->pInputAttachments[j].layout,
            };
            if (desc->pInputAttachments[j].attachment != VK_ATTACHMENT_UNUSED)
               pass->attachments[desc->pInputAttachments[j].attachment]
                  .view_mask |= subpass->view_mask;
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->color_attachments[j] = (struct tu_subpass_attachment) {
               .attachment = desc->pColorAttachments[j].attachment,
               .layout = desc->pColorAttachments[j].layout,
            };
            if (desc->pColorAttachments[j].attachment !=
                VK_ATTACHMENT_UNUSED) {
               pass->attachments[desc->pColorAttachments[j].attachment]
                  .view_mask |= subpass->view_mask;
               color_sample_count =
                  pCreateInfo
                     ->pAttachments[desc->pColorAttachments[j].attachment]
                     .samples;
            }
         }
      }

      subpass->has_resolve = false;
      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            uint32_t a = desc->pResolveAttachments[j].attachment;
            subpass->resolve_attachments[j] = (struct tu_subpass_attachment) {
               .attachment = desc->pResolveAttachments[j].attachment,
               .layout = desc->pResolveAttachments[j].layout,
            };
            if (a != VK_ATTACHMENT_UNUSED) {
               subpass->has_resolve = true;
               pass->attachments[desc->pResolveAttachments[j].attachment]
                  .view_mask |= subpass->view_mask;
            }
         }
      }

      if (desc->pDepthStencilAttachment) {
         subpass->depth_stencil_attachment = (struct tu_subpass_attachment) {
            .attachment = desc->pDepthStencilAttachment->attachment,
            .layout = desc->pDepthStencilAttachment->layout,
         };
         if (desc->pDepthStencilAttachment->attachment !=
             VK_ATTACHMENT_UNUSED) {
            pass->attachments[desc->pDepthStencilAttachment->attachment]
               .view_mask |= subpass->view_mask;
            depth_sample_count =
               pCreateInfo
                  ->pAttachments[desc->pDepthStencilAttachment->attachment]
                  .samples;
         }
      } else {
         subpass->depth_stencil_attachment.attachment = VK_ATTACHMENT_UNUSED;
      }

      subpass->max_sample_count =
         MAX2(color_sample_count, depth_sample_count);
   }

   for (unsigned i = 0; i < pCreateInfo->dependencyCount; ++i) {
      uint32_t dst = pCreateInfo->pDependencies[i].dstSubpass;
      if (dst == VK_SUBPASS_EXTERNAL) {
         pass->end_barrier.src_stage_mask =
            pCreateInfo->pDependencies[i].srcStageMask;
         pass->end_barrier.src_access_mask =
            pCreateInfo->pDependencies[i].srcAccessMask;
         pass->end_barrier.dst_access_mask =
            pCreateInfo->pDependencies[i].dstAccessMask;
      } else {
         pass->subpasses[dst].start_barrier.src_stage_mask =
            pCreateInfo->pDependencies[i].srcStageMask;
         pass->subpasses[dst].start_barrier.src_access_mask =
            pCreateInfo->pDependencies[i].srcAccessMask;
         pass->subpasses[dst].start_barrier.dst_access_mask =
            pCreateInfo->pDependencies[i].dstAccessMask;
      }
   }

   *pRenderPass = tu_render_pass_to_handle(pass);

   return VK_SUCCESS;
}

void
tu_DestroyRenderPass(VkDevice _device,
                     VkRenderPass _pass,
                     const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_render_pass, pass, _pass);

   if (!_pass)
      return;
   vk_free2(&device->alloc, pAllocator, pass->subpass_attachments);
   vk_free2(&device->alloc, pAllocator, pass);
}

void
tu_GetRenderAreaGranularity(VkDevice _device,
                            VkRenderPass renderPass,
                            VkExtent2D *pGranularity)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   pGranularity->width = device->physical_device->tile_align_w;
   pGranularity->height = device->physical_device->tile_align_h;
}
