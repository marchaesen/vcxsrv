/*
 * Copyright © 2019 Raspberry Pi
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

#include "vk_descriptors.h"
#include "vk_util.h"

#include "v3dv_private.h"

/*
 * For a given descriptor defined by the descriptor_set it belongs, its
 * binding layout, and array_index, it returns the map region assigned to it
 * from the descriptor pool bo.
 */
static void*
descriptor_bo_map(struct v3dv_device *device,
                  struct v3dv_descriptor_set *set,
                  const struct v3dv_descriptor_set_binding_layout *binding_layout,
                  uint32_t array_index)
{
   assert(v3dv_X(device, descriptor_bo_size)(binding_layout->type) > 0);
   return set->pool->bo->map +
      set->base_offset + binding_layout->descriptor_offset +
      array_index * v3dv_X(device, descriptor_bo_size)(binding_layout->type);
}

static bool
descriptor_type_is_dynamic(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return true;
      break;
   default:
      return false;
   }
}

/*
 * Tries to get a real descriptor using a descriptor map index from the
 * descriptor_state + pipeline_layout.
 */
struct v3dv_descriptor *
v3dv_descriptor_map_get_descriptor(struct v3dv_descriptor_state *descriptor_state,
                                   struct v3dv_descriptor_map *map,
                                   struct v3dv_pipeline_layout *pipeline_layout,
                                   uint32_t index,
                                   uint32_t *dynamic_offset)
{
   assert(index < map->num_desc);

   uint32_t set_number = map->set[index];
   assert((descriptor_state->valid & 1 << set_number));

   struct v3dv_descriptor_set *set =
      descriptor_state->descriptor_sets[set_number];
   assert(set);

   uint32_t binding_number = map->binding[index];
   assert(binding_number < set->layout->binding_count);

   const struct v3dv_descriptor_set_binding_layout *binding_layout =
      &set->layout->binding[binding_number];

   uint32_t array_index = map->array_index[index];
   assert(array_index < binding_layout->array_size);

   if (descriptor_type_is_dynamic(binding_layout->type)) {
      uint32_t dynamic_offset_index =
         pipeline_layout->set[set_number].dynamic_offset_start +
         binding_layout->dynamic_offset_index + array_index;

      *dynamic_offset = descriptor_state->dynamic_offsets[dynamic_offset_index];
   }

   return &set->descriptors[binding_layout->descriptor_index + array_index];
}

/* Equivalent to map_get_descriptor but it returns a reloc with the bo
 * associated with that descriptor (suballocation of the descriptor pool bo)
 *
 * It also returns the descriptor type, so the caller could do extra
 * validation or adding extra offsets if the bo contains more that one field.
 */
static struct v3dv_cl_reloc
v3dv_descriptor_map_get_descriptor_bo(struct v3dv_device *device,
                                      struct v3dv_descriptor_state *descriptor_state,
                                      struct v3dv_descriptor_map *map,
                                      struct v3dv_pipeline_layout *pipeline_layout,
                                      uint32_t index,
                                      VkDescriptorType *out_type)
{
   assert(index < map->num_desc);

   uint32_t set_number = map->set[index];
   assert(descriptor_state->valid & 1 << set_number);

   struct v3dv_descriptor_set *set =
      descriptor_state->descriptor_sets[set_number];
   assert(set);

   uint32_t binding_number = map->binding[index];
   assert(binding_number < set->layout->binding_count);

   const struct v3dv_descriptor_set_binding_layout *binding_layout =
      &set->layout->binding[binding_number];

   assert(v3dv_X(device, descriptor_bo_size)(binding_layout->type) > 0);
   *out_type = binding_layout->type;

   uint32_t array_index = map->array_index[index];
   assert(array_index < binding_layout->array_size);

   struct v3dv_cl_reloc reloc = {
      .bo = set->pool->bo,
      .offset = set->base_offset + binding_layout->descriptor_offset +
      array_index * v3dv_X(device, descriptor_bo_size)(binding_layout->type),
   };

   return reloc;
}

/*
 * The difference between this method and v3dv_descriptor_map_get_descriptor,
 * is that if the sampler are added as immutable when creating the set layout,
 * they are bound to the set layout, so not part of the descriptor per
 * se. This method return early in that case.
 */
