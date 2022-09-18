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

#define SPM_RING_BASE_ALIGN 32

static bool
radv_spm_init_bo(struct radv_device *device)
{
   struct radeon_winsys *ws = device->ws;
   uint64_t size = 32 * 1024 * 1024; /* Default to 1MB. */
   uint16_t sample_interval = 4096; /* Default to 4096 clk. */
   VkResult result;

   device->spm_trace.buffer_size = size;
   device->spm_trace.sample_interval = sample_interval;

   struct radeon_winsys_bo *bo = NULL;
   result = ws->buffer_create(
      ws, size, 4096, RADEON_DOMAIN_VRAM,
      RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_ZERO_VRAM,
      RADV_BO_PRIORITY_SCRATCH, 0, &bo);
   device->spm_trace.bo = bo;
   if (result != VK_SUCCESS)
      return false;

   result = ws->buffer_make_resident(ws, device->spm_trace.bo, true);
   if (result != VK_SUCCESS)
      return false;

   device->spm_trace.ptr = ws->buffer_map(device->spm_trace.bo);
   if (!device->spm_trace.ptr)
      return false;

   return true;
}

static void
radv_emit_spm_counters(struct radv_device *device, struct radeon_cmdbuf *cs)
{
   struct ac_spm_trace_data *spm_trace = &device->spm_trace;

   for (uint32_t b = 0; b < spm_trace->num_used_sq_block_sel; b++) {
      struct ac_spm_block_select *sq_block_sel = &spm_trace->sq_block_sel[b];
      const struct ac_spm_counter_select *cntr_sel = &sq_block_sel->counters[0];
      uint32_t reg_base = R_036700_SQ_PERFCOUNTER0_SELECT;

      radeon_set_uconfig_reg_seq(cs, reg_base + b * 4, 1);
      radeon_emit(cs, cntr_sel->sel0 | S_036700_SQC_BANK_MASK(0xf)); /* SQC_BANK_MASK only gfx10 */
   }

   for (uint32_t b = 0; b < spm_trace->num_block_sel; b++) {
      struct ac_spm_block_select *block_sel = &spm_trace->block_sel[b];
      struct ac_pc_block_base *regs = block_sel->b->b->b;

      radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX, block_sel->grbm_gfx_index);

      for (unsigned c = 0; c < block_sel->num_counters; c++) {
         const struct ac_spm_counter_select *cntr_sel = &block_sel->counters[c];

         if (!cntr_sel->active)
            continue;

         radeon_set_uconfig_reg_seq(cs, regs->select0[c], 1);
         radeon_emit(cs, cntr_sel->sel0);

         radeon_set_uconfig_reg_seq(cs, regs->select1[c], 1);
         radeon_emit(cs, cntr_sel->sel1);
      }
   }

   /* Restore global broadcasting. */
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                              S_030800_SE_BROADCAST_WRITES(1) | S_030800_SH_BROADCAST_WRITES(1) |
                              S_030800_INSTANCE_BROADCAST_WRITES(1));
}

