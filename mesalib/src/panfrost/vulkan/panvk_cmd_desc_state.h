/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_DESC_STATE_H
#define PANVK_CMD_DESC_STATE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "genxml/gen_macros.h"

#include "panvk_cmd_pool.h"
#include "panvk_descriptor_set.h"
#include "panvk_macros.h"
#include "panvk_shader.h"

#include "vk_alloc.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"

#include "pan_pool.h"

struct panvk_cmd_buffer;

struct panvk_shader_desc_state {
#if PAN_ARCH <= 7
   uint64_t tables[PANVK_BIFROST_DESC_TABLE_COUNT];
   uint64_t img_attrib_table;
   uint64_t dyn_ssbos;
#else
   struct {
      uint64_t dev_addr;
      uint32_t size;
   } driver_set;
   uint64_t res_table;
#endif
};

struct panvk_push_set {
   struct panvk_cmd_pool_obj base;
   struct panvk_descriptor_set set;
   struct panvk_opaque_desc descs[MAX_PUSH_DESCS];
};

struct panvk_descriptor_state {
   const struct panvk_descriptor_set *sets[MAX_SETS];
   struct panvk_descriptor_set *push_sets[MAX_SETS];
   BITSET_DECLARE(dirty_push_sets, MAX_SETS);

   uint32_t dyn_buf_offsets[MAX_SETS][MAX_DYNAMIC_BUFFERS];
};

#if PAN_ARCH <= 7
VkResult panvk_per_arch(cmd_prepare_dyn_ssbos)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader *shader,
   struct panvk_shader_desc_state *shader_desc_state);

VkResult panvk_per_arch(cmd_prepare_shader_desc_tables)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader *shader,
   struct panvk_shader_desc_state *shader_desc_state);
#else
void panvk_per_arch(cmd_fill_dyn_bufs)(
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader *shader, struct mali_buffer_packed *buffers);

VkResult panvk_per_arch(cmd_prepare_shader_res_table)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader *shader,
   struct panvk_shader_desc_state *shader_desc_state);
#endif

VkResult panvk_per_arch(cmd_prepare_push_descs)(
   struct panvk_cmd_buffer *cmdbuf, struct panvk_descriptor_state *desc_state,
   uint32_t used_set_mask);

#endif