const struct v3dv_sampler *
v3dv_descriptor_map_get_sampler(struct v3dv_descriptor_state *descriptor_state,
                                struct v3dv_descriptor_map *map,
                                struct v3dv_pipeline_layout *pipeline_layout,
                                uint32_t index)
{
   assert(index < map->num_desc);

   uint32_t set_number = map->set[index];
   assert(descriptor_state->valid & 1 << set_number);

   struct v3dv_descriptor_set *set =
      descriptor_state->descriptor_sets[set_number];
   assert(set);

   uint32_t binding_number = map->binding[index];
   assert(binding_number < set->layout->binding_count);

   const struct v3dv_descriptor_set_binding_layout *binding_layout =
      &set->layout->binding[binding_number];

   uint32_t array_index = map->array_index[index];
   assert(array_index < binding_layout->array_size);

   if (binding_layout->immutable_samplers_offset != 0) {
      assert(binding_layout->type == VK_DESCRIPTOR_TYPE_SAMPLER ||
             binding_layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

      const struct v3dv_sampler *immutable_samplers =
         v3dv_immutable_samplers(set->layout, binding_layout);

      assert(immutable_samplers);
      const struct v3dv_sampler *sampler = &immutable_samplers[array_index];
      assert(sampler);

      return sampler;
   }

   struct v3dv_descriptor *descriptor =
      &set->descriptors[binding_layout->descriptor_index + array_index];

   assert(descriptor->type == VK_DESCRIPTOR_TYPE_SAMPLER ||
          descriptor->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

   assert(descriptor->sampler);

   return descriptor->sampler;
}


struct v3dv_cl_reloc
v3dv_descriptor_map_get_sampler_state(struct v3dv_device *device,
                                      struct v3dv_descriptor_state *descriptor_state,
                                      struct v3dv_descriptor_map *map,
                                      struct v3dv_pipeline_layout *pipeline_layout,
                                      uint32_t index)
{
   VkDescriptorType type;
   struct v3dv_cl_reloc reloc =
      v3dv_descriptor_map_get_descriptor_bo(device, descriptor_state, map,
                                            pipeline_layout,
                                            index, &type);

   assert(type == VK_DESCRIPTOR_TYPE_SAMPLER ||
          type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

   if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      reloc.offset += v3dv_X(device, combined_image_sampler_sampler_state_offset)();

   return reloc;
}

struct v3dv_bo*
v3dv_descriptor_map_get_texture_bo(struct v3dv_descriptor_state *descriptor_state,
                                   struct v3dv_descriptor_map *map,
                                   struct v3dv_pipeline_layout *pipeline_layout,
                                   uint32_t index)

{
   struct v3dv_descriptor *descriptor =
      v3dv_descriptor_map_get_descriptor(descriptor_state, map,
                                         pipeline_layout, index, NULL);

   switch (descriptor->type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      assert(descriptor->buffer_view);
      return descriptor->buffer_view->buffer->mem->bo;
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
      assert(descriptor->image_view);
      struct v3dv_image *image =
         (struct v3dv_image *) descriptor->image_view->vk.image;
      return image->mem->bo;
   }
   default:
      unreachable("descriptor type doesn't has a texture bo");
   }
}

struct v3dv_cl_reloc
v3dv_descriptor_map_get_texture_shader_state(struct v3dv_device *device,
                                             struct v3dv_descriptor_state *descriptor_state,
                                             struct v3dv_descriptor_map *map,
                                             struct v3dv_pipeline_layout *pipeline_layout,
                                             uint32_t index)
{
   VkDescriptorType type;
   struct v3dv_cl_reloc reloc =
      v3dv_descriptor_map_get_descriptor_bo(device,
                                            descriptor_state, map,
                                            pipeline_layout,
                                            index, &type);

   assert(type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
          type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
          type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
          type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
          type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
          type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);

   if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      reloc.offset += v3dv_X(device, combined_image_sampler_texture_state_offset)();

   return reloc;
}

/*
 * As anv and tu already points:
 *
 * "Pipeline layouts.  These have nothing to do with the pipeline.  They are
 * just multiple descriptor set layouts pasted together."
 */

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreatePipelineLayout(VkDevice _device,
                         const VkPipelineLayoutCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkPipelineLayout *pPipelineLayout)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_pipeline_layout *layout;

   assert(pCreateInfo->sType ==
          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);

   layout = vk_object_zalloc(&device->vk, pAllocator, sizeof(*layout),
                             VK_OBJECT_TYPE_PIPELINE_LAYOUT);
   if (layout == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   layout->num_sets = pCreateInfo->setLayoutCount;

   uint32_t dynamic_offset_count = 0;
   for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
      V3DV_FROM_HANDLE(v3dv_descriptor_set_layout, set_layout,
                     pCreateInfo->pSetLayouts[set]);
      layout->set[set].layout = set_layout;

      layout->set[set].dynamic_offset_start = dynamic_offset_count;
      for (uint32_t b = 0; b < set_layout->binding_count; b++) {
         dynamic_offset_count += set_layout->binding[b].array_size *
            set_layout->binding[b].dynamic_offset_count;
      }

      layout->shader_stages |= set_layout->shader_stages;
   }

   layout->push_constant_size = 0;
   for (unsigned i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
      const VkPushConstantRange *range = pCreateInfo->pPushConstantRanges + i;
      layout->push_constant_size =
         MAX2(layout->push_constant_size, range->offset + range->size);
   }

   layout->push_constant_size = align(layout->push_constant_size, 4096);

   layout->dynamic_offset_count = dynamic_offset_count;

   *pPipelineLayout = v3dv_pipeline_layout_to_handle(layout);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyPipelineLayout(VkDevice _device,
                          VkPipelineLayout _pipelineLayout,
                          const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_pipeline_layout, pipeline_layout, _pipelineLayout);

   if (!pipeline_layout)
      return;
   vk_object_free(&device->vk, pAllocator, pipeline_layout);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateDescriptorPool(VkDevice _device,
                          const VkDescriptorPoolCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkDescriptorPool *pDescriptorPool)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_descriptor_pool *pool;
   /* size is for the vulkan object descriptor pool. The final size would
    * depend on some of FREE_DESCRIPTOR flags used
    */
   uint64_t size = sizeof(struct v3dv_descriptor_pool);
   /* bo_size is for the descriptor related info that we need to have on a GPU
    * address (so on v3dv_bo_alloc allocated memory), like for example the
    * texture sampler state. Note that not all the descriptors use it
    */
   uint32_t bo_size = 0;
   uint32_t descriptor_count = 0;

   assert(pCreateInfo->poolSizeCount > 0);
   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      /* Verify supported descriptor type */
      switch(pCreateInfo->pPoolSizes[i].type) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         break;
      default:
         unreachable("Unimplemented descriptor type");
         break;
      }

      assert(pCreateInfo->pPoolSizes[i].descriptorCount > 0);
      descriptor_count += pCreateInfo->pPoolSizes[i].descriptorCount;
      bo_size += v3dv_X(device, descriptor_bo_size)(pCreateInfo->pPoolSizes[i].type) *
         pCreateInfo->pPoolSizes[i].descriptorCount;
   }

   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
      uint64_t host_size =
         pCreateInfo->maxSets * sizeof(struct v3dv_descriptor_set);
      host_size += sizeof(struct v3dv_descriptor) * descriptor_count;
      size += host_size;
   } else {
      size += sizeof(struct v3dv_descriptor_pool_entry) * pCreateInfo->maxSets;
   }

   pool = vk_object_zalloc(&device->vk, pAllocator, size,
                           VK_OBJECT_TYPE_DESCRIPTOR_POOL);

   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
      pool->host_memory_base = (uint8_t*)pool + sizeof(struct v3dv_descriptor_pool);
      pool->host_memory_ptr = pool->host_memory_base;
      pool->host_memory_end = (uint8_t*)pool + size;
   }

   pool->max_entry_count = pCreateInfo->maxSets;

   if (bo_size > 0) {
      pool->bo = v3dv_bo_alloc(device, bo_size, "descriptor pool bo", true);
      if (!pool->bo)
         goto out_of_device_memory;

      bool ok = v3dv_bo_map(device, pool->bo, pool->bo->size);
      if (!ok)
         goto out_of_device_memory;

      pool->current_offset = 0;
   } else {
      pool->bo = NULL;
   }

   *pDescriptorPool = v3dv_descriptor_pool_to_handle(pool);

   return VK_SUCCESS;

 out_of_device_memory:
   vk_object_free(&device->vk, pAllocator, pool);
   return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
}

