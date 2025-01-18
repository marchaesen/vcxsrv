/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_trans_nir.c
 *
 * \brief NIR translation functions.
 */

#include "compiler/glsl/list.h"
#include "compiler/shader_enums.h"
#include "hwdef/rogue_hw_defs.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/bitset.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/ralloc.h"

#include <assert.h>
#include <stdio.h>

/** Translation context. */
typedef struct _trans_ctx {
   pco_ctx *pco_ctx; /** PCO compiler context. */
   pco_shader *shader; /** Current shader. */
   pco_func *func; /** Current function. */
   pco_builder b; /** Builder. */
   gl_shader_stage stage; /** Shader stage. */

   BITSET_WORD *float_types; /** NIR SSA float vars. */
   BITSET_WORD *int_types; /** NIR SSA int vars. */
} trans_ctx;

/* Forward declarations. */
static pco_block *trans_cf_nodes(trans_ctx *tctx,
                                 pco_cf_node *parent_cf_node,
                                 struct list_head *cf_node_list,
                                 struct exec_list *nir_cf_node_list);

/**
 * \brief Splits a vector destination into scalar components.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] instr Instruction producing the vector destination.
 * \param[in] dest Instruction destination.
 */
static void split_dest_comps(trans_ctx *tctx, pco_instr *instr, pco_ref dest)
{
   unsigned chans = pco_ref_get_chans(dest);
   assert(chans > 1);

   pco_func *func = tctx->func;

   pco_vec_info *vec_info = rzalloc_size(func->vec_infos, sizeof(*vec_info));
   vec_info->instr = instr;
   vec_info->comps =
      rzalloc_array_size(vec_info, sizeof(*vec_info->comps), chans);

   for (unsigned u = 0; u < chans; ++u) {
      pco_ref comp = pco_ref_new_ssa(func, pco_ref_get_bits(dest), 1);
      vec_info->comps[u] = pco_comp(&tctx->b, comp, dest, pco_ref_val16(u));
   }

   _mesa_hash_table_u64_insert(func->vec_infos, dest.val, vec_info);
}

/**
 * \brief Translates a NIR def into a PCO reference.
 *
 * \param[in] def The nir def.
 * \return The PCO reference.
 */
static inline pco_ref pco_ref_nir_def(const nir_def *def)
{
   return pco_ref_ssa(def->index, def->bit_size, def->num_components);
}

/**
 * \brief Translates a NIR src into a PCO reference.
 *
 * \param[in] src The nir src.
 * \return The PCO reference.
 */
static inline pco_ref pco_ref_nir_src(const nir_src *src)
{
   return pco_ref_nir_def(src->ssa);
}

/**
 * \brief Translates a NIR def into a PCO reference with type information.
 *
 * \param[in] def The nir def.
 * \param[in] tctx Translation context.
 * \return The PCO reference.
 */
static inline pco_ref pco_ref_nir_def_t(const nir_def *def, trans_ctx *tctx)
{
   pco_ref ref = pco_ref_nir_def(def);

   bool is_float = BITSET_TEST(tctx->float_types, def->index);
   bool is_int = BITSET_TEST(tctx->int_types, def->index);

   if (is_float)
      ref.dtype = PCO_DTYPE_FLOAT;
   else if (is_int)
      ref.dtype = PCO_DTYPE_UNSIGNED;

   return ref;
}

/**
 * \brief Translates a NIR src into a PCO reference with type information.
 *
 * \param[in] src The nir src.
 * \param[in] tctx Translation context.
 * \return The PCO reference.
 */
static inline pco_ref pco_ref_nir_src_t(const nir_src *src, trans_ctx *tctx)
{
   return pco_ref_nir_def_t(src->ssa, tctx);
}

/**
 * \brief Translates a NIR alu src into a PCO reference with type information,
 *        extracting from and/or building new vectors as needed.
 *
 * \param[in] src The nir src.
 * \param[in,out] tctx Translation context.
 * \return The PCO reference.
 */
