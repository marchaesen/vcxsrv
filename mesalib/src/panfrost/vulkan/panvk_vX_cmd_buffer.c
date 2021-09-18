/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "gen_macros.h"

#include "panvk_cs.h"
#include "panvk_private.h"
#include "panfrost-quirks.h"

#include "pan_blitter.h"
#include "pan_cs.h"
#include "pan_encoder.h"

#include "util/rounding.h"
#include "util/u_pack_color.h"
#include "vk_format.h"

static void
panvk_cmd_prepare_fragment_job(struct panvk_cmd_buffer *cmdbuf)
{
   assert(cmdbuf->state.bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS);

   struct panvk_batch *batch = cmdbuf->state.batch;
   struct panfrost_ptr job_ptr =
      pan_pool_alloc_desc(&cmdbuf->desc_pool.base, FRAGMENT_JOB);

   panvk_per_arch(emit_fragment_job)(cmdbuf->state.framebuffer,
                                     cmdbuf->state.batch->fb.desc.gpu,
                                     job_ptr.cpu);
   cmdbuf->state.batch->fragment_job = job_ptr.gpu;
   util_dynarray_append(&batch->jobs, void *, job_ptr.cpu);
}

#if PAN_ARCH == 5
void
panvk_per_arch(cmd_get_polygon_list)(struct panvk_cmd_buffer *cmdbuf,
                                     unsigned width, unsigned height,
                                     bool has_draws)
{
   struct panfrost_device *pdev = &cmdbuf->device->physical_device->pdev;
   struct panvk_batch *batch = cmdbuf->state.batch;

   if (batch->tiler.ctx.midgard.polygon_list)
      return;

   unsigned size =
      panfrost_tiler_get_polygon_list_size(pdev, width, height, has_draws);
   size = util_next_power_of_two(size);

   /* Create the BO as invisible if we can. In the non-hierarchical tiler case,
    * we need to write the polygon list manually because there's not WRITE_VALUE
    * job in the chain. */
   bool init_polygon_list = !has_draws && (pdev->quirks & MIDGARD_NO_HIER_TILING);
   batch->tiler.ctx.midgard.polygon_list =
      panfrost_bo_create(pdev, size,
                         init_polygon_list ? 0 : PAN_BO_INVISIBLE,
                         "Polygon list");


   if (init_polygon_list) {
      assert(batch->tiler.ctx.midgard.polygon_list->ptr.cpu);
      uint32_t *polygon_list_body =
         batch->tiler.ctx.midgard.polygon_list->ptr.cpu +
         MALI_MIDGARD_TILER_MINIMUM_HEADER_SIZE;
      polygon_list_body[0] = 0xa0000000;
   }

   batch->tiler.ctx.midgard.disable = !has_draws;
}
#endif

#if PAN_ARCH <= 5
static void
panvk_copy_fb_desc(struct panvk_cmd_buffer *cmdbuf, void *src)
{
   struct panvk_batch *batch = cmdbuf->state.batch;
   const struct panvk_subpass *subpass = cmdbuf->state.subpass;
   uint32_t size = pan_size(MULTI_TARGET_FRAMEBUFFER);

   if (subpass->zs_attachment.idx != VK_ATTACHMENT_UNUSED)
      size += pan_size(ZS_CRC_EXTENSION);

   size += MAX2(subpass->color_count, 1) * pan_size(RENDER_TARGET);

   memcpy(batch->fb.desc.cpu, src, size);
}
#endif

