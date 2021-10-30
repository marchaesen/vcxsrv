/*
 * Copyright © 2020 Valve Corporation
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

#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_format.h"
#include "vk_util.h"

#include "util/log.h"

static void
translate_references(VkAttachmentReference2 **reference_ptr,
                     uint32_t reference_count,
                     const VkAttachmentReference *reference,
                     const VkRenderPassCreateInfo *pass_info,
                     bool is_input_attachment)
{
   VkAttachmentReference2 *reference2 = *reference_ptr;
   *reference_ptr += reference_count;
   for (uint32_t i = 0; i < reference_count; i++) {
      reference2[i] = (VkAttachmentReference2) {
         .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
         .pNext = NULL,
         .attachment = reference[i].attachment,
         .layout = reference[i].layout,
      };

      if (is_input_attachment &&
          reference2[i].attachment != VK_ATTACHMENT_UNUSED) {
         assert(reference2[i].attachment < pass_info->attachmentCount);
         const VkAttachmentDescription *att =
            &pass_info->pAttachments[reference2[i].attachment];
         reference2[i].aspectMask = vk_format_aspects(att->format);
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CreateRenderPass(VkDevice _device,
                           const VkRenderPassCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkRenderPass *pRenderPass)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   uint32_t reference_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      reference_count += pCreateInfo->pSubpasses[i].inputAttachmentCount;
      reference_count += pCreateInfo->pSubpasses[i].colorAttachmentCount;
      if (pCreateInfo->pSubpasses[i].pResolveAttachments)
         reference_count += pCreateInfo->pSubpasses[i].colorAttachmentCount;
      if (pCreateInfo->pSubpasses[i].pDepthStencilAttachment)
         reference_count += 1;
   }

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, VkRenderPassCreateInfo2, create_info, 1);
   VK_MULTIALLOC_DECL(&ma, VkSubpassDescription2, subpasses,
                           pCreateInfo->subpassCount);
   VK_MULTIALLOC_DECL(&ma, VkAttachmentDescription2, attachments,
                           pCreateInfo->attachmentCount);
   VK_MULTIALLOC_DECL(&ma, VkSubpassDependency2, dependencies,
                           pCreateInfo->dependencyCount);
   VK_MULTIALLOC_DECL(&ma, VkAttachmentReference2, references,
                           reference_count);
   if (!vk_multialloc_alloc2(&ma, &device->alloc, pAllocator,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND))
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkAttachmentReference2 *reference_ptr = references;

   const VkRenderPassMultiviewCreateInfo *multiview_info = NULL;
   const VkRenderPassInputAttachmentAspectCreateInfo *aspect_info = NULL;
   vk_foreach_struct(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_RENDER_PASS_INPUT_ATTACHMENT_ASPECT_CREATE_INFO:
         aspect_info = (const VkRenderPassInputAttachmentAspectCreateInfo *)ext;
         /* We don't care about this information */
         break;

      case VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO:
         multiview_info = (const VkRenderPassMultiviewCreateInfo*) ext;
         break;

      default:
         mesa_logd("%s: ignored VkStructureType %u\n", __func__, ext->sType);
         break;
      }
   }

   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      attachments[i] = (VkAttachmentDescription2) {
         .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
         .pNext = NULL,
         .flags = pCreateInfo->pAttachments[i].flags,
         .format = pCreateInfo->pAttachments[i].format,
         .samples = pCreateInfo->pAttachments[i].samples,
         .loadOp = pCreateInfo->pAttachments[i].loadOp,
         .storeOp = pCreateInfo->pAttachments[i].storeOp,
         .stencilLoadOp = pCreateInfo->pAttachments[i].stencilLoadOp,
         .stencilStoreOp = pCreateInfo->pAttachments[i].stencilStoreOp,
         .initialLayout = pCreateInfo->pAttachments[i].initialLayout,
         .finalLayout = pCreateInfo->pAttachments[i].finalLayout,
      };
   }

   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      subpasses[i] = (VkSubpassDescription2) {
         .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
         .pNext = NULL,
         .flags = pCreateInfo->pSubpasses[i].flags,
         .pipelineBindPoint = pCreateInfo->pSubpasses[i].pipelineBindPoint,
         .viewMask = 0,
         .inputAttachmentCount = pCreateInfo->pSubpasses[i].inputAttachmentCount,
         .colorAttachmentCount = pCreateInfo->pSubpasses[i].colorAttachmentCount,
         .preserveAttachmentCount = pCreateInfo->pSubpasses[i].preserveAttachmentCount,
         .pPreserveAttachments = pCreateInfo->pSubpasses[i].pPreserveAttachments,
      };

      if (multiview_info && multiview_info->subpassCount) {
         assert(multiview_info->subpassCount == pCreateInfo->subpassCount);
         subpasses[i].viewMask = multiview_info->pViewMasks[i];
      }

      subpasses[i].pInputAttachments = reference_ptr;
      translate_references(&reference_ptr,
                           subpasses[i].inputAttachmentCount,
                           pCreateInfo->pSubpasses[i].pInputAttachments,
                           pCreateInfo, true);
      subpasses[i].pColorAttachments = reference_ptr;
      translate_references(&reference_ptr,
                           subpasses[i].colorAttachmentCount,
                           pCreateInfo->pSubpasses[i].pColorAttachments,
                           pCreateInfo, false);
      subpasses[i].pResolveAttachments = NULL;
      if (pCreateInfo->pSubpasses[i].pResolveAttachments) {
         subpasses[i].pResolveAttachments = reference_ptr;
         translate_references(&reference_ptr,
                              subpasses[i].colorAttachmentCount,
                              pCreateInfo->pSubpasses[i].pResolveAttachments,
                              pCreateInfo, false);
      }
      subpasses[i].pDepthStencilAttachment = NULL;
      if (pCreateInfo->pSubpasses[i].pDepthStencilAttachment) {
         subpasses[i].pDepthStencilAttachment = reference_ptr;
         translate_references(&reference_ptr, 1,
                              pCreateInfo->pSubpasses[i].pDepthStencilAttachment,
                              pCreateInfo, false);
      }
   }

   assert(reference_ptr == references + reference_count);

   if (aspect_info != NULL) {
      for (uint32_t i = 0; i < aspect_info->aspectReferenceCount; i++) {
         const VkInputAttachmentAspectReference *ref =
            &aspect_info->pAspectReferences[i];

         assert(ref->subpass < pCreateInfo->subpassCount);
         VkSubpassDescription2 *subpass = &subpasses[ref->subpass];

         assert(ref->inputAttachmentIndex < subpass->inputAttachmentCount);
         VkAttachmentReference2 *att = (VkAttachmentReference2 *)
            &subpass->pInputAttachments[ref->inputAttachmentIndex];

         att->aspectMask = ref->aspectMask;
      }
   }

   for (uint32_t i = 0; i < pCreateInfo->dependencyCount; i++) {
      dependencies[i] = (VkSubpassDependency2) {
         .sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
         .pNext = NULL,
         .srcSubpass = pCreateInfo->pDependencies[i].srcSubpass,
         .dstSubpass = pCreateInfo->pDependencies[i].dstSubpass,
         .srcStageMask = pCreateInfo->pDependencies[i].srcStageMask,
         .dstStageMask = pCreateInfo->pDependencies[i].dstStageMask,
         .srcAccessMask = pCreateInfo->pDependencies[i].srcAccessMask,
         .dstAccessMask = pCreateInfo->pDependencies[i].dstAccessMask,
         .dependencyFlags = pCreateInfo->pDependencies[i].dependencyFlags,
         .viewOffset = 0,
      };

      if (multiview_info && multiview_info->dependencyCount) {
         assert(multiview_info->dependencyCount == pCreateInfo->dependencyCount);
         dependencies[i].viewOffset = multiview_info->pViewOffsets[i];
      }
   }

   *create_info = (VkRenderPassCreateInfo2) {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
      .pNext = pCreateInfo->pNext,
      .flags = pCreateInfo->flags,
      .attachmentCount = pCreateInfo->attachmentCount,
      .pAttachments = attachments,
      .subpassCount = pCreateInfo->subpassCount,
      .pSubpasses = subpasses,
      .dependencyCount = pCreateInfo->dependencyCount,
      .pDependencies = dependencies,
   };

   if (multiview_info && multiview_info->correlationMaskCount > 0) {
      create_info->correlatedViewMaskCount = multiview_info->correlationMaskCount;
      create_info->pCorrelatedViewMasks = multiview_info->pCorrelationMasks;
   }

   VkResult result =
      device->dispatch_table.CreateRenderPass2(_device, create_info,
                                               pAllocator, pRenderPass);

   vk_free2(&device->alloc, pAllocator, create_info);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                             const VkRenderPassBeginInfo* pRenderPassBegin,
                             VkSubpassContents contents)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   VkSubpassBeginInfo info = {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
      .contents = contents,
   };

   disp->device->dispatch_table.CmdBeginRenderPass2(commandBuffer,
                                                    pRenderPassBegin, &info);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdEndRenderPass(VkCommandBuffer commandBuffer)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   VkSubpassEndInfo info = {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
   };

   disp->device->dispatch_table.CmdEndRenderPass2(commandBuffer, &info);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdNextSubpass(VkCommandBuffer commandBuffer,
                         VkSubpassContents contents)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   VkSubpassBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
      .contents = contents,
   };

   VkSubpassEndInfo end_info = {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
   };

   disp->device->dispatch_table.CmdNextSubpass2(commandBuffer, &begin_info,
                                                &end_info);
}