static inline pco_ref
pco_ref_nir_alu_src_t(const nir_alu_instr *alu, unsigned src, trans_ctx *tctx)
{
   const nir_alu_src *alu_src = &alu->src[src];
   /* unsigned chans = nir_src_num_components(alu_src->src); */
   unsigned chans = nir_ssa_alu_instr_src_components(alu, src);

   bool seq_comps =
      nir_is_sequential_comp_swizzle((uint8_t *)alu_src->swizzle, chans);
   pco_ref ref = pco_ref_nir_src_t(&alu_src->src, tctx);
   uint8_t swizzle0 = alu_src->swizzle[0];

   /* Multiple channels, but referencing the entire vector; return as-is. */
   if (!swizzle0 && seq_comps && chans == nir_src_num_components(alu_src->src))
      return ref;

   pco_vec_info *vec_info =
      _mesa_hash_table_u64_search(tctx->func->vec_infos, ref.val);
   assert(vec_info);

   /* One channel; just return its component. */
   if (chans == 1)
      return vec_info->comps[swizzle0]->dest[0];

   /* Multiple channels, either a partial vec and/or swizzling; we need to build
    * a new vec for this.
    */
   pco_ref comps[NIR_MAX_VEC_COMPONENTS] = { 0 };
   for (unsigned u = 0; u < chans; ++u)
      comps[u] = vec_info->comps[alu_src->swizzle[u]]->dest[0];

   pco_ref vec = pco_ref_new_ssa(tctx->func, pco_ref_get_bits(ref), chans);
   pco_instr *instr = pco_vec(&tctx->b, vec, chans, comps);

   split_dest_comps(tctx, instr, vec);

   return vec;
}

/**
 * \brief Translates a NIR vs load_input intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr load_input intrinsic.
 * \param[in] dest Instruction destination.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_load_input_vs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref dest)
{
   ASSERTED unsigned base = nir_intrinsic_base(intr);
   assert(!base);

   ASSERTED nir_alu_type type = nir_intrinsic_dest_type(intr);
   assert(type == nir_type_float32);
   /* TODO: f16 support. */

   ASSERTED const nir_src offset = intr->src[0];
   assert(nir_src_as_uint(offset) == 0);

   gl_vert_attrib location = nir_intrinsic_io_semantics(intr).location;
   unsigned component = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(dest);

   const pco_range *range = &tctx->shader->data.vs.attribs[location];
   assert(component + chans <= range->count);

   pco_ref src =
      pco_ref_hwreg_vec(range->start + component, PCO_REG_CLASS_VTXIN, chans);
   return pco_mov(&tctx->b, dest, src, .rpt = chans);
}

/**
 * \brief Translates a NIR vs store_output intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr store_output intrinsic.
 * \param[in] src Instruction source.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_store_output_vs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref src)
{
   ASSERTED unsigned base = nir_intrinsic_base(intr);
   assert(!base);

   ASSERTED nir_alu_type type = nir_intrinsic_src_type(intr);
   assert(type == nir_type_float32);
   /* TODO: f16 support. */

   ASSERTED const nir_src offset = intr->src[1];
   assert(nir_src_as_uint(offset) == 0);

   gl_varying_slot location = nir_intrinsic_io_semantics(intr).location;
   unsigned component = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(src);

   /* Only contiguous write masks supported. */
   ASSERTED unsigned write_mask = nir_intrinsic_write_mask(intr);
   assert(write_mask == BITFIELD_MASK(chans));

   const pco_range *range = &tctx->shader->data.vs.varyings[location];
   assert(component + chans <= range->count);

   pco_ref vtxout_addr = pco_ref_val8(range->start + component);
   return pco_uvsw_write(&tctx->b, src, vtxout_addr, .rpt = chans);
}

