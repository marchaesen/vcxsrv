/*
 * Copyright © 2021 Collabora Ltd.
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

#include "nir/nir_builder.h"
#include "pan_blitter.h"
#include "pan_encoder.h"
#include "pan_shader.h"

#include "panvk_private.h"

#include "vk_format.h"

void
panvk_per_arch(CmdBlitImage)(VkCommandBuffer commandBuffer,
                             VkImage srcImage,
                             VkImageLayout srcImageLayout,
                             VkImage destImage,
                             VkImageLayout destImageLayout,
                             uint32_t regionCount,
                             const VkImageBlit *pRegions,
                             VkFilter filter)

{
   panvk_stub();
}

void
panvk_per_arch(CmdCopyImage)(VkCommandBuffer commandBuffer,
                             VkImage srcImage,
                             VkImageLayout srcImageLayout,
                             VkImage destImage,
                             VkImageLayout destImageLayout,
                             uint32_t regionCount,
                             const VkImageCopy *pRegions)
{
   panvk_stub();
}

void
panvk_per_arch(CmdCopyBufferToImage)(VkCommandBuffer commandBuffer,
                                     VkBuffer srcBuffer,
                                     VkImage destImage,
                                     VkImageLayout destImageLayout,
                                     uint32_t regionCount,
                                     const VkBufferImageCopy *pRegions)
{
   panvk_stub();
}

void
panvk_per_arch(CmdCopyImageToBuffer)(VkCommandBuffer commandBuffer,
                                     VkImage srcImage,
                                     VkImageLayout srcImageLayout,
                                     VkBuffer destBuffer,
                                     uint32_t regionCount,
                                     const VkBufferImageCopy *pRegions)
{
   panvk_stub();
}

void
panvk_per_arch(CmdCopyBuffer)(VkCommandBuffer commandBuffer,
                              VkBuffer srcBuffer,
                              VkBuffer destBuffer,
                              uint32_t regionCount,
                              const VkBufferCopy *pRegions)
{
   panvk_stub();
}

void
panvk_per_arch(CmdResolveImage)(VkCommandBuffer cmd_buffer_h,
                                VkImage src_image_h,
                                VkImageLayout src_image_layout,
                                VkImage dest_image_h,
                                VkImageLayout dest_image_layout,
                                uint32_t region_count,
                                const VkImageResolve *regions)
{
   panvk_stub();
}

void
panvk_per_arch(CmdFillBuffer)(VkCommandBuffer commandBuffer,
                              VkBuffer dstBuffer,
                              VkDeviceSize dstOffset,
                              VkDeviceSize fillSize,
                              uint32_t data)
{
   panvk_stub();
}

void
panvk_per_arch(CmdUpdateBuffer)(VkCommandBuffer commandBuffer,
                                VkBuffer dstBuffer,
                                VkDeviceSize dstOffset,
                                VkDeviceSize dataSize,
                                const void *pData)
{
   panvk_stub();
}

void
panvk_per_arch(CmdClearColorImage)(VkCommandBuffer commandBuffer,
                                   VkImage image,
                                   VkImageLayout imageLayout,
                                   const VkClearColorValue *pColor,
                                   uint32_t rangeCount,
                                   const VkImageSubresourceRange *pRanges)
{
   panvk_stub();
}

void
panvk_per_arch(CmdClearDepthStencilImage)(VkCommandBuffer commandBuffer,
                                          VkImage image_h,
                                          VkImageLayout imageLayout,
                                          const VkClearDepthStencilValue *pDepthStencil,
                                          uint32_t rangeCount,
                                          const VkImageSubresourceRange *pRanges)
{
   panvk_stub();
}

static mali_ptr
panvk_meta_emit_viewport(struct pan_pool *pool,
                         uint16_t minx, uint16_t miny,
                         uint16_t maxx, uint16_t maxy)
{
   struct panfrost_ptr vp = pan_pool_alloc_desc(pool, VIEWPORT);

   pan_pack(vp.cpu, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;
   }

   return vp.gpu;
}

static mali_ptr
panvk_meta_clear_attachments_shader(struct panfrost_device *pdev,
                                    struct pan_pool *bin_pool,
                                    unsigned rt,
                                    enum glsl_base_type base_type,
                                    struct pan_shader_info *shader_info)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                     pan_shader_get_compiler_options(pdev),
                                     "panvk_meta_clear_attachment(base_type=%d,rt=%d)",
                                     base_type,
                                     rt);

   b.shader->info.internal = true;
   b.shader->info.num_ubos = 1;

   const struct glsl_type *out_type = glsl_vector_type(base_type, 4);
   nir_variable *out =
      nir_variable_create(b.shader, nir_var_shader_out, out_type, "out");
   out->data.location = FRAG_RESULT_DATA0 + rt;

   nir_ssa_def *clear_values = nir_load_ubo(&b, 4, 32, nir_imm_int(&b, 0),
                                            nir_imm_int(&b, 0),
                                            .align_mul = 4,
                                            .align_offset = 0,
                                            .range_base = 0,
                                            .range = ~0);
   nir_store_var(&b, out, clear_values, 0xff);

   struct panfrost_compile_inputs inputs = {
      .gpu_id = pdev->gpu_id,
      .is_blit = true,
   };

   struct util_dynarray binary;

   util_dynarray_init(&binary, NULL);
   pan_shader_compile(pdev, b.shader, &inputs, &binary, shader_info);

   /* Make sure UBO words have been upgraded to push constants */
   assert(shader_info->ubo_count == 1);
   assert(shader_info->push.count == 4);

   mali_ptr shader =
      pan_pool_upload_aligned(bin_pool, binary.data, binary.size,
                              PAN_ARCH >= 6 ? 128 : 64);

   util_dynarray_fini(&binary);
   ralloc_free(b.shader);

   return shader;
}

