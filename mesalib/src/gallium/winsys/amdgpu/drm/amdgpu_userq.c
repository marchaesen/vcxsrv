/*
 * Copyright © 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "amdgpu_bo.h"
#include "ac_linux_drm.h"

static bool
amdgpu_userq_ring_init(struct amdgpu_winsys *aws, struct amdgpu_userq *userq)
{
   /* Allocate ring and user fence in one buffer. */
   uint32_t gtt_bo_size = AMDGPU_USERQ_RING_SIZE + aws->info.gart_page_size;
   userq->gtt_bo = amdgpu_bo_create(aws, gtt_bo_size, 256, RADEON_DOMAIN_GTT,
                                    RADEON_FLAG_GL2_BYPASS | RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->gtt_bo)
      return false;

   userq->gtt_bo_map = amdgpu_bo_map(&aws->dummy_sws.base, userq->gtt_bo, NULL,
                                     PIPE_MAP_READ | PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED);
   if (!userq->gtt_bo_map)
      return false;

   userq->wptr_bo = amdgpu_bo_create(aws, aws->info.gart_page_size, 256, RADEON_DOMAIN_GTT,
                                     RADEON_FLAG_GL2_BYPASS | RADEON_FLAG_NO_SUBALLOC |
                                        RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->wptr_bo)
      return false;

   userq->wptr_bo_map = amdgpu_bo_map(&aws->dummy_sws.base, userq->wptr_bo, NULL,
                                      PIPE_MAP_READ | PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED);
   if (!userq->wptr_bo_map)
      return false;

   userq->ring_ptr = (uint32_t*)userq->gtt_bo_map;
   userq->user_fence_ptr = (uint64_t*)(userq->gtt_bo_map + AMDGPU_USERQ_RING_SIZE);
   userq->user_fence_va = amdgpu_bo_get_va(userq->gtt_bo) + AMDGPU_USERQ_RING_SIZE;
   *userq->user_fence_ptr = 0;
   *userq->wptr_bo_map = 0;
   userq->next_wptr = 0;

   userq->rptr_bo = amdgpu_bo_create(aws, aws->info.gart_page_size, 256, RADEON_DOMAIN_VRAM,
                                     RADEON_FLAG_CLEAR_VRAM | RADEON_FLAG_GL2_BYPASS |
                                        RADEON_FLAG_NO_SUBALLOC |
                                        RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->rptr_bo)
      return false;

   return true;
}

void
amdgpu_userq_deinit(struct amdgpu_winsys *aws, struct amdgpu_userq *userq)
{
   if (userq->userq_handle)
      ac_drm_free_userqueue(aws->dev, userq->userq_handle);

   radeon_bo_reference(&aws->dummy_sws.base, &userq->gtt_bo, NULL);
   radeon_bo_reference(&aws->dummy_sws.base, &userq->wptr_bo, NULL);
   radeon_bo_reference(&aws->dummy_sws.base, &userq->rptr_bo, NULL);
   radeon_bo_reference(&aws->dummy_sws.base, &userq->doorbell_bo, NULL);

   switch (userq->ip_type) {
   case AMD_IP_GFX:
      radeon_bo_reference(&aws->dummy_sws.base, &userq->gfx_data.csa_bo, NULL);
      radeon_bo_reference(&aws->dummy_sws.base, &userq->gfx_data.shadow_bo, NULL);
      break;
   case AMD_IP_COMPUTE:
      radeon_bo_reference(&aws->dummy_sws.base, &userq->compute_data.eop_bo, NULL);
      break;
   case AMD_IP_SDMA:
      radeon_bo_reference(&aws->dummy_sws.base, &userq->sdma_data.csa_bo, NULL);
      break;
   default:
      fprintf(stderr, "amdgpu: userq unsupported for ip = %d\n", userq->ip_type);
   }
}

