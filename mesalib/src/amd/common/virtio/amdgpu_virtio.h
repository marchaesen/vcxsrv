/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AMDGPU_VIRTIO_H
#define AMDGPU_VIRTIO_H

struct amdvgpu_bo;
struct amdvgpu_device;
struct amdvgpu_context;
typedef struct amdvgpu_device* amdvgpu_device_handle;
typedef struct amdvgpu_bo* amdvgpu_bo_handle;

struct amdvgpu_bo_import_result {
   amdvgpu_bo_handle buf_handle;
   uint64_t alloc_size;
};

int amdvgpu_device_initialize(int fd, uint32_t *drm_major, uint32_t *drm_minor,
                              amdvgpu_device_handle* dev);
int amdvgpu_device_deinitialize(amdvgpu_device_handle dev);
int amdvgpu_bo_va_op_raw(amdvgpu_device_handle dev,
                         uint32_t res_id,
                         uint64_t offset,
                         uint64_t size,
                         uint64_t addr,
                         uint64_t flags,
                         uint32_t ops);
int amdvgpu_bo_import(amdvgpu_device_handle dev,
                      enum amdgpu_bo_handle_type type,
                      uint32_t handle,
                      struct amdvgpu_bo_import_result *result);
int amdvgpu_bo_export(amdvgpu_device_handle dev, amdvgpu_bo_handle bo,
                      enum amdgpu_bo_handle_type type,
                      uint32_t *shared_handle);
int amdvgpu_bo_cpu_map(amdvgpu_device_handle dev, amdvgpu_bo_handle bo_handle, void **cpu);
int amdvgpu_bo_cpu_unmap(amdvgpu_device_handle dev, amdvgpu_bo_handle bo);
int amdvgpu_bo_alloc(amdvgpu_device_handle dev,
                     struct amdgpu_bo_alloc_request *request,
                     amdvgpu_bo_handle *bo);
int amdvgpu_bo_free(amdvgpu_device_handle dev, struct amdvgpu_bo *bo);
int amdvgpu_bo_wait_for_idle(amdvgpu_device_handle dev,
                             amdvgpu_bo_handle bo,
                             uint64_t abs_timeout_ns);
int
amdvgpu_bo_set_metadata(amdvgpu_device_handle dev, uint32_t res_id,
                        struct amdgpu_bo_metadata *info);
int amdvgpu_query_info(amdvgpu_device_handle dev, struct drm_amdgpu_info *info);
int amdvgpu_bo_query_info(amdvgpu_device_handle dev, uint32_t res_id, struct amdgpu_bo_info *info);
int amdvgpu_cs_ctx_create2(amdvgpu_device_handle dev, int32_t priority, uint32_t *ctx_virtio);
int amdvgpu_cs_ctx_free(amdvgpu_device_handle dev, uint32_t ctx);
int amdvgpu_cs_ctx_stable_pstate(amdvgpu_device_handle dev,
                                 uint32_t ctx,
                                 uint32_t op,
                                 uint32_t flags,
                                 uint32_t *out_flags);
int amdvgpu_cs_query_reset_state2(amdvgpu_device_handle dev,
                                  uint32_t ctx,
                                  uint64_t *flags);
int
amdvgpu_va_range_alloc(amdvgpu_device_handle dev,
                       enum amdgpu_gpu_va_range va_range_type,
                       uint64_t size,
                       uint64_t va_base_alignment,
                       uint64_t va_base_required,
                       uint64_t *va_base_allocated,
                       amdgpu_va_handle *va_range_handle,
                       uint64_t flags);
int amdvgpu_cs_query_fence_status(amdvgpu_device_handle dev,
                                  uint32_t ctx,
                                  uint32_t ip_type,
                                  uint32_t ip_instance, uint32_t ring,
                                  uint64_t fence_seq_no,
                                  uint64_t timeout_ns, uint64_t flags,
                                  uint32_t *expired);
int
amdvgpu_device_get_fd(amdvgpu_device_handle dev);
const char *
amdvgpu_get_marketing_name(amdvgpu_device_handle dev);
int
amdvgpu_cs_submit_raw2(amdvgpu_device_handle dev, uint32_t ctx_id,
                       uint32_t bo_list_handle,
                       int num_chunks, struct drm_amdgpu_cs_chunk *chunks,
                       uint64_t *seqno);
int amdvgpu_vm_reserve_vmid(amdvgpu_device_handle dev, int reserve);
int
amdvgpu_query_sw_info(amdvgpu_device_handle dev, enum amdgpu_sw_info info, void *value);

#endif
