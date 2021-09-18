/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_command_buffer.h"

#include "venus-protocol/vn_protocol_driver_command_buffer.h"
#include "venus-protocol/vn_protocol_driver_command_pool.h"

#include "vn_device.h"
#include "vn_image.h"
#include "vn_render_pass.h"

static bool
vn_image_memory_barrier_has_present_src(
   const VkImageMemoryBarrier *img_barriers, uint32_t count)
{
   for (uint32_t i = 0; i < count; i++) {
      if (img_barriers[i].oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
          img_barriers[i].newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         return true;
   }
   return false;
}

static VkImageMemoryBarrier *
vn_cmd_get_image_memory_barriers(struct vn_command_buffer *cmd,
                                 uint32_t count)
{
   /* avoid shrinking in case of non efficient reallocation implementation */
   if (count > cmd->builder.image_barrier_count) {
      size_t size = sizeof(VkImageMemoryBarrier) * count;
      VkImageMemoryBarrier *img_barriers =
         vk_realloc(&cmd->allocator, cmd->builder.image_barriers, size,
                    VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!img_barriers)
         return NULL;

      /* update upon successful reallocation */
      cmd->builder.image_barrier_count = count;
      cmd->builder.image_barriers = img_barriers;
   }

   return cmd->builder.image_barriers;
}

/* About VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, the spec says
 *
 *    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR must only be used for presenting a
 *    presentable image for display. A swapchain's image must be transitioned
 *    to this layout before calling vkQueuePresentKHR, and must be
 *    transitioned away from this layout after calling vkAcquireNextImageKHR.
 *
 * That allows us to treat the layout internally as
 *
 *  - VK_IMAGE_LAYOUT_GENERAL
 *  - VK_QUEUE_FAMILY_FOREIGN_EXT has the ownership, if the image is not a
 *    prime blit source
 *
 * while staying performant.
 *
 * About queue family ownerships, the spec says
 *
 *    A queue family can take ownership of an image subresource or buffer
 *    range of a resource created with VK_SHARING_MODE_EXCLUSIVE, without an
 *    ownership transfer, in the same way as for a resource that was just
 *    created; however, taking ownership in this way has the effect that the
 *    contents of the image subresource or buffer range are undefined.
 *
 * It is unclear if that is applicable to external resources, which supposedly
 * have the same semantics
 *
 *    Binding a resource to a memory object shared between multiple Vulkan
 *    instances or other APIs does not change the ownership of the underlying
 *    memory. The first entity to access the resource implicitly acquires
 *    ownership. Accessing a resource backed by memory that is owned by a
 *    particular instance or API has the same semantics as accessing a
 *    VK_SHARING_MODE_EXCLUSIVE resource[...]
 *
 * We should get the spec clarified, or get rid of this completely broken code
 * (TODO).
 *
 * Assuming a queue family can acquire the ownership implicitly when the
 * contents are not needed, we do not need to worry about
 * VK_IMAGE_LAYOUT_UNDEFINED.  We can use VK_IMAGE_LAYOUT_PRESENT_SRC_KHR as
 * the sole signal to trigger queue family ownership transfers.
 *
 * When the image has VK_SHARING_MODE_CONCURRENT, we can, and are required to,
 * use VK_QUEUE_FAMILY_IGNORED as the other queue family whether we are
 * transitioning to or from VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
 *
 * When the image has VK_SHARING_MODE_EXCLUSIVE, we have to work out who the
 * other queue family is.  It is easier when the barrier does not also define
 * a queue family ownership transfer (i.e., srcQueueFamilyIndex equals to
 * dstQueueFamilyIndex).  The other queue family must be the queue family the
 * command buffer was allocated for.
 *
 * When the barrier also defines a queue family ownership transfer, it is
 * submitted both to the source queue family to release the ownership and to
 * the destination queue family to acquire the ownership.  Depending on
 * whether the barrier transitions to or from VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
 * we are only interested in the ownership release or acquire respectively and
 * should be careful to avoid double releases/acquires.
 *
 * I haven't followed all transition paths mentally to verify the correctness.
 * I likely also violate some VUs or miss some cases below.  They are
 * hopefully fixable and are left as TODOs.
 */
static void
vn_cmd_fix_image_memory_barrier(const struct vn_command_buffer *cmd,
                                const VkImageMemoryBarrier *src_barrier,
                                VkImageMemoryBarrier *out_barrier)
{
   const struct vn_image *img = vn_image_from_handle(src_barrier->image);

   *out_barrier = *src_barrier;

   /* no fix needed */
   if (out_barrier->oldLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
       out_barrier->newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
      return;

   assert(img->is_wsi);

   if (VN_PRESENT_SRC_INTERNAL_LAYOUT == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
      return;

   /* prime blit src or no layout transition */
   if (img->is_prime_blit_src ||
       out_barrier->oldLayout == out_barrier->newLayout) {
      if (out_barrier->oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         out_barrier->oldLayout = VN_PRESENT_SRC_INTERNAL_LAYOUT;
      if (out_barrier->newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
         out_barrier->newLayout = VN_PRESENT_SRC_INTERNAL_LAYOUT;
      return;
   }

   if (out_barrier->oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
      out_barrier->oldLayout = VN_PRESENT_SRC_INTERNAL_LAYOUT;

      /* no availability operation needed */
      out_barrier->srcAccessMask = 0;

      const uint32_t dst_qfi = out_barrier->dstQueueFamilyIndex;
      if (img->sharing_mode == VK_SHARING_MODE_CONCURRENT) {
         out_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
         out_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      } else if (dst_qfi == out_barrier->srcQueueFamilyIndex ||
                 dst_qfi == cmd->queue_family_index) {
         out_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
         out_barrier->dstQueueFamilyIndex = cmd->queue_family_index;
      } else {
         /* The barrier also defines a queue family ownership transfer, and
          * this is the one that gets submitted to the source queue family to
          * release the ownership.  Skip both the transfer and the transition.
          */
         out_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         out_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         out_barrier->newLayout = out_barrier->oldLayout;
      }
   } else {
      out_barrier->newLayout = VN_PRESENT_SRC_INTERNAL_LAYOUT;

      /* no visibility operation needed */
      out_barrier->dstAccessMask = 0;

      const uint32_t src_qfi = out_barrier->srcQueueFamilyIndex;
      if (img->sharing_mode == VK_SHARING_MODE_CONCURRENT) {
         out_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         out_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
      } else if (src_qfi == out_barrier->dstQueueFamilyIndex ||
                 src_qfi == cmd->queue_family_index) {
         out_barrier->srcQueueFamilyIndex = cmd->queue_family_index;
         out_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
      } else {
         /* The barrier also defines a queue family ownership transfer, and
          * this is the one that gets submitted to the destination queue
          * family to acquire the ownership.  Skip both the transfer and the
          * transition.
          */
         out_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         out_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         out_barrier->oldLayout = out_barrier->newLayout;
      }
   }
}

static const VkImageMemoryBarrier *
vn_cmd_wait_events_fix_image_memory_barriers(
   struct vn_command_buffer *cmd,
   const VkImageMemoryBarrier *src_barriers,
   uint32_t count,
   uint32_t *out_transfer_count)
{
   *out_transfer_count = 0;

   if (cmd->builder.render_pass ||
       !vn_image_memory_barrier_has_present_src(src_barriers, count))
      return src_barriers;

   VkImageMemoryBarrier *img_barriers =
      vn_cmd_get_image_memory_barriers(cmd, count * 2);
   if (!img_barriers) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return src_barriers;
   }

   /* vkCmdWaitEvents cannot be used for queue family ownership transfers.
    * Nothing appears to be said about the submission order of image memory
    * barriers in the same array.  We take the liberty to move queue family
    * ownership transfers to the tail.
    */
   VkImageMemoryBarrier *transfer_barriers = img_barriers + count;
   uint32_t transfer_count = 0;
   uint32_t valid_count = 0;
   for (uint32_t i = 0; i < count; i++) {
      VkImageMemoryBarrier *img_barrier = &img_barriers[valid_count];
      vn_cmd_fix_image_memory_barrier(cmd, &src_barriers[i], img_barrier);

      if (VN_PRESENT_SRC_INTERNAL_LAYOUT == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
         valid_count++;
         continue;
      }

      if (img_barrier->srcQueueFamilyIndex ==
          img_barrier->dstQueueFamilyIndex) {
         valid_count++;
      } else {
         transfer_barriers[transfer_count++] = *img_barrier;
      }
   }

   assert(valid_count + transfer_count == count);
   if (transfer_count) {
      /* copy back to the tail */
      memcpy(&img_barriers[valid_count], transfer_barriers,
             sizeof(*transfer_barriers) * transfer_count);
      *out_transfer_count = transfer_count;
   }

   return img_barriers;
}

static const VkImageMemoryBarrier *
vn_cmd_pipeline_barrier_fix_image_memory_barriers(
   struct vn_command_buffer *cmd,
   const VkImageMemoryBarrier *src_barriers,
   uint32_t count)
{
   if (cmd->builder.render_pass ||
       !vn_image_memory_barrier_has_present_src(src_barriers, count))
      return src_barriers;

   VkImageMemoryBarrier *img_barriers =
      vn_cmd_get_image_memory_barriers(cmd, count);
   if (!img_barriers) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return src_barriers;
   }

   for (uint32_t i = 0; i < count; i++) {
      vn_cmd_fix_image_memory_barrier(cmd, &src_barriers[i],
                                      &img_barriers[i]);
   }

   return img_barriers;
}

static void
vn_cmd_encode_memory_barriers(struct vn_command_buffer *cmd,
                              VkPipelineStageFlags src_stage_mask,
                              VkPipelineStageFlags dst_stage_mask,
                              uint32_t buf_barrier_count,
                              const VkBufferMemoryBarrier *buf_barriers,
                              uint32_t img_barrier_count,
                              const VkImageMemoryBarrier *img_barriers)
{
   const VkCommandBuffer cmd_handle = vn_command_buffer_to_handle(cmd);

   const size_t cmd_size = vn_sizeof_vkCmdPipelineBarrier(
      cmd_handle, src_stage_mask, dst_stage_mask, 0, 0, NULL,
      buf_barrier_count, buf_barriers, img_barrier_count, img_barriers);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size)) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return;
   }

   vn_encode_vkCmdPipelineBarrier(
      &cmd->cs, 0, cmd_handle, src_stage_mask, dst_stage_mask, 0, 0, NULL,
      buf_barrier_count, buf_barriers, img_barrier_count, img_barriers);
}

