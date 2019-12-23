/*
 * Copyright © 2018 Valve Corporation
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
 *
 */

#include <array>
#include <unordered_map>
#include "aco_ir.h"
#include "nir.h"
#include "vulkan/radv_shader.h"
#include "vulkan/radv_descriptor_set.h"
#include "vulkan/radv_shader_args.h"
#include "sid.h"
#include "ac_exp_param.h"
#include "ac_shader_util.h"

#include "util/u_math.h"

#define MAX_INLINE_PUSH_CONSTS 8

namespace aco {

struct vs_output_state {
   uint8_t mask[VARYING_SLOT_VAR31 + 1];
   Temp outputs[VARYING_SLOT_VAR31 + 1][4];
};

struct isel_context {
   const struct radv_nir_compiler_options *options;
   struct radv_shader_args *args;
   Program *program;
   nir_shader *shader;
   uint32_t constant_data_offset;
   Block *block;
   bool *divergent_vals;
   std::unique_ptr<Temp[]> allocated;
   std::unordered_map<unsigned, std::array<Temp,NIR_MAX_VEC_COMPONENTS>> allocated_vec;
   Stage stage; /* Stage */
   bool has_gfx10_wave64_bpermute = false;
   struct {
      bool has_branch;
      uint16_t loop_nest_depth = 0;
      struct {
         unsigned header_idx;
         Block* exit;
         bool has_divergent_continue = false;
         bool has_divergent_branch = false;
      } parent_loop;
      struct {
         bool is_divergent = false;
      } parent_if;
      bool exec_potentially_empty = false;
      std::unique_ptr<unsigned[]> nir_to_aco; /* NIR block index to ACO block index */
   } cf_info;

   Temp arg_temps[AC_MAX_ARGS];

   /* inputs common for merged stages */
   Temp merged_wave_info = Temp(0, s1);

   /* FS inputs */
   Temp persp_centroid, linear_centroid;

   /* VS inputs */
   bool needs_instance_id;

