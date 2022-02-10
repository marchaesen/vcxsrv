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

VkResult
panvk_CreateDescriptorSetLayout(VkDevice _device,
                                const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkDescriptorSetLayout *pSetLayout)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_descriptor_set_layout *set_layout;
   VkDescriptorSetLayoutBinding *bindings = NULL;
   unsigned num_bindings = 0;
   VkResult result;

   if (pCreateInfo->bindingCount) {
      result =
         vk_create_sorted_bindings(pCreateInfo->pBindings,
                                   pCreateInfo->bindingCount,
                                   &bindings);
      if (result != VK_SUCCESS)
         return vk_error(device, result);

      num_bindings = bindings[pCreateInfo->bindingCount - 1].binding + 1;
   }

   unsigned num_immutable_samplers = 0;
   for (unsigned i = 0; i < pCreateInfo->bindingCount; i++) {
      if (bindings[i].pImmutableSamplers)
         num_immutable_samplers += bindings[i].descriptorCount;
   }

   size_t size = sizeof(*set_layout) +
                 (sizeof(struct panvk_descriptor_set_binding_layout) *
                  num_bindings) +
                 (sizeof(struct panvk_sampler *) * num_immutable_samplers);
   set_layout = vk_object_zalloc(&device->vk, pAllocator, size,
                                 VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
   if (!set_layout) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto err_free_bindings;
   }

   struct panvk_sampler **immutable_samplers =
      (struct panvk_sampler **)((uint8_t *)set_layout + sizeof(*set_layout) +
                                (sizeof(struct panvk_descriptor_set_binding_layout) *
                                 num_bindings));

   set_layout->flags = pCreateInfo->flags;
   set_layout->binding_count = num_bindings;

   unsigned sampler_idx = 0, tex_idx = 0, ubo_idx = 0, ssbo_idx = 0;
   unsigned dynoffset_idx = 0, desc_idx = 0;

   for (unsigned i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &bindings[i];
      struct panvk_descriptor_set_binding_layout *binding_layout =
         &set_layout->bindings[binding->binding];

      binding_layout->type = binding->descriptorType;
      binding_layout->array_size = binding->descriptorCount;
      binding_layout->shader_stages = binding->stageFlags;
      if (binding->pImmutableSamplers) {
         binding_layout->immutable_samplers = immutable_samplers;
         immutable_samplers += binding_layout->array_size;
         for (unsigned j = 0; j < binding_layout->array_size; j++) {
            VK_FROM_HANDLE(panvk_sampler, sampler, binding->pImmutableSamplers[j]);
            binding_layout->immutable_samplers[j] = sampler;
         }
      }

      binding_layout->desc_idx = desc_idx;
      desc_idx += binding->descriptorCount;
      switch (binding_layout->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         binding_layout->sampler_idx = sampler_idx;
         sampler_idx += binding_layout->array_size;
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         binding_layout->sampler_idx = sampler_idx;
         binding_layout->tex_idx = tex_idx;
         sampler_idx += binding_layout->array_size;
         tex_idx += binding_layout->array_size;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         binding_layout->tex_idx = tex_idx;
         tex_idx += binding_layout->array_size;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         binding_layout->dynoffset_idx = dynoffset_idx;
         dynoffset_idx += binding_layout->array_size;
         FALLTHROUGH;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         binding_layout->ubo_idx = ubo_idx;
         ubo_idx += binding_layout->array_size;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         binding_layout->dynoffset_idx = dynoffset_idx;
         dynoffset_idx += binding_layout->array_size;
         FALLTHROUGH;
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         binding_layout->ssbo_idx = ssbo_idx;
         ssbo_idx += binding_layout->array_size;
         break;
      default:
         unreachable("Invalid descriptor type");
      }
   }

   set_layout->num_descs = desc_idx;
   set_layout->num_samplers = sampler_idx;
   set_layout->num_textures = tex_idx;
   set_layout->num_ubos = ubo_idx;
   set_layout->num_ssbos = ssbo_idx;
   set_layout->num_dynoffsets = dynoffset_idx;

   free(bindings);
   *pSetLayout = panvk_descriptor_set_layout_to_handle(set_layout);
   return VK_SUCCESS;

err_free_bindings:
   free(bindings);
   return vk_error(device, result);
}

void
panvk_DestroyDescriptorSetLayout(VkDevice _device,
                                 VkDescriptorSetLayout _set_layout,
                                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_descriptor_set_layout, set_layout, _set_layout);

   if (!set_layout)
      return;

   vk_object_free(&device->vk, pAllocator, set_layout);
}

