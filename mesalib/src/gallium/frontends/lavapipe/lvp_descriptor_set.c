/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"
#include "vk_descriptors.h"
#include "vk_util.h"
#include "u_math.h"

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateDescriptorSetLayout(
    VkDevice                                    _device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_descriptor_set_layout *set_layout;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
   uint32_t num_bindings = 0;
   uint32_t immutable_sampler_count = 0;
   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      num_bindings = MAX2(num_bindings, pCreateInfo->pBindings[j].binding + 1);
      /* From the Vulkan 1.1.97 spec for VkDescriptorSetLayoutBinding:
       *
       *    "If descriptorType specifies a VK_DESCRIPTOR_TYPE_SAMPLER or
       *    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER type descriptor, then
       *    pImmutableSamplers can be used to initialize a set of immutable
       *    samplers. [...]  If descriptorType is not one of these descriptor
       *    types, then pImmutableSamplers is ignored.
       *
       * We need to be careful here and only parse pImmutableSamplers if we
       * have one of the right descriptor types.
       */
      VkDescriptorType desc_type = pCreateInfo->pBindings[j].descriptorType;
      if ((desc_type == VK_DESCRIPTOR_TYPE_SAMPLER ||
           desc_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) &&
          pCreateInfo->pBindings[j].pImmutableSamplers)
         immutable_sampler_count += pCreateInfo->pBindings[j].descriptorCount;
   }

   size_t size = sizeof(struct lvp_descriptor_set_layout) +
                 num_bindings * sizeof(set_layout->binding[0]) +
                 immutable_sampler_count * sizeof(struct lvp_sampler *);

   set_layout = vk_descriptor_set_layout_zalloc(&device->vk, size);
   if (!set_layout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   set_layout->immutable_sampler_count = immutable_sampler_count;
   /* We just allocate all the samplers at the end of the struct */
   struct pipe_sampler_state **samplers =
      (struct pipe_sampler_state **)&set_layout->binding[num_bindings];

   set_layout->binding_count = num_bindings;
   set_layout->shader_stages = 0;
   set_layout->size = 0;

   VkDescriptorSetLayoutBinding *bindings = NULL;
   VkResult result = vk_create_sorted_bindings(pCreateInfo->pBindings,
                                               pCreateInfo->bindingCount,
                                               &bindings);
   if (result != VK_SUCCESS) {
      vk_descriptor_set_layout_unref(&device->vk, &set_layout->vk);
      return vk_error(device, result);
   }

   uint32_t dynamic_offset_count = 0;
   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      const VkDescriptorSetLayoutBinding *binding = bindings + j;
      uint32_t b = binding->binding;

      set_layout->binding[b].array_size = binding->descriptorCount;
      set_layout->binding[b].descriptor_index = set_layout->size;
      set_layout->binding[b].type = binding->descriptorType;
      set_layout->binding[b].valid = true;
      if (binding->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
         set_layout->size++;
      else
         set_layout->size += binding->descriptorCount;

      for (gl_shader_stage stage = MESA_SHADER_VERTEX; stage < MESA_SHADER_STAGES; stage++) {
         set_layout->binding[b].stage[stage].const_buffer_index = -1;
         set_layout->binding[b].stage[stage].shader_buffer_index = -1;
         set_layout->binding[b].stage[stage].sampler_index = -1;
         set_layout->binding[b].stage[stage].sampler_view_index = -1;
         set_layout->binding[b].stage[stage].image_index = -1;
         set_layout->binding[b].stage[stage].uniform_block_index = -1;
      }

      if (binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
          binding->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
         set_layout->binding[b].dynamic_index = dynamic_offset_count;
         dynamic_offset_count += binding->descriptorCount;
      }
      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         lvp_foreach_stage(s, binding->stageFlags) {
            set_layout->binding[b].stage[s].sampler_index = set_layout->stage[s].sampler_count;
            set_layout->stage[s].sampler_count += binding->descriptorCount;
         }
         if (binding->pImmutableSamplers) {
            set_layout->binding[b].immutable_samplers = samplers;
            samplers += binding->descriptorCount;

            for (uint32_t i = 0; i < binding->descriptorCount; i++) {
               if (binding->pImmutableSamplers[i])
                  set_layout->binding[b].immutable_samplers[i] =
                     &lvp_sampler_from_handle(binding->pImmutableSamplers[i])->state;
               else
                  set_layout->binding[b].immutable_samplers[i] = NULL;
            }
         }
         break;
      default:
         break;
      }

      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         lvp_foreach_stage(s, binding->stageFlags) {
            set_layout->binding[b].stage[s].const_buffer_index = set_layout->stage[s].const_buffer_count;
            set_layout->stage[s].const_buffer_count += binding->descriptorCount;
         }
        break;
      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         lvp_foreach_stage(s, binding->stageFlags) {
            set_layout->binding[b].stage[s].uniform_block_offset = set_layout->stage[s].uniform_block_size;
            set_layout->binding[b].stage[s].uniform_block_index = set_layout->stage[s].uniform_block_count;
            set_layout->stage[s].uniform_block_size += binding->descriptorCount;
            set_layout->stage[s].uniform_block_sizes[set_layout->stage[s].uniform_block_count++] = binding->descriptorCount;
         }
        break;
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         lvp_foreach_stage(s, binding->stageFlags) {
            set_layout->binding[b].stage[s].shader_buffer_index = set_layout->stage[s].shader_buffer_count;
            set_layout->stage[s].shader_buffer_count += binding->descriptorCount;
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         lvp_foreach_stage(s, binding->stageFlags) {
            set_layout->binding[b].stage[s].image_index = set_layout->stage[s].image_count;
            set_layout->stage[s].image_count += binding->descriptorCount;
         }
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         lvp_foreach_stage(s, binding->stageFlags) {
            set_layout->binding[b].stage[s].sampler_view_index = set_layout->stage[s].sampler_view_count;
            set_layout->stage[s].sampler_view_count += binding->descriptorCount;
         }
         break;
      default:
         break;
      }

      set_layout->shader_stages |= binding->stageFlags;
   }

