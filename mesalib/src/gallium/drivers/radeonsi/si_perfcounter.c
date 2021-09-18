/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "si_build_pm4.h"
#include "si_query.h"
#include "util/u_memory.h"

#include "ac_perfcounter.h"

struct si_query_group {
   struct si_query_group *next;
   struct ac_pc_block *block;
   unsigned sub_gid;     /* only used during init */
   unsigned result_base; /* only used during init */
   int se;
   int instance;
   unsigned num_counters;
   unsigned selectors[AC_QUERY_MAX_COUNTERS];
};

struct si_query_counter {
   unsigned base;
   unsigned qwords;
   unsigned stride; /* in uint64s */
};

struct si_query_pc {
   struct si_query b;
   struct si_query_buffer buffer;

   /* Size of the results in memory, in bytes. */
   unsigned result_size;

   unsigned shaders;
   unsigned num_counters;
   struct si_query_counter *counters;
   struct si_query_group *groups;
};

static void si_pc_emit_instance(struct si_context *sctx, int se, int instance)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned value = S_030800_SH_BROADCAST_WRITES(1);

   if (se >= 0) {
      value |= S_030800_SE_INDEX(se);
   } else {
      value |= S_030800_SE_BROADCAST_WRITES(1);
   }

   if (sctx->chip_class >= GFX10) {
      /* TODO: Expose counters from each shader array separately if needed. */
      value |= S_030800_SA_BROADCAST_WRITES(1);
   }

   if (instance >= 0) {
      value |= S_030800_INSTANCE_INDEX(instance);
   } else {
      value |= S_030800_INSTANCE_BROADCAST_WRITES(1);
   }

   radeon_begin(cs);
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX, value);
   radeon_end();
}

static void si_pc_emit_shaders(struct si_context *sctx, unsigned shaders)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   radeon_begin(cs);
   radeon_set_uconfig_reg_seq(cs, R_036780_SQ_PERFCOUNTER_CTRL, 2, false);
   radeon_emit(cs, shaders & 0x7f);
   radeon_emit(cs, 0xffffffff);
   radeon_end();
}

static void si_pc_emit_select(struct si_context *sctx, struct ac_pc_block *block, unsigned count,
                              unsigned *selectors)
{
   struct ac_pc_block_base *regs = block->b->b;
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned idx;

   assert(count <= regs->num_counters);

   /* Fake counters. */
   if (!regs->select0)
      return;

   radeon_begin(cs);

   for (idx = 0; idx < count; ++idx) {
      radeon_set_uconfig_reg_seq(cs, regs->select0[idx], 1, false);
      radeon_emit(cs, selectors[idx] | regs->select_or);
   }

   for (idx = 0; idx < regs->num_spm_counters; idx++) {
      radeon_set_uconfig_reg_seq(cs, regs->select1[idx], 1, false);
      radeon_emit(cs, 0);
   }

   radeon_end();
}

static void si_pc_emit_start(struct si_context *sctx, struct si_resource *buffer, uint64_t va)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   si_cp_copy_data(sctx, &sctx->gfx_cs, COPY_DATA_DST_MEM, buffer, va - buffer->gpu_address,
                   COPY_DATA_IMM, NULL, 1);

   radeon_begin(cs);
   radeon_set_uconfig_reg(cs, R_036020_CP_PERFMON_CNTL,
                          S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_DISABLE_AND_RESET));
   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_PERFCOUNTER_START) | EVENT_INDEX(0));
   radeon_set_uconfig_reg(cs, R_036020_CP_PERFMON_CNTL,
                          S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_START_COUNTING));
   radeon_end();
}

/* Note: The buffer was already added in si_pc_emit_start, so we don't have to
 * do it again in here. */
static void si_pc_emit_stop(struct si_context *sctx, struct si_resource *buffer, uint64_t va)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   si_cp_release_mem(sctx, cs, V_028A90_BOTTOM_OF_PIPE_TS, 0, EOP_DST_SEL_MEM, EOP_INT_SEL_NONE,
                     EOP_DATA_SEL_VALUE_32BIT, buffer, va, 0, SI_NOT_QUERY);
   si_cp_wait_mem(sctx, cs, va, 0, 0xffffffff, WAIT_REG_MEM_EQUAL);

   radeon_begin(cs);
   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_PERFCOUNTER_SAMPLE) | EVENT_INDEX(0));
   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_PERFCOUNTER_STOP) | EVENT_INDEX(0));
   radeon_set_uconfig_reg(
      cs, R_036020_CP_PERFMON_CNTL,
      S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_STOP_COUNTING) | S_036020_PERFMON_SAMPLE_ENABLE(1));
   radeon_end();
}

