/*
 * Copyright (C) 2017 Rob Clark <robclark@freedesktop.org>
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

#include "pipe/p_state.h"

#include "freedreno_resource.h"

#include "fd5_compute.h"
#include "fd5_context.h"
#include "fd5_emit.h"

struct fd5_compute_stateobj {
	struct ir3_shader *shader;
};


static void *
fd5_create_compute_state(struct pipe_context *pctx,
		const struct pipe_compute_state *cso)
{
	struct fd_context *ctx = fd_context(pctx);

	/* req_input_mem will only be non-zero for cl kernels (ie. clover).
	 * This isn't a perfect test because I guess it is possible (but
	 * uncommon) for none for the kernel parameters to be a global,
	 * but ctx->set_global_bindings() can't fail, so this is the next
	 * best place to fail if we need a newer version of kernel driver:
	 */
	if ((cso->req_input_mem > 0) &&
			fd_device_version(ctx->dev) < FD_VERSION_BO_IOVA) {
		return NULL;
	}

	struct ir3_compiler *compiler = ctx->screen->compiler;
	struct fd5_compute_stateobj *so = CALLOC_STRUCT(fd5_compute_stateobj);
	so->shader = ir3_shader_create_compute(compiler, cso, &ctx->debug, pctx->screen);
	return so;
}

static void
fd5_delete_compute_state(struct pipe_context *pctx, void *hwcso)
{
	struct fd5_compute_stateobj *so = hwcso;
	ir3_shader_state_delete(pctx, so->shader);
	free(so);
}

/* maybe move to fd5_program? */
static void
cs_program_emit(struct fd_ringbuffer *ring, struct ir3_shader_variant *v,
		const struct pipe_grid_info *info)
{
	const unsigned *local_size = info->block;
	const struct ir3_info *i = &v->info;
	enum a3xx_threadsize thrsz;
	unsigned instrlen = v->instrlen;

	/* if shader is more than 32*16 instructions, don't preload it.  Similar
	 * to the combined restriction of 64*16 for VS+FS
	 */
	if (instrlen > 32)
		instrlen = 0;

	/* maybe the limit should be 1024.. basically if we can't have full
	 * occupancy, use TWO_QUAD mode to reduce divergence penalty.
	 */
	if ((local_size[0] * local_size[1] * local_size[2]) < 512) {
		thrsz = TWO_QUADS;
	} else {
		thrsz = FOUR_QUADS;
	}

	OUT_PKT4(ring, REG_A5XX_SP_SP_CNTL, 1);
	OUT_RING(ring, 0x00000000);        /* SP_SP_CNTL */

	OUT_PKT4(ring, REG_A5XX_HLSQ_CONTROL_0_REG, 1);
	OUT_RING(ring, A5XX_HLSQ_CONTROL_0_REG_FSTHREADSIZE(TWO_QUADS) |
		A5XX_HLSQ_CONTROL_0_REG_CSTHREADSIZE(thrsz) |
		0x00000880 /* XXX */);

	OUT_PKT4(ring, REG_A5XX_SP_CS_CTRL_REG0, 1);
	OUT_RING(ring, A5XX_SP_CS_CTRL_REG0_THREADSIZE(thrsz) |
		A5XX_SP_CS_CTRL_REG0_HALFREGFOOTPRINT(i->max_half_reg + 1) |
		A5XX_SP_CS_CTRL_REG0_FULLREGFOOTPRINT(i->max_reg + 1) |
		A5XX_SP_CS_CTRL_REG0_BRANCHSTACK(0x3) |  // XXX need to figure this out somehow..
		0x6 /* XXX */);

	OUT_PKT4(ring, REG_A5XX_HLSQ_CS_CONFIG, 1);
	OUT_RING(ring, A5XX_HLSQ_CS_CONFIG_CONSTOBJECTOFFSET(0) |
		A5XX_HLSQ_CS_CONFIG_SHADEROBJOFFSET(0) |
		A5XX_HLSQ_CS_CONFIG_ENABLED);

	OUT_PKT4(ring, REG_A5XX_HLSQ_CS_CNTL, 1);
	OUT_RING(ring, A5XX_HLSQ_CS_CNTL_INSTRLEN(instrlen) |
		COND(v->has_ssbo, A5XX_HLSQ_CS_CNTL_SSBO_ENABLE));

	OUT_PKT4(ring, REG_A5XX_SP_CS_CONFIG, 1);
	OUT_RING(ring, A5XX_SP_CS_CONFIG_CONSTOBJECTOFFSET(0) |
		A5XX_SP_CS_CONFIG_SHADEROBJOFFSET(0) |
		A5XX_SP_CS_CONFIG_ENABLED);

	assert(v->constlen % 4 == 0);
	unsigned constlen = v->constlen / 4;
	OUT_PKT4(ring, REG_A5XX_HLSQ_CS_CONSTLEN, 2);
	OUT_RING(ring, constlen);          /* HLSQ_CS_CONSTLEN */
	OUT_RING(ring, instrlen);          /* HLSQ_CS_INSTRLEN */

	OUT_PKT4(ring, REG_A5XX_SP_CS_OBJ_START_LO, 2);
	OUT_RELOC(ring, v->bo, 0, 0, 0);   /* SP_CS_OBJ_START_LO/HI */

	OUT_PKT4(ring, REG_A5XX_HLSQ_UPDATE_CNTL, 1);
	OUT_RING(ring, 0x1f00000);

	uint32_t local_invocation_id, work_group_id;
	local_invocation_id = ir3_find_sysval_regid(v, SYSTEM_VALUE_LOCAL_INVOCATION_ID);
	work_group_id = ir3_find_sysval_regid(v, SYSTEM_VALUE_WORK_GROUP_ID);

	OUT_PKT4(ring, REG_A5XX_HLSQ_CS_CNTL_0, 2);
	OUT_RING(ring, A5XX_HLSQ_CS_CNTL_0_WGIDCONSTID(work_group_id) |
		A5XX_HLSQ_CS_CNTL_0_UNK0(regid(63, 0)) |
		A5XX_HLSQ_CS_CNTL_0_UNK1(regid(63, 0)) |
		A5XX_HLSQ_CS_CNTL_0_LOCALIDREGID(local_invocation_id));
	OUT_RING(ring, 0x1);               /* HLSQ_CS_CNTL_1 */

	if (instrlen > 0)
		fd5_emit_shader(ring, v);
}

