/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_PM4_H
#define SI_PM4_H

#include <stdint.h>
#include <stdbool.h>

#include "ac_pm4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* forward definitions */
struct si_screen;
struct si_context;

/* State atoms are callbacks which write a sequence of packets into a GPU
 * command buffer (AKA indirect buffer, AKA IB, AKA command stream, AKA CS).
 */
struct si_atom {
   /* The index is only used by si_pm4_emit_state. Non-pm4 atoms don't use it. */
   void (*emit)(struct si_context *ctx, unsigned index);
};

struct si_pm4_state {
   /* For shader states only */
   struct si_atom atom;

   struct ac_pm4_state base;
};

void si_pm4_clear_state(struct si_pm4_state *state, struct si_screen *sscreen,
                        bool is_compute_queue);
void si_pm4_free_state(struct si_context *sctx, struct si_pm4_state *state, unsigned idx);

void si_pm4_emit_commands(struct si_context *sctx, struct si_pm4_state *state);
void si_pm4_emit_state(struct si_context *sctx, unsigned index);
void si_pm4_emit_shader(struct si_context *sctx, unsigned index);
void si_pm4_reset_emitted(struct si_context *sctx);
struct si_pm4_state *si_pm4_create_sized(struct si_screen *sscreen, unsigned max_dw,
                                         bool is_compute_queue);
struct si_pm4_state *si_pm4_clone(struct si_screen *sscreen, struct si_pm4_state *orig);

#ifdef __cplusplus
}
#endif

#endif