static void
descriptor_set_destroy(struct v3dv_device *device,
                       struct v3dv_descriptor_pool *pool,
                       struct v3dv_descriptor_set *set,
                       bool free_bo)
{
   assert(!pool->host_memory_base);

   if (free_bo && !pool->host_memory_base) {
      for (uint32_t i = 0; i < pool->entry_count; i++) {
         if (pool->entries[i].set == set) {
            memmove(&pool->entries[i], &pool->entries[i+1],
                    sizeof(pool->entries[i]) * (pool->entry_count - i - 1));
            --pool->entry_count;
            break;
         }
      }
   }
   vk_object_free(&device->vk, NULL, set);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyDescriptorPool(VkDevice _device,
                           VkDescriptorPool _pool,
                           const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_descriptor_pool, pool, _pool);

   if (!pool)
      return;

   if (!pool->host_memory_base) {
      for(int i = 0; i < pool->entry_count; ++i) {
         descriptor_set_destroy(device, pool, pool->entries[i].set, false);
      }
   }

   if (pool->bo) {
      v3dv_bo_free(device, pool->bo);
      pool->bo = NULL;
   }

   vk_object_free(&device->vk, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_ResetDescriptorPool(VkDevice _device,
                         VkDescriptorPool descriptorPool,
                         VkDescriptorPoolResetFlags flags)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_descriptor_pool, pool, descriptorPool);

   if (!pool->host_memory_base) {
      for(int i = 0; i < pool->entry_count; ++i) {
         descriptor_set_destroy(device, pool, pool->entries[i].set, false);
      }
   } else {
      /* We clean-up the host memory, so when allocating a new set from the
       * pool, it is already 0
       */
      uint32_t host_size = pool->host_memory_end - pool->host_memory_base;
      memset(pool->host_memory_base, 0, host_size);
   }

   pool->entry_count = 0;
   pool->host_memory_ptr = pool->host_memory_base;
   pool->current_offset = 0;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateDescriptorSetLayout(VkDevice _device,
                               const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkDescriptorSetLayout *pSetLayout)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_descriptor_set_layout *set_layout;

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
      if ((desc_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           desc_type == VK_DESCRIPTOR_TYPE_SAMPLER) &&
           pCreateInfo->pBindings[j].pImmutableSamplers) {
         immutable_sampler_count += pCreateInfo->pBindings[j].descriptorCount;
      }
   }

   uint32_t samplers_offset = sizeof(struct v3dv_descriptor_set_layout) +
      num_bindings * sizeof(set_layout->binding[0]);
   uint32_t size = samplers_offset +
      immutable_sampler_count * sizeof(struct v3dv_sampler);

   set_layout = vk_object_zalloc(&device->vk, pAllocator, size,
                                 VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);

   if (!set_layout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* We just allocate all the immutable samplers at the end of the struct */
   struct v3dv_sampler *samplers = (void*) &set_layout->binding[num_bindings];

   assert(pCreateInfo->bindingCount == 0 || num_bindings > 0);

   VkDescriptorSetLayoutBinding *bindings = NULL;
   VkResult result = vk_create_sorted_bindings(pCreateInfo->pBindings,
                                               pCreateInfo->bindingCount, &bindings);
   if (result != VK_SUCCESS) {
      vk_object_free(&device->vk, pAllocator, set_layout);
      return vk_error(device, result);
   }

   memset(set_layout->binding, 0,
          size - sizeof(struct v3dv_descriptor_set_layout));

   set_layout->binding_count = num_bindings;
   set_layout->flags = pCreateInfo->flags;
   set_layout->shader_stages = 0;
   set_layout->bo_size = 0;

   uint32_t descriptor_count = 0;
   uint32_t dynamic_offset_count = 0;

   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = bindings + i;
      uint32_t binding_number = binding->binding;

      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         set_layout->binding[binding_number].dynamic_offset_count = 1;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         /* Nothing here, just to keep the descriptor type filtering below */
         break;
      default:
         unreachable("Unknown descriptor type\n");
         break;
      }

      set_layout->binding[binding_number].type = binding->descriptorType;
      set_layout->binding[binding_number].array_size = binding->descriptorCount;
      set_layout->binding[binding_number].descriptor_index = descriptor_count;
      set_layout->binding[binding_number].dynamic_offset_index = dynamic_offset_count;

      if ((binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) &&
          binding->pImmutableSamplers) {

         set_layout->binding[binding_number].immutable_samplers_offset = samplers_offset;

         for (uint32_t i = 0; i < binding->descriptorCount; i++)
            samplers[i] = *v3dv_sampler_from_handle(binding->pImmutableSamplers[i]);

         samplers += binding->descriptorCount;
         samplers_offset += sizeof(struct v3dv_sampler) * binding->descriptorCount;
      }

      descriptor_count += binding->descriptorCount;
      dynamic_offset_count += binding->descriptorCount *
         set_layout->binding[binding_number].dynamic_offset_count;

      set_layout->shader_stages |= binding->stageFlags;

      set_layout->binding[binding_number].descriptor_offset = set_layout->bo_size;
      set_layout->bo_size +=
         v3dv_X(device, descriptor_bo_size)(set_layout->binding[binding_number].type) *
         binding->descriptorCount;
   }

   free(bindings);

   set_layout->descriptor_count = descriptor_count;
   set_layout->dynamic_offset_count = dynamic_offset_count;

   *pSetLayout = v3dv_descriptor_set_layout_to_handle(set_layout);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyDescriptorSetLayout(VkDevice _device,
                                VkDescriptorSetLayout _set_layout,
                                const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_descriptor_set_layout, set_layout, _set_layout);

   if (!set_layout)
      return;

   vk_object_free(&device->vk, pAllocator, set_layout);
}

