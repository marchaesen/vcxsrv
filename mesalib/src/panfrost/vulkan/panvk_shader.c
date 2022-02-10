/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_shader.c which is:
 * Copyright © 2019 Google LLC
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

#include "nir_builder.h"
#include "nir_lower_blend.h"
#include "spirv/nir_spirv.h"
#include "util/mesa-sha1.h"

#include "pan_shader.h"

#include "vk_util.h"

void
panvk_shader_destroy(struct panvk_device *dev,
                     struct panvk_shader *shader,
                     const VkAllocationCallbacks *alloc)
{
   util_dynarray_fini(&shader->binary);
   vk_free2(&dev->vk.alloc, alloc, shader);
}

VkResult
panvk_CreateShaderModule(VkDevice _device,
                         const VkShaderModuleCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkShaderModule *pShaderModule)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_shader_module *module;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
   assert(pCreateInfo->flags == 0);
   assert(pCreateInfo->codeSize % 4 == 0);

   module = vk_object_zalloc(&device->vk, pAllocator,
                             sizeof(*module) + pCreateInfo->codeSize,
                             VK_OBJECT_TYPE_SHADER_MODULE);
   if (module == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   module->code_size = pCreateInfo->codeSize;
   memcpy(module->code, pCreateInfo->pCode, pCreateInfo->codeSize);

   _mesa_sha1_compute(module->code, module->code_size, module->sha1);

   *pShaderModule = panvk_shader_module_to_handle(module);

   return VK_SUCCESS;
}

void
panvk_DestroyShaderModule(VkDevice _device,
                          VkShaderModule _module,
                          const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_shader_module, module, _module);

   if (!module)
      return;

   vk_object_free(&device->vk, pAllocator, module);
}