static void si_pc_emit_read(struct si_context *sctx, struct ac_pc_block *block, unsigned count,
                            uint64_t va)
{
   struct ac_pc_block_base *regs = block->b->b;
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned idx;
   unsigned reg = regs->counter0_lo;
   unsigned reg_delta = 8;

   radeon_begin(cs);

   if (regs->select0) {
      for (idx = 0; idx < count; ++idx) {
         if (regs->counters)
            reg = regs->counters[idx];

         radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
         radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_PERF) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                            COPY_DATA_COUNT_SEL); /* 64 bits */
         radeon_emit(cs, reg >> 2);
         radeon_emit(cs, 0); /* unused */
         radeon_emit(cs, va);
         radeon_emit(cs, va >> 32);
         va += sizeof(uint64_t);
         reg += reg_delta;
      }
   } else {
      /* Fake counters. */
      for (idx = 0; idx < count; ++idx) {
         radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
         radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                            COPY_DATA_COUNT_SEL);
         radeon_emit(cs, 0); /* immediate */
         radeon_emit(cs, 0);
         radeon_emit(cs, va);
         radeon_emit(cs, va >> 32);
         va += sizeof(uint64_t);
      }
   }
   radeon_end();
}

static void si_pc_query_destroy(struct si_context *sctx, struct si_query *squery)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   while (query->groups) {
      struct si_query_group *group = query->groups;
      query->groups = group->next;
      FREE(group);
   }

   FREE(query->counters);

   si_query_buffer_destroy(sctx->screen, &query->buffer);
   FREE(query);
}

void si_inhibit_clockgating(struct si_context *sctx, struct radeon_cmdbuf *cs, bool inhibit)
{
   radeon_begin(&sctx->gfx_cs);

   if (sctx->chip_class >= GFX10) {
      radeon_set_uconfig_reg(cs, R_037390_RLC_PERFMON_CLK_CNTL,
                             S_037390_PERFMON_CLOCK_STATE(inhibit));
   } else if (sctx->chip_class >= GFX8) {
      radeon_set_uconfig_reg(cs, R_0372FC_RLC_PERFMON_CLK_CNTL,
                             S_0372FC_PERFMON_CLOCK_STATE(inhibit));
   }
   radeon_end();
}

static void si_pc_query_resume(struct si_context *sctx, struct si_query *squery)
/*
                                   struct si_query_hw *hwquery,
                                   struct si_resource *buffer, uint64_t va)*/
{
   struct si_query_pc *query = (struct si_query_pc *)squery;
   int current_se = -1;
   int current_instance = -1;

   if (!si_query_buffer_alloc(sctx, &query->buffer, NULL, query->result_size))
      return;
   si_need_gfx_cs_space(sctx, 0);

   if (query->shaders)
      si_pc_emit_shaders(sctx, query->shaders);

   si_inhibit_clockgating(sctx, &sctx->gfx_cs, true);

   for (struct si_query_group *group = query->groups; group; group = group->next) {
      struct ac_pc_block *block = group->block;

      if (group->se != current_se || group->instance != current_instance) {
         current_se = group->se;
         current_instance = group->instance;
         si_pc_emit_instance(sctx, group->se, group->instance);
      }

      si_pc_emit_select(sctx, block, group->num_counters, group->selectors);
   }

   if (current_se != -1 || current_instance != -1)
      si_pc_emit_instance(sctx, -1, -1);

   uint64_t va = query->buffer.buf->gpu_address + query->buffer.results_end;
   si_pc_emit_start(sctx, query->buffer.buf, va);
}

