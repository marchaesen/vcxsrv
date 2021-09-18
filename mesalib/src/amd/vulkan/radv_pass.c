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
#include "radv_private.h"

#include "vk_util.h"

static void
radv_render_pass_add_subpass_dep(struct radv_render_pass *pass, const VkSubpassDependency2 *dep)
{
   uint32_t src = dep->srcSubpass;
   uint32_t dst = dep->dstSubpass;

   /* Ignore subpass self-dependencies as they allow the app to call
    * vkCmdPipelineBarrier() inside the render pass and the driver should
    * only do the barrier when called, not when starting the render pass.
    */
   if (src == dst)
      return;

   /* Accumulate all ingoing external dependencies to the first subpass. */
   if (src == VK_SUBPASS_EXTERNAL)
      dst = 0;

   if (dst == VK_SUBPASS_EXTERNAL) {
      if (dep->dstStageMask != VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
         pass->end_barrier.src_stage_mask |= dep->srcStageMask;
      pass->end_barrier.src_access_mask |= dep->srcAccessMask;
      pass->end_barrier.dst_access_mask |= dep->dstAccessMask;
   } else {
      if (dep->dstStageMask != VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
         pass->subpasses[dst].start_barrier.src_stage_mask |= dep->srcStageMask;
      pass->subpasses[dst].start_barrier.src_access_mask |= dep->srcAccessMask;
      pass->subpasses[dst].start_barrier.dst_access_mask |= dep->dstAccessMask;
   }
}

static void
radv_render_pass_add_implicit_deps(struct radv_render_pass *pass)
{
   /* From the Vulkan 1.0.39 spec:
    *
    *    If there is no subpass dependency from VK_SUBPASS_EXTERNAL to the
    *    first subpass that uses an attachment, then an implicit subpass
    *    dependency exists from VK_SUBPASS_EXTERNAL to the first subpass it is
    *    used in. The implicit subpass dependency only exists if there
    *    exists an automatic layout transition away from initialLayout.
    *    The subpass dependency operates as if defined with the
    *    following parameters:
    *
    *    VkSubpassDependency implicitDependency = {
    *        .srcSubpass = VK_SUBPASS_EXTERNAL;
    *        .dstSubpass = firstSubpass; // First subpass attachment is used in
    *        .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    *        .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    *        .srcAccessMask = 0;
    *        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
    *                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    *        .dependencyFlags = 0;
    *    };
    *
    *    Similarly, if there is no subpass dependency from the last subpass
    *    that uses an attachment to VK_SUBPASS_EXTERNAL, then an implicit
    *    subpass dependency exists from the last subpass it is used in to
    *    VK_SUBPASS_EXTERNAL. The implicit subpass dependency only exists
    *    if there exists an automatic layout transition into finalLayout.
    *    The subpass dependency operates as if defined with the following
    *    parameters:
    *
    *    VkSubpassDependency implicitDependency = {
    *        .srcSubpass = lastSubpass; // Last subpass attachment is used in
    *        .dstSubpass = VK_SUBPASS_EXTERNAL;
    *        .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    *        .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    *        .srcAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
    *                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
    *                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    *        .dstAccessMask = 0;
    *        .dependencyFlags = 0;
    *    };
    */
   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      struct radv_subpass *subpass = &pass->subpasses[i];
      bool add_ingoing_dep = false, add_outgoing_dep = false;

      for (uint32_t j = 0; j < subpass->attachment_count; j++) {
         struct radv_subpass_attachment *subpass_att = &subpass->attachments[j];
         if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
            continue;

         struct radv_render_pass_attachment *pass_att = &pass->attachments[subpass_att->attachment];
         uint32_t initial_layout = pass_att->initial_layout;
         uint32_t stencil_initial_layout = pass_att->stencil_initial_layout;
         uint32_t final_layout = pass_att->final_layout;
         uint32_t stencil_final_layout = pass_att->stencil_final_layout;

         /* The implicit subpass dependency only exists if
          * there exists an automatic layout transition away
          * from initialLayout.
          */
         if (pass_att->first_subpass_idx == i && !subpass->has_ingoing_dep &&
             ((subpass_att->layout != initial_layout) ||
              (subpass_att->layout != stencil_initial_layout))) {
            add_ingoing_dep = true;
         }

         /* The implicit subpass dependency only exists if
          * there exists an automatic layout transition into
          * finalLayout.
          */
         if (pass_att->last_subpass_idx == i && !subpass->has_outgoing_dep &&
             ((subpass_att->layout != final_layout) ||
              (subpass_att->layout != stencil_final_layout))) {
            add_outgoing_dep = true;
         }
      }

      if (add_ingoing_dep) {
         const VkSubpassDependency2KHR implicit_ingoing_dep = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = i, /* first subpass attachment is used in */
            .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask =
               VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0,
         };

         radv_render_pass_add_subpass_dep(pass, &implicit_ingoing_dep);
      }

      if (add_outgoing_dep) {
         const VkSubpassDependency2KHR implicit_outgoing_dep = {
            .srcSubpass = i, /* last subpass attachment is used in */
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask =
               VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
            .dependencyFlags = 0,
         };

         radv_render_pass_add_subpass_dep(pass, &implicit_outgoing_dep);
      }
   }
}