void
panvk_per_arch(cmd_close_batch)(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_batch *batch = cmdbuf->state.batch;
#if PAN_ARCH <= 5
   uint32_t tmp_fbd[(pan_size(MULTI_TARGET_FRAMEBUFFER) +
                     pan_size(ZS_CRC_EXTENSION) +
                     (MAX_RTS * pan_size(RENDER_TARGET))) / 4];
#endif

   assert(batch);

   if (!batch->fragment_job && !batch->scoreboard.first_job) {
      if (util_dynarray_num_elements(&batch->event_ops, struct panvk_event_op) == 0) {
         /* Content-less batch, let's drop it */
         vk_free(&cmdbuf->pool->alloc, batch);
      } else {
         /* Batch has no jobs but is needed for synchronization, let's add a
          * NULL job so the SUBMIT ioctl doesn't choke on it.
          */
         struct panfrost_ptr ptr = pan_pool_alloc_desc(&cmdbuf->desc_pool.base,
                                                       JOB_HEADER);
         util_dynarray_append(&batch->jobs, void *, ptr.cpu);
         panfrost_add_job(&cmdbuf->desc_pool.base, &batch->scoreboard,
                          MALI_JOB_TYPE_NULL, false, false, 0, 0,
                          &ptr, false);
         list_addtail(&batch->node, &cmdbuf->batches);
      }
      cmdbuf->state.batch = NULL;
      return;
   }

   struct panfrost_device *pdev = &cmdbuf->device->physical_device->pdev;

   list_addtail(&cmdbuf->state.batch->node, &cmdbuf->batches);

   struct pan_tls_info tlsinfo = { 0 };

   if (cmdbuf->state.pipeline) {
      tlsinfo.tls.size = cmdbuf->state.pipeline->tls_size;
      tlsinfo.wls.size = cmdbuf->state.pipeline->wls_size;
   }

   if (tlsinfo.tls.size) {
      tlsinfo.tls.ptr =
         pan_pool_alloc_aligned(&cmdbuf->tls_pool.base, tlsinfo.tls.size, 4096).gpu;
   }

   if (tlsinfo.wls.size) {
      unsigned wls_size =
         pan_wls_mem_size(pdev, &cmdbuf->state.compute.wg_count, tlsinfo.wls.size);
      tlsinfo.wls.ptr =
         pan_pool_alloc_aligned(&cmdbuf->tls_pool.base, wls_size, 4096).gpu;
   }

   if ((PAN_ARCH >= 6 || !cmdbuf->state.batch->fb.desc.cpu) &&
       cmdbuf->state.batch->tls.cpu) {
      pan_emit_tls(pdev, &tlsinfo, cmdbuf->state.batch->tls.cpu);
   }

   if (cmdbuf->state.batch->fb.desc.cpu) {
#if PAN_ARCH == 5
      panvk_per_arch(cmd_get_polygon_list)(cmdbuf,
                                           batch->fb.info->width,
                                           batch->fb.info->height,
                                           false);

      mali_ptr polygon_list =
         cmdbuf->state.batch->tiler.ctx.midgard.polygon_list->ptr.gpu;
      struct panfrost_ptr writeval_job =
         panfrost_scoreboard_initialize_tiler(&cmdbuf->desc_pool.base,
                                              &cmdbuf->state.batch->scoreboard,
                                              polygon_list);
      if (writeval_job.cpu)
         util_dynarray_append(&cmdbuf->state.batch->jobs, void *, writeval_job.cpu);
#endif

#if PAN_ARCH <= 5
      void *fbd = tmp_fbd;
#else
      void *fbd = cmdbuf->state.batch->fb.desc.cpu;
#endif

      cmdbuf->state.batch->fb.desc.gpu |=
         panvk_per_arch(emit_fb)(cmdbuf->device,
                                 cmdbuf->state.batch,
                                 cmdbuf->state.subpass,
                                 cmdbuf->state.framebuffer,
                                 cmdbuf->state.clear,
                                 &tlsinfo, &cmdbuf->state.batch->tiler.ctx,
                                 fbd);

#if PAN_ARCH <= 5
      panvk_copy_fb_desc(cmdbuf, tmp_fbd);
      memcpy(cmdbuf->state.batch->tiler.templ,
             pan_section_ptr(fbd, MULTI_TARGET_FRAMEBUFFER, TILER),
             pan_size(TILER_CONTEXT));
#endif

      panvk_cmd_prepare_fragment_job(cmdbuf);
   }

   cmdbuf->state.batch = NULL;
}

void
panvk_per_arch(CmdNextSubpass2)(VkCommandBuffer commandBuffer,
                                const VkSubpassBeginInfo *pSubpassBeginInfo,
                                const VkSubpassEndInfo *pSubpassEndInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   panvk_per_arch(cmd_close_batch)(cmdbuf);

   cmdbuf->state.subpass++;
   panvk_cmd_open_batch(cmdbuf);
   memset(&cmdbuf->state.compute, 0, sizeof(cmdbuf->state.compute));
}

void
panvk_per_arch(CmdNextSubpass)(VkCommandBuffer cmd, VkSubpassContents contents)
{
   VkSubpassBeginInfo binfo = {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
      .contents = contents
   };
   VkSubpassEndInfo einfo = {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
   };

   panvk_per_arch(CmdNextSubpass2)(cmd, &binfo, &einfo);
}

void
panvk_per_arch(cmd_alloc_fb_desc)(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_batch *batch = cmdbuf->state.batch;

   if (batch->fb.desc.gpu)
      return;

   const struct panvk_subpass *subpass = cmdbuf->state.subpass;
   bool has_zs_ext = subpass->zs_attachment.idx != VK_ATTACHMENT_UNUSED;
   unsigned tags = MALI_FBD_TAG_IS_MFBD;

   batch->fb.info = cmdbuf->state.framebuffer;
   batch->fb.desc =
      pan_pool_alloc_desc_aggregate(&cmdbuf->desc_pool.base,
                                    PAN_DESC(MULTI_TARGET_FRAMEBUFFER),
                                    PAN_DESC_ARRAY(has_zs_ext ? 1 : 0, ZS_CRC_EXTENSION),
                                    PAN_DESC_ARRAY(MAX2(subpass->color_count, 1), RENDER_TARGET));

   /* Tag the pointer */
   batch->fb.desc.gpu |= tags;
}

void
panvk_per_arch(cmd_alloc_tls_desc)(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_batch *batch = cmdbuf->state.batch;

   assert(batch);
   if (batch->tls.gpu)
      return;

   if (PAN_ARCH == 5 &&
       cmdbuf->state.bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);
      batch->tls = batch->fb.desc;
      batch->tls.gpu &= ~63ULL;
   } else {
      batch->tls =
         pan_pool_alloc_desc(&cmdbuf->desc_pool.base, LOCAL_STORAGE);
   }
}

static void
panvk_cmd_upload_sysval(struct panvk_cmd_buffer *cmdbuf,
                        unsigned id, union panvk_sysval_data *data)
{
   switch (PAN_SYSVAL_TYPE(id)) {
   case PAN_SYSVAL_VIEWPORT_SCALE:
      panvk_sysval_upload_viewport_scale(&cmdbuf->state.viewport, data);
      break;
   case PAN_SYSVAL_VIEWPORT_OFFSET:
      panvk_sysval_upload_viewport_offset(&cmdbuf->state.viewport, data);
      break;
   case PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS:
      /* TODO: support base_{vertex,instance} */
      data->u32[0] = data->u32[1] = data->u32[2] = 0;
      break;
   default:
      unreachable("Invalid static sysval");
   }
}

