/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>

#include "vk_alloc.h"
#include "vk_log.h"

#include "panvk_device.h"
#include "panvk_priv_bo.h"

#include "kmod/pan_kmod.h"

#include "genxml/decode.h"

VkResult
panvk_priv_bo_create(struct panvk_device *dev, size_t size, uint32_t flags,
                     VkSystemAllocationScope scope, struct panvk_priv_bo **out)
{
   VkResult result;
   int ret;
   struct panvk_priv_bo *priv_bo =
      vk_zalloc(&dev->vk.alloc, sizeof(*priv_bo), 8, scope);

   if (!priv_bo)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct pan_kmod_bo *bo =
      pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, size, flags);
   if (!bo) {
      result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_free_priv_bo;
   }

   priv_bo->bo = bo;
   priv_bo->dev = dev;

   if (!(flags & PAN_KMOD_BO_FLAG_NO_MMAP)) {
      priv_bo->addr.host = pan_kmod_bo_mmap(
         bo, 0, pan_kmod_bo_size(bo), PROT_READ | PROT_WRITE, MAP_SHARED, NULL);
      if (priv_bo->addr.host == MAP_FAILED) {
         result = panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto err_put_bo;
      }
   }

   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_MAP,
      .va = {
         .start = PAN_KMOD_VM_MAP_AUTO_VA,
         .size = pan_kmod_bo_size(bo),
      },
      .map = {
         .bo = priv_bo->bo,
         .bo_offset = 0,
      },
   };

   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      simple_mtx_lock(&dev->as.lock);
      op.va.start = util_vma_heap_alloc(
         &dev->as.heap, op.va.size, op.va.size > 0x200000 ? 0x200000 : 0x1000);
      simple_mtx_unlock(&dev->as.lock);
      if (!op.va.start) {
         result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_munmap_bo;
      }
   }

   ret = pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
   if (ret) {
      result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_return_va;
   }

   priv_bo->addr.dev = op.va.start;

   if (dev->debug.decode_ctx) {
      pandecode_inject_mmap(dev->debug.decode_ctx, priv_bo->addr.dev,
                            priv_bo->addr.host, pan_kmod_bo_size(priv_bo->bo),
                            NULL);
   }

   p_atomic_set(&priv_bo->refcnt, 1);

   *out = priv_bo;
   return VK_SUCCESS;

err_return_va:
   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      simple_mtx_lock(&dev->as.lock);
      util_vma_heap_free(&dev->as.heap, op.va.start, op.va.size);
      simple_mtx_unlock(&dev->as.lock);
   }

err_munmap_bo:
   if (priv_bo->addr.host) {
      ret = os_munmap(priv_bo->addr.host, pan_kmod_bo_size(bo));
      assert(!ret);
   }

err_put_bo:
   pan_kmod_bo_put(bo);

err_free_priv_bo:
   vk_free(&dev->vk.alloc, priv_bo);
   return result;
}

static void
panvk_priv_bo_destroy(struct panvk_priv_bo *priv_bo)
{
   struct panvk_device *dev = priv_bo->dev;

   if (dev->debug.decode_ctx) {
      pandecode_inject_free(dev->debug.decode_ctx, priv_bo->addr.dev,
                            pan_kmod_bo_size(priv_bo->bo));
   }

   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
      .va = {
         .start = priv_bo->addr.dev,
         .size = pan_kmod_bo_size(priv_bo->bo),
      },
   };
   ASSERTED int ret =
      pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
   assert(!ret);

   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      simple_mtx_lock(&dev->as.lock);
      util_vma_heap_free(&dev->as.heap, op.va.start, op.va.size);
      simple_mtx_unlock(&dev->as.lock);
   }

   if (priv_bo->addr.host) {
      ret = os_munmap(priv_bo->addr.host, pan_kmod_bo_size(priv_bo->bo));
      assert(!ret);
   }

   pan_kmod_bo_put(priv_bo->bo);
   vk_free(&dev->vk.alloc, priv_bo);
}

void
panvk_priv_bo_unref(struct panvk_priv_bo *priv_bo)
{
   if (!priv_bo || p_atomic_dec_return(&priv_bo->refcnt))
      return;

   panvk_priv_bo_destroy(priv_bo);
}