   /* VS output information */
   unsigned num_clip_distances;
   unsigned num_cull_distances;
   vs_output_state vs_output;
};

Temp get_arg(isel_context *ctx, struct ac_arg arg)
{
   assert(arg.used);
   return ctx->arg_temps[arg.arg_index];
}

unsigned get_interp_input(nir_intrinsic_op intrin, enum glsl_interp_mode interp)
{
   switch (interp) {
   case INTERP_MODE_SMOOTH:
   case INTERP_MODE_NONE:
      if (intrin == nir_intrinsic_load_barycentric_pixel ||
          intrin == nir_intrinsic_load_barycentric_at_sample ||
          intrin == nir_intrinsic_load_barycentric_at_offset)
         return S_0286CC_PERSP_CENTER_ENA(1);
      else if (intrin == nir_intrinsic_load_barycentric_centroid)
         return S_0286CC_PERSP_CENTROID_ENA(1);
      else if (intrin == nir_intrinsic_load_barycentric_sample)
         return S_0286CC_PERSP_SAMPLE_ENA(1);
      break;
   case INTERP_MODE_NOPERSPECTIVE:
      if (intrin == nir_intrinsic_load_barycentric_pixel)
         return S_0286CC_LINEAR_CENTER_ENA(1);
      else if (intrin == nir_intrinsic_load_barycentric_centroid)
         return S_0286CC_LINEAR_CENTROID_ENA(1);
      else if (intrin == nir_intrinsic_load_barycentric_sample)
         return S_0286CC_LINEAR_SAMPLE_ENA(1);
      break;
   default:
      break;
   }
   return 0;
}

void init_context(isel_context *ctx, nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   unsigned lane_mask_size = ctx->program->lane_mask.size();

   ctx->shader = shader;
   ctx->divergent_vals = nir_divergence_analysis(shader, nir_divergence_view_index_uniform);

   std::unique_ptr<Temp[]> allocated{new Temp[impl->ssa_alloc]()};

   unsigned spi_ps_inputs = 0;

   std::unique_ptr<unsigned[]> nir_to_aco{new unsigned[impl->num_blocks]()};

   bool done = false;
   while (!done) {
      done = true;
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            switch(instr->type) {
            case nir_instr_type_alu: {
               nir_alu_instr *alu_instr = nir_instr_as_alu(instr);
               unsigned size =  alu_instr->dest.dest.ssa.num_components;
               if (alu_instr->dest.dest.ssa.bit_size == 64)
                  size *= 2;
               RegType type = RegType::sgpr;
               switch(alu_instr->op) {
                  case nir_op_fmul:
                  case nir_op_fadd:
                  case nir_op_fsub:
                  case nir_op_fmax:
                  case nir_op_fmin:
                  case nir_op_fmax3:
                  case nir_op_fmin3:
                  case nir_op_fmed3:
                  case nir_op_fneg:
                  case nir_op_fabs:
                  case nir_op_fsat:
                  case nir_op_fsign:
                  case nir_op_frcp:
                  case nir_op_frsq:
                  case nir_op_fsqrt:
                  case nir_op_fexp2:
                  case nir_op_flog2:
                  case nir_op_ffract:
                  case nir_op_ffloor:
                  case nir_op_fceil:
                  case nir_op_ftrunc:
                  case nir_op_fround_even:
                  case nir_op_fsin:
                  case nir_op_fcos:
                  case nir_op_f2f32:
                  case nir_op_f2f64:
                  case nir_op_u2f32:
                  case nir_op_u2f64:
                  case nir_op_i2f32:
                  case nir_op_i2f64:
                  case nir_op_pack_half_2x16:
                  case nir_op_unpack_half_2x16_split_x:
                  case nir_op_unpack_half_2x16_split_y:
                  case nir_op_fddx:
                  case nir_op_fddy:
                  case nir_op_fddx_fine:
                  case nir_op_fddy_fine:
                  case nir_op_fddx_coarse:
                  case nir_op_fddy_coarse:
                  case nir_op_fquantize2f16:
                  case nir_op_ldexp:
                  case nir_op_frexp_sig:
                  case nir_op_frexp_exp:
                  case nir_op_cube_face_index:
                  case nir_op_cube_face_coord:
                     type = RegType::vgpr;
                     break;
                  case nir_op_flt:
                  case nir_op_fge:
                  case nir_op_feq:
                  case nir_op_fne:
                  case nir_op_ilt:
                  case nir_op_ige:
                  case nir_op_ult:
                  case nir_op_uge:
                  case nir_op_ieq:
                  case nir_op_ine:
                  case nir_op_i2b1:
                     size = lane_mask_size;
                     break;
                  case nir_op_f2i64:
                  case nir_op_f2u64:
                  case nir_op_b2i32:
                  case nir_op_b2f32:
                  case nir_op_f2i32:
                  case nir_op_f2u32:
                     type = ctx->divergent_vals[alu_instr->dest.dest.ssa.index] ? RegType::vgpr : RegType::sgpr;
                     break;
                  case nir_op_bcsel:
                     if (alu_instr->dest.dest.ssa.bit_size == 1) {
                        size = lane_mask_size;
                     } else {
                        if (ctx->divergent_vals[alu_instr->dest.dest.ssa.index]) {
                           type = RegType::vgpr;
                        } else {
                           if (allocated[alu_instr->src[1].src.ssa->index].type() == RegType::vgpr ||
                               allocated[alu_instr->src[2].src.ssa->index].type() == RegType::vgpr) {
                              type = RegType::vgpr;
                           }
                        }
                        if (alu_instr->src[1].src.ssa->num_components == 1 && alu_instr->src[2].src.ssa->num_components == 1) {
                           assert(allocated[alu_instr->src[1].src.ssa->index].size() == allocated[alu_instr->src[2].src.ssa->index].size());
                           size = allocated[alu_instr->src[1].src.ssa->index].size();
                        }
                     }
                     break;
                  case nir_op_mov:
                     if (alu_instr->dest.dest.ssa.bit_size == 1) {
                        size = lane_mask_size;
                     } else {
                        type = ctx->divergent_vals[alu_instr->dest.dest.ssa.index] ? RegType::vgpr : RegType::sgpr;
                     }
                     break;
                  default:
                     if (alu_instr->dest.dest.ssa.bit_size == 1) {
                        size = lane_mask_size;
                     } else {
                        for (unsigned i = 0; i < nir_op_infos[alu_instr->op].num_inputs; i++) {
                           if (allocated[alu_instr->src[i].src.ssa->index].type() == RegType::vgpr)
                              type = RegType::vgpr;
                        }
                     }
                     break;
               }
               allocated[alu_instr->dest.dest.ssa.index] = Temp(0, RegClass(type, size));
               break;
            }
            case nir_instr_type_load_const: {
               unsigned size = nir_instr_as_load_const(instr)->def.num_components;
               if (nir_instr_as_load_const(instr)->def.bit_size == 64)
                  size *= 2;
               else if (nir_instr_as_load_const(instr)->def.bit_size == 1)
                  size *= lane_mask_size;
               allocated[nir_instr_as_load_const(instr)->def.index] = Temp(0, RegClass(RegType::sgpr, size));
               break;
            }
            case nir_instr_type_intrinsic: {
               nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);
               if (!nir_intrinsic_infos[intrinsic->intrinsic].has_dest)
                  break;
               unsigned size =  intrinsic->dest.ssa.num_components;
               if (intrinsic->dest.ssa.bit_size == 64)
                  size *= 2;
               RegType type = RegType::sgpr;
               switch(intrinsic->intrinsic) {
                  case nir_intrinsic_load_push_constant:
                  case nir_intrinsic_load_work_group_id:
                  case nir_intrinsic_load_num_work_groups:
                  case nir_intrinsic_load_subgroup_id:
                  case nir_intrinsic_load_num_subgroups:
                  case nir_intrinsic_load_first_vertex:
                  case nir_intrinsic_load_base_instance:
                  case nir_intrinsic_get_buffer_size:
                  case nir_intrinsic_vote_all:
                  case nir_intrinsic_vote_any:
                  case nir_intrinsic_read_first_invocation:
                  case nir_intrinsic_read_invocation:
                  case nir_intrinsic_first_invocation:
                     type = RegType::sgpr;
                     if (intrinsic->dest.ssa.bit_size == 1)
                        size = lane_mask_size;
                     break;
                  case nir_intrinsic_ballot:
                     type = RegType::sgpr;
                     break;
                  case nir_intrinsic_load_sample_id:
                  case nir_intrinsic_load_sample_mask_in:
                  case nir_intrinsic_load_input:
                  case nir_intrinsic_load_vertex_id:
                  case nir_intrinsic_load_vertex_id_zero_base:
                  case nir_intrinsic_load_barycentric_sample:
                  case nir_intrinsic_load_barycentric_pixel:
                  case nir_intrinsic_load_barycentric_centroid:
                  case nir_intrinsic_load_barycentric_at_sample:
                  case nir_intrinsic_load_barycentric_at_offset:
                  case nir_intrinsic_load_interpolated_input:
                  case nir_intrinsic_load_frag_coord:
                  case nir_intrinsic_load_sample_pos:
                  case nir_intrinsic_load_layer_id:
                  case nir_intrinsic_load_local_invocation_id:
                  case nir_intrinsic_load_local_invocation_index:
                  case nir_intrinsic_load_subgroup_invocation:
                  case nir_intrinsic_write_invocation_amd:
                  case nir_intrinsic_mbcnt_amd:
                  case nir_intrinsic_load_instance_id:
                  case nir_intrinsic_ssbo_atomic_add:
                  case nir_intrinsic_ssbo_atomic_imin:
                  case nir_intrinsic_ssbo_atomic_umin:
                  case nir_intrinsic_ssbo_atomic_imax:
                  case nir_intrinsic_ssbo_atomic_umax:
                  case nir_intrinsic_ssbo_atomic_and:
                  case nir_intrinsic_ssbo_atomic_or:
                  case nir_intrinsic_ssbo_atomic_xor:
                  case nir_intrinsic_ssbo_atomic_exchange:
                  case nir_intrinsic_ssbo_atomic_comp_swap:
                  case nir_intrinsic_global_atomic_add:
                  case nir_intrinsic_global_atomic_imin:
                  case nir_intrinsic_global_atomic_umin:
                  case nir_intrinsic_global_atomic_imax:
                  case nir_intrinsic_global_atomic_umax:
                  case nir_intrinsic_global_atomic_and:
                  case nir_intrinsic_global_atomic_or:
                  case nir_intrinsic_global_atomic_xor:
                  case nir_intrinsic_global_atomic_exchange:
                  case nir_intrinsic_global_atomic_comp_swap:
                  case nir_intrinsic_image_deref_atomic_add:
                  case nir_intrinsic_image_deref_atomic_umin:
                  case nir_intrinsic_image_deref_atomic_imin:
                  case nir_intrinsic_image_deref_atomic_umax:
                  case nir_intrinsic_image_deref_atomic_imax:
                  case nir_intrinsic_image_deref_atomic_and:
                  case nir_intrinsic_image_deref_atomic_or:
                  case nir_intrinsic_image_deref_atomic_xor:
                  case nir_intrinsic_image_deref_atomic_exchange:
                  case nir_intrinsic_image_deref_atomic_comp_swap:
                  case nir_intrinsic_image_deref_size:
                  case nir_intrinsic_shared_atomic_add:
                  case nir_intrinsic_shared_atomic_imin:
                  case nir_intrinsic_shared_atomic_umin:
                  case nir_intrinsic_shared_atomic_imax:
                  case nir_intrinsic_shared_atomic_umax:
                  case nir_intrinsic_shared_atomic_and:
                  case nir_intrinsic_shared_atomic_or:
                  case nir_intrinsic_shared_atomic_xor:
                  case nir_intrinsic_shared_atomic_exchange:
                  case nir_intrinsic_shared_atomic_comp_swap:
                  case nir_intrinsic_load_scratch:
                     type = RegType::vgpr;
                     break;
                  case nir_intrinsic_shuffle:
                  case nir_intrinsic_quad_broadcast:
                  case nir_intrinsic_quad_swap_horizontal:
                  case nir_intrinsic_quad_swap_vertical:
                  case nir_intrinsic_quad_swap_diagonal:
                  case nir_intrinsic_quad_swizzle_amd:
                  case nir_intrinsic_masked_swizzle_amd:
                  case nir_intrinsic_inclusive_scan:
                  case nir_intrinsic_exclusive_scan:
                     if (intrinsic->dest.ssa.bit_size == 1) {
                        size = lane_mask_size;
                        type = RegType::sgpr;
                     } else if (!ctx->divergent_vals[intrinsic->dest.ssa.index]) {
                        type = RegType::sgpr;
                     } else {
                        type = RegType::vgpr;
                     }
                     break;
                  case nir_intrinsic_load_view_index:
                     type = ctx->stage == fragment_fs ? RegType::vgpr : RegType::sgpr;
                     break;
                  case nir_intrinsic_load_front_face:
                  case nir_intrinsic_load_helper_invocation:
                  case nir_intrinsic_is_helper_invocation:
                     type = RegType::sgpr;
                     size = lane_mask_size;
                     break;
                  case nir_intrinsic_reduce:
                     if (intrinsic->dest.ssa.bit_size == 1) {
                        size = lane_mask_size;
                        type = RegType::sgpr;
                     } else if (!ctx->divergent_vals[intrinsic->dest.ssa.index]) {
                        type = RegType::sgpr;
                     } else {
                        type = RegType::vgpr;
                     }
                     break;
                  case nir_intrinsic_load_ubo:
                  case nir_intrinsic_load_ssbo:
                  case nir_intrinsic_load_global:
                  case nir_intrinsic_vulkan_resource_index:
                     type = ctx->divergent_vals[intrinsic->dest.ssa.index] ? RegType::vgpr : RegType::sgpr;
                     break;
                  /* due to copy propagation, the swizzled imov is removed if num dest components == 1 */
                  case nir_intrinsic_load_shared:
                     if (ctx->divergent_vals[intrinsic->dest.ssa.index])
                        type = RegType::vgpr;
                     else
                        type = RegType::sgpr;
                     break;
                  default:
                     for (unsigned i = 0; i < nir_intrinsic_infos[intrinsic->intrinsic].num_srcs; i++) {
                        if (allocated[intrinsic->src[i].ssa->index].type() == RegType::vgpr)
                           type = RegType::vgpr;
                     }
                     break;
               }
               allocated[intrinsic->dest.ssa.index] = Temp(0, RegClass(type, size));

               switch(intrinsic->intrinsic) {
                  case nir_intrinsic_load_barycentric_sample:
                  case nir_intrinsic_load_barycentric_pixel:
                  case nir_intrinsic_load_barycentric_centroid:
                  case nir_intrinsic_load_barycentric_at_sample:
                  case nir_intrinsic_load_barycentric_at_offset: {
                     glsl_interp_mode mode = (glsl_interp_mode)nir_intrinsic_interp_mode(intrinsic);
                     spi_ps_inputs |= get_interp_input(intrinsic->intrinsic, mode);
                     break;
                  }
                  case nir_intrinsic_load_front_face:
                     spi_ps_inputs |= S_0286CC_FRONT_FACE_ENA(1);
                     break;
                  case nir_intrinsic_load_frag_coord:
                  case nir_intrinsic_load_sample_pos: {
                     uint8_t mask = nir_ssa_def_components_read(&intrinsic->dest.ssa);
                     for (unsigned i = 0; i < 4; i++) {
                        if (mask & (1 << i))
                           spi_ps_inputs |= S_0286CC_POS_X_FLOAT_ENA(1) << i;

                     }
                     break;
                  }
                  case nir_intrinsic_load_sample_id:
                     spi_ps_inputs |= S_0286CC_ANCILLARY_ENA(1);
                     break;
                  case nir_intrinsic_load_sample_mask_in:
                     spi_ps_inputs |= S_0286CC_ANCILLARY_ENA(1);
                     spi_ps_inputs |= S_0286CC_SAMPLE_COVERAGE_ENA(1);
                     break;
                  default:
                     break;
               }
               break;
            }
            case nir_instr_type_tex: {
               nir_tex_instr* tex = nir_instr_as_tex(instr);
               unsigned size = tex->dest.ssa.num_components;

               if (tex->dest.ssa.bit_size == 64)
                  size *= 2;
               if (tex->op == nir_texop_texture_samples)
                  assert(!ctx->divergent_vals[tex->dest.ssa.index]);
               if (ctx->divergent_vals[tex->dest.ssa.index])
                  allocated[tex->dest.ssa.index] = Temp(0, RegClass(RegType::vgpr, size));
               else
                  allocated[tex->dest.ssa.index] = Temp(0, RegClass(RegType::sgpr, size));
               break;
            }
            case nir_instr_type_parallel_copy: {
               nir_foreach_parallel_copy_entry(entry, nir_instr_as_parallel_copy(instr)) {
                  allocated[entry->dest.ssa.index] = allocated[entry->src.ssa->index];
               }
               break;
            }
            case nir_instr_type_ssa_undef: {
               unsigned size = nir_instr_as_ssa_undef(instr)->def.num_components;
               if (nir_instr_as_ssa_undef(instr)->def.bit_size == 64)
                  size *= 2;
               allocated[nir_instr_as_ssa_undef(instr)->def.index] = Temp(0, RegClass(RegType::sgpr, size));
               break;
            }
            case nir_instr_type_phi: {
               nir_phi_instr* phi = nir_instr_as_phi(instr);
               RegType type;
               unsigned size = phi->dest.ssa.num_components;

               if (phi->dest.ssa.bit_size == 1) {
                  assert(size == 1 && "multiple components not yet supported on boolean phis.");
                  type = RegType::sgpr;
                  size *= lane_mask_size;
                  allocated[phi->dest.ssa.index] = Temp(0, RegClass(type, size));
                  break;
               }

               if (ctx->divergent_vals[phi->dest.ssa.index]) {
                  type = RegType::vgpr;
               } else {
                  type = RegType::sgpr;
                  nir_foreach_phi_src (src, phi) {
                     if (allocated[src->src.ssa->index].type() == RegType::vgpr)
                        type = RegType::vgpr;
                     if (allocated[src->src.ssa->index].type() == RegType::none)
                        done = false;
                  }
               }

               size *= phi->dest.ssa.bit_size == 64 ? 2 : 1;
               RegClass rc = RegClass(type, size);
               if (rc != allocated[phi->dest.ssa.index].regClass()) {
                  done = false;
               } else {
                  nir_foreach_phi_src(src, phi)
                     assert(allocated[src->src.ssa->index].size() == rc.size());
               }
               allocated[phi->dest.ssa.index] = Temp(0, rc);
               break;
            }
            default:
               break;
            }
         }
      }
   }

   if (G_0286CC_POS_W_FLOAT_ENA(spi_ps_inputs)) {
      /* If POS_W_FLOAT (11) is enabled, at least one of PERSP_* must be enabled too */
      spi_ps_inputs |= S_0286CC_PERSP_CENTER_ENA(1);
   }

   if (!(spi_ps_inputs & 0x7F)) {
      /* At least one of PERSP_* (0xF) or LINEAR_* (0x70) must be enabled */
      spi_ps_inputs |= S_0286CC_PERSP_CENTER_ENA(1);
   }

   ctx->program->config->spi_ps_input_ena = spi_ps_inputs;
   ctx->program->config->spi_ps_input_addr = spi_ps_inputs;

   for (unsigned i = 0; i < impl->ssa_alloc; i++)
      allocated[i] = Temp(ctx->program->allocateId(), allocated[i].regClass());

   ctx->allocated.reset(allocated.release());
   ctx->cf_info.nir_to_aco.reset(nir_to_aco.release());
}

