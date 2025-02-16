/*
 * Copyright © 2015 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "util/u_debug.h"
#include "util/u_math.h"

#include "ir3_compiler.h"
#include "ir3_nir.h"
#include "ir3_shader.h"

/* For use by binning_pass shaders, where const_state is const, but expected
 * to be already set up when we compiled the corresponding non-binning variant
 */
nir_def *
ir3_get_shared_driver_ubo(nir_builder *b, const struct ir3_driver_ubo *ubo)
{
   assert(ubo->idx > 0);

   /* Binning shader shared ir3_driver_ubo definitions but not shader info */
   b->shader->info.num_ubos = MAX2(b->shader->info.num_ubos, ubo->idx + 1);
   return nir_imm_int(b, ubo->idx);
}

nir_def *
ir3_get_driver_ubo(nir_builder *b, struct ir3_driver_ubo *ubo)
{
   /* Pick a UBO index to use as our constant data.  Skip UBO 0 since that's
    * reserved for gallium's cb0.
    */
   if (ubo->idx == -1) {
      if (b->shader->info.num_ubos == 0)
         b->shader->info.num_ubos++;
      ubo->idx = b->shader->info.num_ubos++;
      return nir_imm_int(b, ubo->idx);
   }

   return ir3_get_shared_driver_ubo(b, ubo);
}

nir_def *
ir3_get_driver_consts_ubo(nir_builder *b, struct ir3_shader_variant *v)
{
   if (v->binning_pass)
      return ir3_get_shared_driver_ubo(b, &ir3_const_state(v)->consts_ubo);
   return ir3_get_driver_ubo(b, &ir3_const_state_mut(v)->consts_ubo);
}

static const struct glsl_type *
get_driver_ubo_type(const struct ir3_driver_ubo *ubo)
{
   return glsl_array_type(glsl_uint_type(), ubo->size, 0);
}

/* Create or update the size of a driver-ubo: */
void
ir3_update_driver_ubo(nir_shader *nir, const struct ir3_driver_ubo *ubo, const char *name)
{
   if (ubo->idx < 0)
      return;


   nir_foreach_variable_in_shader(var, nir) {
      if (var->data.mode != nir_var_mem_ubo)
         continue;
      if (var->data.binding != ubo->idx)
         continue;

      /* UBO already exists, make sure it is big enough: */
      if (glsl_array_size(var->type) < ubo->size)
         var->type = get_driver_ubo_type(ubo);
   }

   /* UBO variable does not exist yet, so create it: */
   nir_variable *var =
      nir_variable_create(nir, nir_var_mem_ubo, get_driver_ubo_type(ubo), name);
   var->data.driver_location = ubo->idx;
}

static nir_def *
load_driver_ubo(nir_builder *b, unsigned components, nir_def *ubo, unsigned offset)
{
   return nir_load_ubo(b, components, 32, ubo,
                       nir_imm_int(b, offset * sizeof(uint32_t)),
                       .align_mul = 16,
                       .align_offset = (offset % 4) * sizeof(uint32_t),
                       .range_base = offset * sizeof(uint32_t),
                       .range = components * sizeof(uint32_t));
}

/* For use by binning_pass shaders, where const_state is const, but expected
 * to be already set up when we compiled the corresponding non-binning variant
 */
nir_def *
ir3_load_shared_driver_ubo(nir_builder *b, unsigned components,
                           const struct ir3_driver_ubo *ubo,
                           unsigned offset)
{
   assert(ubo->size >= MAX2(ubo->size, offset + components));

   return load_driver_ubo(b, components, ir3_get_shared_driver_ubo(b, ubo), offset);
}

nir_def *
ir3_load_driver_ubo(nir_builder *b, unsigned components,
                    struct ir3_driver_ubo *ubo,
                    unsigned offset)
{
   ubo->size = MAX2(ubo->size, offset + components);

   return load_driver_ubo(b, components, ir3_get_driver_ubo(b, ubo), offset);
}

nir_def *
ir3_load_driver_ubo_indirect(nir_builder *b, unsigned components,
                             struct ir3_driver_ubo *ubo,
                             unsigned base, nir_def *offset,
                             unsigned range)
{
   assert(range > 0);
   ubo->size = MAX2(ubo->size, base + components + (range - 1) * 4);

   return nir_load_ubo(b, components, 32, ir3_get_driver_ubo(b, ubo),
                       nir_iadd(b, nir_imul24(b, offset, nir_imm_int(b, 16)),
                                nir_imm_int(b, base * sizeof(uint32_t))),
                       .align_mul = 16,
                       .align_offset = (base % 4) * sizeof(uint32_t),
                       .range_base = base * sizeof(uint32_t),
                       .range = components * sizeof(uint32_t) +
                        (range - 1) * 16);
}

static bool
ir3_nir_should_scalarize_mem(const nir_instr *instr, const void *data)
{
   const struct ir3_compiler *compiler = data;
   const nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   /* Scalarize load_ssbo's that we could otherwise lower to isam,
    * as the tex cache benefit outweighs the benefit of vectorizing
    * Don't do this if (vectorized) isam.v is supported.
    */
   if ((intrin->intrinsic == nir_intrinsic_load_ssbo) &&
       (nir_intrinsic_access(intrin) & ACCESS_CAN_REORDER) &&
       compiler->has_isam_ssbo && !compiler->has_isam_v) {
      return true;
   }

   if ((intrin->intrinsic == nir_intrinsic_load_ssbo &&
        intrin->def.bit_size == 8) ||
       (intrin->intrinsic == nir_intrinsic_store_ssbo &&
        intrin->src[0].ssa->bit_size == 8)) {
      return true;
   }

   return false;
}

static bool
ir3_nir_should_vectorize_mem(unsigned align_mul, unsigned align_offset,
                             unsigned bit_size, unsigned num_components,
                             int64_t hole_size, nir_intrinsic_instr *low,
                             nir_intrinsic_instr *high, void *data)
{
   if (hole_size > 0 || !nir_num_components_valid(num_components))
      return false;

   struct ir3_compiler *compiler = data;
   unsigned byte_size = bit_size / 8;

   if (low->intrinsic == nir_intrinsic_load_const_ir3)
      return bit_size <= 32 && num_components <= 4;

   if (low->intrinsic == nir_intrinsic_store_const_ir3)
      return bit_size == 32 && num_components <= 4;

   /* Don't vectorize load_ssbo's that we could otherwise lower to isam,
    * as the tex cache benefit outweighs the benefit of vectorizing. If we
    * support isam.v, we can vectorize this though.
    */
   if ((low->intrinsic == nir_intrinsic_load_ssbo) &&
       (nir_intrinsic_access(low) & ACCESS_CAN_REORDER) &&
       compiler->has_isam_ssbo && !compiler->has_isam_v) {
      return false;
   }

   if (low->intrinsic != nir_intrinsic_load_ubo) {
      return bit_size <= 32 && align_mul >= byte_size &&
         align_offset % byte_size == 0 &&
         num_components <= 4;
   }

   assert(bit_size >= 8);
   if (bit_size != 32)
      return false;

   int size = num_components * byte_size;

   /* Don't care about alignment past vec4. */
   assert(util_is_power_of_two_nonzero(align_mul));
   align_mul = MIN2(align_mul, 16);
   align_offset &= 15;

   /* Our offset alignment should aways be at least 4 bytes */
   if (align_mul < 4)
      return false;

   unsigned worst_start_offset = 16 - align_mul + align_offset;
   if (worst_start_offset + size > 16)
      return false;

   return true;
}