static mali_ptr
panvk_meta_clear_attachments_emit_rsd(struct panfrost_device *pdev,
                                      struct pan_pool *desc_pool,
                                      enum pipe_format format,
                                      unsigned rt,
                                      struct pan_shader_info *shader_info,
                                      mali_ptr shader)
{
   struct panfrost_ptr rsd_ptr =
      pan_pool_alloc_desc_aggregate(desc_pool,
                                    PAN_DESC(RENDERER_STATE),
                                    PAN_DESC(BLEND));

   /* TODO: Support multiple render targets */
   assert(rt == 0);

   pan_pack(rsd_ptr.cpu, RENDERER_STATE, cfg) {
      pan_shader_prepare_rsd(pdev, shader_info, shader, &cfg);
      cfg.properties.depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION;
      cfg.multisample_misc.sample_mask = UINT16_MAX;
      cfg.multisample_misc.depth_function = MALI_FUNC_ALWAYS;
      cfg.stencil_mask_misc.stencil_mask_front = 0xFF;
      cfg.stencil_mask_misc.stencil_mask_back = 0xFF;
      cfg.stencil_front.compare_function = MALI_FUNC_ALWAYS;
      cfg.stencil_front.stencil_fail = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.depth_fail = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.depth_pass = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.mask = 0xFF;
      cfg.stencil_back = cfg.stencil_front;

#if PAN_ARCH >= 6
      cfg.properties.bifrost.allow_forward_pixel_to_be_killed = true;
      cfg.properties.bifrost.allow_forward_pixel_to_kill = true;
      cfg.properties.bifrost.zs_update_operation =
         MALI_PIXEL_KILL_STRONG_EARLY;
      cfg.properties.bifrost.pixel_kill_operation =
         MALI_PIXEL_KILL_FORCE_EARLY;
#else
      cfg.properties.midgard.shader_reads_tilebuffer = false;
      cfg.properties.midgard.work_register_count = shader_info->work_reg_count;
      cfg.properties.midgard.force_early_z = true;
      cfg.stencil_mask_misc.alpha_test_compare_function = MALI_FUNC_ALWAYS;
#endif
   }

   pan_pack(rsd_ptr.cpu + pan_size(RENDERER_STATE), BLEND, cfg) {
      cfg.round_to_fb_precision = true;
      cfg.load_destination = false;
#if PAN_ARCH >= 6
      cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_OPAQUE;
      cfg.bifrost.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
      cfg.bifrost.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
      cfg.bifrost.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
      cfg.bifrost.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
      cfg.bifrost.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
      cfg.bifrost.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
      cfg.bifrost.equation.color_mask = 0xf;
      cfg.bifrost.internal.fixed_function.num_comps = 4;
      cfg.bifrost.internal.fixed_function.conversion.memory_format =
         panfrost_format_to_bifrost_blend(pdev, format, false);
      cfg.bifrost.internal.fixed_function.conversion.register_format =
         shader_info->bifrost.blend[rt].format;
#else
      cfg.midgard.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
      cfg.midgard.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
      cfg.midgard.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
      cfg.midgard.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
      cfg.midgard.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
      cfg.midgard.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
      cfg.midgard.equation.color_mask =
         (1 << util_format_get_nr_components(format)) - 1;
#endif
   }

   return rsd_ptr.gpu;
}

