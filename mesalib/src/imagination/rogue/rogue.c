/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "compiler/shader_enums.h"
#include "compiler/spirv/nir_spirv.h"
#include "nir/nir.h"
#include "rogue.h"
#include "rogue_build_data.h"
#include "rogue_compiler.h"
#include "rogue_constreg.h"
#include "rogue_encode.h"
#include "rogue_nir.h"
#include "rogue_nir_helpers.h"
#include "rogue_operand.h"
#include "rogue_regalloc.h"
#include "rogue_shader.h"
#include "rogue_validate.h"
#include "util/macros.h"
#include "util/memstream.h"
#include "util/ralloc.h"

/**
 * \file rogue.c
 *
 * \brief Contains the top-level Rogue compiler interface for Vulkan driver and
 * the offline compiler.
 */

/**
 * \brief Converts a SPIR-V shader to NIR.
 *
 * \param[in] ctx Shared multi-stage build context.
 * \param[in] stage Shader stage.
 * \param[in] spirv_size SPIR-V data length in DWORDs.
 * \param[in] spirv_data SPIR-V data.
 * \param[in] num_spec Number of SPIR-V specializations.
 * \param[in] spec SPIR-V specializations.
 * \return A nir_shader* if successful, or NULL if unsuccessful.
 */
nir_shader *rogue_spirv_to_nir(struct rogue_build_ctx *ctx,
                               gl_shader_stage stage,
                               const char *entry,
                               size_t spirv_size,
                               const uint32_t *spirv_data,
                               unsigned num_spec,
                               struct nir_spirv_specialization *spec)
{
   nir_shader *nir;

   nir = spirv_to_nir(spirv_data,
                      spirv_size,
                      spec,
                      num_spec,
                      stage,
                      entry,
                      rogue_get_spirv_options(ctx->compiler),
                      rogue_get_compiler_options(ctx->compiler));
   if (!nir)
      return NULL;

   ralloc_steal(ctx, nir);

   /* Apply passes. */
   if (!rogue_nir_passes(ctx, nir, stage)) {
      ralloc_free(nir);
      return NULL;
   }

   /* Collect I/O data to pass back to the driver. */
   if (!rogue_collect_io_data(ctx, nir)) {
      ralloc_free(nir);
      return NULL;
   }

   return nir;
}

/**
 * \brief Converts a Rogue shader to binary.
 *
 * \param[in] ctx Shared multi-stage build context.
 * \param[in] shader Rogue shader.
 * \return A rogue_shader_binary* if successful, or NULL if unsuccessful.
 */
struct rogue_shader_binary *rogue_to_binary(struct rogue_build_ctx *ctx,
                                            const struct rogue_shader *shader)
{
   struct rogue_shader_binary *binary;
   struct u_memstream mem;
   size_t buf_size;
   char *buf;

   if (!rogue_validate_shader(shader))
      return NULL;

   if (!u_memstream_open(&mem, &buf, &buf_size))
      return NULL;

   if (!rogue_encode_shader(shader, u_memstream_get(&mem))) {
      u_memstream_close(&mem);
      free(buf);
      return NULL;
   }

   u_memstream_close(&mem);

   binary = rzalloc_size(ctx, sizeof(*binary) + buf_size);
   if (!binary) {
      free(buf);
      return NULL;
   }

   binary->size = buf_size;
   memcpy(binary->data, buf, buf_size);

   free(buf);

   return binary;
}

static bool
setup_alu_dest(struct rogue_instr *instr, size_t dest_index, nir_alu_instr *alu)
{
   assert(dest_index == 0);

   /* Dest validation. */
   assert(nir_dest_num_components(alu->dest.dest) == 1 ||
          nir_dest_num_components(alu->dest.dest) == 4);
   assert(nir_dest_bit_size(alu->dest.dest) == 32);

   size_t nir_dest_reg = nir_alu_dest_regindex(alu);

   if (nir_dest_num_components(alu->dest.dest) == 1) {
      CHECK(rogue_instr_set_operand_vreg(instr, dest_index, nir_dest_reg));
   } else {
      size_t comp = nir_alu_dest_comp(alu);
      CHECK(rogue_instr_set_operand_vreg_vec(instr,
                                             dest_index,
                                             comp,
                                             nir_dest_reg));
   }

   return true;
}