static void
radv_render_pass_compile(struct radv_render_pass *pass)
{
   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      struct radv_subpass *subpass = &pass->subpasses[i];

      for (uint32_t j = 0; j < subpass->attachment_count; j++) {
         struct radv_subpass_attachment *subpass_att = &subpass->attachments[j];
         if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
            continue;

         struct radv_render_pass_attachment *pass_att = &pass->attachments[subpass_att->attachment];

         pass_att->first_subpass_idx = VK_SUBPASS_EXTERNAL;
         pass_att->last_subpass_idx = VK_SUBPASS_EXTERNAL;
      }
   }

   for (uint32_t i = 0; i < pass->subpass_count; i++) {
      struct radv_subpass *subpass = &pass->subpasses[i];
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

      if (subpass->vrs_attachment && subpass->vrs_attachment->attachment == VK_ATTACHMENT_UNUSED)
         subpass->vrs_attachment = NULL;

      for (uint32_t j = 0; j < subpass->attachment_count; j++) {
         struct radv_subpass_attachment *subpass_att = &subpass->attachments[j];
         if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
            continue;

         struct radv_render_pass_attachment *pass_att = &pass->attachments[subpass_att->attachment];

         if (i < pass_att->first_subpass_idx)
            pass_att->first_subpass_idx = i;
         pass_att->last_subpass_idx = i;
      }

      subpass->has_color_att = false;
      for (uint32_t j = 0; j < subpass->color_count; j++) {
         struct radv_subpass_attachment *subpass_att = &subpass->color_attachments[j];
         if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
            continue;

         subpass->has_color_att = true;

         struct radv_render_pass_attachment *pass_att = &pass->attachments[subpass_att->attachment];

         color_sample_count = pass_att->samples;
      }

      if (subpass->depth_stencil_attachment) {
         const uint32_t a = subpass->depth_stencil_attachment->attachment;
         struct radv_render_pass_attachment *pass_att = &pass->attachments[a];
         depth_sample_count = pass_att->samples;
      }

      subpass->max_sample_count = MAX2(color_sample_count, depth_sample_count);
      subpass->color_sample_count = color_sample_count;
      subpass->depth_sample_count = depth_sample_count;

      /* We have to handle resolve attachments specially */
      subpass->has_color_resolve = false;
      if (subpass->resolve_attachments) {
         for (uint32_t j = 0; j < subpass->color_count; j++) {
            struct radv_subpass_attachment *resolve_att = &subpass->resolve_attachments[j];

            if (resolve_att->attachment == VK_ATTACHMENT_UNUSED)
               continue;

            subpass->has_color_resolve = true;
         }
      }

      for (uint32_t j = 0; j < subpass->input_count; ++j) {
         if (subpass->input_attachments[j].attachment == VK_ATTACHMENT_UNUSED)
            continue;

         for (uint32_t k = 0; k < subpass->color_count; ++k) {
            if (subpass->color_attachments[k].attachment ==
                subpass->input_attachments[j].attachment) {
               subpass->input_attachments[j].in_render_loop = true;
               subpass->color_attachments[k].in_render_loop = true;
            }
         }

         if (subpass->depth_stencil_attachment && subpass->depth_stencil_attachment->attachment ==
                                                     subpass->input_attachments[j].attachment) {
            subpass->input_attachments[j].in_render_loop = true;
            subpass->depth_stencil_attachment->in_render_loop = true;
         }
      }
   }
}

static void
radv_destroy_render_pass(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                         struct radv_render_pass *pass)
{
   vk_object_base_finish(&pass->base);
   vk_free2(&device->vk.alloc, pAllocator, pass->subpass_attachments);
   vk_free2(&device->vk.alloc, pAllocator, pass);
}

