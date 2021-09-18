/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include "gen_macros.h"

#include "panvk_private.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "util/mesa-sha1.h"
#include "vk_descriptors.h"
#include "vk_util.h"

#include "pan_bo.h"
#include "panvk_cs.h"

static VkResult
panvk_per_arch(descriptor_set_create)(struct panvk_device *device,
                                      struct panvk_descriptor_pool *pool,
                                      const struct panvk_descriptor_set_layout *layout,
                                      struct panvk_descriptor_set **out_set)
{
   struct panvk_descriptor_set *set;

   /* TODO: Allocate from the pool! */
   set = vk_object_zalloc(&device->vk, NULL,
                          sizeof(struct panvk_descriptor_set),
                          VK_OBJECT_TYPE_DESCRIPTOR_SET);
   if (!set)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   set->layout = layout;
   set->descs = vk_alloc(&device->vk.alloc,
                         sizeof(*set->descs) * layout->num_descs, 8,
                         VK_OBJECT_TYPE_DESCRIPTOR_SET);
   if (!set->descs)
      goto err_free_set;

   if (layout->num_ubos) {
      set->ubos = vk_zalloc(&device->vk.alloc,
                            pan_size(UNIFORM_BUFFER) * layout->num_ubos, 8,
                            VK_OBJECT_TYPE_DESCRIPTOR_SET);
      if (!set->ubos)
         goto err_free_set;
   }

   if (layout->num_samplers) {
      set->samplers = vk_zalloc(&device->vk.alloc,
                                pan_size(SAMPLER) * layout->num_samplers, 8,
                                VK_OBJECT_TYPE_DESCRIPTOR_SET);
      if (!set->samplers)
         goto err_free_set;
   }

   if (layout->num_textures) {
      set->textures =
         vk_zalloc(&device->vk.alloc,
                   (PAN_ARCH >= 6 ? pan_size(TEXTURE) : sizeof(mali_ptr)) *
                   layout->num_textures,
                   8, VK_OBJECT_TYPE_DESCRIPTOR_SET);
      if (!set->textures)
         goto err_free_set;
   }

   for (unsigned i = 0; i < layout->binding_count; i++) {
      if (!layout->bindings[i].immutable_samplers)
         continue;

      for (unsigned j = 0; j < layout->bindings[i].array_size; j++) {
         set->descs[layout->bindings[i].desc_idx].image.sampler =
            layout->bindings[i].immutable_samplers[j];
      }
   }

   *out_set = set;
   return VK_SUCCESS;

err_free_set:
   vk_free(&device->vk.alloc, set->textures);
   vk_free(&device->vk.alloc, set->samplers);
   vk_free(&device->vk.alloc, set->ubos);
   vk_free(&device->vk.alloc, set->descs);
   vk_object_free(&device->vk, NULL, set);
   return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
}

VkResult
panvk_per_arch(AllocateDescriptorSets)(VkDevice _device,
                                       const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                       VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, pAllocateInfo->descriptorPool);
   VkResult result;
   unsigned i;

   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      VK_FROM_HANDLE(panvk_descriptor_set_layout, layout,
                     pAllocateInfo->pSetLayouts[i]);
      struct panvk_descriptor_set *set = NULL;

      result = panvk_per_arch(descriptor_set_create)(device, pool, layout, &set);
      if (result != VK_SUCCESS)
         goto err_free_sets;

      pDescriptorSets[i] = panvk_descriptor_set_to_handle(set);
   }

   return VK_SUCCESS;

err_free_sets:
   panvk_FreeDescriptorSets(_device, pAllocateInfo->descriptorPool, i, pDescriptorSets);
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++)
      pDescriptorSets[i] = VK_NULL_HANDLE;

   return result; 
}

static void
panvk_set_image_desc(struct panvk_descriptor *desc,
                     const VkDescriptorImageInfo *pImageInfo)
{
   VK_FROM_HANDLE(panvk_sampler, sampler, pImageInfo->sampler);
   VK_FROM_HANDLE(panvk_image_view, image_view, pImageInfo->imageView);
   desc->image.sampler = sampler;
   desc->image.view = image_view;
   desc->image.layout = pImageInfo->imageLayout;
}

