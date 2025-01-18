/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_PM4_H
#define AC_PM4_H

#include "ac_gpu_info.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ac_pm4_state {
   const struct radeon_info *info;

   /* PKT3_SET_*_REG handling */
   uint16_t last_reg;   /* register offset in dwords */
   uint16_t last_pm4;
   uint16_t ndw;        /* number of dwords in pm4 */
   uint8_t last_opcode;
   uint8_t last_idx;
   bool is_compute_queue;
   bool packed_is_padded; /* whether SET_*_REG_PAIRS_PACKED is padded to an even number of regs */

   /* commands for the DE */
   uint16_t max_dw;

   /* Used by SQTT to override the shader address */
   bool debug_sqtt;
   uint32_t spi_shader_pgm_lo_reg;

   /* This must be the last field because the array can continue after the structure. */
   uint32_t pm4[64];
};

void
ac_pm4_set_reg(struct ac_pm4_state *state, unsigned reg, uint32_t val);

void
ac_pm4_set_reg_custom(struct ac_pm4_state *state, unsigned reg, uint32_t val,
                      unsigned opcode, unsigned idx);

void
ac_pm4_set_reg_idx3(struct ac_pm4_state *state, unsigned reg, uint32_t val);

void
ac_pm4_clear_state(struct ac_pm4_state *state, const struct radeon_info *info,
                   bool debug_sqtt, bool is_compute_queue);

void
ac_pm4_cmd_begin(struct ac_pm4_state *state, unsigned opcode);

void
ac_pm4_cmd_add(struct ac_pm4_state *state, uint32_t dw);

void
ac_pm4_cmd_end(struct ac_pm4_state *state, bool predicate);

void
ac_pm4_finalize(struct ac_pm4_state *state);

struct ac_pm4_state *
ac_pm4_create_sized(const struct radeon_info *info, bool debug_sqtt,
                    unsigned max_dw, bool is_compute_queue);

void
ac_pm4_free_state(struct ac_pm4_state *state);

#ifdef __cplusplus
}
#endif

#endif