static void
vn_present_src_attachment_to_image_memory_barrier(
   const struct vn_image *img,
   const struct vn_present_src_attachment *att,
   VkImageMemoryBarrier *img_barrier)
{
   *img_barrier = (VkImageMemoryBarrier)
   {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = att->src_access_mask,
      .dstAccessMask = att->dst_access_mask,
      .oldLayout = att->acquire ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                : VN_PRESENT_SRC_INTERNAL_LAYOUT,
      .newLayout = att->acquire ? VN_PRESENT_SRC_INTERNAL_LAYOUT
                                : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .image = vn_image_to_handle((struct vn_image *)img),
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .levelCount = 1,
         .layerCount = 1,
      },
   };
}

static void
vn_cmd_transfer_present_src_images(
   struct vn_command_buffer *cmd,
   const struct vn_image *const *images,
   const struct vn_present_src_attachment *atts,
   uint32_t count)
{
   VkImageMemoryBarrier *img_barriers =
      vn_cmd_get_image_memory_barriers(cmd, count);
   if (!img_barriers) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return;
   }

   VkPipelineStageFlags src_stage_mask = 0;
   VkPipelineStageFlags dst_stage_mask = 0;
   for (uint32_t i = 0; i < count; i++) {
      src_stage_mask |= atts[i].src_stage_mask;
      dst_stage_mask |= atts[i].dst_stage_mask;

      vn_present_src_attachment_to_image_memory_barrier(images[i], &atts[i],
                                                        &img_barriers[i]);
      vn_cmd_fix_image_memory_barrier(cmd, &img_barriers[i],
                                      &img_barriers[i]);
   }

   if (VN_PRESENT_SRC_INTERNAL_LAYOUT == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
      return;

   vn_cmd_encode_memory_barriers(cmd, src_stage_mask, dst_stage_mask, 0, NULL,
                                 count, img_barriers);
}