static unsigned
ir3_lower_bit_size(const nir_instr *instr, UNUSED void *data)
{
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);
      switch (intrinsic->intrinsic) {
      case nir_intrinsic_exclusive_scan:
      case nir_intrinsic_inclusive_scan:
      case nir_intrinsic_quad_broadcast:
      case nir_intrinsic_quad_swap_diagonal:
      case nir_intrinsic_quad_swap_horizontal:
      case nir_intrinsic_quad_swap_vertical:
      case nir_intrinsic_reduce:
         return intrinsic->def.bit_size == 8 ? 16 : 0;
      default:
         break;
      }
   }

   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      switch (alu->op) {
      case nir_op_iabs:
      case nir_op_iadd_sat:
      case nir_op_imax:
      case nir_op_imin:
      case nir_op_ineg:
      case nir_op_ishl:
      case nir_op_ishr:
      case nir_op_isub_sat:
      case nir_op_uadd_sat:
      case nir_op_umax:
      case nir_op_umin:
      case nir_op_ushr:
         return alu->def.bit_size == 8 ? 16 : 0;
      case nir_op_ieq:
      case nir_op_ige:
      case nir_op_ilt:
      case nir_op_ine:
      case nir_op_uge:
      case nir_op_ult:
         return nir_src_bit_size(alu->src[0].src) == 8 ? 16 : 0;
      default:
         break;
      }
   }

   return 0;
}

static void
ir3_get_variable_size_align_bytes(const glsl_type *type, unsigned *size, unsigned *align)
{
   switch (type->base_type) {
   case GLSL_TYPE_ARRAY:
   case GLSL_TYPE_INTERFACE:
   case GLSL_TYPE_STRUCT:
      glsl_size_align_handle_array_and_structs(type, ir3_get_variable_size_align_bytes,
                                               size, align);
      break;
   case GLSL_TYPE_UINT8:
   case GLSL_TYPE_INT8:
      /* 8-bit values are handled through 16-bit half-registers, so the resulting size
       * and alignment value has to be doubled to reflect the actual variable size
       * requirement.
       */
      *size = 2 * glsl_get_components(type);
      *align = 2;
      break;
   default:
      glsl_get_natural_size_align_bytes(type, size, align);
      break;
   }
}

#define OPT(nir, pass, ...)                                                    \
   ({                                                                          \
      bool this_progress = false;                                              \
      NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);                       \
      this_progress;                                                           \
   })

#define OPT_V(nir, pass, ...) NIR_PASS_V(nir, pass, ##__VA_ARGS__)

bool
ir3_optimize_loop(struct ir3_compiler *compiler,
                  const struct ir3_shader_nir_options *options,
                  nir_shader *s)
{
   MESA_TRACE_FUNC();

   bool progress;
   bool did_progress = false;
   unsigned lower_flrp = (s->options->lower_flrp16 ? 16 : 0) |
                         (s->options->lower_flrp32 ? 32 : 0) |
                         (s->options->lower_flrp64 ? 64 : 0);

   do {
      progress = false;

      OPT_V(s, nir_lower_vars_to_ssa);
      progress |= OPT(s, nir_lower_alu_to_scalar, NULL, NULL);
      progress |= OPT(s, nir_lower_phis_to_scalar, false);

      progress |= OPT(s, nir_copy_prop);
      progress |= OPT(s, nir_opt_deref);
      progress |= OPT(s, nir_opt_dce);
      progress |= OPT(s, nir_opt_cse);

      progress |= OPT(s, nir_opt_find_array_copies);
      progress |= OPT(s, nir_opt_copy_prop_vars);
      progress |= OPT(s, nir_opt_dead_write_vars);
      progress |= OPT(s, nir_split_struct_vars, nir_var_function_temp);

      static int gcm = -1;
      if (gcm == -1)
         gcm = debug_get_num_option("GCM", 0);
      if (gcm == 1)
         progress |= OPT(s, nir_opt_gcm, true);
      else if (gcm == 2)
         progress |= OPT(s, nir_opt_gcm, false);
      progress |= OPT(s, nir_opt_peephole_select, 16, true, true);
      progress |= OPT(s, nir_opt_intrinsics);
      /* NOTE: GS lowering inserts an output var with varying slot that
       * is larger than VARYING_SLOT_MAX (ie. GS_VERTEX_FLAGS_IR3),
       * which triggers asserts in nir_shader_gather_info().  To work
       * around that skip lowering phi precision for GS.
       *
       * Calling nir_shader_gather_info() late also seems to cause
       * problems for tess lowering, for now since we only enable
       * fp16/int16 for frag and compute, skip phi precision lowering
       * for other stages.
       */
      if ((s->info.stage == MESA_SHADER_FRAGMENT) ||
          (s->info.stage == MESA_SHADER_COMPUTE) ||
          (s->info.stage == MESA_SHADER_KERNEL)) {
         progress |= OPT(s, nir_opt_phi_precision);
      }
      progress |= OPT(s, nir_opt_algebraic);
      progress |= OPT(s, nir_lower_alu);
      progress |= OPT(s, nir_lower_pack);
      progress |= OPT(s, nir_lower_bit_size, ir3_lower_bit_size, NULL);
      progress |= OPT(s, nir_opt_constant_folding);

      const nir_opt_offsets_options offset_options = {
         /* How large an offset we can encode in the instr's immediate field.
          */
         .uniform_max = (1 << 9) - 1,

         /* STL/LDL have 13b for offset with MSB being a sign bit, but this opt
          * doesn't deal with negative offsets.
          */
         .shared_max = (1 << 12) - 1,

         .buffer_max = 0,
         .max_offset_cb = ir3_nir_max_imm_offset,
         .max_offset_data = compiler,
         .allow_offset_wrap = true,
      };
      progress |= OPT(s, nir_opt_offsets, &offset_options);

      nir_load_store_vectorize_options vectorize_opts = {
         .modes = nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_uniform,
         .callback = ir3_nir_should_vectorize_mem,
         .robust_modes = options->robust_modes,
         .cb_data = compiler,
      };
      progress |= OPT(s, nir_opt_load_store_vectorize, &vectorize_opts);

      if (lower_flrp != 0) {
         if (OPT(s, nir_lower_flrp, lower_flrp, false /* always_precise */)) {
            OPT(s, nir_opt_constant_folding);
            progress = true;
         }

         /* Nothing should rematerialize any flrps, so we only
          * need to do this lowering once.
          */
         lower_flrp = 0;
      }

      progress |= OPT(s, nir_opt_dead_cf);
      if (OPT(s, nir_opt_loop)) {
         progress |= true;
         /* If nir_opt_loop makes progress, then we need to clean
          * things up if we want any hope of nir_opt_if or nir_opt_loop_unroll
          * to make progress.
          */
         OPT(s, nir_copy_prop);
         OPT(s, nir_opt_dce);
      }
      progress |= OPT(s, nir_opt_if, nir_opt_if_optimize_phi_true_false);
      progress |= OPT(s, nir_opt_loop_unroll);
      progress |= OPT(s, nir_opt_remove_phis);
      progress |= OPT(s, nir_opt_undef);
      did_progress |= progress;
   } while (progress);

   OPT(s, nir_lower_var_copies);
   return did_progress;
}

