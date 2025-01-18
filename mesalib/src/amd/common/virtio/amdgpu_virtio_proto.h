#ifndef AMDGPU_VIRTIO_PROTO_H
#define AMDGPU_VIRTIO_PROTO_H

#include <stdint.h>
#include "amdgpu.h"
#include "amdgpu_drm.h"
#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic error "-Wpadded"
#endif

enum amdgpu_ccmd {
   AMDGPU_CCMD_QUERY_INFO = 1,
   AMDGPU_CCMD_GEM_NEW,
   AMDGPU_CCMD_BO_VA_OP,
   AMDGPU_CCMD_CS_SUBMIT,
   AMDGPU_CCMD_SET_METADATA,
   AMDGPU_CCMD_BO_QUERY_INFO,
   AMDGPU_CCMD_CREATE_CTX,
   AMDGPU_CCMD_RESERVE_VMID,
   AMDGPU_CCMD_SET_PSTATE,
   AMDGPU_CCMD_CS_QUERY_FENCE_STATUS,
};

struct amdgpu_ccmd_rsp {
   struct vdrm_ccmd_rsp base;
   int32_t ret;
};
static_assert(sizeof(struct amdgpu_ccmd_rsp) == 8, "bug");

#define AMDGPU_STATIC_ASSERT_SIZE(t) \
   static_assert(sizeof(struct t) % 8 == 0, "sizeof(struct " #t ") not multiple of 8"); \
   static_assert(alignof(struct t) <= 8, "alignof(struct " #t ") too large");

/**
 * Defines the layout of shmem buffer used for host->guest communication.
 */
struct amdvgpu_shmem {
   struct vdrm_shmem base;

   /**
    * Counter that is incremented on asynchronous errors, like SUBMIT
    * or GEM_NEW failures.  The guest should treat errors as context-
    * lost.
    */
   uint32_t async_error;

   uint32_t __pad;

   struct amdgpu_heap_info gtt;
   struct amdgpu_heap_info vram;
   struct amdgpu_heap_info vis_vram;
};
AMDGPU_STATIC_ASSERT_SIZE(amdvgpu_shmem)
DEFINE_CAST(vdrm_shmem, amdvgpu_shmem)


#define AMDGPU_CCMD(_cmd, _len) (struct vdrm_ccmd_req){ \
       .cmd = AMDGPU_CCMD_##_cmd,                         \
       .len = (_len),                                     \
   }

/*
 * AMDGPU_CCMD_QUERY_INFO
 *
 * This is amdgpu_query_info.
 */
struct amdgpu_ccmd_query_info_req {
   struct vdrm_ccmd_req hdr;
   struct drm_amdgpu_info info;
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_query_info_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_query_info_req)

struct amdgpu_ccmd_query_info_rsp {
   struct amdgpu_ccmd_rsp hdr;
   uint8_t payload[];
};
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_query_info_rsp)

struct amdgpu_ccmd_gem_new_req {
   struct vdrm_ccmd_req hdr;

   uint64_t blob_id;

   /* This is amdgpu_bo_alloc_request but padded correctly. */
   struct {
      uint64_t alloc_size;
      uint64_t phys_alignment;
      uint32_t preferred_heap;
      uint32_t __pad;
      uint64_t flags;
   } r;
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_gem_new_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_gem_new_req)


/*
 * AMDGPU_CCMD_BO_VA_OP
 *
 */
struct amdgpu_ccmd_bo_va_op_req {
   struct vdrm_ccmd_req hdr;
   uint64_t va;
   uint64_t vm_map_size;
   uint64_t flags; /* Passed directly to kernel */
   uint64_t flags2; /* AMDGPU_CCMD_BO_VA_OP_* */
   uint64_t offset;
   uint32_t res_id;
   uint32_t op;
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_bo_va_op_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_bo_va_op_req)
/* Specifies that this is a sparse BO. */
#define AMDGPU_CCMD_BO_VA_OP_SPARSE_BO (1 << 0)

/*
 * AMDGPU_CCMD_CS_SUBMIT
 */
struct amdgpu_ccmd_cs_submit_req {
   struct vdrm_ccmd_req hdr;

   uint32_t ctx_id;
   uint32_t num_chunks; /* limited to AMDGPU_CCMD_CS_SUBMIT_MAX_NUM_CHUNKS */
   uint32_t pad;
   uint32_t ring_idx;

   /* Starts with a descriptor array:
    *     (chunk_id, offset_in_payload), ...
    */
   uint8_t payload[];
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_cs_submit_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_cs_submit_req)
#define AMDGPU_CCMD_CS_SUBMIT_MAX_NUM_CHUNKS 128

