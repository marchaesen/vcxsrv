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
#include "main/macros.h"
#include "amd_family.h"

struct radeon_info;
struct ac_surf_info;
struct radeon_surf;

#define FREE(x) free(x)

enum radeon_bo_domain { /* bitfield */
	RADEON_DOMAIN_GTT  = 2,
	RADEON_DOMAIN_VRAM = 4,
	RADEON_DOMAIN_VRAM_GTT = RADEON_DOMAIN_VRAM | RADEON_DOMAIN_GTT
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
};

enum radeon_bo_usage { /* bitfield */
	RADEON_USAGE_READ = 2,
	RADEON_USAGE_WRITE = 4,
	RADEON_USAGE_READWRITE = RADEON_USAGE_READ | RADEON_USAGE_WRITE
};

enum ring_type {
	RING_GFX = 0,
	RING_COMPUTE,
	RING_DMA,
	RING_UVD,
	RING_VCE,
	RING_LAST,
};

enum radeon_ctx_priority {
	RADEON_CTX_PRIORITY_INVALID = -1,
	RADEON_CTX_PRIORITY_LOW = 0,
	RADEON_CTX_PRIORITY_MEDIUM,
	RADEON_CTX_PRIORITY_HIGH,
	RADEON_CTX_PRIORITY_REALTIME,
};

enum radeon_value_id {
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

struct radeon_winsys_cs {
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
		} gfx9;
	} u;

	/* Additional metadata associated with the buffer, in bytes.
	 * The maximum size is 64 * 4. This is opaque for the winsys & kernel.
	 * Supported by amdgpu only.
	 */
	uint32_t                size_metadata;
	uint32_t                metadata[64];
};

uint32_t syncobj_handle;
struct radeon_winsys_fence;

struct radeon_winsys_bo {
	uint64_t va;
	bool is_local;
};
struct radv_winsys_sem_counts {
	uint32_t syncobj_count;
	uint32_t sem_count;
	uint32_t *syncobj;
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
						  enum radeon_bo_flag flags);

	void (*buffer_destroy)(struct radeon_winsys_bo *bo);
	void *(*buffer_map)(struct radeon_winsys_bo *bo);

	struct radeon_winsys_bo *(*buffer_from_ptr)(struct radeon_winsys *ws,
						    void *pointer,
						    uint64_t size);

	struct radeon_winsys_bo *(*buffer_from_fd)(struct radeon_winsys *ws,
						   int fd,
						   unsigned *stride, unsigned *offset);

	bool (*buffer_get_fd)(struct radeon_winsys *ws,
			      struct radeon_winsys_bo *bo,
			      int *fd);

	void (*buffer_unmap)(struct radeon_winsys_bo *bo);

	void (*buffer_set_metadata)(struct radeon_winsys_bo *bo,
				    struct radeon_bo_metadata *md);

	void (*buffer_virtual_bind)(struct radeon_winsys_bo *parent,
	                            uint64_t offset, uint64_t size,
	                            struct radeon_winsys_bo *bo, uint64_t bo_offset);
	struct radeon_winsys_ctx *(*ctx_create)(struct radeon_winsys *ws,
						enum radeon_ctx_priority priority);
	void (*ctx_destroy)(struct radeon_winsys_ctx *ctx);

	bool (*ctx_wait_idle)(struct radeon_winsys_ctx *ctx,
	                      enum ring_type ring_type, int ring_index);

	struct radeon_winsys_cs *(*cs_create)(struct radeon_winsys *ws,
					      enum ring_type ring_type);

	void (*cs_destroy)(struct radeon_winsys_cs *cs);

	void (*cs_reset)(struct radeon_winsys_cs *cs);

	bool (*cs_finalize)(struct radeon_winsys_cs *cs);

	void (*cs_grow)(struct radeon_winsys_cs * cs, size_t min_size);

	int (*cs_submit)(struct radeon_winsys_ctx *ctx,
			 int queue_index,
			 struct radeon_winsys_cs **cs_array,
			 unsigned cs_count,
			 struct radeon_winsys_cs *initial_preamble_cs,
			 struct radeon_winsys_cs *continue_preamble_cs,
			 struct radv_winsys_sem_info *sem_info,
			 const struct radv_winsys_bo_list *bo_list, /* optional */
			 bool can_patch,
			 struct radeon_winsys_fence *fence);

	void (*cs_add_buffer)(struct radeon_winsys_cs *cs,
			      struct radeon_winsys_bo *bo,
			      uint8_t priority);

	void (*cs_execute_secondary)(struct radeon_winsys_cs *parent,
				    struct radeon_winsys_cs *child);

	void (*cs_dump)(struct radeon_winsys_cs *cs, FILE* file, const int *trace_ids, int trace_id_count);

	int (*surface_init)(struct radeon_winsys *ws,
			    const struct ac_surf_info *surf_info,
			    struct radeon_surf *surf);

	int (*surface_best)(struct radeon_winsys *ws,
			    struct radeon_surf *surf);

	struct radeon_winsys_fence *(*create_fence)();
	void (*destroy_fence)(struct radeon_winsys_fence *fence);
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
	int (*create_syncobj)(struct radeon_winsys *ws, uint32_t *handle);
	void (*destroy_syncobj)(struct radeon_winsys *ws, uint32_t handle);

	void (*reset_syncobj)(struct radeon_winsys *ws, uint32_t handle);
	void (*signal_syncobj)(struct radeon_winsys *ws, uint32_t handle);
	bool (*wait_syncobj)(struct radeon_winsys *ws, const uint32_t *handles, uint32_t handle_count,
			     bool wait_all, uint64_t timeout);

	int (*export_syncobj)(struct radeon_winsys *ws, uint32_t syncobj, int *fd);
	int (*import_syncobj)(struct radeon_winsys *ws, int fd, uint32_t *syncobj);

	int (*export_syncobj_to_sync_file)(struct radeon_winsys *ws, uint32_t syncobj, int *fd);

	/* Note that this, unlike the normal import, uses an existing syncobj. */
	int (*import_syncobj_from_sync_file)(struct radeon_winsys *ws, uint32_t syncobj, int fd);

};

static inline void radeon_emit(struct radeon_winsys_cs *cs, uint32_t value)
{
	cs->buf[cs->cdw++] = value;
}

static inline void radeon_emit_array(struct radeon_winsys_cs *cs,
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
				      struct radeon_winsys_cs *cs,
				      struct radeon_winsys_bo *bo,
				      uint8_t priority)
{
	if (bo->is_local)
		return;

	ws->cs_add_buffer(cs, bo, priority);
}

#endif /* RADV_RADEON_WINSYS_H */
