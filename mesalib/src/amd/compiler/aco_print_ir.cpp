#include "aco_ir.h"
#include "aco_builder.h"

#include "sid.h"
#include "ac_shader_util.h"

namespace aco {

static const char *reduce_ops[] = {
   [iadd32] = "iadd32",
   [iadd64] = "iadd64",
   [imul32] = "imul32",
   [imul64] = "imul64",
   [fadd32] = "fadd32",
   [fadd64] = "fadd64",
   [fmul32] = "fmul32",
   [fmul64] = "fmul64",
   [imin32] = "imin32",
   [imin64] = "imin64",
   [imax32] = "imax32",
   [imax64] = "imax64",
   [umin32] = "umin32",
   [umin64] = "umin64",
   [umax32] = "umax32",
   [umax64] = "umax64",
   [fmin32] = "fmin32",
   [fmin64] = "fmin64",
   [fmax32] = "fmax32",
   [fmax64] = "fmax64",
   [iand32] = "iand32",
   [iand64] = "iand64",
   [ior32] = "ior32",
   [ior64] = "ior64",
   [ixor32] = "ixor32",
   [ixor64] = "ixor64",
   [gfx10_wave64_bpermute] = "gfx10_wave64_bpermute",
};

static void print_reg_class(const RegClass rc, FILE *output)
{
   switch (rc) {
      case RegClass::s1: fprintf(output, " s1: "); return;
      case RegClass::s2: fprintf(output, " s2: "); return;
      case RegClass::s3: fprintf(output, " s3: "); return;
      case RegClass::s4: fprintf(output, " s4: "); return;
      case RegClass::s6: fprintf(output, " s6: "); return;
      case RegClass::s8: fprintf(output, " s8: "); return;
      case RegClass::s16: fprintf(output, "s16: "); return;
      case RegClass::v1: fprintf(output, " v1: "); return;
      case RegClass::v2: fprintf(output, " v2: "); return;
      case RegClass::v3: fprintf(output, " v3: "); return;
      case RegClass::v4: fprintf(output, " v4: "); return;
      case RegClass::v5: fprintf(output, " v5: "); return;
      case RegClass::v6: fprintf(output, " v6: "); return;
      case RegClass::v7: fprintf(output, " v7: "); return;
      case RegClass::v8: fprintf(output, " v8: "); return;
      case RegClass::v1b: fprintf(output, " v1b: "); return;
      case RegClass::v2b: fprintf(output, " v2b: "); return;
      case RegClass::v3b: fprintf(output, " v3b: "); return;
      case RegClass::v4b: fprintf(output, " v4b: "); return;
      case RegClass::v6b: fprintf(output, " v6b: "); return;
      case RegClass::v8b: fprintf(output, " v8b: "); return;
      case RegClass::v1_linear: fprintf(output, " v1: "); return;
      case RegClass::v2_linear: fprintf(output, " v2: "); return;
   }
}

void print_physReg(PhysReg reg, unsigned bytes, FILE *output)
{
   if (reg == 124) {
      fprintf(output, ":m0");
   } else if (reg == 106) {
      fprintf(output, ":vcc");
   } else if (reg == 253) {
      fprintf(output, ":scc");
   } else if (reg == 126) {
      fprintf(output, ":exec");
   } else {
      bool is_vgpr = reg / 256;
      unsigned r = reg % 256;
      unsigned size = DIV_ROUND_UP(bytes, 4);
      fprintf(output, ":%c[%d", is_vgpr ? 'v' : 's', r);
      if (size > 1)
         fprintf(output, "-%d]", r + size -1);
      else
         fprintf(output, "]");
      if (reg.byte() || bytes % 4)
         fprintf(output, "[%d:%d]", reg.byte()*8, (reg.byte()+bytes) * 8);
   }
}

static void print_constant(uint8_t reg, FILE *output)
{
   if (reg >= 128 && reg <= 192) {
      fprintf(output, "%d", reg - 128);
      return;
   } else if (reg >= 192 && reg <= 208) {
      fprintf(output, "%d", 192 - reg);
      return;
   }

   switch (reg) {
   case 240:
      fprintf(output, "0.5");
      break;
   case 241:
      fprintf(output, "-0.5");
      break;
   case 242:
      fprintf(output, "1.0");
      break;
   case 243:
      fprintf(output, "-1.0");
      break;
   case 244:
      fprintf(output, "2.0");
      break;
   case 245:
      fprintf(output, "-2.0");
      break;
   case 246:
      fprintf(output, "4.0");
      break;
   case 247:
      fprintf(output, "-4.0");
      break;
   case 248:
      fprintf(output, "1/(2*PI)");
      break;
   }
}

static void print_operand(const Operand *operand, FILE *output)
{
   if (operand->isLiteral()) {
      fprintf(output, "0x%x", operand->constantValue());
   } else if (operand->isConstant()) {
      print_constant(operand->physReg().reg(), output);
   } else if (operand->isUndefined()) {
      print_reg_class(operand->regClass(), output);
      fprintf(output, "undef");
   } else {
      if (operand->isLateKill())
         fprintf(output, "(latekill)");

      fprintf(output, "%%%d", operand->tempId());

      if (operand->isFixed())
         print_physReg(operand->physReg(), operand->bytes(), output);
   }
}

static void print_definition(const Definition *definition, FILE *output)
{
   print_reg_class(definition->regClass(), output);
   fprintf(output, "%%%d", definition->tempId());

   if (definition->isFixed())
      print_physReg(definition->physReg(), definition->bytes(), output);
}

static void print_barrier_reorder(bool can_reorder, barrier_interaction barrier, FILE *output)
{
   if (can_reorder)
      fprintf(output, " reorder");

   if (barrier & barrier_buffer)
      fprintf(output, " buffer");
   if (barrier & barrier_image)
      fprintf(output, " image");
   if (barrier & barrier_atomic)
      fprintf(output, " atomic");
   if (barrier & barrier_shared)
      fprintf(output, " shared");
   if (barrier & barrier_gs_data)
      fprintf(output, " gs_data");
   if (barrier & barrier_gs_sendmsg)
      fprintf(output, " gs_sendmsg");
}

static void print_instr_format_specific(struct Instruction *instr, FILE *output)
{
   switch (instr->format) {
   case Format::SOPK: {
      SOPK_instruction* sopk = static_cast<SOPK_instruction*>(instr);
      fprintf(output, " imm:%d", sopk->imm & 0x8000 ? (sopk->imm - 65536) : sopk->imm);
      break;
   }
   case Format::SOPP: {
      SOPP_instruction* sopp = static_cast<SOPP_instruction*>(instr);
      uint16_t imm = sopp->imm;
      switch (instr->opcode) {
      case aco_opcode::s_waitcnt: {
         /* we usually should check the chip class for vmcnt/lgkm, but
          * insert_waitcnt() should fill it in regardless. */
         unsigned vmcnt = (imm & 0xF) | ((imm & (0x3 << 14)) >> 10);
         if (vmcnt != 63) fprintf(output, " vmcnt(%d)", vmcnt);
         if (((imm >> 4) & 0x7) < 0x7) fprintf(output, " expcnt(%d)", (imm >> 4) & 0x7);
         if (((imm >> 8) & 0x3F) < 0x3F) fprintf(output, " lgkmcnt(%d)", (imm >> 8) & 0x3F);
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
         case sendmsg_none:
            fprintf(output, " sendmsg(MSG_NONE)");
            break;
         case _sendmsg_gs:
            fprintf(output, " sendmsg(gs%s%s, %u)",
                    imm & 0x10 ? ", cut" : "", imm & 0x20 ? ", emit" : "", imm >> 8);
            break;
         case _sendmsg_gs_done:
            fprintf(output, " sendmsg(gs_done%s%s, %u)",
                    imm & 0x10 ? ", cut" : "", imm & 0x20 ? ", emit" : "", imm >> 8);
            break;
         case sendmsg_save_wave:
            fprintf(output, " sendmsg(save_wave)");
            break;
         case sendmsg_stall_wave_gen:
            fprintf(output, " sendmsg(stall_wave_gen)");
            break;
         case sendmsg_halt_waves:
            fprintf(output, " sendmsg(halt_waves)");
            break;
         case sendmsg_ordered_ps_done:
            fprintf(output, " sendmsg(ordered_ps_done)");
            break;
         case sendmsg_early_prim_dealloc:
            fprintf(output, " sendmsg(early_prim_dealloc)");
            break;
         case sendmsg_gs_alloc_req:
            fprintf(output, " sendmsg(gs_alloc_req)");
            break;
         }
         break;
      }
      default: {
         if (imm)
            fprintf(output, " imm:%u", imm);
         break;
      }
      }
      if (sopp->block != -1)
         fprintf(output, " block:BB%d", sopp->block);
      break;
   }
   case Format::SMEM: {
      SMEM_instruction* smem = static_cast<SMEM_instruction*>(instr);
      if (smem->glc)
         fprintf(output, " glc");
      if (smem->dlc)
         fprintf(output, " dlc");
      if (smem->nv)
         fprintf(output, " nv");
      print_barrier_reorder(smem->can_reorder, smem->barrier, output);
      break;
   }
   case Format::VINTRP: {
      Interp_instruction* vintrp = static_cast<Interp_instruction*>(instr);
      fprintf(output, " attr%d.%c", vintrp->attribute, "xyzw"[vintrp->component]);
      break;
   }
   case Format::DS: {
      DS_instruction* ds = static_cast<DS_instruction*>(instr);
      if (ds->offset0)
         fprintf(output, " offset0:%u", ds->offset0);
      if (ds->offset1)
         fprintf(output, " offset1:%u", ds->offset1);
      if (ds->gds)
         fprintf(output, " gds");
      break;
   }
   case Format::MUBUF: {
      MUBUF_instruction* mubuf = static_cast<MUBUF_instruction*>(instr);
      if (mubuf->offset)
         fprintf(output, " offset:%u", mubuf->offset);
      if (mubuf->offen)
         fprintf(output, " offen");
      if (mubuf->idxen)
         fprintf(output, " idxen");
      if (mubuf->addr64)
         fprintf(output, " addr64");
      if (mubuf->glc)
         fprintf(output, " glc");
      if (mubuf->dlc)
         fprintf(output, " dlc");
      if (mubuf->slc)
         fprintf(output, " slc");
      if (mubuf->tfe)
         fprintf(output, " tfe");
      if (mubuf->lds)
         fprintf(output, " lds");
      if (mubuf->disable_wqm)
         fprintf(output, " disable_wqm");
      print_barrier_reorder(mubuf->can_reorder, mubuf->barrier, output);
      break;
   }
   case Format::MIMG: {
      MIMG_instruction* mimg = static_cast<MIMG_instruction*>(instr);
      unsigned identity_dmask = !instr->definitions.empty() ?
                                (1 << instr->definitions[0].size()) - 1 :
                                0xf;
      if ((mimg->dmask & identity_dmask) != identity_dmask)
         fprintf(output, " dmask:%s%s%s%s",
                 mimg->dmask & 0x1 ? "x" : "",
                 mimg->dmask & 0x2 ? "y" : "",
                 mimg->dmask & 0x4 ? "z" : "",
                 mimg->dmask & 0x8 ? "w" : "");
      switch (mimg->dim) {
      case ac_image_1d:
         fprintf(output, " 1d");
         break;
      case ac_image_2d:
         fprintf(output, " 2d");
         break;
      case ac_image_3d:
         fprintf(output, " 3d");
         break;
      case ac_image_cube:
         fprintf(output, " cube");
         break;
      case ac_image_1darray:
         fprintf(output, " 1darray");
         break;
      case ac_image_2darray:
         fprintf(output, " 2darray");
         break;
      case ac_image_2dmsaa:
         fprintf(output, " 2dmsaa");
         break;
      case ac_image_2darraymsaa:
         fprintf(output, " 2darraymsaa");
         break;
      }
      if (mimg->unrm)
         fprintf(output, " unrm");
      if (mimg->glc)
         fprintf(output, " glc");
      if (mimg->dlc)
         fprintf(output, " dlc");
      if (mimg->slc)
         fprintf(output, " slc");
      if (mimg->tfe)
         fprintf(output, " tfe");
      if (mimg->da)
         fprintf(output, " da");
      if (mimg->lwe)
         fprintf(output, " lwe");
      if (mimg->r128 || mimg->a16)
         fprintf(output, " r128/a16");
      if (mimg->d16)
         fprintf(output, " d16");
      if (mimg->disable_wqm)
         fprintf(output, " disable_wqm");
      print_barrier_reorder(mimg->can_reorder, mimg->barrier, output);
      break;
   }
   case Format::EXP: {
      Export_instruction* exp = static_cast<Export_instruction*>(instr);
      unsigned identity_mask = exp->compressed ? 0x5 : 0xf;
      if ((exp->enabled_mask & identity_mask) != identity_mask)
         fprintf(output, " en:%c%c%c%c",
                 exp->enabled_mask & 0x1 ? 'r' : '*',
                 exp->enabled_mask & 0x2 ? 'g' : '*',
                 exp->enabled_mask & 0x4 ? 'b' : '*',
                 exp->enabled_mask & 0x8 ? 'a' : '*');
      if (exp->compressed)
         fprintf(output, " compr");
      if (exp->done)
         fprintf(output, " done");
      if (exp->valid_mask)
         fprintf(output, " vm");

      if (exp->dest <= V_008DFC_SQ_EXP_MRT + 7)
         fprintf(output, " mrt%d", exp->dest - V_008DFC_SQ_EXP_MRT);
      else if (exp->dest == V_008DFC_SQ_EXP_MRTZ)
         fprintf(output, " mrtz");
      else if (exp->dest == V_008DFC_SQ_EXP_NULL)
         fprintf(output, " null");
      else if (exp->dest >= V_008DFC_SQ_EXP_POS && exp->dest <= V_008DFC_SQ_EXP_POS + 3)
         fprintf(output, " pos%d", exp->dest - V_008DFC_SQ_EXP_POS);
      else if (exp->dest >= V_008DFC_SQ_EXP_PARAM && exp->dest <= V_008DFC_SQ_EXP_PARAM + 31)
         fprintf(output, " param%d", exp->dest - V_008DFC_SQ_EXP_PARAM);
      break;
   }
   case Format::PSEUDO_BRANCH: {
      Pseudo_branch_instruction* branch = static_cast<Pseudo_branch_instruction*>(instr);
      /* Note: BB0 cannot be a branch target */
      if (branch->target[0] != 0)
         fprintf(output, " BB%d", branch->target[0]);
      if (branch->target[1] != 0)
         fprintf(output, ", BB%d", branch->target[1]);
      break;
   }
   case Format::PSEUDO_REDUCTION: {
      Pseudo_reduction_instruction* reduce = static_cast<Pseudo_reduction_instruction*>(instr);
      fprintf(output, " op:%s", reduce_ops[reduce->reduce_op]);
      if (reduce->cluster_size)
         fprintf(output, " cluster_size:%u", reduce->cluster_size);
      break;
   }
   case Format::FLAT:
   case Format::GLOBAL:
   case Format::SCRATCH: {
      FLAT_instruction* flat = static_cast<FLAT_instruction*>(instr);
      if (flat->offset)
         fprintf(output, " offset:%u", flat->offset);
      if (flat->glc)
         fprintf(output, " glc");
      if (flat->dlc)
         fprintf(output, " dlc");
      if (flat->slc)
         fprintf(output, " slc");
      if (flat->lds)
         fprintf(output, " lds");
      if (flat->nv)
         fprintf(output, " nv");
      if (flat->disable_wqm)
         fprintf(output, " disable_wqm");
      print_barrier_reorder(flat->can_reorder, flat->barrier, output);
      break;
   }
   case Format::MTBUF: {
      MTBUF_instruction* mtbuf = static_cast<MTBUF_instruction*>(instr);
      fprintf(output, " dfmt:");
      switch (mtbuf->dfmt) {
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
      switch (mtbuf->nfmt) {
      case V_008F0C_BUF_NUM_FORMAT_UNORM: fprintf(output, "unorm"); break;
      case V_008F0C_BUF_NUM_FORMAT_SNORM: fprintf(output, "snorm"); break;
      case V_008F0C_BUF_NUM_FORMAT_USCALED: fprintf(output, "uscaled"); break;
      case V_008F0C_BUF_NUM_FORMAT_SSCALED: fprintf(output, "sscaled"); break;
      case V_008F0C_BUF_NUM_FORMAT_UINT: fprintf(output, "uint"); break;
      case V_008F0C_BUF_NUM_FORMAT_SINT: fprintf(output, "sint"); break;
      case V_008F0C_BUF_NUM_FORMAT_SNORM_OGL: fprintf(output, "snorm"); break;
      case V_008F0C_BUF_NUM_FORMAT_FLOAT: fprintf(output, "float"); break;
      }
      if (mtbuf->offset)
         fprintf(output, " offset:%u", mtbuf->offset);
      if (mtbuf->offen)
         fprintf(output, " offen");
      if (mtbuf->idxen)
         fprintf(output, " idxen");
      if (mtbuf->glc)
         fprintf(output, " glc");
      if (mtbuf->dlc)
         fprintf(output, " dlc");
      if (mtbuf->slc)
         fprintf(output, " slc");
      if (mtbuf->tfe)
         fprintf(output, " tfe");
      if (mtbuf->disable_wqm)
         fprintf(output, " disable_wqm");
      print_barrier_reorder(mtbuf->can_reorder, mtbuf->barrier, output);
      break;
   }
   default: {
      break;
   }
   }
   if (instr->isVOP3()) {
      VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*>(instr);
      switch (vop3->omod) {
      case 1:
         fprintf(output, " *2");
         break;
      case 2:
         fprintf(output, " *4");
         break;
      case 3:
         fprintf(output, " *0.5");
         break;
      }
      if (vop3->clamp)
         fprintf(output, " clamp");
      if (vop3->opsel & (1 << 3))
         fprintf(output, " opsel_hi");
   } else if (instr->isDPP()) {
      DPP_instruction* dpp = static_cast<DPP_instruction*>(instr);
      if (dpp->dpp_ctrl <= 0xff) {
         fprintf(output, " quad_perm:[%d,%d,%d,%d]",
                 dpp->dpp_ctrl & 0x3, (dpp->dpp_ctrl >> 2) & 0x3,
                 (dpp->dpp_ctrl >> 4) & 0x3, (dpp->dpp_ctrl >> 6) & 0x3);
      } else if (dpp->dpp_ctrl >= 0x101 && dpp->dpp_ctrl <= 0x10f) {
         fprintf(output, " row_shl:%d", dpp->dpp_ctrl & 0xf);
      } else if (dpp->dpp_ctrl >= 0x111 && dpp->dpp_ctrl <= 0x11f) {
         fprintf(output, " row_shr:%d", dpp->dpp_ctrl & 0xf);
      } else if (dpp->dpp_ctrl >= 0x121 && dpp->dpp_ctrl <= 0x12f) {
         fprintf(output, " row_ror:%d", dpp->dpp_ctrl & 0xf);
      } else if (dpp->dpp_ctrl == dpp_wf_sl1) {
         fprintf(output, " wave_shl:1");
      } else if (dpp->dpp_ctrl == dpp_wf_rl1) {
         fprintf(output, " wave_rol:1");
      } else if (dpp->dpp_ctrl == dpp_wf_sr1) {
         fprintf(output, " wave_shr:1");
      } else if (dpp->dpp_ctrl == dpp_wf_rr1) {
         fprintf(output, " wave_ror:1");
      } else if (dpp->dpp_ctrl == dpp_row_mirror) {
         fprintf(output, " row_mirror");
      } else if (dpp->dpp_ctrl == dpp_row_half_mirror) {
         fprintf(output, " row_half_mirror");
      } else if (dpp->dpp_ctrl == dpp_row_bcast15) {
         fprintf(output, " row_bcast:15");
      } else if (dpp->dpp_ctrl == dpp_row_bcast31) {
         fprintf(output, " row_bcast:31");
      } else {
         fprintf(output, " dpp_ctrl:0x%.3x", dpp->dpp_ctrl);
      }
      if (dpp->row_mask != 0xf)
         fprintf(output, " row_mask:0x%.1x", dpp->row_mask);
      if (dpp->bank_mask != 0xf)
         fprintf(output, " bank_mask:0x%.1x", dpp->bank_mask);
      if (dpp->bound_ctrl)
         fprintf(output, " bound_ctrl:1");
   } else if ((int)instr->format & (int)Format::SDWA) {
      SDWA_instruction* sdwa = static_cast<SDWA_instruction*>(instr);
      switch (sdwa->omod) {
      case 1:
         fprintf(output, " *2");
         break;
      case 2:
         fprintf(output, " *4");
         break;
      case 3:
         fprintf(output, " *0.5");
         break;
      }
      if (sdwa->clamp)
         fprintf(output, " clamp");
      switch (sdwa->dst_sel & sdwa_asuint) {
      case sdwa_udword:
         break;
      case sdwa_ubyte0:
      case sdwa_ubyte1:
      case sdwa_ubyte2:
      case sdwa_ubyte3:
         fprintf(output, " dst_sel:%sbyte%u", sdwa->dst_sel & sdwa_sext ? "s" : "u",
                 sdwa->dst_sel & sdwa_bytenum);
         break;
      case sdwa_uword0:
      case sdwa_uword1:
         fprintf(output, " dst_sel:%sword%u", sdwa->dst_sel & sdwa_sext ? "s" : "u",
                 sdwa->dst_sel & sdwa_wordnum);
         break;
      }
      if (sdwa->dst_preserve)
         fprintf(output, " dst_preserve");
   }
}

void aco_print_instr(struct Instruction *instr, FILE *output)
{
   if (!instr->definitions.empty()) {
      for (unsigned i = 0; i < instr->definitions.size(); ++i) {
         print_definition(&instr->definitions[i], output);
         if (i + 1 != instr->definitions.size())
            fprintf(output, ", ");
      }
      fprintf(output, " = ");
   }
   fprintf(output, "%s", instr_info.name[(int)instr->opcode]);
   if (instr->operands.size()) {
      bool abs[instr->operands.size()];
      bool neg[instr->operands.size()];
      bool opsel[instr->operands.size()];
      uint8_t sel[instr->operands.size()];
      if ((int)instr->format & (int)Format::VOP3A) {
         VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*>(instr);
         for (unsigned i = 0; i < instr->operands.size(); ++i) {
            abs[i] = vop3->abs[i];
            neg[i] = vop3->neg[i];
            opsel[i] = vop3->opsel & (1 << i);
            sel[i] = sdwa_udword;
         }
      } else if (instr->isDPP()) {
         DPP_instruction* dpp = static_cast<DPP_instruction*>(instr);
         for (unsigned i = 0; i < instr->operands.size(); ++i) {
            abs[i] = i < 2 ? dpp->abs[i] : false;
            neg[i] = i < 2 ? dpp->neg[i] : false;
            opsel[i] = false;
            sel[i] = sdwa_udword;
         }
      } else if (instr->isSDWA()) {
         SDWA_instruction* sdwa = static_cast<SDWA_instruction*>(instr);
         for (unsigned i = 0; i < instr->operands.size(); ++i) {
            abs[i] = i < 2 ? sdwa->abs[i] : false;
            neg[i] = i < 2 ? sdwa->neg[i] : false;
            opsel[i] = false;
            sel[i] = i < 2 ? sdwa->sel[i] : sdwa_udword;
         }
      } else {
         for (unsigned i = 0; i < instr->operands.size(); ++i) {
            abs[i] = false;
            neg[i] = false;
            opsel[i] = false;
            sel[i] = sdwa_udword;
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
         else if (sel[i] & sdwa_sext)
            fprintf(output, "sext(");
         print_operand(&instr->operands[i], output);
         if (opsel[i] || (sel[i] & sdwa_sext))
            fprintf(output, ")");
         if (!(sel[i] & sdwa_isra)) {
            if (sel[i] & sdwa_udword) {
               /* print nothing */
            } else if (sel[i] & sdwa_isword) {
               unsigned index = sel[i] & sdwa_wordnum;
               fprintf(output, "[%u:%u]", index * 16, index * 16 + 15);
            } else {
               unsigned index = sel[i] & sdwa_bytenum;
               fprintf(output, "[%u:%u]", index * 8, index * 8 + 7);
            }
         }
         if (abs[i])
            fprintf(output, "|");
       }
   }
   print_instr_format_specific(instr, output);
}

static void print_block_kind(uint16_t kind, FILE *output)
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
}

void aco_print_block(const struct Block* block, FILE *output)
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
   for (auto const& instr : block->instructions) {
      fprintf(output, "\t");
      aco_print_instr(instr.get(), output);
      fprintf(output, "\n");
   }
}

void aco_print_program(Program *program, FILE *output)
{
   for (Block const& block : program->blocks)
      aco_print_block(&block, output);

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

}