static void
vn_cmd_begin_render_pass(struct vn_command_buffer *cmd,
                         const struct vn_render_pass *pass,
                         const struct vn_framebuffer *fb,
                         const VkRenderPassBeginInfo *begin_info)
{
   cmd->builder.render_pass = pass;
   cmd->builder.framebuffer = fb;

   if (!pass->present_src_count ||
       cmd->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
      return;

   /* find fb attachments */
   const VkImageView *views;
   ASSERTED uint32_t view_count;
   if (fb->image_view_count) {
      views = fb->image_views;
      view_count = fb->image_view_count;
   } else {
      const VkRenderPassAttachmentBeginInfo *imageless_info =
         vk_find_struct_const(begin_info->pNext,
                              RENDER_PASS_ATTACHMENT_BEGIN_INFO);
      assert(imageless_info);
      views = imageless_info->pAttachments;
      view_count = imageless_info->attachmentCount;
   }

   const struct vn_image **images =
      vk_alloc(&cmd->allocator, sizeof(*images) * pass->present_src_count,
               VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!images) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return;
   }

   for (uint32_t i = 0; i < pass->present_src_count; i++) {
      const uint32_t index = pass->present_src_attachments[i].index;
      assert(index < view_count);
      images[i] = vn_image_view_from_handle(views[index])->image;
   }

   if (pass->acquire_count) {
      vn_cmd_transfer_present_src_images(
         cmd, images, pass->present_src_attachments, pass->acquire_count);
   }

   cmd->builder.present_src_images = images;
}

static void
vn_cmd_end_render_pass(struct vn_command_buffer *cmd)
{
   const struct vn_render_pass *pass = cmd->builder.render_pass;

   cmd->builder.render_pass = NULL;
   cmd->builder.framebuffer = NULL;

   if (!pass->present_src_count || !cmd->builder.present_src_images)
      return;

   const struct vn_image **images = cmd->builder.present_src_images;
   cmd->builder.present_src_images = NULL;

   if (pass->release_count) {
      vn_cmd_transfer_present_src_images(
         cmd, images + pass->acquire_count,
         pass->present_src_attachments + pass->acquire_count,
         pass->release_count);
   }

   vk_free(&cmd->allocator, images);
}

/* command pool commands */

VkResult
vn_CreateCommandPool(VkDevice device,
                     const VkCommandPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkCommandPool *pCommandPool)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_command_pool *pool =
      vk_zalloc(alloc, sizeof(*pool), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&pool->base, VK_OBJECT_TYPE_COMMAND_POOL, &dev->base);

   pool->allocator = *alloc;
   pool->queue_family_index = pCreateInfo->queueFamilyIndex;
   list_inithead(&pool->command_buffers);

   VkCommandPool pool_handle = vn_command_pool_to_handle(pool);
   vn_async_vkCreateCommandPool(dev->instance, device, pCreateInfo, NULL,
                                &pool_handle);

   *pCommandPool = pool_handle;

   return VK_SUCCESS;
}

void
vn_DestroyCommandPool(VkDevice device,
                      VkCommandPool commandPool,
                      const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_command_pool *pool = vn_command_pool_from_handle(commandPool);
   const VkAllocationCallbacks *alloc;

   if (!pool)
      return;

   alloc = pAllocator ? pAllocator : &pool->allocator;

   /* We must emit vkDestroyCommandPool before freeing the command buffers in
    * pool->command_buffers.  Otherwise, another thread might reuse their
    * object ids while they still refer to the command buffers in the
    * renderer.
    */
   vn_async_vkDestroyCommandPool(dev->instance, device, commandPool, NULL);

   list_for_each_entry_safe(struct vn_command_buffer, cmd,
                            &pool->command_buffers, head) {
      vn_cs_encoder_fini(&cmd->cs);
      vn_object_base_fini(&cmd->base);
      vk_free(alloc, cmd);
   }

   vn_object_base_fini(&pool->base);
   vk_free(alloc, pool);
}

VkResult
vn_ResetCommandPool(VkDevice device,
                    VkCommandPool commandPool,
                    VkCommandPoolResetFlags flags)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_command_pool *pool = vn_command_pool_from_handle(commandPool);

   list_for_each_entry_safe(struct vn_command_buffer, cmd,
                            &pool->command_buffers, head) {
      vn_cs_encoder_reset(&cmd->cs);
      cmd->state = VN_COMMAND_BUFFER_STATE_INITIAL;
   }

   vn_async_vkResetCommandPool(dev->instance, device, commandPool, flags);

   return VK_SUCCESS;
}

void
vn_TrimCommandPool(VkDevice device,
                   VkCommandPool commandPool,
                   VkCommandPoolTrimFlags flags)
{
   struct vn_device *dev = vn_device_from_handle(device);

   vn_async_vkTrimCommandPool(dev->instance, device, commandPool, flags);
}

/* command buffer commands */

VkResult
vn_AllocateCommandBuffers(VkDevice device,
                          const VkCommandBufferAllocateInfo *pAllocateInfo,
                          VkCommandBuffer *pCommandBuffers)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_command_pool *pool =
      vn_command_pool_from_handle(pAllocateInfo->commandPool);
   const VkAllocationCallbacks *alloc = &pool->allocator;

   for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
      struct vn_command_buffer *cmd =
         vk_zalloc(alloc, sizeof(*cmd), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!cmd) {
         for (uint32_t j = 0; j < i; j++) {
            cmd = vn_command_buffer_from_handle(pCommandBuffers[j]);
            vn_cs_encoder_fini(&cmd->cs);
            list_del(&cmd->head);
            vn_object_base_fini(&cmd->base);
            vk_free(alloc, cmd);
         }
         memset(pCommandBuffers, 0,
                sizeof(*pCommandBuffers) * pAllocateInfo->commandBufferCount);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      vn_object_base_init(&cmd->base, VK_OBJECT_TYPE_COMMAND_BUFFER,
                          &dev->base);
      cmd->device = dev;
      cmd->allocator = pool->allocator;
      cmd->level = pAllocateInfo->level;
      cmd->queue_family_index = pool->queue_family_index;

      list_addtail(&cmd->head, &pool->command_buffers);

      cmd->state = VN_COMMAND_BUFFER_STATE_INITIAL;
      vn_cs_encoder_init_indirect(&cmd->cs, dev->instance, 16 * 1024);

      VkCommandBuffer cmd_handle = vn_command_buffer_to_handle(cmd);
      pCommandBuffers[i] = cmd_handle;
   }

   vn_async_vkAllocateCommandBuffers(dev->instance, device, pAllocateInfo,
                                     pCommandBuffers);

   return VK_SUCCESS;
}

