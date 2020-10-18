/*
 * Copyright 2020 Valve Corporation
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Jonathan Marek <jonathan@marek.ca>
 */

#include <vulkan/vulkan.h>
#include <vulkan/vk_android_native_buffer.h> /* android tu_entrypoints.h depends on this */
#include <assert.h>

#include "tu_entrypoints.h"
#include "vk_util.h"

void
tu_GetPhysicalDeviceFeatures(VkPhysicalDevice pdev, VkPhysicalDeviceFeatures *features)
{
   VkPhysicalDeviceFeatures2 features2;
   features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
   features2.pNext = NULL;
   tu_GetPhysicalDeviceFeatures2(pdev, &features2);
   *features = features2.features;
}

void
tu_GetPhysicalDeviceProperties(VkPhysicalDevice pdev, VkPhysicalDeviceProperties *props)
{
   VkPhysicalDeviceProperties2 props2;
   props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
   props2.pNext = NULL;
   tu_GetPhysicalDeviceProperties2(pdev, &props2);
   *props = props2.properties;
}

void
tu_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pdev,
                                          uint32_t *count,
                                          VkQueueFamilyProperties *props)
{
   if (!props)
      return tu_GetPhysicalDeviceQueueFamilyProperties2(pdev, count, NULL);

   VkQueueFamilyProperties2 props2[*count];
   for (uint32_t i = 0; i < *count; i++) {
      props2[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
      props2[i].pNext = NULL;
   }
   tu_GetPhysicalDeviceQueueFamilyProperties2(pdev, count, props2);
   for (uint32_t i = 0; i < *count; i++)
      props[i] = props2[i].queueFamilyProperties;
}

void
tu_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice pdev, VkPhysicalDeviceMemoryProperties *props)
{
   VkPhysicalDeviceMemoryProperties2 props2;
   props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
   props2.pNext = NULL;
   tu_GetPhysicalDeviceMemoryProperties2(pdev, &props2);
   *props = props2.memoryProperties;
}

void
tu_GetPhysicalDeviceFormatProperties(VkPhysicalDevice pdev, VkFormat format, VkFormatProperties *props)
{
   VkFormatProperties2 props2 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
   tu_GetPhysicalDeviceFormatProperties2(pdev, format, &props2);
   *props = props2.formatProperties;
}

VkResult
tu_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pdev,
                                          VkFormat format,
                                          VkImageType type,
                                          VkImageTiling tiling,
                                          VkImageUsageFlags usage,
                                          VkImageCreateFlags flags,
                                          VkImageFormatProperties *props)
{
   VkImageFormatProperties2 props2 = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
   VkResult result = tu_GetPhysicalDeviceImageFormatProperties2(pdev, &(VkPhysicalDeviceImageFormatInfo2) {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = format,
      .type = type,
      .tiling = tiling,
      .usage = usage,
      .flags = flags
   }, &props2);
   *props = props2.imageFormatProperties;
   return result;
}

void
tu_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice pdev,
                                                VkFormat format,
                                                VkImageType type,
                                                VkSampleCountFlagBits samples,
                                                VkImageUsageFlags usage,
                                                VkImageTiling tiling,
                                                uint32_t *count,
                                                VkSparseImageFormatProperties *props)
{
   const VkPhysicalDeviceSparseImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = format,
      .type = type,
      .samples = samples,
      .usage = usage,
      .tiling = tiling,
   };

   if (!props)
      return tu_GetPhysicalDeviceSparseImageFormatProperties2(pdev, &info, count, NULL);

   VkSparseImageFormatProperties2 props2[*count];
   for (uint32_t i = 0; i < *count; i++) {
      props2[i].sType = VK_STRUCTURE_TYPE_SPARSE_IMAGE_FORMAT_PROPERTIES_2;
      props2[i].pNext = NULL;
   }
   tu_GetPhysicalDeviceSparseImageFormatProperties2(pdev, &info, count, props2);
   for (uint32_t i = 0; i < *count; i++)
      props[i] = props2[i].properties;
}

void
tu_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue)
{
   tu_GetDeviceQueue2(device, &(VkDeviceQueueInfo2) {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
      .queueFamilyIndex = queueFamilyIndex,
      .queueIndex = queueIndex
   }, pQueue);
}

void
tu_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *reqs)
{
   VkMemoryRequirements2 reqs2 = { .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
   tu_GetBufferMemoryRequirements2(device, &(VkBufferMemoryRequirementsInfo2) {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
      .buffer = buffer
   }, &reqs2);
   *reqs = reqs2.memoryRequirements;
}

void
tu_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *reqs)
{
   VkMemoryRequirements2 reqs2 = { .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
   tu_GetImageMemoryRequirements2(device, &(VkImageMemoryRequirementsInfo2) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image
   }, &reqs2);
   *reqs = reqs2.memoryRequirements;
}

void
tu_GetImageSparseMemoryRequirements(VkDevice device,
                                    VkImage image,
                                    uint32_t *count,
                                    VkSparseImageMemoryRequirements *reqs)
{
   const VkImageSparseMemoryRequirementsInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image
   };

   if (!reqs)
      return tu_GetImageSparseMemoryRequirements2(device, &info, count, NULL);

   VkSparseImageMemoryRequirements2 reqs2[*count];
   for (uint32_t i = 0; i < *count; i++) {
      reqs2[i].sType = VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2;
      reqs2[i].pNext = NULL;
   }
   tu_GetImageSparseMemoryRequirements2(device, &info, count, reqs2);
   for (uint32_t i = 0; i < *count; i++)
      reqs[i] = reqs2[i].memoryRequirements;
}