static void
panvk_cmd_prepare_sysvals(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_descriptor_state *desc_state =
      &cmdbuf->descriptors[cmdbuf->state.bind_point];
   const struct panvk_pipeline *pipeline = cmdbuf->state.pipeline;

   if (!pipeline->num_sysvals)
      return;

   for (unsigned i = 0; i < ARRAY_SIZE(desc_state->sysvals); i++) {
      unsigned sysval_count = pipeline->sysvals[i].ids.sysval_count;
      if (!sysval_count ||
          (desc_state->sysvals[i] &&
           !(cmdbuf->state.dirty & pipeline->sysvals[i].dirty_mask)))
         continue;

      struct panfrost_ptr sysvals =
         pan_pool_alloc_aligned(&cmdbuf->desc_pool.base, sysval_count * 16, 16);
      union panvk_sysval_data *data = sysvals.cpu;

      for (unsigned s = 0; s < pipeline->sysvals[i].ids.sysval_count; s++) {
         panvk_cmd_upload_sysval(cmdbuf, pipeline->sysvals[i].ids.sysvals[s],
                                 &data[s]);
      }

      desc_state->sysvals[i] = sysvals.gpu;
   }
}

static void
panvk_cmd_prepare_ubos(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_descriptor_state *desc_state =
      &cmdbuf->descriptors[cmdbuf->state.bind_point];
   const struct panvk_pipeline *pipeline =
      cmdbuf->state.pipeline;

   if (!pipeline->num_ubos || desc_state->ubos)
      return;

   panvk_cmd_prepare_sysvals(cmdbuf);

   struct panfrost_ptr ubos =
      pan_pool_alloc_desc_array(&cmdbuf->desc_pool.base,
                                pipeline->num_ubos,
                                UNIFORM_BUFFER);

   panvk_per_arch(emit_ubos)(pipeline, desc_state, ubos.cpu);

   desc_state->ubos = ubos.gpu;
}

static void
panvk_cmd_prepare_textures(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_descriptor_state *desc_state =
      &cmdbuf->descriptors[cmdbuf->state.bind_point];
   const struct panvk_pipeline *pipeline = cmdbuf->state.pipeline;
   unsigned num_textures = pipeline->layout->num_textures;

   if (!num_textures || desc_state->textures)
      return;

   unsigned tex_entry_size = PAN_ARCH >= 6 ?
                             pan_size(TEXTURE) :
                             sizeof(mali_ptr);
   struct panfrost_ptr textures =
      pan_pool_alloc_aligned(&cmdbuf->desc_pool.base,
                             num_textures * tex_entry_size,
                             tex_entry_size);

   void *texture = textures.cpu;

   for (unsigned i = 0; i < ARRAY_SIZE(desc_state->sets); i++) {
      if (!desc_state->sets[i].set) continue;

      memcpy(texture,
             desc_state->sets[i].set->textures,
             desc_state->sets[i].set->layout->num_textures *
             tex_entry_size);

      texture += desc_state->sets[i].set->layout->num_textures *
                 tex_entry_size;
   }

   desc_state->textures = textures.gpu;
}

static void
panvk_cmd_prepare_samplers(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_descriptor_state *desc_state =
      &cmdbuf->descriptors[cmdbuf->state.bind_point];
   const struct panvk_pipeline *pipeline = cmdbuf->state.pipeline;
   unsigned num_samplers = pipeline->layout->num_samplers;

   if (!num_samplers || desc_state->samplers)
      return;

   struct panfrost_ptr samplers =
      pan_pool_alloc_desc_array(&cmdbuf->desc_pool.base,
                                num_samplers,
                                SAMPLER);

   void *sampler = samplers.cpu;

   for (unsigned i = 0; i < ARRAY_SIZE(desc_state->sets); i++) {
      if (!desc_state->sets[i].set) continue;

      memcpy(sampler,
             desc_state->sets[i].set->samplers,
             desc_state->sets[i].set->layout->num_samplers *
             pan_size(SAMPLER));

      sampler += desc_state->sets[i].set->layout->num_samplers;
   }

   desc_state->samplers = samplers.gpu;
}