static inline VkResult
out_of_pool_memory(const struct v3dv_device *device,
                   const struct v3dv_descriptor_pool *pool)
{
   /* Don't log OOPM errors for internal driver pools, we handle these properly
    * by allocating a new pool, so they don't point to real issues.
    */
   if (!pool->is_driver_internal)
      return vk_error(device, VK_ERROR_OUT_OF_POOL_MEMORY);
   else
      return VK_ERROR_OUT_OF_POOL_MEMORY;
}

static VkResult
descriptor_set_create(struct v3dv_device *device,
                      struct v3dv_descriptor_pool *pool,
                      const struct v3dv_descriptor_set_layout *layout,
                      struct v3dv_descriptor_set **out_set)
{
   struct v3dv_descriptor_set *set;
   uint32_t descriptor_count = layout->descriptor_count;
   unsigned mem_size = sizeof(struct v3dv_descriptor_set) +
      sizeof(struct v3dv_descriptor) * descriptor_count;

   if (pool->host_memory_base) {
      if (pool->host_memory_end - pool->host_memory_ptr < mem_size)
         return out_of_pool_memory(device, pool);

      set = (struct v3dv_descriptor_set*)pool->host_memory_ptr;
      pool->host_memory_ptr += mem_size;

      vk_object_base_init(&device->vk, &set->base, VK_OBJECT_TYPE_DESCRIPTOR_SET);
   } else {
      set = vk_object_zalloc(&device->vk, NULL, mem_size,
                             VK_OBJECT_TYPE_DESCRIPTOR_SET);

      if (!set)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   set->pool = pool;

   set->layout = layout;

   /* FIXME: VK_EXT_descriptor_indexing introduces
    * VARIABLE_DESCRIPTOR_LAYOUT_COUNT. That would affect the layout_size used
    * below for bo allocation
    */

   uint32_t offset = 0;
   uint32_t index = pool->entry_count;

   if (layout->bo_size) {
      if (!pool->host_memory_base && pool->entry_count == pool->max_entry_count) {
         vk_object_free(&device->vk, NULL, set);
         return out_of_pool_memory(device, pool);
      }

      /* We first try to allocate linearly fist, so that we don't spend time
       * looking for gaps if the app only allocates & resets via the pool.
       *
       * If that fails, we try to find a gap from previously freed subregions
       * iterating through the descriptor pool entries. Note that we are not
       * doing that if we have a pool->host_memory_base. We only have that if
       * VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT is not set, so in
       * that case the user can't free subregions, so it doesn't make sense to
       * even try (or track those subregions).
       */
      if (pool->current_offset + layout->bo_size <= pool->bo->size) {
         offset = pool->current_offset;
         pool->current_offset += layout->bo_size;
      } else if (!pool->host_memory_base) {
         for (index = 0; index < pool->entry_count; index++) {
            if (pool->entries[index].offset - offset >= layout->bo_size)
               break;
            offset = pool->entries[index].offset + pool->entries[index].size;
         }
         if (pool->bo->size - offset < layout->bo_size) {
            vk_object_free(&device->vk, NULL, set);
            return out_of_pool_memory(device, pool);
         }
         memmove(&pool->entries[index + 1], &pool->entries[index],
                 sizeof(pool->entries[0]) * (pool->entry_count - index));
      } else {
         assert(pool->host_memory_base);
         return out_of_pool_memory(device, pool);
      }

      set->base_offset = offset;
   }

   if (!pool->host_memory_base) {
      pool->entries[index].set = set;
      pool->entries[index].offset = offset;
      pool->entries[index].size = layout->bo_size;
      pool->entry_count++;
   }

   /* Go through and fill out immutable samplers if we have any */
   for (uint32_t b = 0; b < layout->binding_count; b++) {
      if (layout->binding[b].immutable_samplers_offset == 0)
         continue;

      const struct v3dv_sampler *samplers =
         (const struct v3dv_sampler *)((const char *)layout +
                                       layout->binding[b].immutable_samplers_offset);

      for (uint32_t i = 0; i < layout->binding[b].array_size; i++) {
         uint32_t combined_offset =
            layout->binding[b].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ?
            v3dv_X(device, combined_image_sampler_sampler_state_offset)() : 0;

         void *desc_map = descriptor_bo_map(device, set, &layout->binding[b], i);
         desc_map += combined_offset;

         memcpy(desc_map,
                samplers[i].sampler_state,
                sizeof(samplers[i].sampler_state));
      }
   }

   *out_set = set;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_AllocateDescriptorSets(VkDevice _device,
                            const VkDescriptorSetAllocateInfo *pAllocateInfo,
                            VkDescriptorSet *pDescriptorSets)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_descriptor_pool, pool, pAllocateInfo->descriptorPool);

