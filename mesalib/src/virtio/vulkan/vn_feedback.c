/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vn_feedback.h"

#include "vn_device.h"
#include "vn_physical_device.h"
#include "vn_queue.h"

/* coherent buffer with bound and mapped memory */
struct vn_feedback_buffer {
   VkBuffer buffer;
   VkDeviceMemory memory;
   void *data;

   struct list_head head;
};

static uint32_t
vn_get_memory_type_index(const VkPhysicalDeviceMemoryProperties *mem_props,
                         uint32_t mem_type_bits,
                         VkMemoryPropertyFlags required_mem_flags)
{
   u_foreach_bit(mem_type_index, mem_type_bits)
   {
      assert(mem_type_index < mem_props->memoryTypeCount);
      if ((mem_props->memoryTypes[mem_type_index].propertyFlags &
           required_mem_flags) == required_mem_flags)
         return mem_type_index;
   }

   return UINT32_MAX;
}

static VkResult
vn_feedback_buffer_create(struct vn_device *dev,
                          uint32_t size,
                          const VkAllocationCallbacks *alloc,
                          struct vn_feedback_buffer **out_feedback_buf)
{
   const bool exclusive = dev->queue_family_count == 1;
   const VkPhysicalDeviceMemoryProperties *mem_props =
      &dev->physical_device->memory_properties.memoryProperties;
   VkDevice dev_handle = vn_device_to_handle(dev);
   struct vn_feedback_buffer *feedback_buf;
   VkResult result;

   feedback_buf = vk_zalloc(alloc, sizeof(*feedback_buf), VN_DEFAULT_ALIGN,
                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!feedback_buf)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* use concurrent to avoid explicit queue family ownership transfer for
    * device created with queues from multiple queue families
    */
   const VkBufferCreateInfo buf_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode =
         exclusive ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
      /* below favors the current venus protocol */
      .queueFamilyIndexCount = exclusive ? 0 : dev->queue_family_count,
      .pQueueFamilyIndices = exclusive ? NULL : dev->queue_families,
   };
   result = vn_CreateBuffer(dev_handle, &buf_create_info, alloc,
                            &feedback_buf->buffer);
   if (result != VK_SUCCESS)
      goto out_free_feedback_buf;

   struct vn_buffer *buf = vn_buffer_from_handle(feedback_buf->buffer);
   const VkMemoryRequirements *mem_req =
      &buf->requirements.memory.memoryRequirements;
   const uint32_t mem_type_index =
      vn_get_memory_type_index(mem_props, mem_req->memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   if (mem_type_index >= mem_props->memoryTypeCount) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto out_destroy_buffer;
   }

   const VkMemoryAllocateInfo mem_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_req->size,
      .memoryTypeIndex = mem_type_index,
   };
   result = vn_AllocateMemory(dev_handle, &mem_alloc_info, alloc,
                              &feedback_buf->memory);
   if (result != VK_SUCCESS)
      goto out_destroy_buffer;

   const VkBindBufferMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
      .buffer = feedback_buf->buffer,
      .memory = feedback_buf->memory,
      .memoryOffset = 0,
   };
   result = vn_BindBufferMemory2(dev_handle, 1, &bind_info);
   if (result != VK_SUCCESS)
      goto out_free_memory;

   result = vn_MapMemory(dev_handle, feedback_buf->memory, 0, VK_WHOLE_SIZE,
                         0, &feedback_buf->data);
   if (result != VK_SUCCESS)
      goto out_free_memory;

   *out_feedback_buf = feedback_buf;

   return VK_SUCCESS;

out_free_memory:
   vn_FreeMemory(dev_handle, feedback_buf->memory, alloc);

out_destroy_buffer:
   vn_DestroyBuffer(dev_handle, feedback_buf->buffer, alloc);

out_free_feedback_buf:
   vk_free(alloc, feedback_buf);

   return result;
}

static void
vn_feedback_buffer_destroy(struct vn_device *dev,
                           struct vn_feedback_buffer *feedback_buf,
                           const VkAllocationCallbacks *alloc)
{
   VkDevice dev_handle = vn_device_to_handle(dev);

   vn_UnmapMemory(dev_handle, feedback_buf->memory);
   vn_FreeMemory(dev_handle, feedback_buf->memory, alloc);
   vn_DestroyBuffer(dev_handle, feedback_buf->buffer, alloc);
   vk_free(alloc, feedback_buf);
}

static VkResult
vn_feedback_pool_grow_locked(struct vn_feedback_pool *pool)
{
   VN_TRACE_FUNC();
   struct vn_feedback_buffer *feedback_buf = NULL;
   VkResult result;

   result = vn_feedback_buffer_create(pool->device, pool->size, pool->alloc,
                                      &feedback_buf);
   if (result != VK_SUCCESS)
      return result;

   pool->used = 0;

   list_add(&feedback_buf->head, &pool->feedback_buffers);

   return VK_SUCCESS;
}

