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

/* Return true if we have to fallback to sysmem rendering because the
 * dependency can't be satisfied with tiled rendering.
 */

static bool
dep_invalid_for_gmem(const VkSubpassDependency2 *dep)
{
   /* External dependencies don't matter here. */
   if (dep->srcSubpass == VK_SUBPASS_EXTERNAL ||
       dep->dstSubpass == VK_SUBPASS_EXTERNAL)
      return false;

   /* We can conceptually break down the process of rewriting a sysmem
    * renderpass into a gmem one into two parts:
    *
    * 1. Split each draw and multisample resolve into N copies, one for each
    * bin. (If hardware binning, add one more copy where the FS is disabled
    * for the binning pass). This is always allowed because the vertex stage
    * is allowed to run an arbitrary number of times and there are no extra
    * ordering constraints within a draw.
    * 2. Take the last copy of the second-to-last draw and slide it down to
    * before the last copy of the last draw. Repeat for each earlier draw
    * until the draw pass for the last bin is complete, then repeat for each
    * earlier bin until we finish with the first bin.
    *
    * During this rearranging process, we can't slide draws past each other in
    * a way that breaks the subpass dependencies. For each draw, we must slide
    * it past (copies of) the rest of the draws in the renderpass. We can
    * slide a draw past another if there isn't a dependency between them, or
    * if the dependenc(ies) are dependencies between framebuffer-space stages
    * only with the BY_REGION bit set. Note that this includes
    * self-dependencies, since these may result in pipeline barriers that also
    * break the rearranging process.
    */

   /* This is straight from the Vulkan 1.2 spec, section 6.1.4 "Framebuffer
    * Region Dependencies":
    */
   const VkPipelineStageFlags framebuffer_space_stages =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

   return
      (dep->srcStageMask & ~framebuffer_space_stages) ||
      (dep->dstStageMask & ~framebuffer_space_stages) ||
      !(dep->dependencyFlags & VK_DEPENDENCY_BY_REGION_BIT);
}