static bool trans_constreg_operand(struct rogue_instr *instr,
                                   size_t operand_index,
                                   uint32_t const_value)
{
   size_t const_reg = rogue_constreg_lookup(const_value);

   /* Only values that can be sourced from const regs should be left from the
    * rogue_nir_constreg pass.
    */
   assert(const_reg != ROGUE_NO_CONST_REG);

   CHECK(rogue_instr_set_operand_reg(instr,
                                     operand_index,
                                     ROGUE_OPERAND_TYPE_REG_CONST,
                                     const_reg));

   return true;
}

static bool trans_nir_alu_fmax(struct rogue_shader *shader, nir_alu_instr *alu)
{
   /* Src validation. */
   assert(nir_src_num_components(alu->src[0].src) == 1);
   assert(nir_src_bit_size(alu->src[0].src) == 32);

   assert(nir_src_num_components(alu->src[1].src) == 1);
   assert(nir_src_bit_size(alu->src[1].src) == 32);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_MAX);

   CHECK(setup_alu_dest(instr, 0, alu));

   for (size_t u = 0; u < nir_op_infos[nir_op_fmax].num_inputs; ++u) {
      /* Handle values that can be pulled from const regs. */
      if (nir_alu_src_is_const(alu, u)) {
         CHECK(trans_constreg_operand(instr, u + 1, nir_alu_src_const(alu, u)));
         continue;
      }

      size_t nir_src_reg = nir_alu_src_regindex(alu, u);

      CHECK(rogue_instr_set_operand_vreg(instr, u + 1, nir_src_reg));
   }

   return true;
}

static bool trans_nir_alu_fmin(struct rogue_shader *shader, nir_alu_instr *alu)
{
   /* Src validation. */
   assert(nir_src_num_components(alu->src[0].src) == 1);
   assert(nir_src_bit_size(alu->src[0].src) == 32);

   assert(nir_src_num_components(alu->src[1].src) == 1);
   assert(nir_src_bit_size(alu->src[1].src) == 32);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_MIN);

   CHECK(setup_alu_dest(instr, 0, alu));

   for (size_t u = 0; u < nir_op_infos[nir_op_fmin].num_inputs; ++u) {
      /* Handle values that can be pulled from const regs. */
      if (nir_alu_src_is_const(alu, u)) {
         CHECK(trans_constreg_operand(instr, u + 1, nir_alu_src_const(alu, u)));
         continue;
      }

      size_t nir_src_reg = nir_alu_src_regindex(alu, u);

      CHECK(rogue_instr_set_operand_vreg(instr, u + 1, nir_src_reg));
   }

   return true;
}

static bool trans_nir_alu_mov_imm(struct rogue_shader *shader,
                                  nir_alu_instr *alu)
{
   /* Src validation. */
   assert(nir_src_num_components(alu->src[0].src) == 1);
   assert(nir_src_bit_size(alu->src[0].src) == 32);

   uint32_t value = nir_alu_src_const(alu, 0);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_MOV_IMM);

   CHECK(setup_alu_dest(instr, 0, alu));
   CHECK(rogue_instr_set_operand_imm(instr, 1, value));

   return true;
}

static bool trans_nir_alu_mov(struct rogue_shader *shader, nir_alu_instr *alu)
{
   /* Constant value that isn't in constregs. */
   if (nir_alu_src_is_const(alu, 0) &&
       nir_dest_num_components(alu->dest.dest) == 1)
      return trans_nir_alu_mov_imm(shader, alu);

   /* Src validation. */
   assert(nir_src_num_components(alu->src[0].src) == 1);
   assert(nir_src_bit_size(alu->src[0].src) == 32);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_MOV);

   CHECK(setup_alu_dest(instr, 0, alu));

   /* Handle values that can be pulled from const regs. */
   if (nir_alu_src_is_const(alu, 0)) {
      return trans_constreg_operand(instr, 1, nir_alu_src_const(alu, 0));
   }

   size_t nir_src_reg = nir_alu_src_regindex(alu, 0);
   CHECK(rogue_instr_set_operand_vreg(instr, 1, nir_src_reg));

   return true;
}