static mali_ptr
panvk_meta_clear_attachment_emit_push_constants(struct panfrost_device *pdev,
                                                const struct panfrost_ubo_push *pushmap,
                                                struct pan_pool *pool,
                                                const VkClearValue *clear_value)
{
   assert(pushmap->count <= (sizeof(*clear_value) / 4));

   uint32_t *in = (uint32_t *)clear_value;
   uint32_t pushvals[sizeof(*clear_value) / 4];

   for (unsigned i = 0; i < pushmap->count; i++) {
      assert(i < ARRAY_SIZE(pushvals));
      assert(pushmap->words[i].ubo == 0);
      assert(pushmap->words[i].offset < sizeof(*clear_value));
      pushvals[i] = in[pushmap->words[i].offset / 4];
   }

   return pan_pool_upload_aligned(pool, pushvals, sizeof(pushvals), 16);
}

static mali_ptr
panvk_meta_clear_attachment_emit_ubo(struct panfrost_device *pdev,
                                     const struct panfrost_ubo_push *pushmap,
                                     struct pan_pool *pool,
                                     const VkClearValue *clear_value)
{
   struct panfrost_ptr ubo = pan_pool_alloc_desc(pool, UNIFORM_BUFFER);

   pan_pack(ubo.cpu, UNIFORM_BUFFER, cfg) {
      cfg.entries = DIV_ROUND_UP(sizeof(*clear_value), 16);
      cfg.pointer = pan_pool_upload_aligned(pool, clear_value, sizeof(*clear_value), 16);
   }

   return ubo.gpu;
}

static void
panvk_meta_clear_attachment_emit_dcd(struct pan_pool *pool,
                                     mali_ptr coords,
                                     mali_ptr ubo, mali_ptr push_constants,
                                     mali_ptr vpd, mali_ptr tsd, mali_ptr rsd,
                                     void *out)
{
   pan_pack(out, DRAW, cfg) {
      cfg.four_components_per_vertex = true;
      cfg.draw_descriptor_is_64b = true;
      cfg.thread_storage = tsd;
      cfg.state = rsd;
      cfg.uniform_buffers = ubo;
      cfg.push_uniforms = push_constants;
      cfg.position = coords;
      cfg.viewport = vpd;
      cfg.texture_descriptor_is_64b = PAN_ARCH <= 5;
   }
}

static struct panfrost_ptr
panvk_meta_clear_attachment_emit_tiler_job(struct pan_pool *desc_pool,
                                           struct pan_scoreboard *scoreboard,
                                           mali_ptr coords,
                                           mali_ptr ubo, mali_ptr push_constants,
                                           mali_ptr vpd, mali_ptr rsd,
                                           mali_ptr tsd, mali_ptr tiler)
{
   struct panfrost_ptr job =
      pan_pool_alloc_desc(desc_pool, TILER_JOB);

   panvk_meta_clear_attachment_emit_dcd(desc_pool,
                                        coords,
                                        ubo, push_constants,
                                        vpd, tsd, rsd,
                                        pan_section_ptr(job.cpu, TILER_JOB, DRAW));

   pan_section_pack(job.cpu, TILER_JOB, PRIMITIVE, cfg) {
      cfg.draw_mode = MALI_DRAW_MODE_TRIANGLE_STRIP;
      cfg.index_count = 4;
      cfg.job_task_split = 6;
   }

   pan_section_pack(job.cpu, TILER_JOB, PRIMITIVE_SIZE, cfg) {
      cfg.constant = 1.0f;
   }

   void *invoc = pan_section_ptr(job.cpu,
                                 TILER_JOB,
                                 INVOCATION);
   panfrost_pack_work_groups_compute(invoc, 1, 4,
                                     1, 1, 1, 1, true, false);

#if PAN_ARCH >= 6
   pan_section_pack(job.cpu, TILER_JOB, PADDING, cfg);
   pan_section_pack(job.cpu, TILER_JOB, TILER, cfg) {
      cfg.address = tiler;
   }
#endif

   panfrost_add_job(desc_pool, scoreboard, MALI_JOB_TYPE_TILER,
                    false, false, 0, 0, &job, false);
   return job;
}