static bool
should_split_wrmask(const nir_instr *instr, const void *data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_scratch:
      return true;
   default:
      return false;
   }
}

static bool
ir3_nir_lower_ssbo_size_filter(const nir_instr *instr, const void *data)
{
   return instr->type == nir_instr_type_intrinsic &&
          nir_instr_as_intrinsic(instr)->intrinsic ==
             nir_intrinsic_get_ssbo_size;
}

static nir_def *
ir3_nir_lower_ssbo_size_instr(nir_builder *b, nir_instr *instr, void *data)
{
   uint8_t ssbo_size_to_bytes_shift = *(uint8_t *) data;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   return nir_ishl_imm(b, &intr->def, ssbo_size_to_bytes_shift);
}

static bool
ir3_nir_lower_ssbo_size(nir_shader *s, uint8_t ssbo_size_to_bytes_shift)
{
   return nir_shader_lower_instructions(s, ir3_nir_lower_ssbo_size_filter,
                                        ir3_nir_lower_ssbo_size_instr,
                                        &ssbo_size_to_bytes_shift);
}

void
ir3_nir_lower_io_to_temporaries(nir_shader *s)
{
   /* Outputs consumed by the VPC, VS inputs, and FS outputs are all handled
    * by the hardware pre-loading registers at the beginning and then reading
    * them at the end, so we can't access them indirectly except through
    * normal register-indirect accesses, and therefore ir3 doesn't support
    * indirect accesses on those. Other i/o is lowered in ir3_nir_lower_tess,
    * and indirects work just fine for those. GS outputs may be consumed by
    * VPC, but have their own lowering in ir3_nir_lower_gs() which does
    * something similar to nir_lower_io_to_temporaries so we shouldn't need
    * to lower them.
    *
    * Note: this might be a little inefficient for VS or TES outputs which are
    * when the next stage isn't an FS, but it probably don't make sense to
    * depend on the next stage before variant creation.
    *
    * TODO: for gallium, mesa/st also does some redundant lowering, including
    * running this pass for GS inputs/outputs which we don't want but not
    * including TES outputs or FS inputs which we do need. We should probably
    * stop doing that once we're sure all drivers are doing their own
    * indirect i/o lowering.
    */
   bool lower_input = s->info.stage == MESA_SHADER_VERTEX ||
                      s->info.stage == MESA_SHADER_FRAGMENT;
   bool lower_output = s->info.stage != MESA_SHADER_TESS_CTRL &&
                       s->info.stage != MESA_SHADER_GEOMETRY;
   if (lower_input || lower_output) {
      NIR_PASS_V(s, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(s),
                 lower_output, lower_input);

      /* nir_lower_io_to_temporaries() creates global variables and copy
       * instructions which need to be cleaned up.
       */
      NIR_PASS_V(s, nir_split_var_copies);
      NIR_PASS_V(s, nir_lower_var_copies);
      NIR_PASS_V(s, nir_lower_global_vars_to_local);
   }

   /* Regardless of the above, we need to lower indirect references to
    * compact variables such as clip/cull distances because due to how
    * TCS<->TES IO works we cannot handle indirect accesses that "straddle"
    * vec4 components. nir_lower_indirect_derefs has a special case for
    * compact variables, so it will actually lower them even though we pass
    * in 0 modes.
    *
    * Using temporaries would be slightly better but
    * nir_lower_io_to_temporaries currently doesn't support TCS i/o.
    */
   NIR_PASS_V(s, nir_lower_indirect_derefs, 0, UINT32_MAX);
}

/**
 * Inserts an add of 0.5 to floating point array index values in texture coordinates.
 */
static bool
ir3_nir_lower_array_sampler_cb(struct nir_builder *b, nir_instr *instr, void *_data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   if (!tex->is_array || tex->op == nir_texop_lod)
      return false;

   int coord_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord_idx == -1 ||
       nir_tex_instr_src_type(tex, coord_idx) != nir_type_float)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   unsigned ncomp = tex->coord_components;
   nir_def *src = tex->src[coord_idx].src.ssa;

   assume(ncomp >= 1);
   nir_def *ai = nir_channel(b, src, ncomp - 1);
   ai = nir_fadd_imm(b, ai, 0.5);
   nir_src_rewrite(&tex->src[coord_idx].src,
                   nir_vector_insert_imm(b, src, ai, ncomp - 1));
   return true;
}

static bool
ir3_nir_lower_array_sampler(nir_shader *shader)
{
   return nir_shader_instructions_pass(
      shader, ir3_nir_lower_array_sampler_cb,
      nir_metadata_control_flow, NULL);
}

void
ir3_finalize_nir(struct ir3_compiler *compiler,
                 const struct ir3_shader_nir_options *options,
                 nir_shader *s)
{
   MESA_TRACE_FUNC();

   struct nir_lower_tex_options tex_options = {
      .lower_rect = 0,
      .lower_tg4_offsets = true,
      .lower_invalid_implicit_lod = true,
      .lower_index_to_offset = true,
   };

   if (compiler->gen >= 4) {
      /* a4xx seems to have *no* sam.p */
      tex_options.lower_txp = ~0; /* lower all txp */
   } else {
      /* a3xx just needs to avoid sam.p for 3d tex */
      tex_options.lower_txp = (1 << GLSL_SAMPLER_DIM_3D);
   }

   if (ir3_shader_debug & IR3_DBG_DISASM) {
      mesa_logi("----------------------");
      nir_log_shaderi(s);
      mesa_logi("----------------------");
   }

   if (s->info.stage == MESA_SHADER_GEOMETRY)
      NIR_PASS_V(s, ir3_nir_lower_gs);

   NIR_PASS_V(s, nir_lower_frexp);
   NIR_PASS_V(s, nir_lower_amul, ir3_glsl_type_size);

   OPT_V(s, nir_lower_wrmasks, should_split_wrmask, s);

   OPT_V(s, nir_lower_tex, &tex_options);
   OPT_V(s, nir_lower_load_const_to_scalar);

   if (compiler->array_index_add_half)
      OPT_V(s, ir3_nir_lower_array_sampler);

   OPT_V(s, nir_lower_is_helper_invocation);

   ir3_optimize_loop(compiler, options, s);

   /* do idiv lowering after first opt loop to get a chance to propagate
    * constants for divide by immed power-of-two:
    */
   nir_lower_idiv_options idiv_options = {
      .allow_fp16 = true,
   };
   bool idiv_progress = OPT(s, nir_opt_idiv_const, 8);
   idiv_progress |= OPT(s, nir_lower_idiv, &idiv_options);

   if (idiv_progress)
      ir3_optimize_loop(compiler, options, s);

   OPT_V(s, nir_remove_dead_variables, nir_var_function_temp, NULL);

   if (ir3_shader_debug & IR3_DBG_DISASM) {
      mesa_logi("----------------------");
      nir_log_shaderi(s);
      mesa_logi("----------------------");
   }

   /* st_program.c's parameter list optimization requires that future nir
    * variants don't reallocate the uniform storage, so we have to remove
    * uniforms that occupy storage.  But we don't want to remove samplers,
    * because they're needed for YUV variant lowering.
    */
   nir_foreach_uniform_variable_safe (var, s) {
      if (var->data.mode == nir_var_uniform &&
          (glsl_type_get_image_count(var->type) ||
           glsl_type_get_sampler_count(var->type)))
         continue;

      exec_node_remove(&var->node);
   }
   nir_validate_shader(s, "after uniform var removal");

   nir_sweep(s);
}