static void
panvk_set_texel_buffer_view_desc(struct panvk_descriptor *desc,
                                 const VkBufferView *pTexelBufferView)
{
   VK_FROM_HANDLE(panvk_buffer_view, buffer_view, *pTexelBufferView);
   desc->buffer_view = buffer_view;
}

static void
panvk_set_buffer_info_desc(struct panvk_descriptor *desc,
                           const VkDescriptorBufferInfo *pBufferInfo)
{
   VK_FROM_HANDLE(panvk_buffer, buffer, pBufferInfo->buffer);
   desc->buffer_info.buffer = buffer;
   desc->buffer_info.offset = pBufferInfo->offset;
   desc->buffer_info.range = pBufferInfo->range;
}

static void
panvk_per_arch(set_ubo_desc)(void *ubo,
                             const VkDescriptorBufferInfo *pBufferInfo)
{
   VK_FROM_HANDLE(panvk_buffer, buffer, pBufferInfo->buffer);
   size_t size = pBufferInfo->range == VK_WHOLE_SIZE ?
                 (buffer->bo->size - pBufferInfo->offset) :
                 pBufferInfo->range;

   panvk_per_arch(emit_ubo)(buffer->bo->ptr.gpu + pBufferInfo->offset, size,  ubo);
}

static void
panvk_set_sampler_desc(void *desc,
                       const VkDescriptorImageInfo *pImageInfo)
{
   VK_FROM_HANDLE(panvk_sampler, sampler, pImageInfo->sampler);

   memcpy(desc, &sampler->desc, sizeof(sampler->desc));
}

static void
panvk_per_arch(set_texture_desc)(struct panvk_descriptor_set *set,
                                 unsigned idx,
                                 const VkDescriptorImageInfo *pImageInfo)
{
   VK_FROM_HANDLE(panvk_image_view, view, pImageInfo->imageView);

#if PAN_ARCH > 5
   memcpy(&((struct mali_bifrost_texture_packed *)set->textures)[idx],
          view->desc, pan_size(TEXTURE));
#else
   ((mali_ptr *)set->textures)[idx] = view->bo->ptr.gpu;
#endif
}