void
vn_FreeCommandBuffers(VkDevice device,
                      VkCommandPool commandPool,
                      uint32_t commandBufferCount,
                      const VkCommandBuffer *pCommandBuffers)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_command_pool *pool = vn_command_pool_from_handle(commandPool);
   const VkAllocationCallbacks *alloc = &pool->allocator;

   vn_async_vkFreeCommandBuffers(dev->instance, device, commandPool,
                                 commandBufferCount, pCommandBuffers);

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      struct vn_command_buffer *cmd =
         vn_command_buffer_from_handle(pCommandBuffers[i]);

      if (!cmd)
         continue;

      if (cmd->builder.image_barriers)
         vk_free(alloc, cmd->builder.image_barriers);

      vn_cs_encoder_fini(&cmd->cs);
      list_del(&cmd->head);

      vn_object_base_fini(&cmd->base);
      vk_free(alloc, cmd);
   }
}

VkResult
vn_ResetCommandBuffer(VkCommandBuffer commandBuffer,
                      VkCommandBufferResetFlags flags)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);

   vn_cs_encoder_reset(&cmd->cs);
   cmd->state = VN_COMMAND_BUFFER_STATE_INITIAL;

   vn_async_vkResetCommandBuffer(cmd->device->instance, commandBuffer, flags);

   return VK_SUCCESS;
}

VkResult
vn_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                      const VkCommandBufferBeginInfo *pBeginInfo)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   struct vn_instance *instance = cmd->device->instance;
   size_t cmd_size;

   vn_cs_encoder_reset(&cmd->cs);

   VkCommandBufferBeginInfo local_begin_info;
   if (pBeginInfo->pInheritanceInfo &&
       cmd->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      local_begin_info = *pBeginInfo;
      local_begin_info.pInheritanceInfo = NULL;
      pBeginInfo = &local_begin_info;
   }

   cmd_size = vn_sizeof_vkBeginCommandBuffer(commandBuffer, pBeginInfo);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size)) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return vn_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   vn_encode_vkBeginCommandBuffer(&cmd->cs, 0, commandBuffer, pBeginInfo);

   cmd->state = VN_COMMAND_BUFFER_STATE_RECORDING;

   if (cmd->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
       (pBeginInfo->flags &
        VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)) {
      const VkCommandBufferInheritanceInfo *inheritance_info =
         pBeginInfo->pInheritanceInfo;
      vn_cmd_begin_render_pass(
         cmd, vn_render_pass_from_handle(inheritance_info->renderPass),
         vn_framebuffer_from_handle(inheritance_info->framebuffer), NULL);
   }

   return VK_SUCCESS;
}

static VkResult
vn_cmd_submit(struct vn_command_buffer *cmd)
{
   struct vn_instance *instance = cmd->device->instance;

   if (cmd->state != VN_COMMAND_BUFFER_STATE_RECORDING)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_cs_encoder_commit(&cmd->cs);
   if (vn_cs_encoder_get_fatal(&cmd->cs)) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      vn_cs_encoder_reset(&cmd->cs);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   vn_instance_wait_roundtrip(instance, cmd->cs.current_buffer_roundtrip);
   VkResult result = vn_instance_ring_submit(instance, &cmd->cs);
   if (result != VK_SUCCESS) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return result;
   }

   vn_cs_encoder_reset(&cmd->cs);

   return VK_SUCCESS;
}

VkResult
vn_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   struct vn_instance *instance = cmd->device->instance;
   size_t cmd_size;

   cmd_size = vn_sizeof_vkEndCommandBuffer(commandBuffer);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size)) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return vn_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   vn_encode_vkEndCommandBuffer(&cmd->cs, 0, commandBuffer);

   VkResult result = vn_cmd_submit(cmd);
   if (result != VK_SUCCESS) {
      cmd->state = VN_COMMAND_BUFFER_STATE_INVALID;
      return vn_error(instance, result);
   }

   cmd->state = VN_COMMAND_BUFFER_STATE_EXECUTABLE;

   return VK_SUCCESS;
}

void
vn_CmdBindPipeline(VkCommandBuffer commandBuffer,
                   VkPipelineBindPoint pipelineBindPoint,
                   VkPipeline pipeline)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size =
      vn_sizeof_vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBindPipeline(&cmd->cs, 0, commandBuffer, pipelineBindPoint,
                               pipeline);
}

void
vn_CmdSetViewport(VkCommandBuffer commandBuffer,
                  uint32_t firstViewport,
                  uint32_t viewportCount,
                  const VkViewport *pViewports)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdSetViewport(commandBuffer, firstViewport,
                                         viewportCount, pViewports);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetViewport(&cmd->cs, 0, commandBuffer, firstViewport,
                              viewportCount, pViewports);
}

void
vn_CmdSetScissor(VkCommandBuffer commandBuffer,
                 uint32_t firstScissor,
                 uint32_t scissorCount,
                 const VkRect2D *pScissors)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdSetScissor(commandBuffer, firstScissor,
                                        scissorCount, pScissors);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetScissor(&cmd->cs, 0, commandBuffer, firstScissor,
                             scissorCount, pScissors);
}

void
vn_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdSetLineWidth(commandBuffer, lineWidth);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetLineWidth(&cmd->cs, 0, commandBuffer, lineWidth);
}

void
vn_CmdSetDepthBias(VkCommandBuffer commandBuffer,
                   float depthBiasConstantFactor,
                   float depthBiasClamp,
                   float depthBiasSlopeFactor)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size =
      vn_sizeof_vkCmdSetDepthBias(commandBuffer, depthBiasConstantFactor,
                                  depthBiasClamp, depthBiasSlopeFactor);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetDepthBias(&cmd->cs, 0, commandBuffer,
                               depthBiasConstantFactor, depthBiasClamp,
                               depthBiasSlopeFactor);
}