Pseudo_instruction *add_startpgm(struct isel_context *ctx)
{
   unsigned arg_count = ctx->args->ac.arg_count;
   if (ctx->stage == fragment_fs) {
      /* LLVM optimizes away unused FS inputs and computes spi_ps_input_addr
       * itself and then communicates the results back via the ELF binary.
       * Mirror what LLVM does by re-mapping the VGPR arguments here.
       *
       * TODO: If we made the FS input scanning code into a separate pass that
       * could run before argument setup, then this wouldn't be necessary
       * anymore.
       */
      struct ac_shader_args *args = &ctx->args->ac;
      arg_count = 0;
      for (unsigned i = 0, vgpr_arg = 0, vgpr_reg = 0; i < args->arg_count; i++) {
         if (args->args[i].file != AC_ARG_VGPR) {
            arg_count++;
            continue;
         }

         if (!(ctx->program->config->spi_ps_input_addr & (1 << vgpr_arg))) {
            args->args[i].skip = true;
         } else {
            args->args[i].offset = vgpr_reg;
            vgpr_reg += args->args[i].size;
            arg_count++;
         }
         vgpr_arg++;
      }
   }

   aco_ptr<Pseudo_instruction> startpgm{create_instruction<Pseudo_instruction>(aco_opcode::p_startpgm, Format::PSEUDO, 0, arg_count + 1)};
   for (unsigned i = 0, arg = 0; i < ctx->args->ac.arg_count; i++) {
      if (ctx->args->ac.args[i].skip)
         continue;

      enum ac_arg_regfile file = ctx->args->ac.args[i].file;
      unsigned size = ctx->args->ac.args[i].size;
      unsigned reg = ctx->args->ac.args[i].offset;
      RegClass type = RegClass(file == AC_ARG_SGPR ? RegType::sgpr : RegType::vgpr, size);
      Temp dst = Temp{ctx->program->allocateId(), type};
      ctx->arg_temps[i] = dst;
      startpgm->definitions[arg] = Definition(dst);
      startpgm->definitions[arg].setFixed(PhysReg{file == AC_ARG_SGPR ? reg : reg + 256});
      arg++;
   }
   startpgm->definitions[arg_count] = Definition{ctx->program->allocateId(), exec, ctx->program->lane_mask};
   Pseudo_instruction *instr = startpgm.get();
   ctx->block->instructions.push_back(std::move(startpgm));

   /* Stash these in the program so that they can be accessed later when
    * handling spilling.
    */
   ctx->program->private_segment_buffer = get_arg(ctx, ctx->args->ring_offsets);
   ctx->program->scratch_offset = get_arg(ctx, ctx->args->scratch_offset);

   return instr;
}

