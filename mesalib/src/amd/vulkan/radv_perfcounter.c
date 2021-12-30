/*
 * Copyright © 2021 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <inttypes.h>

#include "radv_cs.h"
#include "radv_private.h"
#include "sid.h"

void
radv_perfcounter_emit_shaders(struct radeon_cmdbuf *cs, unsigned shaders)
{
   radeon_set_uconfig_reg_seq(cs, R_036780_SQ_PERFCOUNTER_CTRL, 2);
   radeon_emit(cs, shaders & 0x7f);
   radeon_emit(cs, 0xffffffff);
}

void
radv_perfcounter_emit_reset(struct radeon_cmdbuf *cs)
{
   radeon_set_uconfig_reg(cs, R_036020_CP_PERFMON_CNTL,
                              S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_DISABLE_AND_RESET) |
                              S_036020_SPM_PERFMON_STATE(V_036020_STRM_PERFMON_STATE_DISABLE_AND_RESET));
}

void
radv_perfcounter_emit_start(struct radv_device *device, struct radeon_cmdbuf *cs, int family)
{
   /* Start SPM counters. */
   radeon_set_uconfig_reg(cs, R_036020_CP_PERFMON_CNTL,
                              S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_DISABLE_AND_RESET) |
                              S_036020_SPM_PERFMON_STATE(V_036020_STRM_PERFMON_STATE_START_COUNTING));

   /* Start windowed performance counters. */
   if (family == RADV_QUEUE_GENERAL) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_PERFCOUNTER_START) | EVENT_INDEX(0));
   }
   radeon_set_sh_reg(cs, R_00B82C_COMPUTE_PERFCOUNT_ENABLE, S_00B82C_PERFCOUNT_ENABLE(1));
}

void
radv_perfcounter_emit_stop(struct radv_device *device, struct radeon_cmdbuf *cs, int family)
{
   /* Stop windowed performance counters. */
   if (family == RADV_QUEUE_GENERAL) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_PERFCOUNTER_STOP) | EVENT_INDEX(0));
   }
   radeon_set_sh_reg(cs, R_00B82C_COMPUTE_PERFCOUNT_ENABLE, S_00B82C_PERFCOUNT_ENABLE(0));

   /* Stop SPM counters. */
   radeon_set_uconfig_reg(cs, R_036020_CP_PERFMON_CNTL,
                              S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_DISABLE_AND_RESET) |
                              S_036020_SPM_PERFMON_STATE(V_036020_STRM_PERFMON_STATE_STOP_COUNTING));
}
