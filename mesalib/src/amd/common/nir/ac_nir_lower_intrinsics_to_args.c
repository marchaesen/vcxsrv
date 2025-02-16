/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"

typedef struct {
   const struct ac_shader_args *const args;
   const enum amd_gfx_level gfx_level;
   bool has_ls_vgpr_init_bug;
   unsigned wave_size;
   unsigned workgroup_size;
   const enum ac_hw_stage hw_stage;

   nir_def *vertex_id;
   nir_def *instance_id;
   nir_def *vs_rel_patch_id;
   nir_def *tes_u;
   nir_def *tes_v;
   nir_def *tes_patch_id;
   nir_def *tes_rel_patch_id;
} lower_intrinsics_to_args_state;

static nir_def *
preload_arg(lower_intrinsics_to_args_state *s, nir_function_impl *impl, struct ac_arg arg,
            struct ac_arg ls_buggy_arg, unsigned upper_bound)
{
   nir_builder start_b = nir_builder_at(nir_before_impl(impl));
   nir_def *value = ac_nir_load_arg_upper_bound(&start_b, s->args, arg, upper_bound);

   /* If there are no HS threads, SPI mistakenly loads the LS VGPRs starting at VGPR 0. */
   if ((s->hw_stage == AC_HW_LOCAL_SHADER || s->hw_stage == AC_HW_HULL_SHADER) &&
       s->has_ls_vgpr_init_bug) {
      nir_def *count = ac_nir_unpack_arg(&start_b, s->args, s->args->merged_wave_info, 8, 8);
      nir_def *hs_empty = nir_ieq_imm(&start_b, count, 0);
      value = nir_bcsel(&start_b, hs_empty,
                        ac_nir_load_arg_upper_bound(&start_b, s->args, ls_buggy_arg, upper_bound),
                        value);
   }
   return value;
}

static nir_def *
load_subgroup_id_lowered(lower_intrinsics_to_args_state *s, nir_builder *b)
{
   if (s->workgroup_size <= s->wave_size) {
      return nir_imm_int(b, 0);
   } else if (s->hw_stage == AC_HW_COMPUTE_SHADER) {
      assert(s->gfx_level < GFX12 && s->args->tg_size.used);

      if (s->gfx_level >= GFX10_3) {
         return ac_nir_unpack_arg(b, s->args, s->args->tg_size, 20, 5);
      } else {
         /* GFX6-10 don't actually support a wave id, but we can
          * use the ordered id because ORDERED_APPEND_* is set to
          * zero in the compute dispatch initiator.
          */
         return ac_nir_unpack_arg(b, s->args, s->args->tg_size, 6, 6);
      }
   } else if (s->hw_stage == AC_HW_HULL_SHADER && s->gfx_level >= GFX11) {
      assert(s->args->tcs_wave_id.used);
      return ac_nir_unpack_arg(b, s->args, s->args->tcs_wave_id, 0, 3);
   } else if (s->hw_stage == AC_HW_LEGACY_GEOMETRY_SHADER ||
              s->hw_stage == AC_HW_NEXT_GEN_GEOMETRY_SHADER) {
      assert(s->args->merged_wave_info.used);
      return ac_nir_unpack_arg(b, s->args, s->args->merged_wave_info, 24, 4);
   } else {
      return nir_imm_int(b, 0);
   }
}