void
vn_CmdSetBlendConstants(VkCommandBuffer commandBuffer,
                        const float blendConstants[4])
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdSetBlendConstants(commandBuffer, blendConstants);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetBlendConstants(&cmd->cs, 0, commandBuffer,
                                    blendConstants);
}

void
vn_CmdSetDepthBounds(VkCommandBuffer commandBuffer,
                     float minDepthBounds,
                     float maxDepthBounds)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdSetDepthBounds(commandBuffer, minDepthBounds,
                                            maxDepthBounds);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetDepthBounds(&cmd->cs, 0, commandBuffer, minDepthBounds,
                                 maxDepthBounds);
}

void
vn_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                            VkStencilFaceFlags faceMask,
                            uint32_t compareMask)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdSetStencilCompareMask(commandBuffer, faceMask,
                                                   compareMask);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetStencilCompareMask(&cmd->cs, 0, commandBuffer, faceMask,
                                        compareMask);
}

void
vn_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                          VkStencilFaceFlags faceMask,
                          uint32_t writeMask)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size =
      vn_sizeof_vkCmdSetStencilWriteMask(commandBuffer, faceMask, writeMask);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetStencilWriteMask(&cmd->cs, 0, commandBuffer, faceMask,
                                      writeMask);
}

void
vn_CmdSetStencilReference(VkCommandBuffer commandBuffer,
                          VkStencilFaceFlags faceMask,
                          uint32_t reference)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size =
      vn_sizeof_vkCmdSetStencilReference(commandBuffer, faceMask, reference);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetStencilReference(&cmd->cs, 0, commandBuffer, faceMask,
                                      reference);
}

void
vn_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                         VkPipelineBindPoint pipelineBindPoint,
                         VkPipelineLayout layout,
                         uint32_t firstSet,
                         uint32_t descriptorSetCount,
                         const VkDescriptorSet *pDescriptorSets,
                         uint32_t dynamicOffsetCount,
                         const uint32_t *pDynamicOffsets)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdBindDescriptorSets(
      commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount,
      pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBindDescriptorSets(&cmd->cs, 0, commandBuffer,
                                     pipelineBindPoint, layout, firstSet,
                                     descriptorSetCount, pDescriptorSets,
                                     dynamicOffsetCount, pDynamicOffsets);
}

void
vn_CmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                      VkBuffer buffer,
                      VkDeviceSize offset,
                      VkIndexType indexType)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdBindIndexBuffer(commandBuffer, buffer, offset,
                                             indexType);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBindIndexBuffer(&cmd->cs, 0, commandBuffer, buffer, offset,
                                  indexType);
}

void
vn_CmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                        uint32_t firstBinding,
                        uint32_t bindingCount,
                        const VkBuffer *pBuffers,
                        const VkDeviceSize *pOffsets)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdBindVertexBuffers(
      commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBindVertexBuffers(&cmd->cs, 0, commandBuffer, firstBinding,
                                    bindingCount, pBuffers, pOffsets);
}

void
vn_CmdDraw(VkCommandBuffer commandBuffer,
           uint32_t vertexCount,
           uint32_t instanceCount,
           uint32_t firstVertex,
           uint32_t firstInstance)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDraw(commandBuffer, vertexCount, instanceCount,
                                  firstVertex, firstInstance);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDraw(&cmd->cs, 0, commandBuffer, vertexCount, instanceCount,
                       firstVertex, firstInstance);
}

void
vn_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                  uint32_t indexCount,
                  uint32_t instanceCount,
                  uint32_t firstIndex,
                  int32_t vertexOffset,
                  uint32_t firstInstance)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size =
      vn_sizeof_vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount,
                                 firstIndex, vertexOffset, firstInstance);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDrawIndexed(&cmd->cs, 0, commandBuffer, indexCount,
                              instanceCount, firstIndex, vertexOffset,
                              firstInstance);
}

void
vn_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                   VkBuffer buffer,
                   VkDeviceSize offset,
                   uint32_t drawCount,
                   uint32_t stride)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDrawIndirect(commandBuffer, buffer, offset,
                                          drawCount, stride);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDrawIndirect(&cmd->cs, 0, commandBuffer, buffer, offset,
                               drawCount, stride);
}

void
vn_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                          VkBuffer buffer,
                          VkDeviceSize offset,
                          uint32_t drawCount,
                          uint32_t stride)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDrawIndexedIndirect(commandBuffer, buffer,
                                                 offset, drawCount, stride);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDrawIndexedIndirect(&cmd->cs, 0, commandBuffer, buffer,
                                      offset, drawCount, stride);
}

void
vn_CmdDrawIndirectCount(VkCommandBuffer commandBuffer,
                        VkBuffer buffer,
                        VkDeviceSize offset,
                        VkBuffer countBuffer,
                        VkDeviceSize countBufferOffset,
                        uint32_t maxDrawCount,
                        uint32_t stride)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDrawIndirectCount(commandBuffer, buffer, offset,
                                               countBuffer, countBufferOffset,
                                               maxDrawCount, stride);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDrawIndirectCount(&cmd->cs, 0, commandBuffer, buffer,
                                    offset, countBuffer, countBufferOffset,
                                    maxDrawCount, stride);
}

void
vn_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer,
                               VkBuffer buffer,
                               VkDeviceSize offset,
                               VkBuffer countBuffer,
                               VkDeviceSize countBufferOffset,
                               uint32_t maxDrawCount,
                               uint32_t stride)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDrawIndexedIndirectCount(
      commandBuffer, buffer, offset, countBuffer, countBufferOffset,
      maxDrawCount, stride);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDrawIndexedIndirectCount(
      &cmd->cs, 0, commandBuffer, buffer, offset, countBuffer,
      countBufferOffset, maxDrawCount, stride);
}

void
vn_CmdDispatch(VkCommandBuffer commandBuffer,
               uint32_t groupCountX,
               uint32_t groupCountY,
               uint32_t groupCountZ)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDispatch(commandBuffer, groupCountX, groupCountY,
                                      groupCountZ);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDispatch(&cmd->cs, 0, commandBuffer, groupCountX,
                           groupCountY, groupCountZ);
}