static bool
lower_subgroup_id_filter(const nir_instr *instr, const void *unused)
{
   (void)unused;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   return intr->intrinsic == nir_intrinsic_load_subgroup_invocation ||
          intr->intrinsic == nir_intrinsic_load_subgroup_id ||
          intr->intrinsic == nir_intrinsic_load_num_subgroups;
}

static nir_def *
lower_subgroup_id(nir_builder *b, nir_instr *instr, void *_shader)
{
   struct ir3_shader *shader = _shader;

   /* Vulkan allows implementations to tile workgroup invocations even when
    * subgroup operations are involved, which is implied by this Note:
    *
    *    "There is no direct relationship between SubgroupLocalInvocationId and
    *    LocalInvocationId or LocalInvocationIndex."
    *
    * However there is no way to get SubgroupId directly, so we have to use
    * LocalInvocationIndex here. This means that whenever we do this lowering we
    * have to force linear dispatch to make sure that the relation between
    * SubgroupId/SubgroupLocalInvocationId and LocalInvocationIndex is what we
    * expect, unless the shader forces us to do the quad layout in which case we
    * have to use the tiled layout.
    */
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic == nir_intrinsic_load_subgroup_id &&
       shader->nir->info.derivative_group == DERIVATIVE_GROUP_QUADS) {
      /* We have to manually figure out which subgroup we're in using the
       * tiling. The tiling is 4x4, unless one of the dimensions is not a
       * multiple of 4 in which case it drops to 2.
       */
      nir_def *local_size = nir_load_workgroup_size(b);
      nir_def *local_size_x = nir_channel(b, local_size, 0);
      nir_def *local_size_y = nir_channel(b, local_size, 1);
      /* Calculate the shift from invocation to tile index for x and y */
      nir_def *x_shift = nir_bcsel(b,
                                   nir_ieq_imm(b,
                                               nir_iand_imm(b, local_size_x, 3),
                                               0),
                                   nir_imm_int(b, 2), nir_imm_int(b, 1));
      nir_def *y_shift = nir_bcsel(b,
                                   nir_ieq_imm(b,
                                               nir_iand_imm(b, local_size_y, 3),
                                               0),
                                   nir_imm_int(b, 2), nir_imm_int(b, 1));
      nir_def *id = nir_load_local_invocation_id(b);
      nir_def *id_x = nir_channel(b, id, 0);
      nir_def *id_y = nir_channel(b, id, 1);
      /* Calculate which tile we're in */
      nir_def *tile_id =
         nir_iadd(b, nir_imul24(b, nir_ishr(b, id_y, y_shift),
                                nir_ishr(b, local_size_x, x_shift)),
                  nir_ishr(b, id_x, x_shift));
      /* Finally calculate the subgroup id */
      return nir_ishr(b, tile_id, nir_isub(b,
                                           nir_load_subgroup_id_shift_ir3(b),
                                           nir_iadd(b, x_shift, y_shift)));
   }

   /* Just use getfiberid if we have to use tiling */
   if (intr->intrinsic == nir_intrinsic_load_subgroup_invocation &&
       shader->nir->info.derivative_group == DERIVATIVE_GROUP_QUADS) {
      return NULL;
   }


   if (intr->intrinsic == nir_intrinsic_load_subgroup_invocation) {
      shader->cs.force_linear_dispatch = true;
      return nir_iand(
         b, nir_load_local_invocation_index(b),
         nir_iadd_imm(b, nir_load_subgroup_size(b), -1));
   } else if (intr->intrinsic == nir_intrinsic_load_subgroup_id) {
      shader->cs.force_linear_dispatch = true;
      return nir_ishr(b, nir_load_local_invocation_index(b),
                      nir_load_subgroup_id_shift_ir3(b));
   } else {
      assert(intr->intrinsic == nir_intrinsic_load_num_subgroups);
      /* If the workgroup size is constant,
       * nir_lower_compute_system_values() will replace local_size with a
       * constant so this can mostly be constant folded away.
       */
      nir_def *local_size = nir_load_workgroup_size(b);
      nir_def *size =
         nir_imul24(b, nir_channel(b, local_size, 0),
                    nir_imul24(b, nir_channel(b, local_size, 1),
                               nir_channel(b, local_size, 2)));
      nir_def *one = nir_imm_int(b, 1);
      return nir_iadd(b, one,
                      nir_ishr(b, nir_isub(b, size, one),
                               nir_load_subgroup_id_shift_ir3(b)));
   }
}

static bool
ir3_nir_lower_subgroup_id_cs(nir_shader *nir, struct ir3_shader *shader)
{
   return nir_shader_lower_instructions(nir, lower_subgroup_id_filter,
                                        lower_subgroup_id, shader);
}

/**
 * Late passes that need to be done after pscreen->finalize_nir()
 */
