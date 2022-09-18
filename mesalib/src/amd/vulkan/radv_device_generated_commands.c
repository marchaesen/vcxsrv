/*
 * Copyright © 2021 Google
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

#include "radv_meta.h"
#include "radv_private.h"

#include "nir_builder.h"

static void
radv_get_sequence_size(const struct radv_indirect_command_layout *layout,
                       const struct radv_graphics_pipeline *pipeline, uint32_t *cmd_size,
                       uint32_t *upload_size)
{
   *cmd_size = 0;
   *upload_size = 0;

   if (layout->bind_vbo_mask) {
      *upload_size += 16 * util_bitcount(pipeline->vb_desc_usage_mask);

     /* One PKT3_SET_SH_REG for emitting VBO pointer (32-bit) */
      *cmd_size += 3 * 4;
   }

   if (layout->push_constant_mask) {
      bool need_copy = false;

      for (unsigned i = 0; i < ARRAY_SIZE(pipeline->base.shaders); ++i) {
         if (!pipeline->base.shaders[i])
            continue;

         struct radv_userdata_locations *locs = &pipeline->base.shaders[i]->info.user_sgprs_locs;
         if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0) {
            /* One PKT3_SET_SH_REG for emitting push constants pointer (32-bit) */
            *cmd_size += 3 * 4;
            need_copy = true;
         }
         if (locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0)
            /* One PKT3_SET_SH_REG writing all inline push constants. */
            *cmd_size += (2 + locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].num_sgprs) * 4;
      }
      if (need_copy)
         *upload_size +=
            align(pipeline->base.push_constant_size + 16 * pipeline->base.dynamic_offset_count, 16);
   }

   if (layout->binds_index_buffer) {
      /* Index type write (normal reg write) + index buffer base write (64-bits, but special packet
       * so only 1 word overhead) + index buffer size (again, special packet so only 1 word
       * overhead)
       */
      *cmd_size += (3 + 3 + 2) * 4;
   }

   if (layout->indexed) {
      /* userdata writes + instance count + indexed draw */
      *cmd_size += (5 + 2 + 5) * 4;
   } else {
      /* userdata writes + instance count + non-indexed draw */
      *cmd_size += (5 + 2 + 3) * 4;
   }

   if (layout->binds_state) {
      /* One PKT3_SET_CONTEXT_REG (PA_SU_SC_MODE_CNTL) */
      *cmd_size += 3 * 4;

      if (pipeline->base.device->physical_device->rad_info.has_gfx9_scissor_bug) {
         /* 1 reg write of 4 regs + 1 reg write of 2 regs per scissor */
         *cmd_size += (8 + 2 * MAX_SCISSORS) * 4;
      }
   }
}

static uint32_t
radv_align_cmdbuf_size(uint32_t size)
{
   return align(MAX2(1, size), 256);
}

uint32_t
radv_get_indirect_cmdbuf_size(const VkGeneratedCommandsInfoNV *cmd_info)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, cmd_info->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, cmd_info->pipeline);
   struct radv_graphics_pipeline *graphics_pipeline = radv_pipeline_to_graphics(pipeline);

   uint32_t cmd_size, upload_size;
   radv_get_sequence_size(layout, graphics_pipeline, &cmd_size, &upload_size);
   return radv_align_cmdbuf_size(cmd_size * cmd_info->sequencesCount);
}

enum radv_dgc_token_type {
   RADV_DGC_INDEX_BUFFER,
   RADV_DGC_DRAW,
   RADV_DGC_INDEXED_DRAW,
};

struct radv_dgc_token {
   uint16_t type; /* enum radv_dgc_token_type, but making the size explicit */
   uint16_t offset; /* offset in the input stream */
   union {
      struct {
         uint16_t vtx_base_sgpr;
      } draw;
      struct {
         uint16_t index_size;
         uint16_t vtx_base_sgpr;
         uint32_t max_index_count;
      } indexed_draw;
   };
};

struct radv_dgc_params {
   uint32_t cmd_buf_stride;
   uint32_t cmd_buf_size;
   uint32_t upload_stride;
   uint32_t upload_addr;
   uint32_t sequence_count;
   uint32_t stream_stride;

   /* draw info */
   uint16_t draw_indexed;
   uint16_t draw_params_offset;
   uint16_t base_index_size;
   uint16_t vtx_base_sgpr;
   uint32_t max_index_count;

   /* bind index buffer info. Valid if base_index_size == 0 && draw_indexed */
   uint16_t index_buffer_offset;

   uint8_t vbo_cnt;

   uint8_t const_copy;

   /* Which VBOs are set in this indirect layout. */
   uint32_t vbo_bind_mask;

   uint16_t vbo_reg;
   uint16_t const_copy_size;

   uint64_t push_constant_mask;

   uint32_t ibo_type_32;
   uint32_t ibo_type_8;

   uint16_t push_constant_shader_cnt;

   uint16_t emit_state;
   uint32_t pa_su_sc_mode_cntl_base;
   uint16_t state_offset;
   uint16_t scissor_count;
   uint16_t scissor_offset; /* in parameter buffer. */
};

enum {
   DGC_USES_DRAWID = 1u << 14,
   DGC_USES_BASEINSTANCE = 1u << 15,
};

enum {
   DGC_DYNAMIC_STRIDE = 1u << 15,
};

enum {
   DGC_DESC_STREAM,
   DGC_DESC_PREPARE,
   DGC_DESC_PARAMS,
   DGC_DESC_COUNT,
   DGC_NUM_DESCS,
};

struct dgc_cmdbuf {
   nir_ssa_def *descriptor;
   nir_variable *offset;
};

static void
dgc_emit(nir_builder *b, struct dgc_cmdbuf *cs, nir_ssa_def *value)
{
   assert(value->bit_size >= 32);
   nir_ssa_def *offset = nir_load_var(b, cs->offset);
   nir_store_ssbo(b, value, cs->descriptor, offset,.access = ACCESS_NON_READABLE);
   nir_store_var(b, cs->offset, nir_iadd_imm(b, offset, value->num_components * value->bit_size / 8), 0x1);
}


#define load_param32(b, field)                                                                     \
   nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0),                                         \
                          .base = offsetof(struct radv_dgc_params, field), .range = 4)

#define load_param16(b, field)                                                                     \
   nir_ubfe_imm(                                                                                   \
      (b),                                                                                         \
      nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0),                                      \
                             .base = (offsetof(struct radv_dgc_params, field) & ~3), .range = 4),  \
      (offsetof(struct radv_dgc_params, field) & 2) * 8, 16)

#define load_param8(b, field)                                                                      \
   nir_ubfe_imm(                                                                                   \
      (b),                                                                                         \
      nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0),                                      \
                             .base = (offsetof(struct radv_dgc_params, field) & ~3), .range = 4),  \
      (offsetof(struct radv_dgc_params, field) & 3) * 8, 8)

#define load_param64(b, field)                                                                     \
   nir_pack_64_2x32((b), nir_load_push_constant((b), 2, 32, nir_imm_int((b), 0),                   \
                          .base = offsetof(struct radv_dgc_params, field), .range = 8))

static nir_ssa_def *
nir_pkt3(nir_builder *b, unsigned op, nir_ssa_def *len)
{
   len = nir_iand_imm(b, len, 0x3fff);
   return nir_ior_imm(b, nir_ishl_imm(b, len, 16), PKT_TYPE_S(3) | PKT3_IT_OPCODE_S(op));
}

