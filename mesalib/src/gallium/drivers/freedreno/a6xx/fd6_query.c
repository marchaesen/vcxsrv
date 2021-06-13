/*
 * Copyright (C) 2017 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

/* NOTE: see https://github.com/freedreno/freedreno/wiki/A5xx-Queries */

#include "freedreno_query_acc.h"
#include "freedreno_resource.h"

#include "fd6_context.h"
#include "fd6_emit.h"
#include "fd6_format.h"
#include "fd6_query.h"

struct PACKED fd6_query_sample {
	uint64_t start;
	uint64_t result;
	uint64_t stop;
};

/* offset of a single field of an array of fd6_query_sample: */
#define query_sample_idx(aq, idx, field)        \
	fd_resource((aq)->prsc)->bo,                \
	(idx * sizeof(struct fd6_query_sample)) +   \
	offsetof(struct fd6_query_sample, field),   \
	0, 0

/* offset of a single field of fd6_query_sample: */
#define query_sample(aq, field)                 \
	query_sample_idx(aq, 0, field)

/*
 * Occlusion Query:
 *
 * OCCLUSION_COUNTER and OCCLUSION_PREDICATE differ only in how they
 * interpret results
 */

static void
occlusion_resume(struct fd_acc_query *aq, struct fd_batch *batch)
{
	struct fd_ringbuffer *ring = batch->draw;

	OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_COUNT_CONTROL, 1);
	OUT_RING(ring, A6XX_RB_SAMPLE_COUNT_CONTROL_COPY);

	OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_COUNT_ADDR, 2);
	OUT_RELOC(ring, query_sample(aq, start));

	fd6_event_write(batch, ring, ZPASS_DONE, false);

	fd6_context(batch->ctx)->samples_passed_queries++;
}

static void
occlusion_pause(struct fd_acc_query *aq, struct fd_batch *batch)
	assert_dt
{
	struct fd_ringbuffer *ring = batch->draw;

	OUT_PKT7(ring, CP_MEM_WRITE, 4);
	OUT_RELOC(ring, query_sample(aq, stop));
	OUT_RING(ring, 0xffffffff);
	OUT_RING(ring, 0xffffffff);

	OUT_PKT7(ring, CP_WAIT_MEM_WRITES, 0);

	OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_COUNT_CONTROL, 1);
	OUT_RING(ring, A6XX_RB_SAMPLE_COUNT_CONTROL_COPY);

	OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_COUNT_ADDR, 2);
	OUT_RELOC(ring, query_sample(aq, stop));

	fd6_event_write(batch, ring, ZPASS_DONE, false);

	/* To avoid stalling in the draw buffer, emit code the code to compute the
	 * counter delta in the epilogue ring.
	 */
	struct fd_ringbuffer *epilogue = fd_batch_get_epilogue(batch);
	fd_wfi(batch, epilogue);

	/* result += stop - start: */
	OUT_PKT7(epilogue, CP_MEM_TO_MEM, 9);
	OUT_RING(epilogue, CP_MEM_TO_MEM_0_DOUBLE |
			CP_MEM_TO_MEM_0_NEG_C);
	OUT_RELOC(epilogue, query_sample(aq, result));     /* dst */
	OUT_RELOC(epilogue, query_sample(aq, result));      /* srcA */
	OUT_RELOC(epilogue, query_sample(aq, stop));        /* srcB */
	OUT_RELOC(epilogue, query_sample(aq, start));       /* srcC */

	fd6_context(batch->ctx)->samples_passed_queries--;
}

static void
occlusion_counter_result(struct fd_acc_query *aq, void *buf,
		union pipe_query_result *result)
{
	struct fd6_query_sample *sp = buf;
	result->u64 = sp->result;
}

static void
occlusion_predicate_result(struct fd_acc_query *aq, void *buf,
		union pipe_query_result *result)
{
	struct fd6_query_sample *sp = buf;
	result->b = !!sp->result;
}