static bool trans_nir_alu_pack_unorm_4x8(struct rogue_shader *shader,
                                         nir_alu_instr *alu)
{
   /* Src/dest validation. */
   assert(nir_dest_num_components(alu->dest.dest) == 1);
   assert(nir_dest_bit_size(alu->dest.dest) == 32);

   assert(nir_src_num_components(alu->src[0].src) == 4);
   assert(nir_src_bit_size(alu->src[0].src) == 32);

   size_t nir_src_reg = nir_alu_src_regindex(alu, 0);
   size_t nir_dest_reg = nir_alu_dest_regindex(alu);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_PACK_U8888);

   CHECK(rogue_instr_set_operand_vreg(instr, 0, nir_dest_reg));

   /* Ensure all 4 components are being sourced in order. */
   for (size_t u = 0; u < nir_src_num_components(alu->src[0].src); ++u)
      assert(alu->src->swizzle[u] == u);

   CHECK(rogue_instr_set_operand_vreg_vec(instr,
                                          1,
                                          ROGUE_COMPONENT_ALL,
                                          nir_src_reg));

   return true;
}

static bool trans_nir_alu_fmul(struct rogue_shader *shader, nir_alu_instr *alu)
{
   /* Src validation. */
   assert(nir_src_num_components(alu->src[0].src) == 1);
   assert(nir_src_bit_size(alu->src[0].src) == 32);

   assert(nir_src_num_components(alu->src[1].src) == 1);
   assert(nir_src_bit_size(alu->src[1].src) == 32);

   size_t nir_in_reg_a = nir_alu_src_regindex(alu, 0);
   size_t nir_in_reg_b = nir_alu_src_regindex(alu, 1);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_MUL);

   CHECK(setup_alu_dest(instr, 0, alu));
   CHECK(rogue_instr_set_operand_vreg(instr, 1, nir_in_reg_a));
   CHECK(rogue_instr_set_operand_vreg(instr, 2, nir_in_reg_b));

   return true;
}

static bool trans_nir_alu_ffma(struct rogue_shader *shader, nir_alu_instr *alu)
{
   /* Src validation. */
   assert(nir_src_num_components(alu->src[0].src) == 1);
   assert(nir_src_bit_size(alu->src[0].src) == 32);

   assert(nir_src_num_components(alu->src[1].src) == 1);
   assert(nir_src_bit_size(alu->src[1].src) == 32);

   assert(nir_src_num_components(alu->src[2].src) == 1);
   assert(nir_src_bit_size(alu->src[2].src) == 32);

   size_t nir_in_reg_a = nir_alu_src_regindex(alu, 0);
   size_t nir_in_reg_b = nir_alu_src_regindex(alu, 1);
   size_t nir_in_reg_c = nir_alu_src_regindex(alu, 2);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_FMA);

   CHECK(setup_alu_dest(instr, 0, alu));
   CHECK(rogue_instr_set_operand_vreg(instr, 1, nir_in_reg_a));
   CHECK(rogue_instr_set_operand_vreg(instr, 2, nir_in_reg_b));
   CHECK(rogue_instr_set_operand_vreg(instr, 3, nir_in_reg_c));

   return true;
}

static bool trans_nir_alu(struct rogue_shader *shader, nir_alu_instr *alu)
{
   switch (alu->op) {
   case nir_op_fmax:
      return trans_nir_alu_fmax(shader, alu);

   case nir_op_fmin:
      return trans_nir_alu_fmin(shader, alu);

   case nir_op_pack_unorm_4x8:
      return trans_nir_alu_pack_unorm_4x8(shader, alu);

   case nir_op_mov:
      return trans_nir_alu_mov(shader, alu);

   case nir_op_fmul:
      return trans_nir_alu_fmul(shader, alu);

   case nir_op_ffma:
      return trans_nir_alu_ffma(shader, alu);

   default:
      break;
   }

   unreachable("Unimplemented NIR ALU instruction.");
}