static void
panvk_draw_prepare_fs_rsd(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_draw_info *draw)
{
   const struct panvk_pipeline *pipeline = cmdbuf->state.pipeline;

   if (!pipeline->fs.dynamic_rsd) {
      draw->fs_rsd = pipeline->rsds[MESA_SHADER_FRAGMENT];
      return;
   }

   if (!cmdbuf->state.fs_rsd) {
      struct panfrost_ptr rsd =
         pan_pool_alloc_desc_aggregate(&cmdbuf->desc_pool.base,
                                       PAN_DESC(RENDERER_STATE),
                                       PAN_DESC_ARRAY(pipeline->blend.state.rt_count,
                                                      BLEND));

      struct mali_renderer_state_packed rsd_dyn;
      struct mali_renderer_state_packed *rsd_templ =
         (struct mali_renderer_state_packed *)&pipeline->fs.rsd_template;

      STATIC_ASSERT(sizeof(pipeline->fs.rsd_template) >= sizeof(*rsd_templ));

      panvk_per_arch(emit_dyn_fs_rsd)(pipeline, &cmdbuf->state, &rsd_dyn);
      pan_merge(rsd_dyn, (*rsd_templ), RENDERER_STATE);
      memcpy(rsd.cpu, &rsd_dyn, sizeof(rsd_dyn));

      void *bd = rsd.cpu + pan_size(RENDERER_STATE);
      for (unsigned i = 0; i < pipeline->blend.state.rt_count; i++) {
         if (pipeline->blend.constant[i].index != ~0) {
            struct mali_blend_packed bd_dyn;
            struct mali_blend_packed *bd_templ =
               (struct mali_blend_packed *)&pipeline->blend.bd_template[i];

            STATIC_ASSERT(sizeof(pipeline->blend.bd_template[0]) >= sizeof(*bd_templ));
            panvk_per_arch(emit_blend_constant)(cmdbuf->device, pipeline, i,
                                                cmdbuf->state.blend.constants,
                                                &bd_dyn);
            pan_merge(bd_dyn, (*bd_templ), BLEND);
            memcpy(bd, &bd_dyn, sizeof(bd_dyn));
         }
         bd += pan_size(BLEND);
      }

      cmdbuf->state.fs_rsd = rsd.gpu;
   }

   draw->fs_rsd = cmdbuf->state.fs_rsd;
}

#if PAN_ARCH >= 6
void
panvk_per_arch(cmd_get_tiler_context)(struct panvk_cmd_buffer *cmdbuf,
                                      unsigned width, unsigned height)
{
   struct panvk_batch *batch = cmdbuf->state.batch;

   if (batch->tiler.descs.cpu)
      return;

   batch->tiler.descs =
      pan_pool_alloc_desc_aggregate(&cmdbuf->desc_pool.base,
                                    PAN_DESC(TILER_CONTEXT),
                                    PAN_DESC(TILER_HEAP));
   STATIC_ASSERT(sizeof(batch->tiler.templ) >=
                 pan_size(TILER_CONTEXT) + pan_size(TILER_HEAP));

   struct panfrost_ptr desc = {
      .gpu = batch->tiler.descs.gpu,
      .cpu = batch->tiler.templ,
   };

   panvk_per_arch(emit_tiler_context)(cmdbuf->device, width, height, &desc);
   memcpy(batch->tiler.descs.cpu, batch->tiler.templ,
          pan_size(TILER_CONTEXT) + pan_size(TILER_HEAP));
   batch->tiler.ctx.bifrost = batch->tiler.descs.gpu;
}
#endif

static void
panvk_draw_prepare_tiler_context(struct panvk_cmd_buffer *cmdbuf,
                                 struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->state.batch;

#if PAN_ARCH == 5
   panvk_per_arch(cmd_get_polygon_list)(cmdbuf,
                                        batch->fb.info->width,
                                        batch->fb.info->height,
                                        true);
#else
   panvk_per_arch(cmd_get_tiler_context)(cmdbuf,
                                         batch->fb.info->width,
                                         batch->fb.info->height);
#endif

   draw->tiler_ctx = &batch->tiler.ctx;
}

static void
panvk_draw_prepare_varyings(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_info *draw)
{
   const struct panvk_pipeline *pipeline = cmdbuf->state.pipeline;
   struct panvk_varyings_info *varyings = &cmdbuf->state.varyings;

   panvk_varyings_alloc(varyings, &cmdbuf->varying_pool.base,
                        draw->vertex_count);

   unsigned buf_count = panvk_varyings_buf_count(varyings);
   struct panfrost_ptr bufs =
      pan_pool_alloc_desc_array(&cmdbuf->desc_pool.base,
                                buf_count, ATTRIBUTE_BUFFER);

   panvk_per_arch(emit_varying_bufs)(varyings, bufs.cpu);
   if (BITSET_TEST(varyings->active, VARYING_SLOT_POS)) {
      draw->position = varyings->buf[varyings->varying[VARYING_SLOT_POS].buf].address +
                       varyings->varying[VARYING_SLOT_POS].offset;
   }

   if (BITSET_TEST(varyings->active, VARYING_SLOT_PSIZ)) {
      draw->psiz = varyings->buf[varyings->varying[VARYING_SLOT_PSIZ].buf].address +
                       varyings->varying[VARYING_SLOT_POS].offset;
   } else if (pipeline->ia.topology == MALI_DRAW_MODE_LINES ||
              pipeline->ia.topology == MALI_DRAW_MODE_LINE_STRIP ||
              pipeline->ia.topology == MALI_DRAW_MODE_LINE_LOOP) {
      draw->line_width = pipeline->dynamic_state_mask & PANVK_DYNAMIC_LINE_WIDTH ?
                         cmdbuf->state.rast.line_width : pipeline->rast.line_width;
   } else {
      draw->line_width = 1.0f;
   }
   draw->varying_bufs = bufs.gpu;

   for (unsigned s = 0; s < MESA_SHADER_STAGES; s++) {
      if (!varyings->stage[s].count) continue;

      struct panfrost_ptr attribs =
         pan_pool_alloc_desc_array(&cmdbuf->desc_pool.base,
                                   varyings->stage[s].count,
                                   ATTRIBUTE);

      panvk_per_arch(emit_varyings)(cmdbuf->device, varyings, s, attribs.cpu);
      draw->stages[s].varyings = attribs.gpu;
   }
}