VkResult
vn_feedback_pool_init(struct vn_device *dev,
                      struct vn_feedback_pool *pool,
                      uint32_t size,
                      const VkAllocationCallbacks *alloc)
{
   simple_mtx_init(&pool->mutex, mtx_plain);

   pool->device = dev;
   pool->alloc = alloc;
   pool->size = size;
   pool->used = size;
   list_inithead(&pool->feedback_buffers);
   list_inithead(&pool->free_slots);

   return VK_SUCCESS;
}

void
vn_feedback_pool_fini(struct vn_feedback_pool *pool)
{
   list_for_each_entry_safe(struct vn_feedback_slot, slot, &pool->free_slots,
                            head)
      vk_free(pool->alloc, slot);

   list_for_each_entry_safe(struct vn_feedback_buffer, feedback_buf,
                            &pool->feedback_buffers, head)
      vn_feedback_buffer_destroy(pool->device, feedback_buf, pool->alloc);

   simple_mtx_destroy(&pool->mutex);
}

static struct vn_feedback_buffer *
vn_feedback_pool_alloc_locked(struct vn_feedback_pool *pool,
                              uint32_t size,
                              uint32_t *out_offset)
{
   VN_TRACE_FUNC();
   const uint32_t aligned_size = align(size, 4);

   if (unlikely(aligned_size > pool->size - pool->used)) {
      VkResult result = vn_feedback_pool_grow_locked(pool);
      if (result != VK_SUCCESS)
         return NULL;

      assert(aligned_size <= pool->size - pool->used);
   }

   *out_offset = pool->used;
   pool->used += aligned_size;

   return list_first_entry(&pool->feedback_buffers, struct vn_feedback_buffer,
                           head);
}

struct vn_feedback_slot *
vn_feedback_pool_alloc(struct vn_feedback_pool *pool,
                       enum vn_feedback_type type)
{
   /* TODO Make slot size variable for VkQueryPool feedback. Currently it's
    * MAX2(sizeof(VkResult), sizeof(uint64_t)).
    */
   static const uint32_t slot_size = 8;
   struct vn_feedback_buffer *feedback_buf;
   uint32_t offset;
   struct vn_feedback_slot *slot;

   simple_mtx_lock(&pool->mutex);
   if (!list_is_empty(&pool->free_slots)) {
      slot =
         list_first_entry(&pool->free_slots, struct vn_feedback_slot, head);
      list_del(&slot->head);
      simple_mtx_unlock(&pool->mutex);

      slot->type = type;
      return slot;
   }

   slot = vk_alloc(pool->alloc, sizeof(*slot), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!slot) {
      simple_mtx_unlock(&pool->mutex);
      return NULL;
   }

   feedback_buf = vn_feedback_pool_alloc_locked(pool, slot_size, &offset);
   simple_mtx_unlock(&pool->mutex);

   if (!feedback_buf) {
      vk_free(pool->alloc, slot);
      return NULL;
   }

   slot->type = type;
   slot->offset = offset;
   slot->buffer = feedback_buf->buffer;
   slot->data = feedback_buf->data + offset;

   return slot;
}

void
vn_feedback_pool_free(struct vn_feedback_pool *pool,
                      struct vn_feedback_slot *slot)
{
   simple_mtx_lock(&pool->mutex);
   list_add(&slot->head, &pool->free_slots);
   simple_mtx_unlock(&pool->mutex);
}

void
vn_feedback_event_cmd_record(VkCommandBuffer cmd_handle,
                             VkEvent ev_handle,
                             VkPipelineStageFlags stage_mask,
                             VkResult status)
{
   /* For vkCmdSetEvent and vkCmdResetEvent feedback interception.
    *
    * The injection point is after the event call to avoid introducing
    * unexpected src stage waiting for VK_PIPELINE_STAGE_HOST_BIT and
    * VK_PIPELINE_STAGE_TRANSFER_BIT if they are not already being waited by
    * vkCmdSetEvent or vkCmdResetEvent. On the other hand, the delay in the
    * feedback signal is acceptable for the nature of VkEvent, and the event
    * feedback cmds lifecycle is guarded by the intercepted command buffer.
    */
   struct vn_event *ev = vn_event_from_handle(ev_handle);
   struct vn_feedback_slot *slot = ev->feedback_slot;

   if (!slot)
      return;

   STATIC_ASSERT(sizeof(*slot->status) == 4);

   const VkBufferMemoryBarrier buf_barrier_before = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask =
         VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = slot->buffer,
      .offset = slot->offset,
      .size = 4,
   };
   vn_CmdPipelineBarrier(cmd_handle,
                         stage_mask | VK_PIPELINE_STAGE_HOST_BIT |
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                         &buf_barrier_before, 0, NULL);
   vn_CmdFillBuffer(cmd_handle, slot->buffer, slot->offset, 4, status);

   const VkBufferMemoryBarrier buf_barrier_after = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = slot->buffer,
      .offset = slot->offset,
      .size = 4,
   };
   vn_CmdPipelineBarrier(cmd_handle, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1,
                         &buf_barrier_after, 0, NULL);
}