static const struct fd_acc_sample_provider occlusion_counter = {
		.query_type = PIPE_QUERY_OCCLUSION_COUNTER,
		.size = sizeof(struct fd6_query_sample),
		.resume = occlusion_resume,
		.pause = occlusion_pause,
		.result = occlusion_counter_result,
};

static const struct fd_acc_sample_provider occlusion_predicate = {
		.query_type = PIPE_QUERY_OCCLUSION_PREDICATE,
		.size = sizeof(struct fd6_query_sample),
		.resume = occlusion_resume,
		.pause = occlusion_pause,
		.result = occlusion_predicate_result,
};

static const struct fd_acc_sample_provider occlusion_predicate_conservative = {
		.query_type = PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE,
		.size = sizeof(struct fd6_query_sample),
		.resume = occlusion_resume,
		.pause = occlusion_pause,
		.result = occlusion_predicate_result,
};

/*
 * Timestamp Queries:
 */

static void
timestamp_resume(struct fd_acc_query *aq, struct fd_batch *batch)
{
	struct fd_ringbuffer *ring = batch->draw;

	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS) |
			CP_EVENT_WRITE_0_TIMESTAMP);
	OUT_RELOC(ring, query_sample(aq, start));
	OUT_RING(ring, 0x00000000);

	fd_reset_wfi(batch);
}

static void
time_elapsed_pause(struct fd_acc_query *aq, struct fd_batch *batch)
	assert_dt
{
	struct fd_ringbuffer *ring = batch->draw;

	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS) |
			CP_EVENT_WRITE_0_TIMESTAMP);
	OUT_RELOC(ring, query_sample(aq, stop));
	OUT_RING(ring, 0x00000000);

	fd_reset_wfi(batch);
	fd_wfi(batch, ring);

	/* result += stop - start: */
	OUT_PKT7(ring, CP_MEM_TO_MEM, 9);
	OUT_RING(ring, CP_MEM_TO_MEM_0_DOUBLE |
			CP_MEM_TO_MEM_0_NEG_C);
	OUT_RELOC(ring, query_sample(aq, result));     /* dst */
	OUT_RELOC(ring, query_sample(aq, result));      /* srcA */
	OUT_RELOC(ring, query_sample(aq, stop));        /* srcB */
	OUT_RELOC(ring, query_sample(aq, start));       /* srcC */
}

static void
timestamp_pause(struct fd_acc_query *aq, struct fd_batch *batch)
{
	/* We captured a timestamp in timestamp_resume(), nothing to do here. */
}

/* timestamp logging for u_trace: */
static void
record_timestamp(struct fd_ringbuffer *ring, struct fd_bo *bo, unsigned offset)
{
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS) |
			CP_EVENT_WRITE_0_TIMESTAMP);
	OUT_RELOC(ring, bo, offset, 0, 0);
	OUT_RING(ring, 0x00000000);
}

static uint64_t
ticks_to_ns(uint64_t ts)
{
	/* This is based on the 19.2MHz always-on rbbm timer.
	 *
	 * TODO we should probably query this value from kernel..
	 */
	return ts * (1000000000 / 19200000);
}

static void
time_elapsed_accumulate_result(struct fd_acc_query *aq, void *buf,
		union pipe_query_result *result)
{
	struct fd6_query_sample *sp = buf;
	result->u64 = ticks_to_ns(sp->result);
}

static void
timestamp_accumulate_result(struct fd_acc_query *aq, void *buf,
		union pipe_query_result *result)
{
	struct fd6_query_sample *sp = buf;
	result->u64 = ticks_to_ns(sp->start);
}

static const struct fd_acc_sample_provider time_elapsed = {
		.query_type = PIPE_QUERY_TIME_ELAPSED,
		.always = true,
		.size = sizeof(struct fd6_query_sample),
		.resume = timestamp_resume,
		.pause = time_elapsed_pause,
		.result = time_elapsed_accumulate_result,
};

/* NOTE: timestamp query isn't going to give terribly sensible results
 * on a tiler.  But it is needed by qapitrace profile heatmap.  If you
 * add in a binning pass, the results get even more non-sensical.  So
 * we just return the timestamp on the last tile and hope that is
 * kind of good enough.
 */

