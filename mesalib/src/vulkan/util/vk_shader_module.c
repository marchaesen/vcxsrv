/*
 * Copyright © 2017 Intel Corporation
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

#include "vk_shader_module.h"
#include "util/mesa-sha1.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CreateShaderModule(VkDevice _device,
                             const VkShaderModuleCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkShaderModule *pShaderModule)
{
    VK_FROM_HANDLE(vk_device, device, _device);
    struct vk_shader_module *module;

    assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    assert(pCreateInfo->flags == 0);

    module = vk_object_alloc(device, pAllocator,
                             sizeof(*module) + pCreateInfo->codeSize,
                             VK_OBJECT_TYPE_SHADER_MODULE);
    if (module == NULL)
       return VK_ERROR_OUT_OF_HOST_MEMORY;

    module->size = pCreateInfo->codeSize;
    module->nir = NULL;
    memcpy(module->data, pCreateInfo->pCode, module->size);

    _mesa_sha1_compute(module->data, module->size, module->sha1);

    *pShaderModule = vk_shader_module_to_handle(module);

    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_DestroyShaderModule(VkDevice _device,
                              VkShaderModule _module,
                              const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   VK_FROM_HANDLE(vk_shader_module, module, _module);

   if (!module)
      return;

   /* NIR modules (which are only created internally by the driver) are not
    * dynamically allocated so we should never call this for them.
    * Instead the driver is responsible for freeing the NIR code when it is
    * no longer needed.
    */
   assert(module->nir == NULL);

   vk_object_free(device, pAllocator, module);
}