void
vn_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                       VkBuffer buffer,
                       VkDeviceSize offset)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDispatchIndirect(commandBuffer, buffer, offset);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDispatchIndirect(&cmd->cs, 0, commandBuffer, buffer,
                                   offset);
}

void
vn_CmdCopyBuffer(VkCommandBuffer commandBuffer,
                 VkBuffer srcBuffer,
                 VkBuffer dstBuffer,
                 uint32_t regionCount,
                 const VkBufferCopy *pRegions)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer,
                                        regionCount, pRegions);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdCopyBuffer(&cmd->cs, 0, commandBuffer, srcBuffer, dstBuffer,
                             regionCount, pRegions);
}

void
vn_CmdCopyImage(VkCommandBuffer commandBuffer,
                VkImage srcImage,
                VkImageLayout srcImageLayout,
                VkImage dstImage,
                VkImageLayout dstImageLayout,
                uint32_t regionCount,
                const VkImageCopy *pRegions)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdCopyImage(commandBuffer, srcImage,
                                       srcImageLayout, dstImage,
                                       dstImageLayout, regionCount, pRegions);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdCopyImage(&cmd->cs, 0, commandBuffer, srcImage,
                            srcImageLayout, dstImage, dstImageLayout,
                            regionCount, pRegions);
}

void
vn_CmdBlitImage(VkCommandBuffer commandBuffer,
                VkImage srcImage,
                VkImageLayout srcImageLayout,
                VkImage dstImage,
                VkImageLayout dstImageLayout,
                uint32_t regionCount,
                const VkImageBlit *pRegions,
                VkFilter filter)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdBlitImage(
      commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
      regionCount, pRegions, filter);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBlitImage(&cmd->cs, 0, commandBuffer, srcImage,
                            srcImageLayout, dstImage, dstImageLayout,
                            regionCount, pRegions, filter);
}

void
vn_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                        VkBuffer srcBuffer,
                        VkImage dstImage,
                        VkImageLayout dstImageLayout,
                        uint32_t regionCount,
                        const VkBufferImageCopy *pRegions)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size =
      vn_sizeof_vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage,
                                       dstImageLayout, regionCount, pRegions);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdCopyBufferToImage(&cmd->cs, 0, commandBuffer, srcBuffer,
                                    dstImage, dstImageLayout, regionCount,
                                    pRegions);
}

void
vn_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                        VkImage srcImage,
                        VkImageLayout srcImageLayout,
                        VkBuffer dstBuffer,
                        uint32_t regionCount,
                        const VkBufferImageCopy *pRegions)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   bool prime_blit = false;
   if (srcImageLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
       VN_PRESENT_SRC_INTERNAL_LAYOUT != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
      srcImageLayout = VN_PRESENT_SRC_INTERNAL_LAYOUT;

      /* sanity check */
      const struct vn_image *img = vn_image_from_handle(srcImage);
      prime_blit = img->is_wsi && img->is_prime_blit_src;
      assert(prime_blit);
   }

   cmd_size = vn_sizeof_vkCmdCopyImageToBuffer(commandBuffer, srcImage,
                                               srcImageLayout, dstBuffer,
                                               regionCount, pRegions);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdCopyImageToBuffer(&cmd->cs, 0, commandBuffer, srcImage,
                                    srcImageLayout, dstBuffer, regionCount,
                                    pRegions);

   if (prime_blit) {
      const VkBufferMemoryBarrier buf_barrier = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .srcQueueFamilyIndex = cmd->queue_family_index,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
         .buffer = dstBuffer,
         .size = VK_WHOLE_SIZE,
      };
      vn_cmd_encode_memory_barriers(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1,
                                    &buf_barrier, 0, NULL);
   }
}

void
vn_CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                   VkBuffer dstBuffer,
                   VkDeviceSize dstOffset,
                   VkDeviceSize dataSize,
                   const void *pData)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset,
                                          dataSize, pData);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdUpdateBuffer(&cmd->cs, 0, commandBuffer, dstBuffer,
                               dstOffset, dataSize, pData);
}

void
vn_CmdFillBuffer(VkCommandBuffer commandBuffer,
                 VkBuffer dstBuffer,
                 VkDeviceSize dstOffset,
                 VkDeviceSize size,
                 uint32_t data)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdFillBuffer(commandBuffer, dstBuffer, dstOffset,
                                        size, data);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdFillBuffer(&cmd->cs, 0, commandBuffer, dstBuffer, dstOffset,
                             size, data);
}

void
vn_CmdClearColorImage(VkCommandBuffer commandBuffer,
                      VkImage image,
                      VkImageLayout imageLayout,
                      const VkClearColorValue *pColor,
                      uint32_t rangeCount,
                      const VkImageSubresourceRange *pRanges)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdClearColorImage(
      commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdClearColorImage(&cmd->cs, 0, commandBuffer, image,
                                  imageLayout, pColor, rangeCount, pRanges);
}

void
vn_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                             VkImage image,
                             VkImageLayout imageLayout,
                             const VkClearDepthStencilValue *pDepthStencil,
                             uint32_t rangeCount,
                             const VkImageSubresourceRange *pRanges)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdClearDepthStencilImage(
      commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdClearDepthStencilImage(&cmd->cs, 0, commandBuffer, image,
                                         imageLayout, pDepthStencil,
                                         rangeCount, pRanges);
}

void
vn_CmdClearAttachments(VkCommandBuffer commandBuffer,
                       uint32_t attachmentCount,
                       const VkClearAttachment *pAttachments,
                       uint32_t rectCount,
                       const VkClearRect *pRects)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdClearAttachments(
      commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdClearAttachments(&cmd->cs, 0, commandBuffer,
                                   attachmentCount, pAttachments, rectCount,
                                   pRects);
}

void
vn_CmdResolveImage(VkCommandBuffer commandBuffer,
                   VkImage srcImage,
                   VkImageLayout srcImageLayout,
                   VkImage dstImage,
                   VkImageLayout dstImageLayout,
                   uint32_t regionCount,
                   const VkImageResolve *pRegions)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdResolveImage(
      commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
      regionCount, pRegions);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdResolveImage(&cmd->cs, 0, commandBuffer, srcImage,
                               srcImageLayout, dstImage, dstImageLayout,
                               regionCount, pRegions);
}

