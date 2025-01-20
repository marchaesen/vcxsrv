/*
 * Copyright © 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AMDGPU_USERQ_H
#define AMDGPU_USERQ_H

#ifdef __cplusplus
extern "C" {
#endif

/* ring size should be power of 2 and enough to hold AMDGPU_FENCE_RING_SIZE ibs */
#define AMDGPU_USERQ_RING_SIZE 0x10000
#define AMDGPU_USERQ_RING_SIZE_DW (AMDGPU_USERQ_RING_SIZE >> 2)
#define AMDGPU_USERQ_RING_SIZE_DW_MASK (AMDGPU_USERQ_RING_SIZE_DW - 1)

/* An offset into doorbell page. Any number will work. */
#define AMDGPU_USERQ_DOORBELL_INDEX 4

#define amdgpu_pkt_begin() uint32_t __num_dw_written = 0; \
   uint32_t __ring_start = *userq->wptr_bo_map & AMDGPU_USERQ_RING_SIZE_DW_MASK;

#define amdgpu_pkt_add_dw(value) do { \
   *(userq->ring_ptr + ((__ring_start + __num_dw_written) & AMDGPU_USERQ_RING_SIZE_DW_MASK)) \
      = value; \
   __num_dw_written++; \
} while (0)

#define amdgpu_pkt_end() do { \
   *userq->wptr_bo_map += __num_dw_written; \
} while (0)

struct amdgpu_winsys;

struct amdgpu_userq_gfx_data {
   struct pb_buffer_lean *csa_bo;
   struct pb_buffer_lean *shadow_bo;
};

struct amdgpu_userq_compute_data {
   struct pb_buffer_lean *eop_bo;
};

struct amdgpu_userq_sdma_data {
   struct pb_buffer_lean *csa_bo;
};

/* For gfx, compute and sdma ip there will be one userqueue per process.
 * i.e. commands from all context will be submitted to single userqueue.
 * There will be one struct amdgpu_userq per gfx, compute and sdma ip.
 */
struct amdgpu_userq {
   struct pb_buffer_lean *gtt_bo;
   uint8_t *gtt_bo_map;

   uint32_t *ring_ptr;
   uint64_t *user_fence_ptr;
   uint64_t user_fence_va;
   uint64_t user_fence_seq_num;

   struct pb_buffer_lean *wptr_bo;
   uint64_t *wptr_bo_map;
   struct pb_buffer_lean *rptr_bo;

   struct pb_buffer_lean *doorbell_bo;
   uint64_t *doorbell_bo_map;

   uint32_t userq_handle;
   enum amd_ip_type ip_type;
   simple_mtx_t lock;

   union {
      struct amdgpu_userq_gfx_data gfx_data;
      struct amdgpu_userq_compute_data compute_data;
      struct amdgpu_userq_sdma_data sdma_data;
   };
};

bool
amdgpu_userq_init(struct amdgpu_winsys *aws, struct amdgpu_userq *userq, enum amd_ip_type ip_type);
void
amdgpu_userq_deinit(struct amdgpu_winsys *aws, struct amdgpu_userq *userq);

#ifdef __cplusplus
}
#endif

#endif