   VkResult result = VK_SUCCESS;
   struct v3dv_descriptor_set *set = NULL;
   uint32_t i = 0;

   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      V3DV_FROM_HANDLE(v3dv_descriptor_set_layout, layout,
                       pAllocateInfo->pSetLayouts[i]);

      result = descriptor_set_create(device, pool, layout, &set);
      if (result != VK_SUCCESS)
         break;

      pDescriptorSets[i] = v3dv_descriptor_set_to_handle(set);
   }

   if (result != VK_SUCCESS) {
      v3dv_FreeDescriptorSets(_device, pAllocateInfo->descriptorPool,
                              i, pDescriptorSets);
      for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
         pDescriptorSets[i] = VK_NULL_HANDLE;
      }
   }

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_FreeDescriptorSets(VkDevice _device,
                        VkDescriptorPool descriptorPool,
                        uint32_t count,
                        const VkDescriptorSet *pDescriptorSets)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_descriptor_pool, pool, descriptorPool);

   for (uint32_t i = 0; i < count; i++) {
      V3DV_FROM_HANDLE(v3dv_descriptor_set, set, pDescriptorSets[i]);
      if (set && !pool->host_memory_base)
         descriptor_set_destroy(device, pool, set, true);
   }

   return VK_SUCCESS;
}