static bool trans_nir_intrinsic_load_input_fs(struct rogue_shader *shader,
                                              nir_intrinsic_instr *intr)
{
   struct rogue_fs_build_data *fs_data = &shader->ctx->stage_data.fs;

   /* Src/dest validation. */
   assert(nir_dest_num_components(intr->dest) == 1);
   assert(nir_dest_bit_size(intr->dest) == 32);

   assert(nir_src_num_components(intr->src[0]) == 1);
   assert(nir_src_bit_size(intr->src[0]) == 32);
   assert(nir_intr_src_is_const(intr, 0));

   /* Intrinsic index validation. */
   assert(nir_intrinsic_dest_type(intr) == nir_type_float32);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   size_t component = nir_intrinsic_component(intr);
   size_t coeff_index = rogue_coeff_index_fs(&fs_data->iterator_args,
                                             io_semantics.location,
                                             component);
   size_t wcoeff_index = rogue_coeff_index_fs(&fs_data->iterator_args, ~0, 0);
   size_t drc_num = rogue_acquire_drc(shader);
   uint64_t source_count = nir_dest_num_components(intr->dest);

   size_t nir_dest_reg = nir_intr_dest_regindex(intr);

   /* pixiter.w instruction. */
   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_PIX_ITER_W);

   CHECK(rogue_instr_set_operand_vreg(instr, 0, nir_dest_reg));
   CHECK(rogue_instr_set_operand_drc(instr, 1, drc_num));
   CHECK(rogue_instr_set_operand_reg(instr,
                                     2,
                                     ROGUE_OPERAND_TYPE_REG_COEFF,
                                     coeff_index));
   CHECK(rogue_instr_set_operand_reg(instr,
                                     3,
                                     ROGUE_OPERAND_TYPE_REG_COEFF,
                                     wcoeff_index));
   CHECK(rogue_instr_set_operand_imm(instr, 4, source_count));

   /* wdf instruction must follow the pixiter.w. */
   instr = rogue_shader_insert(shader, ROGUE_OP_WDF);

   CHECK(rogue_instr_set_operand_drc(instr, 0, drc_num));
   rogue_release_drc(shader, drc_num);

   return true;
}

static bool trans_nir_intrinsic_load_input_vs(struct rogue_shader *shader,
                                              nir_intrinsic_instr *intr)
{
   /* Src/dest validation. */
   assert(nir_dest_num_components(intr->dest) == 1);
   assert(nir_dest_bit_size(intr->dest) == 32);

   assert(nir_src_num_components(intr->src[0]) == 1);
   assert(nir_src_bit_size(intr->src[0]) == 32);
   assert(nir_intr_src_is_const(intr, 0));

   /* Intrinsic index validation. */
   assert(nir_intrinsic_dest_type(intr) == nir_type_float32);

   size_t component = nir_intrinsic_component(intr);
   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   size_t vi_reg_index = ((io_semantics.location - VERT_ATTRIB_GENERIC0) * 3) +
                         component; /* TODO: get these properly with the
                                     * intrinsic index (ssa argument)
                                     */

   size_t nir_dest_reg = nir_intr_dest_regindex(intr);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_MOV);

   CHECK(rogue_instr_set_operand_vreg(instr, 0, nir_dest_reg));
   CHECK(rogue_instr_set_operand_reg(instr,
                                     1,
                                     ROGUE_OPERAND_TYPE_REG_VERTEX_IN,
                                     vi_reg_index));

   return true;
}

static bool trans_nir_intrinsic_load_input(struct rogue_shader *shader,
                                           nir_intrinsic_instr *intr)
{
   switch (shader->stage) {
   case MESA_SHADER_FRAGMENT:
      return trans_nir_intrinsic_load_input_fs(shader, intr);

   case MESA_SHADER_VERTEX:
      return trans_nir_intrinsic_load_input_vs(shader, intr);

   default:
      break;
   }

   unreachable("Unimplemented NIR load_input variant.");
}

static bool trans_nir_intrinsic_store_output_fs(struct rogue_shader *shader,
                                                nir_intrinsic_instr *intr)
{
   /* Src/dest validation. */
   assert(nir_src_num_components(intr->src[0]) == 1);
   assert(nir_src_bit_size(intr->src[0]) == 32);
   assert(!nir_intr_src_is_const(intr, 0));

   assert(nir_src_num_components(intr->src[1]) == 1);
   assert(nir_src_bit_size(intr->src[1]) == 32);
   assert(nir_intr_src_is_const(intr, 1));

   /* Intrinsic index validation. */
   assert(nir_intrinsic_src_type(intr) == nir_type_uint32);

   /* Fetch the output offset. */
   /* TODO: Is this really the right value to use for pixel out reg. num? */
   size_t offset = nir_intr_src_const(intr, 1);

   /* Fetch the components. */
   size_t src_reg = nir_intr_src_regindex(intr, 0);

   /* mov.olchk instruction. */
   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_MOV);

   CHECK(rogue_instr_set_operand_reg(instr,
                                     0,
                                     ROGUE_OPERAND_TYPE_REG_PIXEL_OUT,
                                     offset));
   CHECK(rogue_instr_set_operand_vreg(instr, 1, src_reg));
   CHECK(rogue_instr_set_flag(instr, ROGUE_INSTR_FLAG_OLCHK));

   return true;
}