static void
dgc_emit_userdata_vertex(nir_builder *b, struct dgc_cmdbuf *cs, nir_ssa_def *vtx_base_sgpr,
                         nir_ssa_def *first_vertex, nir_ssa_def *first_instance, nir_ssa_def *drawid)
{
   vtx_base_sgpr = nir_u2u32(b, vtx_base_sgpr);
   nir_ssa_def *has_drawid =
      nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);
   nir_ssa_def *has_baseinstance =
      nir_test_mask(b, vtx_base_sgpr, DGC_USES_BASEINSTANCE);

   nir_ssa_def *pkt_cnt = nir_imm_int(b, 1);
   pkt_cnt = nir_bcsel(b, has_drawid, nir_iadd_imm(b, pkt_cnt, 1), pkt_cnt);
   pkt_cnt = nir_bcsel(b, has_baseinstance, nir_iadd_imm(b, pkt_cnt, 1), pkt_cnt);

   nir_ssa_def *values[5] = {
      nir_pkt3(b, PKT3_SET_SH_REG, pkt_cnt), nir_iand_imm(b, vtx_base_sgpr, 0x3FFF), first_vertex,
      nir_imm_int(b, PKT3_NOP_PAD),          nir_imm_int(b, PKT3_NOP_PAD),
   };

   values[3] = nir_bcsel(b, nir_ior(b, has_drawid, has_baseinstance),
                         nir_bcsel(b, has_drawid, drawid, first_instance), values[4]);
   values[4] = nir_bcsel(b, nir_iand(b, has_drawid, has_baseinstance), first_instance, values[4]);

   dgc_emit(b, cs, nir_vec(b, values, 5));
}

static void
dgc_emit_instance_count(nir_builder *b, struct dgc_cmdbuf *cs, nir_ssa_def *instance_count)
{
   nir_ssa_def *values[2] = {nir_imm_int(b, PKT3(PKT3_NUM_INSTANCES, 0, false)), instance_count};

   dgc_emit(b, cs, nir_vec(b, values, 2));
}

static void
dgc_emit_draw_indexed(nir_builder *b, struct dgc_cmdbuf *cs, nir_ssa_def *index_offset,
                      nir_ssa_def *index_count, nir_ssa_def *max_index_count)
{
   nir_ssa_def *values[5] = {nir_imm_int(b, PKT3(PKT3_DRAW_INDEX_OFFSET_2, 3, false)),
                             max_index_count, index_offset, index_count,
                             nir_imm_int(b, V_0287F0_DI_SRC_SEL_DMA)};

   dgc_emit(b, cs, nir_vec(b, values, 5));
}

static void
dgc_emit_draw(nir_builder *b, struct dgc_cmdbuf *cs, nir_ssa_def *vertex_count)
{
   nir_ssa_def *values[3] = {nir_imm_int(b, PKT3(PKT3_DRAW_INDEX_AUTO, 1, false)), vertex_count,
                             nir_imm_int(b, V_0287F0_DI_SRC_SEL_AUTO_INDEX)};

   dgc_emit(b, cs, nir_vec(b, values, 3));
}