static const struct fd_acc_sample_provider timestamp = {
		.query_type = PIPE_QUERY_TIMESTAMP,
		.always = true,
		.size = sizeof(struct fd6_query_sample),
		.resume = timestamp_resume,
		.pause = timestamp_pause,
		.result = timestamp_accumulate_result,
};

struct PACKED fd6_primitives_sample {
	struct {
		uint64_t emitted, generated;
	} start[4], stop[4], result;

	uint64_t prim_start[16], prim_stop[16], prim_emitted;
};


#define primitives_relocw(ring, aq, field) \
	OUT_RELOC(ring, fd_resource((aq)->prsc)->bo, offsetof(struct fd6_primitives_sample, field), 0, 0);
#define primitives_reloc(ring, aq, field) \
	OUT_RELOC(ring, fd_resource((aq)->prsc)->bo, offsetof(struct fd6_primitives_sample, field), 0, 0);

#ifdef DEBUG_COUNTERS
static const unsigned counter_count = 10;
static const unsigned counter_base = REG_A6XX_RBBM_PRIMCTR_0_LO;

static void
log_counters(struct fd6_primitives_sample *ps)
{
	const char *labels[] = {
		"vs_vertices_in",
		"vs_primitives_out",
		"hs_vertices_in",
		"hs_patches_out",
		"ds_vertices_in",
		"ds_primitives_out",
		"gs_primitives_in",
		"gs_primitives_out",
		"ras_primitives_in",
		"x",
	};

	printf("  counter\t\tstart\t\t\tstop\t\t\tdiff\n");
	for (int i = 0; i < counter_count; i++) {
		printf("  RBBM_PRIMCTR_%d\t0x%016llx\t0x%016llx\t%lld\t%s\n",
				i + (counter_base - REG_A6XX_RBBM_PRIMCTR_0_LO) / 2,
				ps->prim_start[i], ps->prim_stop[i], ps->prim_stop[i] - ps->prim_start[i], labels[i]);
	}

	printf("  so counts\n");
	for (int i = 0; i < ARRAY_SIZE(ps->start); i++) {
		printf("  CHANNEL %d emitted\t0x%016llx\t0x%016llx\t%lld\n",
				i, ps->start[i].generated, ps->stop[i].generated, ps->stop[i].generated - ps->start[i].generated);
		printf("  CHANNEL %d generated\t0x%016llx\t0x%016llx\t%lld\n",
				i, ps->start[i].emitted, ps->stop[i].emitted, ps->stop[i].emitted - ps->start[i].emitted);
	}

	printf("generated %lld, emitted %lld\n", ps->result.generated, ps->result.emitted);
}

#else

static const unsigned counter_count = 1;
static const unsigned counter_base = REG_A6XX_RBBM_PRIMCTR_8_LO;

static void
log_counters(struct fd6_primitives_sample *ps)
{
}

#endif

static void
primitives_generated_resume(struct fd_acc_query *aq, struct fd_batch *batch)
	assert_dt
{
	struct fd_ringbuffer *ring = batch->draw;

	fd_wfi(batch, ring);

	OUT_PKT7(ring, CP_REG_TO_MEM, 3);
	OUT_RING(ring, CP_REG_TO_MEM_0_64B |
			CP_REG_TO_MEM_0_CNT(counter_count) |
			CP_REG_TO_MEM_0_REG(counter_base));
	primitives_relocw(ring, aq, prim_start);

	fd6_event_write(batch, ring, START_PRIMITIVE_CTRS, false);
}

