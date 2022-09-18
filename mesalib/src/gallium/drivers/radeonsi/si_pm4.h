/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SI_PM4_H
#define SI_PM4_H

#include "winsys/radeon_winsys.h"

#ifdef __cplusplus
extern "C" {
#endif

// forward defines
struct si_context;

/* State atoms are callbacks which write a sequence of packets into a GPU
 * command buffer (AKA indirect buffer, AKA IB, AKA command stream, AKA CS).
 */
struct si_atom {
   void (*emit)(struct si_context *ctx);
};

struct si_pm4_state {
   /* PKT3_SET_*_REG handling */
   uint16_t last_reg;   /* register offset in dwords */
   uint16_t last_pm4;
   uint16_t ndw;        /* number of dwords in pm4 */
   uint8_t last_opcode;

   /* For shader states only */
   bool is_shader;
   struct si_atom atom;

   /* commands for the DE */
   uint16_t max_dw;

   /* This must be the last field because the array can continue after the structure. */
   uint32_t pm4[64];
};

void si_pm4_cmd_add(struct si_pm4_state *state, uint32_t dw);
void si_pm4_set_reg(struct si_pm4_state *state, unsigned reg, uint32_t val);
void si_pm4_set_reg_idx3(struct si_pm4_state *state, unsigned reg, uint32_t val);

void si_pm4_clear_state(struct si_pm4_state *state);
void si_pm4_free_state(struct si_context *sctx, struct si_pm4_state *state, unsigned idx);

void si_pm4_emit(struct si_context *sctx, struct si_pm4_state *state);
void si_pm4_reset_emitted(struct si_context *sctx, bool first_cs);

#ifdef __cplusplus
}
#endif

#endif
