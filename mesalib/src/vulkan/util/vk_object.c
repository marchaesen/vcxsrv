/*
 * Copyright © 2020 Intel Corporation
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

#include "vk_object.h"

#include "vk_alloc.h"
#include "util/hash_table.h"
#include "util/ralloc.h"

void
vk_object_base_init(UNUSED struct vk_device *device,
                    struct vk_object_base *base,
                    UNUSED VkObjectType obj_type)
{
   base->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   base->type = obj_type;
   util_sparse_array_init(&base->private_data, sizeof(uint64_t), 8);
}

void
vk_object_base_finish(struct vk_object_base *base)
{
   util_sparse_array_finish(&base->private_data);
}

void
vk_device_init(struct vk_device *device,
               UNUSED const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *instance_alloc,
               const VkAllocationCallbacks *device_alloc)
{
   vk_object_base_init(device, &device->base, VK_OBJECT_TYPE_DEVICE);
   if (device_alloc)
      device->alloc = *device_alloc;
   else
      device->alloc = *instance_alloc;

   p_atomic_set(&device->private_data_next_index, 0);

#ifdef ANDROID
   mtx_init(&device->swapchain_private_mtx, mtx_plain);
   device->swapchain_private = NULL;
#endif /* ANDROID */
}

void
vk_device_finish(UNUSED struct vk_device *device)
{
#ifdef ANDROID
   if (device->swapchain_private) {
      hash_table_foreach(device->swapchain_private, entry)
         util_sparse_array_finish(entry->data);
      ralloc_free(device->swapchain_private);
   }
#endif /* ANDROID */

   vk_object_base_finish(&device->base);
}

void *
vk_object_alloc(struct vk_device *device,
                const VkAllocationCallbacks *alloc,
                size_t size,
                VkObjectType obj_type)
{
   void *ptr = vk_alloc2(&device->alloc, alloc, size, 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (ptr == NULL)
      return NULL;

   vk_object_base_init(device, (struct vk_object_base *)ptr, obj_type);

   return ptr;
}

void *
vk_object_zalloc(struct vk_device *device,
                const VkAllocationCallbacks *alloc,
                size_t size,
                VkObjectType obj_type)
{
   void *ptr = vk_zalloc2(&device->alloc, alloc, size, 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (ptr == NULL)
      return NULL;

   vk_object_base_init(device, (struct vk_object_base *)ptr, obj_type);

   return ptr;
}

void
vk_object_free(struct vk_device *device,
               const VkAllocationCallbacks *alloc,
               void *data)
{
   vk_object_base_finish((struct vk_object_base *)data);
   vk_free2(&device->alloc, alloc, data);
}

VkResult
vk_private_data_slot_create(struct vk_device *device,
                            const VkPrivateDataSlotCreateInfoEXT* pCreateInfo,
                            const VkAllocationCallbacks* pAllocator,
                            VkPrivateDataSlotEXT* pPrivateDataSlot)
{
   struct vk_private_data_slot *slot =
      vk_alloc2(&device->alloc, pAllocator, sizeof(*slot), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (slot == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vk_object_base_init(device, &slot->base,
                       VK_OBJECT_TYPE_PRIVATE_DATA_SLOT_EXT);
   slot->index = p_atomic_inc_return(&device->private_data_next_index);

   *pPrivateDataSlot = vk_private_data_slot_to_handle(slot);

   return VK_SUCCESS;
}

void
vk_private_data_slot_destroy(struct vk_device *device,
                             VkPrivateDataSlotEXT privateDataSlot,
                             const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(vk_private_data_slot, slot, privateDataSlot);
   if (slot == NULL)
      return;

   vk_object_base_finish(&slot->base);
   vk_free2(&device->alloc, pAllocator, slot);
}

#ifdef ANDROID
static VkResult
get_swapchain_private_data_locked(struct vk_device *device,
                                  uint64_t objectHandle,
                                  struct vk_private_data_slot *slot,
                                  uint64_t **private_data)
{
   if (unlikely(device->swapchain_private == NULL)) {
      /* Even though VkSwapchain is a non-dispatchable object, we know a
       * priori that Android swapchains are actually pointers so we can use
       * the pointer hash table for them.
       */
      device->swapchain_private = _mesa_pointer_hash_table_create(NULL);
      if (device->swapchain_private == NULL)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   struct hash_entry *entry =
      _mesa_hash_table_search(device->swapchain_private,
                              (void *)(uintptr_t)objectHandle);
   if (unlikely(entry == NULL)) {
      struct util_sparse_array *swapchain_private =
         ralloc(device->swapchain_private, struct util_sparse_array);
      util_sparse_array_init(swapchain_private, sizeof(uint64_t), 8);

      entry = _mesa_hash_table_insert(device->swapchain_private,
                                      (void *)(uintptr_t)objectHandle,
                                      swapchain_private);
      if (entry == NULL)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   struct util_sparse_array *swapchain_private = entry->data;
   *private_data = util_sparse_array_get(swapchain_private, slot->index);

   return VK_SUCCESS;
}
#endif /* ANDROID */

static VkResult
vk_object_base_private_data(struct vk_device *device,
                            VkObjectType objectType,
                            uint64_t objectHandle,
                            VkPrivateDataSlotEXT privateDataSlot,
                            uint64_t **private_data)
{
   VK_FROM_HANDLE(vk_private_data_slot, slot, privateDataSlot);

#ifdef ANDROID
   /* There is an annoying spec corner here on Android.  Because WSI is
    * implemented in the Vulkan loader which doesn't know about the
    * VK_EXT_private_data extension, we have to handle VkSwapchainKHR in the
    * driver as a special case.  On future versions of Android where the
    * loader does understand VK_EXT_private_data, we'll never see a
    * vkGet/SetPrivateDataEXT call on a swapchain because the loader will
    * handle it.
    */
   if (objectType == VK_OBJECT_TYPE_SWAPCHAIN_KHR) {
      mtx_lock(&device->swapchain_private_mtx);
      VkResult result = get_swapchain_private_data_locked(device, objectHandle,
                                                          slot, private_data);
      mtx_unlock(&device->swapchain_private_mtx);
      return result;
   }
#endif /* ANDROID */

   struct vk_object_base *obj =
      vk_object_base_from_u64_handle(objectHandle, objectType);
   *private_data = util_sparse_array_get(&obj->private_data, slot->index);

   return VK_SUCCESS;
}

VkResult
vk_object_base_set_private_data(struct vk_device *device,
                                VkObjectType objectType,
                                uint64_t objectHandle,
                                VkPrivateDataSlotEXT privateDataSlot,
                                uint64_t data)
{
   uint64_t *private_data;
   VkResult result = vk_object_base_private_data(device,
                                                 objectType, objectHandle,
                                                 privateDataSlot,
                                                 &private_data);
   if (unlikely(result != VK_SUCCESS))
      return result;

   *private_data = data;
   return VK_SUCCESS;
}

void
vk_object_base_get_private_data(struct vk_device *device,
                                VkObjectType objectType,
                                uint64_t objectHandle,
                                VkPrivateDataSlotEXT privateDataSlot,
                                uint64_t *pData)
{
   uint64_t *private_data;
   VkResult result = vk_object_base_private_data(device,
                                                 objectType, objectHandle,
                                                 privateDataSlot,
                                                 &private_data);
   if (likely(result == VK_SUCCESS)) {
      *pData = *private_data;
   } else {
      *pData = 0;
   }
}