int
type_size(const struct glsl_type *type, bool bindless)
{
   // TODO: don't we need type->std430_base_alignment() here?
   return glsl_count_attribute_slots(type, false);
}

void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
   *align = comp_size;
}

static bool
mem_vectorize_callback(unsigned align, unsigned bit_size,
                       unsigned num_components, unsigned high_offset,
                       nir_intrinsic_instr *low, nir_intrinsic_instr *high)
{
   if ((bit_size != 32 && bit_size != 64) || num_components > 4)
      return false;

   /* >128 bit loads are split except with SMEM */
   if (bit_size * num_components > 128)
      return false;

   switch (low->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_load_push_constant:
      return align % 4 == 0;
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
      assert(nir_src_as_deref(low->src[0])->mode == nir_var_mem_shared);
      /* fallthrough */
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
      if (bit_size * num_components > 64) /* 96 and 128 bit loads require 128 bit alignment and are split otherwise */
         return align % 16 == 0;
      else
         return align % 4 == 0;
   default:
      return false;
   }
   return false;
}

void
setup_vs_variables(isel_context *ctx, nir_shader *nir)
{
   nir_foreach_variable(variable, &nir->inputs)
   {
      variable->data.driver_location = variable->data.location * 4;
   }
   nir_foreach_variable(variable, &nir->outputs)
   {
      variable->data.driver_location = variable->data.location * 4;
   }

   radv_vs_output_info *outinfo = &ctx->program->info->vs.outinfo;

   memset(outinfo->vs_output_param_offset, AC_EXP_PARAM_UNDEFINED,
          sizeof(outinfo->vs_output_param_offset));

   ctx->needs_instance_id = ctx->program->info->vs.needs_instance_id;

   bool export_clip_dists = ctx->options->key.vs_common_out.export_clip_dists;

   outinfo->param_exports = 0;
   int pos_written = 0x1;
   if (outinfo->writes_pointsize || outinfo->writes_viewport_index || outinfo->writes_layer)
      pos_written |= 1 << 1;

   nir_foreach_variable(variable, &nir->outputs)
   {
      int idx = variable->data.location;
      unsigned slots = variable->type->count_attribute_slots(false);
      if (variable->data.compact) {
         unsigned component_count = variable->data.location_frac + variable->type->length;
         slots = (component_count + 3) / 4;
      }

      if (idx >= VARYING_SLOT_VAR0 || idx == VARYING_SLOT_LAYER || idx == VARYING_SLOT_PRIMITIVE_ID ||
          ((idx == VARYING_SLOT_CLIP_DIST0 || idx == VARYING_SLOT_CLIP_DIST1) && export_clip_dists)) {
         for (unsigned i = 0; i < slots; i++) {
            if (outinfo->vs_output_param_offset[idx + i] == AC_EXP_PARAM_UNDEFINED)
               outinfo->vs_output_param_offset[idx + i] = outinfo->param_exports++;
         }
      }
   }
   if (outinfo->writes_layer &&
       outinfo->vs_output_param_offset[VARYING_SLOT_LAYER] == AC_EXP_PARAM_UNDEFINED) {
      /* when ctx->options->key.has_multiview_view_index = true, the layer
       * variable isn't declared in NIR and it's isel's job to get the layer */
      outinfo->vs_output_param_offset[VARYING_SLOT_LAYER] = outinfo->param_exports++;
   }

   if (outinfo->export_prim_id) {
      assert(outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] == AC_EXP_PARAM_UNDEFINED);
      outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] = outinfo->param_exports++;
   }

   ctx->num_clip_distances = util_bitcount(outinfo->clip_dist_mask);
   ctx->num_cull_distances = util_bitcount(outinfo->cull_dist_mask);

   assert(ctx->num_clip_distances + ctx->num_cull_distances <= 8);

   if (ctx->num_clip_distances + ctx->num_cull_distances > 0)
      pos_written |= 1 << 2;
   if (ctx->num_clip_distances + ctx->num_cull_distances > 4)
      pos_written |= 1 << 3;

   outinfo->pos_exports = util_bitcount(pos_written);
}

