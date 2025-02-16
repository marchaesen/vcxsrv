/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_LINUX_DRM_H
#define AC_LINUX_DRM_H

#include <stdbool.h>
#include <stdint.h>

#ifndef _WIN32
#include "drm-uapi/amdgpu_drm.h"
#include "amdgpu.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* All functions are static inline stubs on Windows. */
#ifdef _WIN32
#define PROC static inline
#define TAIL                                                                                       \
   {                                                                                               \
      return -1;                                                                                   \
   }
#define TAILV                                                                                      \
   {                                                                                               \
   }
#define TAILPTR                                                                                    \
   {                                                                                               \
      return NULL;                                                                                 \
   }
typedef void* amdgpu_va_handle;
#else
#define PROC
#define TAIL
#define TAILV
#define TAILPTR
#endif

struct ac_drm_device;
typedef struct ac_drm_device ac_drm_device;

typedef union ac_drm_bo {
#ifdef _WIN32
   void *abo;
#else
   amdgpu_bo_handle abo;
#endif
#ifdef HAVE_AMDGPU_VIRTIO
   struct amdvgpu_bo *vbo;
#endif
} ac_drm_bo;

struct ac_drm_bo_import_result {
   ac_drm_bo bo;
   uint64_t alloc_size;
};

PROC int ac_drm_device_initialize(int fd, bool is_virtio,
                                  uint32_t *major_version, uint32_t *minor_version,
                                  ac_drm_device **device_handle) TAIL;
PROC uintptr_t ac_drm_device_get_cookie(ac_drm_device *dev) TAIL;
PROC void ac_drm_device_deinitialize(ac_drm_device *dev) TAILV;
PROC int ac_drm_device_get_fd(ac_drm_device *dev) TAIL;
PROC int ac_drm_bo_set_metadata(ac_drm_device *dev, uint32_t bo_handle,
                                struct amdgpu_bo_metadata *info) TAIL;
PROC int ac_drm_bo_query_info(ac_drm_device *dev, uint32_t bo_handle, struct amdgpu_bo_info *info) TAIL;
PROC int ac_drm_bo_wait_for_idle(ac_drm_device *dev, ac_drm_bo bo, uint64_t timeout_ns,
                                 bool *busy) TAIL;
PROC int ac_drm_bo_va_op(ac_drm_device *dev, uint32_t bo_handle, uint64_t offset, uint64_t size,
                         uint64_t addr, uint64_t flags, uint32_t ops) TAIL;
PROC int ac_drm_bo_va_op_raw(ac_drm_device *dev, uint32_t bo_handle, uint64_t offset, uint64_t size,
                             uint64_t addr, uint64_t flags, uint32_t ops) TAIL;
PROC int ac_drm_bo_va_op_raw2(ac_drm_device *dev, uint32_t bo_handle, uint64_t offset, uint64_t size,
                              uint64_t addr, uint64_t flags, uint32_t ops,
                              uint32_t vm_timeline_syncobj_out, uint64_t vm_timeline_point,
                              uint64_t input_fence_syncobj_handles,
                              uint32_t num_syncobj_handles) TAIL;
PROC int ac_drm_cs_ctx_create2(ac_drm_device *dev, uint32_t priority, uint32_t *ctx_id) TAIL;
PROC int ac_drm_cs_ctx_free(ac_drm_device *dev, uint32_t ctx_id) TAIL;
PROC int ac_drm_cs_ctx_stable_pstate(ac_drm_device *dev, uint32_t ctx_id, uint32_t op,
                                     uint32_t flags, uint32_t *out_flags) TAIL;
PROC int ac_drm_cs_query_reset_state2(ac_drm_device *dev, uint32_t ctx_id, uint64_t *flags) TAIL;
PROC int ac_drm_cs_query_fence_status(ac_drm_device *dev, uint32_t ctx_id, uint32_t ip_type,
                                      uint32_t ip_instance, uint32_t ring, uint64_t fence_seq_no,
                                      uint64_t timeout_ns, uint64_t flags, uint32_t *expired) TAIL;
PROC int ac_drm_cs_create_syncobj2(int device_fd, uint32_t flags, uint32_t *handle) TAIL;
PROC int ac_drm_cs_create_syncobj(int device_fd, uint32_t *handle) TAIL;
PROC int ac_drm_cs_destroy_syncobj(int device_fd, uint32_t handle) TAIL;
PROC int ac_drm_cs_syncobj_wait(int device_fd, uint32_t *handles, unsigned num_handles,
                                int64_t timeout_nsec, unsigned flags,
                                uint32_t *first_signaled) TAIL;
PROC int ac_drm_cs_syncobj_query2(int device_fd, uint32_t *handles, uint64_t *points,
                                  unsigned num_handles, uint32_t flags) TAIL;
PROC int ac_drm_cs_import_syncobj(int device_fd, int shared_fd, uint32_t *handle) TAIL;
PROC int ac_drm_cs_syncobj_export_sync_file(int device_fd, uint32_t syncobj,
                                            int *sync_file_fd) TAIL;
PROC int ac_drm_cs_syncobj_import_sync_file(int device_fd, uint32_t syncobj, int sync_file_fd) TAIL;
PROC int ac_drm_cs_syncobj_export_sync_file2(int device_fd, uint32_t syncobj, uint64_t point,
                                             uint32_t flags, int *sync_file_fd) TAIL;