static void
panvk_draw_prepare_attributes(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_info *draw)
{
   /* TODO: images */
   if (!cmdbuf->state.pipeline->attribs.buf_count)
      return;

   if (cmdbuf->state.vb.attribs) {
      draw->stages[MESA_SHADER_VERTEX].attributes = cmdbuf->state.vb.attribs;
      draw->attribute_bufs = cmdbuf->state.vb.attrib_bufs;
      return;
   }

   unsigned buf_count = cmdbuf->state.pipeline->attribs.buf_count +
                        (PAN_ARCH >= 6 ? 1 : 0);
   struct panfrost_ptr bufs =
      pan_pool_alloc_desc_array(&cmdbuf->desc_pool.base,
                                buf_count * 2, ATTRIBUTE_BUFFER);

   panvk_per_arch(emit_attrib_bufs)(&cmdbuf->state.pipeline->attribs,
                                    cmdbuf->state.vb.bufs,
                                    cmdbuf->state.vb.count,
                                    draw, bufs.cpu);
   cmdbuf->state.vb.attrib_bufs = bufs.gpu;

   struct panfrost_ptr attribs =
      pan_pool_alloc_desc_array(&cmdbuf->desc_pool.base,
                                cmdbuf->state.pipeline->attribs.attrib_count,
                                ATTRIBUTE);

   panvk_per_arch(emit_attribs)(cmdbuf->device, &cmdbuf->state.pipeline->attribs,
                                cmdbuf->state.vb.bufs, cmdbuf->state.vb.count,
                                attribs.cpu);
   cmdbuf->state.vb.attribs = attribs.gpu;
   draw->stages[MESA_SHADER_VERTEX].attributes = cmdbuf->state.vb.attribs;
   draw->attribute_bufs = cmdbuf->state.vb.attrib_bufs;
}

static void
panvk_draw_prepare_viewport(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_info *draw)
{
   const struct panvk_pipeline *pipeline = cmdbuf->state.pipeline;

   if (pipeline->vpd) {
      draw->viewport = pipeline->vpd;
   } else if (cmdbuf->state.vpd) {
      draw->viewport = cmdbuf->state.vpd;
   } else {
      struct panfrost_ptr vp =
         pan_pool_alloc_desc(&cmdbuf->desc_pool.base, VIEWPORT);

      const VkViewport *viewport =
         pipeline->dynamic_state_mask & PANVK_DYNAMIC_VIEWPORT ?
         &cmdbuf->state.viewport : &pipeline->viewport;
      const VkRect2D *scissor =
         pipeline->dynamic_state_mask & PANVK_DYNAMIC_SCISSOR ?
         &cmdbuf->state.scissor : &pipeline->scissor;

      panvk_per_arch(emit_viewport)(viewport, scissor, vp.cpu);
      draw->viewport = cmdbuf->state.vpd = vp.gpu;
   }
}

static void
panvk_draw_prepare_vertex_job(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->state.batch;
   struct panfrost_ptr ptr =
      pan_pool_alloc_desc(&cmdbuf->desc_pool.base, COMPUTE_JOB);

   util_dynarray_append(&batch->jobs, void *, ptr.cpu);
   draw->jobs.vertex = ptr;
   panvk_per_arch(emit_vertex_job)(cmdbuf->state.pipeline,
                                   draw, ptr.cpu);

}

static void
panvk_draw_prepare_tiler_job(struct panvk_cmd_buffer *cmdbuf,
                             struct panvk_draw_info *draw)
{
   struct panvk_batch *batch = cmdbuf->state.batch;
   struct panfrost_ptr ptr =
      pan_pool_alloc_desc(&cmdbuf->desc_pool.base, TILER_JOB);

   util_dynarray_append(&batch->jobs, void *, ptr.cpu);
   draw->jobs.tiler = ptr;
   panvk_per_arch(emit_tiler_job)(cmdbuf->state.pipeline,
                                  draw, ptr.cpu);
}