#ifndef NDEBUG
   /* this otherwise crashes later and is annoying to track down */
   unsigned array[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_COMPUTE_BIT,
   };
   for (unsigned i = 0; i <= MESA_SHADER_COMPUTE; i++) {
      uint16_t const_buffer_count = 0;
      uint16_t shader_buffer_count = 0;
      uint16_t sampler_count = 0;
      uint16_t sampler_view_count = 0;
      uint16_t image_count = 0;
      if (set_layout->shader_stages & array[i]) {
         const_buffer_count += set_layout->stage[i].const_buffer_count;
         shader_buffer_count += set_layout->stage[i].shader_buffer_count;
         sampler_count += set_layout->stage[i].sampler_count;
         sampler_view_count += set_layout->stage[i].sampler_view_count;
         image_count += set_layout->stage[i].image_count;
      }
      assert(const_buffer_count <= device->physical_device->device_limits.maxPerStageDescriptorUniformBuffers);
      assert(shader_buffer_count <= device->physical_device->device_limits.maxPerStageDescriptorStorageBuffers);
      assert(sampler_count <= device->physical_device->device_limits.maxPerStageDescriptorSamplers);
      assert(sampler_view_count <= device->physical_device->device_limits.maxPerStageDescriptorSampledImages);
      assert(image_count <= device->physical_device->device_limits.maxPerStageDescriptorStorageImages);
   }