static enum glsl_base_type
panvk_meta_get_format_type(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   int i;

   i = util_format_get_first_non_void_channel(format);
   assert(i >= 0);

   if (desc->channel[i].normalized)
      return GLSL_TYPE_FLOAT;

   switch(desc->channel[i].type) {

   case UTIL_FORMAT_TYPE_UNSIGNED:
      return GLSL_TYPE_UINT;

   case UTIL_FORMAT_TYPE_SIGNED:
      return GLSL_TYPE_INT;

   case UTIL_FORMAT_TYPE_FLOAT:
      return GLSL_TYPE_FLOAT;

   default:
      unreachable("Unhandled format");
      return GLSL_TYPE_FLOAT;
   }
}

static void
panvk_meta_clear_attachment(struct panvk_cmd_buffer *cmdbuf,
                            uint32_t attachment,
                            VkImageAspectFlags mask,
                            const VkClearValue *clear_value,
                            const VkClearRect *clear_rect)
{
   struct panvk_physical_device *dev = cmdbuf->device->physical_device;
   struct panfrost_device *pdev = &dev->pdev;
   struct panvk_meta *meta = &cmdbuf->device->physical_device->meta;
   struct panvk_batch *batch = cmdbuf->state.batch;
   const struct panvk_render_pass *pass = cmdbuf->state.pass;
   const struct panvk_render_pass_attachment *att = &pass->attachments[attachment];
   unsigned minx = MAX2(clear_rect->rect.offset.x, 0);
   unsigned miny = MAX2(clear_rect->rect.offset.y, 0);
   unsigned maxx = MAX2(clear_rect->rect.offset.x + clear_rect->rect.extent.width - 1, 0);
   unsigned maxy = MAX2(clear_rect->rect.offset.y + clear_rect->rect.extent.height - 1, 0);

   /* TODO: Support depth/stencil */
   assert(mask == VK_IMAGE_ASPECT_COLOR_BIT);

   panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);
   panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf);

#if PAN_ARCH <= 5
   panvk_per_arch(cmd_get_polygon_list)(cmdbuf,
                                        batch->fb.info->width,
                                        batch->fb.info->height,
                                        true);
#else
   panvk_per_arch(cmd_get_tiler_context)(cmdbuf,
                                         batch->fb.info->width,
                                         batch->fb.info->height);
#endif

   mali_ptr vpd = panvk_meta_emit_viewport(&cmdbuf->desc_pool.base,
                                           minx, miny, maxx, maxy);

   float rect[] = {
      minx, miny, 0.0, 1.0,
      maxx + 1, miny, 0.0, 1.0,
      minx, maxy + 1, 0.0, 1.0,
      maxx + 1, maxy + 1, 0.0, 1.0,
   };
   mali_ptr coordinates = pan_pool_upload_aligned(&cmdbuf->desc_pool.base,
                                                  rect, sizeof(rect), 64);

   enum glsl_base_type base_type = panvk_meta_get_format_type(att->format);
   mali_ptr shader = meta->clear_attachment[attachment][base_type].shader;
   struct pan_shader_info *shader_info =
      &meta->clear_attachment[attachment][base_type].shader_info;

   mali_ptr rsd =
      panvk_meta_clear_attachments_emit_rsd(pdev,
                                            &cmdbuf->desc_pool.base,
                                            att->format,
                                            attachment,
                                            shader_info,
                                            shader);

   mali_ptr pushconsts =
      panvk_meta_clear_attachment_emit_push_constants(pdev, &shader_info->push,
                                                      &cmdbuf->desc_pool.base,
                                                      clear_value);
   mali_ptr ubo =
      panvk_meta_clear_attachment_emit_ubo(pdev, &shader_info->push,
                                           &cmdbuf->desc_pool.base,
                                           clear_value);

   mali_ptr tsd = PAN_ARCH >= 6 ? batch->tls.gpu : batch->fb.desc.gpu;
   mali_ptr tiler = PAN_ARCH >= 6 ? batch->tiler.descs.gpu : 0;

   struct panfrost_ptr job;

   job = panvk_meta_clear_attachment_emit_tiler_job(&cmdbuf->desc_pool.base,
                                                    &batch->scoreboard,
                                                    coordinates,
                                                    ubo, pushconsts,
                                                    vpd, rsd, tsd, tiler);

   util_dynarray_append(&batch->jobs, void *, job.cpu);
}