void
ir3_nir_post_finalize(struct ir3_shader *shader)
{
   struct nir_shader *s = shader->nir;
   struct ir3_compiler *compiler = shader->compiler;

   MESA_TRACE_FUNC();

   NIR_PASS_V(s, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
              ir3_glsl_type_size, nir_lower_io_lower_64bit_to_32 |
              nir_lower_io_use_interpolated_input_intrinsics);

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      /* NOTE: lower load_barycentric_at_sample first, since it
       * produces load_barycentric_at_offset:
       */
      NIR_PASS_V(s, ir3_nir_lower_load_barycentric_at_sample);
      NIR_PASS_V(s, ir3_nir_lower_load_barycentric_at_offset);
      NIR_PASS_V(s, ir3_nir_move_varying_inputs);
      NIR_PASS_V(s, nir_lower_fb_read);
      NIR_PASS_V(s, ir3_nir_lower_layer_id);
      NIR_PASS_V(s, ir3_nir_lower_frag_shading_rate);
   }

   if (s->info.stage == MESA_SHADER_VERTEX || s->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS_V(s, ir3_nir_lower_primitive_shading_rate);
   }

   if (compiler->gen >= 6 && s->info.stage == MESA_SHADER_FRAGMENT &&
       !(ir3_shader_debug & IR3_DBG_NOFP16)) {
      /* Lower FS mediump inputs to 16-bit. If you declared it mediump, you
       * probably want 16-bit instructions (and have set
       * mediump/RelaxedPrecision on most of the rest of the shader's
       * instructions).  If we don't lower it in NIR, then comparisons of the
       * results of mediump ALU ops with the mediump input will happen in highp,
       * causing extra conversions (and, incidentally, causing
       * dEQP-GLES2.functional.shaders.algorithm.rgb_to_hsl_fragment on ANGLE to
       * fail)
       *
       * However, we can't do flat inputs because flat.b doesn't have the
       * destination type for how to downconvert the
       * 32-bit-in-the-varyings-interpolator value. (also, even if it did, watch
       * out for how gl_nir_lower_packed_varyings packs all flat-interpolated
       * things together as ivec4s, so when we lower a formerly-float input
       * you'd end up with an incorrect f2f16(i2i32(load_input())) instead of
       * load_input).
       */
      uint64_t mediump_varyings = 0;
      nir_foreach_shader_in_variable(var, s) {
         if ((var->data.precision == GLSL_PRECISION_MEDIUM ||
              var->data.precision == GLSL_PRECISION_LOW) &&
             var->data.interpolation != INTERP_MODE_FLAT) {
            mediump_varyings |= BITFIELD64_BIT(var->data.location);
         }
      }

      if (mediump_varyings) {
         NIR_PASS_V(s, nir_lower_mediump_io,
                  nir_var_shader_in,
                  mediump_varyings,
                  false);
      }

      /* This should come after input lowering, to opportunistically lower non-mediump outputs. */
      NIR_PASS_V(s, nir_lower_mediump_io, nir_var_shader_out, 0, false);
   }

   {
      /* If the API-facing subgroup size is forced to a particular value, lower
       * it here. Beyond this point nir_intrinsic_load_subgroup_size will return
       * the "real" subgroup size.
       */
      unsigned subgroup_size = 0, max_subgroup_size = 0;
      ir3_shader_get_subgroup_size(compiler, &shader->options, s->info.stage,
                                   &subgroup_size, &max_subgroup_size);

      nir_lower_subgroups_options options = {
            .subgroup_size = subgroup_size,
            .ballot_bit_size = 32,
            .ballot_components = max_subgroup_size / 32,
            .lower_to_scalar = true,
            .lower_vote_eq = true,
            .lower_vote_bool_eq = true,
            .lower_subgroup_masks = true,
            .lower_read_invocation_to_cond = true,
            .lower_shuffle = !compiler->has_shfl,
            .lower_relative_shuffle = !compiler->has_shfl,
            .lower_rotate_to_shuffle = !compiler->has_shfl,
            .lower_rotate_clustered_to_shuffle = true,
            .lower_inverse_ballot = true,
            .lower_reduce = true,
            .filter = ir3_nir_lower_subgroups_filter,
            .filter_data = compiler,
      };

      if (!((s->info.stage == MESA_SHADER_COMPUTE) ||
            (s->info.stage == MESA_SHADER_KERNEL) ||
            compiler->has_getfiberid)) {
         options.subgroup_size = 1;
         options.lower_vote_trivial = true;
      }

      OPT(s, nir_lower_subgroups, &options);
      OPT(s, ir3_nir_lower_shuffle, shader);
   }

   if ((s->info.stage == MESA_SHADER_COMPUTE) ||
       (s->info.stage == MESA_SHADER_KERNEL)) {
      bool progress = false;
      NIR_PASS(progress, s, ir3_nir_lower_subgroup_id_cs, shader);

      if (s->info.derivative_group == DERIVATIVE_GROUP_LINEAR)
         shader->cs.force_linear_dispatch = true;

      /* ir3_nir_lower_subgroup_id_cs creates extra compute intrinsics which
       * we need to lower again.
       */
      if (progress)
         NIR_PASS_V(s, nir_lower_compute_system_values, NULL);
   }

   /* we cannot ensure that ir3_finalize_nir() is only called once, so
    * we also need to do any run-once workarounds here:
    */
   OPT_V(s, ir3_nir_apply_trig_workarounds);

   const nir_lower_image_options lower_image_opts = {
      .lower_cube_size = true,
      .lower_image_samples_to_one = true
   };
   NIR_PASS_V(s, nir_lower_image, &lower_image_opts);

   const nir_lower_idiv_options lower_idiv_options = {
      .allow_fp16 = true,
   };
   NIR_PASS_V(s, nir_lower_idiv, &lower_idiv_options); /* idiv generated by cube lowering */


   /* The resinfo opcode returns the size in dwords on a4xx */
   if (compiler->gen == 4)
      OPT_V(s, ir3_nir_lower_ssbo_size, 2);

   /* The resinfo opcode we have for getting the SSBO size on a6xx returns a
    * byte length divided by IBO_0_FMT, while the NIR intrinsic coming in is a
    * number of bytes. Switch things so the NIR intrinsic in our backend means
    * dwords.
    */
   if (compiler->gen >= 6)
      OPT_V(s, ir3_nir_lower_ssbo_size, compiler->options.storage_16bit ? 1 : 2);

   ir3_optimize_loop(compiler, &shader->options.nir_options, s);
}

static bool
lower_ucp_vs(struct ir3_shader_variant *so)
{
   if (!so->key.ucp_enables)
      return false;

   gl_shader_stage last_geom_stage;

   if (so->key.has_gs) {
      last_geom_stage = MESA_SHADER_GEOMETRY;
   } else if (so->key.tessellation) {
      last_geom_stage = MESA_SHADER_TESS_EVAL;
   } else {
      last_geom_stage = MESA_SHADER_VERTEX;
   }

   return so->type == last_geom_stage;
}

static bool
output_slot_used_for_binning(gl_varying_slot slot)
{
   return slot == VARYING_SLOT_POS || slot == VARYING_SLOT_PSIZ ||
          slot == VARYING_SLOT_CLIP_DIST0 || slot == VARYING_SLOT_CLIP_DIST1 ||
          slot == VARYING_SLOT_VIEWPORT;
}

static bool
remove_nonbinning_output(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output &&
       intr->intrinsic != nir_intrinsic_store_per_view_output)
      return false;

   nir_io_semantics io = nir_intrinsic_io_semantics(intr);

   if (output_slot_used_for_binning(io.location))
      return false;

   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_binning(nir_shader *s)
{
   return nir_shader_intrinsics_pass(s, remove_nonbinning_output,
                                     nir_metadata_control_flow, NULL);
}

nir_mem_access_size_align
ir3_mem_access_size_align(nir_intrinsic_op intrin, uint8_t bytes,
                 uint8_t bit_size, uint32_t align,
                 uint32_t align_offset, bool offset_is_const,
                 enum gl_access_qualifier access, const void *cb_data)
{
   align = nir_combined_align(align, align_offset);
   assert(util_is_power_of_two_nonzero(align));

   /* But if we're only aligned to 1 byte, use 8-bit loads. If we're only
    * aligned to 2 bytes, use 16-bit loads, unless we needed 8-bit loads due to
    * the size.
    */
   if ((bytes & 1) || (align == 1))
      bit_size = 8;
   else if ((bytes & 2) || (align == 2))
      bit_size = 16;
   else if (bit_size >= 32)
      bit_size = 32;

   if (intrin == nir_intrinsic_load_ubo)
      bit_size = 32;

   return (nir_mem_access_size_align){
      .num_components = MAX2(1, MIN2(bytes / (bit_size / 8), 4)),
      .bit_size = bit_size,
      .align = bit_size / 8,
      .shift = nir_mem_access_shift_method_scalar,
   };
}

static bool
atomic_supported(const nir_instr * instr, const void * data)
{
   /* No atomic 64b arithmetic is supported in A7XX so far */
   return nir_instr_as_intrinsic(instr)->def.bit_size != 64;
}

