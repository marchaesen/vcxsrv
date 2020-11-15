/*
 * Copyright © 2019 Raspberry Pi
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

#include "v3dv_private.h"
#include "vk_format_info.h"

static uint32_t
num_subpass_attachments(const VkSubpassDescription *desc)
{
   return desc->inputAttachmentCount +
          desc->colorAttachmentCount +
          (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
          (desc->pDepthStencilAttachment != NULL);
}

static void
set_use_tlb_resolve(struct v3dv_render_pass_attachment *att)
{
   const struct v3dv_format *format = v3dv_get_format(att->desc.format);
   att->use_tlb_resolve = v3dv_format_supports_tlb_resolve(format);
}

static void
pass_find_subpass_range_for_attachments(struct v3dv_render_pass *pass)
{
   for (uint32_t i = 0; i < pass->attachment_count; i++) {
      pass->attachments[i].first_subpass = pass->subpass_count - 1;
      pass->attachments[i].last_subpass = 0;
   }

   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      const struct v3dv_subpass *subpass = &pass->subpasses[i];

      for (uint32_t j = 0; j < subpass->color_count; j++) {
         uint32_t attachment_idx = subpass->color_attachments[j].attachment;
         if (attachment_idx == VK_ATTACHMENT_UNUSED)
            continue;

         if (i < pass->attachments[attachment_idx].first_subpass)
            pass->attachments[attachment_idx].first_subpass = i;
         if (i > pass->attachments[attachment_idx].last_subpass)
            pass->attachments[attachment_idx].last_subpass = i;

         if (subpass->resolve_attachments &&
             subpass->resolve_attachments[j].attachment != VK_ATTACHMENT_UNUSED) {
            set_use_tlb_resolve(&pass->attachments[attachment_idx]);
         }
      }

      uint32_t ds_attachment_idx = subpass->ds_attachment.attachment;
      if (ds_attachment_idx != VK_ATTACHMENT_UNUSED) {
         if (i < pass->attachments[ds_attachment_idx].first_subpass)
            pass->attachments[ds_attachment_idx].first_subpass = i;
         if (i > pass->attachments[ds_attachment_idx].last_subpass)
            pass->attachments[ds_attachment_idx].last_subpass = i;
      }

      for (uint32_t j = 0; j < subpass->input_count; j++) {
         uint32_t input_attachment_idx = subpass->input_attachments[j].attachment;
         if (input_attachment_idx == VK_ATTACHMENT_UNUSED)
            continue;
         if (i < pass->attachments[input_attachment_idx].first_subpass)
            pass->attachments[input_attachment_idx].first_subpass = i;
         if (i > pass->attachments[input_attachment_idx].last_subpass)
            pass->attachments[input_attachment_idx].last_subpass = i;
      }

      if (subpass->resolve_attachments) {
         for (uint32_t j = 0; j < subpass->color_count; j++) {
            uint32_t attachment_idx = subpass->resolve_attachments[j].attachment;
            if (attachment_idx == VK_ATTACHMENT_UNUSED)
               continue;
            if (i < pass->attachments[attachment_idx].first_subpass)
               pass->attachments[attachment_idx].first_subpass = i;
            if (i > pass->attachments[attachment_idx].last_subpass)
               pass->attachments[attachment_idx].last_subpass = i;
         }
      }
   }
}


VkResult
v3dv_CreateRenderPass(VkDevice _device,
                      const VkRenderPassCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkRenderPass *pRenderPass)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_render_pass *pass;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);

   size_t size = sizeof(*pass);
   size_t subpasses_offset = size;
   size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
   size_t attachments_offset = size;
   size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

   pass = vk_alloc2(&device->alloc, pAllocator, size, 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(pass, 0, size);
   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->attachments = (void *) pass + attachments_offset;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->subpasses = (void *) pass + subpasses_offset;

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++)
      pass->attachments[i].desc = pCreateInfo->pAttachments[i];

   uint32_t subpass_attachment_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];
      subpass_attachment_count += num_subpass_attachments(desc);
   }

   if (subpass_attachment_count) {
      const size_t subpass_attachment_bytes =
         subpass_attachment_count * sizeof(struct v3dv_subpass_attachment);
      pass->subpass_attachments =
         vk_alloc2(&device->alloc, pAllocator, subpass_attachment_bytes, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (pass->subpass_attachments == NULL) {
         vk_free2(&device->alloc, pAllocator, pass);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else {
      pass->subpass_attachments = NULL;
   }

   struct v3dv_subpass_attachment *p = pass->subpass_attachments;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];
      struct v3dv_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = p;
         p += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            subpass->input_attachments[j] = (struct v3dv_subpass_attachment) {
               .attachment = desc->pInputAttachments[j].attachment,
               .layout = desc->pInputAttachments[j].layout,
            };
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            const uint32_t attachment_idx =
               desc->pColorAttachments[j].attachment;
            subpass->color_attachments[j] = (struct v3dv_subpass_attachment) {
               .attachment = attachment_idx,
               .layout = desc->pColorAttachments[j].layout,
            };
            if (attachment_idx != VK_ATTACHMENT_UNUSED) {
               VkFormat format = pass->attachments[attachment_idx].desc.format;
               subpass->has_srgb_rt |= vk_format_is_srgb(format);
            }
         }
      }

      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->resolve_attachments[j] = (struct v3dv_subpass_attachment) {
               .attachment = desc->pResolveAttachments[j].attachment,
               .layout = desc->pResolveAttachments[j].layout,
            };
         }
      }

      if (desc->pDepthStencilAttachment) {
         subpass->ds_attachment = (struct v3dv_subpass_attachment) {
            .attachment = desc->pDepthStencilAttachment->attachment,
            .layout = desc->pDepthStencilAttachment->layout,
         };

         /* GFXH-1461: if depth is cleared but stencil is loaded (or viceversa),
          * the clear might get lost. If a subpass has this then we can't emit
          * the clear using the TLB and we have to do it as a draw call.
          *
          * FIXME: separate stencil.
          */
         if (subpass->ds_attachment.attachment != VK_ATTACHMENT_UNUSED) {
            struct v3dv_render_pass_attachment *att =
               &pass->attachments[subpass->ds_attachment.attachment];
            if (att->desc.format == VK_FORMAT_D24_UNORM_S8_UINT) {
               if (att->desc.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR &&
                   att->desc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
                  subpass->do_depth_clear_with_draw = true;
               } else if (att->desc.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD &&
                          att->desc.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                  subpass->do_stencil_clear_with_draw = true;
               }
            }
         }
      } else {
         subpass->ds_attachment.attachment = VK_ATTACHMENT_UNUSED;
      }
   }

   pass_find_subpass_range_for_attachments(pass);

   /* FIXME: handle subpass dependencies */

   *pRenderPass = v3dv_render_pass_to_handle(pass);

   return VK_SUCCESS;
}