/*
 * AMDGPU_CCMD_SET_METADATA
 */
struct amdgpu_ccmd_set_metadata_req {
   struct vdrm_ccmd_req hdr;
   uint64_t flags;
   uint64_t tiling_info;
   uint32_t res_id;
   uint32_t size_metadata;
   uint32_t umd_metadata[];
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_set_metadata_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_set_metadata_req)


/*
 * AMDGPU_CCMD_BO_QUERY_INFO
 */
struct amdgpu_ccmd_bo_query_info_req {
   struct vdrm_ccmd_req hdr;
   uint32_t res_id;
   uint32_t pad; /* must be zero */
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_bo_query_info_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_bo_query_info_req)

struct amdgpu_ccmd_bo_query_info_rsp {
   struct amdgpu_ccmd_rsp hdr;
   /* This is almost struct amdgpu_bo_info, but padded to get
    * the same struct on 32 bit and 64 bit builds.
    */
   struct {
      uint64_t                   alloc_size;           /*     0     8 */
      uint64_t                   phys_alignment;       /*     8     8 */
      uint32_t                   preferred_heap;       /*    16     4 */
      uint32_t                   __pad;                /*    20     4 */
      uint64_t                   alloc_flags;          /*    24     8 */
      /* This is almost struct amdgpu_bo_metadata, but padded to get
       * the same struct on 32 bit and 64 bit builds.
       */
      struct {
         uint64_t                flags;                /*    32     8 */
         uint64_t                tiling_info;          /*    40     8 */
         uint32_t                size_metadata;        /*    48     4 */
         uint32_t                umd_metadata[64];     /*    52   256 */
         uint32_t                __pad;                /*    308    4 */
      } metadata;
   } info;
};
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_bo_query_info_rsp)

/*
 * AMDGPU_CCMD_CREATE_CTX
 */
struct amdgpu_ccmd_create_ctx_req {
   struct vdrm_ccmd_req hdr;
   union {
      int32_t priority; /* create */
      uint32_t id;      /* destroy */
   };
   uint32_t flags; /* AMDGPU_CCMD_CREATE_CTX_* */
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_create_ctx_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_create_ctx_req)
/* Destroy a context instead of creating one */
#define AMDGPU_CCMD_CREATE_CTX_DESTROY (1 << 0)

struct amdgpu_ccmd_create_ctx_rsp {
   struct amdgpu_ccmd_rsp hdr;
   uint32_t ctx_id;
   uint32_t pad;
};
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_create_ctx_rsp)

/*
 * AMDGPU_CCMD_RESERVE_VMID
 */
struct amdgpu_ccmd_reserve_vmid_req {
   struct vdrm_ccmd_req hdr;
   uint64_t flags; /* AMDGPU_CCMD_RESERVE_VMID_* */
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_reserve_vmid_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_reserve_vmid_req)
/* Unreserve a VMID instead of reserving one */
#define AMDGPU_CCMD_RESERVE_VMID_UNRESERVE (1 << 0)

/*
 * AMDGPU_CCMD_SET_PSTATE
 */
struct amdgpu_ccmd_set_pstate_req {
   struct vdrm_ccmd_req hdr;
   uint32_t ctx_id;
   uint32_t op;
   uint32_t flags;
   uint32_t pad;
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_set_pstate_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_set_pstate_req)

struct amdgpu_ccmd_set_pstate_rsp {
   struct amdgpu_ccmd_rsp hdr;
   uint32_t out_flags;
   uint32_t pad;
};
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_set_pstate_rsp)

/*
 * AMDGPU_CCMD_CS_QUERY_FENCE_STATUS
 */
struct amdgpu_ccmd_cs_query_fence_status_req {
   struct vdrm_ccmd_req hdr;

   uint32_t ctx_id;

   uint32_t ip_type;
   uint32_t ip_instance;
   uint32_t ring;

   uint64_t fence;

   uint64_t timeout_ns;
   uint64_t flags;
};
DEFINE_CAST(vdrm_ccmd_req, amdgpu_ccmd_cs_query_fence_status_req)
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_cs_query_fence_status_req)

struct amdgpu_ccmd_cs_query_fence_status_rsp {
   struct amdgpu_ccmd_rsp hdr;
   uint32_t expired;
   uint32_t pad;
};
AMDGPU_STATIC_ASSERT_SIZE(amdgpu_ccmd_cs_query_fence_status_rsp)

#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif

#endif