void
panvk_per_arch(CmdDraw)(VkCommandBuffer commandBuffer,
                        uint32_t vertexCount,
                        uint32_t instanceCount,
                        uint32_t firstVertex,
                        uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   struct panvk_batch *batch = cmdbuf->state.batch;

   /* There are only 16 bits in the descriptor for the job ID, make sure all
    * the 3 (2 in Bifrost) jobs in this draw are in the same batch.
    */
   if (batch->scoreboard.job_index >= (UINT16_MAX - 3)) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_cmd_open_batch(cmdbuf);
      batch = cmdbuf->state.batch;
   }

   if (cmdbuf->state.pipeline->fs.required)
      panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);

   panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf);
   panvk_cmd_prepare_ubos(cmdbuf);
   panvk_cmd_prepare_textures(cmdbuf);
   panvk_cmd_prepare_samplers(cmdbuf);

   /* TODO: indexed draws */

   struct panvk_draw_info draw = {
      .first_vertex = firstVertex,
      .vertex_count = vertexCount,
      .first_instance = firstInstance,
      .instance_count = instanceCount,
      .padded_vertex_count = panfrost_padded_vertex_count(vertexCount),
      .offset_start = firstVertex,
      .tls = batch->tls.gpu,
      .fb = batch->fb.desc.gpu,
      .ubos = cmdbuf->descriptors[VK_PIPELINE_BIND_POINT_GRAPHICS].ubos,
      .textures = cmdbuf->descriptors[VK_PIPELINE_BIND_POINT_GRAPHICS].textures,
      .samplers = cmdbuf->descriptors[VK_PIPELINE_BIND_POINT_GRAPHICS].samplers,
   };

   STATIC_ASSERT(sizeof(draw.invocation) >= sizeof(struct mali_invocation_packed));
   panfrost_pack_work_groups_compute((struct mali_invocation_packed *)&draw.invocation,
                                     1, vertexCount, instanceCount, 1, 1, 1, true, false);
   panvk_draw_prepare_fs_rsd(cmdbuf, &draw);
   panvk_draw_prepare_varyings(cmdbuf, &draw);
   panvk_draw_prepare_attributes(cmdbuf, &draw);
   panvk_draw_prepare_viewport(cmdbuf, &draw);
   panvk_draw_prepare_tiler_context(cmdbuf, &draw);
   panvk_draw_prepare_vertex_job(cmdbuf, &draw);
   panvk_draw_prepare_tiler_job(cmdbuf, &draw);

   const struct panvk_pipeline *pipeline = cmdbuf->state.pipeline;
   unsigned vjob_id =
      panfrost_add_job(&cmdbuf->desc_pool.base, &batch->scoreboard,
                       MALI_JOB_TYPE_VERTEX, false, false, 0, 0,
                       &draw.jobs.vertex, false);

   if (pipeline->fs.required) {
      panfrost_add_job(&cmdbuf->desc_pool.base, &batch->scoreboard,
                       MALI_JOB_TYPE_TILER, false, false, vjob_id, 0,
                       &draw.jobs.tiler, false);
   }

   /* Clear the dirty flags all at once */
   cmdbuf->state.dirty = 0;
}

VkResult
panvk_per_arch(EndCommandBuffer)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (cmdbuf->state.batch)
      panvk_per_arch(cmd_close_batch)(cmdbuf);

   cmdbuf->status = PANVK_CMD_BUFFER_STATUS_EXECUTABLE;

   return cmdbuf->record_result;
}

void
panvk_per_arch(CmdEndRenderPass2)(VkCommandBuffer commandBuffer,
                                  const VkSubpassEndInfoKHR *pSubpassEndInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   panvk_per_arch(cmd_close_batch)(cmdbuf);
   vk_free(&cmdbuf->pool->alloc, cmdbuf->state.clear);
   cmdbuf->state.batch = NULL;
   cmdbuf->state.pass = NULL;
   cmdbuf->state.subpass = NULL;
   cmdbuf->state.framebuffer = NULL;
   cmdbuf->state.clear = NULL;
   memset(&cmdbuf->state.compute, 0, sizeof(cmdbuf->state.compute));
}

void
panvk_per_arch(CmdEndRenderPass)(VkCommandBuffer cmd)
{
   VkSubpassEndInfoKHR einfo = {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
   };

   panvk_per_arch(CmdEndRenderPass2)(cmd, &einfo);
}


void
panvk_per_arch(CmdPipelineBarrier)(VkCommandBuffer commandBuffer,
                                   VkPipelineStageFlags srcStageMask,
                                   VkPipelineStageFlags destStageMask,
                                   VkDependencyFlags dependencyFlags,
                                   uint32_t memoryBarrierCount,
                                   const VkMemoryBarrier *pMemoryBarriers,
                                   uint32_t bufferMemoryBarrierCount,
                                   const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                                   uint32_t imageMemoryBarrierCount,
                                   const VkImageMemoryBarrier *pImageMemoryBarriers)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   /* Caches are flushed/invalidated at batch boundaries for now, nothing to do
    * for memory barriers assuming we implement barriers with the creation of a
    * new batch.
    * FIXME: We can probably do better with a CacheFlush job that has the
    * barrier flag set to true.
    */
   if (cmdbuf->state.batch) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_cmd_open_batch(cmdbuf);
   }
}

static void
panvk_add_set_event_operation(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_event *event,
                              enum panvk_event_op_type type)
{
   struct panvk_event_op op = {
      .type = type,
      .event = event,
   };

   if (cmdbuf->state.batch == NULL) {
      /* No open batch, let's create a new one so this operation happens in
       * the right order.
       */
      panvk_cmd_open_batch(cmdbuf);
      util_dynarray_append(&cmdbuf->state.batch->event_ops,
                           struct panvk_event_op,
                           op);
      panvk_per_arch(cmd_close_batch)(cmdbuf);
   } else {
      /* Let's close the current batch so the operation executes before any
       * future commands.
       */
      util_dynarray_append(&cmdbuf->state.batch->event_ops,
                           struct panvk_event_op,
                           op);
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_cmd_open_batch(cmdbuf);
   }
}

static void
panvk_add_wait_event_operation(struct panvk_cmd_buffer *cmdbuf,
                               struct panvk_event *event)
{
   struct panvk_event_op op = {
      .type = PANVK_EVENT_OP_WAIT,
      .event = event,
   };