#endif

   free(bindings);

   set_layout->dynamic_offset_count = dynamic_offset_count;

   *pSetLayout = lvp_descriptor_set_layout_to_handle(set_layout);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreatePipelineLayout(
    VkDevice                                    _device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_pipeline_layout *layout;

   layout = vk_pipeline_layout_zalloc(&device->vk, sizeof(*layout),
                                      pCreateInfo);

   for (uint32_t set = 0; set < layout->vk.set_count; set++) {
      if (layout->vk.set_layouts[set] == NULL)
         continue;

      const struct lvp_descriptor_set_layout *set_layout =
         vk_to_lvp_descriptor_set_layout(layout->vk.set_layouts[set]);

      for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
         layout->stage[i].uniform_block_size += set_layout->stage[i].uniform_block_size;
         for (unsigned j = 0; j < set_layout->stage[i].uniform_block_count; j++) {
            assert(layout->stage[i].uniform_block_count + j < MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BLOCKS * MAX_SETS);
            layout->stage[i].uniform_block_sizes[layout->stage[i].uniform_block_count + j] = set_layout->stage[i].uniform_block_sizes[j];
         }
         layout->stage[i].uniform_block_count += set_layout->stage[i].uniform_block_count;
      }
   }

#ifndef NDEBUG
   /* this otherwise crashes later and is annoying to track down */
   unsigned array[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_COMPUTE_BIT,
   };
   for (unsigned i = 0; i <= MESA_SHADER_COMPUTE; i++) {
      uint16_t const_buffer_count = 0;
      uint16_t shader_buffer_count = 0;
      uint16_t sampler_count = 0;
      uint16_t sampler_view_count = 0;
      uint16_t image_count = 0;
      for (unsigned j = 0; j < layout->vk.set_count; j++) {
         if (layout->vk.set_layouts[j] == NULL)
            continue;

         const struct lvp_descriptor_set_layout *set_layout =
            vk_to_lvp_descriptor_set_layout(layout->vk.set_layouts[j]);

         if (set_layout->shader_stages & array[i]) {
            const_buffer_count += set_layout->stage[i].const_buffer_count;
            shader_buffer_count += set_layout->stage[i].shader_buffer_count;
            sampler_count += set_layout->stage[i].sampler_count;
            sampler_view_count += set_layout->stage[i].sampler_view_count;
            image_count += set_layout->stage[i].image_count;
         }
      }
      assert(const_buffer_count <= device->physical_device->device_limits.maxPerStageDescriptorUniformBuffers);
      assert(shader_buffer_count <= device->physical_device->device_limits.maxPerStageDescriptorStorageBuffers);
      assert(sampler_count <= device->physical_device->device_limits.maxPerStageDescriptorSamplers);
      assert(sampler_view_count <= device->physical_device->device_limits.maxPerStageDescriptorSampledImages);
      assert(image_count <= device->physical_device->device_limits.maxPerStageDescriptorStorageImages);
   }
#endif

   layout->push_constant_size = 0;
   for (unsigned i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
      const VkPushConstantRange *range = pCreateInfo->pPushConstantRanges + i;
      layout->push_constant_size = MAX2(layout->push_constant_size,
                                        range->offset + range->size);
      layout->push_constant_stages |= (range->stageFlags & BITFIELD_MASK(MESA_SHADER_STAGES));
   }
   layout->push_constant_size = align(layout->push_constant_size, 16);
   *pPipelineLayout = lvp_pipeline_layout_to_handle(layout);

   return VK_SUCCESS;
}

VkResult
lvp_descriptor_set_create(struct lvp_device *device,
                          struct lvp_descriptor_set_layout *layout,
                          struct lvp_descriptor_set **out_set)
{
   struct lvp_descriptor_set *set;
   size_t base_size = sizeof(*set) + layout->size * sizeof(set->descriptors[0]);
   size_t size = base_size;
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
      size += layout->stage[i].uniform_block_size;
   set = vk_alloc(&device->vk.alloc /* XXX: Use the pool */, size, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!set)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* A descriptor set may not be 100% filled. Clear the set so we can can
    * later detect holes in it.
    */
   memset(set, 0, size);

   vk_object_base_init(&device->vk, &set->base,
                       VK_OBJECT_TYPE_DESCRIPTOR_SET);
   set->layout = layout;
   vk_descriptor_set_layout_ref(&layout->vk);