void
vn_CmdSetEvent(VkCommandBuffer commandBuffer,
               VkEvent event,
               VkPipelineStageFlags stageMask)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdSetEvent(commandBuffer, event, stageMask);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetEvent(&cmd->cs, 0, commandBuffer, event, stageMask);
}

void
vn_CmdResetEvent(VkCommandBuffer commandBuffer,
                 VkEvent event,
                 VkPipelineStageFlags stageMask)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdResetEvent(commandBuffer, event, stageMask);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdResetEvent(&cmd->cs, 0, commandBuffer, event, stageMask);
}

void
vn_CmdWaitEvents(VkCommandBuffer commandBuffer,
                 uint32_t eventCount,
                 const VkEvent *pEvents,
                 VkPipelineStageFlags srcStageMask,
                 VkPipelineStageFlags dstStageMask,
                 uint32_t memoryBarrierCount,
                 const VkMemoryBarrier *pMemoryBarriers,
                 uint32_t bufferMemoryBarrierCount,
                 const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                 uint32_t imageMemoryBarrierCount,
                 const VkImageMemoryBarrier *pImageMemoryBarriers)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   uint32_t transfer_count;
   pImageMemoryBarriers = vn_cmd_wait_events_fix_image_memory_barriers(
      cmd, pImageMemoryBarriers, imageMemoryBarrierCount, &transfer_count);
   imageMemoryBarrierCount -= transfer_count;

   cmd_size = vn_sizeof_vkCmdWaitEvents(
      commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask,
      memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
      pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdWaitEvents(&cmd->cs, 0, commandBuffer, eventCount, pEvents,
                             srcStageMask, dstStageMask, memoryBarrierCount,
                             pMemoryBarriers, bufferMemoryBarrierCount,
                             pBufferMemoryBarriers, imageMemoryBarrierCount,
                             pImageMemoryBarriers);

   if (transfer_count) {
      pImageMemoryBarriers += imageMemoryBarrierCount;
      vn_cmd_encode_memory_barriers(cmd, srcStageMask, dstStageMask, 0, NULL,
                                    transfer_count, pImageMemoryBarriers);
   }
}

void
vn_CmdPipelineBarrier(VkCommandBuffer commandBuffer,
                      VkPipelineStageFlags srcStageMask,
                      VkPipelineStageFlags dstStageMask,
                      VkDependencyFlags dependencyFlags,
                      uint32_t memoryBarrierCount,
                      const VkMemoryBarrier *pMemoryBarriers,
                      uint32_t bufferMemoryBarrierCount,
                      const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                      uint32_t imageMemoryBarrierCount,
                      const VkImageMemoryBarrier *pImageMemoryBarriers)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   pImageMemoryBarriers = vn_cmd_pipeline_barrier_fix_image_memory_barriers(
      cmd, pImageMemoryBarriers, imageMemoryBarrierCount);

   cmd_size = vn_sizeof_vkCmdPipelineBarrier(
      commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
      memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
      pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdPipelineBarrier(
      &cmd->cs, 0, commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
      memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
      pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

void
vn_CmdBeginQuery(VkCommandBuffer commandBuffer,
                 VkQueryPool queryPool,
                 uint32_t query,
                 VkQueryControlFlags flags)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size =
      vn_sizeof_vkCmdBeginQuery(commandBuffer, queryPool, query, flags);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBeginQuery(&cmd->cs, 0, commandBuffer, queryPool, query,
                             flags);
}

void
vn_CmdEndQuery(VkCommandBuffer commandBuffer,
               VkQueryPool queryPool,
               uint32_t query)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdEndQuery(commandBuffer, queryPool, query);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdEndQuery(&cmd->cs, 0, commandBuffer, queryPool, query);
}

void
vn_CmdResetQueryPool(VkCommandBuffer commandBuffer,
                     VkQueryPool queryPool,
                     uint32_t firstQuery,
                     uint32_t queryCount)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdResetQueryPool(commandBuffer, queryPool,
                                            firstQuery, queryCount);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdResetQueryPool(&cmd->cs, 0, commandBuffer, queryPool,
                                 firstQuery, queryCount);
}

void
vn_CmdWriteTimestamp(VkCommandBuffer commandBuffer,
                     VkPipelineStageFlagBits pipelineStage,
                     VkQueryPool queryPool,
                     uint32_t query)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdWriteTimestamp(commandBuffer, pipelineStage,
                                            queryPool, query);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdWriteTimestamp(&cmd->cs, 0, commandBuffer, pipelineStage,
                                 queryPool, query);
}

void
vn_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer,
                           VkQueryPool queryPool,
                           uint32_t firstQuery,
                           uint32_t queryCount,
                           VkBuffer dstBuffer,
                           VkDeviceSize dstOffset,
                           VkDeviceSize stride,
                           VkQueryResultFlags flags)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdCopyQueryPoolResults(
      commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset,
      stride, flags);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdCopyQueryPoolResults(&cmd->cs, 0, commandBuffer, queryPool,
                                       firstQuery, queryCount, dstBuffer,
                                       dstOffset, stride, flags);
}

void
vn_CmdPushConstants(VkCommandBuffer commandBuffer,
                    VkPipelineLayout layout,
                    VkShaderStageFlags stageFlags,
                    uint32_t offset,
                    uint32_t size,
                    const void *pValues)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdPushConstants(commandBuffer, layout, stageFlags,
                                           offset, size, pValues);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdPushConstants(&cmd->cs, 0, commandBuffer, layout,
                                stageFlags, offset, size, pValues);
}

void
vn_CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                      const VkRenderPassBeginInfo *pRenderPassBegin,
                      VkSubpassContents contents)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   vn_cmd_begin_render_pass(
      cmd, vn_render_pass_from_handle(pRenderPassBegin->renderPass),
      vn_framebuffer_from_handle(pRenderPassBegin->framebuffer),
      pRenderPassBegin);

   cmd_size = vn_sizeof_vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin,
                                             contents);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBeginRenderPass(&cmd->cs, 0, commandBuffer,
                                  pRenderPassBegin, contents);
}