   if (cmdbuf->state.batch == NULL) {
      /* No open batch, let's create a new one and have it wait for this event. */
      panvk_cmd_open_batch(cmdbuf);
      util_dynarray_append(&cmdbuf->state.batch->event_ops,
                           struct panvk_event_op,
                           op);
   } else {
      /* Let's close the current batch so any future commands wait on the
       * event signal operation.
       */
      if (cmdbuf->state.batch->fragment_job ||
          cmdbuf->state.batch->scoreboard.first_job) {
         panvk_per_arch(cmd_close_batch)(cmdbuf);
         panvk_cmd_open_batch(cmdbuf);
      }
      util_dynarray_append(&cmdbuf->state.batch->event_ops,
                           struct panvk_event_op,
                           op);
   }
}

void
panvk_per_arch(CmdSetEvent)(VkCommandBuffer commandBuffer,
                            VkEvent _event,
                            VkPipelineStageFlags stageMask)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_event, event, _event);

   /* vkCmdSetEvent cannot be called inside a render pass */
   assert(cmdbuf->state.pass == NULL);

   panvk_add_set_event_operation(cmdbuf, event, PANVK_EVENT_OP_SET);
}

void
panvk_per_arch(CmdResetEvent)(VkCommandBuffer commandBuffer,
                              VkEvent _event,
                              VkPipelineStageFlags stageMask)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_event, event, _event);

   /* vkCmdResetEvent cannot be called inside a render pass */
   assert(cmdbuf->state.pass == NULL);

   panvk_add_set_event_operation(cmdbuf, event, PANVK_EVENT_OP_RESET);
}

void
panvk_per_arch(CmdWaitEvents)(VkCommandBuffer commandBuffer,
                              uint32_t eventCount,
                              const VkEvent *pEvents,
                              VkPipelineStageFlags srcStageMask,
                              VkPipelineStageFlags dstStageMask,
                              uint32_t memoryBarrierCount,
                              const VkMemoryBarrier *pMemoryBarriers,
                              uint32_t bufferMemoryBarrierCount,
                              const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                              uint32_t imageMemoryBarrierCount,
                              const VkImageMemoryBarrier *pImageMemoryBarriers)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   assert(eventCount > 0);

   for (uint32_t i = 0; i < eventCount; i++) {
      VK_FROM_HANDLE(panvk_event, event, pEvents[i]);
      panvk_add_wait_event_operation(cmdbuf, event);
   }
}

static VkResult
panvk_reset_cmdbuf(struct panvk_cmd_buffer *cmdbuf)
{
   cmdbuf->record_result = VK_SUCCESS;

   list_for_each_entry_safe(struct panvk_batch, batch, &cmdbuf->batches, node) {
      list_del(&batch->node);
      util_dynarray_fini(&batch->jobs);
#if PAN_ARCH <= 5
      panfrost_bo_unreference(batch->tiler.ctx.midgard.polygon_list);
#endif

      util_dynarray_fini(&batch->event_ops);

      vk_free(&cmdbuf->pool->alloc, batch);
   }

   panvk_pool_reset(&cmdbuf->desc_pool);
   panvk_pool_reset(&cmdbuf->tls_pool);
   panvk_pool_reset(&cmdbuf->varying_pool);
   cmdbuf->status = PANVK_CMD_BUFFER_STATUS_INITIAL;

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++)
      memset(&cmdbuf->descriptors[i].sets, 0, sizeof(cmdbuf->descriptors[i].sets));

   return cmdbuf->record_result;
}

static void
panvk_destroy_cmdbuf(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *device = cmdbuf->device;

   list_del(&cmdbuf->pool_link);

   list_for_each_entry_safe(struct panvk_batch, batch, &cmdbuf->batches, node) {
      list_del(&batch->node);
      util_dynarray_fini(&batch->jobs);
#if PAN_ARCH <= 5
      panfrost_bo_unreference(batch->tiler.ctx.midgard.polygon_list);
#endif

      util_dynarray_fini(&batch->event_ops);

      vk_free(&cmdbuf->pool->alloc, batch);
   }

   panvk_pool_cleanup(&cmdbuf->desc_pool);
   panvk_pool_cleanup(&cmdbuf->tls_pool);
   panvk_pool_cleanup(&cmdbuf->varying_pool);
   vk_object_free(&device->vk, NULL, cmdbuf);
}

static VkResult
panvk_create_cmdbuf(struct panvk_device *device,
                    struct panvk_cmd_pool *pool,
                    VkCommandBufferLevel level,
                    struct panvk_cmd_buffer **cmdbuf_out)
{
   struct panvk_cmd_buffer *cmdbuf;

   cmdbuf = vk_object_zalloc(&device->vk, NULL, sizeof(*cmdbuf),
                             VK_OBJECT_TYPE_COMMAND_BUFFER);
   if (!cmdbuf)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   cmdbuf->device = device;
   cmdbuf->level = level;
   cmdbuf->pool = pool;

   if (pool) {
      list_addtail(&cmdbuf->pool_link, &pool->active_cmd_buffers);
      cmdbuf->queue_family_index = pool->queue_family_index;
   } else {
      /* Init the pool_link so we can safely call list_del when we destroy
       * the command buffer
       */
      list_inithead(&cmdbuf->pool_link);
      cmdbuf->queue_family_index = PANVK_QUEUE_GENERAL;
   }

