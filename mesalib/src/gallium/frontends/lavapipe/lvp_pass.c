/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"

static void
lvp_render_pass_compile(struct lvp_render_pass *pass)
{
   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      struct lvp_subpass *subpass = &pass->subpasses[i];

      for (uint32_t j = 0; j < subpass->attachment_count; j++) {
         struct lvp_subpass_attachment *subpass_att =
            &subpass->attachments[j];
         if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
            continue;

         struct lvp_render_pass_attachment *pass_att =
            &pass->attachments[subpass_att->attachment];

         pass_att->first_subpass_idx = UINT32_MAX;
      }
   }

   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      struct lvp_subpass *subpass = &pass->subpasses[i];
      uint32_t color_sample_count = 1, depth_sample_count = 1;

      /* We don't allow depth_stencil_attachment to be non-NULL and
       * be VK_ATTACHMENT_UNUSED.  This way something can just check
       * for NULL and be guaranteed that they have a valid
       * attachment.
       */
      if (subpass->depth_stencil_attachment &&
          subpass->depth_stencil_attachment->attachment == VK_ATTACHMENT_UNUSED)
         subpass->depth_stencil_attachment = NULL;

      if (subpass->ds_resolve_attachment &&
          subpass->ds_resolve_attachment->attachment == VK_ATTACHMENT_UNUSED)
         subpass->ds_resolve_attachment = NULL;

      for (uint32_t j = 0; j < subpass->attachment_count; j++) {
         struct lvp_subpass_attachment *subpass_att =
            &subpass->attachments[j];
         if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
            continue;

         struct lvp_render_pass_attachment *pass_att =
            &pass->attachments[subpass_att->attachment];

         if (i < pass_att->first_subpass_idx)
            pass_att->first_subpass_idx = i;
         pass_att->last_subpass_idx = i;
      }

      subpass->has_color_att = false;
      for (uint32_t j = 0; j < subpass->color_count; j++) {
         struct lvp_subpass_attachment *subpass_att =
            &subpass->color_attachments[j];
         if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
            continue;

         subpass->has_color_att = true;

         struct lvp_render_pass_attachment *pass_att =
            &pass->attachments[subpass_att->attachment];

         color_sample_count = pass_att->samples;
      }

      if (subpass->depth_stencil_attachment) {
         const uint32_t a =
            subpass->depth_stencil_attachment->attachment;
         struct lvp_render_pass_attachment *pass_att =
            &pass->attachments[a];
         depth_sample_count = pass_att->samples;
      }

      subpass->max_sample_count = MAX2(color_sample_count,
                                       depth_sample_count);

      /* We have to handle resolve attachments specially */
      subpass->has_color_resolve = false;
      if (subpass->resolve_attachments) {
         for (uint32_t j = 0; j < subpass->color_count; j++) {
            struct lvp_subpass_attachment *resolve_att =
               &subpass->resolve_attachments[j];

            if (resolve_att->attachment == VK_ATTACHMENT_UNUSED)
               continue;

            subpass->has_color_resolve = true;
         }
      }

      for (uint32_t j = 0; j < subpass->input_count; ++j) {
         if (subpass->input_attachments[j].attachment == VK_ATTACHMENT_UNUSED)
            continue;

         for (uint32_t k = 0; k < subpass->color_count; ++k) {
            if (subpass->color_attachments[k].attachment == subpass->input_attachments[j].attachment) {
               subpass->input_attachments[j].in_render_loop = true;
               subpass->color_attachments[k].in_render_loop = true;
            }
         }

         if (subpass->depth_stencil_attachment &&
             subpass->depth_stencil_attachment->attachment == subpass->input_attachments[j].attachment) {
            subpass->input_attachments[j].in_render_loop = true;
            subpass->depth_stencil_attachment->in_render_loop = true;
         }
      }
   }
}

static unsigned
lvp_num_subpass_attachments(const VkSubpassDescription *desc)
{
   return desc->inputAttachmentCount +
      desc->colorAttachmentCount +
      (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
      (desc->pDepthStencilAttachment != NULL);
}

VkResult lvp_CreateRenderPass(
   VkDevice                                    _device,
   const VkRenderPassCreateInfo*               pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkRenderPass*                               pRenderPass)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_render_pass *pass;
   size_t size;
   size_t attachments_offset;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);

   size = sizeof(*pass);
   size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
   attachments_offset = size;
   size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

   pass = vk_alloc2(&device->vk.alloc, pAllocator, size, 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Clear the subpasses along with the parent pass. This required because
    * each array member of lvp_subpass must be a valid pointer if not NULL.
    */
   memset(pass, 0, size);

   vk_object_base_init(&device->vk, &pass->base,
                       VK_OBJECT_TYPE_RENDER_PASS);
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = (void *) pass + attachments_offset;

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      struct lvp_render_pass_attachment *att = &pass->attachments[i];

      att->format = pCreateInfo->pAttachments[i].format;
      att->samples = pCreateInfo->pAttachments[i].samples;
      att->load_op = pCreateInfo->pAttachments[i].loadOp;
      att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
      att->final_layout = pCreateInfo->pAttachments[i].finalLayout;
      att->first_subpass_idx = UINT32_MAX;
   }

   uint32_t subpass_attachment_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      subpass_attachment_count += lvp_num_subpass_attachments(&pCreateInfo->pSubpasses[i]);
   }

   if (subpass_attachment_count) {
      pass->subpass_attachments =
         vk_alloc2(&device->vk.alloc, pAllocator,
                   subpass_attachment_count * sizeof(struct lvp_subpass_attachment), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (pass->subpass_attachments == NULL) {
         vk_free2(&device->vk.alloc, pAllocator, pass);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else
      pass->subpass_attachments = NULL;

   struct lvp_subpass_attachment *p = pass->subpass_attachments;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];
      struct lvp_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->attachment_count = lvp_num_subpass_attachments(desc);
      subpass->attachments = p;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = p;
         p += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            subpass->input_attachments[j] = (struct lvp_subpass_attachment) {
               .attachment = desc->pInputAttachments[j].attachment,
               .layout = desc->pInputAttachments[j].layout,
            };
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->color_attachments[j] = (struct lvp_subpass_attachment) {
               .attachment = desc->pColorAttachments[j].attachment,
               .layout = desc->pColorAttachments[j].layout,
            };
         }
      }

      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->resolve_attachments[j] = (struct lvp_subpass_attachment) {
               .attachment = desc->pResolveAttachments[j].attachment,
               .layout = desc->pResolveAttachments[j].layout,
            };
         }
      }

      if (desc->pDepthStencilAttachment) {
         subpass->depth_stencil_attachment = p++;

         *subpass->depth_stencil_attachment = (struct lvp_subpass_attachment) {
            .attachment = desc->pDepthStencilAttachment->attachment,
            .layout = desc->pDepthStencilAttachment->layout,
         };
      }
   }

   lvp_render_pass_compile(pass);
   *pRenderPass = lvp_render_pass_to_handle(pass);

   return VK_SUCCESS;
}

void lvp_DestroyRenderPass(
   VkDevice                                    _device,
   VkRenderPass                                _pass,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_render_pass, pass, _pass);

   if (!_pass)
      return;
   vk_object_base_finish(&pass->base);
   vk_free2(&device->vk.alloc, pAllocator, pass->subpass_attachments);
   vk_free2(&device->vk.alloc, pAllocator, pass);
}

void lvp_GetRenderAreaGranularity(
   VkDevice                                    device,
   VkRenderPass                                renderPass,
   VkExtent2D*                                 pGranularity)
{
   *pGranularity = (VkExtent2D) { 1, 1 };
}