static void
tu_render_pass_add_subpass_dep(struct tu_render_pass *pass,
                               const VkSubpassDependency2 *dep)
{
   uint32_t src = dep->srcSubpass;
   uint32_t dst = dep->dstSubpass;

   if (dep_invalid_for_gmem(dep))
      pass->gmem_pixels = 0;

   /* Ignore subpass self-dependencies as they allow the app to call
    * vkCmdPipelineBarrier() inside the render pass and the driver should only
    * do the barrier when called, not when starting the render pass.
    */
   if (src == dst)
      return;

   struct tu_subpass_barrier *src_barrier;
   if (src == VK_SUBPASS_EXTERNAL) {
      src_barrier = &pass->subpasses[0].start_barrier;
   } else if (src == pass->subpass_count - 1) {
      src_barrier = &pass->end_barrier;
   } else {
      src_barrier = &pass->subpasses[src + 1].start_barrier;
   }

   struct tu_subpass_barrier *dst_barrier;
   if (dst == VK_SUBPASS_EXTERNAL) {
      dst_barrier = &pass->end_barrier;
   } else {
      dst_barrier = &pass->subpasses[dst].start_barrier;
   }

   if (dep->dstStageMask != VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
      src_barrier->src_stage_mask |= dep->srcStageMask;
   src_barrier->src_access_mask |= dep->srcAccessMask;
   dst_barrier->dst_access_mask |= dep->dstAccessMask;
}

/* We currently only care about undefined layouts, because we have to
 * flush/invalidate CCU for those. PREINITIALIZED is the same thing as
 * UNDEFINED for anything not linear tiled, but we don't know yet whether the
 * images used are tiled, so just assume they are.
 */

static bool
layout_undefined(VkImageLayout layout)
{
   return layout == VK_IMAGE_LAYOUT_UNDEFINED ||
          layout == VK_IMAGE_LAYOUT_PREINITIALIZED;
}

/* This implements the following bit of spec text:
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
 *
 * Note: currently this is the only use we have for layout transitions,
 * besides needing to invalidate CCU at the beginning, so we also flag
 * transitions from UNDEFINED here.
 */
static void
tu_render_pass_add_implicit_deps(struct tu_render_pass *pass,
                                 const VkRenderPassCreateInfo2 *info)
{
   const VkAttachmentDescription2* att = info->pAttachments;
   bool has_external_src[info->subpassCount];
   bool has_external_dst[info->subpassCount];
   bool att_used[pass->attachment_count];

   memset(has_external_src, 0, sizeof(has_external_src));
   memset(has_external_dst, 0, sizeof(has_external_dst));

   for (uint32_t i = 0; i < info->dependencyCount; i++) {
      uint32_t src = info->pDependencies[i].srcSubpass;
      uint32_t dst = info->pDependencies[i].dstSubpass;

      if (src == dst)
         continue;

      if (src == VK_SUBPASS_EXTERNAL)
         has_external_src[dst] = true;
      if (dst == VK_SUBPASS_EXTERNAL)
         has_external_dst[src] = true;
   }

   memset(att_used, 0, sizeof(att_used));

   for (unsigned i = 0; i < info->subpassCount; i++) {
      if (!has_external_src[i])
         continue;

      const VkSubpassDescription2 *subpass = &info->pSubpasses[i];
      bool src_implicit_dep = false;

      for (unsigned j = 0; j < subpass->inputAttachmentCount; j++) {
         uint32_t a = subpass->pInputAttachments[j].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;
         if (att[a].initialLayout != subpass->pInputAttachments[j].layout && !att_used[a])
            src_implicit_dep = true;
         att_used[a] = true;
      }

      for (unsigned j = 0; j < subpass->colorAttachmentCount; j++) {
         uint32_t a = subpass->pColorAttachments[j].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;
         if (att[a].initialLayout != subpass->pColorAttachments[j].layout && !att_used[a])
            src_implicit_dep = true;
         att_used[a] = true;
      }

      if (subpass->pResolveAttachments) {
         for (unsigned j = 0; j < subpass->colorAttachmentCount; j++) {
            uint32_t a = subpass->pResolveAttachments[j].attachment;
            if (a == VK_ATTACHMENT_UNUSED)
               continue;
            if (att[a].initialLayout != subpass->pResolveAttachments[j].layout && !att_used[a])
               src_implicit_dep = true;
            att_used[a] = true;
         }
      }

      if (src_implicit_dep) {
         tu_render_pass_add_subpass_dep(pass, &(VkSubpassDependency2KHR) {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = i,
            .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0,
         });
      }
   }

   memset(att_used, 0, sizeof(att_used));

   for (int i = info->subpassCount - 1; i >= 0; i--) {
      if (!has_external_dst[i])
         continue;

      const VkSubpassDescription2 *subpass = &info->pSubpasses[i];
      bool dst_implicit_dep = false;

      for (unsigned j = 0; j < subpass->inputAttachmentCount; j++) {
         uint32_t a = subpass->pInputAttachments[j].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;
         if (att[a].finalLayout != subpass->pInputAttachments[j].layout && !att_used[a])
            dst_implicit_dep = true;
         att_used[a] = true;
      }

      for (unsigned j = 0; j < subpass->colorAttachmentCount; j++) {
         uint32_t a = subpass->pColorAttachments[j].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;
         if (att[a].finalLayout != subpass->pColorAttachments[j].layout && !att_used[a])
            dst_implicit_dep = true;
         att_used[a] = true;
      }

      if (subpass->pResolveAttachments) {
         for (unsigned j = 0; j < subpass->colorAttachmentCount; j++) {
            uint32_t a = subpass->pResolveAttachments[j].attachment;
            if (a == VK_ATTACHMENT_UNUSED)
               continue;
            if (att[a].finalLayout != subpass->pResolveAttachments[j].layout && !att_used[a])
               dst_implicit_dep = true;
            att_used[a] = true;
         }
      }

      if (dst_implicit_dep) {
         tu_render_pass_add_subpass_dep(pass, &(VkSubpassDependency2KHR) {
            .srcSubpass = i,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
            .dependencyFlags = 0,
         });
      }
   }

   /* Handle UNDEFINED transitions, similar to the handling in tu_barrier().
    * Assume that if an attachment has an initial layout of UNDEFINED, it gets
    * transitioned eventually.
    */
   for (unsigned i = 0; i < info->attachmentCount; i++) {
      if (layout_undefined(att[i].initialLayout)) {
         if (vk_format_is_depth_or_stencil(att[i].format)) {
            pass->subpasses[0].start_barrier.incoherent_ccu_depth = true;
         } else {
            pass->subpasses[0].start_barrier.incoherent_ccu_color = true;
         }
      }
   }
}

static void update_samples(struct tu_subpass *subpass,
                           VkSampleCountFlagBits samples)
{
   assert(subpass->samples == 0 || subpass->samples == samples);
   subpass->samples = samples;
}

static void
tu_render_pass_gmem_config(struct tu_render_pass *pass,
                           const struct tu_physical_device *phys_dev)
{
   uint32_t block_align_shift = 3; /* log2(gmem_align/(tile_align_w*tile_align_h)) */
   uint32_t tile_align_w = phys_dev->info.tile_align_w;
   uint32_t gmem_align = (1 << block_align_shift) * tile_align_w * phys_dev->info.tile_align_h;

   /* calculate total bytes per pixel */
   uint32_t cpp_total = 0;
   for (uint32_t i = 0; i < pass->attachment_count; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];
      bool cpp1 = (att->cpp == 1);
      if (att->gmem_offset >= 0) {
         cpp_total += att->cpp;

         /* take into account the separate stencil: */
         if (att->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            cpp1 = (att->samples == 1);
            cpp_total += att->samples;
         }

         /* texture pitch must be aligned to 64, use a tile_align_w that is
          * a multiple of 64 for cpp==1 attachment to work as input attachment
          */
         if (cpp1 && tile_align_w % 64 != 0) {
            tile_align_w *= 2;
            block_align_shift -= 1;
         }
      }
   }

   pass->tile_align_w = tile_align_w;

   /* no gmem attachments */
   if (cpp_total == 0) {
      /* any value non-zero value so tiling config works with no attachments */
      pass->gmem_pixels = 1024*1024;
      return;
   }

   /* TODO: using ccu_offset_gmem so that BLIT_OP_SCALE resolve path
    * doesn't break things. maybe there is a better solution?
    * TODO: this algorithm isn't optimal
    * for example, two attachments with cpp = {1, 4}
    * result:  nblocks = {12, 52}, pixels = 196608
    * optimal: nblocks = {13, 51}, pixels = 208896
    */
   uint32_t gmem_blocks = phys_dev->info.a6xx.ccu_offset_gmem / gmem_align;
   uint32_t offset = 0, pixels = ~0u, i;
   for (i = 0; i < pass->attachment_count; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];
      if (att->gmem_offset < 0)
         continue;

      att->gmem_offset = offset;

      uint32_t align = MAX2(1, att->cpp >> block_align_shift);
      uint32_t nblocks = MAX2((gmem_blocks * att->cpp / cpp_total) & ~(align - 1), align);

      if (nblocks > gmem_blocks)
         break;

      gmem_blocks -= nblocks;
      cpp_total -= att->cpp;
      offset += nblocks * gmem_align;
      pixels = MIN2(pixels, nblocks * gmem_align / att->cpp);

      /* repeat the same for separate stencil */
      if (att->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         att->gmem_offset_stencil = offset;

         /* note: for s8_uint, block align is always 1 */
         uint32_t nblocks = gmem_blocks * att->samples / cpp_total;
         if (nblocks > gmem_blocks)
            break;

         gmem_blocks -= nblocks;
         cpp_total -= att->samples;
         offset += nblocks * gmem_align;
         pixels = MIN2(pixels, nblocks * gmem_align / att->samples);
      }
   }

   /* if the loop didn't complete then the gmem config is impossible */
   if (i == pass->attachment_count)
      pass->gmem_pixels = pixels;
}