/**
 * \brief Translates a NIR fs load_input intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr load_input intrinsic.
 * \param[in] dest Instruction destination.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_load_input_fs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref dest)
{
   pco_fs_data *fs_data = &tctx->shader->data.fs;
   ASSERTED unsigned base = nir_intrinsic_base(intr);
   assert(!base);

   unsigned component = nir_intrinsic_component(intr);
   unsigned chans = pco_ref_get_chans(dest);

   const nir_src offset = intr->src[0];
   assert(nir_src_as_uint(offset) == 0);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   gl_varying_slot location = io_semantics.location;

   nir_variable *var = nir_find_variable_with_location(tctx->shader->nir,
                                                       nir_var_shader_in,
                                                       location);

   enum pco_itr_mode itr_mode = PCO_ITR_MODE_PIXEL;
   assert(!(var->data.sample && var->data.centroid));
   if (var->data.sample)
      itr_mode = PCO_ITR_MODE_SAMPLE;
   else if (var->data.centroid)
      itr_mode = PCO_ITR_MODE_CENTROID;

   if (location == VARYING_SLOT_POS) {
      /* Only scalar supported for now. */
      /* TODO: support vector for zw. */
      assert(chans == 1);

      /* TODO: support packing/partial vars. */
      assert(!var->data.location_frac);

      assert(var->data.interpolation == INTERP_MODE_NOPERSPECTIVE);

      /* Special case: x and y are loaded from special registers. */
      /* TODO: select appropriate regs if sample rate shading. */
      switch (component) {
      case 0: /* x */
         return pco_mov(&tctx->b,
                        dest,
                        pco_ref_hwreg(PCO_SR_X_P, PCO_REG_CLASS_SPEC));

      case 1: /* y */
         return pco_mov(&tctx->b,
                        dest,
                        pco_ref_hwreg(PCO_SR_Y_P, PCO_REG_CLASS_SPEC));

      case 2:
         assert(fs_data->uses.z);
         component = 0;
         break;

      case 3:
         assert(fs_data->uses.w);
         component = fs_data->uses.z ? 1 : 0;
         break;

      default:
         unreachable();
      }
   }

   const pco_range *range = &fs_data->varyings[location];
   assert(component + (ROGUE_USC_COEFFICIENT_SET_SIZE * chans) <= range->count);

   unsigned coeffs_index =
      range->start + (ROGUE_USC_COEFFICIENT_SET_SIZE * component);

   pco_ref coeffs = pco_ref_hwreg_vec(coeffs_index,
                                      PCO_REG_CLASS_COEFF,
                                      ROGUE_USC_COEFFICIENT_SET_SIZE * chans);
   pco_ref itr_count = pco_ref_val16(chans);

   bool usc_itrsmp_enhanced =
      PVR_HAS_FEATURE(tctx->pco_ctx->dev_info, usc_itrsmp_enhanced);

   switch (var->data.interpolation) {
   case INTERP_MODE_SMOOTH: {
      assert(fs_data->uses.w);

      unsigned wcoeffs_index = fs_data->uses.z ? ROGUE_USC_COEFFICIENT_SET_SIZE
                                               : 0;

      pco_ref wcoeffs = pco_ref_hwreg_vec(wcoeffs_index,
                                          PCO_REG_CLASS_COEFF,
                                          ROGUE_USC_COEFFICIENT_SET_SIZE);

      return usc_itrsmp_enhanced ? pco_ditrp(&tctx->b,
                                             dest,
                                             pco_ref_drc(PCO_DRC_0),
                                             coeffs,
                                             wcoeffs,
                                             itr_count,
                                             .itr_mode = itr_mode)
                                 : pco_fitrp(&tctx->b,
                                             dest,
                                             pco_ref_drc(PCO_DRC_0),
                                             coeffs,
                                             wcoeffs,
                                             itr_count,
                                             .itr_mode = itr_mode);
   }

   case INTERP_MODE_NOPERSPECTIVE:
      return usc_itrsmp_enhanced ? pco_ditr(&tctx->b,
                                            dest,
                                            pco_ref_drc(PCO_DRC_0),
                                            coeffs,
                                            itr_count,
                                            .itr_mode = itr_mode)
                                 : pco_fitr(&tctx->b,
                                            dest,
                                            pco_ref_drc(PCO_DRC_0),
                                            coeffs,
                                            itr_count,
                                            .itr_mode = itr_mode);

   default:
      /* Should have been previously lowered. */
      unreachable();
   }
}

/**
 * \brief Translates a NIR fs store_output intrinsic into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr store_output intrinsic.
 * \param[in] src Instruction source.
 * \return The translated PCO instruction.
 */