void
v3dv_DestroyRenderPass(VkDevice _device,
                       VkRenderPass _pass,
                       const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_render_pass, pass, _pass);

   if (!_pass)
      return;

   vk_free2(&device->alloc, pAllocator, pass->subpass_attachments);
   vk_free2(&device->alloc, pAllocator, pass);
}

static void
subpass_get_granularity(struct v3dv_render_pass *pass,
                        uint32_t subpass_idx,
                        VkExtent2D *granularity)
{
   static const uint8_t tile_sizes[] = {
      64, 64,
      64, 32,
      32, 32,
      32, 16,
      16, 16,
      16,  8,
       8,  8
   };

   /* Our tile size depends on the number of color attachments and the maximum
    * bpp across them.
    */
   assert(subpass_idx < pass->subpass_count);
   struct v3dv_subpass *subpass = &pass->subpasses[subpass_idx];
   const uint32_t color_attachment_count = subpass->color_count;

   uint32_t max_internal_bpp = 0;
   for (uint32_t i = 0; i < color_attachment_count; i++) {
      uint32_t attachment_idx = subpass->color_attachments[i].attachment;
      if (attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;
      const VkAttachmentDescription *desc =
         &pass->attachments[attachment_idx].desc;
      const struct v3dv_format *format = v3dv_get_format(desc->format);
      uint32_t internal_type, internal_bpp;
      v3dv_get_internal_type_bpp_for_output_format(format->rt_type,
                                                   &internal_type,
                                                   &internal_bpp);
      max_internal_bpp = MAX2(max_internal_bpp, internal_bpp);
   }

   uint32_t idx = 0;
   if (color_attachment_count > 2)
      idx += 2;
   else if (color_attachment_count > 1)
      idx += 1;

   idx += max_internal_bpp;

   assert(idx < ARRAY_SIZE(tile_sizes));
   *granularity = (VkExtent2D) {
      .width = tile_sizes[idx * 2],
      .height = tile_sizes[idx * 2 + 1]
   };
}

void
v3dv_GetRenderAreaGranularity(VkDevice device,
                              VkRenderPass renderPass,
                              VkExtent2D *pGranularity)
{
   V3DV_FROM_HANDLE(v3dv_render_pass, pass, renderPass);

   *pGranularity = (VkExtent2D) {
      .width = 64,
      .height = 64,
   };

   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      VkExtent2D sg;
      subpass_get_granularity(pass, i, &sg);
      pGranularity->width = MIN2(pGranularity->width, sg.width);
      pGranularity->height = MIN2(pGranularity->height, sg.height);
   }
}

/* Checks whether the render area rectangle covers a region that is aligned to
 * tile boundaries. This means that we are writing to all pixels covered by
 * all tiles in that area (except for pixels on edge tiles that are outside
 * the framebuffer dimensions).
 *
 * When our framebuffer is aligned to tile boundaries we know we are writing
 * valid data to all all pixels in each tile and we can apply certain
 * optimizations, like avoiding tile loads, since we know that none of the
 * original pixel values in each tile for that area need to be preserved.
 * We also use this to decide if we can use TLB clears, as these clear whole
 * tiles so we can't use them if the render area is not aligned.
 *
 * Note that when an image is created it will possibly include padding blocks
 * depending on its tiling layout. When the framebuffer dimensions are not
 * aligned to tile boundaries then edge tiles are only partially covered by the
 * framebuffer pixels, but tile stores still seem to store full tiles
 * writing to the padded sections. This is important when the framebuffer
 * is aliasing a smaller section of a larger image, as in that case the edge
 * tiles of the framebuffer would overwrite valid pixels in the larger image.
 * In that case, we can't flag the area as being aligned.
 */
bool
v3dv_subpass_area_is_tile_aligned(const VkRect2D *area,
                                  struct v3dv_framebuffer *fb,
                                  struct v3dv_render_pass *pass,
                                  uint32_t subpass_idx)
{
   assert(subpass_idx < pass->subpass_count);

   VkExtent2D granularity;
   subpass_get_granularity(pass, subpass_idx, &granularity);

   return area->offset.x % granularity.width == 0 &&
          area->offset.y % granularity.height == 0 &&
         (area->extent.width % granularity.width == 0 ||
          (fb->has_edge_padding &&
           area->offset.x + area->extent.width >= fb->width)) &&
         (area->extent.height % granularity.height == 0 ||
          (fb->has_edge_padding &&
           area->offset.y + area->extent.height >= fb->height));
}