void
setup_variables(isel_context *ctx, nir_shader *nir)
{
   switch (nir->info.stage) {
   case MESA_SHADER_FRAGMENT: {
      nir_foreach_variable(variable, &nir->outputs)
      {
         int idx = variable->data.location + variable->data.index;
         variable->data.driver_location = idx * 4;
      }
      break;
   }
   case MESA_SHADER_COMPUTE: {
      ctx->program->config->lds_size = (nir->info.cs.shared_size + ctx->program->lds_alloc_granule - 1) /
                                       ctx->program->lds_alloc_granule;
      break;
   }
   case MESA_SHADER_VERTEX: {
      setup_vs_variables(ctx, nir);
      break;
   }
   default:
      unreachable("Unhandled shader stage.");
   }
}

isel_context
setup_isel_context(Program* program,
                   unsigned shader_count,
                   struct nir_shader *const *shaders,
                   ac_shader_config* config,
                   struct radv_shader_args *args)
{
   program->stage = 0;
   for (unsigned i = 0; i < shader_count; i++) {
      switch (shaders[i]->info.stage) {
      case MESA_SHADER_VERTEX:
         program->stage |= sw_vs;
         break;
      case MESA_SHADER_TESS_CTRL:
         program->stage |= sw_tcs;
         break;
      case MESA_SHADER_TESS_EVAL:
         program->stage |= sw_tes;
         break;
      case MESA_SHADER_GEOMETRY:
         program->stage |= sw_gs;
         break;
      case MESA_SHADER_FRAGMENT:
         program->stage |= sw_fs;
         break;
      case MESA_SHADER_COMPUTE:
         program->stage |= sw_cs;
         break;
      default:
         unreachable("Shader stage not implemented");
      }
   }
   if (program->stage == sw_vs)
      program->stage |= hw_vs;
   else if (program->stage == sw_fs)
      program->stage |= hw_fs;
   else if (program->stage == sw_cs)
      program->stage |= hw_cs;
   else
      unreachable("Shader stage not implemented");