/* FIXME: make sure those values are correct */
#define PANVK_MAX_TEXTURES     (1 << 16)
#define PANVK_MAX_SAMPLERS     (1 << 16)
#define PANVK_MAX_UBOS         255

void
panvk_GetDescriptorSetLayoutSupport(VkDevice _device,
                                    const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                    VkDescriptorSetLayoutSupport *pSupport)
{
   VK_FROM_HANDLE(panvk_device, device, _device);

   pSupport->supported = false;

   VkDescriptorSetLayoutBinding *bindings;
   VkResult result =
      vk_create_sorted_bindings(pCreateInfo->pBindings,
                                pCreateInfo->bindingCount,
                                &bindings);
   if (result != VK_SUCCESS) {
      vk_error(device, result);
      return;
   }

   unsigned sampler_idx = 0, tex_idx = 0, ubo_idx = 0, UNUSED ssbo_idx = 0,
      UNUSED dynoffset_idx = 0;
   for (unsigned i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &bindings[i];

      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         sampler_idx += binding->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         sampler_idx += binding->descriptorCount;
         tex_idx += binding->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         tex_idx += binding->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         dynoffset_idx += binding->descriptorCount;
         FALLTHROUGH;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         ubo_idx += binding->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         dynoffset_idx += binding->descriptorCount;
         FALLTHROUGH;
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         ssbo_idx += binding->descriptorCount;
         break;
      default:
         unreachable("Invalid descriptor type");
      }
   }

   /* The maximum values apply to all sets attached to a pipeline since all
    * sets descriptors have to be merged in a single array.
    */
   if (tex_idx > PANVK_MAX_TEXTURES / MAX_SETS ||
       sampler_idx > PANVK_MAX_SAMPLERS / MAX_SETS ||
       ubo_idx > PANVK_MAX_UBOS / MAX_SETS)
      return;

   pSupport->supported = true;
}

/*
 * Pipeline layouts.  These have nothing to do with the pipeline.  They are
 * just multiple descriptor set layouts pasted together.
 */

VkResult
panvk_CreatePipelineLayout(VkDevice _device,
                           const VkPipelineLayoutCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipelineLayout *pPipelineLayout)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_pipeline_layout *layout;
   struct mesa_sha1 ctx;

   layout = vk_object_zalloc(&device->vk, pAllocator, sizeof(*layout),
                             VK_OBJECT_TYPE_PIPELINE_LAYOUT);
   if (layout == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   layout->num_sets = pCreateInfo->setLayoutCount;
   _mesa_sha1_init(&ctx);

   unsigned sampler_idx = 0, tex_idx = 0, ssbo_idx = 0, ubo_idx = 0, dynoffset_idx = 0;
   for (unsigned set = 0; set < pCreateInfo->setLayoutCount; set++) {
      VK_FROM_HANDLE(panvk_descriptor_set_layout, set_layout,
                     pCreateInfo->pSetLayouts[set]);
      layout->sets[set].layout = set_layout;
      layout->sets[set].sampler_offset = sampler_idx;
      layout->sets[set].tex_offset = tex_idx;
      layout->sets[set].ubo_offset = ubo_idx;
      layout->sets[set].ssbo_offset = ssbo_idx;
      layout->sets[set].dynoffset_offset = dynoffset_idx;
      sampler_idx += set_layout->num_samplers;
      tex_idx += set_layout->num_textures;
      ubo_idx += set_layout->num_ubos + (set_layout->num_dynoffsets != 0);
      ssbo_idx += set_layout->num_ssbos;
      dynoffset_idx += set_layout->num_dynoffsets;

      for (unsigned b = 0; b < set_layout->binding_count; b++) {
         struct panvk_descriptor_set_binding_layout *binding_layout =
            &set_layout->bindings[b];

         if (binding_layout->immutable_samplers) {
            for (unsigned s = 0; s < binding_layout->array_size; s++) {
               struct panvk_sampler *sampler = binding_layout->immutable_samplers[s];

               _mesa_sha1_update(&ctx, &sampler->desc, sizeof(sampler->desc));
            }
         }
         _mesa_sha1_update(&ctx, &binding_layout->type, sizeof(binding_layout->type));
         _mesa_sha1_update(&ctx, &binding_layout->array_size, sizeof(binding_layout->array_size));
         _mesa_sha1_update(&ctx, &binding_layout->desc_idx, sizeof(binding_layout->sampler_idx));
         _mesa_sha1_update(&ctx, &binding_layout->shader_stages, sizeof(binding_layout->shader_stages));
      }
   }

   layout->num_samplers = sampler_idx;
   layout->num_textures = tex_idx;
   layout->num_ubos = ubo_idx;
   layout->num_ssbos = ssbo_idx;
   layout->num_dynoffsets = dynoffset_idx;

   _mesa_sha1_final(&ctx, layout->sha1);

   *pPipelineLayout = panvk_pipeline_layout_to_handle(layout);
   return VK_SUCCESS;
}

