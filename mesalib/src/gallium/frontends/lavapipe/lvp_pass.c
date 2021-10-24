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

#include "vk_util.h"

static unsigned
lvp_num_subpass_attachments2(const VkSubpassDescription2 *desc)
{
   const VkSubpassDescriptionDepthStencilResolve *ds_resolve =
      vk_find_struct_const(desc->pNext,
                           SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE);
   return desc->inputAttachmentCount +
      desc->colorAttachmentCount +
      (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
      (desc->pDepthStencilAttachment != NULL) +
      (ds_resolve && ds_resolve->pDepthStencilResolveAttachment);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateRenderPass2(
    VkDevice                                    _device,
    const VkRenderPassCreateInfo2*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_render_pass *pass;
   size_t attachments_offset;
   size_t size;

   uint32_t subpass_attachment_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      subpass_attachment_count += lvp_num_subpass_attachments2(&pCreateInfo->pSubpasses[i]);
   }

   size = sizeof(*pass);
   size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
   attachments_offset = size;
   size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);
   uint32_t subpass_attachment_offset = size;
   size += subpass_attachment_count * sizeof(void*);

   pass = vk_zalloc2(&device->vk.alloc, pAllocator, size, 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Clear the subpasses along with the parent pass. This required because
    * each array member of lvp_subpass must be a valid pointer if not NULL.
    */
   memset(pass, 0, size);

   vk_object_base_init(&device->vk, &pass->base,
                       VK_OBJECT_TYPE_RENDER_PASS);
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = (struct lvp_render_pass_attachment *)((char *)pass + attachments_offset);

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      struct lvp_render_pass_attachment *att = &pass->attachments[i];

      att->format = pCreateInfo->pAttachments[i].format;
      att->samples = pCreateInfo->pAttachments[i].samples;
      att->load_op = pCreateInfo->pAttachments[i].loadOp;
      att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
      att->attachment = i;

      bool is_zs = util_format_is_depth_or_stencil(lvp_vk_format_to_pipe_format(att->format));
      pass->has_zs_attachment |= is_zs;
      pass->has_color_attachment |= !is_zs;
   }

   uint32_t subpass_attachment_idx = 0;
#define ATTACHMENT_OFFSET (struct lvp_render_pass_attachment**)(((uint8_t*)pass) + subpass_attachment_offset + (subpass_attachment_idx * sizeof(void*)))
#define CHECK_UNUSED_ATTACHMENT(SRC, DST, IDX) do { \
      if (desc->SRC[IDX].attachment == VK_ATTACHMENT_UNUSED) \
         subpass->DST[IDX] = NULL; \
      else \
         subpass->DST[IDX] = &pass->attachments[desc->SRC[IDX].attachment]; \
   } while (0)

   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2 *desc = &pCreateInfo->pSubpasses[i];
      struct lvp_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->view_mask = desc->viewMask;
      subpass->has_color_resolve = false;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = ATTACHMENT_OFFSET;
         subpass_attachment_idx += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            CHECK_UNUSED_ATTACHMENT(pInputAttachments, input_attachments, j);
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = ATTACHMENT_OFFSET;
         subpass_attachment_idx += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            CHECK_UNUSED_ATTACHMENT(pColorAttachments, color_attachments, j);
         }
      }

      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = ATTACHMENT_OFFSET;
         subpass_attachment_idx += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            CHECK_UNUSED_ATTACHMENT(pResolveAttachments, resolve_attachments, j);
            if (subpass->resolve_attachments[j])
               subpass->has_color_resolve = true;
         }
      }

      if (desc->pDepthStencilAttachment) {
         subpass->depth_stencil_attachment = ATTACHMENT_OFFSET;
         subpass_attachment_idx++;

         CHECK_UNUSED_ATTACHMENT(pDepthStencilAttachment, depth_stencil_attachment, 0);
      }

      const VkSubpassDescriptionDepthStencilResolve *ds_resolve =
         vk_find_struct_const(desc->pNext, SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE);

      if (ds_resolve && ds_resolve->pDepthStencilResolveAttachment) {
         subpass->ds_resolve_attachment = ATTACHMENT_OFFSET;
         subpass_attachment_idx++;

         if (ds_resolve->pDepthStencilResolveAttachment->attachment == VK_ATTACHMENT_UNUSED)
            *subpass->ds_resolve_attachment = NULL;
         else
            *subpass->ds_resolve_attachment = &pass->attachments[ds_resolve->pDepthStencilResolveAttachment->attachment];

         subpass->depth_resolve_mode = ds_resolve->depthResolveMode;
         subpass->stencil_resolve_mode = ds_resolve->stencilResolveMode;
      }
   }
#undef ATTACHMENT_OFFSET
   *pRenderPass = lvp_render_pass_to_handle(pass);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyRenderPass(
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

VKAPI_ATTR void VKAPI_CALL lvp_GetRenderAreaGranularity(
   VkDevice                                    device,
   VkRenderPass                                renderPass,
   VkExtent2D*                                 pGranularity)
{
   *pGranularity = (VkExtent2D) { 1, 1 };
}
