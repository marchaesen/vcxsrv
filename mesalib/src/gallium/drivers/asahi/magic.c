/*
 * Copyright 2021 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#
#include <stdint.h>
#include "agx_state.h"
#include "magic.h"

/* The structures managed in this file appear to be software defined (either in
 * the macOS kernel driver or in the AGX firmware) */

/* Odd pattern */
static uint64_t
demo_unk6(struct agx_pool *pool)
{
   struct agx_ptr ptr = agx_pool_alloc_aligned(pool, 0x4000 * sizeof(uint64_t), 64);
   uint64_t *buf = ptr.cpu;
   memset(buf, 0, sizeof(*buf));

   for (unsigned i = 1; i < 0x3ff; ++i)
      buf[i] = (i + 1);

   return ptr.gpu;
}

static uint64_t
demo_zero(struct agx_pool *pool, unsigned count)
{
   struct agx_ptr ptr = agx_pool_alloc_aligned(pool, count, 64);
   memset(ptr.cpu, 0, count);
   return ptr.gpu;
}

static size_t
asahi_size_resource(struct pipe_resource *prsrc, unsigned level)
{
   struct agx_resource *rsrc = agx_resource(prsrc);
   size_t size = rsrc->layout.size_B;

   if (rsrc->separate_stencil)
      size += asahi_size_resource(&rsrc->separate_stencil->base, level);

   return size;
}

static size_t
asahi_size_surface(struct pipe_surface *surf)
{
   return asahi_size_resource(surf->texture, surf->u.tex.level);
}

static size_t
asahi_size_attachments(struct pipe_framebuffer_state *framebuffer)
{
   size_t sum = 0;

   for (unsigned i = 0; i < framebuffer->nr_cbufs; ++i)
      sum += asahi_size_surface(framebuffer->cbufs[i]);

   if (framebuffer->zsbuf)
      sum += asahi_size_surface(framebuffer->zsbuf);

   return sum;
}

static enum agx_iogpu_attachment_type
asahi_classify_attachment(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   if (util_format_has_depth(desc))
      return AGX_IOGPU_ATTACHMENT_TYPE_DEPTH;
   else if (util_format_has_stencil(desc))
      return AGX_IOGPU_ATTACHMENT_TYPE_STENCIL;
   else
      return AGX_IOGPU_ATTACHMENT_TYPE_COLOUR;
}

static uint64_t
agx_map_surface_resource(struct pipe_surface *surf, struct agx_resource *rsrc)
{
   return agx_map_texture_gpu(rsrc, surf->u.tex.first_layer);
}

static uint64_t
agx_map_surface(struct pipe_surface *surf)
{
   return agx_map_surface_resource(surf, agx_resource(surf->texture));
}

static void
asahi_pack_iogpu_attachment(void *out, struct agx_resource *rsrc,
                            unsigned total_size)
{
   agx_pack(out, IOGPU_ATTACHMENT, cfg) {
      cfg.type = asahi_classify_attachment(rsrc->layout.format);
      cfg.address = rsrc->bo->ptr.gpu;
      cfg.size = rsrc->layout.size_B;
      cfg.percent = (100 * cfg.size) / total_size;
   }
}

static unsigned
asahi_pack_iogpu_attachments(void *out, struct pipe_framebuffer_state *framebuffer)
{
   unsigned total_attachment_size = asahi_size_attachments(framebuffer);
   struct agx_iogpu_attachment_packed *attachments = out;
   unsigned nr = 0;

   for (unsigned i = 0; i < framebuffer->nr_cbufs; ++i) {
      asahi_pack_iogpu_attachment(attachments + (nr++),
                                  agx_resource(framebuffer->cbufs[i]->texture),
                                  total_attachment_size);
   }

   if (framebuffer->zsbuf) {
         struct agx_resource *rsrc = agx_resource(framebuffer->zsbuf->texture);

         asahi_pack_iogpu_attachment(attachments + (nr++),
                                     rsrc, total_attachment_size);

         if (rsrc->separate_stencil) {
            asahi_pack_iogpu_attachment(attachments + (nr++),
                                        rsrc->separate_stencil,
                                        total_attachment_size);
         }
   }

   return nr;
}