static void
attachment_set_ops(struct tu_render_pass_attachment *att,
                   VkAttachmentLoadOp load_op,
                   VkAttachmentLoadOp stencil_load_op,
                   VkAttachmentStoreOp store_op,
                   VkAttachmentStoreOp stencil_store_op)
{
   /* load/store ops */
   att->clear_mask =
      (load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) ? VK_IMAGE_ASPECT_COLOR_BIT : 0;
   att->load = (load_op == VK_ATTACHMENT_LOAD_OP_LOAD);
   att->store = (store_op == VK_ATTACHMENT_STORE_OP_STORE);

   bool stencil_clear = (stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR);
   bool stencil_load = (stencil_load_op == VK_ATTACHMENT_LOAD_OP_LOAD);
   bool stencil_store = (stencil_store_op == VK_ATTACHMENT_STORE_OP_STORE);

   switch (att->format) {
   case VK_FORMAT_D24_UNORM_S8_UINT: /* || stencil load/store */
      if (att->clear_mask)
         att->clear_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
      if (stencil_clear)
         att->clear_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
      if (stencil_load)
         att->load = true;
      if (stencil_store)
         att->store = true;
      break;
   case VK_FORMAT_S8_UINT: /* replace load/store with stencil load/store */
      att->clear_mask = stencil_clear ? VK_IMAGE_ASPECT_COLOR_BIT : 0;
      att->load = stencil_load;
      att->store = stencil_store;
      break;
   case VK_FORMAT_D32_SFLOAT_S8_UINT: /* separate stencil */
      if (att->clear_mask)
         att->clear_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
      if (stencil_clear)
         att->clear_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
      if (stencil_load)
         att->load_stencil = true;
      if (stencil_store)
         att->store_stencil = true;
      break;
   default:
      break;
   }
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

   pass = vk_object_zalloc(&device->vk, pAllocator, size,
                           VK_OBJECT_TYPE_RENDER_PASS);
   if (pass == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   pass->attachment_count = pCreateInfo->attachmentCount;
   pass->subpass_count = pCreateInfo->subpassCount;
   pass->attachments = (void *) pass + attachments_offset;

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];

      att->format = pCreateInfo->pAttachments[i].format;
      att->samples = pCreateInfo->pAttachments[i].samples;
      /* for d32s8, cpp is for the depth image, and
       * att->samples will be used as the cpp for the stencil image
       */
      if (att->format == VK_FORMAT_D32_SFLOAT_S8_UINT)
         att->cpp = 4 * att->samples;
      else
         att->cpp = vk_format_get_blocksize(att->format) * att->samples;
      att->gmem_offset = -1;

      attachment_set_ops(att,
                         pCreateInfo->pAttachments[i].loadOp,
                         pCreateInfo->pAttachments[i].stencilLoadOp,
                         pCreateInfo->pAttachments[i].storeOp,
                         pCreateInfo->pAttachments[i].stencilStoreOp);
   }
   uint32_t subpass_attachment_count = 0;
   struct tu_subpass_attachment *p;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2 *desc = &pCreateInfo->pSubpasses[i];

      subpass_attachment_count +=
         desc->inputAttachmentCount + desc->colorAttachmentCount +
         (desc->pResolveAttachments ? desc->colorAttachmentCount : 0);
   }

   if (subpass_attachment_count) {
      pass->subpass_attachments = vk_alloc2(
         &device->vk.alloc, pAllocator,
         subpass_attachment_count * sizeof(struct tu_subpass_attachment), 8,
         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (pass->subpass_attachments == NULL) {
         vk_object_free(&device->vk, pAllocator, pass);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else
      pass->subpass_attachments = NULL;

   p = pass->subpass_attachments;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      const VkSubpassDescription2 *desc = &pCreateInfo->pSubpasses[i];
      struct tu_subpass *subpass = &pass->subpasses[i];

      subpass->input_count = desc->inputAttachmentCount;
      subpass->color_count = desc->colorAttachmentCount;
      subpass->samples = 0;
      subpass->srgb_cntl = 0;

      subpass->multiview_mask = desc->viewMask;

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

               if (vk_format_is_srgb(pass->attachments[a].format))
                  subpass->srgb_cntl |= 1 << j;

               pass->attachments[a].clear_views |= subpass->multiview_mask;
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

   /* disable unused attachments */
   for (uint32_t i = 0; i < pass->attachment_count; i++) {
      struct tu_render_pass_attachment *att = &pass->attachments[i];
      if (att->gmem_offset < 0) {
         att->clear_mask = 0;
         att->load = false;
      }
   }

   /* From the VK_KHR_multiview spec:
    *
    *    Multiview is all-or-nothing for a render pass - that is, either all
    *    subpasses must have a non-zero view mask (though some subpasses may
    *    have only one view) or all must be zero.
    *
    * This means we only have to check one of the view masks.
    */
   if (pCreateInfo->pSubpasses[0].viewMask) {
      /* It seems multiview must use sysmem rendering. */
      pass->gmem_pixels = 0;
   } else {
      tu_render_pass_gmem_config(pass, device->physical_device);
   }

   for (unsigned i = 0; i < pCreateInfo->dependencyCount; ++i) {
      tu_render_pass_add_subpass_dep(pass, &pCreateInfo->pDependencies[i]);
   }

   tu_render_pass_add_implicit_deps(pass, pCreateInfo);
 
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

   vk_free2(&device->vk.alloc, pAllocator, pass->subpass_attachments);
   vk_object_free(&device->vk, pAllocator, pass);
}

void
tu_GetRenderAreaGranularity(VkDevice _device,
                            VkRenderPass renderPass,
                            VkExtent2D *pGranularity)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   pGranularity->width = device->physical_device->info.gmem_align_w;
   pGranularity->height = device->physical_device->info.gmem_align_h;
}