static void
descriptor_bo_copy(struct v3dv_device *device,
                   struct v3dv_descriptor_set *dst_set,
                   const struct v3dv_descriptor_set_binding_layout *dst_binding_layout,
                   uint32_t dst_array_index,
                   struct v3dv_descriptor_set *src_set,
                   const struct v3dv_descriptor_set_binding_layout *src_binding_layout,
                   uint32_t src_array_index)
{
   assert(dst_binding_layout->type == src_binding_layout->type);

   void *dst_map = descriptor_bo_map(device, dst_set, dst_binding_layout, dst_array_index);
   void *src_map = descriptor_bo_map(device, src_set, src_binding_layout, src_array_index);

   memcpy(dst_map, src_map, v3dv_X(device, descriptor_bo_size)(src_binding_layout->type));
}

static void
write_buffer_descriptor(struct v3dv_descriptor *descriptor,
                        VkDescriptorType desc_type,
                        const VkDescriptorBufferInfo *buffer_info)
{
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, buffer_info->buffer);

   descriptor->type = desc_type;
   descriptor->buffer = buffer;
   descriptor->offset = buffer_info->offset;
   if (buffer_info->range == VK_WHOLE_SIZE) {
      descriptor->range = buffer->size - buffer_info->offset;
   } else {
      assert(descriptor->range <= UINT32_MAX);
      descriptor->range = buffer_info->range;
   }
}

static void
write_image_descriptor(struct v3dv_device *device,
                       struct v3dv_descriptor *descriptor,
                       VkDescriptorType desc_type,
                       struct v3dv_descriptor_set *set,
                       const struct v3dv_descriptor_set_binding_layout *binding_layout,
                       struct v3dv_image_view *iview,
                       struct v3dv_sampler *sampler,
                       uint32_t array_index)
{
   descriptor->type = desc_type;
   descriptor->sampler = sampler;
   descriptor->image_view = iview;

   void *desc_map = descriptor_bo_map(device, set,
                                      binding_layout, array_index);

   if (iview) {
      const uint32_t tex_state_index =
         iview->vk.view_type != VK_IMAGE_VIEW_TYPE_CUBE_ARRAY ||
         desc_type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? 0 : 1;
      memcpy(desc_map,
             iview->texture_shader_state[tex_state_index],
             sizeof(iview->texture_shader_state[0]));
      desc_map += v3dv_X(device, combined_image_sampler_sampler_state_offset)();
   }

   if (sampler && !binding_layout->immutable_samplers_offset) {
      /* For immutable samplers this was already done as part of the
       * descriptor set create, as that info can't change later
       */
      memcpy(desc_map,
             sampler->sampler_state,
             sizeof(sampler->sampler_state));
   }
}


static void
write_buffer_view_descriptor(struct v3dv_device *device,
                             struct v3dv_descriptor *descriptor,
                             VkDescriptorType desc_type,
                             struct v3dv_descriptor_set *set,
                             const struct v3dv_descriptor_set_binding_layout *binding_layout,
                             struct v3dv_buffer_view *bview,
                             uint32_t array_index)
{
   assert(bview);
   descriptor->type = desc_type;
   descriptor->buffer_view = bview;

   void *desc_map = descriptor_bo_map(device, set, binding_layout, array_index);

   memcpy(desc_map,
          bview->texture_shader_state,
          sizeof(bview->texture_shader_state));
}