static void si_pc_query_suspend(struct si_context *sctx, struct si_query *squery)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   if (!query->buffer.buf)
      return;

   uint64_t va = query->buffer.buf->gpu_address + query->buffer.results_end;
   query->buffer.results_end += query->result_size;

   si_pc_emit_stop(sctx, query->buffer.buf, va);

   for (struct si_query_group *group = query->groups; group; group = group->next) {
      struct ac_pc_block *block = group->block;
      unsigned se = group->se >= 0 ? group->se : 0;
      unsigned se_end = se + 1;

      if ((block->b->b->flags & AC_PC_BLOCK_SE) && (group->se < 0))
         se_end = sctx->screen->info.max_se;

      do {
         unsigned instance = group->instance >= 0 ? group->instance : 0;

         do {
            si_pc_emit_instance(sctx, se, instance);
            si_pc_emit_read(sctx, block, group->num_counters, va);
            va += sizeof(uint64_t) * group->num_counters;
         } while (group->instance < 0 && ++instance < block->num_instances);
      } while (++se < se_end);
   }

   si_pc_emit_instance(sctx, -1, -1);

   si_inhibit_clockgating(sctx, &sctx->gfx_cs, false);
}

static bool si_pc_query_begin(struct si_context *ctx, struct si_query *squery)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   si_query_buffer_reset(ctx, &query->buffer);

   list_addtail(&query->b.active_list, &ctx->active_queries);
   ctx->num_cs_dw_queries_suspend += query->b.num_cs_dw_suspend;

   si_pc_query_resume(ctx, squery);

   return true;
}

static bool si_pc_query_end(struct si_context *ctx, struct si_query *squery)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   si_pc_query_suspend(ctx, squery);

   list_del(&squery->active_list);
   ctx->num_cs_dw_queries_suspend -= squery->num_cs_dw_suspend;

   return query->buffer.buf != NULL;
}

static void si_pc_query_add_result(struct si_query_pc *query, void *buffer,
                                   union pipe_query_result *result)
{
   uint64_t *results = buffer;
   unsigned i, j;

   for (i = 0; i < query->num_counters; ++i) {
      struct si_query_counter *counter = &query->counters[i];

      for (j = 0; j < counter->qwords; ++j) {
         uint32_t value = results[counter->base + j * counter->stride];
         result->batch[i].u64 += value;
      }
   }
}

static bool si_pc_query_get_result(struct si_context *sctx, struct si_query *squery, bool wait,
                                   union pipe_query_result *result)
{
   struct si_query_pc *query = (struct si_query_pc *)squery;

   memset(result, 0, sizeof(result->batch[0]) * query->num_counters);

   for (struct si_query_buffer *qbuf = &query->buffer; qbuf; qbuf = qbuf->previous) {
      unsigned usage = PIPE_MAP_READ | (wait ? 0 : PIPE_MAP_DONTBLOCK);
      unsigned results_base = 0;
      void *map;

      if (squery->b.flushed)
         map = sctx->ws->buffer_map(sctx->ws, qbuf->buf->buf, NULL, usage);
      else
         map = si_buffer_map(sctx, qbuf->buf, usage);

      if (!map)
         return false;

      while (results_base != qbuf->results_end) {
         si_pc_query_add_result(query, map + results_base, result);
         results_base += query->result_size;
      }
   }

   return true;
}

static const struct si_query_ops batch_query_ops = {
   .destroy = si_pc_query_destroy,
   .begin = si_pc_query_begin,
   .end = si_pc_query_end,
   .get_result = si_pc_query_get_result,

   .suspend = si_pc_query_suspend,
   .resume = si_pc_query_resume,
};

static struct si_query_group *get_group_state(struct si_screen *screen, struct si_query_pc *query,
                                              struct ac_pc_block *block, unsigned sub_gid)
{
   struct si_perfcounters *pc = screen->perfcounters;
   struct si_query_group *group = query->groups;

   while (group) {
      if (group->block == block && group->sub_gid == sub_gid)
         return group;
      group = group->next;
   }

   group = CALLOC_STRUCT(si_query_group);
   if (!group)
      return NULL;

   group->block = block;
   group->sub_gid = sub_gid;