void
ir3_nir_lower_variant(struct ir3_shader_variant *so,
                      const struct ir3_shader_nir_options *options,
                      nir_shader *s)
{
   MESA_TRACE_FUNC();

   if (ir3_shader_debug & IR3_DBG_DISASM) {
      mesa_logi("----------------------");
      nir_log_shaderi(s);
      mesa_logi("----------------------");
   }

   bool progress = false;

   progress |= OPT(s, nir_lower_io_to_scalar, nir_var_mem_ssbo,
                   ir3_nir_should_scalarize_mem, so->compiler);

   if (so->key.has_gs || so->key.tessellation) {
      switch (so->type) {
      case MESA_SHADER_VERTEX:
         NIR_PASS_V(s, ir3_nir_lower_to_explicit_output, so,
                    so->key.tessellation);
         progress = true;
         break;
      case MESA_SHADER_TESS_CTRL:
         NIR_PASS_V(s, nir_lower_io_to_scalar,
                     nir_var_shader_in | nir_var_shader_out, NULL, NULL);
         NIR_PASS_V(s, ir3_nir_lower_tess_ctrl, so, so->key.tessellation);
         NIR_PASS_V(s, ir3_nir_lower_to_explicit_input, so);
         progress = true;
         break;
      case MESA_SHADER_TESS_EVAL:
         NIR_PASS_V(s, ir3_nir_lower_tess_eval, so, so->key.tessellation);
         if (so->key.has_gs)
            NIR_PASS_V(s, ir3_nir_lower_to_explicit_output, so,
                       so->key.tessellation);
         progress = true;
         break;
      case MESA_SHADER_GEOMETRY:
         NIR_PASS_V(s, ir3_nir_lower_to_explicit_input, so);
         progress = true;
         break;
      default:
         break;
      }
   }

   /* Note that it is intentional to use the VS lowering pass for GS, since we
    * lower GS into something that looks more like a VS in ir3_nir_lower_gs():
    */
   if (lower_ucp_vs(so)) {
      progress |= OPT(s, nir_lower_clip_vs, so->key.ucp_enables, false, true, NULL);
   } else if (s->info.stage == MESA_SHADER_FRAGMENT) {
      if (so->key.ucp_enables && !so->compiler->has_clip_cull)
         progress |= OPT(s, nir_lower_clip_fs, so->key.ucp_enables, true, true);
   }

   if (so->binning_pass) {
      if (OPT(s, lower_binning)) {
         progress = true;

         /* outputs_written has changed. */
         nir_shader_gather_info(s, nir_shader_get_entrypoint(s));
      }
   }

   /* Move large constant variables to the constants attached to the NIR
    * shader, which we will upload in the immediates range.  This generates
    * amuls, so we need to clean those up after.
    *
    * Passing no size_align, we would get packed values, which if we end up
    * having to load with LDC would result in extra reads to unpack from
    * straddling loads.  Align everything to vec4 to avoid that, though we
    * could theoretically do better.
    */
   OPT_V(s, nir_opt_large_constants, glsl_get_vec4_size_align_bytes,
         32 /* bytes */);
   progress |= OPT(s, ir3_nir_lower_load_constant, so);

   /* Lower large temporaries to scratch, which in Qualcomm terms is private
    * memory, to avoid excess register pressure. This should happen after
    * nir_opt_large_constants, because loading from a UBO is much, much less
    * expensive.
    */
   if (so->compiler->has_pvtmem) {
      progress |= OPT(s, nir_lower_vars_to_scratch, nir_var_function_temp,
                      16 * 16 /* bytes */,
                      ir3_get_variable_size_align_bytes, glsl_get_natural_size_align_bytes);
   }

   /* Lower scratch writemasks */
   progress |= OPT(s, nir_lower_wrmasks, should_split_wrmask, s);
   progress |= OPT(s, nir_lower_atomics, atomic_supported);

   if (OPT(s, nir_lower_locals_to_regs, 1)) {
      progress = true;

      /* Split 64b registers into two 32b ones. */
      OPT_V(s, ir3_nir_lower_64b_regs);
   }

   nir_lower_mem_access_bit_sizes_options mem_bit_size_options = {
      .modes = nir_var_mem_constant | nir_var_mem_ubo |
               nir_var_mem_global | nir_var_mem_shared |
               nir_var_function_temp | nir_var_mem_ssbo,
      .callback = ir3_mem_access_size_align,
   };

   progress |= OPT(s, nir_lower_mem_access_bit_sizes, &mem_bit_size_options);
   progress |= OPT(s, ir3_nir_lower_64b_global);
   progress |= OPT(s, ir3_nir_lower_64b_undef);
   progress |= OPT(s, nir_lower_int64);
   progress |= OPT(s, ir3_nir_lower_64b_intrinsics);
   progress |= OPT(s, nir_lower_64bit_phis);

   /* Cleanup code leftover from lowering passes before opt_preamble */
   if (progress) {
      progress |= OPT(s, nir_opt_constant_folding);
   }

   progress |= OPT(s, ir3_nir_opt_subgroups, so);

   if (so->compiler->load_shader_consts_via_preamble)
      progress |= OPT(s, ir3_nir_lower_driver_params_to_ubo, so);

   if (!so->binning_pass) {
      ir3_setup_const_state(s, so, ir3_const_state_mut(so));
   }

   /* Do the preamble before analysing UBO ranges, because it's usually
    * higher-value and because it can result in eliminating some indirect UBO
    * accesses where otherwise we'd have to push the whole range. However we
    * have to lower the preamble after UBO lowering so that UBO lowering can
    * insert instructions in the preamble to push UBOs.
    */
   if (so->compiler->has_preamble &&
       !(ir3_shader_debug & IR3_DBG_NOPREAMBLE))
      progress |= OPT(s, ir3_nir_opt_preamble, so);

   if (so->compiler->load_shader_consts_via_preamble)
      progress |= OPT(s, ir3_nir_lower_driver_params_to_ubo, so);

   /* TODO: ldg.k might also work on a6xx */
   if (so->compiler->gen >= 7)
      progress |= OPT(s, ir3_nir_lower_const_global_loads, so);

   if (!so->binning_pass)
      OPT_V(s, ir3_nir_analyze_ubo_ranges, so);

   progress |= OPT(s, ir3_nir_lower_ubo_loads, so);

   if (so->compiler->gen >= 7 &&
       !(ir3_shader_debug & (IR3_DBG_NOPREAMBLE | IR3_DBG_NODESCPREFETCH)))
      progress |= OPT(s, ir3_nir_opt_prefetch_descriptors, so);

   if (so->shader_options.push_consts_type == IR3_PUSH_CONSTS_SHARED_PREAMBLE)
      progress |= OPT(s, ir3_nir_lower_push_consts_to_preamble, so);

   progress |= OPT(s, ir3_nir_lower_preamble, so);

   progress |= OPT(s, nir_lower_amul, ir3_glsl_type_size);

   /* UBO offset lowering has to come after we've decided what will
    * be left as load_ubo
    */
   if (so->compiler->gen >= 6)
      progress |= OPT(s, nir_lower_ubo_vec4);

   progress |= OPT(s, ir3_nir_lower_io_offsets);

   if (!so->binning_pass) {
      ir3_const_alloc_all_reserved_space(&ir3_const_state_mut(so)->allocs);
   }

   if (progress)
      ir3_optimize_loop(so->compiler, options, s);

   /* verify that progress is always set */
   assert(!ir3_optimize_loop(so->compiler, options, s));

   /* Fixup indirect load_const_ir3's which end up with a const base offset
    * which is too large to encode.  Do this late(ish) so we actually
    * can differentiate indirect vs non-indirect.
    */
   if (OPT(s, ir3_nir_fixup_load_const_ir3))
      ir3_optimize_loop(so->compiler, options, s);

   /* Do late algebraic optimization to turn add(a, neg(b)) back into
    * subs, then the mandatory cleanup after algebraic.  Note that it may
    * produce fnegs, and if so then we need to keep running to squash
    * fneg(fneg(a)).
    */
   bool more_late_algebraic = true;
   while (more_late_algebraic) {
      more_late_algebraic = OPT(s, nir_opt_algebraic_late);
      if (!more_late_algebraic && so->compiler->gen >= 5) {
         /* Lowers texture operations that have only f2f16 or u2u16 called on
          * them to have a 16-bit destination.  Also, lower 16-bit texture
          * coordinates that had been upconverted to 32-bits just for the
          * sampler to just be 16-bit texture sources.
          */
         struct nir_opt_tex_srcs_options opt_srcs_options = {
            .sampler_dims = ~0,
            .src_types = (1 << nir_tex_src_coord) |
                         (1 << nir_tex_src_lod) |
                         (1 << nir_tex_src_bias) |
                         (1 << nir_tex_src_offset) |
                         (1 << nir_tex_src_comparator) |
                         (1 << nir_tex_src_min_lod) |
                         (1 << nir_tex_src_ms_index) |
                         (1 << nir_tex_src_ddx) |
                         (1 << nir_tex_src_ddy),
         };
         struct nir_opt_16bit_tex_image_options opt_16bit_options = {
            .rounding_mode = nir_rounding_mode_rtz,
            .opt_tex_dest_types = nir_type_float,
            /* blob dumps have no half regs on pixel 2's ldib or stib, so only enable for a6xx+. */
            .opt_image_dest_types = so->compiler->gen >= 6 ?
                                        nir_type_float | nir_type_uint | nir_type_int : 0,
            .opt_image_store_data = so->compiler->gen >= 6,
            .opt_srcs_options_count = 1,
            .opt_srcs_options = &opt_srcs_options,
         };
         OPT(s, nir_opt_16bit_tex_image, &opt_16bit_options);
      }
      OPT_V(s, nir_opt_constant_folding);
      OPT_V(s, nir_copy_prop);
      OPT_V(s, nir_opt_dce);
      OPT_V(s, nir_opt_cse);
   }

   OPT_V(s, nir_opt_sink, nir_move_const_undef);

   if (ir3_shader_debug & IR3_DBG_DISASM) {
      mesa_logi("----------------------");
      nir_log_shaderi(s);
      mesa_logi("----------------------");
   }

   nir_sweep(s);
}

