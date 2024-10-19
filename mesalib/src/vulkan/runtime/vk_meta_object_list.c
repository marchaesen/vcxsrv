/*
 * Copyright 2022 Collabora Ltd
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vk_meta_object_list.h"
#include "vk_device.h"

void
vk_meta_destroy_object(struct vk_device *device, struct vk_object_base *obj)
{
   const struct vk_device_dispatch_table *disp = &device->dispatch_table;
   VkDevice _device = vk_device_to_handle(device);

   switch (obj->type) {
   case VK_OBJECT_TYPE_BUFFER:
      disp->DestroyBuffer(_device, (VkBuffer)(uintptr_t)obj, NULL);
      break;
   case VK_OBJECT_TYPE_BUFFER_VIEW:
      disp->DestroyBufferView(_device, (VkBufferView)(uintptr_t)obj, NULL);
      break;
   case VK_OBJECT_TYPE_IMAGE_VIEW:
      disp->DestroyImageView(_device, (VkImageView)(uintptr_t)obj, NULL);
      break;
   case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
      disp->DestroyDescriptorSetLayout(_device, (VkDescriptorSetLayout)(uintptr_t)obj, NULL);
      break;
   case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
      disp->DestroyPipelineLayout(_device, (VkPipelineLayout)(uintptr_t)obj, NULL);
      break;
   case VK_OBJECT_TYPE_PIPELINE:
      disp->DestroyPipeline(_device, (VkPipeline)(uintptr_t)obj, NULL);
      break;
   case VK_OBJECT_TYPE_SAMPLER:
      disp->DestroySampler(_device, (VkSampler)(uintptr_t)obj, NULL);
      break;
   case VK_OBJECT_TYPE_SHADER_EXT:
      disp->DestroyShaderEXT(_device, (VkShaderEXT)(uintptr_t)obj, NULL);
      break;
   default:
      unreachable("Unsupported object type");
   }
}


void
vk_meta_object_list_init(struct vk_meta_object_list *mol)
{
   util_dynarray_init(&mol->arr, NULL);
}

void
vk_meta_object_list_reset(struct vk_device *device,
                          struct vk_meta_object_list *mol)
{
   util_dynarray_foreach(&mol->arr, struct vk_object_base *, obj)
      vk_meta_destroy_object(device, *obj);

   util_dynarray_clear(&mol->arr);
}

void
vk_meta_object_list_finish(struct vk_device *device,
                           struct vk_meta_object_list *mol)
{
   vk_meta_object_list_reset(device, mol);
   util_dynarray_fini(&mol->arr);
}