   if (block->b->b->flags & AC_PC_BLOCK_SHADER) {
      unsigned sub_gids = block->num_instances;
      unsigned shader_id;
      unsigned shaders;
      unsigned query_shaders;

      if (ac_pc_block_has_per_se_groups(&pc->base, block))
         sub_gids = sub_gids * screen->info.max_se;
      shader_id = sub_gid / sub_gids;
      sub_gid = sub_gid % sub_gids;

      shaders = ac_pc_shader_type_bits[shader_id];

      query_shaders = query->shaders & ~AC_PC_SHADERS_WINDOWING;
      if (query_shaders && query_shaders != shaders) {
         fprintf(stderr, "si_perfcounter: incompatible shader groups\n");
         FREE(group);
         return NULL;
      }
      query->shaders = shaders;
   }

   if (block->b->b->flags & AC_PC_BLOCK_SHADER_WINDOWED && !query->shaders) {
      // A non-zero value in query->shaders ensures that the shader
      // masking is reset unless the user explicitly requests one.
      query->shaders = AC_PC_SHADERS_WINDOWING;
   }

   if (ac_pc_block_has_per_se_groups(&pc->base, block)) {
      group->se = sub_gid / block->num_instances;
      sub_gid = sub_gid % block->num_instances;
   } else {
      group->se = -1;
   }

   if (ac_pc_block_has_per_instance_groups(&pc->base, block)) {
      group->instance = sub_gid;
   } else {
      group->instance = -1;
   }

   group->next = query->groups;
   query->groups = group;

   return group;
}

struct pipe_query *si_create_batch_query(struct pipe_context *ctx, unsigned num_queries,
                                         unsigned *query_types)
{
   struct si_screen *screen = (struct si_screen *)ctx->screen;
   struct si_perfcounters *pc = screen->perfcounters;
   struct ac_pc_block *block;
   struct si_query_group *group;
   struct si_query_pc *query;
   unsigned base_gid, sub_gid, sub_index;
   unsigned i, j;

   if (!pc)
      return NULL;

   query = CALLOC_STRUCT(si_query_pc);
   if (!query)
      return NULL;

   query->b.ops = &batch_query_ops;

   query->num_counters = num_queries;

   /* Collect selectors per group */
   for (i = 0; i < num_queries; ++i) {
      unsigned sub_gid;

      if (query_types[i] < SI_QUERY_FIRST_PERFCOUNTER)
         goto error;

      block =
         ac_lookup_counter(&pc->base, query_types[i] - SI_QUERY_FIRST_PERFCOUNTER, &base_gid, &sub_index);
      if (!block)
         goto error;

      sub_gid = sub_index / block->b->selectors;
      sub_index = sub_index % block->b->selectors;

      group = get_group_state(screen, query, block, sub_gid);
      if (!group)
         goto error;

      if (group->num_counters >= block->b->b->num_counters) {
         fprintf(stderr, "perfcounter group %s: too many selected\n", block->b->b->name);
         goto error;
      }
      group->selectors[group->num_counters] = sub_index;
      ++group->num_counters;
   }

   /* Compute result bases and CS size per group */
   query->b.num_cs_dw_suspend = pc->num_stop_cs_dwords;
   query->b.num_cs_dw_suspend += pc->num_instance_cs_dwords;

   i = 0;
   for (group = query->groups; group; group = group->next) {
      struct ac_pc_block *block = group->block;
      unsigned read_dw;
      unsigned instances = 1;

      if ((block->b->b->flags & AC_PC_BLOCK_SE) && group->se < 0)
         instances = screen->info.max_se;
      if (group->instance < 0)
         instances *= block->num_instances;

      group->result_base = i;
      query->result_size += sizeof(uint64_t) * instances * group->num_counters;
      i += instances * group->num_counters;

      read_dw = 6 * group->num_counters;
      query->b.num_cs_dw_suspend += instances * read_dw;
      query->b.num_cs_dw_suspend += instances * pc->num_instance_cs_dwords;
   }

   if (query->shaders) {
      if (query->shaders == AC_PC_SHADERS_WINDOWING)
         query->shaders = 0xffffffff;
   }

   /* Map user-supplied query array to result indices */
   query->counters = CALLOC(num_queries, sizeof(*query->counters));
   for (i = 0; i < num_queries; ++i) {
      struct si_query_counter *counter = &query->counters[i];
      struct ac_pc_block *block;

      block =
         ac_lookup_counter(&pc->base, query_types[i] - SI_QUERY_FIRST_PERFCOUNTER, &base_gid, &sub_index);

      sub_gid = sub_index / block->b->selectors;
      sub_index = sub_index % block->b->selectors;

      group = get_group_state(screen, query, block, sub_gid);
      assert(group != NULL);

      for (j = 0; j < group->num_counters; ++j) {
         if (group->selectors[j] == sub_index)
            break;
      }

      counter->base = group->result_base + j;
      counter->stride = group->num_counters;

      counter->qwords = 1;
      if ((block->b->b->flags & AC_PC_BLOCK_SE) && group->se < 0)
         counter->qwords = screen->info.max_se;
      if (group->instance < 0)
         counter->qwords *= block->num_instances;
   }

   return (struct pipe_query *)query;

error:
   si_pc_query_destroy((struct si_context *)ctx, &query->b);
   return NULL;
}