static bool
lower_intrinsic_to_arg(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   lower_intrinsics_to_args_state *s = (lower_intrinsics_to_args_state *)state;
   nir_def *replacement = NULL;
   b->cursor = nir_after_instr(&intrin->instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_subgroup_id:
      if (s->gfx_level >= GFX12 && s->hw_stage == AC_HW_COMPUTE_SHADER)
         return false; /* Lowered in backend compilers. */
      replacement = load_subgroup_id_lowered(s, b);
      break;
   case nir_intrinsic_load_num_subgroups: {
      if (s->hw_stage == AC_HW_COMPUTE_SHADER) {
         assert(s->args->tg_size.used);
         replacement = ac_nir_unpack_arg(b, s->args, s->args->tg_size, 0, 6);
      } else if (s->hw_stage == AC_HW_LEGACY_GEOMETRY_SHADER ||
                 s->hw_stage == AC_HW_NEXT_GEN_GEOMETRY_SHADER) {
         assert(s->args->merged_wave_info.used);
         replacement = ac_nir_unpack_arg(b, s->args, s->args->merged_wave_info, 28, 4);
      } else {
         replacement = nir_imm_int(b, 1);
      }

      break;
   }
   case nir_intrinsic_load_workgroup_id:
      if (b->shader->info.stage == MESA_SHADER_MESH) {
         /* This lowering is only valid with fast_launch = 2, otherwise we assume that
          * lower_workgroup_id_to_index removed any uses of the workgroup id by this point.
          */
         assert(s->gfx_level >= GFX11);
         nir_def *xy = ac_nir_load_arg(b, s->args, s->args->tess_offchip_offset);
         nir_def *z = ac_nir_load_arg(b, s->args, s->args->gs_attr_offset);
         replacement = nir_vec3(b, nir_extract_u16(b, xy, nir_imm_int(b, 0)),
                                nir_extract_u16(b, xy, nir_imm_int(b, 1)),
                                nir_extract_u16(b, z, nir_imm_int(b, 1)));
      } else {
         return false;
      }
      break;
   case nir_intrinsic_load_pixel_coord:
      replacement = nir_unpack_32_2x16(b, ac_nir_load_arg(b, s->args, s->args->pos_fixed_pt));
      break;
   case nir_intrinsic_load_frag_coord:
      replacement = nir_vec4(b, ac_nir_load_arg(b, s->args, s->args->frag_pos[0]),
                             ac_nir_load_arg(b, s->args, s->args->frag_pos[1]),
                             ac_nir_load_arg(b, s->args, s->args->frag_pos[2]),
                             ac_nir_load_arg(b, s->args, s->args->frag_pos[3]));
      break;
   case nir_intrinsic_load_local_invocation_id: {
      unsigned num_bits[3];
      nir_def *vec[3];

      for (unsigned i = 0; i < 3; i++) {
         bool has_chan = b->shader->info.workgroup_size_variable ||
                         b->shader->info.workgroup_size[i] > 1;
         /* Extract as few bits possible - we want the constant to be an inline constant
          * instead of a literal.
          */
         num_bits[i] = !has_chan ? 0 :
                       b->shader->info.workgroup_size_variable ?
                                   10 : util_logbase2_ceil(b->shader->info.workgroup_size[i]);
      }

      if (s->args->local_invocation_ids_packed.used) {
         unsigned extract_bits[3];
         memcpy(extract_bits, num_bits, sizeof(num_bits));

         /* Thread IDs are packed in VGPR0, 10 bits per component.
          * Always extract all remaining bits if later ID components are always 0, which will
          * translate to a bit shift.
          */
         if (num_bits[2]) {
            extract_bits[2] = 12; /* Z > 0 */
         } else if (num_bits[1])
            extract_bits[1] = 22; /* Y > 0, Z == 0 */
         else if (num_bits[0])
            extract_bits[0] = 32; /* X > 0, Y == 0, Z == 0 */

         nir_def *ids_packed =
            ac_nir_load_arg_upper_bound(b, s->args, s->args->local_invocation_ids_packed,
                                        b->shader->info.workgroup_size_variable ?
                                           0 : ((b->shader->info.workgroup_size[0] - 1) |
                                                ((b->shader->info.workgroup_size[1] - 1) << 10) |
                                                ((b->shader->info.workgroup_size[2] - 1) << 20)));

         for (unsigned i = 0; i < 3; i++) {
            vec[i] = !num_bits[i] ? nir_imm_int(b, 0) :
                                    ac_nir_unpack_value(b,  ids_packed, i * 10, extract_bits[i]);
         }
      } else {
         const struct ac_arg ids[] = {
            s->args->local_invocation_id_x,
            s->args->local_invocation_id_y,
            s->args->local_invocation_id_z,
         };

         for (unsigned i = 0; i < 3; i++) {
            unsigned max = b->shader->info.workgroup_size_variable ?
                              1023 : (b->shader->info.workgroup_size[i] - 1);
            vec[i] = !num_bits[i] ? nir_imm_int(b, 0) :
                                    ac_nir_load_arg_upper_bound(b, s->args, ids[i], max);
         }
      }
      replacement = nir_vec(b, vec, 3);
      break;
   }
   case nir_intrinsic_load_merged_wave_info_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->merged_wave_info);
      break;
   case nir_intrinsic_load_workgroup_num_input_vertices_amd:
      replacement = ac_nir_unpack_arg(b, s->args, s->args->gs_tg_info, 12, 9);
      break;
   case nir_intrinsic_load_workgroup_num_input_primitives_amd:
      replacement = ac_nir_unpack_arg(b, s->args, s->args->gs_tg_info, 22, 9);
      break;
   case nir_intrinsic_load_packed_passthrough_primitive_amd:
      /* NGG passthrough mode: the HW already packs the primitive export value to a single register.
       */
      replacement = ac_nir_load_arg(b, s->args, s->args->gs_vtx_offset[0]);
      break;
   case nir_intrinsic_load_ordered_id_amd:
      replacement = ac_nir_unpack_arg(b, s->args, s->args->gs_tg_info, 0, 12);
      break;
   case nir_intrinsic_load_ring_tess_offchip_offset_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->tess_offchip_offset);
      break;
   case nir_intrinsic_load_ring_tess_factors_offset_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->tcs_factor_offset);
      break;
   case nir_intrinsic_load_ring_es2gs_offset_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->es2gs_offset);
      break;
   case nir_intrinsic_load_ring_gs2vs_offset_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->gs2vs_offset);
      break;
   case nir_intrinsic_load_gs_vertex_offset_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->gs_vtx_offset[nir_intrinsic_base(intrin)]);
      break;
   case nir_intrinsic_load_streamout_config_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->streamout_config);
      break;
   case nir_intrinsic_load_streamout_write_index_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->streamout_write_index);
      break;
   case nir_intrinsic_load_streamout_offset_amd:
      replacement = ac_nir_load_arg(b, s->args, s->args->streamout_offset[nir_intrinsic_base(intrin)]);
      break;
   case nir_intrinsic_load_ring_attr_offset_amd: {
      nir_def *ring_attr_offset = ac_nir_load_arg(b, s->args, s->args->gs_attr_offset);
      replacement = nir_ishl_imm(b, nir_ubfe_imm(b, ring_attr_offset, 0, 15), 9); /* 512b increments. */
      break;
   }
   case nir_intrinsic_load_first_vertex:
      replacement = ac_nir_load_arg(b, s->args, s->args->base_vertex);
      break;
   case nir_intrinsic_load_base_instance:
      replacement = ac_nir_load_arg(b, s->args, s->args->start_instance);
      break;
   case nir_intrinsic_load_draw_id:
      replacement = ac_nir_load_arg(b, s->args, s->args->draw_id);
      break;
   case nir_intrinsic_load_view_index:
      replacement = ac_nir_load_arg_upper_bound(b, s->args, s->args->view_index, 1);
      break;
   case nir_intrinsic_load_invocation_id:
      if (b->shader->info.stage == MESA_SHADER_TESS_CTRL) {
         replacement = ac_nir_unpack_arg(b, s->args, s->args->tcs_rel_ids, 8, 5);
      } else if (b->shader->info.stage == MESA_SHADER_GEOMETRY) {
         if (s->gfx_level >= GFX12) {
            replacement = ac_nir_unpack_arg(b, s->args, s->args->gs_vtx_offset[0], 27, 5);
         } else if (s->gfx_level >= GFX10) {
            replacement = ac_nir_unpack_arg(b, s->args, s->args->gs_invocation_id, 0, 5);
         } else {
            replacement = ac_nir_load_arg_upper_bound(b, s->args, s->args->gs_invocation_id, 31);
         }
      } else {
         unreachable("unexpected shader stage");
      }
      break;
   case nir_intrinsic_load_sample_id:
      replacement = ac_nir_unpack_arg(b, s->args, s->args->ancillary, 8, 4);
      break;
   case nir_intrinsic_load_sample_pos:
      replacement = nir_vec2(b, nir_ffract(b, ac_nir_load_arg(b, s->args, s->args->frag_pos[0])),
                             nir_ffract(b, ac_nir_load_arg(b, s->args, s->args->frag_pos[1])));
      break;
   case nir_intrinsic_load_frag_shading_rate: {
      /* VRS Rate X = Ancillary[2:3]
       * VRS Rate Y = Ancillary[4:5]
       */
      nir_def *x_rate = ac_nir_unpack_arg(b, s->args, s->args->ancillary, 2, 2);
      nir_def *y_rate = ac_nir_unpack_arg(b, s->args, s->args->ancillary, 4, 2);

      /* xRate = xRate == 0x1 ? Horizontal2Pixels : None. */
      x_rate = nir_bcsel(b, nir_ieq_imm(b, x_rate, 1), nir_imm_int(b, 4), nir_imm_int(b, 0));

      /* yRate = yRate == 0x1 ? Vertical2Pixels : None. */
      y_rate = nir_bcsel(b, nir_ieq_imm(b, y_rate, 1), nir_imm_int(b, 1), nir_imm_int(b, 0));
      replacement = nir_ior(b, x_rate, y_rate);
      break;
   }
   case nir_intrinsic_load_front_face:
      replacement = nir_fgt_imm(b, ac_nir_load_arg(b, s->args, s->args->front_face), 0);
      break;
   case nir_intrinsic_load_front_face_fsign:
      replacement = ac_nir_load_arg(b, s->args, s->args->front_face);
      break;
   case nir_intrinsic_load_layer_id:
      replacement = ac_nir_unpack_arg(b, s->args, s->args->ancillary,
                                      16, s->gfx_level >= GFX12 ? 14 : 13);
      break;
   case nir_intrinsic_load_barycentric_optimize_amd: {
      nir_def *prim_mask = ac_nir_load_arg(b, s->args, s->args->prim_mask);
      /* enabled when bit 31 is set */
      replacement = nir_ilt_imm(b, prim_mask, 0);
      break;
   }
   case nir_intrinsic_load_barycentric_pixel:
      if (nir_intrinsic_interp_mode(intrin) == INTERP_MODE_NOPERSPECTIVE)
         replacement = ac_nir_load_arg(b, s->args, s->args->linear_center);
      else
         replacement = ac_nir_load_arg(b, s->args, s->args->persp_center);
      break;
   case nir_intrinsic_load_barycentric_centroid:
      if (nir_intrinsic_interp_mode(intrin) == INTERP_MODE_NOPERSPECTIVE)
         replacement = ac_nir_load_arg(b, s->args, s->args->linear_centroid);
      else
         replacement = ac_nir_load_arg(b, s->args, s->args->persp_centroid);
      break;
   case nir_intrinsic_load_barycentric_sample:
      if (nir_intrinsic_interp_mode(intrin) == INTERP_MODE_NOPERSPECTIVE)
         replacement = ac_nir_load_arg(b, s->args, s->args->linear_sample);
      else
         replacement = ac_nir_load_arg(b, s->args, s->args->persp_sample);
      break;
   case nir_intrinsic_load_barycentric_model:
      replacement = ac_nir_load_arg(b, s->args, s->args->pull_model);
      break;
   case nir_intrinsic_load_barycentric_at_offset: {
      nir_def *baryc = nir_intrinsic_interp_mode(intrin) == INTERP_MODE_NOPERSPECTIVE ?
                          ac_nir_load_arg(b, s->args, s->args->linear_center) :
                          ac_nir_load_arg(b, s->args, s->args->persp_center);
      nir_def *i = nir_channel(b, baryc, 0);
      nir_def *j = nir_channel(b, baryc, 1);
      nir_def *offset_x = nir_channel(b, intrin->src[0].ssa, 0);
      nir_def *offset_y = nir_channel(b, intrin->src[0].ssa, 1);
      nir_def *ddx_i = nir_ddx(b, i);
      nir_def *ddx_j = nir_ddx(b, j);
      nir_def *ddy_i = nir_ddy(b, i);
      nir_def *ddy_j = nir_ddy(b, j);

      /* Interpolate standard barycentrics by offset. */
      nir_def *offset_i = nir_ffma(b, ddy_i, offset_y, nir_ffma(b, ddx_i, offset_x, i));
      nir_def *offset_j = nir_ffma(b, ddy_j, offset_y, nir_ffma(b, ddx_j, offset_x, j));
      replacement = nir_vec2(b, offset_i, offset_j);
      break;
   }
   case nir_intrinsic_load_gs_wave_id_amd:
      if (s->args->merged_wave_info.used)
         replacement = ac_nir_unpack_arg(b, s->args, s->args->merged_wave_info, 16, 8);
      else if (s->args->gs_wave_id.used)
         replacement = ac_nir_load_arg(b, s->args, s->args->gs_wave_id);
      else
         unreachable("Shader doesn't have GS wave ID.");
      break;
   case nir_intrinsic_overwrite_vs_arguments_amd:
      s->vertex_id = intrin->src[0].ssa;
      s->instance_id = intrin->src[1].ssa;
      nir_instr_remove(&intrin->instr);
      return true;
   case nir_intrinsic_overwrite_tes_arguments_amd:
      s->tes_u = intrin->src[0].ssa;
      s->tes_v = intrin->src[1].ssa;
      s->tes_patch_id = intrin->src[2].ssa;
      s->tes_rel_patch_id = intrin->src[3].ssa;
      nir_instr_remove(&intrin->instr);
      return true;
   case nir_intrinsic_load_vertex_id_zero_base:
      if (!s->vertex_id)
         s->vertex_id = preload_arg(s, b->impl, s->args->vertex_id, s->args->tcs_patch_id, 0);
      replacement = s->vertex_id;
      break;
   case nir_intrinsic_load_instance_id:
      if (!s->instance_id)
         s->instance_id = preload_arg(s, b->impl, s->args->instance_id, s->args->vertex_id, 0);
      replacement = s->instance_id;
      break;
   case nir_intrinsic_load_tess_rel_patch_id_amd:
      if (b->shader->info.stage == MESA_SHADER_TESS_CTRL) {
         replacement = ac_nir_unpack_arg(b, s->args, s->args->tcs_rel_ids, 0, 8);
      } else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
         if (s->tes_rel_patch_id) {
            replacement = s->tes_rel_patch_id;
         } else {
            replacement = ac_nir_load_arg(b, s->args, s->args->tes_rel_patch_id);
            if (b->shader->info.tess.tcs_vertices_out) {
               /* Setting an upper bound like this will actually make it possible
                * to optimize some multiplications (in address calculations) so that
                * constant additions can be added to the const offset in memory load instructions.
                */
               nir_intrinsic_set_arg_upper_bound_u32_amd(nir_instr_as_intrinsic(replacement->parent_instr),
                                                         2048 / b->shader->info.tess.tcs_vertices_out);
            }
         }
      } else {
         unreachable("invalid stage");
      }
      break;
   case nir_intrinsic_load_primitive_id:
      if (b->shader->info.stage == MESA_SHADER_GEOMETRY) {
         replacement = ac_nir_load_arg(b, s->args, s->args->gs_prim_id);
      } else if (b->shader->info.stage == MESA_SHADER_TESS_CTRL) {
         replacement = ac_nir_load_arg(b, s->args, s->args->tcs_patch_id);
      } else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
         replacement = s->tes_patch_id ? s->tes_patch_id :
                                         ac_nir_load_arg(b, s->args, s->args->tes_patch_id);
      } else if (b->shader->info.stage == MESA_SHADER_VERTEX) {
         if (s->hw_stage == AC_HW_VERTEX_SHADER)
            replacement = ac_nir_load_arg(b, s->args, s->args->vs_prim_id); /* legacy */
         else
            replacement = ac_nir_load_arg(b, s->args, s->args->gs_prim_id); /* NGG */
      } else {
         unreachable("invalid stage");
      }
      break;
   case nir_intrinsic_load_tess_coord: {
      nir_def *coord[3] = {
         s->tes_u ? s->tes_u : ac_nir_load_arg(b, s->args, s->args->tes_u),
         s->tes_v ? s->tes_v : ac_nir_load_arg(b, s->args, s->args->tes_v),
         nir_imm_float(b, 0),
      };

      /* For triangles, the vector should be (u, v, 1-u-v). */
      if (b->shader->info.tess._primitive_mode == TESS_PRIMITIVE_TRIANGLES)
         coord[2] = nir_fsub(b, nir_imm_float(b, 1), nir_fadd(b, coord[0], coord[1]));
      replacement = nir_vec(b, coord, 3);
      break;
   }
   case nir_intrinsic_load_local_invocation_index:
      /* GFX11 HS has subgroup_id, so use it instead of vs_rel_patch_id. */
      if (s->gfx_level < GFX11 &&
          (s->hw_stage == AC_HW_LOCAL_SHADER || s->hw_stage == AC_HW_HULL_SHADER)) {
         if (!s->vs_rel_patch_id) {
            s->vs_rel_patch_id = preload_arg(s, b->impl, s->args->vs_rel_patch_id,
                                             s->args->tcs_rel_ids, 255);
         }
         replacement = s->vs_rel_patch_id;
      } else if (s->workgroup_size <= s->wave_size) {
         /* Just a subgroup invocation ID. */
         replacement = nir_mbcnt_amd(b, nir_imm_intN_t(b, ~0ull, s->wave_size), nir_imm_int(b, 0));
      } else if (s->gfx_level < GFX12 && s->hw_stage == AC_HW_COMPUTE_SHADER && s->wave_size == 64) {
         /* After the AND the bits are already multiplied by 64 (left shifted by 6) so we can just
          * feed that to mbcnt. (GFX12 doesn't have tg_size)
          */
         nir_def *wave_id_mul_64 = nir_iand_imm(b, ac_nir_load_arg(b, s->args, s->args->tg_size), 0xfc0);
         replacement = nir_mbcnt_amd(b, nir_imm_intN_t(b, ~0ull, s->wave_size), wave_id_mul_64);
      } else {
         nir_def *subgroup_id;

         if (s->gfx_level >= GFX12 && s->hw_stage == AC_HW_COMPUTE_SHADER) {
            subgroup_id = nir_load_subgroup_id(b);
         } else {
            subgroup_id = load_subgroup_id_lowered(s, b);
         }

         replacement = nir_mbcnt_amd(b, nir_imm_intN_t(b, ~0ull, s->wave_size),
                                     nir_imul_imm(b, subgroup_id, s->wave_size));
      }
      break;
   case nir_intrinsic_load_subgroup_invocation:
      replacement = nir_mbcnt_amd(b, nir_imm_intN_t(b, ~0ull, s->wave_size), nir_imm_int(b, 0));
      break;
   default:
      return false;
   }

   assert(replacement);
   nir_def_replace(&intrin->def, replacement);
   return true;
}

bool
ac_nir_lower_intrinsics_to_args(nir_shader *shader, const enum amd_gfx_level gfx_level,
                                bool has_ls_vgpr_init_bug, const enum ac_hw_stage hw_stage,
                                unsigned wave_size, unsigned workgroup_size,
                                const struct ac_shader_args *ac_args)
{
   lower_intrinsics_to_args_state state = {
      .gfx_level = gfx_level,
      .hw_stage = hw_stage,
      .has_ls_vgpr_init_bug = has_ls_vgpr_init_bug,
      .wave_size = wave_size,
      .workgroup_size = workgroup_size,
      .args = ac_args,
   };

   return nir_shader_intrinsics_pass(shader, lower_intrinsic_to_arg,
                                     nir_metadata_control_flow, &state);
}