unsigned
demo_cmdbuf(uint64_t *buf, size_t size,
            struct agx_pool *pool,
            struct pipe_framebuffer_state *framebuffer,
            uint64_t encoder_ptr,
            uint64_t encoder_id,
            uint64_t scissor_ptr,
            uint64_t depth_bias_ptr,
            uint32_t pipeline_clear,
            uint32_t pipeline_load,
            uint32_t pipeline_store,
            bool clear_pipeline_textures,
            unsigned clear_buffers,
            double clear_depth,
            unsigned clear_stencil)
{
   bool should_clear_depth = clear_buffers & PIPE_CLEAR_DEPTH;
   bool should_clear_stencil = clear_buffers & PIPE_CLEAR_STENCIL;

   uint32_t *map = (uint32_t *) buf;
   memset(map, 0, 518 * 4);

   uint64_t deflake_buffer = demo_zero(pool, 0x7e0);
   uint64_t deflake_1 = deflake_buffer + 0x2a0;
   uint64_t deflake_2 = deflake_buffer + 0x20;

   uint64_t unk_buffer_2 = demo_zero(pool, 0x8000);

   uint64_t depth_buffer = 0;
   uint64_t stencil_buffer = 0;

   agx_pack(map + 16, IOGPU_GRAPHICS, cfg) {
      cfg.opengl_depth_clipping = true;

      cfg.deflake_1 = deflake_1;
      cfg.deflake_2 = deflake_2;
      cfg.deflake_3 = deflake_buffer;

      cfg.clear_pipeline_bind = 0xffff8002 | (clear_pipeline_textures ? 0x210 : 0);
      cfg.clear_pipeline = pipeline_clear;

      /* store pipeline used when entire frame completes */
      cfg.store_pipeline_bind = 0x12;
      cfg.store_pipeline = pipeline_store;
      cfg.scissor_array = scissor_ptr;
      cfg.depth_bias_array = depth_bias_ptr;

      if (framebuffer->zsbuf) {
         struct pipe_surface *zsbuf = framebuffer->zsbuf;
         const struct util_format_description *desc =
            util_format_description(agx_resource(zsbuf->texture)->layout.format);

         assert(desc->format == PIPE_FORMAT_Z32_FLOAT ||
                desc->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
                desc->format == PIPE_FORMAT_S8_UINT);

         cfg.depth_width = framebuffer->width;
         cfg.depth_height = framebuffer->height;

         if (util_format_has_depth(desc)) {
            depth_buffer = agx_map_surface(zsbuf);

            cfg.zls_control.z_store_enable = true;
            cfg.zls_control.z_load_enable = !should_clear_depth;
         } else {
            stencil_buffer = agx_map_surface(zsbuf);
            cfg.zls_control.s_store_enable = true;
            cfg.zls_control.s_load_enable = !should_clear_stencil;
         }

         if (agx_resource(zsbuf->texture)->separate_stencil) {
            stencil_buffer = agx_map_surface_resource(zsbuf,
                  agx_resource(zsbuf->texture)->separate_stencil);

            cfg.zls_control.s_store_enable = true;
            cfg.zls_control.s_load_enable = !should_clear_stencil;
         }

         /* It's unclear how tile size is conveyed for depth/stencil targets,
          * which interactions with mipmapping (for example of a 33x33
          * depth/stencil attachment)
          */
         if (zsbuf->u.tex.level != 0)
            unreachable("todo: mapping other levels");

         cfg.depth_buffer_1 = depth_buffer;
         cfg.depth_buffer_2 = depth_buffer;

         cfg.stencil_buffer_1 = stencil_buffer;
         cfg.stencil_buffer_2 = stencil_buffer;
      }

      cfg.width_1 = framebuffer->width;
      cfg.height_1 = framebuffer->height;
      cfg.pointer = unk_buffer_2;

      cfg.set_when_reloading_z_or_s_1 = clear_pipeline_textures;

      if (depth_buffer && !should_clear_depth) {
         cfg.set_when_reloading_z_or_s_1 = true;
         cfg.set_when_reloading_z_or_s_2 = true;
      }

      if (stencil_buffer && !should_clear_stencil) {
         cfg.set_when_reloading_z_or_s_1 = true;
         cfg.set_when_reloading_z_or_s_2 = true;
      }

      cfg.depth_clear_value = fui(clear_depth);
      cfg.stencil_clear_value = clear_stencil & 0xff;

      cfg.partial_reload_pipeline_bind = 0xffff8212;
      cfg.partial_reload_pipeline = pipeline_load;

      cfg.partial_store_pipeline_bind = 0x12;
      cfg.partial_store_pipeline = pipeline_store;

      cfg.depth_buffer_3 = depth_buffer;
      cfg.stencil_buffer_3 = stencil_buffer;
      cfg.encoder_id = encoder_id;
      cfg.unknown_buffer = demo_unk6(pool);
      cfg.width_2 = framebuffer->width;
      cfg.height_2 = framebuffer->height;
      cfg.unk_352 = clear_pipeline_textures ? 0x0 : 0x1;
   }

   unsigned offset_unk = (484 * 4);
   unsigned offset_attachments = (496 * 4);

   unsigned nr_attachments =
      asahi_pack_iogpu_attachments(map + (offset_attachments / 4) + 4,
                                   framebuffer);

   map[(offset_attachments / 4) + 3] = nr_attachments;

   unsigned total_size = offset_attachments + (AGX_IOGPU_ATTACHMENT_LENGTH * nr_attachments) + 16;

   agx_pack(map, IOGPU_HEADER, cfg) {
      cfg.total_size = total_size;
      cfg.attachment_offset = offset_attachments;
      cfg.attachment_length = nr_attachments * AGX_IOGPU_ATTACHMENT_LENGTH;
      cfg.unknown_offset = offset_unk;
      cfg.encoder = encoder_ptr;
   }

   return total_size;
}