VkResult
tu_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset)
{
   return tu_BindBufferMemory2(device, 1, &(VkBindBufferMemoryInfo) {
      .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
      .buffer = buffer,
      .memory = memory,
      .memoryOffset = offset
   });
}

VkResult
tu_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset)
{
   return tu_BindImageMemory2(device, 1, &(VkBindImageMemoryInfo) {
      .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image = image,
      .memory = memory,
      .memoryOffset = offset
   });
}

static void
translate_references(VkAttachmentReference2 **reference_ptr,
                     const VkAttachmentReference *reference,
                     uint32_t count)
{
   VkAttachmentReference2 *reference2 = *reference_ptr;
   *reference_ptr += count;
   for (uint32_t i = 0; i < count; i++) {
      reference2[i] = (VkAttachmentReference2) {
         .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
         .pNext = NULL,
         .attachment = reference[i].attachment,
         .layout = reference[i].layout,
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
      };
   }
}

VkResult
tu_CreateRenderPass(VkDevice device,
                    const VkRenderPassCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkRenderPass *pRenderPass)
{
   /* note: these counts shouldn't be excessively high, so allocating it all
    * on the stack should be OK..
    * also note preserve attachments aren't translated, currently unused
    */
   VkAttachmentDescription2 attachments[pCreateInfo->attachmentCount];
   VkSubpassDescription2 subpasses[pCreateInfo->subpassCount];
   VkSubpassDependency2 dependencies[pCreateInfo->dependencyCount];
   uint32_t reference_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
      reference_count += pCreateInfo->pSubpasses[i].inputAttachmentCount;
      reference_count += pCreateInfo->pSubpasses[i].colorAttachmentCount;
      if (pCreateInfo->pSubpasses[i].pResolveAttachments)
         reference_count += pCreateInfo->pSubpasses[i].colorAttachmentCount;
      if (pCreateInfo->pSubpasses[i].pDepthStencilAttachment)
         reference_count += 1;
   }
   VkAttachmentReference2 reference[reference_count];
   VkAttachmentReference2 *reference_ptr = reference;

   VkRenderPassMultiviewCreateInfo *multiview_info = NULL;
   vk_foreach_struct(ext, pCreateInfo->pNext) {
      if (ext->sType == VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO) {
         multiview_info = (VkRenderPassMultiviewCreateInfo*) ext;
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
      };

      if (multiview_info && multiview_info->subpassCount)
         subpasses[i].viewMask = multiview_info->pViewMasks[i];

      subpasses[i].pInputAttachments = reference_ptr;
      translate_references(&reference_ptr,
                           pCreateInfo->pSubpasses[i].pInputAttachments,
                           subpasses[i].inputAttachmentCount);
      subpasses[i].pColorAttachments = reference_ptr;
      translate_references(&reference_ptr,
                           pCreateInfo->pSubpasses[i].pColorAttachments,
                           subpasses[i].colorAttachmentCount);
      subpasses[i].pResolveAttachments = NULL;
      if (pCreateInfo->pSubpasses[i].pResolveAttachments) {
         subpasses[i].pResolveAttachments = reference_ptr;
         translate_references(&reference_ptr,
                              pCreateInfo->pSubpasses[i].pResolveAttachments,
                              subpasses[i].colorAttachmentCount);
      }
      subpasses[i].pDepthStencilAttachment = NULL;
      if (pCreateInfo->pSubpasses[i].pDepthStencilAttachment) {
         subpasses[i].pDepthStencilAttachment = reference_ptr;
         translate_references(&reference_ptr,
                              pCreateInfo->pSubpasses[i].pDepthStencilAttachment,
                              1);
      }
   }

   assert(reference_ptr == reference + reference_count);

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

      if (multiview_info && multiview_info->dependencyCount)
         dependencies[i].viewOffset = multiview_info->pViewOffsets[i];
   }

   VkRenderPassCreateInfo2 create_info = {
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

   if (multiview_info) {
      create_info.correlatedViewMaskCount = multiview_info->correlationMaskCount;
      create_info.pCorrelatedViewMasks = multiview_info->pCorrelationMasks;
   }

   return tu_CreateRenderPass2(device, &create_info, pAllocator, pRenderPass);
}

void
tu_CmdBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo *info, VkSubpassContents contents)
{
   return tu_CmdBeginRenderPass2(cmd, info, &(VkSubpassBeginInfo) {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
      .contents = contents
   });
}

void
tu_CmdNextSubpass(VkCommandBuffer cmd, VkSubpassContents contents)
{
   return tu_CmdNextSubpass2(cmd, &(VkSubpassBeginInfo) {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
      .contents = contents
   }, &(VkSubpassEndInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
   });
}

void
tu_CmdEndRenderPass(VkCommandBuffer cmd)
{
   return tu_CmdEndRenderPass2(cmd, &(VkSubpassEndInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
   });
}
