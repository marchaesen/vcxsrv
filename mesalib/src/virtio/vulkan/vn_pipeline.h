/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_PIPELINE_H
#define VN_PIPELINE_H

#include "vn_common.h"

struct vn_shader_module {
   struct vn_object_base base;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_shader_module,
                               base.base,
                               VkShaderModule,
                               VK_OBJECT_TYPE_SHADER_MODULE)

struct vn_pipeline_layout {
   struct vn_object_base base;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_pipeline_layout,
                               base.base,
                               VkPipelineLayout,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT)

struct vn_pipeline_cache {
   struct vn_object_base base;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_pipeline_cache,
                               base.base,
                               VkPipelineCache,
                               VK_OBJECT_TYPE_PIPELINE_CACHE)

struct vn_pipeline {
   struct vn_object_base base;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_pipeline,
                               base.base,
                               VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)

#endif /* VN_PIPELINE_H */