static void
panvk_meta_clear_attachment_init(struct panvk_physical_device *dev)
{
   for (unsigned rt = 0; rt < MAX_RTS; rt++) {
      dev->meta.clear_attachment[rt][GLSL_TYPE_UINT].shader =
         panvk_meta_clear_attachments_shader(
               &dev->pdev,
               &dev->meta.bin_pool.base,
               rt,
               GLSL_TYPE_UINT,
               &dev->meta.clear_attachment[rt][GLSL_TYPE_UINT].shader_info);

      dev->meta.clear_attachment[rt][GLSL_TYPE_INT].shader =
         panvk_meta_clear_attachments_shader(
               &dev->pdev,
               &dev->meta.bin_pool.base,
               rt,
               GLSL_TYPE_INT,
               &dev->meta.clear_attachment[rt][GLSL_TYPE_INT].shader_info);

      dev->meta.clear_attachment[rt][GLSL_TYPE_FLOAT].shader =
         panvk_meta_clear_attachments_shader(
               &dev->pdev,
               &dev->meta.bin_pool.base,
               rt,
               GLSL_TYPE_FLOAT,
               &dev->meta.clear_attachment[rt][GLSL_TYPE_FLOAT].shader_info);
   }
}

void
panvk_per_arch(CmdClearAttachments)(VkCommandBuffer commandBuffer,
                                    uint32_t attachmentCount,
                                    const VkClearAttachment *pAttachments,
                                    uint32_t rectCount,
                                    const VkClearRect *pRects)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   const struct panvk_subpass *subpass = cmdbuf->state.subpass;

   for (unsigned i = 0; i < attachmentCount; i++) {
      for (unsigned j = 0; j < rectCount; j++) {

         uint32_t attachment;
         if (pAttachments[i].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            unsigned idx = pAttachments[i].colorAttachment;
            attachment = subpass->color_attachments[idx].idx;
         } else {
            attachment = subpass->zs_attachment.idx;
         }

         if (attachment == VK_ATTACHMENT_UNUSED)
               continue;

         panvk_meta_clear_attachment(cmdbuf, attachment,
                                     pAttachments[i].aspectMask,
                                     &pAttachments[i].clearValue,
                                     &pRects[j]);
      }
   }
}

void
panvk_per_arch(meta_init)(struct panvk_physical_device *dev)
{
   panvk_pool_init(&dev->meta.bin_pool, &dev->pdev, NULL, PAN_BO_EXECUTE,
                   16 * 1024, "panvk_meta binary pool", false);
   panvk_pool_init(&dev->meta.desc_pool, &dev->pdev, NULL, 0,
                   16 * 1024, "panvk_meta descriptor pool", false);
   panvk_pool_init(&dev->meta.blitter.bin_pool, &dev->pdev, NULL,
                   PAN_BO_EXECUTE, 16 * 1024,
                   "panvk_meta blitter binary pool", false);
   panvk_pool_init(&dev->meta.blitter.desc_pool, &dev->pdev, NULL,
                   0, 16 * 1024, "panvk_meta blitter descriptor pool",
                   false);
   pan_blitter_init(&dev->pdev, &dev->meta.blitter.bin_pool.base,
                    &dev->meta.blitter.desc_pool.base);
   panvk_meta_clear_attachment_init(dev);
}

void
panvk_per_arch(meta_cleanup)(struct panvk_physical_device *dev)
{
   pan_blitter_cleanup(&dev->pdev);
   panvk_pool_cleanup(&dev->meta.blitter.desc_pool);
   panvk_pool_cleanup(&dev->meta.blitter.bin_pool);
   panvk_pool_cleanup(&dev->meta.desc_pool);
   panvk_pool_cleanup(&dev->meta.bin_pool);
}