static void
primitives_generated_pause(struct fd_acc_query *aq, struct fd_batch *batch)
	assert_dt
{
	struct fd_ringbuffer *ring = batch->draw;

	fd_wfi(batch, ring);

	/* snapshot the end values: */
	OUT_PKT7(ring, CP_REG_TO_MEM, 3);
	OUT_RING(ring, CP_REG_TO_MEM_0_64B |
			CP_REG_TO_MEM_0_CNT(counter_count) |
			CP_REG_TO_MEM_0_REG(counter_base));
	primitives_relocw(ring, aq, prim_stop);

	fd6_event_write(batch, ring, STOP_PRIMITIVE_CTRS, false);

	/* result += stop - start: */
	OUT_PKT7(ring, CP_MEM_TO_MEM, 9);
	OUT_RING(ring, CP_MEM_TO_MEM_0_DOUBLE |
			CP_MEM_TO_MEM_0_NEG_C | 0x40000000);
	primitives_relocw(ring, aq, result.generated);
	primitives_reloc(ring, aq, prim_emitted);
	primitives_reloc(ring, aq, prim_stop[(REG_A6XX_RBBM_PRIMCTR_8_LO - counter_base) / 2])
		primitives_reloc(ring, aq, prim_start[(REG_A6XX_RBBM_PRIMCTR_8_LO - counter_base) / 2]);
}

static void
primitives_generated_result(struct fd_acc_query *aq, void *buf,
		union pipe_query_result *result)
{
	struct fd6_primitives_sample *ps = buf;

	log_counters(ps);

	result->u64 = ps->result.generated;
}

static const struct fd_acc_sample_provider primitives_generated = {
	.query_type = PIPE_QUERY_PRIMITIVES_GENERATED,
	.size = sizeof(struct fd6_primitives_sample),
	.resume = primitives_generated_resume,
	.pause = primitives_generated_pause,
	.result = primitives_generated_result,
};

static void
primitives_emitted_resume(struct fd_acc_query *aq, struct fd_batch *batch)
	assert_dt
{
	struct fd_ringbuffer *ring = batch->draw;

	fd_wfi(batch, ring);
	OUT_PKT4(ring, REG_A6XX_VPC_SO_STREAM_COUNTS, 2);
	primitives_relocw(ring, aq, start[0]);

	fd6_event_write(batch, ring, WRITE_PRIMITIVE_COUNTS, false);
}

static void
primitives_emitted_pause(struct fd_acc_query *aq, struct fd_batch *batch)
	assert_dt
{
	struct fd_ringbuffer *ring = batch->draw;

	fd_wfi(batch, ring);

	OUT_PKT4(ring, REG_A6XX_VPC_SO_STREAM_COUNTS, 2);
	primitives_relocw(ring, aq, stop[0]);
	fd6_event_write(batch, ring, WRITE_PRIMITIVE_COUNTS, false);

	fd6_event_write(batch, batch->draw, CACHE_FLUSH_TS, true);

	/* result += stop - start: */
	OUT_PKT7(ring, CP_MEM_TO_MEM, 9);
	OUT_RING(ring, CP_MEM_TO_MEM_0_DOUBLE |
			CP_MEM_TO_MEM_0_NEG_C | 0x80000000);
	primitives_relocw(ring, aq, result.emitted);
	primitives_reloc(ring, aq, result.emitted);
	primitives_reloc(ring, aq, stop[aq->base.index].emitted);
	primitives_reloc(ring, aq, start[aq->base.index].emitted);
}

static void
primitives_emitted_result(struct fd_acc_query *aq, void *buf,
		union pipe_query_result *result)
{
	struct fd6_primitives_sample *ps = buf;

	log_counters(ps);

	result->u64 = ps->result.emitted;
}

static const struct fd_acc_sample_provider primitives_emitted = {
	.query_type = PIPE_QUERY_PRIMITIVES_EMITTED,
	.size = sizeof(struct fd6_primitives_sample),
	.resume = primitives_emitted_resume,
	.pause = primitives_emitted_pause,
	.result = primitives_emitted_result,
};

/*
 * Performance Counter (batch) queries:
 *
 * Only one of these is active at a time, per design of the gallium
 * batch_query API design.  On perfcntr query tracks N query_types,
 * each of which has a 'fd_batch_query_entry' that maps it back to
 * the associated group and counter.
 */

struct fd_batch_query_entry {
	uint8_t gid;        /* group-id */
	uint8_t cid;        /* countable-id within the group */
};