static struct agx_map_header
demo_map_header(uint64_t cmdbuf_id, uint64_t encoder_id, unsigned cmdbuf_size, unsigned count)
{
   /* Structure: header followed by resource groups. For now, we use a single
    * resource group for every resource. This could be optimized.
    */
   unsigned length = sizeof(struct agx_map_header);
   length += count * sizeof(struct agx_map_entry);
   assert(length < 0x10000);

   return (struct agx_map_header) {
      .cmdbuf_id = cmdbuf_id,
      .segment_count = 1,
      .length = length,
      .encoder_id = encoder_id,
      .kernel_commands_start_offset = 0,
      .kernel_commands_end_offset = cmdbuf_size,
      .total_resources = count,
      .resource_group_count = count,
      .unk = 0x8000,
   };
}

void
demo_mem_map(void *map, size_t size, unsigned *handles, unsigned count,
             uint64_t cmdbuf_id, uint64_t encoder_id, unsigned cmdbuf_size)
{
   struct agx_map_header *header = map;
   struct agx_map_entry *entries = (struct agx_map_entry *) (((uint8_t *) map) + sizeof(*header));
   struct agx_map_entry *end = (struct agx_map_entry *) (((uint8_t *) map) + size);

   /* Header precedes the entry */
   *header = demo_map_header(cmdbuf_id, encoder_id, cmdbuf_size, count);

   /* Add an entry for each BO mapped */
   for (unsigned i = 0; i < count; ++i) {
	   assert((entries + i) < end);
      entries[i] = (struct agx_map_entry) {
         .resource_id = { handles[i] },
         .resource_unk = { 0x20 },
         .resource_flags = { 0x1 },
         .resource_count = 1
      };
   }
}