static pco_instr *
trans_store_output_fs(trans_ctx *tctx, nir_intrinsic_instr *intr, pco_ref src)
{
   ASSERTED unsigned base = nir_intrinsic_base(intr);
   assert(!base);

   assert(pco_ref_is_scalar(src));
   unsigned component = nir_intrinsic_component(intr);

   ASSERTED const nir_src offset = intr->src[1];
   assert(nir_src_as_uint(offset) == 0);

   gl_varying_slot location = nir_intrinsic_io_semantics(intr).location;

   const pco_range *range = &tctx->shader->data.fs.outputs[location];
   assert(component < range->count);

   ASSERTED bool output_reg = tctx->shader->data.fs.output_reg[location];
   assert(output_reg);
   /* TODO: tile buffer support. */

   pco_ref dest = pco_ref_hwreg(range->start + component, PCO_REG_CLASS_PIXOUT);
   return pco_mov(&tctx->b, dest, src, .olchk = true);
}

/**
 * \brief Translates a NIR intrinsic instruction into PCO.
 *
 * \param[in,out] tctx Translation context.
 * \param[in] intr The nir intrinsic instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_intr(trans_ctx *tctx, nir_intrinsic_instr *intr)
{
   const nir_intrinsic_info *info = &nir_intrinsic_infos[intr->intrinsic];

   pco_ref dest = info->has_dest ? pco_ref_nir_def_t(&intr->def, tctx)
                                 : pco_ref_null();

   pco_ref src[NIR_MAX_VEC_COMPONENTS] = { 0 };
   for (unsigned s = 0; s < info->num_srcs; ++s)
      src[s] = pco_ref_nir_src_t(&intr->src[s], tctx);

   pco_instr *instr;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_input:
      if (tctx->stage == MESA_SHADER_VERTEX)
         instr = trans_load_input_vs(tctx, intr, dest);
      else if (tctx->stage == MESA_SHADER_FRAGMENT)
         instr = trans_load_input_fs(tctx, intr, dest);
      else
         unreachable("Unsupported stage for \"nir_intrinsic_load_input\".");
      break;

   case nir_intrinsic_store_output:
      if (tctx->stage == MESA_SHADER_VERTEX)
         instr = trans_store_output_vs(tctx, intr, src[0]);
      else if (tctx->stage == MESA_SHADER_FRAGMENT)
         instr = trans_store_output_fs(tctx, intr, src[0]);
      else
         unreachable("Unsupported stage for \"nir_intrinsic_store_output\".");
      break;

   default:
      printf("Unsupported intrinsic: \"");
      nir_print_instr(&intr->instr, stdout);
      printf("\"\n");
      unreachable();
      break;
   }

   if (!pco_ref_is_scalar(dest))
      split_dest_comps(tctx, instr, dest);

   return instr;
}

/**
 * \brief Attempts to collate a vector within a vector.
 *
 * If a vector references another vector in its entirety in order/without
 * swizzling, we try to store a reference to said vector rather than its
 * individual components.
 *
 * \param[in] src The source/vector channel to start checking from.
 * \param[in] from The instruction the vector components are from.
 * \param[in] vec The potential vector reference from the parent instruction.
 * \param[in] vec_chans The number of sources/vector channels.
 * \return The number of collated sources, or 0 if collation failed.
 */
static pco_ref
try_collate_vec(pco_ref *src, pco_instr *from, pco_ref vec, unsigned vec_chans)
{
   /* Skip the first one since it's our reference (and we already know its
    * component is 0.
    */
   for (unsigned s = 1; s < vec_chans; ++s) {
      pco_instr *parent_instr = find_parent_instr_from(src[s], from);
      assert(parent_instr);

      if (parent_instr->op != PCO_OP_COMP)
         return pco_ref_null();

      pco_ref comp_src = parent_instr->src[0];
      unsigned comp_idx = pco_ref_get_imm(parent_instr->src[1]);
      ASSERTED unsigned chans = pco_ref_get_chans(comp_src);

      if (!pco_refs_are_equal(comp_src, vec))
         return pco_ref_null();

      assert(chans == vec_chans);

      if (comp_idx != s)
         return pco_ref_null();
   }

   return vec;
}

