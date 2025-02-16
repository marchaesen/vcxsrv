/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "util/mesa-blake3.h"

#include "vk_descriptor_update_template.h"
#include "vk_descriptors.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_util.h"
#include "vk_ycbcr_conversion.h"

#include "util/bitset.h"

#include "genxml/gen_macros.h"

#include "panvk_descriptor_set_layout.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_macros.h"
#include "panvk_sampler.h"

#define PANVK_MAX_DESCS_PER_SET (1 << 24)

static bool
is_texture(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return true;

   default:
      return false;
   }
}

static bool
is_sampler(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return true;

   default:
      return false;
   }
}

static bool
binding_has_immutable_samplers(const VkDescriptorSetLayoutBinding *binding)
{
   return is_sampler(binding->descriptorType) &&
      binding->pImmutableSamplers != NULL;
}

static void
set_immutable_sampler(struct panvk_descriptor_set_binding_layout *binding_layout,
                      uint32_t index, struct panvk_sampler *sampler)
{
   binding_layout->immutable_samplers[index] = sampler;
   if (!sampler->vk.ycbcr_conversion)
      return;

   binding_layout->textures_per_desc =
      MAX2(vk_format_get_plane_count(sampler->vk.ycbcr_conversion->state.format),
           binding_layout->textures_per_desc);
   binding_layout->samplers_per_desc =
      MAX2(sampler->desc_count, binding_layout->samplers_per_desc);
}

VkResult
panvk_per_arch(CreateDescriptorSetLayout)(
   VkDevice _device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VkDescriptorSetLayoutBinding *bindings = NULL;
   unsigned num_bindings = 0;
   VkResult result;

   unsigned immutable_sampler_count = 0;
   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[j];
      num_bindings = MAX2(num_bindings, binding->binding + 1);

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
      if (binding_has_immutable_samplers(binding))
         immutable_sampler_count += binding->descriptorCount;
   }

   if (pCreateInfo->bindingCount) {
      result = vk_create_sorted_bindings(pCreateInfo->pBindings,
                                         pCreateInfo->bindingCount, &bindings);
      if (result != VK_SUCCESS)
         return panvk_error(device, result);

      num_bindings = bindings[pCreateInfo->bindingCount - 1].binding + 1;
   }

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_set_layout, layout, 1);
   VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_set_binding_layout,
                      binding_layouts, num_bindings);
   VK_MULTIALLOC_DECL(&ma, struct panvk_sampler *, samplers,
                      immutable_sampler_count);

   if (!vk_descriptor_set_layout_multizalloc(&device->vk, &ma)) {
      free(bindings);
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   layout->flags = pCreateInfo->flags;
   layout->bindings = binding_layouts;
   layout->binding_count = num_bindings;

   const VkDescriptorSetLayoutBindingFlagsCreateInfo *binding_flags_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);

   unsigned desc_idx = 0;
   unsigned dyn_buf_idx = 0;
   for (unsigned i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &bindings[i];
      struct panvk_descriptor_set_binding_layout *binding_layout =
         &layout->bindings[binding->binding];

      if (binding->descriptorCount == 0)
         continue;

      binding_layout->type = binding->descriptorType;

      if (binding_flags_info && binding_flags_info->bindingCount > 0) {
         assert(binding_flags_info->bindingCount == pCreateInfo->bindingCount);
         binding_layout->flags = binding_flags_info->pBindingFlags[i];
      }

      binding_layout->desc_count = binding->descriptorCount;
      if (is_texture(binding_layout->type))
         binding_layout->textures_per_desc = 1;

      if (is_sampler(binding_layout->type))
         binding_layout->samplers_per_desc = 1;

      if (binding_has_immutable_samplers(binding)) {
         binding_layout->immutable_samplers = samplers;
         samplers += binding->descriptorCount;
         for (uint32_t j = 0; j < binding->descriptorCount; j++) {
            VK_FROM_HANDLE(panvk_sampler, sampler,
                           binding->pImmutableSamplers[j]);
            set_immutable_sampler(binding_layout, j, sampler);
         }
      }

      if (vk_descriptor_type_is_dynamic(binding_layout->type)) {
         binding_layout->desc_idx = dyn_buf_idx;
         dyn_buf_idx += binding_layout->desc_count;
      } else {
         binding_layout->desc_idx = desc_idx;
         desc_idx += panvk_get_desc_stride(binding_layout) *
                     binding_layout->desc_count;
      }
   }

   layout->desc_count = desc_idx;
   layout->dyn_buf_count = dyn_buf_idx;

   struct mesa_blake3 hash_ctx;
   _mesa_blake3_init(&hash_ctx);

   _mesa_blake3_update(&hash_ctx, &layout->binding_count,
                       sizeof(layout->binding_count));
   _mesa_blake3_update(&hash_ctx, &layout->desc_count,
                       sizeof(layout->desc_count));
   _mesa_blake3_update(&hash_ctx, &layout->dyn_buf_count,
                       sizeof(layout->dyn_buf_count));

   for (uint32_t b = 0; b < num_bindings; b++) {
      _mesa_blake3_update(&hash_ctx, &layout->bindings[b].type,
                          sizeof(layout->bindings[b].type));
      _mesa_blake3_update(&hash_ctx, &layout->bindings[b].flags,
                          sizeof(layout->bindings[b].flags));
      _mesa_blake3_update(&hash_ctx, &layout->bindings[b].desc_count,
                          sizeof(layout->bindings[b].desc_count));
      _mesa_blake3_update(&hash_ctx, &layout->bindings[b].textures_per_desc,
                          sizeof(layout->bindings[b].textures_per_desc));
      _mesa_blake3_update(&hash_ctx, &layout->bindings[b].samplers_per_desc,
                          sizeof(layout->bindings[b].samplers_per_desc));

      if (layout->bindings[b].immutable_samplers != NULL) {
         for (uint32_t i = 0; i < layout->bindings[b].desc_count; i++) {
            const struct panvk_sampler *sampler =
               layout->bindings[b].immutable_samplers[i];

            /* We zalloc the object, so it's safe to hash the whole thing */
            if (sampler != NULL && sampler->vk.ycbcr_conversion != NULL)
               _mesa_blake3_update(&hash_ctx,
                                   &sampler->vk.ycbcr_conversion->state,
                                   sizeof(sampler->vk.ycbcr_conversion->state));
         }
      }
   }

   _mesa_blake3_final(&hash_ctx, layout->vk.blake3);

   free(bindings);
   *pSetLayout = panvk_descriptor_set_layout_to_handle(layout);

   return VK_SUCCESS;
}

