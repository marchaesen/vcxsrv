/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Based on radeon_winsys.h which is:
 * Copyright 2008 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
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

#ifndef RADV_RADEON_WINSYS_H
#define RADV_RADEON_WINSYS_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "amd_family.h"
#include "util/u_memory.h"
#include "util/u_math.h"

struct radeon_info;
struct ac_surf_info;
struct radeon_surf;

enum radeon_bo_domain { /* bitfield */
	RADEON_DOMAIN_GTT  = 2,
	RADEON_DOMAIN_VRAM = 4,
	RADEON_DOMAIN_VRAM_GTT = RADEON_DOMAIN_VRAM | RADEON_DOMAIN_GTT,
	RADEON_DOMAIN_GDS = 8,
	RADEON_DOMAIN_OA = 16,
};

enum radeon_bo_flag { /* bitfield */
	RADEON_FLAG_GTT_WC =        (1 << 0),
	RADEON_FLAG_CPU_ACCESS =    (1 << 1),
	RADEON_FLAG_NO_CPU_ACCESS = (1 << 2),
	RADEON_FLAG_VIRTUAL =       (1 << 3),
	RADEON_FLAG_VA_UNCACHED =   (1 << 4),
	RADEON_FLAG_IMPLICIT_SYNC = (1 << 5),
	RADEON_FLAG_NO_INTERPROCESS_SHARING = (1 << 6),
	RADEON_FLAG_READ_ONLY =     (1 << 7),
	RADEON_FLAG_32BIT =         (1 << 8),
	RADEON_FLAG_PREFER_LOCAL_BO = (1 << 9),
	RADEON_FLAG_ZERO_VRAM = (1 << 10),
};

enum radeon_ctx_priority {
	RADEON_CTX_PRIORITY_INVALID = -1,
	RADEON_CTX_PRIORITY_LOW = 0,
	RADEON_CTX_PRIORITY_MEDIUM,
	RADEON_CTX_PRIORITY_HIGH,
	RADEON_CTX_PRIORITY_REALTIME,
};

enum radeon_value_id {
	RADEON_ALLOCATED_VRAM,
	RADEON_ALLOCATED_VRAM_VIS,
	RADEON_ALLOCATED_GTT,
	RADEON_TIMESTAMP,
	RADEON_NUM_BYTES_MOVED,
	RADEON_NUM_EVICTIONS,
	RADEON_NUM_VRAM_CPU_PAGE_FAULTS,
	RADEON_VRAM_USAGE,
	RADEON_VRAM_VIS_USAGE,
	RADEON_GTT_USAGE,
	RADEON_GPU_TEMPERATURE,
	RADEON_CURRENT_SCLK,
	RADEON_CURRENT_MCLK,
};

struct radeon_cmdbuf {
	unsigned cdw;  /* Number of used dwords. */
	unsigned max_dw; /* Maximum number of dwords. */
	uint32_t *buf; /* The base pointer of the chunk. */
};

#define RADEON_SURF_TYPE_MASK                   0xFF
#define RADEON_SURF_TYPE_SHIFT                  0
#define     RADEON_SURF_TYPE_1D                     0
#define     RADEON_SURF_TYPE_2D                     1
#define     RADEON_SURF_TYPE_3D                     2
#define     RADEON_SURF_TYPE_CUBEMAP                3
#define     RADEON_SURF_TYPE_1D_ARRAY               4
#define     RADEON_SURF_TYPE_2D_ARRAY               5
#define RADEON_SURF_MODE_MASK                   0xFF
#define RADEON_SURF_MODE_SHIFT                  8

#define RADEON_SURF_GET(v, field)   (((v) >> RADEON_SURF_ ## field ## _SHIFT) & RADEON_SURF_ ## field ## _MASK)
#define RADEON_SURF_SET(v, field)   (((v) & RADEON_SURF_ ## field ## _MASK) << RADEON_SURF_ ## field ## _SHIFT)
#define RADEON_SURF_CLR(v, field)   ((v) & ~(RADEON_SURF_ ## field ## _MASK << RADEON_SURF_ ## field ## _SHIFT))

enum radeon_bo_layout {
	RADEON_LAYOUT_LINEAR = 0,
	RADEON_LAYOUT_TILED,
	RADEON_LAYOUT_SQUARETILED,

	RADEON_LAYOUT_UNKNOWN
};

/* Tiling info for display code, DRI sharing, and other data. */
struct radeon_bo_metadata {
	/* Tiling flags describing the texture layout for display code
	 * and DRI sharing.
	 */
	union {
		struct {
			enum radeon_bo_layout   microtile;
			enum radeon_bo_layout   macrotile;
			unsigned                pipe_config;
			unsigned                bankw;
			unsigned                bankh;
			unsigned                tile_split;
			unsigned                mtilea;
			unsigned                num_banks;
			unsigned                stride;
			bool                    scanout;
		} legacy;