/**
 * \brief Attempts to collate vector sources.
 *
 * \param[in] tctx Translation context.
 * \param[in] dest Instruction destination.
 * \param[in] num_srcs The number of sources/vector channels.
 * \param[in] src The sources/vector components.
 * \return The number of collated sources, or 0 if collation failed.
 */
static unsigned try_collate_vec_srcs(trans_ctx *tctx,
                                     unsigned num_srcs,
                                     pco_ref *src,
                                     pco_ref *collated_src)
{
   bool collated_vector = false;
   unsigned num_srcs_collated = 0;
   pco_instr *from = pco_cursor_instr(tctx->b.cursor);

   for (unsigned s = 0; s < num_srcs; ++s) {
      pco_instr *parent_instr = find_parent_instr_from(src[s], from);
      assert(parent_instr);

      /* This is a purely scalar source; append it and continue. */
      if (parent_instr->op != PCO_OP_COMP) {
         collated_src[num_srcs_collated++] = src[s];
         continue;
      }

      pco_ref comp_src = parent_instr->src[0];
      unsigned comp_idx = pco_ref_get_imm(parent_instr->src[1]);
      unsigned chans = pco_ref_get_chans(comp_src);

      /* We have a vector source, but it either:
       * - doesn't start from the first element
       * - is bigger than the remaining channels of *this* vec
       * so it's impossible for it to be contained in its entirety;
       * append the component and continue.
       */
      if (comp_idx != 0 || chans > (num_srcs - s)) {
         collated_src[num_srcs_collated++] = src[s];
         continue;
      }

      /* We have a candidate for an entire vector to be inserted. */
      pco_ref collated_ref = try_collate_vec(&src[s], from, comp_src, chans);
      if (pco_ref_is_null(collated_ref)) {
         collated_src[num_srcs_collated++] = src[s];
         continue;
      }

      /* We were successful, record this and increment accordingly. */
      collated_src[num_srcs_collated++] = collated_ref;

      s += chans - 1;
      collated_vector = true;
   }

   return collated_vector ? num_srcs_collated : 0;
}

/**
 * \brief Translates a NIR vec instruction into PCO, attempting collation.
 *
 * \param[in] tctx Translation context.
 * \param[in] dest Instruction destination.
 * \param[in] num_srcs The number of sources/vector components.
 * \param[in] src The sources/vector components.
 * \return The PCO instruction.
 */
static pco_instr *pco_trans_nir_vec(trans_ctx *tctx,
                                    pco_ref dest,
                                    unsigned num_srcs,
                                    pco_ref *src)

{
   /* If a vec contains entire other vecs, try to reference them directly. */
   pco_ref collated_src[NIR_MAX_VEC_COMPONENTS] = { 0 };
   unsigned num_srcs_collated =
      try_collate_vec_srcs(tctx, num_srcs, src, collated_src);
   if (!num_srcs_collated)
      return pco_vec(&tctx->b, dest, num_srcs, src);

   pco_instr *instr = pco_vec(&tctx->b, dest, num_srcs_collated, collated_src);

   /* Record the collated vectors. */
   for (unsigned s = 0; s < num_srcs_collated; ++s) {
      if (pco_ref_is_scalar(collated_src[s]))
         continue;

      pco_vec_info *vec_info =
         _mesa_hash_table_u64_search(tctx->func->vec_infos,
                                     collated_src[s].val);
      assert(vec_info);

      /* Skip if there are multiple users. */
      vec_info->vec_user = vec_info->vec_user ? VEC_USER_MULTI : instr;
   }

   return instr;
}