   /* Go through and fill out immutable samplers if we have any */
   struct lvp_descriptor *desc = set->descriptors;
   uint8_t *uniform_mem = (uint8_t*)(set) + base_size;
   for (uint32_t b = 0; b < layout->binding_count; b++) {
      if (layout->binding[b].type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         desc->info.uniform = uniform_mem;
         uniform_mem += layout->binding[b].array_size;
         desc++;
      } else {
         if (layout->binding[b].immutable_samplers) {
            for (uint32_t i = 0; i < layout->binding[b].array_size; i++)
               desc[i].info.sampler = layout->binding[b].immutable_samplers[i];
         }
         desc += layout->binding[b].array_size;
      }
   }

   *out_set = set;

   return VK_SUCCESS;
}

void
lvp_descriptor_set_destroy(struct lvp_device *device,
                           struct lvp_descriptor_set *set)
{
   vk_descriptor_set_layout_unref(&device->vk, &set->layout->vk);
   vk_object_base_finish(&set->base);
   vk_free(&device->vk.alloc, set);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_AllocateDescriptorSets(
    VkDevice                                    _device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_descriptor_pool, pool, pAllocateInfo->descriptorPool);
   VkResult result = VK_SUCCESS;
   struct lvp_descriptor_set *set;
   uint32_t i;

   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      LVP_FROM_HANDLE(lvp_descriptor_set_layout, layout,
                      pAllocateInfo->pSetLayouts[i]);

      result = lvp_descriptor_set_create(device, layout, &set);
      if (result != VK_SUCCESS)
         break;