static void
panvk_per_arch(write_descriptor_set)(struct panvk_device *dev,
                                     const VkWriteDescriptorSet *pDescriptorWrite)
{
   VK_FROM_HANDLE(panvk_descriptor_set, set, pDescriptorWrite->dstSet);
   const struct panvk_descriptor_set_layout *layout = set->layout;
   unsigned dest_offset = pDescriptorWrite->dstArrayElement;
   unsigned binding = pDescriptorWrite->dstBinding;
   struct mali_uniform_buffer_packed *ubos = set->ubos;
   struct mali_midgard_sampler_packed *samplers = set->samplers;
   unsigned src_offset = 0;

   while (src_offset < pDescriptorWrite->descriptorCount &&
          binding < layout->binding_count) {
      const struct panvk_descriptor_set_binding_layout *binding_layout =
         &layout->bindings[binding];

      if (!binding_layout->array_size) {
         binding++;
         dest_offset = 0;
         continue;
      }

      assert(pDescriptorWrite->descriptorType == binding_layout->type);
      unsigned ndescs = MIN2(pDescriptorWrite->descriptorCount - src_offset,
                             binding_layout->array_size - dest_offset);
      struct panvk_descriptor *descs = &set->descs[binding_layout->desc_idx + dest_offset];
      assert(binding_layout->desc_idx + dest_offset + ndescs <= set->layout->num_descs);

      switch (pDescriptorWrite->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         for (unsigned i = 0; i < ndescs; i++) {
            const VkDescriptorImageInfo *info = &pDescriptorWrite->pImageInfo[src_offset + i];

            if ((pDescriptorWrite->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                 pDescriptorWrite->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) &&
                !binding_layout->immutable_samplers) {
               unsigned sampler = binding_layout->sampler_idx + dest_offset + i;

               panvk_set_sampler_desc(&samplers[sampler], info);
            }

            if (pDescriptorWrite->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                pDescriptorWrite->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
               unsigned tex = binding_layout->tex_idx + dest_offset + i;

               panvk_per_arch(set_texture_desc)(set, tex, info);
            }
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (unsigned i = 0; i < ndescs; i++)
            panvk_set_image_desc(&descs[i], &pDescriptorWrite->pImageInfo[src_offset + i]);
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (unsigned i = 0; i < ndescs; i++)
            panvk_set_texel_buffer_view_desc(&descs[i], &pDescriptorWrite->pTexelBufferView[src_offset + i]);
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         for (unsigned i = 0; i < ndescs; i++) {
            unsigned ubo = binding_layout->ubo_idx + dest_offset + i;
            panvk_per_arch(set_ubo_desc)(&ubos[ubo],
                                         &pDescriptorWrite->pBufferInfo[src_offset + i]);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (unsigned i = 0; i < ndescs; i++)
            panvk_set_buffer_info_desc(&descs[i], &pDescriptorWrite->pBufferInfo[src_offset + i]);
         break;
      default:
         unreachable("Invalid type");
      }

      src_offset += ndescs;
      binding++;
      dest_offset = 0;
   }
}

static void
panvk_copy_descriptor_set(struct panvk_device *dev,
                          const VkCopyDescriptorSet *pDescriptorCopy)
{
   VK_FROM_HANDLE(panvk_descriptor_set, dest_set, pDescriptorCopy->dstSet);
   VK_FROM_HANDLE(panvk_descriptor_set, src_set, pDescriptorCopy->srcSet);
   const struct panvk_descriptor_set_layout *dest_layout = dest_set->layout;
   const struct panvk_descriptor_set_layout *src_layout = dest_set->layout;
   unsigned dest_offset = pDescriptorCopy->dstArrayElement;
   unsigned src_offset = pDescriptorCopy->srcArrayElement;
   unsigned dest_binding = pDescriptorCopy->dstBinding;
   unsigned src_binding = pDescriptorCopy->srcBinding;
   unsigned desc_count = pDescriptorCopy->descriptorCount;

   while (desc_count && src_binding < src_layout->binding_count &&
          dest_binding < dest_layout->binding_count) {
      const struct panvk_descriptor_set_binding_layout *dest_binding_layout =
         &src_layout->bindings[dest_binding];

      if (!dest_binding_layout->array_size) {
         dest_binding++;
         dest_offset = 0;
         continue;
      }

      const struct panvk_descriptor_set_binding_layout *src_binding_layout =
         &src_layout->bindings[src_binding];

      if (!src_binding_layout->array_size) {
         src_binding++;
         src_offset = 0;
         continue;
      }

      assert(dest_binding_layout->type == src_binding_layout->type);

      unsigned ndescs = MAX3(desc_count,
                             dest_binding_layout->array_size - dest_offset,
                             src_binding_layout->array_size - src_offset);

      struct panvk_descriptor *dest_descs = dest_set->descs + dest_binding_layout->desc_idx + dest_offset;
      struct panvk_descriptor *src_descs = src_set->descs + src_binding_layout->desc_idx + src_offset;
      memcpy(dest_descs, src_descs, ndescs * sizeof(*dest_descs));
      desc_count -= ndescs;
      dest_offset += ndescs;
      if (dest_offset == dest_binding_layout->array_size) {
         dest_binding++;
         dest_offset = 0;
         continue;
      }
      src_offset += ndescs;
      if (src_offset == src_binding_layout->array_size) {
         src_binding++;
         src_offset = 0;
         continue;
      }
   }

   assert(!desc_count);
}

void
panvk_per_arch(UpdateDescriptorSets)(VkDevice _device,
                                     uint32_t descriptorWriteCount,
                                     const VkWriteDescriptorSet *pDescriptorWrites,
                                     uint32_t descriptorCopyCount,
                                     const VkCopyDescriptorSet *pDescriptorCopies)
{
   VK_FROM_HANDLE(panvk_device, dev, _device);

   for (unsigned i = 0; i < descriptorWriteCount; i++)
      panvk_per_arch(write_descriptor_set)(dev, &pDescriptorWrites[i]);
   for (unsigned i = 0; i < descriptorCopyCount; i++)
      panvk_copy_descriptor_set(dev, &pDescriptorCopies[i]);
}