static bool trans_nir_intrinsic_store_output_vs(struct rogue_shader *shader,
                                                nir_intrinsic_instr *intr)
{
   struct rogue_vs_build_data *vs_data = &shader->ctx->stage_data.vs;

   /* Src/dest validation. */
   assert(nir_src_num_components(intr->src[0]) == 1);
   assert(nir_src_bit_size(intr->src[0]) == 32);
   assert(!nir_intr_src_is_const(intr, 0));

   assert(nir_src_num_components(intr->src[1]) == 1);
   assert(nir_src_bit_size(intr->src[1]) == 32);
   assert(nir_intr_src_is_const(intr, 1));

   /* Intrinsic index validation. */
   assert(nir_intrinsic_src_type(intr) == nir_type_float32);
   assert(util_bitcount(nir_intrinsic_write_mask(intr)) == 1);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   size_t component = nir_intrinsic_component(intr);
   size_t vo_index = rogue_output_index_vs(&vs_data->outputs,
                                           io_semantics.location,
                                           component);

   size_t src_reg = nir_intr_src_regindex(intr, 0);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_VTXOUT);

   CHECK(rogue_instr_set_operand_imm(instr, 0, vo_index));
   CHECK(rogue_instr_set_operand_vreg(instr, 1, src_reg));

   return true;
}

static bool trans_nir_intrinsic_store_output(struct rogue_shader *shader,
                                             nir_intrinsic_instr *intr)
{
   switch (shader->stage) {
   case MESA_SHADER_FRAGMENT:
      return trans_nir_intrinsic_store_output_fs(shader, intr);

   case MESA_SHADER_VERTEX:
      return trans_nir_intrinsic_store_output_vs(shader, intr);

   default:
      break;
   }

   unreachable("Unimplemented NIR store_output variant.");
}

static bool trans_nir_intrinsic_load_ubo(struct rogue_shader *shader,
                                         nir_intrinsic_instr *intr)
{
   struct rogue_ubo_data *ubo_data =
      &shader->ctx->common_data[shader->stage].ubo_data;

   /* Src/dest validation. */
   assert(nir_dest_num_components(intr->dest) == 1);
   assert(nir_dest_bit_size(intr->dest) == 32);

   assert(nir_src_num_components(intr->src[0]) == 2);
   assert(nir_src_bit_size(intr->src[0]) == 32);
   assert(nir_intr_src_is_const(intr, 0));

   assert(nir_src_num_components(intr->src[1]) == 1);
   assert(nir_src_bit_size(intr->src[1]) == 32);
   assert(nir_intr_src_is_const(intr, 1));

   /* Intrinsic index validation. */
   assert((nir_intrinsic_range_base(intr) % ROGUE_REG_SIZE_BYTES) == 0);
   assert(nir_intrinsic_range(intr) == ROGUE_REG_SIZE_BYTES);

   size_t nir_dest_reg = nir_intr_dest_regindex(intr);

   size_t desc_set = nir_intr_src_comp_const(intr, 0, 0);
   size_t binding = nir_intr_src_comp_const(intr, 0, 1);
   size_t offset = nir_intrinsic_range_base(intr);

   size_t sh_num = rogue_ubo_reg(ubo_data, desc_set, binding, offset);

   struct rogue_instr *instr = rogue_shader_insert(shader, ROGUE_OP_MOV);

   CHECK(rogue_instr_set_operand_vreg(instr, 0, nir_dest_reg));
   CHECK(rogue_instr_set_operand_reg(instr,
                                     1,
                                     ROGUE_OPERAND_TYPE_REG_SHARED,
                                     sh_num));
   return true;
}

static bool trans_nir_intrinsic(struct rogue_shader *shader,
                                nir_intrinsic_instr *intr)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_input:
      return trans_nir_intrinsic_load_input(shader, intr);

   case nir_intrinsic_store_output:
      return trans_nir_intrinsic_store_output(shader, intr);

   case nir_intrinsic_load_ubo:
      return trans_nir_intrinsic_load_ubo(shader, intr);

   default:
      break;
   }

   unreachable("Unimplemented NIR intrinsic instruction.");
}