/**
 * \brief Translates a NIR alu instruction into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] alu The nir alu instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_alu(trans_ctx *tctx, nir_alu_instr *alu)
{
   const nir_op_info *info = &nir_op_infos[alu->op];
   unsigned num_srcs = info->num_inputs;

   pco_ref dest = pco_ref_nir_def_t(&alu->def, tctx);

   pco_ref src[NIR_MAX_VEC_COMPONENTS] = { 0 };
   for (unsigned s = 0; s < num_srcs; ++s)
      src[s] = pco_ref_nir_alu_src_t(alu, s, tctx);

   pco_instr *instr;
   switch (alu->op) {
   case nir_op_fneg:
      instr = pco_neg(&tctx->b, dest, src[0]);
      break;

   case nir_op_fabs:
      instr = pco_abs(&tctx->b, dest, src[0]);
      break;

   case nir_op_ffloor:
      instr = pco_flr(&tctx->b, dest, src[0]);
      break;

   case nir_op_fadd:
      instr = pco_fadd(&tctx->b, dest, src[0], src[1]);
      break;

   case nir_op_fmul:
      instr = pco_fmul(&tctx->b, dest, src[0], src[1]);
      break;

   case nir_op_ffma:
      instr = pco_fmad(&tctx->b, dest, src[0], src[1], src[2]);
      break;

   case nir_op_pack_unorm_4x8:
      instr = pco_pck(&tctx->b,
                      dest,
                      src[0],
                      .rpt = 4,
                      .pck_fmt = PCO_PCK_FMT_U8888,
                      .scale = true);
      break;

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec5:
   case nir_op_vec8:
   case nir_op_vec16:
      instr = pco_trans_nir_vec(tctx, dest, num_srcs, src);
      break;

   default:
      printf("Unsupported alu instruction: \"");
      nir_print_instr(&alu->instr, stdout);
      printf("\"\n");
      unreachable();
   }

   if (!pco_ref_is_scalar(dest))
      split_dest_comps(tctx, instr, dest);

   return instr;
}

/**
 * \brief Translates a NIR load constant instruction into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] nconst The nir load constant instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_const(trans_ctx *tctx, nir_load_const_instr *nconst)
{
   unsigned num_bits = nconst->def.bit_size;
   unsigned chans = nconst->def.num_components;

   /* TODO: support more bit sizes/components. */
   assert(num_bits == 32);

   pco_ref dest = pco_ref_nir_def_t(&nconst->def, tctx);

   if (pco_ref_is_scalar(dest)) {
      assert(chans == 1);

      uint64_t val = nir_const_value_as_uint(nconst->value[0], num_bits);
      pco_ref imm =
         pco_ref_imm(val, pco_bits(num_bits), pco_ref_get_dtype(dest));

      return pco_movi32(&tctx->b, dest, imm);
   }

   pco_ref comps[NIR_MAX_VEC_COMPONENTS] = { 0 };
   for (unsigned c = 0; c < chans; ++c) {
      comps[c] = pco_ref_new_ssa(tctx->func, pco_ref_get_bits(dest), 1);

      uint64_t val = nir_const_value_as_uint(nconst->value[c], num_bits);
      pco_ref imm =
         pco_ref_imm(val, pco_bits(num_bits), pco_ref_get_dtype(dest));

      pco_movi32(&tctx->b, comps[c], imm);
   }

   pco_instr *instr = pco_vec(&tctx->b, dest, chans, comps);

   split_dest_comps(tctx, instr, dest);

   return instr;
}

/**
 * \brief Translates a NIR instruction into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] ninstr The nir instruction.
 * \return The PCO instruction.
 */