   panvk_pool_init(&cmdbuf->desc_pool, &device->physical_device->pdev,
                   pool ? &pool->desc_bo_pool : NULL, 0, 64 * 1024,
                   "Command buffer descriptor pool", true);
   panvk_pool_init(&cmdbuf->tls_pool, &device->physical_device->pdev,
                   pool ? &pool->tls_bo_pool : NULL,
                   PAN_BO_INVISIBLE, 64 * 1024, "TLS pool", false);
   panvk_pool_init(&cmdbuf->varying_pool, &device->physical_device->pdev,
                   pool ? &pool->varying_bo_pool : NULL,
                   PAN_BO_INVISIBLE, 64 * 1024, "Varyings pool", false);
   list_inithead(&cmdbuf->batches);
   cmdbuf->status = PANVK_CMD_BUFFER_STATUS_INITIAL;
   *cmdbuf_out = cmdbuf;
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(AllocateCommandBuffers)(VkDevice _device,
                                       const VkCommandBufferAllocateInfo *pAllocateInfo,
                                       VkCommandBuffer *pCommandBuffers)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_cmd_pool, pool, pAllocateInfo->commandPool);

   VkResult result = VK_SUCCESS;
   unsigned i;

   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
      struct panvk_cmd_buffer *cmdbuf = NULL;

      if (!list_is_empty(&pool->free_cmd_buffers)) {
         cmdbuf = list_first_entry(
            &pool->free_cmd_buffers, struct panvk_cmd_buffer, pool_link);

         list_del(&cmdbuf->pool_link);
         list_addtail(&cmdbuf->pool_link, &pool->active_cmd_buffers);

         cmdbuf->level = pAllocateInfo->level;
         vk_object_base_reset(&cmdbuf->base);
      } else {
         result = panvk_create_cmdbuf(device, pool, pAllocateInfo->level, &cmdbuf);
      }

      if (result != VK_SUCCESS)
         goto err_free_cmd_bufs;

      pCommandBuffers[i] = panvk_cmd_buffer_to_handle(cmdbuf);
   }

   return VK_SUCCESS;

err_free_cmd_bufs:
   panvk_per_arch(FreeCommandBuffers)(_device, pAllocateInfo->commandPool, i,
                                      pCommandBuffers);
   for (unsigned j = 0; j < i; j++)
      pCommandBuffers[j] = VK_NULL_HANDLE;

   return result;
}

void
panvk_per_arch(FreeCommandBuffers)(VkDevice device,
                                   VkCommandPool commandPool,
                                   uint32_t commandBufferCount,
                                   const VkCommandBuffer *pCommandBuffers)
{
   for (uint32_t i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, pCommandBuffers[i]);

      if (cmdbuf) {
         if (cmdbuf->pool) {
            list_del(&cmdbuf->pool_link);
            panvk_reset_cmdbuf(cmdbuf);
            list_addtail(&cmdbuf->pool_link,
                         &cmdbuf->pool->free_cmd_buffers);
         } else
            panvk_destroy_cmdbuf(cmdbuf);
      }
   }
}

VkResult
panvk_per_arch(ResetCommandBuffer)(VkCommandBuffer commandBuffer,
                                   VkCommandBufferResetFlags flags)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   return panvk_reset_cmdbuf(cmdbuf);
}

VkResult
panvk_per_arch(BeginCommandBuffer)(VkCommandBuffer commandBuffer,
                                   const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VkResult result = VK_SUCCESS;

   if (cmdbuf->status != PANVK_CMD_BUFFER_STATUS_INITIAL) {
      /* If the command buffer has already been reset with
       * vkResetCommandBuffer, no need to do it again.
       */
      result = panvk_reset_cmdbuf(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
   }

   memset(&cmdbuf->state, 0, sizeof(cmdbuf->state));

   cmdbuf->status = PANVK_CMD_BUFFER_STATUS_RECORDING;

   return VK_SUCCESS;
}

void
panvk_per_arch(DestroyCommandPool)(VkDevice _device,
                                   VkCommandPool commandPool,
                                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_cmd_pool, pool, commandPool);

   list_for_each_entry_safe(struct panvk_cmd_buffer, cmdbuf,
                            &pool->active_cmd_buffers, pool_link)
      panvk_destroy_cmdbuf(cmdbuf);

   list_for_each_entry_safe(struct panvk_cmd_buffer, cmdbuf,
                            &pool->free_cmd_buffers, pool_link)
      panvk_destroy_cmdbuf(cmdbuf);

   panvk_bo_pool_cleanup(&pool->desc_bo_pool);
   panvk_bo_pool_cleanup(&pool->varying_bo_pool);
   panvk_bo_pool_cleanup(&pool->tls_bo_pool);
   vk_object_free(&device->vk, pAllocator, pool);
}

VkResult
panvk_per_arch(ResetCommandPool)(VkDevice device,
                                 VkCommandPool commandPool,
                                 VkCommandPoolResetFlags flags)
{
   VK_FROM_HANDLE(panvk_cmd_pool, pool, commandPool);
   VkResult result;

   list_for_each_entry(struct panvk_cmd_buffer, cmdbuf, &pool->active_cmd_buffers,
                       pool_link)
   {
      result = panvk_reset_cmdbuf(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

void
panvk_per_arch(TrimCommandPool)(VkDevice device,
                                VkCommandPool commandPool,
                                VkCommandPoolTrimFlags flags)
{
   VK_FROM_HANDLE(panvk_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   list_for_each_entry_safe(struct panvk_cmd_buffer, cmdbuf,
                            &pool->free_cmd_buffers, pool_link)
      panvk_destroy_cmdbuf(cmdbuf);
}