		struct {
			/* surface flags */
			unsigned swizzle_mode:5;
			bool scanout;
			uint32_t dcc_offset_256b;
			uint32_t dcc_pitch_max;
			bool dcc_independent_64b_blocks;
			bool dcc_independent_128b_blocks;
			unsigned dcc_max_compressed_block_size;
		} gfx9;
	} u;

	/* Additional metadata associated with the buffer, in bytes.
	 * The maximum size is 64 * 4. This is opaque for the winsys & kernel.
	 * Supported by amdgpu only.
	 */
	uint32_t                size_metadata;
	uint32_t                metadata[64];
};

struct radeon_winsys_fence;
struct radeon_winsys_ctx;

struct radeon_winsys_bo {
	uint64_t va;
	bool is_local;
	bool vram_no_cpu_access;
	bool use_global_list;
	enum radeon_bo_domain initial_domain;
};
struct radv_winsys_sem_counts {
	uint32_t syncobj_count;
	uint32_t syncobj_reset_count; /* for wait only, whether to reset the syncobj */
	uint32_t timeline_syncobj_count;
	uint32_t sem_count;
	uint32_t *syncobj;
	uint64_t *points;
	struct radeon_winsys_sem **sem;
};

struct radv_winsys_sem_info {
	bool cs_emit_signal;
	bool cs_emit_wait;
	struct radv_winsys_sem_counts wait;
	struct radv_winsys_sem_counts signal;
};

struct radv_winsys_bo_list {
	struct radeon_winsys_bo **bos;
	unsigned count;
};

/* Kernel effectively allows 0-31. This sets some priorities for fixed
 * functionality buffers */
enum {
	RADV_BO_PRIORITY_APPLICATION_MAX = 28,

	/* virtual buffers have 0 priority since the priority is not used. */
	RADV_BO_PRIORITY_VIRTUAL = 0,

	RADV_BO_PRIORITY_METADATA = 10,
	/* This should be considerably lower than most of the stuff below,
	 * but how much lower is hard to say since we don't know application
	 * assignments. Put it pretty high since it is GTT anyway. */
	RADV_BO_PRIORITY_QUERY_POOL = 29,

	RADV_BO_PRIORITY_DESCRIPTOR = 30,
	RADV_BO_PRIORITY_UPLOAD_BUFFER = 30,
	RADV_BO_PRIORITY_FENCE = 30,
	RADV_BO_PRIORITY_SHADER = 31,
	RADV_BO_PRIORITY_SCRATCH = 31,
	RADV_BO_PRIORITY_CS = 31,
};

struct radeon_winsys {
	void (*destroy)(struct radeon_winsys *ws);

	void (*query_info)(struct radeon_winsys *ws,
			   struct radeon_info *info);

	uint64_t (*query_value)(struct radeon_winsys *ws,
				enum radeon_value_id value);

	bool (*read_registers)(struct radeon_winsys *ws, unsigned reg_offset,
			       unsigned num_registers, uint32_t *out);

	const char *(*get_chip_name)(struct radeon_winsys *ws);

	struct radeon_winsys_bo *(*buffer_create)(struct radeon_winsys *ws,
						  uint64_t size,
						  unsigned alignment,
						  enum radeon_bo_domain domain,
						  enum radeon_bo_flag flags,
						  unsigned priority);

	void (*buffer_destroy)(struct radeon_winsys *ws,
			       struct radeon_winsys_bo *bo);
	void *(*buffer_map)(struct radeon_winsys_bo *bo);

	struct radeon_winsys_bo *(*buffer_from_ptr)(struct radeon_winsys *ws,
						    void *pointer,
						    uint64_t size,
						    unsigned priority);

	struct radeon_winsys_bo *(*buffer_from_fd)(struct radeon_winsys *ws,
						   int fd,
						   unsigned priority,
						   uint64_t *alloc_size);

	bool (*buffer_get_fd)(struct radeon_winsys *ws,
			      struct radeon_winsys_bo *bo,
			      int *fd);

	bool (*buffer_get_flags_from_fd)(struct radeon_winsys *ws, int fd,
	                                 enum radeon_bo_domain *domains,
	                                 enum radeon_bo_flag *flags);

	void (*buffer_unmap)(struct radeon_winsys_bo *bo);

	void (*buffer_set_metadata)(struct radeon_winsys *ws,
				    struct radeon_winsys_bo *bo,
				    struct radeon_bo_metadata *md);
	void (*buffer_get_metadata)(struct radeon_winsys *ws,
				    struct radeon_winsys_bo *bo,
				    struct radeon_bo_metadata *md);

	VkResult (*buffer_virtual_bind)(struct radeon_winsys *ws,
					struct radeon_winsys_bo *parent,
					uint64_t offset, uint64_t size,
					struct radeon_winsys_bo *bo, uint64_t bo_offset);

	VkResult (*buffer_make_resident)(struct radeon_winsys *ws,
					 struct radeon_winsys_bo *bo,
					 bool resident);

	VkResult (*ctx_create)(struct radeon_winsys *ws,
	                       enum radeon_ctx_priority priority,
	                       struct radeon_winsys_ctx **ctx);
	void (*ctx_destroy)(struct radeon_winsys_ctx *ctx);