static void
build_dgc_buffer_tail(nir_builder *b, nir_ssa_def *sequence_count)
{
   nir_ssa_def *global_id = get_global_ids(b, 1);

   nir_ssa_def *cmd_buf_stride = load_param32(b, cmd_buf_stride);
   nir_ssa_def *cmd_buf_size = load_param32(b, cmd_buf_size);

   nir_push_if(b, nir_ieq_imm(b, global_id, 0));
   {
      nir_ssa_def *cmd_buf_tail_start = nir_imul(b, cmd_buf_stride, sequence_count);

      nir_variable *offset =
         nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "offset");
      nir_store_var(b, offset, cmd_buf_tail_start, 0x1);

      nir_ssa_def *dst_buf = radv_meta_load_descriptor(b, 0, DGC_DESC_PREPARE);
      nir_push_loop(b);
      {
         nir_ssa_def *curr_offset = nir_load_var(b, offset);
         const unsigned MAX_PACKET_WORDS = 0x3FFC;

         nir_push_if(b, nir_ieq(b, curr_offset, cmd_buf_size));
         {
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);

         nir_ssa_def *packet_size = nir_isub(b, cmd_buf_size, curr_offset);
         packet_size = nir_umin(b, packet_size, nir_imm_int(b, MAX_PACKET_WORDS * 4));

         nir_ssa_def *len = nir_ushr_imm(b, packet_size, 2);
         len = nir_iadd_imm(b, len, -2);
         nir_ssa_def *packet = nir_pkt3(b, PKT3_NOP, len);

         nir_store_ssbo(b, packet, dst_buf, curr_offset, .access = ACCESS_NON_READABLE);
         nir_store_var(b, offset, nir_iadd(b, curr_offset, packet_size), 0x1);
      }
      nir_pop_loop(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static nir_shader *
build_dgc_prepare_shader(struct radv_device *dev)
{
   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, "meta_dgc_prepare");
   b.shader->info.workgroup_size[0] = 64;

   nir_ssa_def *global_id = get_global_ids(&b, 1);

   nir_ssa_def *sequence_id = global_id;

   nir_ssa_def *cmd_buf_stride = load_param32(&b, cmd_buf_stride);
   nir_ssa_def *sequence_count = load_param32(&b, sequence_count);
   nir_ssa_def *stream_stride = load_param32(&b, stream_stride);

   nir_variable *count_var = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "sequence_count");
   nir_store_var(&b, count_var, sequence_count, 0x1);

   nir_push_if(&b, nir_ieq_imm(&b, sequence_count, UINT32_MAX));
   {
      nir_ssa_def *count_buf = radv_meta_load_descriptor(&b, 0, DGC_DESC_COUNT);
      nir_ssa_def *cnt = nir_load_ssbo(&b, 1, 32, count_buf, nir_imm_int(&b, 0));
      nir_store_var(&b, count_var, cnt, 0x1);
   }
   nir_pop_if(&b, NULL);

   sequence_count = nir_load_var(&b, count_var);

   nir_push_if(&b, nir_ult(&b, sequence_id, sequence_count));
   {
      struct dgc_cmdbuf cmd_buf = {
         .descriptor = radv_meta_load_descriptor(&b, 0, DGC_DESC_PREPARE),
         .offset = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "cmd_buf_offset"),
      };
      nir_store_var(&b, cmd_buf.offset, nir_imul(&b, global_id, cmd_buf_stride), 1);
      nir_ssa_def *cmd_buf_end = nir_iadd(&b, nir_load_var(&b, cmd_buf.offset), cmd_buf_stride);

      nir_ssa_def *stream_buf = radv_meta_load_descriptor(&b, 0, DGC_DESC_STREAM);
      nir_ssa_def *stream_base = nir_imul(&b, sequence_id, stream_stride);

      nir_variable *upload_offset =
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "upload_offset");
      nir_store_var(&b, upload_offset,
                    nir_iadd(&b, load_param32(&b, cmd_buf_size),
                             nir_imul(&b, load_param32(&b, upload_stride), sequence_id)),
                    0x1);

      nir_ssa_def *vbo_bind_mask = load_param32(&b, vbo_bind_mask);
      nir_ssa_def *vbo_cnt = load_param8(&b, vbo_cnt);
      nir_push_if(&b, nir_ine_imm(&b, vbo_bind_mask, 0));
      {
         nir_variable *vbo_idx =
            nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "vbo_idx");
         nir_store_var(&b, vbo_idx, nir_imm_int(&b, 0), 0x1);

         nir_push_loop(&b);
         {
            nir_push_if(&b, nir_uge(&b, nir_load_var(&b, vbo_idx), vbo_cnt));
            {
               nir_jump(&b, nir_jump_break);
            }
            nir_pop_if(&b, NULL);

            nir_ssa_def *vbo_offset = nir_imul_imm(&b, nir_load_var(&b, vbo_idx), 16);
            nir_variable *vbo_data =
               nir_variable_create(b.shader, nir_var_shader_temp, glsl_uvec4_type(), "vbo_data");

            nir_ssa_def *param_buf = radv_meta_load_descriptor(&b, 0, DGC_DESC_PARAMS);
            nir_store_var(&b, vbo_data,
                          nir_load_ssbo(&b, 4, 32, param_buf, vbo_offset), 0xf);

            nir_ssa_def *vbo_override =
               nir_ine_imm(&b,
                       nir_iand(&b, vbo_bind_mask,
                                nir_ishl(&b, nir_imm_int(&b, 1), nir_load_var(&b, vbo_idx))),
                       0);
            nir_push_if(&b, vbo_override);
            {
               nir_ssa_def *vbo_offset_offset =
                  nir_iadd(&b, nir_imul_imm(&b, vbo_cnt, 16),
                           nir_imul_imm(&b, nir_load_var(&b, vbo_idx), 8));
               nir_ssa_def *vbo_over_data =
                  nir_load_ssbo(&b, 2, 32, param_buf, vbo_offset_offset);
               nir_ssa_def *stream_offset = nir_iadd(
                  &b, stream_base, nir_iand_imm(&b, nir_channel(&b, vbo_over_data, 0), 0x7FFF));
               nir_ssa_def *stream_data =
                  nir_load_ssbo(&b, 4, 32, stream_buf, stream_offset);

               nir_ssa_def *va = nir_pack_64_2x32(&b, nir_channels(&b, stream_data, 0x3));
               nir_ssa_def *size = nir_channel(&b, stream_data, 2);
               nir_ssa_def *stride = nir_channel(&b, stream_data, 3);

               nir_ssa_def *dyn_stride = nir_test_mask(&b, nir_channel(&b, vbo_over_data, 0), DGC_DYNAMIC_STRIDE);
               nir_ssa_def *old_stride =
                  nir_ubfe_imm(&b, nir_channel(&b, nir_load_var(&b, vbo_data), 1), 16, 14);
               stride = nir_bcsel(&b, dyn_stride, stride, old_stride);

               nir_ssa_def *use_per_attribute_vb_descs =
                  nir_test_mask(&b, nir_channel(&b, vbo_over_data, 0), 1u << 31);
               nir_variable *num_records = nir_variable_create(b.shader, nir_var_shader_temp,
                                                               glsl_uint_type(), "num_records");
               nir_store_var(&b, num_records, size, 0x1);

               nir_push_if(&b, use_per_attribute_vb_descs);
               {
                  nir_ssa_def *attrib_end = nir_ubfe_imm(&b, nir_channel(&b, vbo_over_data, 1), 16,
                                                         16);
                  nir_ssa_def *attrib_index_offset =
                     nir_ubfe_imm(&b, nir_channel(&b, vbo_over_data, 1), 0, 16);

                  nir_push_if(&b, nir_ult(&b, nir_load_var(&b, num_records), attrib_end));
                  {
                     nir_store_var(&b, num_records, nir_imm_int(&b, 0), 0x1);
                  }
                  nir_push_else(&b, NULL);
                  nir_push_if(&b, nir_ieq_imm(&b, stride, 0));
                  {
                     nir_store_var(&b, num_records, nir_imm_int(&b, 1), 0x1);
                  }
                  nir_push_else(&b, NULL);
                  {
                     nir_ssa_def *r = nir_iadd(
                        &b,
                        nir_iadd_imm(
                           &b,
                           nir_udiv(&b, nir_isub(&b, nir_load_var(&b, num_records), attrib_end),
                                    stride),
                           1),
                        attrib_index_offset);
                     nir_store_var(&b, num_records, r, 0x1);
                  }
                  nir_pop_if(&b, NULL);
                  nir_pop_if(&b, NULL);

                  nir_ssa_def *convert_cond =
                     nir_ine_imm(&b, nir_load_var(&b, num_records), 0);
                  if (dev->physical_device->rad_info.gfx_level == GFX9)
                     convert_cond = nir_imm_bool(&b, false);
                  else if (dev->physical_device->rad_info.gfx_level != GFX8)
                     convert_cond =
                        nir_iand(&b, convert_cond, nir_ieq_imm(&b, stride, 0));

                  nir_ssa_def *new_records = nir_iadd(
                     &b, nir_imul(&b, nir_iadd_imm(&b, nir_load_var(&b, num_records), -1), stride),
                     attrib_end);
                  new_records =
                     nir_bcsel(&b, convert_cond, new_records, nir_load_var(&b, num_records));
                  nir_store_var(&b, num_records, new_records, 0x1);
               }
               nir_push_else(&b, NULL);
               {
                  if (dev->physical_device->rad_info.gfx_level != GFX8) {
                     nir_push_if(&b, nir_ine_imm(&b, stride, 0));
                     {
                        nir_ssa_def *r = nir_iadd(&b, nir_load_var(&b, num_records),
                                                  nir_iadd_imm(&b, stride, -1));
                        nir_store_var(&b, num_records, nir_udiv(&b, r, stride), 0x1);
                     }
                     nir_pop_if(&b, NULL);
                  }
               }
               nir_pop_if(&b, NULL);

               nir_ssa_def *rsrc_word3 = nir_channel(&b, nir_load_var(&b, vbo_data), 3);
               if (dev->physical_device->rad_info.gfx_level >= GFX10) {
                  nir_ssa_def *oob_select = nir_bcsel(
                     &b, nir_ieq_imm(&b, stride, 0), nir_imm_int(&b, V_008F0C_OOB_SELECT_RAW),
                     nir_imm_int(&b, V_008F0C_OOB_SELECT_STRUCTURED));
                  rsrc_word3 = nir_iand_imm(&b, rsrc_word3, C_008F0C_OOB_SELECT);
                  rsrc_word3 = nir_ior(&b, rsrc_word3, nir_ishl_imm(&b, oob_select, 28));
               }

               nir_ssa_def *va_hi = nir_iand_imm(&b, nir_unpack_64_2x32_split_y(&b, va), 0xFFFF);
               stride = nir_iand_imm(&b, stride, 0x3FFF);
               nir_ssa_def *new_vbo_data[4] = {nir_unpack_64_2x32_split_x(&b, va),
                                               nir_ior(&b, nir_ishl_imm(&b, stride, 16), va_hi),
                                               nir_load_var(&b, num_records), rsrc_word3};
               nir_store_var(&b, vbo_data, nir_vec(&b, new_vbo_data, 4), 0xf);
            }
            nir_pop_if(&b, NULL);

            /* On GFX9, it seems bounds checking is disabled if both
             * num_records and stride are zero. This doesn't seem necessary on GFX8, GFX10 and
             * GFX10.3 but it doesn't hurt.
             */
            nir_ssa_def *num_records = nir_channel(&b, nir_load_var(&b, vbo_data), 2);
            nir_ssa_def *buf_va = nir_iand_imm(
               &b, nir_pack_64_2x32(&b, nir_channels(&b, nir_load_var(&b, vbo_data), 0x3)),
               (1ull << 48) - 1ull);
            nir_push_if(&b,
                        nir_ior(&b, nir_ieq_imm(&b, num_records, 0), nir_ieq_imm(&b, buf_va, 0)));
            {
               nir_ssa_def *new_vbo_data[4] = {nir_imm_int(&b, 0), nir_imm_int(&b, 0),
                                               nir_imm_int(&b, 0), nir_imm_int(&b, 0)};
               nir_store_var(&b, vbo_data, nir_vec(&b, new_vbo_data, 4), 0xf);
            }
            nir_pop_if(&b, NULL);

            nir_ssa_def *upload_off = nir_iadd(&b, nir_load_var(&b, upload_offset), vbo_offset);
            nir_store_ssbo(&b, nir_load_var(&b, vbo_data), cmd_buf.descriptor, upload_off, .access = ACCESS_NON_READABLE);
            nir_store_var(&b, vbo_idx, nir_iadd_imm(&b, nir_load_var(&b, vbo_idx), 1), 0x1);
         }
         nir_pop_loop(&b, NULL);
         nir_ssa_def *packet[3] = {
            nir_imm_int(&b, PKT3(PKT3_SET_SH_REG, 1, 0)), load_param16(&b, vbo_reg),
            nir_iadd(&b, load_param32(&b, upload_addr), nir_load_var(&b, upload_offset))};

         dgc_emit(&b, &cmd_buf, nir_vec(&b, packet, 3));

         nir_store_var(&b, upload_offset,
                       nir_iadd(&b, nir_load_var(&b, upload_offset), nir_imul_imm(&b, vbo_cnt, 16)),
                       0x1);
      }
      nir_pop_if(&b, NULL);


      nir_ssa_def *push_const_mask = load_param64(&b, push_constant_mask);
      nir_push_if(&b, nir_ine_imm(&b, push_const_mask, 0));
      {
         nir_ssa_def *const_copy = nir_ine_imm(&b, load_param8(&b, const_copy), 0);
         nir_ssa_def *const_copy_size = load_param16(&b, const_copy_size);
         nir_ssa_def *const_copy_words = nir_ushr_imm(&b, const_copy_size, 2);
         const_copy_words = nir_bcsel(&b, const_copy, const_copy_words, nir_imm_int(&b, 0));

         nir_variable *idx =
            nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "const_copy_idx");
         nir_store_var(&b, idx, nir_imm_int(&b, 0), 0x1);

         nir_ssa_def *param_buf = radv_meta_load_descriptor(&b, 0, DGC_DESC_PARAMS);
         nir_ssa_def *param_offset = nir_imul_imm(&b, vbo_cnt, 24);
         nir_ssa_def *param_offset_offset = nir_iadd_imm(&b, param_offset, MESA_VULKAN_SHADER_STAGES * 12);
         nir_ssa_def *param_const_offset = nir_iadd_imm(&b, param_offset, MAX_PUSH_CONSTANTS_SIZE + MESA_VULKAN_SHADER_STAGES * 12);
         nir_push_loop(&b);
         {
            nir_ssa_def *cur_idx = nir_load_var(&b, idx);
            nir_push_if(&b, nir_uge(&b, cur_idx, const_copy_words));
            {
               nir_jump(&b, nir_jump_break);
            }
            nir_pop_if(&b, NULL);

            nir_variable *data = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "copy_data");

            nir_ssa_def *update = nir_iand(&b, push_const_mask, nir_ishl(&b, nir_imm_int64(&b, 1), cur_idx));
            update = nir_bcsel(
               &b, nir_ult(&b, cur_idx, nir_imm_int(&b, 64 /* bits in push_const_mask */)), update,
               nir_imm_int64(&b, 0));

            nir_push_if(&b, nir_ine_imm(&b, update, 0));
            {
               nir_ssa_def *stream_offset = nir_load_ssbo(
                  &b, 1, 32, param_buf,
                  nir_iadd(&b, param_offset_offset, nir_ishl_imm(&b, cur_idx, 2)));
               nir_ssa_def *new_data = nir_load_ssbo(&b, 1, 32, stream_buf, nir_iadd(&b, stream_base, stream_offset));
               nir_store_var(&b, data, new_data, 0x1);
            }
            nir_push_else(&b, NULL);
            {
               nir_store_var(
                  &b, data,
                  nir_load_ssbo(&b, 1, 32, param_buf,
                                nir_iadd(&b, param_const_offset, nir_ishl_imm(&b, cur_idx, 2))),
                  0x1);
            }
            nir_pop_if(&b, NULL);

            nir_store_ssbo(
               &b, nir_load_var(&b, data), cmd_buf.descriptor,
               nir_iadd(&b, nir_load_var(&b, upload_offset), nir_ishl_imm(&b, cur_idx, 2)),
               .access = ACCESS_NON_READABLE);

            nir_store_var(&b, idx, nir_iadd_imm(&b, cur_idx, 1), 0x1);
         }
         nir_pop_loop(&b, NULL);

         nir_variable *shader_idx =
            nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "shader_idx");
         nir_store_var(&b, shader_idx, nir_imm_int(&b, 0), 0x1);
         nir_ssa_def *shader_cnt = load_param16(&b, push_constant_shader_cnt);

         nir_push_loop(&b);
         {
            nir_ssa_def *cur_shader_idx = nir_load_var(&b, shader_idx);
            nir_push_if(&b, nir_uge(&b, cur_shader_idx, shader_cnt));
            {
               nir_jump(&b, nir_jump_break);
            }
            nir_pop_if(&b, NULL);

            nir_ssa_def *reg_info = nir_load_ssbo(&b, 3, 32, param_buf, nir_iadd(&b, param_offset, nir_imul_imm(&b, cur_shader_idx, 12)));
            nir_ssa_def *upload_sgpr = nir_ubfe_imm(&b, nir_channel(&b, reg_info, 0), 0, 16);
            nir_ssa_def *inline_sgpr = nir_ubfe_imm(&b, nir_channel(&b, reg_info, 0), 16, 16);
            nir_ssa_def *inline_mask = nir_pack_64_2x32(&b, nir_channels(&b, reg_info, 0x6));

            nir_push_if(&b, nir_ine_imm(&b, upload_sgpr, 0));
            {
               nir_ssa_def *pkt[3] = {
                  nir_imm_int(&b, PKT3(PKT3_SET_SH_REG, 1, 0)),
                  upload_sgpr,
                  nir_iadd(&b, load_param32(&b, upload_addr), nir_load_var(&b, upload_offset))
               };

               dgc_emit(&b, &cmd_buf, nir_vec(&b, pkt, 3));
            }
            nir_pop_if(&b, NULL);

            nir_push_if(&b, nir_ine_imm(&b, inline_sgpr, 0));
            {
               nir_ssa_def *inline_len = nir_bit_count(&b, inline_mask);
               nir_store_var(&b, idx, nir_imm_int(&b, 0), 0x1);

               nir_ssa_def *pkt[2] = {
                  nir_pkt3(&b, PKT3_SET_SH_REG, inline_len),
                  inline_sgpr
               };

               dgc_emit(&b, &cmd_buf, nir_vec(&b, pkt, 2));

               nir_push_loop(&b);
               {
                  nir_ssa_def *cur_idx = nir_load_var(&b, idx);
                  nir_push_if(&b,
                              nir_uge(&b, cur_idx, nir_imm_int(&b, 64 /* bits in inline_mask */)));
                  {
                     nir_jump(&b, nir_jump_break);
                  }
                  nir_pop_if(&b, NULL);

                  nir_ssa_def *l = nir_ishl(&b, nir_imm_int64(&b, 1), cur_idx);
                  nir_push_if(&b, nir_ieq_imm(&b, nir_iand(&b, l, inline_mask), 0));
                  {
                     nir_store_var(&b, idx, nir_iadd_imm(&b, cur_idx, 1), 0x1);
                     nir_jump(&b, nir_jump_continue);
                  }
                  nir_pop_if(&b, NULL);

                  nir_variable *data = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "copy_data");

                  nir_ssa_def *update = nir_iand(&b, push_const_mask, nir_ishl(&b, nir_imm_int64(&b, 1), cur_idx));
                  update = nir_bcsel(
                     &b, nir_ult(&b, cur_idx, nir_imm_int(&b, 64 /* bits in push_const_mask */)),
                     update, nir_imm_int64(&b, 0));

                  nir_push_if(&b, nir_ine_imm(&b, update, 0));
                  {
                     nir_ssa_def *stream_offset = nir_load_ssbo(
                        &b, 1, 32, param_buf,
                        nir_iadd(&b, param_offset_offset, nir_ishl_imm(&b, cur_idx, 2)));
                     nir_ssa_def *new_data = nir_load_ssbo(&b, 1, 32, stream_buf, nir_iadd(&b, stream_base, stream_offset));
                     nir_store_var(&b, data, new_data, 0x1);
                  }
                  nir_push_else(&b, NULL);
                  {
                     nir_store_var(&b, data,
                                   nir_load_ssbo(&b, 1, 32, param_buf,
                                                 nir_iadd(&b, param_const_offset,
                                                          nir_ishl_imm(&b, cur_idx, 2))),
                                   0x1);
                  }
                  nir_pop_if(&b, NULL);

                  dgc_emit(&b, &cmd_buf, nir_load_var(&b, data));

                  nir_store_var(&b, idx, nir_iadd_imm(&b, cur_idx, 1), 0x1);
               }
               nir_pop_loop(&b, NULL);
            }
            nir_pop_if(&b, NULL);
            nir_store_var(&b, shader_idx, nir_iadd_imm(&b, cur_shader_idx, 1), 0x1);
         }
         nir_pop_loop(&b, NULL);
      }
      nir_pop_if(&b, 0);

      nir_push_if(&b, nir_ieq_imm(&b, load_param16(&b, emit_state), 1));
      {
         nir_ssa_def *stream_offset = nir_iadd(&b, load_param16(&b, state_offset), stream_base);
         nir_ssa_def *state = nir_load_ssbo(&b, 1, 32, stream_buf, stream_offset);
         state = nir_iand_imm(&b, state, 1);

         nir_ssa_def *reg =
            nir_ior(&b, load_param32(&b, pa_su_sc_mode_cntl_base), nir_ishl_imm(&b, state, 2));

         nir_ssa_def *cmd_values[3] = {
            nir_imm_int(&b, PKT3(PKT3_SET_CONTEXT_REG, 1, 0)),
            nir_imm_int(&b, (R_028814_PA_SU_SC_MODE_CNTL - SI_CONTEXT_REG_OFFSET) >> 2), reg};

         dgc_emit(&b, &cmd_buf, nir_vec(&b, cmd_values, 3));
      }
      nir_pop_if(&b, NULL);

      nir_ssa_def *scissor_count = load_param16(&b, scissor_count);
      nir_push_if(&b, nir_ine_imm(&b, scissor_count, 0));
      {
         nir_ssa_def *scissor_offset = load_param16(&b, scissor_offset);
         nir_variable *idx = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(),
                                                 "scissor_copy_idx");
         nir_store_var(&b, idx, nir_imm_int(&b, 0), 1);

         nir_push_loop(&b);
         {
            nir_ssa_def *cur_idx = nir_load_var(&b, idx);
            nir_push_if(&b, nir_uge(&b, cur_idx, scissor_count));
            {
               nir_jump(&b, nir_jump_break);
            }
            nir_pop_if(&b, NULL);

            nir_ssa_def *param_buf = radv_meta_load_descriptor(&b, 0, DGC_DESC_PARAMS);
            nir_ssa_def *param_offset = nir_iadd(&b, scissor_offset, nir_imul_imm(&b, cur_idx, 4));
            nir_ssa_def *value = nir_load_ssbo(&b, 1, 32, param_buf, param_offset);

            dgc_emit(&b, &cmd_buf, value);

            nir_store_var(&b, idx, nir_iadd_imm(&b, cur_idx, 1), 1);
         }
         nir_pop_loop(&b, NULL);
      }
      nir_pop_if(&b, NULL);

      nir_push_if(&b, nir_ieq_imm(&b, load_param16(&b, draw_indexed), 0));
      {
         nir_ssa_def *vtx_base_sgpr = load_param16(&b, vtx_base_sgpr);
         nir_ssa_def *stream_offset =
            nir_iadd(&b, load_param16(&b, draw_params_offset), stream_base);

         nir_ssa_def *draw_data0 =
            nir_load_ssbo(&b, 4, 32, stream_buf, stream_offset);
         nir_ssa_def *vertex_count = nir_channel(&b, draw_data0, 0);
         nir_ssa_def *instance_count = nir_channel(&b, draw_data0, 1);
         nir_ssa_def *vertex_offset = nir_channel(&b, draw_data0, 2);
         nir_ssa_def *first_instance = nir_channel(&b, draw_data0, 3);

         nir_push_if(&b, nir_iand(&b, nir_ine_imm(&b, vertex_count, 0), nir_ine_imm(&b, instance_count, 0)));
         {
            dgc_emit_userdata_vertex(&b, &cmd_buf, vtx_base_sgpr, vertex_offset, first_instance, sequence_id);
            dgc_emit_instance_count(&b, &cmd_buf, instance_count);
            dgc_emit_draw(&b, &cmd_buf, vertex_count);
         }
         nir_pop_if(&b, 0);
      }
      nir_push_else(&b, NULL);
      {
         nir_variable *index_size_var =
            nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "index_size");
         nir_store_var(&b, index_size_var, load_param16(&b, base_index_size), 0x1);
         nir_variable *max_index_count_var =
            nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "max_index_count");
         nir_store_var(&b, max_index_count_var, load_param32(&b, max_index_count), 0x1);

         nir_ssa_def *bind_index_buffer = nir_ieq_imm(&b, nir_load_var(&b, index_size_var), 0);
         nir_push_if(&b, bind_index_buffer);
         {
            nir_ssa_def *index_stream_offset =
               nir_iadd(&b, load_param16(&b, index_buffer_offset), stream_base);
            nir_ssa_def *data =
               nir_load_ssbo(&b, 4, 32, stream_buf, index_stream_offset);

            nir_ssa_def *vk_index_type = nir_channel(&b, data, 3);
            nir_ssa_def *index_type = nir_bcsel(
               &b, nir_ieq(&b, vk_index_type, load_param32(&b, ibo_type_32)),
               nir_imm_int(&b, V_028A7C_VGT_INDEX_32), nir_imm_int(&b, V_028A7C_VGT_INDEX_16));
            index_type = nir_bcsel(&b, nir_ieq(&b, vk_index_type, load_param32(&b, ibo_type_8)),
                                   nir_imm_int(&b, V_028A7C_VGT_INDEX_8), index_type);

            nir_ssa_def *index_size = nir_iand_imm(
               &b, nir_ushr(&b, nir_imm_int(&b, 0x142), nir_imul_imm(&b, index_type, 4)), 0xf);
            nir_store_var(&b, index_size_var, index_size, 0x1);

            nir_ssa_def *max_index_count = nir_udiv(&b, nir_channel(&b, data, 2), index_size);
            nir_store_var(&b, max_index_count_var, max_index_count, 0x1);

            nir_ssa_def *cmd_values[3 + 2 + 3];

            if (dev->physical_device->rad_info.gfx_level >= GFX9) {
               unsigned opcode = PKT3_SET_UCONFIG_REG_INDEX;
               if (dev->physical_device->rad_info.gfx_level < GFX9 ||
                   (dev->physical_device->rad_info.gfx_level == GFX9 &&
                    dev->physical_device->rad_info.me_fw_version < 26))
                  opcode = PKT3_SET_UCONFIG_REG;
               cmd_values[0] = nir_imm_int(&b, PKT3(opcode, 1, 0));
               cmd_values[1] = nir_imm_int(
                  &b, (R_03090C_VGT_INDEX_TYPE - CIK_UCONFIG_REG_OFFSET) >> 2 | (2u << 28));
               cmd_values[2] = index_type;
            } else {
               cmd_values[0] = nir_imm_int(&b, PKT3(PKT3_INDEX_TYPE, 0, 0));
               cmd_values[1] = index_type;
               cmd_values[2] = nir_imm_int(&b, PKT3_NOP_PAD);
            }

            nir_ssa_def *addr_upper = nir_channel(&b, data, 1);
            addr_upper = nir_ishr_imm(&b, nir_ishl_imm(&b, addr_upper, 16), 16);

            cmd_values[3] = nir_imm_int(&b, PKT3(PKT3_INDEX_BASE, 1, 0));
            cmd_values[4] = nir_channel(&b, data, 0);
            cmd_values[5] = addr_upper;
            cmd_values[6] = nir_imm_int(&b, PKT3(PKT3_INDEX_BUFFER_SIZE, 0, 0));
            cmd_values[7] = max_index_count;

            dgc_emit(&b, &cmd_buf, nir_vec(&b, cmd_values, 8));
         }
         nir_pop_if(&b, NULL);

         nir_ssa_def *index_size = nir_load_var(&b, index_size_var);
         nir_ssa_def *max_index_count = nir_load_var(&b, max_index_count_var);
         nir_ssa_def *vtx_base_sgpr = load_param16(&b, vtx_base_sgpr);
         nir_ssa_def *stream_offset =
            nir_iadd(&b, load_param16(&b, draw_params_offset), stream_base);

         index_size =
            nir_bcsel(&b, bind_index_buffer, nir_load_var(&b, index_size_var), index_size);
         max_index_count = nir_bcsel(&b, bind_index_buffer, nir_load_var(&b, max_index_count_var),
                                     max_index_count);
         nir_ssa_def *draw_data0 =
            nir_load_ssbo(&b, 4, 32, stream_buf, stream_offset);
         nir_ssa_def *draw_data1 = nir_load_ssbo(
            &b, 1, 32, stream_buf, nir_iadd_imm(&b, stream_offset, 16));
         nir_ssa_def *index_count = nir_channel(&b, draw_data0, 0);
         nir_ssa_def *instance_count = nir_channel(&b, draw_data0, 1);
         nir_ssa_def *first_index = nir_channel(&b, draw_data0, 2);
         nir_ssa_def *vertex_offset = nir_channel(&b, draw_data0, 3);
         nir_ssa_def *first_instance = nir_channel(&b, draw_data1, 0);

         nir_push_if(&b, nir_iand(&b, nir_ine_imm(&b, index_count, 0), nir_ine_imm(&b, instance_count, 0)));
         {
            dgc_emit_userdata_vertex(&b, &cmd_buf, vtx_base_sgpr, vertex_offset, first_instance, sequence_id);
            dgc_emit_instance_count(&b, &cmd_buf, instance_count);
            dgc_emit_draw_indexed(&b, &cmd_buf, first_index, index_count,
                                       max_index_count);
         }
         nir_pop_if(&b, 0);
      }
      nir_pop_if(&b, NULL);

      /* Pad the cmdbuffer if we did not use the whole stride */
      nir_push_if(&b, nir_ine(&b, nir_load_var(&b, cmd_buf.offset), cmd_buf_end));
      {
         nir_ssa_def *cnt = nir_isub(&b, cmd_buf_end, nir_load_var(&b, cmd_buf.offset));
         cnt = nir_ushr_imm(&b, cnt, 2);
         cnt = nir_iadd_imm(&b, cnt, -2);
         nir_ssa_def *pkt = nir_pkt3(&b, PKT3_NOP, cnt);

         dgc_emit(&b, &cmd_buf, pkt);
      }
      nir_pop_if(&b, NULL);
   }
   nir_pop_if(&b, NULL);

   build_dgc_buffer_tail(&b, sequence_count);
   return b.shader;
}