void
panvk_DestroyPipelineLayout(VkDevice _device,
                            VkPipelineLayout _pipelineLayout,
                            const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_pipeline_layout, pipeline_layout, _pipelineLayout);

   if (!pipeline_layout)
      return;

   vk_object_free(&device->vk, pAllocator, pipeline_layout);
}

VkResult
panvk_CreateDescriptorPool(VkDevice _device,
                           const VkDescriptorPoolCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkDescriptorPool *pDescriptorPool)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_descriptor_pool *pool;

   pool = vk_object_zalloc(&device->vk, pAllocator,
                           sizeof(struct panvk_descriptor_pool),
                           VK_OBJECT_TYPE_DESCRIPTOR_POOL);
   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pool->max.sets = pCreateInfo->maxSets;

   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      unsigned desc_count = pCreateInfo->pPoolSizes[i].descriptorCount;

      switch(pCreateInfo->pPoolSizes[i].type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         pool->max.samplers += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         pool->max.combined_image_samplers += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         pool->max.sampled_images += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         pool->max.storage_images += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         pool->max.uniform_texel_bufs += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         pool->max.storage_texel_bufs += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         pool->max.input_attachments += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         pool->max.uniform_bufs += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         pool->max.storage_bufs += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         pool->max.uniform_dyn_bufs += desc_count;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         pool->max.storage_dyn_bufs += desc_count;
         break;
      default:
         unreachable("Invalid descriptor type");
      }
   }

   *pDescriptorPool = panvk_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

void
panvk_DestroyDescriptorPool(VkDevice _device,
                            VkDescriptorPool _pool,
                            const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, _pool);

   if (pool)
      vk_object_free(&device->vk, pAllocator, pool);
}

VkResult
panvk_ResetDescriptorPool(VkDevice _device,
                          VkDescriptorPool _pool,
                          VkDescriptorPoolResetFlags flags)
{
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, _pool);
   memset(&pool->cur, 0, sizeof(pool->cur));
   return VK_SUCCESS;
}

static void
panvk_descriptor_set_destroy(struct panvk_device *device,
                             struct panvk_descriptor_pool *pool,
                             struct panvk_descriptor_set *set)
{
   vk_free(&device->vk.alloc, set->textures);
   vk_free(&device->vk.alloc, set->samplers);
   vk_free(&device->vk.alloc, set->ubos);
   vk_free(&device->vk.alloc, set->descs);
   vk_object_free(&device->vk, NULL, set);
}

VkResult
panvk_FreeDescriptorSets(VkDevice _device,
                         VkDescriptorPool descriptorPool,
                         uint32_t count,
                         const VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, descriptorPool);

   for (unsigned i = 0; i < count; i++) {
      VK_FROM_HANDLE(panvk_descriptor_set, set, pDescriptorSets[i]);

      if (set)
         panvk_descriptor_set_destroy(device, pool, set);
   }
   return VK_SUCCESS;
}

VkResult
panvk_CreateDescriptorUpdateTemplate(VkDevice _device,
                                     const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator,
                                     VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
   panvk_stub();
   return VK_SUCCESS;
}

void
panvk_DestroyDescriptorUpdateTemplate(VkDevice _device,
                                      VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                      const VkAllocationCallbacks *pAllocator)
{
   panvk_stub();
}

void
panvk_UpdateDescriptorSetWithTemplate(VkDevice _device,
                                      VkDescriptorSet descriptorSet,
                                      VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                      const void *pData)
{
   panvk_stub();
}

VkResult
panvk_CreateSamplerYcbcrConversion(VkDevice device,
                                   const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkSamplerYcbcrConversion *pYcbcrConversion)
{
   panvk_stub();
   return VK_SUCCESS;
}

void
panvk_DestroySamplerYcbcrConversion(VkDevice device,
                                    VkSamplerYcbcrConversion ycbcrConversion,
                                    const VkAllocationCallbacks *pAllocator)
{
   panvk_stub();
}
