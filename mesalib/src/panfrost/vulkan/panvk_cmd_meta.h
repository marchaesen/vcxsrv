/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_META_H
#define PANVK_CMD_META_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_descriptor_set.h"

struct panvk_shader;

struct panvk_cmd_meta_compute_save_ctx {
   struct {
      const struct panvk_shader *shader;
      struct panvk_shader_desc_state desc;
   } cs;
   const struct panvk_descriptor_set *set0;
   struct {
      struct panvk_opaque_desc desc_storage[MAX_PUSH_DESCS];
      uint64_t descs_dev_addr;
      uint32_t desc_count;
   } push_set0;
   struct panvk_push_constant_state push_constants;
};

void panvk_per_arch(cmd_meta_compute_start)(
   struct panvk_cmd_buffer *cmdbuf,
   struct panvk_cmd_meta_compute_save_ctx *save_ctx);

void panvk_per_arch(cmd_meta_compute_end)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_cmd_meta_compute_save_ctx *save_ctx);

struct panvk_cmd_meta_graphics_save_ctx {
   const struct panvk_graphics_pipeline *pipeline;
   const struct panvk_descriptor_set *set0;
   struct {
      struct panvk_opaque_desc desc_storage[MAX_PUSH_DESCS];
      uint64_t descs_dev_addr;
      uint32_t desc_count;
   } push_set0;
   struct panvk_push_constant_state push_constants;
   struct panvk_attrib_buf vb0;

   struct {
      struct vk_dynamic_graphics_state all;
      struct vk_vertex_input_state vi;
      struct vk_sample_locations_state sl;
   } dyn_state;

   struct {
      const struct panvk_shader *shader;
      struct panvk_shader_desc_state desc;
   } fs;

   struct {
      const struct panvk_shader *shader;
      struct panvk_shader_desc_state desc;
   } vs;

   struct panvk_occlusion_query_state occlusion_query;
};

void panvk_per_arch(cmd_meta_gfx_start)(
   struct panvk_cmd_buffer *cmdbuf,
   struct panvk_cmd_meta_graphics_save_ctx *save_ctx);

void panvk_per_arch(cmd_meta_gfx_end)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_cmd_meta_graphics_save_ctx *save_ctx);

#endif