void
radv_device_finish_dgc_prepare_state(struct radv_device *device)
{
   radv_DestroyPipeline(radv_device_to_handle(device), device->meta_state.dgc_prepare.pipeline,
                        &device->meta_state.alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              device->meta_state.dgc_prepare.p_layout, &device->meta_state.alloc);
   device->vk.dispatch_table.DestroyDescriptorSetLayout(radv_device_to_handle(device),
                                                        device->meta_state.dgc_prepare.ds_layout,
                                                        &device->meta_state.alloc);
}

VkResult
radv_device_init_dgc_prepare_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *cs = build_dgc_prepare_shader(device);

   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = DGC_NUM_DESCS,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = DGC_DESC_STREAM,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = DGC_DESC_PREPARE,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = DGC_DESC_PARAMS,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = DGC_DESC_COUNT,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &ds_create_info,
                                           &device->meta_state.alloc,
                                           &device->meta_state.dgc_prepare.ds_layout);
   if (result != VK_SUCCESS)
      goto cleanup;

   const VkPipelineLayoutCreateInfo leaf_pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.dgc_prepare.ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
         &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(struct radv_dgc_params)},
   };

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &leaf_pl_create_info,
                                      &device->meta_state.alloc,
                                      &device->meta_state.dgc_prepare.p_layout);
   if (result != VK_SUCCESS)
      goto cleanup;

   VkPipelineShaderStageCreateInfo shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = shader_stage,
      .flags = 0,
      .layout = device->meta_state.dgc_prepare.p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &pipeline_info, &device->meta_state.alloc, &device->meta_state.dgc_prepare.pipeline);
   if (result != VK_SUCCESS)
      goto cleanup;