static bool trans_nir_load_const(struct rogue_shader *shader,
                                 nir_load_const_instr *load_const)
{
   /* Src/dest validation. */
   assert(load_const->def.bit_size == 32);

   /* Ensure that two-component load_consts are used only by load_ubos. */
   if (load_const->def.num_components == 2) {
      nir_foreach_use (use_src, &load_const->def) {
         nir_instr *instr = use_src->parent_instr;
         assert(instr->type == nir_instr_type_intrinsic);

         ASSERTED nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         assert(intr->intrinsic == nir_intrinsic_load_ubo);
      }
   } else {
      assert(load_const->def.num_components == 1);
   }

   /* TODO: This is currently done in MOV_IMM, but instead now would be the
    * time to lookup the constant value, see if it lives in const regs, or if
    * it needs to generate a MOV_IMM (or be constant calc-ed).
    */
   return true;
}

static bool trans_nir_jump_return(struct rogue_shader *shader,
                                  nir_jump_instr *jump)
{
   enum rogue_opcode return_op;

   switch (shader->stage) {
   case MESA_SHADER_FRAGMENT:
      return_op = ROGUE_OP_END_FRAG;
      break;

   case MESA_SHADER_VERTEX:
      return_op = ROGUE_OP_END_VERT;
      break;

   default:
      unreachable("Unimplemented NIR return instruction type.");
   }

   rogue_shader_insert(shader, return_op);

   return true;
}

static bool trans_nir_jump(struct rogue_shader *shader, nir_jump_instr *jump)
{
   switch (jump->type) {
   case nir_jump_return:
      return trans_nir_jump_return(shader, jump);

   default:
      break;
   }

   unreachable("Unimplemented NIR jump instruction type.");
}

/**
 * \brief Converts a NIR shader to Rogue.
 *
 * \param[in] ctx Shared multi-stage build context.
 * \param[in] nir NIR shader.
 * \return A rogue_shader* if successful, or NULL if unsuccessful.
 */
struct rogue_shader *rogue_nir_to_rogue(struct rogue_build_ctx *ctx,
                                        const nir_shader *nir)
{
   gl_shader_stage stage = nir->info.stage;
   struct rogue_shader *shader = rogue_shader_create(ctx, stage);
   if (!shader)
      return NULL;

   /* Make sure we only have a single function. */
   assert(exec_list_length(&nir->functions) == 1);

   /* Translate shader entrypoint. */
   nir_function_impl *entry = nir_shader_get_entrypoint((nir_shader *)nir);
   nir_foreach_block (block, entry) {
      nir_foreach_instr (instr, block) {
         switch (instr->type) {
         case nir_instr_type_alu:
            /* TODO: Cleanup on failure. */
            CHECKF(trans_nir_alu(shader, nir_instr_as_alu(instr)),
                   "Failed to translate NIR ALU instruction.");
            break;

         case nir_instr_type_intrinsic:
            CHECKF(trans_nir_intrinsic(shader, nir_instr_as_intrinsic(instr)),
                   "Failed to translate NIR intrinsic instruction.");
            break;

         case nir_instr_type_load_const:
            CHECKF(trans_nir_load_const(shader, nir_instr_as_load_const(instr)),
                   "Failed to translate NIR load_const instruction.");
            break;

         case nir_instr_type_jump:
            CHECKF(trans_nir_jump(shader, nir_instr_as_jump(instr)),
                   "Failed to translate NIR jump instruction.");
            break;

         default:
            unreachable("Unimplemented NIR instruction type.");
         }
      }
   }

   /* Perform register allocation. */
   /* TODO: handle failure. */
   if (!rogue_ra_alloc(&shader->instr_list,
                       shader->ra,
                       &ctx->common_data[stage].temps,
                       &ctx->common_data[stage].internals))
      return NULL;

   return shader;
}

/**
 * \brief Creates and sets up a shared multi-stage build context.
 *
 * \param[in] compiler The compiler context.
 * \return A pointer to the new build context, or NULL on failure.
 */
struct rogue_build_ctx *
rogue_create_build_context(struct rogue_compiler *compiler)
{
   struct rogue_build_ctx *ctx;

   ctx = rzalloc_size(compiler, sizeof(*ctx));
   if (!ctx)
      return NULL;

   ctx->compiler = compiler;

   /* nir/rogue/binary shaders need to be default-zeroed;
    * this is taken care of by rzalloc_size.
    */

   /* Setup non-zero defaults. */
   ctx->stage_data.fs.msaa_mode = ROGUE_MSAA_MODE_PIXEL;

   return ctx;
}