static void
fd5_launch_grid(struct fd_context *ctx, const struct pipe_grid_info *info)
{
	struct fd5_compute_stateobj *so = ctx->compute;
	struct ir3_shader_key key = {};
	struct ir3_shader_variant *v;
	struct fd_ringbuffer *ring = ctx->batch->draw;
	unsigned nglobal = 0;

	v = ir3_shader_variant(so->shader, key, false, &ctx->debug);
	if (!v)
		return;

	if (ctx->dirty_shader[PIPE_SHADER_COMPUTE] & FD_DIRTY_SHADER_PROG)
		cs_program_emit(ring, v, info);

	fd5_emit_cs_state(ctx, ring, v);
	fd5_emit_cs_consts(v, ring, ctx, info);

	foreach_bit(i, ctx->global_bindings.enabled_mask)
		nglobal++;

	if (nglobal > 0) {
		/* global resources don't otherwise get an OUT_RELOC(), since
		 * the raw ptr address is emitted ir ir3_emit_cs_consts().
		 * So to make the kernel aware that these buffers are referenced
		 * by the batch, emit dummy reloc's as part of a no-op packet
		 * payload:
		 */
		OUT_PKT7(ring, CP_NOP, 2 * nglobal);
		foreach_bit(i, ctx->global_bindings.enabled_mask) {
			struct pipe_resource *prsc = ctx->global_bindings.buf[i];
			OUT_RELOC(ring, fd_resource(prsc)->bo, 0, 0, 0);
		}
	}

	const unsigned *local_size = info->block; // v->shader->nir->info->cs.local_size;
	const unsigned *num_groups = info->grid;
	/* for some reason, mesa/st doesn't set info->work_dim, so just assume 3: */
	const unsigned work_dim = info->work_dim ? info->work_dim : 3;
	OUT_PKT4(ring, REG_A5XX_HLSQ_CS_NDRANGE_0, 7);
	OUT_RING(ring, A5XX_HLSQ_CS_NDRANGE_0_KERNELDIM(work_dim) |
		A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEX(local_size[0] - 1) |
		A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEY(local_size[1] - 1) |
		A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEZ(local_size[2] - 1));
	OUT_RING(ring, A5XX_HLSQ_CS_NDRANGE_1_GLOBALSIZE_X(local_size[0] * num_groups[0]));
	OUT_RING(ring, 0);            /* HLSQ_CS_NDRANGE_2_GLOBALOFF_X */
	OUT_RING(ring, A5XX_HLSQ_CS_NDRANGE_3_GLOBALSIZE_Y(local_size[1] * num_groups[1]));
	OUT_RING(ring, 0);            /* HLSQ_CS_NDRANGE_4_GLOBALOFF_Y */
	OUT_RING(ring, A5XX_HLSQ_CS_NDRANGE_5_GLOBALSIZE_Z(local_size[2] * num_groups[2]));
	OUT_RING(ring, 0);            /* HLSQ_CS_NDRANGE_6_GLOBALOFF_Z */

	OUT_PKT4(ring, REG_A5XX_HLSQ_CS_KERNEL_GROUP_X, 3);
	OUT_RING(ring, 1);            /* HLSQ_CS_KERNEL_GROUP_X */
	OUT_RING(ring, 1);            /* HLSQ_CS_KERNEL_GROUP_Y */
	OUT_RING(ring, 1);            /* HLSQ_CS_KERNEL_GROUP_Z */

	if (info->indirect) {
		struct fd_resource *rsc = fd_resource(info->indirect);

		fd5_emit_flush(ctx, ring);

		OUT_PKT7(ring, CP_EXEC_CS_INDIRECT, 4);
		OUT_RING(ring, 0x00000000);
		OUT_RELOC(ring, rsc->bo, info->indirect_offset, 0, 0);  /* ADDR_LO/HI */
		OUT_RING(ring, A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEX(local_size[0] - 1) |
				A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEY(local_size[1] - 1) |
				A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEZ(local_size[2] - 1));
	} else {
		OUT_PKT7(ring, CP_EXEC_CS, 4);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, CP_EXEC_CS_1_NGROUPS_X(info->grid[0]));
		OUT_RING(ring, CP_EXEC_CS_2_NGROUPS_Y(info->grid[1]));
		OUT_RING(ring, CP_EXEC_CS_3_NGROUPS_Z(info->grid[2]));
	}
}

void
fd5_compute_init(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	ctx->launch_grid = fd5_launch_grid;
	pctx->create_compute_state = fd5_create_compute_state;
	pctx->delete_compute_state = fd5_delete_compute_state;
}