bool
ir3_get_driver_param_info(const nir_shader *shader, nir_intrinsic_instr *intr,
                          struct driver_param_info *param_info)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_base_workgroup_id:
      param_info->offset = IR3_DP_CS(base_group_x);
      break;
   case nir_intrinsic_load_num_workgroups:
      param_info->offset = IR3_DP_CS(num_work_groups_x);
      break;
   case nir_intrinsic_load_workgroup_size:
      param_info->offset = IR3_DP_CS(local_group_size_x);
      break;
   case nir_intrinsic_load_subgroup_size:
      if (shader->info.stage == MESA_SHADER_COMPUTE) {
         param_info->offset = IR3_DP_CS(subgroup_size);
      } else if (shader->info.stage == MESA_SHADER_FRAGMENT) {
         param_info->offset = IR3_DP_FS(subgroup_size);
      } else {
         return false;
      }
      break;
   case nir_intrinsic_load_subgroup_id_shift_ir3:
      param_info->offset = IR3_DP_CS(subgroup_id_shift);
      break;
   case nir_intrinsic_load_work_dim:
      param_info->offset = IR3_DP_CS(work_dim);
      break;
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_first_vertex:
      param_info->offset = IR3_DP_VS(vtxid_base);
      break;
   case nir_intrinsic_load_is_indexed_draw:
      param_info->offset = IR3_DP_VS(is_indexed_draw);
      break;
   case nir_intrinsic_load_draw_id:
      param_info->offset = IR3_DP_VS(draw_id);
      break;
   case nir_intrinsic_load_base_instance:
      param_info->offset = IR3_DP_VS(instid_base);
      break;
   case nir_intrinsic_load_user_clip_plane: {
      uint32_t idx = nir_intrinsic_ucp_id(intr);
      param_info->offset = IR3_DP_VS(ucp[0].x) + 4 * idx;
      break;
   }
   case nir_intrinsic_load_tess_level_outer_default:
      param_info->offset = IR3_DP_TCS(default_outer_level_x);
      break;
   case nir_intrinsic_load_tess_level_inner_default:
      param_info->offset = IR3_DP_TCS(default_inner_level_x);
      break;
   case nir_intrinsic_load_frag_size_ir3:
      param_info->offset = IR3_DP_FS(frag_size);
      break;
   case nir_intrinsic_load_frag_offset_ir3:
      param_info->offset = IR3_DP_FS(frag_offset);
      break;
   case nir_intrinsic_load_frag_invocation_count:
      param_info->offset = IR3_DP_FS(frag_invocation_count);
      break;
   default:
      return false;
   }

   return true;
}

uint32_t
ir3_nir_scan_driver_consts(struct ir3_compiler *compiler, nir_shader *shader,
                           struct ir3_const_image_dims *image_dims)
{
   uint32_t num_driver_params = 0;
   nir_foreach_function (function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block (block, function->impl) {
         nir_foreach_instr (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            unsigned idx;

            if (image_dims) {
               switch (intr->intrinsic) {
               case nir_intrinsic_image_atomic:
               case nir_intrinsic_image_atomic_swap:
               case nir_intrinsic_image_load:
               case nir_intrinsic_image_store:
               case nir_intrinsic_image_size:
                  /* a4xx gets these supplied by the hw directly (maybe CP?) */
                  if (compiler->gen == 5 &&
                     !(intr->intrinsic == nir_intrinsic_image_load &&
                        !(nir_intrinsic_access(intr) & ACCESS_COHERENT))) {
                     idx = nir_src_as_uint(intr->src[0]);
                     if (image_dims->mask & (1 << idx))
                        break;
                     image_dims->mask |= (1 << idx);
                     image_dims->off[idx] = image_dims->count;
                     image_dims->count += 3; /* three const per */
                  }
                  break;
               default:
                  break;
               }
            }

            struct driver_param_info param_info;
            if (ir3_get_driver_param_info(shader, intr, &param_info)) {
               num_driver_params =
                  MAX2(num_driver_params,
                       param_info.offset + nir_intrinsic_dest_components(intr));
            }
         }
      }
   }

   /* TODO: Provide a spot somewhere to safely upload unwanted values, and a way
    * to determine if they're wanted or not. For now we always make the whole
    * driver param range available, since the driver will always instruct the
    * hardware to upload these.
    */
   if (!compiler->has_shared_regfile &&
         shader->info.stage == MESA_SHADER_COMPUTE) {
      num_driver_params =
         MAX2(num_driver_params, IR3_DP_CS(workgroup_id_z) + 1);
   }

   return num_driver_params;
}

void
ir3_const_alloc(struct ir3_const_allocations *const_alloc,
                enum ir3_const_alloc_type type, uint32_t size_vec4,
                uint32_t align_vec4)
{
   struct ir3_const_allocation *alloc = &const_alloc->consts[type];
   assert(alloc->size_vec4 == 0);

   const_alloc->max_const_offset_vec4 =
      align(const_alloc->max_const_offset_vec4, align_vec4);
   alloc->size_vec4 = size_vec4;
   alloc->offset_vec4 = const_alloc->max_const_offset_vec4;
   const_alloc->max_const_offset_vec4 += size_vec4;
}