void
radv_emit_spm_setup(struct radv_device *device, struct radeon_cmdbuf *cs)
{
   struct ac_spm_trace_data *spm_trace = &device->spm_trace;
   uint64_t va = radv_buffer_get_va(spm_trace->bo);
   uint64_t ring_size = spm_trace->buffer_size;

   /* It's required that the ring VA and the size are correctly aligned. */
   assert(!(va & (SPM_RING_BASE_ALIGN - 1)));
   assert(!(ring_size & (SPM_RING_BASE_ALIGN - 1)));
   assert(spm_trace->sample_interval >= 32);

   /* Configure the SPM ring buffer. */
   radeon_set_uconfig_reg(cs, R_037200_RLC_SPM_PERFMON_CNTL,
                              S_037200_PERFMON_RING_MODE(0) | /* no stall and no interrupt on overflow */
                              S_037200_PERFMON_SAMPLE_INTERVAL(spm_trace->sample_interval)); /* in sclk */
   radeon_set_uconfig_reg(cs, R_037204_RLC_SPM_PERFMON_RING_BASE_LO, va);
   radeon_set_uconfig_reg(cs, R_037208_RLC_SPM_PERFMON_RING_BASE_HI,
                              S_037208_RING_BASE_HI(va >> 32));
   radeon_set_uconfig_reg(cs, R_03720C_RLC_SPM_PERFMON_RING_SIZE, ring_size);

   /* Configure the muxsel. */
   uint32_t total_muxsel_lines = 0;
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      total_muxsel_lines += spm_trace->num_muxsel_lines[s];
   }

   radeon_set_uconfig_reg(cs, R_03726C_RLC_SPM_ACCUM_MODE, 0);
   radeon_set_uconfig_reg(cs, R_037210_RLC_SPM_PERFMON_SEGMENT_SIZE, 0);
   radeon_set_uconfig_reg(cs, R_03727C_RLC_SPM_PERFMON_SE3TO0_SEGMENT_SIZE,
                              S_03727C_SE0_NUM_LINE(spm_trace->num_muxsel_lines[0]) |
                              S_03727C_SE1_NUM_LINE(spm_trace->num_muxsel_lines[1]) |
                              S_03727C_SE2_NUM_LINE(spm_trace->num_muxsel_lines[2]) |
                              S_03727C_SE3_NUM_LINE(spm_trace->num_muxsel_lines[3]));
   radeon_set_uconfig_reg(cs, R_037280_RLC_SPM_PERFMON_GLB_SEGMENT_SIZE,
                              S_037280_PERFMON_SEGMENT_SIZE(total_muxsel_lines) |
                              S_037280_GLOBAL_NUM_LINE(spm_trace->num_muxsel_lines[4]));

   /* Upload each muxsel ram to the RLC. */
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      unsigned rlc_muxsel_addr, rlc_muxsel_data;
      unsigned grbm_gfx_index = S_030800_SH_BROADCAST_WRITES(1) |
                                S_030800_INSTANCE_BROADCAST_WRITES(1);

      if (!spm_trace->num_muxsel_lines[s])
         continue;

      if (s == AC_SPM_SEGMENT_TYPE_GLOBAL) {
         grbm_gfx_index |= S_030800_SE_BROADCAST_WRITES(1);

         rlc_muxsel_addr = R_037224_RLC_SPM_GLOBAL_MUXSEL_ADDR;
         rlc_muxsel_data = R_037228_RLC_SPM_GLOBAL_MUXSEL_DATA;
      } else {
         grbm_gfx_index |= S_030800_SE_INDEX(s);

         rlc_muxsel_addr = R_03721C_RLC_SPM_SE_MUXSEL_ADDR;
         rlc_muxsel_data = R_037220_RLC_SPM_SE_MUXSEL_DATA;
      }

      radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX, grbm_gfx_index);

      for (unsigned l = 0; l < spm_trace->num_muxsel_lines[s]; l++) {
         uint32_t *data = (uint32_t *)spm_trace->muxsel_lines[s][l].muxsel;

         /* Select MUXSEL_ADDR to point to the next muxsel. */
         radeon_set_uconfig_reg(cs, rlc_muxsel_addr, l * AC_SPM_MUXSEL_LINE_SIZE);

         /* Write the muxsel line configuration with MUXSEL_DATA. */
         radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 2 + AC_SPM_MUXSEL_LINE_SIZE, 0));
         radeon_emit(cs, S_370_DST_SEL(V_370_MEM_MAPPED_REGISTER) |
                         S_370_WR_CONFIRM(1) |
                         S_370_ENGINE_SEL(V_370_ME) |
                         S_370_WR_ONE_ADDR(1));
         radeon_emit(cs, rlc_muxsel_data >> 2);
         radeon_emit(cs, 0);
         radeon_emit_array(cs, data, AC_SPM_MUXSEL_LINE_SIZE);
      }
   }

   /* Select SPM counters. */
   radv_emit_spm_counters(device, cs);
}

bool
radv_spm_init(struct radv_device *device)
{
   const struct radeon_info *info = &device->physical_device->rad_info;
   struct ac_perfcounters *pc = &device->physical_device->ac_perfcounters;
   struct ac_spm_counter_create_info spm_counters[] = {
      {TCP, 0, 0x9},    /* Number of L2 requests. */
      {TCP, 0, 0x12},   /* Number of L2 misses. */
      {SQ, 0, 0x14f},   /* Number of SCACHE hits. */
      {SQ, 0, 0x150},   /* Number of SCACHE misses. */
      {SQ, 0, 0x151},   /* Number of SCACHE misses duplicate. */
      {SQ, 0, 0x12c},   /* Number of ICACHE hits. */
      {SQ, 0, 0x12d},   /* Number of ICACHE misses. */
      {SQ, 0, 0x12e},   /* Number of ICACHE misses duplicate. */
      {GL1C, 0, 0xe},   /* Number of GL1C requests. */
      {GL1C, 0, 0x12},  /* Number of GL1C misses. */
      {GL2C, 0, 0x3},   /* Number of GL2C requests. */
      {GL2C, 0, info->gfx_level >= GFX10_3 ? 0x2b : 0x23},  /* Number of GL2C misses. */
   };

   /* We failed to initialize the performance counters. */
   if (!pc->blocks)
      return false;

   if (!ac_init_spm(info, pc, ARRAY_SIZE(spm_counters), spm_counters, &device->spm_trace))
      return false;

   if (!radv_spm_init_bo(device))
      return false;

   return true;
}

void
radv_spm_finish(struct radv_device *device)
{
   struct radeon_winsys *ws = device->ws;

   if (device->spm_trace.bo) {
      ws->buffer_make_resident(ws, device->spm_trace.bo, false);
      ws->buffer_destroy(ws, device->spm_trace.bo);
   }

   ac_destroy_spm(&device->spm_trace);
}