cleanup:
   ralloc_free(cs);
   return result;
}

VkResult
radv_CreateIndirectCommandsLayoutNV(VkDevice _device,
                                    const VkIndirectCommandsLayoutCreateInfoNV *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    VkIndirectCommandsLayoutNV *pIndirectCommandsLayout)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_indirect_command_layout *layout;

   size_t size =
      sizeof(*layout) + pCreateInfo->tokenCount * sizeof(VkIndirectCommandsLayoutTokenNV);

   layout =
      vk_zalloc2(&device->vk.alloc, pAllocator, size, alignof(struct radv_indirect_command_layout),
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!layout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &layout->base, VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV);

   layout->input_stride = pCreateInfo->pStreamStrides[0];
   layout->token_count = pCreateInfo->tokenCount;
   typed_memcpy(layout->tokens, pCreateInfo->pTokens, pCreateInfo->tokenCount);

   layout->ibo_type_32 = VK_INDEX_TYPE_UINT32;
   layout->ibo_type_8 = VK_INDEX_TYPE_UINT8_EXT;

   for (unsigned i = 0; i < pCreateInfo->tokenCount; ++i) {
      switch (pCreateInfo->pTokens[i].tokenType) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV:
         layout->draw_params_offset = pCreateInfo->pTokens[i].offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV:
         layout->indexed = true;
         layout->draw_params_offset = pCreateInfo->pTokens[i].offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV:
         layout->binds_index_buffer = true;
         layout->index_buffer_offset = pCreateInfo->pTokens[i].offset;
         /* 16-bit is implied if we find no match. */
         for (unsigned j = 0; j < pCreateInfo->pTokens[i].indexTypeCount; j++) {
            if (pCreateInfo->pTokens[i].pIndexTypes[j] == VK_INDEX_TYPE_UINT32)
               layout->ibo_type_32 = pCreateInfo->pTokens[i].pIndexTypeValues[j];
            else if (pCreateInfo->pTokens[i].pIndexTypes[j] == VK_INDEX_TYPE_UINT8_EXT)
               layout->ibo_type_8 = pCreateInfo->pTokens[i].pIndexTypeValues[j];
         }
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV:
         layout->bind_vbo_mask |= 1u << pCreateInfo->pTokens[i].vertexBindingUnit;
         layout->vbo_offsets[pCreateInfo->pTokens[i].vertexBindingUnit] =
            pCreateInfo->pTokens[i].offset;
         if (pCreateInfo->pTokens[i].vertexDynamicStride)
            layout->vbo_offsets[pCreateInfo->pTokens[i].vertexBindingUnit] |= DGC_DYNAMIC_STRIDE;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV:
         for (unsigned j = pCreateInfo->pTokens[i].pushconstantOffset / 4, k = 0;
              k < pCreateInfo->pTokens[i].pushconstantSize / 4; ++j, ++k) {
            layout->push_constant_mask |= 1ull << j;
            layout->push_constant_offsets[j] = pCreateInfo->pTokens[i].offset + k * 4;
         }
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_STATE_FLAGS_NV:
         layout->binds_state = true;
         layout->state_offset = pCreateInfo->pTokens[i].offset;
         break;
      default:
         unreachable("Unhandled token type");
      }
   }
   if (!layout->indexed)
      layout->binds_index_buffer = false;

   *pIndirectCommandsLayout = radv_indirect_command_layout_to_handle(layout);
   return VK_SUCCESS;
}

