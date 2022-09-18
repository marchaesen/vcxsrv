/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef DRM_HW_H_
#define DRM_HW_H_

struct virgl_renderer_capset_drm {
   uint32_t wire_format_version;
   /* Underlying drm device version: */
   uint32_t version_major;
   uint32_t version_minor;
   uint32_t version_patchlevel;
#define VIRTGPU_DRM_CONTEXT_MSM   1
   uint32_t context_type;
   uint32_t pad;
   union {
      struct {
         uint32_t has_cached_coherent;
         uint32_t priorities;
         uint64_t va_start;
         uint64_t va_size;
         uint32_t gpu_id;
         uint32_t gmem_size;
         uint64_t gmem_base;
         uint64_t chip_id;
         uint32_t max_freq;
      } msm;  /* context_type == VIRTGPU_DRM_CONTEXT_MSM */
   } u;
};

#endif /* DRM_HW_H_ */
