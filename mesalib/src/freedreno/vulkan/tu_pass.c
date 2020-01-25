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
#include "vk_format.h"

static void update_samples(struct tu_subpass *subpass,
                           VkSampleCountFlagBits samples)
{
   assert(subpass->samples == 0 || subpass->samples == samples);
   subpass->samples = samples;
}

#define GMEM_ALIGN 0x4000

static void
compute_gmem_offsets(struct tu_render_pass *pass, uint32_t gmem_size)
{
   /* calculate total bytes per pixel */
   uint32_t cpp_total = 0;
   for (uint32_t i = 0; i < pass->attachment_count; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];
      if (att->gmem_offset >= 0)
         cpp_total += att->cpp;
   }

   /* no gmem attachments */
   if (cpp_total == 0) {
      /* any value non-zero value so tiling config works with no attachments */
      pass->gmem_pixels = 1024*1024;
      return;
   }

   /* TODO: this algorithm isn't optimal
    * for example, two attachments with cpp = {1, 4}
    * result:  nblocks = {12, 52}, pixels = 196608
    * optimal: nblocks = {13, 51}, pixels = 208896
    */
   uint32_t gmem_blocks = gmem_size / GMEM_ALIGN;
   uint32_t offset = 0, pixels = ~0u;
   for (uint32_t i = 0; i < pass->attachment_count; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];
      if (att->gmem_offset < 0)
         continue;

      att->gmem_offset = offset;

      /* Note: divide by 16 is for GMEM_ALIGN=16k, tile align w=64/h=16 */
      uint32_t align = MAX2(1, att->cpp / 16);
      uint32_t nblocks = MAX2((gmem_blocks * att->cpp / cpp_total) & ~(align - 1), align);

      gmem_blocks -= nblocks;
      cpp_total -= att->cpp;
      offset += nblocks * GMEM_ALIGN;
      pixels = MIN2(pixels, nblocks * GMEM_ALIGN / att->cpp);
   }

   pass->gmem_pixels = pixels;
   assert(pixels);
}

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

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];

      att->format = pCreateInfo->pAttachments[i].format;
      att->cpp = vk_format_get_blocksize(att->format) *
                           pCreateInfo->pAttachments[i].samples;
      att->load_op = pCreateInfo->pAttachments[i].loadOp;
      att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
      att->store_op = pCreateInfo->pAttachments[i].storeOp;
      if (pCreateInfo->pAttachments[i].stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE &&
          vk_format_has_stencil(att->format))
         att->store_op = VK_ATTACHMENT_STORE_OP_STORE;
      att->gmem_offset = -1;
   }

   uint32_t subpass_attachment_count = 0;
   struct tu_subpass_attachment *p;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];

      subpass_attachment_count +=
         desc->inputAttachmentCount + desc->colorAttachmentCount +
         (desc->pResolveAttachments ? desc->colorAttachmentCount : 0);
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
      struct tu_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->samples = 0;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = p;
         p += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            uint32_t a = desc->pInputAttachments[j].attachment;
            subpass->input_attachments[j].attachment = a;
            if (a != VK_ATTACHMENT_UNUSED)
               pass->attachments[a].gmem_offset = 0;
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            uint32_t a = desc->pColorAttachments[j].attachment;
            subpass->color_attachments[j].attachment = a;

            if (a != VK_ATTACHMENT_UNUSED) {
               pass->attachments[a].gmem_offset = 0;
               update_samples(subpass, pCreateInfo->pAttachments[a].samples);
            }
         }
      }

      subpass->resolve_attachments = desc->pResolveAttachments ? p : NULL;
      if (desc->pResolveAttachments) {
         p += desc->colorAttachmentCount;
         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->resolve_attachments[j].attachment =
                  desc->pResolveAttachments[j].attachment;
         }
      }

      uint32_t a = desc->pDepthStencilAttachment ?
         desc->pDepthStencilAttachment->attachment : VK_ATTACHMENT_UNUSED;
      subpass->depth_stencil_attachment.attachment = a;
      if (a != VK_ATTACHMENT_UNUSED) {
            pass->attachments[a].gmem_offset = 0;
            update_samples(subpass, pCreateInfo->pAttachments[a].samples);
      }

      subpass->samples = subpass->samples ?: 1;
   }

   *pRenderPass = tu_render_pass_to_handle(pass);

   compute_gmem_offsets(pass, device->physical_device->gmem_size);

   return VK_SUCCESS;
}

VkResult
tu_CreateRenderPass2(VkDevice _device,
                     const VkRenderPassCreateInfo2KHR *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkRenderPass *pRenderPass)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_render_pass *pass;
   size_t size;
   size_t attachments_offset;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2_KHR);

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
      att->cpp = vk_format_get_blocksize(att->format) *
                           pCreateInfo->pAttachments[i].samples;
      att->load_op = pCreateInfo->pAttachments[i].loadOp;
      att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
      att->store_op = pCreateInfo->pAttachments[i].storeOp;
      att->stencil_store_op = pCreateInfo->pAttachments[i].stencilStoreOp;
      if (pCreateInfo->pAttachments[i].stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE &&
          vk_format_has_stencil(att->format))
         att->store_op = VK_ATTACHMENT_STORE_OP_STORE;
      att->gmem_offset = -1;
   }
   uint32_t subpass_attachment_count = 0;
   struct tu_subpass_attachment *p;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2KHR *desc = &pCreateInfo->pSubpasses[i];

      subpass_attachment_count +=
         desc->inputAttachmentCount + desc->colorAttachmentCount +
         (desc->pResolveAttachments ? desc->colorAttachmentCount : 0);
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
      struct tu_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->samples = 0;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = p;
         p += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            uint32_t a = desc->pInputAttachments[j].attachment;
            subpass->input_attachments[j].attachment = a;
            if (a != VK_ATTACHMENT_UNUSED)
               pass->attachments[a].gmem_offset = 0;
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            uint32_t a = desc->pColorAttachments[j].attachment;
            subpass->color_attachments[j].attachment = a;

            if (a != VK_ATTACHMENT_UNUSED) {
               pass->attachments[a].gmem_offset = 0;
               update_samples(subpass, pCreateInfo->pAttachments[a].samples);
            }
         }
      }

      subpass->resolve_attachments = desc->pResolveAttachments ? p : NULL;
      if (desc->pResolveAttachments) {
         p += desc->colorAttachmentCount;
         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->resolve_attachments[j].attachment =
                  desc->pResolveAttachments[j].attachment;
         }
      }


      uint32_t a = desc->pDepthStencilAttachment ?
         desc->pDepthStencilAttachment->attachment : VK_ATTACHMENT_UNUSED;
      subpass->depth_stencil_attachment.attachment = a;
      if (a != VK_ATTACHMENT_UNUSED) {
            pass->attachments[a].gmem_offset = 0;
            update_samples(subpass, pCreateInfo->pAttachments[a].samples);
      }

      subpass->samples = subpass->samples ?: 1;
   }

   *pRenderPass = tu_render_pass_to_handle(pass);

   compute_gmem_offsets(pass, device->physical_device->gmem_size);

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
