/*
 * Copyright (C) 2023 Collabora Ltd.
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
 */

#ifndef __PAN_CSF_H__
#define __PAN_CSF_H__

#include "compiler/shader_enums.h"

#include "pan_bo.h"
#include "pan_desc.h"
#include "pan_mempool.h"

struct cs_builder;
struct cs_load_store_tracker;

enum pan_rendering_pass {
   PAN_INCREMENTAL_RENDERING_FIRST_PASS,
   PAN_INCREMENTAL_RENDERING_MIDDLE_PASS,
   PAN_INCREMENTAL_RENDERING_LAST_PASS,
   PAN_INCREMENTAL_RENDERING_PASS_COUNT
};

struct pan_csf_tiler_oom_ctx {
   /* Number of times the OOM exception handler was called */
   uint32_t counter;

   /* Alternative framebuffer descriptors for incremental rendering */
   struct panfrost_ptr fbds[PAN_INCREMENTAL_RENDERING_PASS_COUNT];

   /* Bounding Box (Register 42 and 43) */
   uint32_t bbox_min;
   uint32_t bbox_max;

   /* Tiler descriptor address */
   uint64_t tiler_desc;

   /* Address of the region reserved for saving registers. */
   uint64_t dump_addr;
} PACKED;

struct panfrost_csf_batch {
   /* CS related fields. */
   struct {
      /* CS builder. */
      struct cs_builder *builder;

      /* CS state, written through the CS, and checked when PAN_MESA_DEBUG=sync.
       */
      struct panfrost_ptr state;

      /* CS load/store tracker if extra checks are enabled. */
      struct cs_load_store_tracker *ls_tracker;
   } cs;

   /* Pool used to allocate CS chunks. */
   struct panfrost_pool cs_chunk_pool;

   struct panfrost_ptr tiler_oom_ctx;

   struct mali_tiler_context_packed *pending_tiler_desc;
};

struct panfrost_csf_context {
   bool is_init;
   uint32_t group_handle;

   struct {
      uint32_t handle;
      struct panfrost_bo *desc_bo;
   } heap;

   /* Temporary geometry buffer. Used as a FIFO by the tiler. */
   struct panfrost_bo *tmp_geom_bo;

   struct {
      struct panfrost_bo *cs_bo;
      struct panfrost_bo *save_bo;
      uint32_t length;
   } tiler_oom_handler;
};

#if defined(PAN_ARCH) && PAN_ARCH >= 10

#include "genxml/gen_macros.h"

struct panfrost_batch;
struct panfrost_context;
struct pan_fb_info;
struct pan_tls_info;
struct pipe_draw_info;
struct pipe_grid_info;
struct pipe_draw_start_count_bias;

int GENX(csf_init_context)(struct panfrost_context *ctx);
void GENX(csf_cleanup_context)(struct panfrost_context *ctx);

int GENX(csf_init_batch)(struct panfrost_batch *batch);
void GENX(csf_cleanup_batch)(struct panfrost_batch *batch);
int GENX(csf_submit_batch)(struct panfrost_batch *batch);

void GENX(csf_prepare_tiler)(struct panfrost_batch *batch,
                             struct pan_fb_info *fb);
void GENX(csf_preload_fb)(struct panfrost_batch *batch, struct pan_fb_info *fb);
void GENX(csf_emit_fbds)(struct panfrost_batch *batch, struct pan_fb_info *fb,
                         struct pan_tls_info *tls);
void GENX(csf_emit_fragment_job)(struct panfrost_batch *batch,
                                 const struct pan_fb_info *pfb);
int GENX(csf_emit_batch_end)(struct panfrost_batch *batch);
void GENX(csf_launch_xfb)(struct panfrost_batch *batch,
                          const struct pipe_draw_info *info, unsigned count);
void GENX(csf_launch_grid)(struct panfrost_batch *batch,
                           const struct pipe_grid_info *info);
void GENX(csf_launch_draw)(struct panfrost_batch *batch,
                           const struct pipe_draw_info *info,
                           unsigned drawid_offset,
                           const struct pipe_draw_start_count_bias *draw,
                           unsigned vertex_count);
void GENX(csf_launch_draw_indirect)(struct panfrost_batch *batch,
                                    const struct pipe_draw_info *info,
                                    unsigned drawid_offset,
                                    const struct pipe_draw_indirect_info *indirect);

void GENX(csf_emit_write_timestamp)(struct panfrost_batch *batch,
                                    struct panfrost_resource *dst,
                                    unsigned offset);

#endif /* PAN_ARCH >= 10 */

#endif /* __PAN_CSF_H__ */
