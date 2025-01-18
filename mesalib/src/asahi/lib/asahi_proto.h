/*
 * Copyright 2024 Sergio Lopez
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef ASAHI_PROTO_H_
#define ASAHI_PROTO_H_

/**
 * Defines the layout of shmem buffer used for host->guest communication.
 */
struct asahi_shmem {
   struct vdrm_shmem base;

   /**
    * Counter that is incremented on asynchronous errors, like SUBMIT
    * or GEM_NEW failures.  The guest should treat errors as context-
    * lost.
    */
   uint32_t async_error;

   /**
    * Counter that is incremented on global fault (see MSM_PARAM_FAULTS)
    */
   uint32_t global_faults;
};
DEFINE_CAST(vdrm_shmem, asahi_shmem)

/*
 * Possible cmd types for "command stream", ie. payload of EXECBUF ioctl:
 */
enum asahi_ccmd {
   ASAHI_CCMD_NOP = 1, /* No payload, can be used to sync with host */
   ASAHI_CCMD_IOCTL_SIMPLE,
   ASAHI_CCMD_GET_PARAMS,
   ASAHI_CCMD_GEM_NEW,
   ASAHI_CCMD_GEM_BIND,
   ASAHI_CCMD_SUBMIT,
};

#define ASAHI_CCMD(_cmd, _len)                                                 \
   (struct vdrm_ccmd_req)                                                      \
   {                                                                           \
      .cmd = ASAHI_CCMD_##_cmd, .len = (_len),                                 \
   }

/*
 * ASAHI_CCMD_NOP
 */
struct asahi_ccmd_nop_req {
   struct vdrm_ccmd_req hdr;
};

/*
 * ASAHI_CCMD_IOCTL_SIMPLE
 *
 * Forward simple/flat IOC_RW or IOC_W ioctls.  Limited ioctls are supported.
 */
struct asahi_ccmd_ioctl_simple_req {
   struct vdrm_ccmd_req hdr;

   uint32_t cmd;
   uint8_t payload[];
};
DEFINE_CAST(vdrm_ccmd_req, asahi_ccmd_ioctl_simple_req)

struct asahi_ccmd_ioctl_simple_rsp {
   struct vdrm_ccmd_rsp hdr;

   /* ioctl return value, interrupted syscalls are handled on the host without
    * returning to the guest.
    */
   int32_t ret;

   /* The output payload for IOC_RW ioctls, the payload is the same size as
    * asahi_context_cmd_ioctl_simple_req.
    *
    * For IOC_W ioctls (userspace writes, kernel reads) this is zero length.
    */
   uint8_t payload[];
};

struct asahi_ccmd_get_params_req {
   struct vdrm_ccmd_req hdr;
   struct drm_asahi_get_params params;
};
DEFINE_CAST(vdrm_ccmd_req, asahi_ccmd_get_params_req)

struct asahi_ccmd_get_params_rsp {
   struct vdrm_ccmd_rsp hdr;
   int32_t ret;
   struct drm_asahi_params_global params;
};

struct asahi_ccmd_gem_new_req {
   struct vdrm_ccmd_req hdr;
   uint32_t flags;
   uint32_t bind_flags;
   uint32_t vm_id;
   uint32_t blob_id;
   uint64_t size;
   uint64_t addr;
};
DEFINE_CAST(vdrm_ccmd_req, asahi_ccmd_gem_new_req)

struct asahi_ccmd_gem_bind_req {
   struct vdrm_ccmd_req hdr;
   uint32_t op;
   uint32_t flags;
   uint32_t vm_id;
   uint32_t res_id;
   uint64_t size;
   uint64_t addr;
};
DEFINE_CAST(vdrm_ccmd_req, asahi_ccmd_gem_bind_req)

struct asahi_ccmd_gem_bind_rsp {
   struct vdrm_ccmd_rsp hdr;
   int32_t ret;
};

#define ASAHI_EXTRES_READ  0x01
#define ASAHI_EXTRES_WRITE 0x02

struct asahi_ccmd_submit_res {
   uint32_t res_id;
   uint32_t flags;
};

struct asahi_ccmd_submit_req {
   struct vdrm_ccmd_req hdr;
   uint32_t queue_id;
   uint32_t result_res_id;
   uint32_t command_count;
   uint32_t extres_count;

   uint8_t payload[];
};
DEFINE_CAST(vdrm_ccmd_req, asahi_ccmd_submit_req)

#endif // ASAHI_PROTO_H_