static unsigned
radv_num_subpass_attachments2(const VkSubpassDescription2 *desc)
{
   const VkSubpassDescriptionDepthStencilResolve *ds_resolve =
      vk_find_struct_const(desc->pNext, SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE);
   const VkFragmentShadingRateAttachmentInfoKHR *vrs =
      vk_find_struct_const(desc->pNext, FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);

   return desc->inputAttachmentCount + desc->colorAttachmentCount +
          (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
          (desc->pDepthStencilAttachment != NULL) +
          (ds_resolve && ds_resolve->pDepthStencilResolveAttachment) +
          (vrs && vrs->pFragmentShadingRateAttachment);
}

static bool
vk_image_layout_depth_only(VkImageLayout layout)
{
   switch (layout) {
   case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
      return true;
   default:
      return false;
   }
}

/* From the Vulkan Specification 1.2.166 - VkAttachmentReference2:
 *
 * "If layout only specifies the layout of the depth aspect of the attachment,
 *  the layout of the stencil aspect is specified by the stencilLayout member
 *  of a VkAttachmentReferenceStencilLayout structure included in the pNext
 *  chain. Otherwise, layout describes the layout for all relevant image
 *  aspects."
 */
static VkImageLayout
stencil_ref_layout(const VkAttachmentReference2 *att_ref)
{
   if (!vk_image_layout_depth_only(att_ref->layout))
      return att_ref->layout;

   const VkAttachmentReferenceStencilLayoutKHR *stencil_ref =
      vk_find_struct_const(att_ref->pNext, ATTACHMENT_REFERENCE_STENCIL_LAYOUT_KHR);
   if (!stencil_ref)
      return VK_IMAGE_LAYOUT_UNDEFINED;

   return stencil_ref->stencilLayout;
}

/* From the Vulkan Specification 1.2.184:
 *
 * "If the pNext chain includes a VkAttachmentDescriptionStencilLayout structure, then the
 *  stencilInitialLayout and stencilFinalLayout members specify the initial and final layouts of the
 *  stencil aspect of a depth/stencil format, and initialLayout and finalLayout only apply to the
 *  depth aspect. For depth-only formats, the VkAttachmentDescriptionStencilLayout structure is
 *  ignored. For stencil-only formats, the initial and final layouts of the stencil aspect are taken
 *  from the VkAttachmentDescriptionStencilLayout structure if present, or initialLayout and
 *  finalLayout if not present."
 *
 * "If format is a depth/stencil format, and either initialLayout or finalLayout does not specify a
 *  layout for the stencil aspect, then the application must specify the initial and final layouts
 *  of the stencil aspect by including a VkAttachmentDescriptionStencilLayout structure in the pNext
 *  chain."
 */
static VkImageLayout
stencil_desc_layout(const VkAttachmentDescription2KHR *att_desc, bool final)
{
   const struct util_format_description *desc = vk_format_description(att_desc->format);
   if (!util_format_has_stencil(desc))
      return VK_IMAGE_LAYOUT_UNDEFINED;

   const VkAttachmentDescriptionStencilLayoutKHR *stencil_desc =
      vk_find_struct_const(att_desc->pNext, ATTACHMENT_DESCRIPTION_STENCIL_LAYOUT_KHR);

   if (stencil_desc)
      return final ? stencil_desc->stencilFinalLayout : stencil_desc->stencilInitialLayout;
   return final ? att_desc->finalLayout : att_desc->initialLayout;
}

VkResult
radv_CreateRenderPass2(VkDevice _device, const VkRenderPassCreateInfo2 *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_render_pass *pass;
   size_t size;
   size_t attachments_offset;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2);

   size = sizeof(*pass);
   size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
   attachments_offset = size;
   size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

   pass = vk_alloc2(&device->vk.alloc, pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pass == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(pass, 0, size);

   vk_object_base_init(&device->vk, &pass->base, VK_OBJECT_TYPE_RENDER_PASS);

   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = (struct radv_render_pass_attachment *)((uint8_t *)pass + attachments_offset);

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      struct radv_render_pass_attachment *att = &pass->attachments[i];

      att->format = pCreateInfo->pAttachments[i].format;
      att->samples = pCreateInfo->pAttachments[i].samples;
      att->load_op = pCreateInfo->pAttachments[i].loadOp;
      att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
      att->initial_layout = pCreateInfo->pAttachments[i].initialLayout;
      att->final_layout = pCreateInfo->pAttachments[i].finalLayout;
      att->stencil_initial_layout = stencil_desc_layout(&pCreateInfo->pAttachments[i], false);
      att->stencil_final_layout = stencil_desc_layout(&pCreateInfo->pAttachments[i], true);
      // att->store_op = pCreateInfo->pAttachments[i].storeOp;
      // att->stencil_store_op = pCreateInfo->pAttachments[i].stencilStoreOp;
   }
   uint32_t subpass_attachment_count = 0;
   struct radv_subpass_attachment *p;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      subpass_attachment_count += radv_num_subpass_attachments2(&pCreateInfo->pSubpasses[i]);
   }

   if (subpass_attachment_count) {
      pass->subpass_attachments =
         vk_alloc2(&device->vk.alloc, pAllocator,
                   subpass_attachment_count * sizeof(struct radv_subpass_attachment), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (pass->subpass_attachments == NULL) {
         radv_destroy_render_pass(device, pAllocator, pass);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else
      pass->subpass_attachments = NULL;

   p = pass->subpass_attachments;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2 *desc = &pCreateInfo->pSubpasses[i];
      struct radv_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->attachment_count = radv_num_subpass_attachments2(desc);
      subpass->attachments = p;
      subpass->view_mask = desc->viewMask;

      if (desc->inputAttachmentCount > 0) {
         subpass->input_attachments = p;
         p += desc->inputAttachmentCount;

         for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
            subpass->input_attachments[j] = (struct radv_subpass_attachment){
               .attachment = desc->pInputAttachments[j].attachment,
               .layout = desc->pInputAttachments[j].layout,
               .stencil_layout = stencil_ref_layout(&desc->pInputAttachments[j]),
            };
         }
      }

      if (desc->colorAttachmentCount > 0) {
         subpass->color_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->color_attachments[j] = (struct radv_subpass_attachment){
               .attachment = desc->pColorAttachments[j].attachment,
               .layout = desc->pColorAttachments[j].layout,
            };
         }
      }

      if (desc->pResolveAttachments) {
         subpass->resolve_attachments = p;
         p += desc->colorAttachmentCount;

         for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
            subpass->resolve_attachments[j] = (struct radv_subpass_attachment){
               .attachment = desc->pResolveAttachments[j].attachment,
               .layout = desc->pResolveAttachments[j].layout,
            };
         }
      }

      if (desc->pDepthStencilAttachment) {
         subpass->depth_stencil_attachment = p++;

         *subpass->depth_stencil_attachment = (struct radv_subpass_attachment){
            .attachment = desc->pDepthStencilAttachment->attachment,
            .layout = desc->pDepthStencilAttachment->layout,
            .stencil_layout = stencil_ref_layout(desc->pDepthStencilAttachment),
         };
      }

      const VkSubpassDescriptionDepthStencilResolve *ds_resolve =
         vk_find_struct_const(desc->pNext, SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE);

      if (ds_resolve && ds_resolve->pDepthStencilResolveAttachment) {
         subpass->ds_resolve_attachment = p++;

         *subpass->ds_resolve_attachment = (struct radv_subpass_attachment){
            .attachment = ds_resolve->pDepthStencilResolveAttachment->attachment,
            .layout = ds_resolve->pDepthStencilResolveAttachment->layout,
            .stencil_layout = stencil_ref_layout(ds_resolve->pDepthStencilResolveAttachment),
         };

         subpass->depth_resolve_mode = ds_resolve->depthResolveMode;
         subpass->stencil_resolve_mode = ds_resolve->stencilResolveMode;
      }

      const VkFragmentShadingRateAttachmentInfoKHR *vrs =
         vk_find_struct_const(desc->pNext, FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);

      if (vrs && vrs->pFragmentShadingRateAttachment) {
         subpass->vrs_attachment = p++;

         *subpass->vrs_attachment = (struct radv_subpass_attachment){
            .attachment = vrs->pFragmentShadingRateAttachment->attachment,
            .layout = vrs->pFragmentShadingRateAttachment->layout,
         };
      }
   }

   for (unsigned i = 0; i < pCreateInfo->dependencyCount; ++i) {
      const VkSubpassDependency2 *dep = &pCreateInfo->pDependencies[i];

      radv_render_pass_add_subpass_dep(pass, &pCreateInfo->pDependencies[i]);

      /* Determine if the subpass has explicit dependencies from/to
       * VK_SUBPASS_EXTERNAL.
       */
      if (dep->srcSubpass == VK_SUBPASS_EXTERNAL && dep->dstSubpass != VK_SUBPASS_EXTERNAL) {
         pass->subpasses[dep->dstSubpass].has_ingoing_dep = true;
      }

      if (dep->dstSubpass == VK_SUBPASS_EXTERNAL && dep->srcSubpass != VK_SUBPASS_EXTERNAL) {
         pass->subpasses[dep->srcSubpass].has_outgoing_dep = true;
      }
   }

   radv_render_pass_compile(pass);

   radv_render_pass_add_implicit_deps(pass);

   *pRenderPass = radv_render_pass_to_handle(pass);

   return VK_SUCCESS;
}

void
radv_DestroyRenderPass(VkDevice _device, VkRenderPass _pass,
                       const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_render_pass, pass, _pass);

   if (!_pass)
      return;

   radv_destroy_render_pass(device, pAllocator, pass);
}

void
radv_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D *pGranularity)
{
   pGranularity->width = 1;
   pGranularity->height = 1;
}