	bool (*ctx_wait_idle)(struct radeon_winsys_ctx *ctx,
	                      enum ring_type ring_type, int ring_index);

	struct radeon_cmdbuf *(*cs_create)(struct radeon_winsys *ws,
					      enum ring_type ring_type);

	void (*cs_destroy)(struct radeon_cmdbuf *cs);

	void (*cs_reset)(struct radeon_cmdbuf *cs);

	VkResult (*cs_finalize)(struct radeon_cmdbuf *cs);

	void (*cs_grow)(struct radeon_cmdbuf * cs, size_t min_size);

	VkResult (*cs_submit)(struct radeon_winsys_ctx *ctx,
			      int queue_index,
			      struct radeon_cmdbuf **cs_array,
			      unsigned cs_count,
			      struct radeon_cmdbuf *initial_preamble_cs,
			      struct radeon_cmdbuf *continue_preamble_cs,
			      struct radv_winsys_sem_info *sem_info,
			      bool can_patch,
			      struct radeon_winsys_fence *fence);

	void (*cs_add_buffer)(struct radeon_cmdbuf *cs,
			      struct radeon_winsys_bo *bo);

	void (*cs_execute_secondary)(struct radeon_cmdbuf *parent,
				    struct radeon_cmdbuf *child);

	void (*cs_dump)(struct radeon_cmdbuf *cs, FILE* file, const int *trace_ids, int trace_id_count);

	void (*dump_bo_ranges)(struct radeon_winsys *ws, FILE *file);

	void (*dump_bo_log)(struct radeon_winsys *ws, FILE *file);

	int (*surface_init)(struct radeon_winsys *ws,
			    const struct ac_surf_info *surf_info,
			    struct radeon_surf *surf);

	struct radeon_winsys_fence *(*create_fence)();
	void (*destroy_fence)(struct radeon_winsys_fence *fence);
	void (*reset_fence)(struct radeon_winsys_fence *fence);
	void (*signal_fence)(struct radeon_winsys_fence *fence);
	bool (*is_fence_waitable)(struct radeon_winsys_fence *fence);
	bool (*fence_wait)(struct radeon_winsys *ws,
			   struct radeon_winsys_fence *fence,
			   bool absolute,
			   uint64_t timeout);
	bool (*fences_wait)(struct radeon_winsys *ws,
			    struct radeon_winsys_fence *const *fences,
			    uint32_t fence_count,
			    bool wait_all,
			    uint64_t timeout);

	/* old semaphores - non shareable */
	struct radeon_winsys_sem *(*create_sem)(struct radeon_winsys *ws);
	void (*destroy_sem)(struct radeon_winsys_sem *sem);

	/* new shareable sync objects */
	int (*create_syncobj)(struct radeon_winsys *ws, bool create_signaled,
			      uint32_t *handle);
	void (*destroy_syncobj)(struct radeon_winsys *ws, uint32_t handle);

	void (*reset_syncobj)(struct radeon_winsys *ws, uint32_t handle);
	void (*signal_syncobj)(struct radeon_winsys *ws, uint32_t handle, uint64_t point);
	VkResult (*query_syncobj)(struct radeon_winsys *ws, uint32_t handle, uint64_t *point);
	bool (*wait_syncobj)(struct radeon_winsys *ws, const uint32_t *handles, uint32_t handle_count,
			     bool wait_all, uint64_t timeout);
	bool (*wait_timeline_syncobj)(struct radeon_winsys *ws, const uint32_t *handles, const uint64_t *points,
	                              uint32_t handle_count, bool wait_all, bool available, uint64_t timeout);

	int (*export_syncobj)(struct radeon_winsys *ws, uint32_t syncobj, int *fd);
	int (*import_syncobj)(struct radeon_winsys *ws, int fd, uint32_t *syncobj);

	int (*export_syncobj_to_sync_file)(struct radeon_winsys *ws, uint32_t syncobj, int *fd);

	/* Note that this, unlike the normal import, uses an existing syncobj. */
	int (*import_syncobj_from_sync_file)(struct radeon_winsys *ws, uint32_t syncobj, int fd);

};

static inline void radeon_emit(struct radeon_cmdbuf *cs, uint32_t value)
{
	cs->buf[cs->cdw++] = value;
}

static inline void radeon_emit_array(struct radeon_cmdbuf *cs,
				     const uint32_t *values, unsigned count)
{
	memcpy(cs->buf + cs->cdw, values, count * 4);
	cs->cdw += count;
}

static inline uint64_t radv_buffer_get_va(struct radeon_winsys_bo *bo)
{
	return bo->va;
}

static inline void radv_cs_add_buffer(struct radeon_winsys *ws,
				      struct radeon_cmdbuf *cs,
				      struct radeon_winsys_bo *bo)
{
	if (bo->use_global_list)
		return;

	ws->cs_add_buffer(cs, bo);
}

enum radeon_bo_domain radv_cmdbuffer_domain(const struct radeon_info *info, uint32_t perftest);

#endif /* RADV_RADEON_WINSYS_H */
