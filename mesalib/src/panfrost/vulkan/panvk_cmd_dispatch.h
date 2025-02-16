/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_DISPATCH_H
#define PANVK_CMD_DISPATCH_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "pan_desc.h"

enum panvk_cmd_compute_dirty_state {
   PANVK_CMD_COMPUTE_DIRTY_CS,
   PANVK_CMD_COMPUTE_DIRTY_DESC_STATE,
   PANVK_CMD_COMPUTE_DIRTY_PUSH_UNIFORMS,
   PANVK_CMD_COMPUTE_DIRTY_STATE_COUNT,
};

struct panvk_cmd_compute_state {
   struct panvk_descriptor_state desc_state;
   const struct panvk_shader *shader;
   struct panvk_compute_sysvals sysvals;
   uint64_t push_uniforms;
   struct {
      struct panvk_shader_desc_state desc;
   } cs;
   BITSET_DECLARE(dirty, PANVK_CMD_COMPUTE_DIRTY_STATE_COUNT);
};

#define compute_state_dirty(__cmdbuf, __name)                                  \
   BITSET_TEST((__cmdbuf)->state.compute.dirty,                                \
               PANVK_CMD_COMPUTE_DIRTY_##__name)

#define compute_state_set_dirty(__cmdbuf, __name)                              \
   BITSET_SET((__cmdbuf)->state.compute.dirty, PANVK_CMD_COMPUTE_DIRTY_##__name)

#define compute_state_clear_all_dirty(__cmdbuf)                                \
   BITSET_ZERO((__cmdbuf)->state.compute.dirty)

#define clear_dirty_after_dispatch(__cmdbuf)                                   \
   do {                                                                        \
      compute_state_clear_all_dirty(__cmdbuf);                                 \
   } while (0)

#define set_compute_sysval(__cmdbuf, __dirty, __name, __val)                   \
   do {                                                                        \
      struct panvk_compute_sysvals __new_sysval;                               \
      __new_sysval.__name = (__val);                                           \
      if (memcmp(&(__cmdbuf)->state.compute.sysvals.__name,                    \
                 &__new_sysval.__name, sizeof(__new_sysval.__name))) {         \
         (__cmdbuf)->state.compute.sysvals.__name = __new_sysval.__name;       \
         BITSET_SET_RANGE(__dirty, sysval_fau_start(compute, __name),          \
                          sysval_fau_start(compute, __name));                  \
      }                                                                        \
   } while (0)

struct panvk_dispatch_info {
   struct {
      uint32_t x, y, z;
   } wg_base;

   struct {
      struct {
         uint32_t x, y, z;
      } wg_count;
   } direct;

   struct {
      uint64_t buffer_dev_addr;
   } indirect;
};

void panvk_per_arch(cmd_prepare_dispatch_sysvals)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_dispatch_info *info);

uint64_t panvk_per_arch(cmd_dispatch_prepare_tls)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader *shader,
   const struct pan_compute_dim *dim, bool indirect);

#endif