PROC int ac_drm_cs_syncobj_transfer(int device_fd, uint32_t dst_handle, uint64_t dst_point,
                                    uint32_t src_handle, uint64_t src_point, uint32_t flags) TAIL;
PROC int ac_drm_cs_submit_raw2(ac_drm_device *dev, uint32_t ctx_id, uint32_t bo_list_handle,
                               int num_chunks, struct drm_amdgpu_cs_chunk *chunks,
                               uint64_t *seq_no) TAIL;
PROC void ac_drm_cs_chunk_fence_info_to_data(uint32_t bo_handle, uint64_t offset,
                                             struct drm_amdgpu_cs_chunk_data *data) TAILV;
PROC int ac_drm_cs_syncobj_timeline_wait(int device_fd, uint32_t *handles, uint64_t *points,
                                         unsigned num_handles, int64_t timeout_nsec, unsigned flags,
                                         uint32_t *first_signaled) TAIL;
PROC int ac_drm_query_info(ac_drm_device *dev, unsigned info_id, unsigned size, void *value) TAIL;
PROC int ac_drm_read_mm_registers(ac_drm_device *dev, unsigned dword_offset, unsigned count,
                                  uint32_t instance, uint32_t flags, uint32_t *values) TAIL;
PROC int ac_drm_query_hw_ip_count(ac_drm_device *dev, unsigned type, uint32_t *count) TAIL;
PROC int ac_drm_query_hw_ip_info(ac_drm_device *dev, unsigned type, unsigned ip_instance,
                                 struct drm_amdgpu_info_hw_ip *info) TAIL;
PROC int ac_drm_query_firmware_version(ac_drm_device *dev, unsigned fw_type, unsigned ip_instance,
                                       unsigned index, uint32_t *version, uint32_t *feature) TAIL;
PROC int ac_drm_query_uq_fw_area_info(ac_drm_device *dev, unsigned type, unsigned ip_instance,
                                      struct drm_amdgpu_info_uq_fw_areas *info) TAIL;
PROC int ac_drm_query_gpu_info(ac_drm_device *dev, struct amdgpu_gpu_info *info) TAIL;
PROC int ac_drm_query_heap_info(ac_drm_device *dev, uint32_t heap, uint32_t flags,
                                struct amdgpu_heap_info *info) TAIL;
PROC int ac_drm_query_sensor_info(ac_drm_device *dev, unsigned sensor_type, unsigned size,
                                  void *value) TAIL;
PROC int ac_drm_query_video_caps_info(ac_drm_device *dev, unsigned cap_type, unsigned size,
                                      void *value) TAIL;
PROC int ac_drm_query_gpuvm_fault_info(ac_drm_device *dev, unsigned size, void *value) TAIL;
PROC int ac_drm_vm_reserve_vmid(ac_drm_device *dev, uint32_t flags) TAIL;
PROC int ac_drm_vm_unreserve_vmid(ac_drm_device *dev, uint32_t flags) TAIL;
PROC const char *ac_drm_get_marketing_name(ac_drm_device *device) TAILPTR;
PROC int ac_drm_query_sw_info(ac_drm_device *dev,
                              enum amdgpu_sw_info info, void *value) TAIL;
PROC int ac_drm_bo_alloc(ac_drm_device *dev, struct amdgpu_bo_alloc_request *alloc_buffer,
                         ac_drm_bo *bo) TAIL;
PROC int ac_drm_bo_export(ac_drm_device *dev, ac_drm_bo bo,
                          enum amdgpu_bo_handle_type type, uint32_t *shared_handle) TAIL;
PROC int ac_drm_bo_import(ac_drm_device *dev, enum amdgpu_bo_handle_type type,
                          uint32_t shared_handle, struct ac_drm_bo_import_result *output) TAIL;
PROC int ac_drm_create_bo_from_user_mem(ac_drm_device *dev, void *cpu,
                                        uint64_t size, ac_drm_bo *bo) TAIL;
PROC int ac_drm_bo_free(ac_drm_device *dev, ac_drm_bo bo) TAIL;
PROC int ac_drm_bo_cpu_map(ac_drm_device *dev, ac_drm_bo bo, void **cpu) TAIL;
PROC int ac_drm_bo_cpu_unmap(ac_drm_device *dev, ac_drm_bo bo) TAIL;
PROC int ac_drm_va_range_alloc(ac_drm_device *dev, enum amdgpu_gpu_va_range va_range_type,
                               uint64_t size, uint64_t va_base_alignment, uint64_t va_base_required,
                               uint64_t *va_base_allocated, amdgpu_va_handle *va_range_handle,
                               uint64_t flags) TAIL;
PROC int ac_drm_va_range_free(amdgpu_va_handle va_range_handle) TAIL;
PROC int ac_drm_create_userqueue(ac_drm_device *dev, uint32_t ip_type, uint32_t doorbell_handle,
                                 uint32_t doorbell_offset, uint64_t queue_va, uint64_t queue_size,
                                 uint64_t wptr_va, uint64_t rptr_va, void *mqd_in,
                                 uint32_t *queue_id) TAIL;
PROC int ac_drm_free_userqueue(ac_drm_device *dev, uint32_t queue_id) TAIL;
PROC int ac_drm_userq_signal(ac_drm_device *dev, struct drm_amdgpu_userq_signal *signal_data) TAIL;
PROC int ac_drm_userq_wait(ac_drm_device *dev, struct drm_amdgpu_userq_wait *wait_data) TAIL;

#ifdef __cplusplus
}
#endif

#endif
