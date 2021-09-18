/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include "si_build_pm4.h"
#include "ac_debug.h"
#include "ac_shadowed_regs.h"
#include "util/u_memory.h"

static void si_build_load_reg(struct si_screen *sscreen, struct si_pm4_state *pm4,
                              enum ac_reg_range_type type,
                              struct si_resource *shadow_regs)
{
   uint64_t gpu_address = shadow_regs->gpu_address;
   unsigned packet, num_ranges, offset;
   const struct ac_reg_range *ranges;

   ac_get_reg_ranges(sscreen->info.chip_class, sscreen->info.family,
                     type, &num_ranges, &ranges);

   switch (type) {
   case SI_REG_RANGE_UCONFIG:
      gpu_address += SI_SHADOWED_UCONFIG_REG_OFFSET;
      offset = CIK_UCONFIG_REG_OFFSET;
      packet = PKT3_LOAD_UCONFIG_REG;
      break;
   case SI_REG_RANGE_CONTEXT:
      gpu_address += SI_SHADOWED_CONTEXT_REG_OFFSET;
      offset = SI_CONTEXT_REG_OFFSET;
      packet = PKT3_LOAD_CONTEXT_REG;
      break;
   default:
      gpu_address += SI_SHADOWED_SH_REG_OFFSET;
      offset = SI_SH_REG_OFFSET;
      packet = PKT3_LOAD_SH_REG;
      break;
   }

   si_pm4_cmd_add(pm4, PKT3(packet, 1 + num_ranges * 2, 0));
   si_pm4_cmd_add(pm4, gpu_address);
   si_pm4_cmd_add(pm4, gpu_address >> 32);
   for (unsigned i = 0; i < num_ranges; i++) {
      si_pm4_cmd_add(pm4, (ranges[i].offset - offset) / 4);
      si_pm4_cmd_add(pm4, ranges[i].size / 4);
   }
}