void
radv_DestroyIndirectCommandsLayoutNV(VkDevice _device,
                                     VkIndirectCommandsLayoutNV indirectCommandsLayout,
                                     const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, indirectCommandsLayout);

   if (!layout)
      return;

   vk_object_base_finish(&layout->base);
   vk_free2(&device->vk.alloc, pAllocator, layout);
}

void
radv_GetGeneratedCommandsMemoryRequirementsNV(
   VkDevice _device, const VkGeneratedCommandsMemoryRequirementsInfoNV *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pInfo->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, pInfo->pipeline);
   struct radv_graphics_pipeline *graphics_pipeline = radv_pipeline_to_graphics(pipeline);

   uint32_t cmd_stride, upload_stride;
   radv_get_sequence_size(layout, graphics_pipeline, &cmd_stride, &upload_stride);

   VkDeviceSize cmd_buf_size = radv_align_cmdbuf_size(cmd_stride * pInfo->maxSequencesCount);
   VkDeviceSize upload_buf_size = upload_stride * pInfo->maxSequencesCount;

   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      device->physical_device->memory_types_32bit;
   pMemoryRequirements->memoryRequirements.alignment = 256;
   pMemoryRequirements->memoryRequirements.size =
      align(cmd_buf_size + upload_buf_size, pMemoryRequirements->memoryRequirements.alignment);
}