VKAPI_ATTR void VKAPI_CALL
v3dv_UpdateDescriptorSets(VkDevice  _device,
                          uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   for (uint32_t i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *writeset = &pDescriptorWrites[i];
      V3DV_FROM_HANDLE(v3dv_descriptor_set, set, writeset->dstSet);

      const struct v3dv_descriptor_set_binding_layout *binding_layout =
         set->layout->binding + writeset->dstBinding;

      struct v3dv_descriptor *descriptor = set->descriptors;

      descriptor += binding_layout->descriptor_index;
      descriptor += writeset->dstArrayElement;

      for (uint32_t j = 0; j < writeset->descriptorCount; ++j) {
         switch(writeset->descriptorType) {

         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
            const VkDescriptorBufferInfo *buffer_info = writeset->pBufferInfo + j;
            write_buffer_descriptor(descriptor, writeset->descriptorType,
                                    buffer_info);
            break;
         }
         case VK_DESCRIPTOR_TYPE_SAMPLER: {
            /* If we are here we shouldn't be modifying a immutable sampler,
             * so we don't ensure that would work or not crash. But let the
             * validation layers check that
             */
            const VkDescriptorImageInfo *image_info = writeset->pImageInfo + j;
            V3DV_FROM_HANDLE(v3dv_sampler, sampler, image_info->sampler);
            write_image_descriptor(device, descriptor, writeset->descriptorType,
                                   set, binding_layout, NULL, sampler,
                                   writeset->dstArrayElement + j);

            break;
         }
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
            const VkDescriptorImageInfo *image_info = writeset->pImageInfo + j;
            V3DV_FROM_HANDLE(v3dv_image_view, iview, image_info->imageView);
            write_image_descriptor(device, descriptor, writeset->descriptorType,
                                   set, binding_layout, iview, NULL,
                                   writeset->dstArrayElement + j);

            break;
         }
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            const VkDescriptorImageInfo *image_info = writeset->pImageInfo + j;
            V3DV_FROM_HANDLE(v3dv_image_view, iview, image_info->imageView);
            V3DV_FROM_HANDLE(v3dv_sampler, sampler, image_info->sampler);
            write_image_descriptor(device, descriptor, writeset->descriptorType,
                                   set, binding_layout, iview, sampler,
                                   writeset->dstArrayElement + j);

            break;
         }
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: {
            V3DV_FROM_HANDLE(v3dv_buffer_view, buffer_view,
                             writeset->pTexelBufferView[j]);
            write_buffer_view_descriptor(device, descriptor, writeset->descriptorType,
                                         set, binding_layout, buffer_view,
                                         writeset->dstArrayElement + j);
            break;
         }
         default:
            unreachable("unimplemented descriptor type");
            break;
         }
         descriptor++;
      }
   }

   for (uint32_t i = 0; i < descriptorCopyCount; i++) {
      const VkCopyDescriptorSet *copyset = &pDescriptorCopies[i];
      V3DV_FROM_HANDLE(v3dv_descriptor_set, src_set,
                       copyset->srcSet);
      V3DV_FROM_HANDLE(v3dv_descriptor_set, dst_set,
                       copyset->dstSet);

      const struct v3dv_descriptor_set_binding_layout *src_binding_layout =
         src_set->layout->binding + copyset->srcBinding;
      const struct v3dv_descriptor_set_binding_layout *dst_binding_layout =
         dst_set->layout->binding + copyset->dstBinding;

      assert(src_binding_layout->type == dst_binding_layout->type);

      struct v3dv_descriptor *src_descriptor = src_set->descriptors;
      struct v3dv_descriptor *dst_descriptor = dst_set->descriptors;

      src_descriptor += src_binding_layout->descriptor_index;
      src_descriptor += copyset->srcArrayElement;

      dst_descriptor += dst_binding_layout->descriptor_index;
      dst_descriptor += copyset->dstArrayElement;

      for (uint32_t j = 0; j < copyset->descriptorCount; j++) {
         *dst_descriptor = *src_descriptor;
         dst_descriptor++;
         src_descriptor++;

         if (v3dv_X(device, descriptor_bo_size)(src_binding_layout->type) > 0) {
            descriptor_bo_copy(device,
                               dst_set, dst_binding_layout,
                               j + copyset->dstArrayElement,
                               src_set, src_binding_layout,
                               j + copyset->srcArrayElement);
         }

      }
   }
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetDescriptorSetLayoutSupport(
   VkDevice _device,
   const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   VkDescriptorSetLayoutSupport *pSupport)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   VkDescriptorSetLayoutBinding *bindings = NULL;
   VkResult result = vk_create_sorted_bindings(
      pCreateInfo->pBindings, pCreateInfo->bindingCount, &bindings);
   if (result != VK_SUCCESS) {
      pSupport->supported = false;
      return;
   }

   bool supported = true;

   uint32_t desc_host_size = sizeof(struct v3dv_descriptor);
   uint32_t host_size = sizeof(struct v3dv_descriptor_set);
   uint32_t bo_size = 0;
   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = bindings + i;

      if ((UINT32_MAX - host_size) / desc_host_size < binding->descriptorCount) {
         supported = false;
         break;
      }

      uint32_t desc_bo_size = v3dv_X(device, descriptor_bo_size)(binding->descriptorType);
      if (desc_bo_size > 0 &&
          (UINT32_MAX - bo_size) / desc_bo_size < binding->descriptorCount) {
         supported = false;
         break;
      }

      host_size += binding->descriptorCount * desc_host_size;
      bo_size += binding->descriptorCount * desc_bo_size;
   }

   free(bindings);

   pSupport->supported = supported;
}