struct fd_batch_query_data {
	struct fd_screen *screen;
	unsigned num_query_entries;
	struct fd_batch_query_entry query_entries[];
};

static void
perfcntr_resume(struct fd_acc_query *aq, struct fd_batch *batch)
	assert_dt
{
	struct fd_batch_query_data *data = aq->query_data;
	struct fd_screen *screen = data->screen;
	struct fd_ringbuffer *ring = batch->draw;

	unsigned counters_per_group[screen->num_perfcntr_groups];
	memset(counters_per_group, 0, sizeof(counters_per_group));

	fd_wfi(batch, ring);

	/* configure performance counters for the requested queries: */
	for (unsigned i = 0; i < data->num_query_entries; i++) {
		struct fd_batch_query_entry *entry = &data->query_entries[i];
		const struct fd_perfcntr_group *g = &screen->perfcntr_groups[entry->gid];
		unsigned counter_idx = counters_per_group[entry->gid]++;

		debug_assert(counter_idx < g->num_counters);

		OUT_PKT4(ring, g->counters[counter_idx].select_reg, 1);
		OUT_RING(ring, g->countables[entry->cid].selector);
	}

	memset(counters_per_group, 0, sizeof(counters_per_group));

	/* and snapshot the start values */
	for (unsigned i = 0; i < data->num_query_entries; i++) {
		struct fd_batch_query_entry *entry = &data->query_entries[i];
		const struct fd_perfcntr_group *g = &screen->perfcntr_groups[entry->gid];
		unsigned counter_idx = counters_per_group[entry->gid]++;
		const struct fd_perfcntr_counter *counter = &g->counters[counter_idx];

		OUT_PKT7(ring, CP_REG_TO_MEM, 3);
		OUT_RING(ring, CP_REG_TO_MEM_0_64B |
			CP_REG_TO_MEM_0_REG(counter->counter_reg_lo));
		OUT_RELOC(ring, query_sample_idx(aq, i, start));
	}
}

static void
perfcntr_pause(struct fd_acc_query *aq, struct fd_batch *batch)
	assert_dt
{
	struct fd_batch_query_data *data = aq->query_data;
	struct fd_screen *screen = data->screen;
	struct fd_ringbuffer *ring = batch->draw;

	unsigned counters_per_group[screen->num_perfcntr_groups];
	memset(counters_per_group, 0, sizeof(counters_per_group));

	fd_wfi(batch, ring);

	/* TODO do we need to bother to turn anything off? */

	/* snapshot the end values: */
	for (unsigned i = 0; i < data->num_query_entries; i++) {
		struct fd_batch_query_entry *entry = &data->query_entries[i];
		const struct fd_perfcntr_group *g = &screen->perfcntr_groups[entry->gid];
		unsigned counter_idx = counters_per_group[entry->gid]++;
		const struct fd_perfcntr_counter *counter = &g->counters[counter_idx];

		OUT_PKT7(ring, CP_REG_TO_MEM, 3);
		OUT_RING(ring, CP_REG_TO_MEM_0_64B |
			CP_REG_TO_MEM_0_REG(counter->counter_reg_lo));
		OUT_RELOC(ring, query_sample_idx(aq, i, stop));
	}

	/* and compute the result: */
	for (unsigned i = 0; i < data->num_query_entries; i++) {
		/* result += stop - start: */
		OUT_PKT7(ring, CP_MEM_TO_MEM, 9);
		OUT_RING(ring, CP_MEM_TO_MEM_0_DOUBLE |
				CP_MEM_TO_MEM_0_NEG_C);
		OUT_RELOC(ring, query_sample_idx(aq, i, result));     /* dst */
		OUT_RELOC(ring, query_sample_idx(aq, i, result));      /* srcA */
		OUT_RELOC(ring, query_sample_idx(aq, i, stop));        /* srcB */
		OUT_RELOC(ring, query_sample_idx(aq, i, start));       /* srcC */
	}
}