      list_addtail(&set->link, &pool->sets);
      pDescriptorSets[i] = lvp_descriptor_set_to_handle(set);
   }

   if (result != VK_SUCCESS)
      lvp_FreeDescriptorSets(_device, pAllocateInfo->descriptorPool,
                             i, pDescriptorSets);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_FreeDescriptorSets(
    VkDevice                                    _device,
    VkDescriptorPool                            descriptorPool,
    uint32_t                                    count,
    const VkDescriptorSet*                      pDescriptorSets)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   for (uint32_t i = 0; i < count; i++) {
      LVP_FROM_HANDLE(lvp_descriptor_set, set, pDescriptorSets[i]);

      if (!set)
         continue;
      list_del(&set->link);
      lvp_descriptor_set_destroy(device, set);
   }
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_UpdateDescriptorSets(
    VkDevice                                    _device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies)
{
   for (uint32_t i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *write = &pDescriptorWrites[i];
      LVP_FROM_HANDLE(lvp_descriptor_set, set, write->dstSet);
      const struct lvp_descriptor_set_binding_layout *bind_layout =
         &set->layout->binding[write->dstBinding];
      struct lvp_descriptor *desc =
         &set->descriptors[bind_layout->descriptor_index];
      if (write->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         const VkWriteDescriptorSetInlineUniformBlock *uniform_data =
            vk_find_struct_const(write->pNext, WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK);
         assert(uniform_data);
         desc->type = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
         memcpy(desc->info.uniform + write->dstArrayElement, uniform_data->pData, uniform_data->dataSize);
         continue;
      }
      desc += write->dstArrayElement;

      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            LVP_FROM_HANDLE(lvp_sampler, sampler,
                            write->pImageInfo[j].sampler);

            desc[j] = (struct lvp_descriptor) {
               .type = VK_DESCRIPTOR_TYPE_SAMPLER,
               .info.sampler = &sampler->state,
            };
         }
         break;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            LVP_FROM_HANDLE(lvp_image_view, iview,
                            write->pImageInfo[j].imageView);
            desc[j].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            desc[j].info.sampler_view = iview ? iview->sv : NULL;
            /*
             * All consecutive bindings updated via a single VkWriteDescriptorSet structure, except those
             * with a descriptorCount of zero, must all either use immutable samplers or must all not
             * use immutable samplers
             */
            if (bind_layout->immutable_samplers) {
               desc[j].info.sampler = bind_layout->immutable_samplers[j];
            } else {
               LVP_FROM_HANDLE(lvp_sampler, sampler,
                               write->pImageInfo[j].sampler);

               desc[j].info.sampler = &sampler->state;
            }
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            LVP_FROM_HANDLE(lvp_image_view, iview,
                            write->pImageInfo[j].imageView);

            desc[j] = (struct lvp_descriptor) {
               .type = write->descriptorType,
               .info.sampler_view = iview ? iview->sv : NULL,
            };
         }
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            LVP_FROM_HANDLE(lvp_image_view, iview,
                            write->pImageInfo[j].imageView);

            desc[j] = (struct lvp_descriptor) {
               .type = write->descriptorType,
               .info.image_view = iview ? iview->iv : ((struct pipe_image_view){0}),
            };
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            LVP_FROM_HANDLE(lvp_buffer_view, bview,
                            write->pTexelBufferView[j]);

            desc[j] = (struct lvp_descriptor) {
               .type = write->descriptorType,
               .info.sampler_view = bview ? bview->sv : NULL,
            };
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            LVP_FROM_HANDLE(lvp_buffer_view, bview,
                            write->pTexelBufferView[j]);

            desc[j] = (struct lvp_descriptor) {
               .type = write->descriptorType,
               .info.image_view = bview ? bview->iv : ((struct pipe_image_view){0}),
            };
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            LVP_FROM_HANDLE(lvp_buffer, buffer, write->pBufferInfo[j].buffer);
            desc[j] = (struct lvp_descriptor) {
               .type = write->descriptorType,
               .info.ubo.buffer_offset = buffer ? write->pBufferInfo[j].offset : 0,
               .info.ubo.buffer = buffer ? buffer->bo : NULL,
               .info.ubo.buffer_size = buffer ? write->pBufferInfo[j].range : 0,
            };
            if (buffer && write->pBufferInfo[j].range == VK_WHOLE_SIZE)
               desc[j].info.ubo.buffer_size = buffer->bo->width0 - desc[j].info.ubo.buffer_offset;
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            LVP_FROM_HANDLE(lvp_buffer, buffer, write->pBufferInfo[j].buffer);
            desc[j] = (struct lvp_descriptor) {
               .type = write->descriptorType,
               .info.ssbo.buffer_offset = buffer ? write->pBufferInfo[j].offset : 0,
               .info.ssbo.buffer = buffer ? buffer->bo : NULL,
               .info.ssbo.buffer_size = buffer ? write->pBufferInfo[j].range : 0,
            };
            if (buffer && write->pBufferInfo[j].range == VK_WHOLE_SIZE)
               desc[j].info.ssbo.buffer_size = buffer->bo->width0 - desc[j].info.ssbo.buffer_offset;
         }
         break;

      default:
         break;
      }
   }

   for (uint32_t i = 0; i < descriptorCopyCount; i++) {
      const VkCopyDescriptorSet *copy = &pDescriptorCopies[i];
      LVP_FROM_HANDLE(lvp_descriptor_set, src, copy->srcSet);
      LVP_FROM_HANDLE(lvp_descriptor_set, dst, copy->dstSet);

      const struct lvp_descriptor_set_binding_layout *src_layout =
         &src->layout->binding[copy->srcBinding];
      struct lvp_descriptor *src_desc =
         &src->descriptors[src_layout->descriptor_index];

      const struct lvp_descriptor_set_binding_layout *dst_layout =
         &dst->layout->binding[copy->dstBinding];
      struct lvp_descriptor *dst_desc =
         &dst->descriptors[dst_layout->descriptor_index];

      if (src_desc->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         dst_desc->type = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
         memcpy(dst_desc->info.uniform + copy->dstArrayElement,
                src_desc->info.uniform + copy->srcArrayElement,
                copy->descriptorCount);
      } else {
         src_desc += copy->srcArrayElement;
         dst_desc += copy->dstArrayElement;

         for (uint32_t j = 0; j < copy->descriptorCount; j++)
            dst_desc[j] = src_desc[j];
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateDescriptorPool(
    VkDevice                                    _device,
    const VkDescriptorPoolCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorPool*                           pDescriptorPool)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_descriptor_pool *pool;
   size_t size = sizeof(struct lvp_descriptor_pool);
   pool = vk_zalloc2(&device->vk.alloc, pAllocator, size, 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pool->base,
                       VK_OBJECT_TYPE_DESCRIPTOR_POOL);
   pool->flags = pCreateInfo->flags;
   list_inithead(&pool->sets);
   *pDescriptorPool = lvp_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

static void lvp_reset_descriptor_pool(struct lvp_device *device,
                                      struct lvp_descriptor_pool *pool)
{
   struct lvp_descriptor_set *set, *tmp;
   LIST_FOR_EACH_ENTRY_SAFE(set, tmp, &pool->sets, link) {
      vk_descriptor_set_layout_unref(&device->vk, &set->layout->vk);
      list_del(&set->link);
      vk_free(&device->vk.alloc, set);
   }
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyDescriptorPool(
    VkDevice                                    _device,
    VkDescriptorPool                            _pool,
    const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_descriptor_pool, pool, _pool);

   if (!_pool)
      return;

   lvp_reset_descriptor_pool(device, pool);
   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_ResetDescriptorPool(
    VkDevice                                    _device,
    VkDescriptorPool                            _pool,
    VkDescriptorPoolResetFlags                  flags)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_descriptor_pool, pool, _pool);

   lvp_reset_descriptor_pool(device, pool);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetDescriptorSetLayoutSupport(VkDevice device,
                                       const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
                                       VkDescriptorSetLayoutSupport* pSupport)
{
   pSupport->supported = true;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateDescriptorUpdateTemplate(VkDevice _device,
                                            const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                            const VkAllocationCallbacks *pAllocator,
                                            VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   const uint32_t entry_count = pCreateInfo->descriptorUpdateEntryCount;
   const size_t size = sizeof(struct lvp_descriptor_update_template) +
      sizeof(VkDescriptorUpdateTemplateEntry) * entry_count;

   struct lvp_descriptor_update_template *templ;

   templ = vk_alloc(&device->vk.alloc, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!templ)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &templ->base,
                       VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);

   templ->ref_cnt = 1;
   templ->type = pCreateInfo->templateType;
   templ->bind_point = pCreateInfo->pipelineBindPoint;
   templ->set = pCreateInfo->set;
   /* This parameter is ignored if templateType is not VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR */
   if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR)
      templ->pipeline_layout = lvp_pipeline_layout_from_handle(pCreateInfo->pipelineLayout);
   else
      templ->pipeline_layout = NULL;
   templ->entry_count = entry_count;

   VkDescriptorUpdateTemplateEntry *entries = (VkDescriptorUpdateTemplateEntry *)(templ + 1);
   for (unsigned i = 0; i < entry_count; i++) {
      entries[i] = pCreateInfo->pDescriptorUpdateEntries[i];
   }

   *pDescriptorUpdateTemplate = lvp_descriptor_update_template_to_handle(templ);
   return VK_SUCCESS;
}

void
lvp_descriptor_template_destroy(struct lvp_device *device, struct lvp_descriptor_update_template *templ)
{
   if (!templ)
      return;

   vk_object_base_finish(&templ->base);
   vk_free(&device->vk.alloc, templ);
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyDescriptorUpdateTemplate(VkDevice _device,
                                         VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                         const VkAllocationCallbacks *pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_descriptor_update_template, templ, descriptorUpdateTemplate);
   lvp_descriptor_template_templ_unref(device, templ);
}

VKAPI_ATTR void VKAPI_CALL lvp_UpdateDescriptorSetWithTemplate(VkDevice _device,
                                         VkDescriptorSet descriptorSet,
                                         VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                         const void *pData)
{
   LVP_FROM_HANDLE(lvp_descriptor_set, set, descriptorSet);
   LVP_FROM_HANDLE(lvp_descriptor_update_template, templ, descriptorUpdateTemplate);
   uint32_t i, j;

   for (i = 0; i < templ->entry_count; ++i) {
      VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];
      const uint8_t *pSrc = ((const uint8_t *) pData) + entry->offset;
      const struct lvp_descriptor_set_binding_layout *bind_layout =
         &set->layout->binding[entry->dstBinding];
      struct lvp_descriptor *desc =
         &set->descriptors[bind_layout->descriptor_index];
      if (entry->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         desc->type = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
         memcpy(desc->info.uniform + entry->dstArrayElement, pSrc, entry->descriptorCount);
         continue;
      }
      for (j = 0; j < entry->descriptorCount; ++j) {
         unsigned idx = j + entry->dstArrayElement;
         switch (entry->descriptorType) {
         case VK_DESCRIPTOR_TYPE_SAMPLER: {
            LVP_FROM_HANDLE(lvp_sampler, sampler,
                            *(VkSampler *)pSrc);
            desc[idx] = (struct lvp_descriptor) {
               .type = VK_DESCRIPTOR_TYPE_SAMPLER,
               .info.sampler = &sampler->state,
            };
            break;
         }
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            VkDescriptorImageInfo *info = (VkDescriptorImageInfo *)pSrc;
            LVP_FROM_HANDLE(lvp_image_view, iview, info->imageView);
            desc[idx] = (struct lvp_descriptor) {
               .type = entry->descriptorType,
               .info.sampler_view = iview ? iview->sv : NULL,
               .info.sampler = info->sampler ? &lvp_sampler_from_handle(info->sampler)->state : NULL,
            };
            break;
         }
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
            VkDescriptorImageInfo *info = (VkDescriptorImageInfo *)pSrc;
            LVP_FROM_HANDLE(lvp_image_view, iview, info->imageView);
            desc[idx] = (struct lvp_descriptor) {
               .type = entry->descriptorType,
               .info.sampler_view = iview ? iview->sv : NULL,
            };
            break;
         }
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            LVP_FROM_HANDLE(lvp_image_view, iview,
                            ((VkDescriptorImageInfo *)pSrc)->imageView);
            desc[idx] = (struct lvp_descriptor) {
               .type = entry->descriptorType,
               .info.image_view = iview ? iview->iv : ((struct pipe_image_view){0}),
            };
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: {
            LVP_FROM_HANDLE(lvp_buffer_view, bview,
                            *(VkBufferView *)pSrc);
            desc[idx] = (struct lvp_descriptor) {
               .type = entry->descriptorType,
               .info.sampler_view = bview ? bview->sv : NULL,
            };
            break;
         }
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
            LVP_FROM_HANDLE(lvp_buffer_view, bview,
                            *(VkBufferView *)pSrc);
            desc[idx] = (struct lvp_descriptor) {
               .type = entry->descriptorType,
               .info.image_view = bview ? bview->iv : ((struct pipe_image_view){0}),
            };
            break;
         }

         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: {
            VkDescriptorBufferInfo *info = (VkDescriptorBufferInfo *)pSrc;
            LVP_FROM_HANDLE(lvp_buffer, buffer, info->buffer);
            desc[idx] = (struct lvp_descriptor) {
               .type = entry->descriptorType,
               .info.ubo.buffer_offset = buffer ? info->offset : 0,
               .info.ubo.buffer = buffer ? buffer->bo : NULL,
               .info.ubo.buffer_size = buffer ? info->range : 0,
            };
            if (buffer && info->range == VK_WHOLE_SIZE)
               desc[idx].info.ubo.buffer_size = buffer->bo->width0 - desc[idx].info.ubo.buffer_offset;
            break;
         }

         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            VkDescriptorBufferInfo *info = (VkDescriptorBufferInfo *)pSrc;
            LVP_FROM_HANDLE(lvp_buffer, buffer, info->buffer);
            desc[idx] = (struct lvp_descriptor) {
               .type = entry->descriptorType,
               .info.ssbo.buffer_offset = buffer ? info->offset : 0,
               .info.ssbo.buffer = buffer ? buffer->bo : NULL,
               .info.ssbo.buffer_size =  buffer ? info->range : 0,
            };
            if (buffer && info->range == VK_WHOLE_SIZE)
               desc[idx].info.ssbo.buffer_size = buffer->bo->width0 - desc[idx].info.ssbo.buffer_offset;
            break;
         }
         default:
            break;
         }
         pSrc += entry->stride;
      }
   }
}