void
vn_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdNextSubpass(commandBuffer, contents);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdNextSubpass(&cmd->cs, 0, commandBuffer, contents);
}

void
vn_CmdEndRenderPass(VkCommandBuffer commandBuffer)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdEndRenderPass(commandBuffer);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdEndRenderPass(&cmd->cs, 0, commandBuffer);

   vn_cmd_end_render_pass(cmd);
}

void
vn_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                       const VkRenderPassBeginInfo *pRenderPassBegin,
                       const VkSubpassBeginInfo *pSubpassBeginInfo)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   vn_cmd_begin_render_pass(
      cmd, vn_render_pass_from_handle(pRenderPassBegin->renderPass),
      vn_framebuffer_from_handle(pRenderPassBegin->framebuffer),
      pRenderPassBegin);

   cmd_size = vn_sizeof_vkCmdBeginRenderPass2(commandBuffer, pRenderPassBegin,
                                              pSubpassBeginInfo);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBeginRenderPass2(&cmd->cs, 0, commandBuffer,
                                   pRenderPassBegin, pSubpassBeginInfo);
}

void
vn_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                   const VkSubpassBeginInfo *pSubpassBeginInfo,
                   const VkSubpassEndInfo *pSubpassEndInfo)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdNextSubpass2(commandBuffer, pSubpassBeginInfo,
                                          pSubpassEndInfo);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdNextSubpass2(&cmd->cs, 0, commandBuffer, pSubpassBeginInfo,
                               pSubpassEndInfo);
}

void
vn_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                     const VkSubpassEndInfo *pSubpassEndInfo)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdEndRenderPass2(commandBuffer, pSubpassEndInfo);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdEndRenderPass2(&cmd->cs, 0, commandBuffer, pSubpassEndInfo);

   vn_cmd_end_render_pass(cmd);
}

void
vn_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                      uint32_t commandBufferCount,
                      const VkCommandBuffer *pCommandBuffers)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdExecuteCommands(
      commandBuffer, commandBufferCount, pCommandBuffers);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdExecuteCommands(&cmd->cs, 0, commandBuffer,
                                  commandBufferCount, pCommandBuffers);
}

void
vn_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdSetDeviceMask(commandBuffer, deviceMask);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdSetDeviceMask(&cmd->cs, 0, commandBuffer, deviceMask);
}

void
vn_CmdDispatchBase(VkCommandBuffer commandBuffer,
                   uint32_t baseGroupX,
                   uint32_t baseGroupY,
                   uint32_t baseGroupZ,
                   uint32_t groupCountX,
                   uint32_t groupCountY,
                   uint32_t groupCountZ)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDispatchBase(commandBuffer, baseGroupX,
                                          baseGroupY, baseGroupZ, groupCountX,
                                          groupCountY, groupCountZ);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDispatchBase(&cmd->cs, 0, commandBuffer, baseGroupX,
                               baseGroupY, baseGroupZ, groupCountX,
                               groupCountY, groupCountZ);
}

void
vn_CmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer,
                           VkQueryPool queryPool,
                           uint32_t query,
                           VkQueryControlFlags flags,
                           uint32_t index)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdBeginQueryIndexedEXT(commandBuffer, queryPool,
                                                  query, flags, index);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBeginQueryIndexedEXT(&cmd->cs, 0, commandBuffer, queryPool,
                                       query, flags, index);
}

void
vn_CmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer,
                         VkQueryPool queryPool,
                         uint32_t query,
                         uint32_t index)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdEndQueryIndexedEXT(commandBuffer, queryPool,
                                                query, index);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdEndQueryIndexedEXT(&cmd->cs, 0, commandBuffer, queryPool,
                                     query, index);
}

void
vn_CmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer,
                                      uint32_t firstBinding,
                                      uint32_t bindingCount,
                                      const VkBuffer *pBuffers,
                                      const VkDeviceSize *pOffsets,
                                      const VkDeviceSize *pSizes)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdBindTransformFeedbackBuffersEXT(
      commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBindTransformFeedbackBuffersEXT(&cmd->cs, 0, commandBuffer,
                                                  firstBinding, bindingCount,
                                                  pBuffers, pOffsets, pSizes);
}

void
vn_CmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                uint32_t firstCounterBuffer,
                                uint32_t counterBufferCount,
                                const VkBuffer *pCounterBuffers,
                                const VkDeviceSize *pCounterBufferOffsets)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdBeginTransformFeedbackEXT(
      commandBuffer, firstCounterBuffer, counterBufferCount, pCounterBuffers,
      pCounterBufferOffsets);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdBeginTransformFeedbackEXT(
      &cmd->cs, 0, commandBuffer, firstCounterBuffer, counterBufferCount,
      pCounterBuffers, pCounterBufferOffsets);
}

void
vn_CmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                              uint32_t firstCounterBuffer,
                              uint32_t counterBufferCount,
                              const VkBuffer *pCounterBuffers,
                              const VkDeviceSize *pCounterBufferOffsets)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdEndTransformFeedbackEXT(
      commandBuffer, firstCounterBuffer, counterBufferCount, pCounterBuffers,
      pCounterBufferOffsets);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdEndTransformFeedbackEXT(
      &cmd->cs, 0, commandBuffer, firstCounterBuffer, counterBufferCount,
      pCounterBuffers, pCounterBufferOffsets);
}

void
vn_CmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer,
                               uint32_t instanceCount,
                               uint32_t firstInstance,
                               VkBuffer counterBuffer,
                               VkDeviceSize counterBufferOffset,
                               uint32_t counterOffset,
                               uint32_t vertexStride)
{
   struct vn_command_buffer *cmd =
      vn_command_buffer_from_handle(commandBuffer);
   size_t cmd_size;

   cmd_size = vn_sizeof_vkCmdDrawIndirectByteCountEXT(
      commandBuffer, instanceCount, firstInstance, counterBuffer,
      counterBufferOffset, counterOffset, vertexStride);
   if (!vn_cs_encoder_reserve(&cmd->cs, cmd_size))
      return;

   vn_encode_vkCmdDrawIndirectByteCountEXT(
      &cmd->cs, 0, commandBuffer, instanceCount, firstInstance, counterBuffer,
      counterBufferOffset, counterOffset, vertexStride);
}
