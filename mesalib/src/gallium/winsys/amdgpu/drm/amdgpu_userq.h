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

#define amdgpu_pkt_begin() uint32_t *__ring_ptr = userq->ring_ptr; \
   uint64_t __next_wptr = userq->next_wptr;

#define amdgpu_pkt_add_dw(value) do { \
   *(__ring_ptr + (__next_wptr & AMDGPU_USERQ_RING_SIZE_DW_MASK)) = value; \
   __next_wptr++; \
} while (0)

#define amdgpu_pkt_end() do { \
   assert(__next_wptr - *userq->user_fence_ptr <= AMDGPU_USERQ_RING_SIZE_DW); \
   userq->next_wptr = __next_wptr; \
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
   /* Holds the wptr value for the in-progress submission. When we're ready
    * to submit it, this value will be written to the door bell.
    * (this avoids writing multiple times to the door bell for the same
    * submission) */
   uint64_t next_wptr;
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