static VkResult
vn_feedback_fence_cmd_record(VkCommandBuffer cmd_handle,
                             struct vn_feedback_slot *slot)

{
   STATIC_ASSERT(sizeof(*slot->status) == 4);

   static const VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = NULL,
      .flags = 0,
      .pInheritanceInfo = NULL,
   };
   VkResult result = vn_BeginCommandBuffer(cmd_handle, &begin_info);
   if (result != VK_SUCCESS)
      return result;

   static const VkMemoryBarrier mem_barrier_before = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .pNext = NULL,
      /* make pending writes available to stay close to fence signal op */
      .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
      /* no need to make all memory visible for feedback update */
      .dstAccessMask = 0,
   };
   const VkBufferMemoryBarrier buf_barrier_before = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .pNext = NULL,
      /* slot memory has been made available via mem_barrier_before */
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = slot->buffer,
      .offset = slot->offset,
      .size = 4,
   };
   vn_CmdPipelineBarrier(cmd_handle, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                         &mem_barrier_before, 1, &buf_barrier_before, 0,
                         NULL);
   vn_CmdFillBuffer(cmd_handle, slot->buffer, slot->offset, 4, VK_SUCCESS);

   const VkBufferMemoryBarrier buf_barrier_after = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = slot->buffer,
      .offset = slot->offset,
      .size = 4,
   };
   vn_CmdPipelineBarrier(cmd_handle, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1,
                         &buf_barrier_after, 0, NULL);

   return vn_EndCommandBuffer(cmd_handle);
}

VkResult
vn_feedback_fence_cmd_alloc(VkDevice dev_handle,
                            struct vn_feedback_cmd_pool *pool,
                            struct vn_feedback_slot *slot,
                            VkCommandBuffer *out_cmd_handle)
{
   const VkCommandBufferAllocateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = NULL,
      .commandPool = pool->pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer cmd_handle;
   VkResult result;

   simple_mtx_lock(&pool->mutex);
   result = vn_AllocateCommandBuffers(dev_handle, &info, &cmd_handle);
   if (result != VK_SUCCESS)
      goto out_unlock;

   result = vn_feedback_fence_cmd_record(cmd_handle, slot);
   if (result != VK_SUCCESS) {
      vn_FreeCommandBuffers(dev_handle, pool->pool, 1, &cmd_handle);
      goto out_unlock;
   }

   *out_cmd_handle = cmd_handle;

out_unlock:
   simple_mtx_unlock(&pool->mutex);

   return result;
}

void
vn_feedback_fence_cmd_free(VkDevice dev_handle,
                           struct vn_feedback_cmd_pool *pool,
                           VkCommandBuffer cmd_handle)
{
   simple_mtx_lock(&pool->mutex);
   vn_FreeCommandBuffers(dev_handle, pool->pool, 1, &cmd_handle);
   simple_mtx_unlock(&pool->mutex);
}

VkResult
vn_feedback_cmd_pools_init(struct vn_device *dev)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;
   VkDevice dev_handle = vn_device_to_handle(dev);
   struct vn_feedback_cmd_pool *pools;
   VkCommandPoolCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
   };

   /* TODO will also condition on timeline semaphore feedback */
   if (VN_PERF(NO_FENCE_FEEDBACK))
      return VK_SUCCESS;

   assert(dev->queue_family_count);

   pools = vk_zalloc(alloc, sizeof(*pools) * dev->queue_family_count,
                     VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!pools)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < dev->queue_family_count; i++) {
      VkResult result;

      info.queueFamilyIndex = dev->queue_families[i];
      result = vn_CreateCommandPool(dev_handle, &info, alloc, &pools[i].pool);
      if (result != VK_SUCCESS) {
         for (uint32_t j = 0; j < i; j++) {
            vn_DestroyCommandPool(dev_handle, pools[j].pool, alloc);
            simple_mtx_destroy(&pools[j].mutex);
         }

         vk_free(alloc, pools);
         return result;
      }

      simple_mtx_init(&pools[i].mutex, mtx_plain);
   }

   dev->cmd_pools = pools;

   return VK_SUCCESS;
}

void
vn_feedback_cmd_pools_fini(struct vn_device *dev)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;
   VkDevice dev_handle = vn_device_to_handle(dev);

   if (!dev->cmd_pools)
      return;

   for (uint32_t i = 0; i < dev->queue_family_count; i++) {
      vn_DestroyCommandPool(dev_handle, dev->cmd_pools[i].pool, alloc);
      simple_mtx_destroy(&dev->cmd_pools[i].mutex);
   }

   vk_free(alloc, dev->cmd_pools);
}