VkResult
v3dv_CreateDescriptorUpdateTemplate(
   VkDevice _device,
   const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_descriptor_update_template *template;

   size_t size = sizeof(*template) +
      pCreateInfo->descriptorUpdateEntryCount * sizeof(template->entries[0]);
   template = vk_object_alloc(&device->vk, pAllocator, size,
                              VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);
   if (template == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   template->bind_point = pCreateInfo->pipelineBindPoint;

   assert(pCreateInfo->templateType ==
          VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET);
   template->set = pCreateInfo->set;

   template->entry_count = pCreateInfo->descriptorUpdateEntryCount;
   for (uint32_t i = 0; i < template->entry_count; i++) {
      const VkDescriptorUpdateTemplateEntry *pEntry =
         &pCreateInfo->pDescriptorUpdateEntries[i];

      template->entries[i] = (struct v3dv_descriptor_template_entry) {
         .type = pEntry->descriptorType,
         .binding = pEntry->dstBinding,
         .array_element = pEntry->dstArrayElement,
         .array_count = pEntry->descriptorCount,
         .offset = pEntry->offset,
         .stride = pEntry->stride,
      };
   }

   *pDescriptorUpdateTemplate =
      v3dv_descriptor_update_template_to_handle(template);

   return VK_SUCCESS;
}

void
v3dv_DestroyDescriptorUpdateTemplate(
   VkDevice _device,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_descriptor_update_template, template,
                    descriptorUpdateTemplate);

   if (!template)
      return;

   vk_object_free(&device->vk, pAllocator, template);
}

void
v3dv_UpdateDescriptorSetWithTemplate(
   VkDevice _device,
   VkDescriptorSet descriptorSet,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_descriptor_set, set, descriptorSet);
   V3DV_FROM_HANDLE(v3dv_descriptor_update_template, template,
                    descriptorUpdateTemplate);

   for (int i = 0; i < template->entry_count; i++) {
      const struct v3dv_descriptor_template_entry *entry =
         &template->entries[i];

      const struct v3dv_descriptor_set_binding_layout *binding_layout =
         set->layout->binding + entry->binding;

      struct v3dv_descriptor *descriptor =
         set->descriptors +
         binding_layout->descriptor_index +
         entry->array_element;

      switch (entry->type) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               pData + entry->offset + j * entry->stride;
            write_buffer_descriptor(descriptor + j, entry->type, info);
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               pData + entry->offset + j * entry->stride;
            V3DV_FROM_HANDLE(v3dv_image_view, iview, info->imageView);
            V3DV_FROM_HANDLE(v3dv_sampler, sampler, info->sampler);
            write_image_descriptor(device, descriptor + j, entry->type,
                                   set, binding_layout, iview, sampler,
                                   entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkBufferView *_bview =
               pData + entry->offset + j * entry->stride;
            V3DV_FROM_HANDLE(v3dv_buffer_view, bview, *_bview);
            write_buffer_view_descriptor(device, descriptor + j, entry->type,
                                         set, binding_layout, bview,
                                         entry->array_element + j);
         }
         break;

      default:
         unreachable("Unsupported descriptor type");
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateSamplerYcbcrConversion(
    VkDevice _device,
    const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSamplerYcbcrConversion *pYcbcrConversion)
{
   unreachable("Ycbcr sampler conversion is not supported");
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroySamplerYcbcrConversion(
    VkDevice _device,
    VkSamplerYcbcrConversion YcbcrConversion,
    const VkAllocationCallbacks *pAllocator)
{
   unreachable("Ycbcr sampler conversion is not supported");
}