void
radv_CmdPreprocessGeneratedCommandsNV(VkCommandBuffer commandBuffer,
                                      const VkGeneratedCommandsInfoNV *pGeneratedCommandsInfo)
{
   /* Can't do anything here as we depend on some dynamic state in some cases that we only know
    * at draw time. */
}

/* Always need to call this directly before draw due to dependence on bound state. */
void
radv_prepare_dgc(struct radv_cmd_buffer *cmd_buffer,
                 const VkGeneratedCommandsInfoNV *pGeneratedCommandsInfo)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout,
                  pGeneratedCommandsInfo->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, pGeneratedCommandsInfo->pipeline);
   VK_FROM_HANDLE(radv_buffer, prep_buffer, pGeneratedCommandsInfo->preprocessBuffer);
   struct radv_graphics_pipeline *graphics_pipeline = radv_pipeline_to_graphics(pipeline);
   struct radv_meta_saved_state saved_state;
   struct radv_buffer token_buffer;

   uint32_t cmd_stride, upload_stride;
   radv_get_sequence_size(layout, graphics_pipeline, &cmd_stride, &upload_stride);

   unsigned cmd_buf_size =
      radv_align_cmdbuf_size(cmd_stride * pGeneratedCommandsInfo->sequencesCount);

   unsigned vb_size = layout->bind_vbo_mask ? util_bitcount(graphics_pipeline->vb_desc_usage_mask) * 24 : 0;
   unsigned const_size = graphics_pipeline->base.push_constant_size +
                         16 * graphics_pipeline->base.dynamic_offset_count +
                         sizeof(layout->push_constant_offsets) + ARRAY_SIZE(graphics_pipeline->base.shaders) * 12;
   if (!layout->push_constant_mask)
      const_size = 0;

   unsigned scissor_size = (8 + 2 * cmd_buffer->state.dynamic.scissor.count) * 4;
   if (!layout->binds_state || !cmd_buffer->state.dynamic.scissor.count ||
       !cmd_buffer->device->physical_device->rad_info.has_gfx9_scissor_bug)
      scissor_size = 0;

   unsigned upload_size = MAX2(vb_size + const_size + scissor_size, 16);

   void *upload_data;
   unsigned upload_offset;
   if (!radv_cmd_buffer_upload_alloc(cmd_buffer, upload_size, &upload_offset, &upload_data)) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   void *upload_data_base = upload_data;

   radv_buffer_init(&token_buffer, cmd_buffer->device, cmd_buffer->upload.upload_bo, upload_size,
                    upload_offset);

   uint64_t upload_addr = radv_buffer_get_va(prep_buffer->bo) + prep_buffer->offset +
                          pGeneratedCommandsInfo->preprocessOffset;

   uint16_t vtx_base_sgpr =
      (cmd_buffer->state.graphics_pipeline->vtx_base_sgpr - SI_SH_REG_OFFSET) >> 2;
   if (cmd_buffer->state.graphics_pipeline->uses_drawid)
      vtx_base_sgpr |= DGC_USES_DRAWID;
   if (cmd_buffer->state.graphics_pipeline->uses_baseinstance)
      vtx_base_sgpr |= DGC_USES_BASEINSTANCE;

   uint16_t vbo_sgpr =
      ((radv_lookup_user_sgpr(&graphics_pipeline->base, MESA_SHADER_VERTEX, AC_UD_VS_VERTEX_BUFFERS)->sgpr_idx * 4 +
        graphics_pipeline->base.user_data_0[MESA_SHADER_VERTEX]) -
       SI_SH_REG_OFFSET) >>
      2;
   struct radv_dgc_params params = {
      .cmd_buf_stride = cmd_stride,
      .cmd_buf_size = cmd_buf_size,
      .upload_addr = (uint32_t)upload_addr,
      .upload_stride = upload_stride,
      .sequence_count = pGeneratedCommandsInfo->sequencesCount,
      .stream_stride = layout->input_stride,
      .draw_indexed = layout->indexed,
      .draw_params_offset = layout->draw_params_offset,
      .base_index_size =
         layout->binds_index_buffer ? 0 : radv_get_vgt_index_size(cmd_buffer->state.index_type),
      .vtx_base_sgpr = vtx_base_sgpr,
      .max_index_count = cmd_buffer->state.max_index_count,
      .index_buffer_offset = layout->index_buffer_offset,
      .vbo_reg = vbo_sgpr,
      .ibo_type_32 = layout->ibo_type_32,
      .ibo_type_8 = layout->ibo_type_8,
      .emit_state = layout->binds_state,
      .pa_su_sc_mode_cntl_base = radv_get_pa_su_sc_mode_cntl(cmd_buffer) & C_028814_FACE,
      .state_offset = layout->state_offset,
   };

   if (layout->bind_vbo_mask) {
      radv_write_vertex_descriptors(cmd_buffer, graphics_pipeline, true, upload_data);

      uint32_t *vbo_info = (uint32_t *)((char *)upload_data + graphics_pipeline->vb_desc_alloc_size);

      uint32_t mask = graphics_pipeline->vb_desc_usage_mask;
      unsigned idx = 0;
      while (mask) {
         unsigned i = u_bit_scan(&mask);
         unsigned binding =
            graphics_pipeline->use_per_attribute_vb_descs ? graphics_pipeline->attrib_bindings[i] : i;
         uint32_t attrib_end = graphics_pipeline->attrib_ends[i];

         params.vbo_bind_mask |= ((layout->bind_vbo_mask >> binding) & 1u) << idx;
         vbo_info[2 * idx] = ((graphics_pipeline->use_per_attribute_vb_descs ? 1u : 0u) << 31) |
                             layout->vbo_offsets[binding];
         vbo_info[2 * idx + 1] = graphics_pipeline->attrib_index_offset[i] | (attrib_end << 16);
         ++idx;
      }
      params.vbo_cnt = idx;
      upload_data = (char *)upload_data + vb_size;
   }

   if (layout->push_constant_mask) {
      uint32_t *desc = upload_data;
      upload_data = (char *)upload_data + ARRAY_SIZE(graphics_pipeline->base.shaders) * 12;

      unsigned idx = 0;
      for (unsigned i = 0; i < ARRAY_SIZE(graphics_pipeline->base.shaders); ++i) {
         if (!graphics_pipeline->base.shaders[i])
            continue;

         struct radv_userdata_locations *locs = &graphics_pipeline->base.shaders[i]->info.user_sgprs_locs;
         if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0)
            params.const_copy = 1;

         if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0 ||
             locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0) {
            unsigned upload_sgpr = 0;
            unsigned inline_sgpr = 0;

            if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0) {
               upload_sgpr =
                  (graphics_pipeline->base.user_data_0[i] + 4 * locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx -
                   SI_SH_REG_OFFSET) >>
                  2;
            }

            if (locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0) {
               inline_sgpr = (graphics_pipeline->base.user_data_0[i] +
                              4 * locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx -
                              SI_SH_REG_OFFSET) >>
                             2;
               desc[idx * 3 + 1] = graphics_pipeline->base.shaders[i]->info.inline_push_constant_mask;
               desc[idx * 3 + 2] = graphics_pipeline->base.shaders[i]->info.inline_push_constant_mask >> 32;
            }
            desc[idx * 3] = upload_sgpr | (inline_sgpr << 16);
            ++idx;
         }
      }

      params.push_constant_shader_cnt = idx;

      params.const_copy_size = graphics_pipeline->base.push_constant_size +
                               16 * graphics_pipeline->base.dynamic_offset_count;
      params.push_constant_mask = layout->push_constant_mask;

      memcpy(upload_data, layout->push_constant_offsets, sizeof(layout->push_constant_offsets));
      upload_data = (char *)upload_data + sizeof(layout->push_constant_offsets);

      memcpy(upload_data, cmd_buffer->push_constants, graphics_pipeline->base.push_constant_size);
      upload_data = (char *)upload_data + graphics_pipeline->base.push_constant_size;

      struct radv_descriptor_state *descriptors_state =
         radv_get_descriptors_state(cmd_buffer, pGeneratedCommandsInfo->pipelineBindPoint);
      memcpy(upload_data, descriptors_state->dynamic_buffers, 16 * graphics_pipeline->base.dynamic_offset_count);
      upload_data = (char *)upload_data + 16 * graphics_pipeline->base.dynamic_offset_count;
   }

   if (scissor_size) {
      params.scissor_offset = (char*)upload_data - (char*)upload_data_base;
      params.scissor_count = scissor_size / 4;

      struct radeon_cmdbuf scissor_cs = {
         .buf = upload_data,
         .cdw = 0,
         .max_dw = scissor_size / 4
      };

      radv_write_scissors(cmd_buffer, &scissor_cs);
      assert(scissor_cs.cdw * 4 == scissor_size);
      upload_data = (char *)upload_data + scissor_size;
   }

   VkWriteDescriptorSet ds_writes[5];
   VkDescriptorBufferInfo buf_info[ARRAY_SIZE(ds_writes)];
   int ds_cnt = 0;
   buf_info[ds_cnt] = (VkDescriptorBufferInfo){.buffer = radv_buffer_to_handle(&token_buffer),
                                               .offset = 0,
                                               .range = upload_size};
   ds_writes[ds_cnt] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                              .dstBinding = DGC_DESC_PARAMS,
                                              .dstArrayElement = 0,
                                              .descriptorCount = 1,
                                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                              .pBufferInfo = &buf_info[ds_cnt]};
   ++ds_cnt;

   buf_info[ds_cnt] = (VkDescriptorBufferInfo){.buffer = pGeneratedCommandsInfo->preprocessBuffer,
                                               .offset = pGeneratedCommandsInfo->preprocessOffset,
                                               .range = pGeneratedCommandsInfo->preprocessSize};
   ds_writes[ds_cnt] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                              .dstBinding = DGC_DESC_PREPARE,
                                              .dstArrayElement = 0,
                                              .descriptorCount = 1,
                                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                              .pBufferInfo = &buf_info[ds_cnt]};
   ++ds_cnt;

   if (pGeneratedCommandsInfo->streamCount > 0) {
      buf_info[ds_cnt] =
         (VkDescriptorBufferInfo){.buffer = pGeneratedCommandsInfo->pStreams[0].buffer,
                                  .offset = pGeneratedCommandsInfo->pStreams[0].offset,
                                  .range = VK_WHOLE_SIZE};
      ds_writes[ds_cnt] =
         (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstBinding = DGC_DESC_STREAM,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                .pBufferInfo = &buf_info[ds_cnt]};
      ++ds_cnt;
   }

   if (pGeneratedCommandsInfo->sequencesCountBuffer != VK_NULL_HANDLE) {
      buf_info[ds_cnt] =
         (VkDescriptorBufferInfo){.buffer = pGeneratedCommandsInfo->sequencesCountBuffer,
                                  .offset = pGeneratedCommandsInfo->sequencesCountOffset,
                                  .range = VK_WHOLE_SIZE};
      ds_writes[ds_cnt] =
         (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstBinding = DGC_DESC_COUNT,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                .pBufferInfo = &buf_info[ds_cnt]};
      ++ds_cnt;
      params.sequence_count = UINT32_MAX;
   }

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.dgc_prepare.pipeline);

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         cmd_buffer->device->meta_state.dgc_prepare.p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 cmd_buffer->device->meta_state.dgc_prepare.p_layout, 0, ds_cnt,
                                 ds_writes);

   unsigned block_count = MAX2(1, round_up_u32(pGeneratedCommandsInfo->sequencesCount, 64));
   radv_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

   radv_buffer_finish(&token_buffer);
   radv_meta_restore(&saved_state, cmd_buffer);

   cmd_buffer->state.flush_bits |=
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE | RADV_CMD_FLAG_INV_L2;
}