static pco_instr *trans_instr(trans_ctx *tctx, nir_instr *ninstr)
{
   switch (ninstr->type) {
   case nir_instr_type_intrinsic:
      return trans_intr(tctx, nir_instr_as_intrinsic(ninstr));

   case nir_instr_type_load_const:
      return trans_const(tctx, nir_instr_as_load_const(ninstr));

   case nir_instr_type_alu:
      return trans_alu(tctx, nir_instr_as_alu(ninstr));

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Translates a NIR block into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] nblock The nir block.
 * \return The PCO block.
 */
static pco_block *trans_block(trans_ctx *tctx, nir_block *nblock)
{
   pco_block *block = pco_block_create(tctx->func);
   tctx->b = pco_builder_create(tctx->func, pco_cursor_after_block(block));

   nir_foreach_instr (ninstr, nblock) {
      trans_instr(tctx, ninstr);
   }

   return block;
}

/**
 * \brief Translates a NIR if into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] nif The nir if.
 * \return The PCO if.
 */
static pco_if *trans_if(trans_ctx *tctx, nir_if *nif)
{
   pco_if *pif = pco_if_create(tctx->func);

   unreachable("finishme: trans_if");

   return pif;
}

/**
 * \brief Translates a NIR loop into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] nloop The nir loop.
 * \return The PCO loop.
 */
static pco_loop *trans_loop(trans_ctx *tctx, nir_loop *nloop)
{
   pco_loop *loop = pco_loop_create(tctx->func);

   unreachable("finishme: trans_loop");

   return loop;
}

/**
 * \brief Translates a NIR function into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] impl The nir function impl.
 * \return The PCO function.
 */
static pco_func *trans_func(trans_ctx *tctx, nir_function_impl *impl)
{
   nir_function *nfunc = impl->function;
   enum pco_func_type func_type = PCO_FUNC_TYPE_CALLABLE;

   if (nfunc->is_preamble)
      func_type = PCO_FUNC_TYPE_PREAMBLE;
   else if (nfunc->is_entrypoint)
      func_type = PCO_FUNC_TYPE_ENTRYPOINT;

   pco_func *func = pco_func_create(tctx->shader, func_type, nfunc->num_params);
   tctx->func = func;

   func->name = ralloc_strdup(func, nfunc->name);
   func->next_ssa = impl->ssa_alloc;

   /* TODO: Function parameter support. */
   assert(func->num_params == 0 && func->params == NULL);

   /* Gather types. */
   tctx->float_types =
      rzalloc_array(NULL, BITSET_WORD, BITSET_WORDS(impl->ssa_alloc));
   tctx->int_types =
      rzalloc_array(NULL, BITSET_WORD, BITSET_WORDS(impl->ssa_alloc));
   nir_gather_types(impl, tctx->float_types, tctx->int_types);

   trans_cf_nodes(tctx, &func->cf_node, &func->body, &impl->body);

   ralloc_free(tctx->float_types);
   ralloc_free(tctx->int_types);

   return func;
}

/**
 * \brief Translates NIR control flow nodes into PCO.
 *
 * \param[in] tctx Translation context.
 * \param[in] parent_cf_node The parent cf node.
 * \param[in] cf_node_list The PCO cf node list.
 * \param[in,out] nir_cf_node_list The NIR cf node list.
 * \return The first block from the cf nodes.
 */
static pco_block *trans_cf_nodes(trans_ctx *tctx,
                                 pco_cf_node *parent_cf_node,
                                 struct list_head *cf_node_list,
                                 struct exec_list *nir_cf_node_list)
{
   pco_block *start_block = NULL;

   pco_cf_node *cf_node;
   foreach_list_typed (nir_cf_node, ncf_node, node, nir_cf_node_list) {
      switch (ncf_node->type) {
      case nir_cf_node_block: {
         pco_block *block = trans_block(tctx, nir_cf_node_as_block(ncf_node));
         cf_node = &block->cf_node;

         if (!start_block)
            start_block = block;
         break;
      }

      case nir_cf_node_if: {
         pco_if *pif = trans_if(tctx, nir_cf_node_as_if(ncf_node));
         cf_node = &pif->cf_node;
         break;
      }

      case nir_cf_node_loop: {
         pco_loop *loop = trans_loop(tctx, nir_cf_node_as_loop(ncf_node));
         cf_node = &loop->cf_node;
         break;
      }

      default:
         unreachable();
      }

      cf_node->parent = parent_cf_node;
      list_addtail(&cf_node->link, cf_node_list);
   }

   return start_block;
}

/**
 * \brief Translates a NIR shader into a PCO shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in] nir NIR shader.
 * \param[in] data Shader-specific data.
 * \param[in] mem_ctx Ralloc memory allocation context.
 * \return The PCO shader.
 */
pco_shader *
pco_trans_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data, void *mem_ctx)
{
   pco_shader *shader = pco_shader_create(ctx, nir, mem_ctx);

   if (data)
      memcpy(&shader->data, data, sizeof(*data));

   trans_ctx tctx = {
      .pco_ctx = ctx,
      .shader = shader,
      .stage = shader->stage,
   };

   nir_foreach_function_with_impl (func, impl, nir) {
      trans_func(&tctx, impl);
   }

   if (pco_should_print_shader(shader))
      pco_print_shader(shader, stdout, "before passes");

   return shader;
}