int si_get_perfcounter_info(struct si_screen *screen, unsigned index,
                            struct pipe_driver_query_info *info)
{
   struct si_perfcounters *pc = screen->perfcounters;
   struct ac_pc_block *block;
   unsigned base_gid, sub;

   if (!pc)
      return 0;

   if (!info) {
      unsigned bid, num_queries = 0;

      for (bid = 0; bid < pc->base.num_blocks; ++bid) {
         num_queries += pc->base.blocks[bid].b->selectors * pc->base.blocks[bid].num_groups;
      }

      return num_queries;
   }

   block = ac_lookup_counter(&pc->base, index, &base_gid, &sub);
   if (!block)
      return 0;

   if (!block->selector_names) {
      if (!ac_init_block_names(&screen->info, &pc->base, block))
         return 0;
   }
   info->name = block->selector_names + sub * block->selector_name_stride;
   info->query_type = SI_QUERY_FIRST_PERFCOUNTER + index;
   info->max_value.u64 = 0;
   info->type = PIPE_DRIVER_QUERY_TYPE_UINT64;
   info->result_type = PIPE_DRIVER_QUERY_RESULT_TYPE_AVERAGE;
   info->group_id = base_gid + sub / block->b->selectors;
   info->flags = PIPE_DRIVER_QUERY_FLAG_BATCH;
   if (sub > 0 && sub + 1 < block->b->selectors * block->num_groups)
      info->flags |= PIPE_DRIVER_QUERY_FLAG_DONT_LIST;
   return 1;
}

int si_get_perfcounter_group_info(struct si_screen *screen, unsigned index,
                                  struct pipe_driver_query_group_info *info)
{
   struct si_perfcounters *pc = screen->perfcounters;
   struct ac_pc_block *block;

   if (!pc)
      return 0;

   if (!info)
      return pc->base.num_groups;

   block = ac_lookup_group(&pc->base, &index);
   if (!block)
      return 0;

   if (!block->group_names) {
      if (!ac_init_block_names(&screen->info, &pc->base, block))
         return 0;
   }
   info->name = block->group_names + index * block->group_name_stride;
   info->num_queries = block->b->selectors;
   info->max_active_queries = block->b->b->num_counters;
   return 1;
}

void si_destroy_perfcounters(struct si_screen *screen)
{
   struct si_perfcounters *pc = screen->perfcounters;

   if (!pc)
      return;

   ac_destroy_perfcounters(&pc->base);
   screen->perfcounters = NULL;
}

void si_init_perfcounters(struct si_screen *screen)
{
   bool separate_se, separate_instance;

   separate_se = debug_get_bool_option("RADEON_PC_SEPARATE_SE", false);
   separate_instance = debug_get_bool_option("RADEON_PC_SEPARATE_INSTANCE", false);

   screen->perfcounters = CALLOC_STRUCT(si_perfcounters);
   if (!screen->perfcounters)
      return;

   screen->perfcounters->num_stop_cs_dwords = 14 + si_cp_write_fence_dwords(screen);
   screen->perfcounters->num_instance_cs_dwords = 3;

   if (!ac_init_perfcounters(&screen->info, separate_se, separate_instance,
                             &screen->perfcounters->base)) {
      si_destroy_perfcounters(screen);
   }
}
