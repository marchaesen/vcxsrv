/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"

#include "ac_nir.h"
#include "si_pipe.h"
#include "si_query.h"
#include "si_state.h"
#include "si_shader_internal.h"

struct lower_abi_state {
   struct si_shader *shader;
   struct si_shader_args *args;

   nir_def *esgs_ring;
   nir_def *tess_offchip_ring;
   nir_def *gsvs_ring[4];
};

#define GET_FIELD_NIR(field) \
   ac_nir_unpack_arg(b, &args->ac, args->vs_state_bits, \
                     field##__SHIFT, util_bitcount(field##__MASK))

nir_def *si_nir_load_internal_binding(nir_builder *b, struct si_shader_args *args,
                                          unsigned slot, unsigned num_components)
{
   nir_def *addr = ac_nir_load_arg(b, &args->ac, args->internal_bindings);
   return nir_load_smem_amd(b, num_components, addr, nir_imm_int(b, slot * 16));
}

static nir_def *build_attr_ring_desc(nir_builder *b, struct si_shader *shader,
                                         struct si_shader_args *args)
{
   struct si_shader_selector *sel = shader->selector;

   nir_def *attr_address =
      b->shader->info.stage == MESA_SHADER_VERTEX && b->shader->info.vs.blit_sgprs_amd ?
      ac_nir_load_arg_at_offset(b, &args->ac, args->vs_blit_inputs,
                                b->shader->info.vs.blit_sgprs_amd - 1) :
      ac_nir_load_arg(b, &args->ac, args->gs_attr_address);

   unsigned stride = 16 * si_shader_num_alloc_param_exports(shader);
   uint32_t desc[4];

   ac_build_attr_ring_descriptor(sel->screen->info.gfx_level,
                                 (uint64_t)sel->screen->info.address32_hi << 32,
                                 0xffffffff, stride, desc);

   nir_def *comp[] = {
      attr_address,
      nir_imm_int(b, desc[1]),
      nir_imm_int(b, desc[2]),
      nir_imm_int(b, desc[3]),
   };

   return nir_vec(b, comp, 4);
}

static nir_def *build_tess_ring_desc(nir_builder *b, struct si_screen *screen,
                                         struct si_shader_args *args)
{
   nir_def *addr = ac_nir_load_arg(b, &args->ac, args->tes_offchip_addr);
   uint32_t desc[4];

   ac_build_raw_buffer_descriptor(screen->info.gfx_level,
                             (uint64_t)screen->info.address32_hi << 32,
                             0xffffffff, desc);

   nir_def *comp[4] = {
      addr,
      nir_imm_int(b, desc[1]),
      nir_imm_int(b, desc[2]),
      nir_imm_int(b, desc[3]),
   };

   return nir_vec(b, comp, 4);
}

static nir_def *build_esgs_ring_desc(nir_builder *b, enum amd_gfx_level gfx_level,
                                         struct si_shader_args *args)
{
   nir_def *desc = si_nir_load_internal_binding(b, args, SI_RING_ESGS, 4);

   if (b->shader->info.stage == MESA_SHADER_GEOMETRY)
      return desc;

   nir_def *vec[4];
   for (int i = 0; i < 4; i++)
      vec[i] = nir_channel(b, desc, i);

   vec[1] = nir_ior_imm(b, vec[1], S_008F04_SWIZZLE_ENABLE_GFX6(1));
   vec[3] = nir_ior_imm(b, vec[3],
                        S_008F0C_ELEMENT_SIZE(1) |
                        S_008F0C_INDEX_STRIDE(3) |
                        S_008F0C_ADD_TID_ENABLE(1));

   /* If MUBUF && ADD_TID_ENABLE, DATA_FORMAT means STRIDE[14:17] on gfx8-9, so set 0. */
   if (gfx_level == GFX8)
      vec[3] = nir_iand_imm(b, vec[3], C_008F0C_DATA_FORMAT);

   return nir_vec(b, vec, 4);
}

static bool build_gsvs_ring_desc(nir_builder *b, struct lower_abi_state *s)
{
   const struct si_shader_selector *sel = s->shader->selector;
   const union si_shader_key *key = &s->shader->key;

   if (s->shader->is_gs_copy_shader) {
      s->gsvs_ring[0] = si_nir_load_internal_binding(b, s->args, SI_RING_GSVS, 4);
      return true;
   } else if (b->shader->info.stage == MESA_SHADER_GEOMETRY && !key->ge.as_ngg) {
      nir_def *base_addr = si_nir_load_internal_binding(b, s->args, SI_RING_GSVS, 2);
      base_addr = nir_pack_64_2x32(b, base_addr);

      /* The conceptual layout of the GSVS ring is
       *   v0c0 .. vLv0 v0c1 .. vLc1 ..
       * but the real memory layout is swizzled across
       * threads:
       *   t0v0c0 .. t15v0c0 t0v1c0 .. t15v1c0 ... t15vLcL
       *   t16v0c0 ..
       * Override the buffer descriptor accordingly.
       */

      for (unsigned stream = 0; stream < 4; stream++) {
         unsigned num_components = sel->info.num_stream_output_components[stream];
         if (!num_components)
            continue;

         unsigned stride = 4 * num_components * b->shader->info.gs.vertices_out;
         /* Limit on the stride field for <= GFX7. */
         assert(stride < (1 << 14));

         unsigned num_records = s->shader->wave_size;

         const struct ac_buffer_state buffer_state = {
            .size = num_records,
            .format = PIPE_FORMAT_R32_FLOAT,
            .swizzle = {
               PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W,
            },
            .stride = stride,
            .swizzle_enable = true,
            .element_size = 1,
            .index_stride = 1,
            .add_tid = true,
            .gfx10_oob_select = V_008F0C_OOB_SELECT_DISABLED,
         };
         uint32_t tmp_desc[4];

         ac_build_buffer_descriptor(sel->screen->info.gfx_level, &buffer_state, tmp_desc);

         nir_def *desc[4];
         desc[0] = nir_unpack_64_2x32_split_x(b, base_addr);
         desc[1] = nir_ior_imm(b, nir_unpack_64_2x32_split_y(b, base_addr), tmp_desc[1]);
         desc[2] = nir_imm_int(b, tmp_desc[2]);
         desc[3] = nir_imm_int(b, tmp_desc[3]);

         s->gsvs_ring[stream] = nir_vec(b, desc, 4);

         /* next stream's desc addr */
         base_addr = nir_iadd_imm(b, base_addr, stride * num_records);
      }

      return true;
   }

   return false;
}

static bool preload_reusable_variables(nir_builder *b, struct lower_abi_state *s)
{
   const struct si_shader_selector *sel = s->shader->selector;
   const union si_shader_key *key = &s->shader->key;
   bool progress = false;

   b->cursor = nir_before_impl(b->impl);

   if (sel->screen->info.gfx_level <= GFX8 && b->shader->info.stage <= MESA_SHADER_GEOMETRY &&
       (key->ge.as_es || b->shader->info.stage == MESA_SHADER_GEOMETRY)) {
      s->esgs_ring = build_esgs_ring_desc(b, sel->screen->info.gfx_level, s->args);
      progress = true;
   }

   if (b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
       b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
      s->tess_offchip_ring = build_tess_ring_desc(b, sel->screen, s->args);
      progress = true;
   }

   return build_gsvs_ring_desc(b, s) || progress;
}

static nir_def *get_num_vertices_per_prim(nir_builder *b, struct lower_abi_state *s)
{
   struct si_shader_args *args = s->args;
   unsigned num_vertices = si_get_num_vertices_per_output_prim(s->shader);

   if (num_vertices)
      return nir_imm_int(b, num_vertices);
   else
      return nir_iadd_imm(b, GET_FIELD_NIR(GS_STATE_OUTPRIM), 1);
}

static nir_def *get_small_prim_precision(nir_builder *b, struct lower_abi_state *s, bool lines)
{
   /* Compute FP32 value "num_samples / quant_mode" using integer ops.
    * See si_shader.h for how this works.
    */
   struct si_shader_args *args = s->args;
   nir_def *precision = GET_FIELD_NIR(GS_STATE_SMALL_PRIM_PRECISION);
   nir_def *log_samples = GET_FIELD_NIR(GS_STATE_SMALL_PRIM_PRECISION_LOG_SAMPLES);

   if (lines)
      precision = nir_iadd(b, precision, log_samples);

   /* The final FP32 value is: 1/2^(15 - precision) */
   return nir_ishl_imm(b, nir_ior_imm(b, precision, 0x70), 23);
}

static bool lower_intrinsic(nir_builder *b, nir_instr *instr, struct lower_abi_state *s)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   struct si_shader *shader = s->shader;
   struct si_shader_args *args = s->args;
   struct si_shader_selector *sel = shader->selector;
   union si_shader_key *key = &shader->key;
   gl_shader_stage stage = b->shader->info.stage;

   b->cursor = nir_before_instr(instr);

   nir_def *replacement = NULL;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_base_vertex: {
      nir_def *indexed = GET_FIELD_NIR(VS_STATE_INDEXED);
      indexed = nir_i2b(b, indexed);

      nir_def *base_vertex = ac_nir_load_arg(b, &args->ac, args->ac.base_vertex);
      replacement = nir_bcsel(b, indexed, base_vertex, nir_imm_int(b, 0));
      break;
   }
   case nir_intrinsic_load_workgroup_size: {
      assert(b->shader->info.workgroup_size_variable && sel->info.uses_variable_block_size);

      nir_def *block_size = ac_nir_load_arg(b, &args->ac, args->block_size);
      nir_def *comp[] = {
         nir_ubfe_imm(b, block_size, 0, 10),
         nir_ubfe_imm(b, block_size, 10, 10),
         nir_ubfe_imm(b, block_size, 20, 10),
      };
      replacement = nir_vec(b, comp, 3);
      break;
   }
   case nir_intrinsic_load_tess_level_outer_default:
   case nir_intrinsic_load_tess_level_inner_default: {
      nir_def *buf = si_nir_load_internal_binding(b, args, SI_HS_CONST_DEFAULT_TESS_LEVELS, 4);
      unsigned num_components = intrin->def.num_components;
      unsigned offset =
         intrin->intrinsic == nir_intrinsic_load_tess_level_inner_default ? 16 : 0;
      replacement = nir_load_ubo(b, num_components, 32, buf, nir_imm_int(b, offset),
                                 .range = ~0);
      break;
   }
   case nir_intrinsic_load_patch_vertices_in:
      if (stage == MESA_SHADER_TESS_CTRL)
         replacement = ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 12, 5);
      else if (stage == MESA_SHADER_TESS_EVAL) {
         replacement = ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 7, 5);
      } else
         unreachable("no nir_load_patch_vertices_in");
      replacement = nir_iadd_imm(b, replacement, 1);
      break;
   case nir_intrinsic_load_sample_mask_in:
      replacement = ac_nir_load_arg(b, &args->ac, args->ac.sample_coverage);
      break;
   case nir_intrinsic_load_lshs_vertex_stride_amd:
      if (stage == MESA_SHADER_VERTEX) {
         replacement = nir_imm_int(b, si_shader_lshs_vertex_stride(shader));
      } else if (stage == MESA_SHADER_TESS_CTRL) {
         if (sel->screen->info.gfx_level >= GFX9 && shader->is_monolithic) {
            replacement = nir_imm_int(b, si_shader_lshs_vertex_stride(shader));
         } else {
            nir_def *num_ls_out = ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 17, 6);
            nir_def *extra_dw = nir_bcsel(b, nir_ieq_imm(b, num_ls_out, 0), nir_imm_int(b, 0), nir_imm_int(b, 4));
            replacement = nir_iadd_nuw(b, nir_ishl_imm(b, num_ls_out, 4), extra_dw);
         }
      } else {
         unreachable("no nir_load_lshs_vertex_stride_amd");
      }
      break;
   case nir_intrinsic_load_esgs_vertex_stride_amd:
      assert(sel->screen->info.gfx_level >= GFX9);
      if (shader->is_monolithic) {
         replacement = nir_imm_int(b, key->ge.part.gs.es->info.esgs_vertex_stride / 4);
      } else {
         nir_def *num_es_outputs = GET_FIELD_NIR(GS_STATE_NUM_ES_OUTPUTS);
         replacement = nir_iadd_imm(b, nir_imul_imm(b, num_es_outputs, 4), 1);
      }
      break;
   case nir_intrinsic_load_tcs_num_patches_amd: {
      nir_def *tmp = ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 0, 7);
      replacement = nir_iadd_imm(b, tmp, 1);
      break;
   }
   case nir_intrinsic_load_hs_out_patch_data_offset_amd: {
      nir_def *per_vtx_out_patch_size = NULL;

      if (stage == MESA_SHADER_TESS_CTRL) {
         const unsigned num_hs_out = util_last_bit64(sel->info.tcs_outputs_written_for_tes);
         const unsigned out_vtx_size = num_hs_out * 16;
         const unsigned out_vtx_per_patch = b->shader->info.tess.tcs_vertices_out;
         per_vtx_out_patch_size = nir_imm_int(b, out_vtx_size * out_vtx_per_patch);
      } else {
         nir_def *num_hs_out = ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 23, 6);
         nir_def *out_vtx_size = nir_ishl_imm(b, num_hs_out, 4);
         nir_def *o = ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 7, 5);
         nir_def *out_vtx_per_patch = nir_iadd_imm_nuw(b, o, 1);
         per_vtx_out_patch_size = nir_imul(b, out_vtx_per_patch, out_vtx_size);
      }

      nir_def *p = ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 0, 7);
      nir_def *num_patches = nir_iadd_imm_nuw(b, p, 1);
      replacement = nir_imul(b, per_vtx_out_patch_size, num_patches);
      break;
   }
   case nir_intrinsic_load_clip_half_line_width_amd: {
      nir_def *addr = ac_nir_load_arg(b, &args->ac, args->small_prim_cull_info);
      replacement = nir_load_smem_amd(b, 2, addr, nir_imm_int(b, 32));
      break;
   }
   case nir_intrinsic_load_cull_triangle_viewport_xy_scale_and_offset_amd: {
      nir_def *addr = ac_nir_load_arg(b, &args->ac, args->small_prim_cull_info);
      replacement = nir_load_smem_amd(b, 4, addr, nir_imm_int(b, 0));
      break;
   }
   case nir_intrinsic_load_cull_line_viewport_xy_scale_and_offset_amd: {
      nir_def *addr = ac_nir_load_arg(b, &args->ac, args->small_prim_cull_info);
      replacement = nir_load_smem_amd(b, 4, addr, nir_imm_int(b, 16));
      break;
   }
   case nir_intrinsic_load_num_vertices_per_primitive_amd:
      replacement = get_num_vertices_per_prim(b, s);
      break;
   case nir_intrinsic_load_cull_ccw_amd:
      /* radeonsi embed cw/ccw info into front/back face enabled */
      replacement = nir_imm_false(b);
      break;
   case nir_intrinsic_load_cull_any_enabled_amd:
      /* If culling is enabled at compile time, it's always enabled at runtime. */
      assert(si_shader_culling_enabled(shader));
      replacement = nir_imm_true(b);
      break;
   case nir_intrinsic_load_cull_back_face_enabled_amd:
      replacement = nir_i2b(b, GET_FIELD_NIR(GS_STATE_CULL_FACE_BACK));
      break;
   case nir_intrinsic_load_cull_front_face_enabled_amd:
      replacement = nir_i2b(b, GET_FIELD_NIR(GS_STATE_CULL_FACE_FRONT));
      break;
   case nir_intrinsic_load_cull_small_triangle_precision_amd:
      replacement = get_small_prim_precision(b, s, false);
      break;
   case nir_intrinsic_load_cull_small_line_precision_amd:
      replacement = get_small_prim_precision(b, s, true);
      break;
   case nir_intrinsic_load_cull_small_triangles_enabled_amd:
      /* Triangles always have small primitive culling enabled. */
      replacement = nir_imm_bool(b, true);
      break;
   case nir_intrinsic_load_cull_small_lines_enabled_amd:
      replacement =
         nir_imm_bool(b, key->ge.opt.ngg_culling & SI_NGG_CULL_SMALL_LINES_DIAMOND_EXIT);
      break;
   case nir_intrinsic_load_provoking_vtx_in_prim_amd:
      replacement = nir_bcsel(b, nir_i2b(b, GET_FIELD_NIR(GS_STATE_PROVOKING_VTX_FIRST)),
                              nir_imm_int(b, 0),
                              nir_iadd_imm(b, get_num_vertices_per_prim(b, s), -1));
      break;
   case nir_intrinsic_load_pipeline_stat_query_enabled_amd:
      replacement = nir_i2b(b, GET_FIELD_NIR(GS_STATE_PIPELINE_STATS_EMU));
      break;
   case nir_intrinsic_load_prim_gen_query_enabled_amd:
   case nir_intrinsic_load_prim_xfb_query_enabled_amd:
      replacement = nir_i2b(b, GET_FIELD_NIR(GS_STATE_STREAMOUT_QUERY_ENABLED));
      break;
   case nir_intrinsic_load_clamp_vertex_color_amd:
      replacement = nir_i2b(b, GET_FIELD_NIR(VS_STATE_CLAMP_VERTEX_COLOR));
      break;
   case nir_intrinsic_load_user_clip_plane: {
      nir_def *buf = si_nir_load_internal_binding(b, args, SI_VS_CONST_CLIP_PLANES, 4);
      unsigned offset = nir_intrinsic_ucp_id(intrin) * 16;
      replacement = nir_load_ubo(b, 4, 32, buf, nir_imm_int(b, offset),
                                 .range = ~0);
      break;
   }
   case nir_intrinsic_load_streamout_buffer_amd: {
      unsigned slot = SI_VS_STREAMOUT_BUF0 + nir_intrinsic_base(intrin);
      replacement = si_nir_load_internal_binding(b, args, slot, 4);
      break;
   }
   case nir_intrinsic_load_xfb_state_address_gfx12_amd: {
      nir_def *address = si_nir_load_internal_binding(b, args, SI_STREAMOUT_STATE_BUF, 1);
      nir_def *address32_hi = nir_imm_int(b, s->shader->selector->screen->info.address32_hi);
      replacement = nir_pack_64_2x32_split(b, address, address32_hi);
      break;
   }
   case nir_intrinsic_atomic_add_gs_emit_prim_count_amd:
   case nir_intrinsic_atomic_add_shader_invocation_count_amd: {
      enum pipe_statistics_query_index index =
         intrin->intrinsic == nir_intrinsic_atomic_add_gs_emit_prim_count_amd ?
         PIPE_STAT_QUERY_GS_PRIMITIVES : PIPE_STAT_QUERY_GS_INVOCATIONS;

      /* GFX11 only needs to emulate PIPE_STAT_QUERY_GS_PRIMITIVES because GS culls,
       * which makes the pipeline statistic incorrect.
       */
      assert(sel->screen->info.gfx_level < GFX11 || index == PIPE_STAT_QUERY_GS_PRIMITIVES);

      nir_def *buf =
         si_nir_load_internal_binding(b, args, SI_GS_QUERY_EMULATED_COUNTERS_BUF, 4);
      unsigned offset = si_query_pipestat_end_dw_offset(sel->screen, index) * 4;

      nir_def *count = intrin->src[0].ssa;
      nir_ssbo_atomic(b, 32, buf, nir_imm_int(b, offset), count,
                      .atomic_op = nir_atomic_op_iadd);
      break;
   }
   case nir_intrinsic_atomic_add_gen_prim_count_amd:
   case nir_intrinsic_atomic_add_xfb_prim_count_amd: {
      nir_def *buf = si_nir_load_internal_binding(b, args, SI_GS_QUERY_BUF, 4);

      unsigned stream = nir_intrinsic_stream_id(intrin);
      unsigned offset = intrin->intrinsic == nir_intrinsic_atomic_add_gen_prim_count_amd ?
         offsetof(struct gfx11_sh_query_buffer_mem, stream[stream].generated_primitives) :
         offsetof(struct gfx11_sh_query_buffer_mem, stream[stream].emitted_primitives);

      nir_def *prim_count = intrin->src[0].ssa;
      nir_ssbo_atomic(b, 32, buf, nir_imm_int(b, offset), prim_count,
                      .atomic_op = nir_atomic_op_iadd);
      break;
   }
   case nir_intrinsic_load_debug_log_desc_amd:
      replacement = si_nir_load_internal_binding(b, args, SI_RING_SHADER_LOG, 4);
      break;
   case nir_intrinsic_load_ring_attr_amd:
      replacement = build_attr_ring_desc(b, shader, args);
      break;
   case nir_intrinsic_load_force_vrs_rates_amd:
      if (sel->screen->info.gfx_level >= GFX11) {
         /* Bits [2:5] = VRS rate
          *
          * The range is [0, 15].
          *
          * If the hw doesn't support VRS 4x4, it will silently use 2x2 instead.
          */
         replacement = nir_imm_int(b, V_0283D0_VRS_SHADING_RATE_4X4 << 2);
      } else {
         /* Bits [2:3] = VRS rate X
          * Bits [4:5] = VRS rate Y
          *
          * The range is [-2, 1]. Values:
          *   1: 2x coarser shading rate in that direction.
          *   0: normal shading rate
          *  -1: 2x finer shading rate (sample shading, not directional)
          *  -2: 4x finer shading rate (sample shading, not directional)
          *
          * Sample shading can't go above 8 samples, so both numbers can't be -2
          * at the same time.
          */
         replacement = nir_imm_int(b, (1 << 2) | (1 << 4));
      }
      break;
   case nir_intrinsic_load_sample_positions_amd: {
      /* Sample locations are packed in 2 user SGPRs, 4 bits per component. */
      nir_def *sample_id = intrin->src[0].ssa;
      nir_def *sample_locs =
         nir_pack_64_2x32_split(b, ac_nir_load_arg(b, &s->args->ac, s->args->sample_locs[0]),
                                ac_nir_load_arg(b, &s->args->ac, s->args->sample_locs[1]));
      sample_locs = nir_ushr(b, sample_locs, nir_imul_imm(b, sample_id, 8));
      sample_locs = nir_u2u32(b, sample_locs);
      nir_def *sample_pos = nir_vec2(b, nir_iand_imm(b, sample_locs, 0xf),
                                     nir_ubfe_imm(b, sample_locs, 4, 4));
      replacement = nir_fmul_imm(b, nir_u2f32(b, sample_pos), 1.0 / 16);
      break;
   }
   case nir_intrinsic_load_ring_tess_factors_amd: {
      assert(s->tess_offchip_ring);
      nir_def *addr = nir_channel(b, s->tess_offchip_ring, 0);
      addr = nir_iadd_imm(b, addr, sel->screen->hs.tess_offchip_ring_size);
      replacement = nir_vector_insert_imm(b, s->tess_offchip_ring, addr, 0);
      break;
   }
   case nir_intrinsic_load_alpha_reference_amd:
      replacement = ac_nir_load_arg(b, &args->ac, args->alpha_reference);
      break;
   case nir_intrinsic_load_color0:
   case nir_intrinsic_load_color1: {
      uint32_t colors_read = sel->info.colors_read;

      int start, offset;
      if (intrin->intrinsic == nir_intrinsic_load_color0) {
         start = 0;
         offset = 0;
      } else {
         start = 4;
         offset = util_bitcount(colors_read & 0xf);
      }

      nir_def *color[4];
      for (int i = 0; i < 4; i++) {
         if (colors_read & BITFIELD_BIT(start + i))
            color[i] = ac_nir_load_arg_at_offset(b, &args->ac, args->color_start, offset++);
         else
            color[i] = nir_undef(b, 1, 32);
      }

      replacement = nir_vec(b, color, 4);
      break;
   }
   case nir_intrinsic_load_point_coord_maybe_flipped: {
      /* Load point coordinates (x, y) which are written by the hw after the interpolated inputs */
      nir_def *baryc = intrin->src[0].ssa;
      replacement = nir_load_interpolated_input(b, 2, 32, baryc, nir_imm_int(b, 0),
                                                .base = si_get_ps_num_interp(shader),
                                                .component = 2);
      break;
   }
   case nir_intrinsic_load_poly_line_smooth_enabled:
      replacement = nir_imm_bool(b, key->ps.mono.poly_line_smoothing);
      break;
   case nir_intrinsic_load_initial_edgeflags_amd: {
      unsigned output_prim = si_get_output_prim_simplified(sel, &shader->key);

      /* Points, lines, and rectangles don't need edge flags. */
      if (output_prim == MESA_PRIM_POINTS || output_prim == MESA_PRIM_LINES ||
          output_prim == SI_PRIM_RECTANGLE_LIST) {
         replacement = nir_imm_int(b, 0);
      } else if (stage == MESA_SHADER_VERTEX) {
         if (sel->screen->info.gfx_level >= GFX12) {
            replacement = nir_iand_imm(b, ac_nir_load_arg(b, &args->ac, args->ac.gs_vtx_offset[0]),
                                       ac_get_all_edge_flag_bits(sel->screen->info.gfx_level));
         } else {
            /* Use the following trick to extract the edge flags:
             *   extracted = v_and_b32 gs_invocation_id, 0x700 ; get edge flags at bits 8, 9, 10
             *   shifted = v_mul_u32_u24 extracted, 0x80402u   ; shift the bits: 8->9, 9->19, 10->29
             *   result = v_and_b32 shifted, 0x20080200        ; remove garbage
             */
            nir_def *tmp = ac_nir_load_arg(b, &args->ac, args->ac.gs_invocation_id);
            tmp = nir_iand_imm(b, tmp, 0x700);
            tmp = nir_imul_imm(b, tmp, 0x80402);
            replacement = nir_iand_imm(b, tmp, 0x20080200);
         }
      } else {
         /* TES and GS: Edge flags are always enabled by the rasterizer state when polygon mode is
          * enabled, so set all edge flags to 1 for triangles.
          */
         replacement = nir_imm_int(b, ac_get_all_edge_flag_bits(sel->screen->info.gfx_level));
      }
      break;
   }
   case nir_intrinsic_load_ring_esgs_amd:
      assert(s->esgs_ring);
      replacement = s->esgs_ring;
      break;
   case nir_intrinsic_load_ring_tess_offchip_amd:
      assert(s->tess_offchip_ring);
      replacement = s->tess_offchip_ring;
      break;
   case nir_intrinsic_load_tcs_tess_levels_to_tes_amd:
      if (shader->is_monolithic) {
         replacement = nir_imm_bool(b, key->ge.opt.tes_reads_tess_factors);
      } else {
         replacement = nir_ine_imm(b, ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 31, 1), 0);
      }
      break;
   case nir_intrinsic_load_tcs_primitive_mode_amd:
      if (shader->is_monolithic) {
         replacement = nir_imm_int(b, key->ge.opt.tes_prim_mode);
      } else {
         if (b->shader->info.tess._primitive_mode != TESS_PRIMITIVE_UNSPECIFIED)
            replacement = nir_imm_int(b, b->shader->info.tess._primitive_mode);
         else
            replacement = ac_nir_unpack_arg(b, &args->ac, args->tcs_offchip_layout, 29, 2);
      }
      break;
   case nir_intrinsic_load_ring_gsvs_amd: {
      unsigned stream_id = nir_intrinsic_stream_id(intrin);
      /* Unused nir_load_ring_gsvs_amd may not be eliminated yet. */
      replacement = s->gsvs_ring[stream_id] ?
         s->gsvs_ring[stream_id] : nir_undef(b, 4, 32);
      break;
   }
   case nir_intrinsic_load_user_data_amd: {
      nir_def *low_vec4 = ac_nir_load_arg(b, &args->ac, args->cs_user_data[0]);
      replacement = nir_pad_vector(b, low_vec4, 8);

      if (args->cs_user_data[1].used && intrin->def.num_components > 4) {
         nir_def *high_vec4 = ac_nir_load_arg(b, &args->ac, args->cs_user_data[1]);
         for (unsigned i = 0; i < high_vec4->num_components; i++)
            replacement = nir_vector_insert_imm(b, replacement, nir_channel(b, high_vec4, i), 4 + i);
      }
      break;
   }
   case nir_intrinsic_load_fbfetch_image_fmask_desc_amd:
      STATIC_ASSERT(SI_PS_IMAGE_COLORBUF0_FMASK % 2 == 0);
      replacement = si_nir_load_internal_binding(b, args, SI_PS_IMAGE_COLORBUF0_FMASK, 8);
      break;
   case nir_intrinsic_load_fbfetch_image_desc_amd:
      STATIC_ASSERT(SI_PS_IMAGE_COLORBUF0 % 2 == 0);
      replacement = si_nir_load_internal_binding(b, args, SI_PS_IMAGE_COLORBUF0, 8);
      break;
   case nir_intrinsic_load_polygon_stipple_buffer_amd:
      replacement = si_nir_load_internal_binding(b, args, SI_PS_CONST_POLY_STIPPLE, 4);
      break;
   default:
      return false;
   }

   if (replacement)
      nir_def_rewrite_uses(&intrin->def, replacement);

   nir_instr_remove(instr);
   nir_instr_free(instr);

   return true;
}

bool si_nir_lower_abi(nir_shader *nir, struct si_shader *shader, struct si_shader_args *args)
{
   struct lower_abi_state state = {
      .shader = shader,
      .args = args,
   };

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder b = nir_builder_create(impl);

   bool progress = preload_reusable_variables(&b, &state);

   nir_foreach_block_safe(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_intrinsic)
            progress |= lower_intrinsic(&b, instr, &state);
      }
   }

   nir_metadata preserved = progress ?
      nir_metadata_control_flow :
      nir_metadata_all;
   nir_progress(true, impl, preserved);

   return progress;
}