static void
perfcntr_accumulate_result(struct fd_acc_query *aq, void *buf,
		union pipe_query_result *result)
{
	struct fd_batch_query_data *data = aq->query_data;
	struct fd6_query_sample *sp = buf;

	for (unsigned i = 0; i < data->num_query_entries; i++) {
		result->batch[i].u64 = sp[i].result;
	}
}

static const struct fd_acc_sample_provider perfcntr = {
		.query_type = FD_QUERY_FIRST_PERFCNTR,
		.always = true,
		.resume = perfcntr_resume,
		.pause = perfcntr_pause,
		.result = perfcntr_accumulate_result,
};

static struct pipe_query *
fd6_create_batch_query(struct pipe_context *pctx,
		unsigned num_queries, unsigned *query_types)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd_screen *screen = ctx->screen;
	struct fd_query *q;
	struct fd_acc_query *aq;
	struct fd_batch_query_data *data;

	data = CALLOC_VARIANT_LENGTH_STRUCT(fd_batch_query_data,
			num_queries * sizeof(data->query_entries[0]));

	data->screen = screen;
	data->num_query_entries = num_queries;

	/* validate the requested query_types and ensure we don't try
	 * to request more query_types of a given group than we have
	 * counters:
	 */
	unsigned counters_per_group[screen->num_perfcntr_groups];
	memset(counters_per_group, 0, sizeof(counters_per_group));

	for (unsigned i = 0; i < num_queries; i++) {
		unsigned idx = query_types[i] - FD_QUERY_FIRST_PERFCNTR;

		/* verify valid query_type, ie. is it actually a perfcntr? */
		if ((query_types[i] < FD_QUERY_FIRST_PERFCNTR) ||
				(idx >= screen->num_perfcntr_queries)) {
			mesa_loge("invalid batch query query_type: %u", query_types[i]);
			goto error;
		}

		struct fd_batch_query_entry *entry = &data->query_entries[i];
		struct pipe_driver_query_info *pq = &screen->perfcntr_queries[idx];

		entry->gid = pq->group_id;

		/* the perfcntr_queries[] table flattens all the countables
		 * for each group in series, ie:
		 *
		 *   (G0,C0), .., (G0,Cn), (G1,C0), .., (G1,Cm), ...
		 *
		 * So to find the countable index just step back through the
		 * table to find the first entry with the same group-id.
		 */
		while (pq > screen->perfcntr_queries) {
			pq--;
			if (pq->group_id == entry->gid)
				entry->cid++;
		}

		if (counters_per_group[entry->gid] >=
				screen->perfcntr_groups[entry->gid].num_counters) {
			mesa_loge("too many counters for group %u", entry->gid);
			goto error;
		}

		counters_per_group[entry->gid]++;
	}

	q = fd_acc_create_query2(ctx, 0, 0, &perfcntr);
	aq = fd_acc_query(q);

	/* sample buffer size is based on # of queries: */
	aq->size = num_queries * sizeof(struct fd6_query_sample);
	aq->query_data = data;

	return (struct pipe_query *)q;

error:
	free(data);
	return NULL;
}

void
fd6_query_context_init(struct pipe_context *pctx)
	disable_thread_safety_analysis
{
	struct fd_context *ctx = fd_context(pctx);

	ctx->create_query = fd_acc_create_query;
	ctx->query_update_batch = fd_acc_query_update_batch;

	ctx->record_timestamp = record_timestamp;
	ctx->ts_to_ns = ticks_to_ns;

	pctx->create_batch_query = fd6_create_batch_query;

	fd_acc_query_register_provider(pctx, &occlusion_counter);
	fd_acc_query_register_provider(pctx, &occlusion_predicate);
	fd_acc_query_register_provider(pctx, &occlusion_predicate_conservative);

	fd_acc_query_register_provider(pctx, &time_elapsed);
	fd_acc_query_register_provider(pctx, &timestamp);

	fd_acc_query_register_provider(pctx, &primitives_generated);
	fd_acc_query_register_provider(pctx, &primitives_emitted);
}