bool
amdgpu_userq_init(struct amdgpu_winsys *aws, struct amdgpu_userq *userq, enum amd_ip_type ip_type)
{
   int r = -1;
   uint32_t hw_ip_type;
   struct drm_amdgpu_userq_mqd_gfx11 gfx_mqd;
   struct drm_amdgpu_userq_mqd_compute_gfx11 compute_mqd;
   struct drm_amdgpu_userq_mqd_sdma_gfx11 sdma_mqd;
   void *mqd;

   simple_mtx_lock(&userq->lock);

   if (userq->gtt_bo) {
      simple_mtx_unlock(&userq->lock);
      return true;
   }

   userq->ip_type = ip_type;
   if (!amdgpu_userq_ring_init(aws, userq))
      goto fail;

   switch (userq->ip_type) {
   case AMD_IP_GFX:
      hw_ip_type = AMDGPU_HW_IP_GFX;
      userq->gfx_data.csa_bo = amdgpu_bo_create(aws, aws->info.fw_based_mcbp.csa_size,
                                                aws->info.fw_based_mcbp.csa_alignment,
                                                RADEON_DOMAIN_VRAM,
                                                RADEON_FLAG_NO_INTERPROCESS_SHARING);
      if (!userq->gfx_data.csa_bo)
         goto fail;

      userq->gfx_data.shadow_bo = amdgpu_bo_create(aws, aws->info.fw_based_mcbp.shadow_size,
                                                   aws->info.fw_based_mcbp.shadow_alignment,
                                                   RADEON_DOMAIN_VRAM,
                                                   RADEON_FLAG_NO_INTERPROCESS_SHARING);
      if (!userq->gfx_data.shadow_bo)
         goto fail;

      gfx_mqd.shadow_va = amdgpu_bo_get_va(userq->gfx_data.shadow_bo);
      gfx_mqd.csa_va = amdgpu_bo_get_va(userq->gfx_data.csa_bo);
      mqd = &gfx_mqd;
      break;
   case AMD_IP_COMPUTE:
      hw_ip_type = AMDGPU_HW_IP_COMPUTE;
      userq->compute_data.eop_bo = amdgpu_bo_create(aws, aws->info.gart_page_size, 256,
                                                    RADEON_DOMAIN_VRAM,
                                                    RADEON_FLAG_NO_INTERPROCESS_SHARING);
      if (!userq->compute_data.eop_bo)
         goto fail;

      compute_mqd.eop_va = amdgpu_bo_get_va(userq->compute_data.eop_bo);
      mqd = &compute_mqd;
      break;
   case AMD_IP_SDMA:
      hw_ip_type = AMDGPU_HW_IP_DMA;
      userq->sdma_data.csa_bo = amdgpu_bo_create(aws, aws->info.fw_based_mcbp.csa_size,
                                                 aws->info.fw_based_mcbp.csa_alignment,
                                                 RADEON_DOMAIN_VRAM,
                                                 RADEON_FLAG_NO_INTERPROCESS_SHARING);
      if (!userq->sdma_data.csa_bo)
         goto fail;

      sdma_mqd.csa_va = amdgpu_bo_get_va(userq->sdma_data.csa_bo);
      mqd = &sdma_mqd;
      break;
   default:
      fprintf(stderr, "amdgpu: userq unsupported for ip = %d\n", userq->ip_type);
      goto fail;
   }

   userq->doorbell_bo = amdgpu_bo_create(aws, aws->info.gart_page_size, 256,
                                         RADEON_DOMAIN_DOORBELL,
                                         RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->doorbell_bo)
      goto fail;

   /* doorbell map should be the last map call, it is used to wait for all mappings before
    * calling amdgpu_create_userqueue().
    */
   userq->doorbell_bo_map = amdgpu_bo_map(&aws->dummy_sws.base, userq->doorbell_bo, NULL,
                                          PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED);
   if (!userq->doorbell_bo_map)
      goto fail;

   /* The VA page table for ring buffer should be ready before job submission so that the packets
    * submitted can be read by gpu. The same applies to rptr, wptr buffers also.
    */
   r = ac_drm_cs_syncobj_timeline_wait(aws->fd, &aws->vm_timeline_syncobj,
                                       &get_real_bo(amdgpu_winsys_bo(userq->doorbell_bo))
                                          ->vm_timeline_point,
                                       1, INT64_MAX, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
                                          DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
   if (r) {
      fprintf(stderr, "amdgpu: waiting for vm fences failed\n");
      goto fail;
   }

   uint64_t ring_va = amdgpu_bo_get_va(userq->gtt_bo);
   r = ac_drm_create_userqueue(aws->dev, hw_ip_type,
                               get_real_bo(amdgpu_winsys_bo(userq->doorbell_bo))->kms_handle,
                               AMDGPU_USERQ_DOORBELL_INDEX, ring_va, AMDGPU_USERQ_RING_SIZE,
                               amdgpu_bo_get_va(userq->wptr_bo), amdgpu_bo_get_va(userq->rptr_bo),
                               mqd, &userq->userq_handle);
   if (r) {
      fprintf(stderr, "amdgpu: failed to create userq\n");
      goto fail;
   }

   simple_mtx_unlock(&userq->lock);
   return true;
fail:
   amdgpu_userq_deinit(aws, userq);
   simple_mtx_unlock(&userq->lock);
   return false;
}