   program->config = config;
   program->info = args->shader_info;
   program->chip_class = args->options->chip_class;
   program->family = args->options->family;
   program->wave_size = args->shader_info->wave_size;
   program->lane_mask = program->wave_size == 32 ? s1 : s2;

   program->lds_alloc_granule = args->options->chip_class >= GFX7 ? 512 : 256;
   program->lds_limit = args->options->chip_class >= GFX7 ? 65536 : 32768;
   program->vgpr_limit = 256;
   program->vgpr_alloc_granule = 3;

   if (args->options->chip_class >= GFX10) {
      program->physical_sgprs = 2560; /* doesn't matter as long as it's at least 128 * 20 */
      program->sgpr_alloc_granule = 127;
      program->sgpr_limit = 106;
      program->vgpr_alloc_granule = program->wave_size == 32 ? 7 : 3;
   } else if (program->chip_class >= GFX8) {
      program->physical_sgprs = 800;
      program->sgpr_alloc_granule = 15;
      if (args->options->family == CHIP_TONGA || args->options->family == CHIP_ICELAND)
         program->sgpr_limit = 94; /* workaround hardware bug */
      else
         program->sgpr_limit = 102;
   } else {
      program->physical_sgprs = 512;
      program->sgpr_alloc_granule = 7;
      program->sgpr_limit = 104;
   }
   /* TODO: we don't have to allocate VCC if we don't need it */
   program->needs_vcc = true;

