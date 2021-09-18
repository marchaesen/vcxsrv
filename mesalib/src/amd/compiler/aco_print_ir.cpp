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

#include "aco_builder.h"
#include "aco_ir.h"

#include "common/ac_shader_util.h"
#include "common/sid.h"

#include <array>

namespace aco {

const std::array<const char*, num_reduce_ops> reduce_ops = []()
{
   std::array<const char*, num_reduce_ops> ret{};
   ret[iadd8] = "iadd8";
   ret[iadd16] = "iadd16";
   ret[iadd32] = "iadd32";
   ret[iadd64] = "iadd64";
   ret[imul8] = "imul8";
   ret[imul16] = "imul16";
   ret[imul32] = "imul32";
   ret[imul64] = "imul64";
   ret[fadd16] = "fadd16";
   ret[fadd32] = "fadd32";
   ret[fadd64] = "fadd64";
   ret[fmul16] = "fmul16";
   ret[fmul32] = "fmul32";
   ret[fmul64] = "fmul64";
   ret[imin8] = "imin8";
   ret[imin16] = "imin16";
   ret[imin32] = "imin32";
   ret[imin64] = "imin64";
   ret[imax8] = "imax8";
   ret[imax16] = "imax16";
   ret[imax32] = "imax32";
   ret[imax64] = "imax64";
   ret[umin8] = "umin8";
   ret[umin16] = "umin16";
   ret[umin32] = "umin32";
   ret[umin64] = "umin64";
   ret[umax8] = "umax8";
   ret[umax16] = "umax16";
   ret[umax32] = "umax32";
   ret[umax64] = "umax64";
   ret[fmin16] = "fmin16";
   ret[fmin32] = "fmin32";
   ret[fmin64] = "fmin64";
   ret[fmax16] = "fmax16";
   ret[fmax32] = "fmax32";
   ret[fmax64] = "fmax64";
   ret[iand8] = "iand8";
   ret[iand16] = "iand16";
   ret[iand32] = "iand32";
   ret[iand64] = "iand64";
   ret[ior8] = "ior8";
   ret[ior16] = "ior16";
   ret[ior32] = "ior32";
   ret[ior64] = "ior64";
   ret[ixor8] = "ixor8";
   ret[ixor16] = "ixor16";
   ret[ixor32] = "ixor32";
   ret[ixor64] = "ixor64";
   return ret;
}();

static void
print_reg_class(const RegClass rc, FILE* output)
{
   if (rc.is_subdword()) {
      fprintf(output, " v%ub: ", rc.bytes());
   } else if (rc.type() == RegType::sgpr) {
      fprintf(output, " s%u: ", rc.size());
   } else if (rc.is_linear()) {
      fprintf(output, " lv%u: ", rc.size());
   } else {
      fprintf(output, " v%u: ", rc.size());
   }
}

void
print_physReg(PhysReg reg, unsigned bytes, FILE* output, unsigned flags)
{
   if (reg == 124) {
      fprintf(output, "m0");
   } else if (reg == 106) {
      fprintf(output, "vcc");
   } else if (reg == 253) {
      fprintf(output, "scc");
   } else if (reg == 126) {
      fprintf(output, "exec");
   } else {
      bool is_vgpr = reg / 256;
      unsigned r = reg % 256;
      unsigned size = DIV_ROUND_UP(bytes, 4);
      if (size == 1 && (flags & print_no_ssa)) {
         fprintf(output, "%c%d", is_vgpr ? 'v' : 's', r);
      } else {
         fprintf(output, "%c[%d", is_vgpr ? 'v' : 's', r);
         if (size > 1)
            fprintf(output, "-%d]", r + size - 1);
         else
            fprintf(output, "]");
      }
      if (reg.byte() || bytes % 4)
         fprintf(output, "[%d:%d]", reg.byte() * 8, (reg.byte() + bytes) * 8);
   }
}

static void
print_constant(uint8_t reg, FILE* output)
{
   if (reg >= 128 && reg <= 192) {
      fprintf(output, "%d", reg - 128);
      return;
   } else if (reg >= 192 && reg <= 208) {
      fprintf(output, "%d", 192 - reg);
      return;
   }

   switch (reg) {
   case 240: fprintf(output, "0.5"); break;
   case 241: fprintf(output, "-0.5"); break;
   case 242: fprintf(output, "1.0"); break;
   case 243: fprintf(output, "-1.0"); break;
   case 244: fprintf(output, "2.0"); break;
   case 245: fprintf(output, "-2.0"); break;
   case 246: fprintf(output, "4.0"); break;
   case 247: fprintf(output, "-4.0"); break;
   case 248: fprintf(output, "1/(2*PI)"); break;
   }
}

void
aco_print_operand(const Operand* operand, FILE* output, unsigned flags)
{
   if (operand->isLiteral() || (operand->isConstant() && operand->bytes() == 1)) {
      if (operand->bytes() == 1)
         fprintf(output, "0x%.2x", operand->constantValue());
      else if (operand->bytes() == 2)
         fprintf(output, "0x%.4x", operand->constantValue());
      else
         fprintf(output, "0x%x", operand->constantValue());
   } else if (operand->isConstant()) {
      print_constant(operand->physReg().reg(), output);
   } else if (operand->isUndefined()) {
      print_reg_class(operand->regClass(), output);
      fprintf(output, "undef");
   } else {
      if (operand->isLateKill())
         fprintf(output, "(latekill)");
      if (operand->is16bit())
         fprintf(output, "(is16bit)");
      if (operand->is24bit())
         fprintf(output, "(is24bit)");
      if ((flags & print_kill) && operand->isKill())
         fprintf(output, "(kill)");

      if (!(flags & print_no_ssa))
         fprintf(output, "%%%d%s", operand->tempId(), operand->isFixed() ? ":" : "");

      if (operand->isFixed())
         print_physReg(operand->physReg(), operand->bytes(), output, flags);
   }
}

static void
print_definition(const Definition* definition, FILE* output, unsigned flags)
{
   if (!(flags & print_no_ssa))
      print_reg_class(definition->regClass(), output);
   if (definition->isPrecise())
      fprintf(output, "(precise)");
   if (definition->isNUW())
      fprintf(output, "(nuw)");
   if (definition->isNoCSE())
      fprintf(output, "(noCSE)");
   if ((flags & print_kill) && definition->isKill())
      fprintf(output, "(kill)");
   if (!(flags & print_no_ssa))
      fprintf(output, "%%%d%s", definition->tempId(), definition->isFixed() ? ":" : "");

   if (definition->isFixed())
      print_physReg(definition->physReg(), definition->bytes(), output, flags);
}

static void
print_storage(storage_class storage, FILE* output)
{
   fprintf(output, " storage:");
   int printed = 0;
   if (storage & storage_buffer)
      printed += fprintf(output, "%sbuffer", printed ? "," : "");
   if (storage & storage_atomic_counter)
      printed += fprintf(output, "%satomic_counter", printed ? "," : "");
   if (storage & storage_image)
      printed += fprintf(output, "%simage", printed ? "," : "");
   if (storage & storage_shared)
      printed += fprintf(output, "%sshared", printed ? "," : "");
   if (storage & storage_vmem_output)
      printed += fprintf(output, "%svmem_output", printed ? "," : "");
   if (storage & storage_scratch)
      printed += fprintf(output, "%sscratch", printed ? "," : "");
   if (storage & storage_vgpr_spill)
      printed += fprintf(output, "%svgpr_spill", printed ? "," : "");
}

static void
print_semantics(memory_semantics sem, FILE* output)
{
   fprintf(output, " semantics:");
   int printed = 0;
   if (sem & semantic_acquire)
      printed += fprintf(output, "%sacquire", printed ? "," : "");
   if (sem & semantic_release)
      printed += fprintf(output, "%srelease", printed ? "," : "");
   if (sem & semantic_volatile)
      printed += fprintf(output, "%svolatile", printed ? "," : "");
   if (sem & semantic_private)
      printed += fprintf(output, "%sprivate", printed ? "," : "");
   if (sem & semantic_can_reorder)
      printed += fprintf(output, "%sreorder", printed ? "," : "");
   if (sem & semantic_atomic)
      printed += fprintf(output, "%satomic", printed ? "," : "");
   if (sem & semantic_rmw)
      printed += fprintf(output, "%srmw", printed ? "," : "");
}

static void
print_scope(sync_scope scope, FILE* output, const char* prefix = "scope")
{
   fprintf(output, " %s:", prefix);
   switch (scope) {
   case scope_invocation: fprintf(output, "invocation"); break;
   case scope_subgroup: fprintf(output, "subgroup"); break;
   case scope_workgroup: fprintf(output, "workgroup"); break;
   case scope_queuefamily: fprintf(output, "queuefamily"); break;
   case scope_device: fprintf(output, "device"); break;
   }
}

static void
print_sync(memory_sync_info sync, FILE* output)
{
   print_storage(sync.storage, output);
   print_semantics(sync.semantics, output);
   print_scope(sync.scope, output);
}

static void
print_instr_format_specific(const Instruction* instr, FILE* output)
{
   switch (instr->format) {
   case Format::SOPK: {
      const SOPK_instruction& sopk = instr->sopk();
      fprintf(output, " imm:%d", sopk.imm & 0x8000 ? (sopk.imm - 65536) : sopk.imm);
      break;
   }
   case Format::SOPP: {
      uint16_t imm = instr->sopp().imm;
      switch (instr->opcode) {
      case aco_opcode::s_waitcnt: {
         /* we usually should check the chip class for vmcnt/lgkm, but
          * insert_waitcnt() should fill it in regardless. */
         unsigned vmcnt = (imm & 0xF) | ((imm & (0x3 << 14)) >> 10);
         if (vmcnt != 63)
            fprintf(output, " vmcnt(%d)", vmcnt);
         if (((imm >> 4) & 0x7) < 0x7)
            fprintf(output, " expcnt(%d)", (imm >> 4) & 0x7);
         if (((imm >> 8) & 0x3F) < 0x3F)
            fprintf(output, " lgkmcnt(%d)", (imm >> 8) & 0x3F);
         break;
      }
      case aco_opcode::s_endpgm:
      case aco_opcode::s_endpgm_saved:
      case aco_opcode::s_endpgm_ordered_ps_done:
      case aco_opcode::s_wakeup:
      case aco_opcode::s_barrier:
      case aco_opcode::s_icache_inv:
      case aco_opcode::s_ttracedata:
      case aco_opcode::s_set_gpr_idx_off: {
         break;
      }
      case aco_opcode::s_sendmsg: {
         unsigned id = imm & sendmsg_id_mask;
         switch (id) {
         case sendmsg_none: fprintf(output, " sendmsg(MSG_NONE)"); break;
         case _sendmsg_gs:
            fprintf(output, " sendmsg(gs%s%s, %u)", imm & 0x10 ? ", cut" : "",
                    imm & 0x20 ? ", emit" : "", imm >> 8);
            break;
         case _sendmsg_gs_done:
            fprintf(output, " sendmsg(gs_done%s%s, %u)", imm & 0x10 ? ", cut" : "",
                    imm & 0x20 ? ", emit" : "", imm >> 8);
            break;
         case sendmsg_save_wave: fprintf(output, " sendmsg(save_wave)"); break;
         case sendmsg_stall_wave_gen: fprintf(output, " sendmsg(stall_wave_gen)"); break;
         case sendmsg_halt_waves: fprintf(output, " sendmsg(halt_waves)"); break;
         case sendmsg_ordered_ps_done: fprintf(output, " sendmsg(ordered_ps_done)"); break;
         case sendmsg_early_prim_dealloc: fprintf(output, " sendmsg(early_prim_dealloc)"); break;
         case sendmsg_gs_alloc_req: fprintf(output, " sendmsg(gs_alloc_req)"); break;
         }
         break;
      }
      default: {
         if (imm)
            fprintf(output, " imm:%u", imm);
         break;
      }
      }
      if (instr->sopp().block != -1)
         fprintf(output, " block:BB%d", instr->sopp().block);
      break;
   }
   case Format::SMEM: {
      const SMEM_instruction& smem = instr->smem();
      if (smem.glc)
         fprintf(output, " glc");
      if (smem.dlc)
         fprintf(output, " dlc");
      if (smem.nv)
         fprintf(output, " nv");
      print_sync(smem.sync, output);
      break;
   }
   case Format::VINTRP: {
      const Interp_instruction& vintrp = instr->vintrp();
      fprintf(output, " attr%d.%c", vintrp.attribute, "xyzw"[vintrp.component]);
      break;
   }
   case Format::DS: {
      const DS_instruction& ds = instr->ds();
      if (ds.offset0)
         fprintf(output, " offset0:%u", ds.offset0);
      if (ds.offset1)
         fprintf(output, " offset1:%u", ds.offset1);
      if (ds.gds)
         fprintf(output, " gds");
      print_sync(ds.sync, output);
      break;
   }
   case Format::MUBUF: {
      const MUBUF_instruction& mubuf = instr->mubuf();
      if (mubuf.offset)
         fprintf(output, " offset:%u", mubuf.offset);
      if (mubuf.offen)
         fprintf(output, " offen");
      if (mubuf.idxen)
         fprintf(output, " idxen");
      if (mubuf.addr64)
         fprintf(output, " addr64");
      if (mubuf.glc)
         fprintf(output, " glc");
      if (mubuf.dlc)
         fprintf(output, " dlc");
      if (mubuf.slc)
         fprintf(output, " slc");
      if (mubuf.tfe)
         fprintf(output, " tfe");
      if (mubuf.lds)
         fprintf(output, " lds");
      if (mubuf.disable_wqm)
         fprintf(output, " disable_wqm");
      print_sync(mubuf.sync, output);
      break;
   }
   case Format::MIMG: {
      const MIMG_instruction& mimg = instr->mimg();
      unsigned identity_dmask =
         !instr->definitions.empty() ? (1 << instr->definitions[0].size()) - 1 : 0xf;
      if ((mimg.dmask & identity_dmask) != identity_dmask)
         fprintf(output, " dmask:%s%s%s%s", mimg.dmask & 0x1 ? "x" : "",
                 mimg.dmask & 0x2 ? "y" : "", mimg.dmask & 0x4 ? "z" : "",
                 mimg.dmask & 0x8 ? "w" : "");
      switch (mimg.dim) {
      case ac_image_1d: fprintf(output, " 1d"); break;
      case ac_image_2d: fprintf(output, " 2d"); break;
      case ac_image_3d: fprintf(output, " 3d"); break;
      case ac_image_cube: fprintf(output, " cube"); break;
      case ac_image_1darray: fprintf(output, " 1darray"); break;
      case ac_image_2darray: fprintf(output, " 2darray"); break;
      case ac_image_2dmsaa: fprintf(output, " 2dmsaa"); break;
      case ac_image_2darraymsaa: fprintf(output, " 2darraymsaa"); break;
      }
      if (mimg.unrm)
         fprintf(output, " unrm");
      if (mimg.glc)
         fprintf(output, " glc");
      if (mimg.dlc)
         fprintf(output, " dlc");
      if (mimg.slc)
         fprintf(output, " slc");
      if (mimg.tfe)
         fprintf(output, " tfe");
      if (mimg.da)
         fprintf(output, " da");
      if (mimg.lwe)
         fprintf(output, " lwe");
      if (mimg.r128 || mimg.a16)
         fprintf(output, " r128/a16");
      if (mimg.d16)
         fprintf(output, " d16");
      if (mimg.disable_wqm)
         fprintf(output, " disable_wqm");
      print_sync(mimg.sync, output);
      break;
   }
   case Format::EXP: {
      const Export_instruction& exp = instr->exp();
      unsigned identity_mask = exp.compressed ? 0x5 : 0xf;
      if ((exp.enabled_mask & identity_mask) != identity_mask)
         fprintf(output, " en:%c%c%c%c", exp.enabled_mask & 0x1 ? 'r' : '*',
                 exp.enabled_mask & 0x2 ? 'g' : '*', exp.enabled_mask & 0x4 ? 'b' : '*',
                 exp.enabled_mask & 0x8 ? 'a' : '*');
      if (exp.compressed)
         fprintf(output, " compr");
      if (exp.done)
         fprintf(output, " done");
      if (exp.valid_mask)
         fprintf(output, " vm");

      if (exp.dest <= V_008DFC_SQ_EXP_MRT + 7)
         fprintf(output, " mrt%d", exp.dest - V_008DFC_SQ_EXP_MRT);
      else if (exp.dest == V_008DFC_SQ_EXP_MRTZ)
         fprintf(output, " mrtz");
      else if (exp.dest == V_008DFC_SQ_EXP_NULL)
         fprintf(output, " null");
      else if (exp.dest >= V_008DFC_SQ_EXP_POS && exp.dest <= V_008DFC_SQ_EXP_POS + 3)
         fprintf(output, " pos%d", exp.dest - V_008DFC_SQ_EXP_POS);
      else if (exp.dest >= V_008DFC_SQ_EXP_PARAM && exp.dest <= V_008DFC_SQ_EXP_PARAM + 31)
         fprintf(output, " param%d", exp.dest - V_008DFC_SQ_EXP_PARAM);
      break;
   }
   case Format::PSEUDO_BRANCH: {
      const Pseudo_branch_instruction& branch = instr->branch();
      /* Note: BB0 cannot be a branch target */
      if (branch.target[0] != 0)
         fprintf(output, " BB%d", branch.target[0]);
      if (branch.target[1] != 0)
         fprintf(output, ", BB%d", branch.target[1]);
      break;
   }
   case Format::PSEUDO_REDUCTION: {
      const Pseudo_reduction_instruction& reduce = instr->reduction();
      fprintf(output, " op:%s", reduce_ops[reduce.reduce_op]);
      if (reduce.cluster_size)
         fprintf(output, " cluster_size:%u", reduce.cluster_size);
      break;
   }
   case Format::PSEUDO_BARRIER: {
      const Pseudo_barrier_instruction& barrier = instr->barrier();
      print_sync(barrier.sync, output);
      print_scope(barrier.exec_scope, output, "exec_scope");
      break;
   }
   case Format::FLAT:
   case Format::GLOBAL:
   case Format::SCRATCH: {
      const FLAT_instruction& flat = instr->flatlike();
      if (flat.offset)
         fprintf(output, " offset:%u", flat.offset);
      if (flat.glc)
         fprintf(output, " glc");
      if (flat.dlc)
         fprintf(output, " dlc");
      if (flat.slc)
         fprintf(output, " slc");
      if (flat.lds)
         fprintf(output, " lds");
      if (flat.nv)
         fprintf(output, " nv");
      if (flat.disable_wqm)
         fprintf(output, " disable_wqm");
      print_sync(flat.sync, output);
      break;
   }
   case Format::MTBUF: {
      const MTBUF_instruction& mtbuf = instr->mtbuf();
      fprintf(output, " dfmt:");
      switch (mtbuf.dfmt) {
      case V_008F0C_BUF_DATA_FORMAT_8: fprintf(output, "8"); break;
      case V_008F0C_BUF_DATA_FORMAT_16: fprintf(output, "16"); break;
      case V_008F0C_BUF_DATA_FORMAT_8_8: fprintf(output, "8_8"); break;
      case V_008F0C_BUF_DATA_FORMAT_32: fprintf(output, "32"); break;
      case V_008F0C_BUF_DATA_FORMAT_16_16: fprintf(output, "16_16"); break;
      case V_008F0C_BUF_DATA_FORMAT_10_11_11: fprintf(output, "10_11_11"); break;
      case V_008F0C_BUF_DATA_FORMAT_11_11_10: fprintf(output, "11_11_10"); break;
      case V_008F0C_BUF_DATA_FORMAT_10_10_10_2: fprintf(output, "10_10_10_2"); break;
      case V_008F0C_BUF_DATA_FORMAT_2_10_10_10: fprintf(output, "2_10_10_10"); break;
      case V_008F0C_BUF_DATA_FORMAT_8_8_8_8: fprintf(output, "8_8_8_8"); break;
      case V_008F0C_BUF_DATA_FORMAT_32_32: fprintf(output, "32_32"); break;
      case V_008F0C_BUF_DATA_FORMAT_16_16_16_16: fprintf(output, "16_16_16_16"); break;
      case V_008F0C_BUF_DATA_FORMAT_32_32_32: fprintf(output, "32_32_32"); break;
      case V_008F0C_BUF_DATA_FORMAT_32_32_32_32: fprintf(output, "32_32_32_32"); break;
      case V_008F0C_BUF_DATA_FORMAT_RESERVED_15: fprintf(output, "reserved15"); break;
      }
      fprintf(output, " nfmt:");
      switch (mtbuf.nfmt) {
      case V_008F0C_BUF_NUM_FORMAT_UNORM: fprintf(output, "unorm"); break;
      case V_008F0C_BUF_NUM_FORMAT_SNORM: fprintf(output, "snorm"); break;
      case V_008F0C_BUF_NUM_FORMAT_USCALED: fprintf(output, "uscaled"); break;
      case V_008F0C_BUF_NUM_FORMAT_SSCALED: fprintf(output, "sscaled"); break;
      case V_008F0C_BUF_NUM_FORMAT_UINT: fprintf(output, "uint"); break;
      case V_008F0C_BUF_NUM_FORMAT_SINT: fprintf(output, "sint"); break;
      case V_008F0C_BUF_NUM_FORMAT_SNORM_OGL: fprintf(output, "snorm"); break;
      case V_008F0C_BUF_NUM_FORMAT_FLOAT: fprintf(output, "float"); break;
      }
      if (mtbuf.offset)
         fprintf(output, " offset:%u", mtbuf.offset);
      if (mtbuf.offen)
         fprintf(output, " offen");
      if (mtbuf.idxen)
         fprintf(output, " idxen");
      if (mtbuf.glc)
         fprintf(output, " glc");
      if (mtbuf.dlc)
         fprintf(output, " dlc");
      if (mtbuf.slc)
         fprintf(output, " slc");
      if (mtbuf.tfe)
         fprintf(output, " tfe");
      if (mtbuf.disable_wqm)
         fprintf(output, " disable_wqm");
      print_sync(mtbuf.sync, output);
      break;
   }
   case Format::VOP3P: {
      if (instr->vop3p().clamp)
         fprintf(output, " clamp");
      break;
   }
   default: {
      break;
   }
   }
   if (instr->isVOP3()) {
      const VOP3_instruction& vop3 = instr->vop3();
      switch (vop3.omod) {
      case 1: fprintf(output, " *2"); break;
      case 2: fprintf(output, " *4"); break;
      case 3: fprintf(output, " *0.5"); break;
      }
      if (vop3.clamp)
         fprintf(output, " clamp");
      if (vop3.opsel & (1 << 3))
         fprintf(output, " opsel_hi");
   } else if (instr->isDPP()) {
      const DPP_instruction& dpp = instr->dpp();
      if (dpp.dpp_ctrl <= 0xff) {
         fprintf(output, " quad_perm:[%d,%d,%d,%d]", dpp.dpp_ctrl & 0x3, (dpp.dpp_ctrl >> 2) & 0x3,
                 (dpp.dpp_ctrl >> 4) & 0x3, (dpp.dpp_ctrl >> 6) & 0x3);
      } else if (dpp.dpp_ctrl >= 0x101 && dpp.dpp_ctrl <= 0x10f) {
         fprintf(output, " row_shl:%d", dpp.dpp_ctrl & 0xf);
      } else if (dpp.dpp_ctrl >= 0x111 && dpp.dpp_ctrl <= 0x11f) {
         fprintf(output, " row_shr:%d", dpp.dpp_ctrl & 0xf);
      } else if (dpp.dpp_ctrl >= 0x121 && dpp.dpp_ctrl <= 0x12f) {
         fprintf(output, " row_ror:%d", dpp.dpp_ctrl & 0xf);
      } else if (dpp.dpp_ctrl == dpp_wf_sl1) {
         fprintf(output, " wave_shl:1");
      } else if (dpp.dpp_ctrl == dpp_wf_rl1) {
         fprintf(output, " wave_rol:1");
      } else if (dpp.dpp_ctrl == dpp_wf_sr1) {
         fprintf(output, " wave_shr:1");
      } else if (dpp.dpp_ctrl == dpp_wf_rr1) {
         fprintf(output, " wave_ror:1");
      } else if (dpp.dpp_ctrl == dpp_row_mirror) {
         fprintf(output, " row_mirror");
      } else if (dpp.dpp_ctrl == dpp_row_half_mirror) {
         fprintf(output, " row_half_mirror");
      } else if (dpp.dpp_ctrl == dpp_row_bcast15) {
         fprintf(output, " row_bcast:15");
      } else if (dpp.dpp_ctrl == dpp_row_bcast31) {
         fprintf(output, " row_bcast:31");
      } else {
         fprintf(output, " dpp_ctrl:0x%.3x", dpp.dpp_ctrl);
      }
      if (dpp.row_mask != 0xf)
         fprintf(output, " row_mask:0x%.1x", dpp.row_mask);
      if (dpp.bank_mask != 0xf)
         fprintf(output, " bank_mask:0x%.1x", dpp.bank_mask);
      if (dpp.bound_ctrl)
         fprintf(output, " bound_ctrl:1");
   } else if (instr->isSDWA()) {
      const SDWA_instruction& sdwa = instr->sdwa();
      switch (sdwa.omod) {
      case 1: fprintf(output, " *2"); break;
      case 2: fprintf(output, " *4"); break;
      case 3: fprintf(output, " *0.5"); break;
      }
      if (sdwa.clamp)
         fprintf(output, " clamp");
      if (!instr->isVOPC()) {
         char sext = sdwa.dst_sel.sign_extend() ? 's' : 'u';
         unsigned offset = sdwa.dst_sel.offset();
         if (instr->definitions[0].isFixed())
            offset += instr->definitions[0].physReg().byte();
         switch (sdwa.dst_sel.size()) {
         case 1: fprintf(output, " dst_sel:%cbyte%u", sext, offset); break;
         case 2: fprintf(output, " dst_sel:%cword%u", sext, offset >> 1); break;
         case 4: fprintf(output, " dst_sel:dword"); break;
         default: break;
         }
         if (instr->definitions[0].bytes() < 4)
            fprintf(output, " dst_preserve");
      }
      for (unsigned i = 0; i < std::min<unsigned>(2, instr->operands.size()); i++) {
         char sext = sdwa.sel[i].sign_extend() ? 's' : 'u';
         unsigned offset = sdwa.sel[i].offset();
         if (instr->operands[i].isFixed())
            offset += instr->operands[i].physReg().byte();
         switch (sdwa.sel[i].size()) {
         case 1: fprintf(output, " src%d_sel:%cbyte%u", i, sext, offset); break;
         case 2: fprintf(output, " src%d_sel:%cword%u", i, sext, offset >> 1); break;
         case 4: fprintf(output, " src%d_sel:dword", i); break;
         default: break;
         }
      }
   }
}

void
aco_print_instr(const Instruction* instr, FILE* output, unsigned flags)
{
   if (!instr->definitions.empty()) {
      for (unsigned i = 0; i < instr->definitions.size(); ++i) {
         print_definition(&instr->definitions[i], output, flags);
         if (i + 1 != instr->definitions.size())
            fprintf(output, ", ");
      }
      fprintf(output, " = ");
   }
   fprintf(output, "%s", instr_info.name[(int)instr->opcode]);
   if (instr->operands.size()) {
      bool* const abs = (bool*)alloca(instr->operands.size() * sizeof(bool));
      bool* const neg = (bool*)alloca(instr->operands.size() * sizeof(bool));
      bool* const opsel = (bool*)alloca(instr->operands.size() * sizeof(bool));
      for (unsigned i = 0; i < instr->operands.size(); ++i) {
         abs[i] = false;
         neg[i] = false;
         opsel[i] = false;
      }
      if (instr->isVOP3()) {
         const VOP3_instruction& vop3 = instr->vop3();
         for (unsigned i = 0; i < 3; ++i) {
            abs[i] = vop3.abs[i];
            neg[i] = vop3.neg[i];
            opsel[i] = vop3.opsel & (1 << i);
         }
      } else if (instr->isDPP()) {
         const DPP_instruction& dpp = instr->dpp();
         for (unsigned i = 0; i < 2; ++i) {
            abs[i] = dpp.abs[i];
            neg[i] = dpp.neg[i];
            opsel[i] = false;
         }
      } else if (instr->isSDWA()) {
         const SDWA_instruction& sdwa = instr->sdwa();
         for (unsigned i = 0; i < 2; ++i) {
            abs[i] = sdwa.abs[i];
            neg[i] = sdwa.neg[i];
            opsel[i] = false;
         }
      }
      for (unsigned i = 0; i < instr->operands.size(); ++i) {
         if (i)
            fprintf(output, ", ");
         else
            fprintf(output, " ");

         if (neg[i])
            fprintf(output, "-");
         if (abs[i])
            fprintf(output, "|");
         if (opsel[i])
            fprintf(output, "hi(");
         aco_print_operand(&instr->operands[i], output, flags);
         if (opsel[i])
            fprintf(output, ")");
         if (abs[i])
            fprintf(output, "|");

         if (instr->isVOP3P()) {
            const VOP3P_instruction& vop3 = instr->vop3p();
            if ((vop3.opsel_lo & (1 << i)) || !(vop3.opsel_hi & (1 << i))) {
               fprintf(output, ".%c%c", vop3.opsel_lo & (1 << i) ? 'y' : 'x',
                       vop3.opsel_hi & (1 << i) ? 'y' : 'x');
            }
            if (vop3.neg_lo[i] && vop3.neg_hi[i])
               fprintf(output, "*[-1,-1]");
            else if (vop3.neg_lo[i])
               fprintf(output, "*[-1,1]");
            else if (vop3.neg_hi[i])
               fprintf(output, "*[1,-1]");
         }
      }
   }
   print_instr_format_specific(instr, output);
}

static void
print_block_kind(uint16_t kind, FILE* output)
{
   if (kind & block_kind_uniform)
      fprintf(output, "uniform, ");
   if (kind & block_kind_top_level)
      fprintf(output, "top-level, ");
   if (kind & block_kind_loop_preheader)
      fprintf(output, "loop-preheader, ");
   if (kind & block_kind_loop_header)
      fprintf(output, "loop-header, ");
   if (kind & block_kind_loop_exit)
      fprintf(output, "loop-exit, ");
   if (kind & block_kind_continue)
      fprintf(output, "continue, ");
   if (kind & block_kind_break)
      fprintf(output, "break, ");
   if (kind & block_kind_continue_or_break)
      fprintf(output, "continue_or_break, ");
   if (kind & block_kind_discard)
      fprintf(output, "discard, ");
   if (kind & block_kind_branch)
      fprintf(output, "branch, ");
   if (kind & block_kind_merge)
      fprintf(output, "merge, ");
   if (kind & block_kind_invert)
      fprintf(output, "invert, ");
   if (kind & block_kind_uses_discard_if)
      fprintf(output, "discard_if, ");
   if (kind & block_kind_needs_lowering)
      fprintf(output, "needs_lowering, ");
   if (kind & block_kind_uses_demote)
      fprintf(output, "uses_demote, ");
   if (kind & block_kind_export_end)
      fprintf(output, "export_end, ");
}

static void
print_stage(Stage stage, FILE* output)
{
   fprintf(output, "ACO shader stage: ");

   if (stage == compute_cs)
      fprintf(output, "compute_cs");
   else if (stage == fragment_fs)
      fprintf(output, "fragment_fs");
   else if (stage == gs_copy_vs)
      fprintf(output, "gs_copy_vs");
   else if (stage == vertex_ls)
      fprintf(output, "vertex_ls");
   else if (stage == vertex_es)
      fprintf(output, "vertex_es");
   else if (stage == vertex_vs)
      fprintf(output, "vertex_vs");
   else if (stage == tess_control_hs)
      fprintf(output, "tess_control_hs");
   else if (stage == vertex_tess_control_hs)
      fprintf(output, "vertex_tess_control_hs");
   else if (stage == tess_eval_es)
      fprintf(output, "tess_eval_es");
   else if (stage == tess_eval_vs)
      fprintf(output, "tess_eval_vs");
   else if (stage == geometry_gs)
      fprintf(output, "geometry_gs");
   else if (stage == vertex_geometry_gs)
      fprintf(output, "vertex_geometry_gs");
   else if (stage == tess_eval_geometry_gs)
      fprintf(output, "tess_eval_geometry_gs");
   else if (stage == vertex_ngg)
      fprintf(output, "vertex_ngg");
   else if (stage == tess_eval_ngg)
      fprintf(output, "tess_eval_ngg");
   else if (stage == vertex_geometry_ngg)
      fprintf(output, "vertex_geometry_ngg");
   else if (stage == tess_eval_geometry_ngg)
      fprintf(output, "tess_eval_geometry_ngg");
   else
      fprintf(output, "unknown");

   fprintf(output, "\n");
}

void
aco_print_block(const Block* block, FILE* output, unsigned flags, const live& live_vars)
{
   fprintf(output, "BB%d\n", block->index);
   fprintf(output, "/* logical preds: ");
   for (unsigned pred : block->logical_preds)
      fprintf(output, "BB%d, ", pred);
   fprintf(output, "/ linear preds: ");
   for (unsigned pred : block->linear_preds)
      fprintf(output, "BB%d, ", pred);
   fprintf(output, "/ kind: ");
   print_block_kind(block->kind, output);
   fprintf(output, "*/\n");

   if (flags & print_live_vars) {
      fprintf(output, "\tlive out:");
      for (unsigned id : live_vars.live_out[block->index])
         fprintf(output, " %%%d", id);
      fprintf(output, "\n");

      RegisterDemand demand = block->register_demand;
      fprintf(output, "\tdemand: %u vgpr, %u sgpr\n", demand.vgpr, demand.sgpr);
   }

   unsigned index = 0;
   for (auto const& instr : block->instructions) {
      fprintf(output, "\t");
      if (flags & print_live_vars) {
         RegisterDemand demand = live_vars.register_demand[block->index][index];
         fprintf(output, "(%3u vgpr, %3u sgpr)   ", demand.vgpr, demand.sgpr);
      }
      if (flags & print_perf_info)
         fprintf(output, "(%3u clk)   ", instr->pass_flags);

      aco_print_instr(instr.get(), output, flags);
      fprintf(output, "\n");
      index++;
   }
}

void
aco_print_program(const Program* program, FILE* output, const live& live_vars, unsigned flags)
{
   switch (program->progress) {
   case CompilationProgress::after_isel: fprintf(output, "After Instruction Selection:\n"); break;
   case CompilationProgress::after_spilling:
      fprintf(output, "After Spilling:\n");
      flags |= print_kill;
      break;
   case CompilationProgress::after_ra: fprintf(output, "After RA:\n"); break;
   }

   print_stage(program->stage, output);

   for (Block const& block : program->blocks)
      aco_print_block(&block, output, flags, live_vars);

   if (program->constant_data.size()) {
      fprintf(output, "\n/* constant data */\n");
      for (unsigned i = 0; i < program->constant_data.size(); i += 32) {
         fprintf(output, "[%06d] ", i);
         unsigned line_size = std::min<size_t>(program->constant_data.size() - i, 32);
         for (unsigned j = 0; j < line_size; j += 4) {
            unsigned size = std::min<size_t>(program->constant_data.size() - (i + j), 4);
            uint32_t v = 0;
            memcpy(&v, &program->constant_data[i + j], size);
            fprintf(output, " %08x", v);
         }
         fprintf(output, "\n");
      }
   }

   fprintf(output, "\n");
}

void
aco_print_program(const Program* program, FILE* output, unsigned flags)
{
   aco_print_program(program, output, live(), flags);
}

} // namespace aco