void
ir3_const_reserve_space(struct ir3_const_allocations *const_alloc,
                        enum ir3_const_alloc_type type, uint32_t size_vec4,
                        uint32_t align_vec4)
{
   struct ir3_const_allocation *alloc = &const_alloc->consts[type];
   assert(alloc->size_vec4 == 0 && alloc->reserved_size_vec4 == 0);

   alloc->reserved_size_vec4 = size_vec4;
   alloc->reserved_align_vec4 = align_vec4;
   /* Be pessimistic here and assume the worst case alignment is needed */
   const_alloc->reserved_vec4 += size_vec4 + align_vec4 - 1;
}

void
ir3_const_free_reserved_space(struct ir3_const_allocations *const_alloc,
                              enum ir3_const_alloc_type type)
{
   struct ir3_const_allocation *alloc = &const_alloc->consts[type];
   assert(const_alloc->reserved_vec4 >= alloc->reserved_size_vec4);

   const_alloc->reserved_vec4 -=
      alloc->reserved_size_vec4 + alloc->reserved_align_vec4 - 1;
   alloc->reserved_size_vec4 = 0;
}

void
ir3_const_alloc_all_reserved_space(struct ir3_const_allocations *const_alloc)
{
   for (int i = 0; i < IR3_CONST_ALLOC_MAX; i++) {
      if (const_alloc->consts[i].reserved_size_vec4 > 0) {
         ir3_const_alloc(const_alloc, i,
                         const_alloc->consts[i].reserved_size_vec4,
                         const_alloc->consts[i].reserved_align_vec4);
         const_alloc->consts[i].reserved_size_vec4 = 0;
      }
   }
   const_alloc->reserved_vec4 = 0;
}

void
ir3_alloc_driver_params(struct ir3_const_allocations *const_alloc,
                        uint32_t *num_driver_params,
                        struct ir3_compiler *compiler,
                        gl_shader_stage shader_stage)
{
   if (*num_driver_params == 0)
      return;

   /* num_driver_params in dwords.  we only need to align to vec4s for the
    * common case of immediate constant uploads, but for indirect dispatch
    * the constants may also be indirect and so we have to align the area in
    * const space to that requirement.
    */
   *num_driver_params = align(*num_driver_params, 4);
   unsigned upload_unit = 1;
   if (shader_stage == MESA_SHADER_COMPUTE ||
       (*num_driver_params >= IR3_DP_VS(vtxid_base))) {
      upload_unit = compiler->const_upload_unit;
   }

   /* offset cannot be 0 for vs params loaded by CP_DRAW_INDIRECT_MULTI */
   if (shader_stage == MESA_SHADER_VERTEX && compiler->gen >= 6)
      const_alloc->max_const_offset_vec4 =
         MAX2(const_alloc->max_const_offset_vec4, 1);

   uint32_t driver_params_size_vec4 =
      align(*num_driver_params / 4, upload_unit);
   ir3_const_alloc(const_alloc, IR3_CONST_ALLOC_DRIVER_PARAMS,
                   driver_params_size_vec4, upload_unit);
}

/* Sets up the variant-dependent constant state for the ir3_shader.
 * The consts allocation flow is as follows:
 * 1) Turnip/Freedreno allocates consts required by corresponding API,
 *    e.g. push const, inline uniforms, etc. Then passes ir3_const_allocations
 *    into IR3.
 * 2) ir3_setup_const_state pre-allocates consts with non-negotiable size.
 * 3) IR3 lowerings afterwards allocate from the free space left.
 * 4) Allocate offsets for consts from step 2)
 */
void
ir3_setup_const_state(nir_shader *nir, struct ir3_shader_variant *v,
                      struct ir3_const_state *const_state)
{
   struct ir3_compiler *compiler = v->compiler;
   unsigned ptrsz = ir3_pointer_size(compiler);

   const_state->num_driver_params =
      ir3_nir_scan_driver_consts(compiler, nir, &const_state->image_dims);

   if ((compiler->gen < 5) && (v->stream_output.num_outputs > 0)) {
      const_state->num_driver_params =
         MAX2(const_state->num_driver_params, IR3_DP_VS(vtxcnt_max) + 1);
   }

   const_state->num_ubos = nir->info.num_ubos;

   assert((const_state->ubo_state.size % 16) == 0);

   /* IR3_CONST_ALLOC_DRIVER_PARAMS could have been allocated earlier. */
   if (const_state->allocs.consts[IR3_CONST_ALLOC_DRIVER_PARAMS].size_vec4 == 0) {
      ir3_alloc_driver_params(&const_state->allocs,
                              &const_state->num_driver_params, compiler,
                              v->type);
   }

   if (const_state->image_dims.count > 0) {
      ir3_const_reserve_space(&const_state->allocs, IR3_CONST_ALLOC_IMAGE_DIMS,
                              align(const_state->image_dims.count, 4) / 4, 1);
   }

   if (v->type == MESA_SHADER_KERNEL && v->cs.req_input_mem) {
      ir3_const_reserve_space(&const_state->allocs,
                              IR3_CONST_ALLOC_KERNEL_PARAMS,
                              align(v->cs.req_input_mem, 4) / 4, 1);
   }

   if ((v->type == MESA_SHADER_VERTEX) && (compiler->gen < 5) &&
       v->stream_output.num_outputs > 0) {
      ir3_const_reserve_space(&const_state->allocs, IR3_CONST_ALLOC_TFBO,
                              align(IR3_MAX_SO_BUFFERS * ptrsz, 4) / 4, 1);
   }

   if (!compiler->load_shader_consts_via_preamble) {
      switch (v->type) {
      case MESA_SHADER_TESS_CTRL:
      case MESA_SHADER_TESS_EVAL:
         ir3_const_reserve_space(&const_state->allocs,
                                 IR3_CONST_ALLOC_PRIMITIVE_PARAM, 2, 1);
         break;
      case MESA_SHADER_GEOMETRY:
         ir3_const_reserve_space(&const_state->allocs,
                                 IR3_CONST_ALLOC_PRIMITIVE_PARAM, 1, 1);
         break;
      default:
         break;
      }
   }

   if (v->type == MESA_SHADER_VERTEX) {
      ir3_const_reserve_space(&const_state->allocs,
                              IR3_CONST_ALLOC_PRIMITIVE_PARAM, 1, 1);
   }

   if ((v->type == MESA_SHADER_TESS_CTRL || v->type == MESA_SHADER_TESS_EVAL ||
        v->type == MESA_SHADER_GEOMETRY)) {
      ir3_const_reserve_space(&const_state->allocs,
                              IR3_CONST_ALLOC_PRIMITIVE_MAP,
                              DIV_ROUND_UP(v->input_size, 4), 1);
   }

   assert(const_state->allocs.max_const_offset_vec4 <= ir3_max_const(v));
}

uint32_t
ir3_const_state_get_free_space(const struct ir3_shader_variant *v,
                               const struct ir3_const_state *const_state,
                               uint32_t align_vec4)
{
   uint32_t aligned_offset_vec4 =
      align(const_state->allocs.max_const_offset_vec4, align_vec4);
   uint32_t free_space_vec4 = ir3_max_const(v) - aligned_offset_vec4 -
                              const_state->allocs.reserved_vec4;
   free_space_vec4 = ROUND_DOWN_TO(free_space_vec4, align_vec4);
   return free_space_vec4;
}
