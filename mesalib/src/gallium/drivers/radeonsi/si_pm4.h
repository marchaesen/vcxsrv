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

#include "radeon/radeon_winsys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SI_PM4_MAX_DW 176

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
   unsigned last_opcode;
   unsigned last_reg;
   unsigned last_pm4;

   /* commands for the DE */
   unsigned ndw;
   uint32_t pm4[SI_PM4_MAX_DW];

   /* For shader states only */
   bool is_shader;
   struct si_atom atom;
};

void si_pm4_cmd_add(struct si_pm4_state *state, uint32_t dw);
void si_pm4_set_reg(struct si_pm4_state *state, unsigned reg, uint32_t val);

void si_pm4_clear_state(struct si_pm4_state *state);
void si_pm4_free_state(struct si_context *sctx, struct si_pm4_state *state, unsigned idx);

void si_pm4_emit(struct si_context *sctx, struct si_pm4_state *state);
void si_pm4_reset_emitted(struct si_context *sctx, bool first_cs);

#ifdef __cplusplus
}
#endif

#endif
