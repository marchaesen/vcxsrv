/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_buffer_view.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_priv_bo.h"

#include "vk_format.h"
#include "vk_log.h"

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateBufferView)(VkDevice _device,
                                 const VkBufferViewCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkBufferView *pView)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_buffer, buffer, pCreateInfo->buffer);

   struct panvk_buffer_view *view = vk_object_zalloc(
      &device->vk, pAllocator, sizeof(*view), VK_OBJECT_TYPE_BUFFER_VIEW);

   if (!view)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_buffer_view_init(&device->vk, &view->vk, pCreateInfo);

   enum pipe_format pfmt = vk_format_to_pipe_format(view->vk.format);

   mali_ptr address = panvk_buffer_gpu_ptr(buffer, pCreateInfo->offset);
   VkBufferUsageFlags tex_usage_mask = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

#if PAN_ARCH >= 9
   /* Valhall passes a texture descriptor to LEA_TEX. */
   tex_usage_mask |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
#endif

   assert(!(address & 63));

   if (buffer->vk.usage & tex_usage_mask) {
      struct panvk_physical_device *physical_device =
         to_panvk_physical_device(device->vk.physical);
      unsigned arch = pan_arch(physical_device->kmod.props.gpu_prod_id);

      struct pan_image plane = {
         .data = {
            .base = address,
            .offset = 0,
	 },
         .layout = {
            .modifier = DRM_FORMAT_MOD_LINEAR,
            .format = pfmt,
            .dim = MALI_TEXTURE_DIMENSION_1D,
            .width = view->vk.elements,
            .height = 1,
            .depth = 1,
            .array_size = 1,
            .nr_samples = 1,
            .nr_slices = 1,
         },
      };

      struct pan_image_view pview = {
         .planes[0] = &plane,
         .format = pfmt,
         .dim = MALI_TEXTURE_DIMENSION_1D,
         .nr_samples = 1,
         .first_level = 0,
         .last_level = 0,
         .first_layer = 0,
         .last_layer = 0,
         .swizzle =
            {
               PIPE_SWIZZLE_X,
               PIPE_SWIZZLE_Y,
               PIPE_SWIZZLE_Z,
               PIPE_SWIZZLE_W,
            },
      };

      pan_image_layout_init(arch, &plane.layout, NULL);

      struct panvk_pool_alloc_info alloc_info = {
         .alignment = pan_alignment(TEXTURE),
         .size = GENX(panfrost_estimate_texture_payload_size)(&pview),
      };

      view->mem = panvk_pool_alloc_mem(&device->mempools.rw, alloc_info);

      struct panfrost_ptr ptr = {
         .gpu = panvk_priv_mem_dev_addr(view->mem),
         .cpu = panvk_priv_mem_host_addr(view->mem),
      };

      GENX(panfrost_new_texture)(&pview, view->descs.tex.opaque, &ptr);
   }

#if PAN_ARCH <= 7
   if (buffer->vk.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) {
      unsigned blksz = vk_format_get_blocksize(pCreateInfo->format);

      pan_pack(view->descs.img_attrib_buf[0].opaque, ATTRIBUTE_BUFFER, cfg) {
         /* The format is the only thing we lack to emit attribute descriptors
          * when copying from the set to the attribute tables. Instead of
          * making the descriptor size to store an extra format, we pack
          * the 22-bit format with the texel stride, which is expected to be
          * fit in remaining 10 bits.
          */
         uint32_t hw_fmt = GENX(panfrost_format_from_pipe_format)(pfmt)->hw;

         assert(blksz < BITFIELD_MASK(10));
         assert(hw_fmt < BITFIELD_MASK(22));

         cfg.type = MALI_ATTRIBUTE_TYPE_3D_LINEAR;
         cfg.pointer = address;
         cfg.stride = blksz | (hw_fmt << 10);
         cfg.size = view->vk.elements * blksz;
      }

      pan_pack(view->descs.img_attrib_buf[1].opaque,
               ATTRIBUTE_BUFFER_CONTINUATION_3D, cfg) {
         cfg.s_dimension = view->vk.elements;
         cfg.t_dimension = 1;
         cfg.r_dimension = 1;
         cfg.row_stride = view->vk.elements * blksz;
      }
   }
#endif

   *pView = panvk_buffer_view_to_handle(view);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyBufferView)(VkDevice _device, VkBufferView bufferView,
                                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_buffer_view, view, bufferView);

   if (!view)
      return;

   panvk_pool_free_mem(&view->mem);
   vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
}
