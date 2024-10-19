/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pm4.h"
#include "si_pipe.h"
#include "si_build_pm4.h"
#include "sid.h"
#include "util/u_memory.h"
#include "ac_debug.h"

void si_pm4_clear_state(struct si_pm4_state *state, struct si_screen *sscreen,
                        bool is_compute_queue)
{
   const bool debug_sqtt = !!(sscreen->debug_flags & DBG(SQTT));

   ac_pm4_clear_state(&state->base, &sscreen->info, debug_sqtt, is_compute_queue);
}

void si_pm4_free_state(struct si_context *sctx, struct si_pm4_state *state, unsigned idx)
{
   if (!state)
      return;

   if (idx != ~0) {
      if (sctx->emitted.array[idx] == state)
         sctx->emitted.array[idx] = NULL;

      if (sctx->queued.array[idx] == state) {
         sctx->queued.array[idx] = NULL;
         sctx->dirty_atoms &= ~BITFIELD64_BIT(idx);
      }
   }

   FREE(state);
}

void si_pm4_emit_commands(struct si_context *sctx, struct si_pm4_state *state)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   radeon_begin(cs);
   radeon_emit_array(state->base.pm4, state->base.ndw);
   radeon_end();
}

void si_pm4_emit_state(struct si_context *sctx, unsigned index)
{
   struct si_pm4_state *state = sctx->queued.array[index];
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   /* All places should unset dirty_states if this doesn't pass. */
   assert(state && state != sctx->emitted.array[index]);

   radeon_begin(cs);
   radeon_emit_array(state->base.pm4, state->base.ndw);
   radeon_end();

   sctx->emitted.array[index] = state;
}

void si_pm4_emit_shader(struct si_context *sctx, unsigned index)
{
   struct si_pm4_state *state = sctx->queued.array[index];

   si_pm4_emit_state(sctx, index);

   radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, ((struct si_shader*)state)->bo,
                             RADEON_USAGE_READ | RADEON_PRIO_SHADER_BINARY);
   if (state->atom.emit)
      state->atom.emit(sctx, -1);
}

void si_pm4_reset_emitted(struct si_context *sctx)
{
   memset(&sctx->emitted, 0, sizeof(sctx->emitted));

   for (unsigned i = 0; i < SI_NUM_STATES; i++) {
      if (sctx->queued.array[i])
         sctx->dirty_atoms |= BITFIELD64_BIT(i);
   }
}

struct si_pm4_state *si_pm4_create_sized(struct si_screen *sscreen, unsigned max_dw,
                                         bool is_compute_queue)
{
   struct si_pm4_state *pm4;
   unsigned size = sizeof(*pm4) + 4 * (max_dw - ARRAY_SIZE(pm4->base.pm4));

   pm4 = (struct si_pm4_state *)calloc(1, size);
   if (pm4) {
      pm4->base.max_dw = max_dw;
      si_pm4_clear_state(pm4, sscreen, is_compute_queue);
   }
   return pm4;
}

struct si_pm4_state *si_pm4_clone(struct si_screen *sscreen, struct si_pm4_state *orig)
{
   struct si_pm4_state *pm4 = si_pm4_create_sized(sscreen, orig->base.max_dw,
                                                  orig->base.is_compute_queue);
   if (pm4)
      memcpy(pm4, orig, sizeof(*pm4) + 4 * (pm4->base.max_dw - ARRAY_SIZE(pm4->base.pm4)));
   return pm4;
}