   isel_context ctx = {};
   ctx.program = program;
   ctx.args = args;
   ctx.options = args->options;
   ctx.stage = program->stage;

   for (unsigned i = 0; i < shader_count; i++) {
      nir_shader *nir = shaders[i];

      /* align and copy constant data */
      while (program->constant_data.size() % 4u)
         program->constant_data.push_back(0);
      ctx.constant_data_offset = program->constant_data.size();
      program->constant_data.insert(program->constant_data.end(),
                                    (uint8_t*)nir->constant_data,
                                    (uint8_t*)nir->constant_data + nir->constant_data_size);

      /* the variable setup has to be done before lower_io / CSE */
      setup_variables(&ctx, nir);

      /* optimize and lower memory operations */
      bool lower_to_scalar = false;
      bool lower_pack = false;
      if (nir_opt_load_store_vectorize(nir,
                                       (nir_variable_mode)(nir_var_mem_ssbo | nir_var_mem_ubo |
                                                           nir_var_mem_push_const | nir_var_mem_shared),
                                       mem_vectorize_callback)) {
         lower_to_scalar = true;
         lower_pack = true;
      }
      if (nir->info.stage != MESA_SHADER_COMPUTE)
         nir_lower_io(nir, (nir_variable_mode)(nir_var_shader_in | nir_var_shader_out), type_size, (nir_lower_io_options)0);
      nir_lower_explicit_io(nir, nir_var_mem_global, nir_address_format_64bit_global);

      if (lower_to_scalar)
         nir_lower_alu_to_scalar(nir, NULL, NULL);
      if (lower_pack)
         nir_lower_pack(nir);

      /* lower ALU operations */
      // TODO: implement logic64 in aco, it's more effective for sgprs
      nir_lower_int64(nir, nir->options->lower_int64_options);

      nir_opt_idiv_const(nir, 32);
      nir_lower_idiv(nir, nir_lower_idiv_precise);

      /* optimize the lowered ALU operations */
      bool more_algebraic = true;
      while (more_algebraic) {
         more_algebraic = false;
         NIR_PASS_V(nir, nir_copy_prop);
         NIR_PASS_V(nir, nir_opt_dce);
         NIR_PASS_V(nir, nir_opt_constant_folding);
         NIR_PASS(more_algebraic, nir, nir_opt_algebraic);
      }

      /* Do late algebraic optimization to turn add(a, neg(b)) back into
      * subs, then the mandatory cleanup after algebraic.  Note that it may
      * produce fnegs, and if so then we need to keep running to squash
      * fneg(fneg(a)).
      */
      bool more_late_algebraic = true;
      while (more_late_algebraic) {
         more_late_algebraic = false;
         NIR_PASS(more_late_algebraic, nir, nir_opt_algebraic_late);
         NIR_PASS_V(nir, nir_opt_constant_folding);
         NIR_PASS_V(nir, nir_copy_prop);
         NIR_PASS_V(nir, nir_opt_dce);
         NIR_PASS_V(nir, nir_opt_cse);
      }

      /* cleanup passes */
      nir_lower_load_const_to_scalar(nir);
      nir_opt_shrink_load(nir);
      nir_move_options move_opts = (nir_move_options)(
         nir_move_const_undef | nir_move_load_ubo | nir_move_load_input | nir_move_comparisons);
      nir_opt_sink(nir, move_opts);
      nir_opt_move(nir, move_opts);
      nir_convert_to_lcssa(nir, true, false);
      nir_lower_phis_to_scalar(nir);

      nir_function_impl *func = nir_shader_get_entrypoint(nir);
      nir_index_ssa_defs(func);
      nir_metadata_require(func, nir_metadata_block_index);

      if (args->options->dump_preoptir) {
         fprintf(stderr, "NIR shader before instruction selection:\n");
         nir_print_shader(nir, stderr);
      }
   }

   unsigned scratch_size = 0;
   for (unsigned i = 0; i < shader_count; i++)
      scratch_size = std::max(scratch_size, shaders[i]->scratch_size);
   ctx.program->config->scratch_bytes_per_wave = align(scratch_size * ctx.program->wave_size, 1024);

   ctx.block = ctx.program->create_and_insert_block();
   ctx.block->loop_nest_depth = 0;
   ctx.block->kind = block_kind_top_level;

   return ctx;
}

}