static struct si_pm4_state *
si_create_shadowing_ib_preamble(struct si_context *sctx)
{
   struct si_pm4_state *pm4 = CALLOC_STRUCT(si_pm4_state);

   if (sctx->screen->dpbb_allowed) {
      si_pm4_cmd_add(pm4, PKT3(PKT3_EVENT_WRITE, 0, 0));
      si_pm4_cmd_add(pm4, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
   }

   /* Wait for idle, because we'll update VGT ring pointers. */
   si_pm4_cmd_add(pm4, PKT3(PKT3_EVENT_WRITE, 0, 0));
   si_pm4_cmd_add(pm4, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));

   /* VGT_FLUSH is required even if VGT is idle. It resets VGT pointers. */
   si_pm4_cmd_add(pm4, PKT3(PKT3_EVENT_WRITE, 0, 0));
   si_pm4_cmd_add(pm4, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));

   if (sctx->chip_class >= GFX10) {
      unsigned gcr_cntl = S_586_GL2_INV(1) | S_586_GL2_WB(1) |
                          S_586_GLM_INV(1) | S_586_GLM_WB(1) |
                          S_586_GL1_INV(1) | S_586_GLV_INV(1) |
                          S_586_GLK_INV(1) | S_586_GLI_INV(V_586_GLI_ALL);

      si_pm4_cmd_add(pm4, PKT3(PKT3_ACQUIRE_MEM, 6, 0));
      si_pm4_cmd_add(pm4, 0);		/* CP_COHER_CNTL */
      si_pm4_cmd_add(pm4, 0xffffffff);  /* CP_COHER_SIZE */
      si_pm4_cmd_add(pm4, 0xffffff);	/* CP_COHER_SIZE_HI */
      si_pm4_cmd_add(pm4, 0);		/* CP_COHER_BASE */
      si_pm4_cmd_add(pm4, 0);		/* CP_COHER_BASE_HI */
      si_pm4_cmd_add(pm4, 0x0000000A);  /* POLL_INTERVAL */
      si_pm4_cmd_add(pm4, gcr_cntl);	/* GCR_CNTL */
   } else if (sctx->chip_class == GFX9) {
      unsigned cp_coher_cntl = S_0301F0_SH_ICACHE_ACTION_ENA(1) |
                               S_0301F0_SH_KCACHE_ACTION_ENA(1) |
                               S_0301F0_TC_ACTION_ENA(1) |
                               S_0301F0_TCL1_ACTION_ENA(1) |
                               S_0301F0_TC_WB_ACTION_ENA(1);

      si_pm4_cmd_add(pm4, PKT3(PKT3_ACQUIRE_MEM, 5, 0));
      si_pm4_cmd_add(pm4, cp_coher_cntl); /* CP_COHER_CNTL */
      si_pm4_cmd_add(pm4, 0xffffffff);    /* CP_COHER_SIZE */
      si_pm4_cmd_add(pm4, 0xffffff);	  /* CP_COHER_SIZE_HI */
      si_pm4_cmd_add(pm4, 0);		  /* CP_COHER_BASE */
      si_pm4_cmd_add(pm4, 0);		  /* CP_COHER_BASE_HI */
      si_pm4_cmd_add(pm4, 0x0000000A);    /* POLL_INTERVAL */
   } else {
      unreachable("invalid chip");
   }

   si_pm4_cmd_add(pm4, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
   si_pm4_cmd_add(pm4, 0);

   si_pm4_cmd_add(pm4, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
   si_pm4_cmd_add(pm4,
                  CC0_UPDATE_LOAD_ENABLES(1) |
                  CC0_LOAD_PER_CONTEXT_STATE(1) |
                  CC0_LOAD_CS_SH_REGS(1) |
                  CC0_LOAD_GFX_SH_REGS(1) |
                  CC0_LOAD_GLOBAL_UCONFIG(1));
   si_pm4_cmd_add(pm4,
                  CC1_UPDATE_SHADOW_ENABLES(1) |
                  CC1_SHADOW_PER_CONTEXT_STATE(1) |
                  CC1_SHADOW_CS_SH_REGS(1) |
                  CC1_SHADOW_GFX_SH_REGS(1) |
                  CC1_SHADOW_GLOBAL_UCONFIG(1));

   for (unsigned i = 0; i < SI_NUM_SHADOWED_REG_RANGES; i++)
      si_build_load_reg(sctx->screen, pm4, i, sctx->shadowed_regs);

   return pm4;
}

static void si_set_context_reg_array(struct radeon_cmdbuf *cs, unsigned reg, unsigned num,
                                     const uint32_t *values)
{
   radeon_begin(cs);
   radeon_set_context_reg_seq(cs, reg, num);
   radeon_emit_array(cs, values, num);
   radeon_end();
}

void si_init_cp_reg_shadowing(struct si_context *sctx)
{
   if (sctx->screen->info.mid_command_buffer_preemption_enabled ||
       sctx->screen->debug_flags & DBG(SHADOW_REGS)) {
      sctx->shadowed_regs =
            si_aligned_buffer_create(sctx->b.screen,
                                     SI_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                     PIPE_USAGE_DEFAULT,
                                     SI_SHADOWED_REG_BUFFER_SIZE,
                                     4096);
      if (!sctx->shadowed_regs)
         fprintf(stderr, "radeonsi: cannot create a shadowed_regs buffer\n");
   }

   si_init_cs_preamble_state(sctx, sctx->shadowed_regs != NULL);

   if (sctx->shadowed_regs) {
      /* We need to clear the shadowed reg buffer. */
      si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, &sctx->shadowed_regs->b.b,
                             0, sctx->shadowed_regs->bo_size, 0, SI_OP_SYNC_AFTER,
                             SI_COHERENCY_CP, L2_BYPASS);

      /* Create the shadowing preamble. */
      struct si_pm4_state *shadowing_preamble =
            si_create_shadowing_ib_preamble(sctx);

      /* Initialize shadowed registers as follows. */
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->shadowed_regs,
                                RADEON_USAGE_READWRITE, RADEON_PRIO_DESCRIPTORS);
      si_pm4_emit(sctx, shadowing_preamble);
      ac_emulate_clear_state(&sctx->screen->info, &sctx->gfx_cs, si_set_context_reg_array);
      si_pm4_emit(sctx, sctx->cs_preamble_state);

      /* The register values are shadowed, so we won't need to set them again. */
      si_pm4_free_state(sctx, sctx->cs_preamble_state, ~0);
      sctx->cs_preamble_state = NULL;

      si_set_tracked_regs_to_clear_state(sctx);

      /* Setup preemption. The shadowing preamble will be executed as a preamble IB,
       * which will load register values from memory on a context switch.
       */
      sctx->ws->cs_setup_preemption(&sctx->gfx_cs, shadowing_preamble->pm4,
                                    shadowing_preamble->ndw);
      si_pm4_free_state(sctx, shadowing_preamble, ~0);
   }
}