void
panvk_per_arch(GetDescriptorSetLayoutSupport)(
   VkDevice _device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   VkDescriptorSetLayoutSupport *pSupport)
{
   pSupport->supported = false;

   unsigned desc_count = 0, dyn_buf_count = 0;
   for (unsigned i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[i];
      VkDescriptorType type = binding->descriptorType;

      if (vk_descriptor_type_is_dynamic(type)) {
         dyn_buf_count += binding->descriptorCount;
         continue;
      }

      unsigned textures_per_desc = is_texture(type) ? 1 : 0;
      unsigned samplers_per_desc = is_sampler(type) ? 1 : 0;
      if (binding_has_immutable_samplers(binding)) {
         for (uint32_t j = 0; j < binding->descriptorCount; j++) {
            VK_FROM_HANDLE(panvk_sampler, sampler,
                           binding->pImmutableSamplers[j]);
            if (!sampler->vk.ycbcr_conversion)
               continue;

            textures_per_desc =
               MAX2(vk_format_get_plane_count(
                       sampler->vk.ycbcr_conversion->state.format),
                    textures_per_desc);
            samplers_per_desc =
               MAX2(sampler->desc_count, samplers_per_desc);
         }
      }

      const struct panvk_descriptor_set_binding_layout layout = {
         .type = type,
         .textures_per_desc = textures_per_desc,
         .samplers_per_desc = samplers_per_desc,
      };

      desc_count += panvk_get_desc_stride(&layout) * binding->descriptorCount;
   }

   if (desc_count > PANVK_MAX_DESCS_PER_SET ||
       dyn_buf_count > MAX_DYNAMIC_BUFFERS)
      return;

   pSupport->supported = true;
}
