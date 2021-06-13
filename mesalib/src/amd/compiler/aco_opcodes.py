#
# Copyright (c) 2018 Valve Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Authors:
#    Daniel Schuermann (daniel.schuermann@campus.tu-berlin.de)


# Class that represents all the information we have about the opcode
# NOTE: this must be kept in sync with aco_op_info

import sys
from enum import Enum

class InstrClass(Enum):
   Valu32 = 0
   ValuConvert32 = 1
   Valu64 = 2
   ValuQuarterRate32 = 3
   ValuFma = 4
   ValuTranscendental32 = 5
   ValuDouble = 6
   ValuDoubleAdd = 7
   ValuDoubleConvert = 8
   ValuDoubleTranscendental = 9
   Salu = 10
   SMem = 11
   Barrier = 12
   Branch = 13
   Sendmsg = 14
   DS = 15
   Export = 16
   VMem = 17
   Waitcnt = 18
   Other = 19

class Format(Enum):
   PSEUDO = 0
   SOP1 = 1
   SOP2 = 2
   SOPK = 3
   SOPP = 4
   SOPC = 5
   SMEM = 6
   DS = 8
   MTBUF = 9
   MUBUF = 10
   MIMG = 11
   EXP = 12
   FLAT = 13
   GLOBAL = 14
   SCRATCH = 15
   PSEUDO_BRANCH = 16
   PSEUDO_BARRIER = 17
   PSEUDO_REDUCTION = 18
   VOP3P = 19
   VOP1 = 1 << 8
   VOP2 = 1 << 9
   VOPC = 1 << 10
   VOP3 = 1 << 11
   VINTRP = 1 << 12
   DPP = 1 << 13
   SDWA = 1 << 14

   def get_builder_fields(self):
      if self == Format.SOPK:
         return [('uint16_t', 'imm', None)]
      elif self == Format.SOPP:
         return [('uint32_t', 'block', '-1'),
                 ('uint32_t', 'imm', '0')]
      elif self == Format.SMEM:
         return [('memory_sync_info', 'sync', 'memory_sync_info()'),
                 ('bool', 'glc', 'false'),
                 ('bool', 'dlc', 'false'),
                 ('bool', 'nv', 'false')]
      elif self == Format.DS:
         return [('int16_t', 'offset0', '0'),
                 ('int8_t', 'offset1', '0'),
                 ('bool', 'gds', 'false')]
      elif self == Format.MTBUF:
         return [('unsigned', 'dfmt', None),
                 ('unsigned', 'nfmt', None),
                 ('unsigned', 'offset', None),
                 ('bool', 'offen', None),
                 ('bool', 'idxen', 'false'),
                 ('bool', 'disable_wqm', 'false'),
                 ('bool', 'glc', 'false'),
                 ('bool', 'dlc', 'false'),
                 ('bool', 'slc', 'false'),
                 ('bool', 'tfe', 'false')]
      elif self == Format.MUBUF:
         return [('unsigned', 'offset', None),
                 ('bool', 'offen', None),
                 ('bool', 'swizzled', 'false'),
                 ('bool', 'idxen', 'false'),
                 ('bool', 'addr64', 'false'),
                 ('bool', 'disable_wqm', 'false'),
                 ('bool', 'glc', 'false'),
                 ('bool', 'dlc', 'false'),
                 ('bool', 'slc', 'false'),
                 ('bool', 'tfe', 'false'),
                 ('bool', 'lds', 'false')]
      elif self == Format.MIMG:
         return [('unsigned', 'dmask', '0xF'),
                 ('bool', 'da', 'false'),
                 ('bool', 'unrm', 'true'),
                 ('bool', 'disable_wqm', 'false'),
                 ('bool', 'glc', 'false'),
                 ('bool', 'dlc', 'false'),
                 ('bool', 'slc', 'false'),
                 ('bool', 'tfe', 'false'),
                 ('bool', 'lwe', 'false'),
                 ('bool', 'r128_a16', 'false', 'r128'),
                 ('bool', 'd16', 'false')]
         return [('unsigned', 'attribute', None),
                 ('unsigned', 'component', None)]
      elif self == Format.EXP:
         return [('unsigned', 'enabled_mask', None),
                 ('unsigned', 'dest', None),
                 ('bool', 'compr', 'false', 'compressed'),
                 ('bool', 'done', 'false'),
                 ('bool', 'vm', 'false', 'valid_mask')]
      elif self == Format.PSEUDO_BRANCH:
         return [('uint32_t', 'target0', '0', 'target[0]'),
                 ('uint32_t', 'target1', '0', 'target[1]')]
      elif self == Format.PSEUDO_REDUCTION:
         return [('ReduceOp', 'op', None, 'reduce_op'),
                 ('unsigned', 'cluster_size', '0')]
      elif self == Format.PSEUDO_BARRIER:
         return [('memory_sync_info', 'sync', None),
                 ('sync_scope', 'exec_scope', 'scope_invocation')]
      elif self == Format.VINTRP:
         return [('unsigned', 'attribute', None),
                 ('unsigned', 'component', None)]
      elif self == Format.DPP:
         return [('uint16_t', 'dpp_ctrl', None),
                 ('uint8_t', 'row_mask', '0xF'),
                 ('uint8_t', 'bank_mask', '0xF'),
                 ('bool', 'bound_ctrl', 'true')]
      elif self == Format.VOP3P:
         return [('uint8_t', 'opsel_lo', None),
                 ('uint8_t', 'opsel_hi', None)]
      elif self in [Format.FLAT, Format.GLOBAL, Format.SCRATCH]:
         return [('uint16_t', 'offset', 0),
                 ('memory_sync_info', 'sync', 'memory_sync_info()'),
                 ('bool', 'glc', 'false'),
                 ('bool', 'slc', 'false'),
                 ('bool', 'lds', 'false'),
                 ('bool', 'nv', 'false')]
      else:
         return []

   def get_builder_field_names(self):
      return [f[1] for f in self.get_builder_fields()]

   def get_builder_field_dests(self):
      return [(f[3] if len(f) >= 4 else f[1]) for f in self.get_builder_fields()]

   def get_builder_field_decls(self):
      return [('%s %s=%s' % (f[0], f[1], f[2]) if f[2] != None else '%s %s' % (f[0], f[1])) for f in self.get_builder_fields()]

   def get_builder_initialization(self, num_operands):
      res = ''
      if self == Format.SDWA:
         for i in range(min(num_operands, 2)):
            res += 'instr->sel[{0}] = op{0}.op.bytes() == 2 ? sdwa_uword : (op{0}.op.bytes() == 1 ? sdwa_ubyte : sdwa_udword);\n'.format(i)
         res += 'instr->dst_sel = def0.bytes() == 2 ? sdwa_uword : (def0.bytes() == 1 ? sdwa_ubyte : sdwa_udword);\n'
         res += 'instr->dst_preserve = true;'
      return res


class Opcode(object):
   """Class that represents all the information we have about the opcode
   NOTE: this must be kept in sync with aco_op_info
   """
   def __init__(self, name, opcode_gfx7, opcode_gfx9, opcode_gfx10, format, input_mod, output_mod, is_atomic, cls):
      """Parameters:

      - name is the name of the opcode (prepend nir_op_ for the enum name)
      - all types are strings that get nir_type_ prepended to them
      - input_types is a list of types
      - algebraic_properties is a space-seperated string, where nir_op_is_ is
        prepended before each entry
      - const_expr is an expression or series of statements that computes the
        constant value of the opcode given the constant values of its inputs.
      """
      assert isinstance(name, str)
      assert isinstance(opcode_gfx7, int)
      assert isinstance(opcode_gfx9, int)
      assert isinstance(opcode_gfx10, int)
      assert isinstance(format, Format)
      assert isinstance(input_mod, bool)
      assert isinstance(output_mod, bool)

      self.name = name
      self.opcode_gfx7 = opcode_gfx7
      self.opcode_gfx9 = opcode_gfx9
      self.opcode_gfx10 = opcode_gfx10
      self.input_mod = "1" if input_mod else "0"
      self.output_mod = "1" if output_mod else "0"
      self.is_atomic = "1" if is_atomic else "0"
      self.format = format
      self.cls = cls

      parts = name.replace('_e64', '').rsplit('_', 2)
      op_dtype = parts[-1]
      def_dtype = parts[-2] if len(parts) > 1 else parts[-1]

      def_dtype_sizes = {'{}{}'.format(prefix, size) : size for prefix in 'biuf' for size in [64, 32, 24, 16]}
      op_dtype_sizes = {k:v for k, v in def_dtype_sizes.items()}
      # inline constants are 32-bit for 16-bit integer/typeless instructions: https://reviews.llvm.org/D81841
      op_dtype_sizes['b16'] = 32
      op_dtype_sizes['i16'] = 32
      op_dtype_sizes['u16'] = 32

      # If we can't tell the definition size and the operand size, default to
      # 32. Some opcodes can have a larger definition size, but
      # get_subdword_definition_info() handles that.
      self.operand_size = op_dtype_sizes.get(op_dtype, 32)
      self.definition_size = def_dtype_sizes.get(def_dtype, self.operand_size)

      # exceptions for operands:
      if 'sad_' in name:
        self.operand_size = 0
      elif name in ['v_mad_u64_u32', 'v_mad_i64_i32']:
        self.operand_size = 0
      elif self.operand_size == 24:
        self.operand_size = 32
      elif op_dtype == 'u8' or op_dtype == 'i8':
        self.operand_size = 32
      elif name in ['v_cvt_f32_ubyte0', 'v_cvt_f32_ubyte1',
                    'v_cvt_f32_ubyte2', 'v_cvt_f32_ubyte3']:
        self.operand_size = 32

      # exceptions for definitions:
      if 'sad_' in name:
        self.definition_size = 0
      elif '_pk' in name:
        self.definition_size = 32


# global dictionary of opcodes
opcodes = {}

def opcode(name, opcode_gfx7 = -1, opcode_gfx9 = -1, opcode_gfx10 = -1, format = Format.PSEUDO, cls = InstrClass.Other, input_mod = False, output_mod = False, is_atomic = False):
   assert name not in opcodes
   opcodes[name] = Opcode(name, opcode_gfx7, opcode_gfx9, opcode_gfx10, format, input_mod, output_mod, is_atomic, cls)

def default_class(opcodes, cls):
   for op in opcodes:
      if isinstance(op[-1], InstrClass):
         yield op
      else:
         yield op + (cls,)

opcode("exp", 0, 0, 0, format = Format.EXP, cls = InstrClass.Export)
opcode("p_parallelcopy")
opcode("p_startpgm")
opcode("p_phi")
opcode("p_linear_phi")
opcode("p_as_uniform")
opcode("p_unit_test")

opcode("p_create_vector")
opcode("p_extract_vector")
opcode("p_split_vector")

# start/end the parts where we can use exec based instructions
# implicitly
opcode("p_logical_start")
opcode("p_logical_end")

# e.g. subgroupMin() in SPIR-V
opcode("p_reduce", format=Format.PSEUDO_REDUCTION)
# e.g. subgroupInclusiveMin()
opcode("p_inclusive_scan", format=Format.PSEUDO_REDUCTION)
# e.g. subgroupExclusiveMin()
opcode("p_exclusive_scan", format=Format.PSEUDO_REDUCTION)

opcode("p_branch", format=Format.PSEUDO_BRANCH)
opcode("p_cbranch", format=Format.PSEUDO_BRANCH)
opcode("p_cbranch_z", format=Format.PSEUDO_BRANCH)
opcode("p_cbranch_nz", format=Format.PSEUDO_BRANCH)

opcode("p_barrier", format=Format.PSEUDO_BARRIER)

opcode("p_spill")
opcode("p_reload")

# start/end linear vgprs
opcode("p_start_linear_vgpr")
opcode("p_end_linear_vgpr")

opcode("p_wqm")
opcode("p_discard_if")
opcode("p_demote_to_helper")
opcode("p_is_helper")
opcode("p_exit_early_if")

# simulates proper bpermute behavior when it's unsupported, eg. GFX10 wave64
opcode("p_bpermute")

opcode("p_constaddr")

# SOP2 instructions: 2 scalar inputs, 1 scalar output (+optional scc)
SOP2 = {
  # GFX6, GFX7, GFX8, GFX9, GFX10, name
   (0x00, 0x00, 0x00, 0x00, 0x00, "s_add_u32"),
   (0x01, 0x01, 0x01, 0x01, 0x01, "s_sub_u32"),
   (0x02, 0x02, 0x02, 0x02, 0x02, "s_add_i32"),
   (0x03, 0x03, 0x03, 0x03, 0x03, "s_sub_i32"),
   (0x04, 0x04, 0x04, 0x04, 0x04, "s_addc_u32"),
   (0x05, 0x05, 0x05, 0x05, 0x05, "s_subb_u32"),
   (0x06, 0x06, 0x06, 0x06, 0x06, "s_min_i32"),
   (0x07, 0x07, 0x07, 0x07, 0x07, "s_min_u32"),
   (0x08, 0x08, 0x08, 0x08, 0x08, "s_max_i32"),
   (0x09, 0x09, 0x09, 0x09, 0x09, "s_max_u32"),
   (0x0a, 0x0a, 0x0a, 0x0a, 0x0a, "s_cselect_b32"),
   (0x0b, 0x0b, 0x0b, 0x0b, 0x0b, "s_cselect_b64"),
   (0x0e, 0x0e, 0x0c, 0x0c, 0x0e, "s_and_b32"),
   (0x0f, 0x0f, 0x0d, 0x0d, 0x0f, "s_and_b64"),
   (0x10, 0x10, 0x0e, 0x0e, 0x10, "s_or_b32"),
   (0x11, 0x11, 0x0f, 0x0f, 0x11, "s_or_b64"),
   (0x12, 0x12, 0x10, 0x10, 0x12, "s_xor_b32"),
   (0x13, 0x13, 0x11, 0x11, 0x13, "s_xor_b64"),
   (0x14, 0x14, 0x12, 0x12, 0x14, "s_andn2_b32"),
   (0x15, 0x15, 0x13, 0x13, 0x15, "s_andn2_b64"),
   (0x16, 0x16, 0x14, 0x14, 0x16, "s_orn2_b32"),
   (0x17, 0x17, 0x15, 0x15, 0x17, "s_orn2_b64"),
   (0x18, 0x18, 0x16, 0x16, 0x18, "s_nand_b32"),
   (0x19, 0x19, 0x17, 0x17, 0x19, "s_nand_b64"),
   (0x1a, 0x1a, 0x18, 0x18, 0x1a, "s_nor_b32"),
   (0x1b, 0x1b, 0x19, 0x19, 0x1b, "s_nor_b64"),
   (0x1c, 0x1c, 0x1a, 0x1a, 0x1c, "s_xnor_b32"),
   (0x1d, 0x1d, 0x1b, 0x1b, 0x1d, "s_xnor_b64"),
   (0x1e, 0x1e, 0x1c, 0x1c, 0x1e, "s_lshl_b32"),
   (0x1f, 0x1f, 0x1d, 0x1d, 0x1f, "s_lshl_b64"),
   (0x20, 0x20, 0x1e, 0x1e, 0x20, "s_lshr_b32"),
   (0x21, 0x21, 0x1f, 0x1f, 0x21, "s_lshr_b64"),
   (0x22, 0x22, 0x20, 0x20, 0x22, "s_ashr_i32"),
   (0x23, 0x23, 0x21, 0x21, 0x23, "s_ashr_i64"),
   (0x24, 0x24, 0x22, 0x22, 0x24, "s_bfm_b32"),
   (0x25, 0x25, 0x23, 0x23, 0x25, "s_bfm_b64"),
   (0x26, 0x26, 0x24, 0x24, 0x26, "s_mul_i32"),
   (0x27, 0x27, 0x25, 0x25, 0x27, "s_bfe_u32"),
   (0x28, 0x28, 0x26, 0x26, 0x28, "s_bfe_i32"),
   (0x29, 0x29, 0x27, 0x27, 0x29, "s_bfe_u64"),
   (0x2a, 0x2a, 0x28, 0x28, 0x2a, "s_bfe_i64"),
   (0x2b, 0x2b, 0x29, 0x29,   -1, "s_cbranch_g_fork", InstrClass.Branch),
   (0x2c, 0x2c, 0x2a, 0x2a, 0x2c, "s_absdiff_i32"),
   (  -1,   -1, 0x2b, 0x2b,   -1, "s_rfe_restore_b64", InstrClass.Branch),
   (  -1,   -1,   -1, 0x2e, 0x2e, "s_lshl1_add_u32"),
   (  -1,   -1,   -1, 0x2f, 0x2f, "s_lshl2_add_u32"),
   (  -1,   -1,   -1, 0x30, 0x30, "s_lshl3_add_u32"),
   (  -1,   -1,   -1, 0x31, 0x31, "s_lshl4_add_u32"),
   (  -1,   -1,   -1, 0x32, 0x32, "s_pack_ll_b32_b16"),
   (  -1,   -1,   -1, 0x33, 0x33, "s_pack_lh_b32_b16"),
   (  -1,   -1,   -1, 0x34, 0x34, "s_pack_hh_b32_b16"),
   (  -1,   -1,   -1, 0x2c, 0x35, "s_mul_hi_u32"),
   (  -1,   -1,   -1, 0x2d, 0x36, "s_mul_hi_i32"),
   # actually a pseudo-instruction. it's lowered to SALU during assembly though, so it's useful to identify it as a SOP2.
   (  -1,   -1,   -1,   -1,   -1, "p_constaddr_addlo"),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name, cls) in default_class(SOP2, InstrClass.Salu):
    opcode(name, gfx7, gfx9, gfx10, Format.SOP2, cls)


# SOPK instructions: 0 input (+ imm), 1 output + optional scc
SOPK = {
  # GFX6, GFX7, GFX8, GFX9, GFX10, name
   (0x00, 0x00, 0x00, 0x00, 0x00, "s_movk_i32"),
   (  -1,   -1,   -1,   -1, 0x01, "s_version"), # GFX10+
   (0x02, 0x02, 0x01, 0x01, 0x02, "s_cmovk_i32"), # GFX8_GFX9
   (0x03, 0x03, 0x02, 0x02, 0x03, "s_cmpk_eq_i32"),
   (0x04, 0x04, 0x03, 0x03, 0x04, "s_cmpk_lg_i32"),
   (0x05, 0x05, 0x04, 0x04, 0x05, "s_cmpk_gt_i32"),
   (0x06, 0x06, 0x05, 0x05, 0x06, "s_cmpk_ge_i32"),
   (0x07, 0x07, 0x06, 0x06, 0x07, "s_cmpk_lt_i32"),
   (0x08, 0x08, 0x07, 0x07, 0x08, "s_cmpk_le_i32"),
   (0x09, 0x09, 0x08, 0x08, 0x09, "s_cmpk_eq_u32"),
   (0x0a, 0x0a, 0x09, 0x09, 0x0a, "s_cmpk_lg_u32"),
   (0x0b, 0x0b, 0x0a, 0x0a, 0x0b, "s_cmpk_gt_u32"),
   (0x0c, 0x0c, 0x0b, 0x0b, 0x0c, "s_cmpk_ge_u32"),
   (0x0d, 0x0d, 0x0c, 0x0c, 0x0d, "s_cmpk_lt_u32"),
   (0x0e, 0x0e, 0x0d, 0x0d, 0x0e, "s_cmpk_le_u32"),
   (0x0f, 0x0f, 0x0e, 0x0e, 0x0f, "s_addk_i32"),
   (0x10, 0x10, 0x0f, 0x0f, 0x10, "s_mulk_i32"),
   (0x11, 0x11, 0x10, 0x10,   -1, "s_cbranch_i_fork", InstrClass.Branch),
   (0x12, 0x12, 0x11, 0x11, 0x12, "s_getreg_b32"),
   (0x13, 0x13, 0x12, 0x12, 0x13, "s_setreg_b32"),
   (0x15, 0x15, 0x14, 0x14, 0x15, "s_setreg_imm32_b32"), # requires 32bit literal
   (  -1,   -1, 0x15, 0x15, 0x16, "s_call_b64", InstrClass.Branch),
   (  -1,   -1,   -1,   -1, 0x17, "s_waitcnt_vscnt", InstrClass.Waitcnt),
   (  -1,   -1,   -1,   -1, 0x18, "s_waitcnt_vmcnt", InstrClass.Waitcnt),
   (  -1,   -1,   -1,   -1, 0x19, "s_waitcnt_expcnt", InstrClass.Waitcnt),
   (  -1,   -1,   -1,   -1, 0x1a, "s_waitcnt_lgkmcnt", InstrClass.Waitcnt),
   (  -1,   -1,   -1,   -1, 0x1b, "s_subvector_loop_begin", InstrClass.Branch),
   (  -1,   -1,   -1,   -1, 0x1c, "s_subvector_loop_end", InstrClass.Branch),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name, cls) in default_class(SOPK, InstrClass.Salu):
   opcode(name, gfx7, gfx9, gfx10, Format.SOPK, cls)


# SOP1 instructions: 1 input, 1 output (+optional SCC)
SOP1 = {
  # GFX6, GFX7, GFX8, GFX9, GFX10, name
   (0x03, 0x03, 0x00, 0x00, 0x03, "s_mov_b32"),
   (0x04, 0x04, 0x01, 0x01, 0x04, "s_mov_b64"),
   (0x05, 0x05, 0x02, 0x02, 0x05, "s_cmov_b32"),
   (0x06, 0x06, 0x03, 0x03, 0x06, "s_cmov_b64"),
   (0x07, 0x07, 0x04, 0x04, 0x07, "s_not_b32"),
   (0x08, 0x08, 0x05, 0x05, 0x08, "s_not_b64"),
   (0x09, 0x09, 0x06, 0x06, 0x09, "s_wqm_b32"),
   (0x0a, 0x0a, 0x07, 0x07, 0x0a, "s_wqm_b64"),
   (0x0b, 0x0b, 0x08, 0x08, 0x0b, "s_brev_b32"),
   (0x0c, 0x0c, 0x09, 0x09, 0x0c, "s_brev_b64"),
   (0x0d, 0x0d, 0x0a, 0x0a, 0x0d, "s_bcnt0_i32_b32"),
   (0x0e, 0x0e, 0x0b, 0x0b, 0x0e, "s_bcnt0_i32_b64"),
   (0x0f, 0x0f, 0x0c, 0x0c, 0x0f, "s_bcnt1_i32_b32"),
   (0x10, 0x10, 0x0d, 0x0d, 0x10, "s_bcnt1_i32_b64"),
   (0x11, 0x11, 0x0e, 0x0e, 0x11, "s_ff0_i32_b32"),
   (0x12, 0x12, 0x0f, 0x0f, 0x12, "s_ff0_i32_b64"),
   (0x13, 0x13, 0x10, 0x10, 0x13, "s_ff1_i32_b32"),
   (0x14, 0x14, 0x11, 0x11, 0x14, "s_ff1_i32_b64"),
   (0x15, 0x15, 0x12, 0x12, 0x15, "s_flbit_i32_b32"),
   (0x16, 0x16, 0x13, 0x13, 0x16, "s_flbit_i32_b64"),
   (0x17, 0x17, 0x14, 0x14, 0x17, "s_flbit_i32"),
   (0x18, 0x18, 0x15, 0x15, 0x18, "s_flbit_i32_i64"),
   (0x19, 0x19, 0x16, 0x16, 0x19, "s_sext_i32_i8"),
   (0x1a, 0x1a, 0x17, 0x17, 0x1a, "s_sext_i32_i16"),
   (0x1b, 0x1b, 0x18, 0x18, 0x1b, "s_bitset0_b32"),
   (0x1c, 0x1c, 0x19, 0x19, 0x1c, "s_bitset0_b64"),
   (0x1d, 0x1d, 0x1a, 0x1a, 0x1d, "s_bitset1_b32"),
   (0x1e, 0x1e, 0x1b, 0x1b, 0x1e, "s_bitset1_b64"),
   (0x1f, 0x1f, 0x1c, 0x1c, 0x1f, "s_getpc_b64"),
   (0x20, 0x20, 0x1d, 0x1d, 0x20, "s_setpc_b64", InstrClass.Branch),
   (0x21, 0x21, 0x1e, 0x1e, 0x21, "s_swappc_b64", InstrClass.Branch),
   (0x22, 0x22, 0x1f, 0x1f, 0x22, "s_rfe_b64", InstrClass.Branch),
   (0x24, 0x24, 0x20, 0x20, 0x24, "s_and_saveexec_b64"),
   (0x25, 0x25, 0x21, 0x21, 0x25, "s_or_saveexec_b64"),
   (0x26, 0x26, 0x22, 0x22, 0x26, "s_xor_saveexec_b64"),
   (0x27, 0x27, 0x23, 0x23, 0x27, "s_andn2_saveexec_b64"),
   (0x28, 0x28, 0x24, 0x24, 0x28, "s_orn2_saveexec_b64"),
   (0x29, 0x29, 0x25, 0x25, 0x29, "s_nand_saveexec_b64"),
   (0x2a, 0x2a, 0x26, 0x26, 0x2a, "s_nor_saveexec_b64"),
   (0x2b, 0x2b, 0x27, 0x27, 0x2b, "s_xnor_saveexec_b64"),
   (0x2c, 0x2c, 0x28, 0x28, 0x2c, "s_quadmask_b32"),
   (0x2d, 0x2d, 0x29, 0x29, 0x2d, "s_quadmask_b64"),
   (0x2e, 0x2e, 0x2a, 0x2a, 0x2e, "s_movrels_b32"),
   (0x2f, 0x2f, 0x2b, 0x2b, 0x2f, "s_movrels_b64"),
   (0x30, 0x30, 0x2c, 0x2c, 0x30, "s_movreld_b32"),
   (0x31, 0x31, 0x2d, 0x2d, 0x31, "s_movreld_b64"),
   (0x32, 0x32, 0x2e, 0x2e,   -1, "s_cbranch_join", InstrClass.Branch),
   (0x34, 0x34, 0x30, 0x30, 0x34, "s_abs_i32"),
   (0x35, 0x35,   -1,   -1, 0x35, "s_mov_fed_b32"),
   (  -1,   -1, 0x32, 0x32,   -1, "s_set_gpr_idx_idx"),
   (  -1,   -1,   -1, 0x33, 0x37, "s_andn1_saveexec_b64"),
   (  -1,   -1,   -1, 0x34, 0x38, "s_orn1_saveexec_b64"),
   (  -1,   -1,   -1, 0x35, 0x39, "s_andn1_wrexec_b64"),
   (  -1,   -1,   -1, 0x36, 0x3a, "s_andn2_wrexec_b64"),
   (  -1,   -1,   -1, 0x37, 0x3b, "s_bitreplicate_b64_b32"),
   (  -1,   -1,   -1,   -1, 0x3c, "s_and_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x3d, "s_or_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x3e, "s_xor_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x3f, "s_andn2_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x40, "s_orn2_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x41, "s_nand_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x42, "s_nor_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x43, "s_xnor_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x44, "s_andn1_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x45, "s_orn1_saveexec_b32"),
   (  -1,   -1,   -1,   -1, 0x46, "s_andn1_wrexec_b32"),
   (  -1,   -1,   -1,   -1, 0x47, "s_andn2_wrexec_b32"),
   (  -1,   -1,   -1,   -1, 0x49, "s_movrelsd_2_b32"),
   # actually a pseudo-instruction. it's lowered to SALU during assembly though, so it's useful to identify it as a SOP1.
   (  -1,   -1,   -1,   -1,   -1, "p_constaddr_getpc"),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name, cls) in default_class(SOP1, InstrClass.Salu):
   opcode(name, gfx7, gfx9, gfx10, Format.SOP1, cls)


# SOPC instructions: 2 inputs and 0 outputs (+SCC)
SOPC = {
  # GFX6, GFX7, GFX8, GFX9, GFX10, name
   (0x00, 0x00, 0x00, 0x00, 0x00, "s_cmp_eq_i32"),
   (0x01, 0x01, 0x01, 0x01, 0x01, "s_cmp_lg_i32"),
   (0x02, 0x02, 0x02, 0x02, 0x02, "s_cmp_gt_i32"),
   (0x03, 0x03, 0x03, 0x03, 0x03, "s_cmp_ge_i32"),
   (0x04, 0x04, 0x04, 0x04, 0x04, "s_cmp_lt_i32"),
   (0x05, 0x05, 0x05, 0x05, 0x05, "s_cmp_le_i32"),
   (0x06, 0x06, 0x06, 0x06, 0x06, "s_cmp_eq_u32"),
   (0x07, 0x07, 0x07, 0x07, 0x07, "s_cmp_lg_u32"),
   (0x08, 0x08, 0x08, 0x08, 0x08, "s_cmp_gt_u32"),
   (0x09, 0x09, 0x09, 0x09, 0x09, "s_cmp_ge_u32"),
   (0x0a, 0x0a, 0x0a, 0x0a, 0x0a, "s_cmp_lt_u32"),
   (0x0b, 0x0b, 0x0b, 0x0b, 0x0b, "s_cmp_le_u32"),
   (0x0c, 0x0c, 0x0c, 0x0c, 0x0c, "s_bitcmp0_b32"),
   (0x0d, 0x0d, 0x0d, 0x0d, 0x0d, "s_bitcmp1_b32"),
   (0x0e, 0x0e, 0x0e, 0x0e, 0x0e, "s_bitcmp0_b64"),
   (0x0f, 0x0f, 0x0f, 0x0f, 0x0f, "s_bitcmp1_b64"),
   (0x10, 0x10, 0x10, 0x10,   -1, "s_setvskip"),
   (  -1,   -1, 0x11, 0x11,   -1, "s_set_gpr_idx_on"),
   (  -1,   -1, 0x12, 0x12, 0x12, "s_cmp_eq_u64"),
   (  -1,   -1, 0x13, 0x13, 0x13, "s_cmp_lg_u64"),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name) in SOPC:
   opcode(name, gfx7, gfx9, gfx10, Format.SOPC, InstrClass.Salu)


# SOPP instructions: 0 inputs (+optional scc/vcc), 0 outputs
SOPP = {
  # GFX6, GFX7, GFX8, GFX9, GFX10, name
   (0x00, 0x00, 0x00, 0x00, 0x00, "s_nop"),
   (0x01, 0x01, 0x01, 0x01, 0x01, "s_endpgm"),
   (0x02, 0x02, 0x02, 0x02, 0x02, "s_branch", InstrClass.Branch),
   (  -1,   -1, 0x03, 0x03, 0x03, "s_wakeup"),
   (0x04, 0x04, 0x04, 0x04, 0x04, "s_cbranch_scc0", InstrClass.Branch),
   (0x05, 0x05, 0x05, 0x05, 0x05, "s_cbranch_scc1", InstrClass.Branch),
   (0x06, 0x06, 0x06, 0x06, 0x06, "s_cbranch_vccz", InstrClass.Branch),
   (0x07, 0x07, 0x07, 0x07, 0x07, "s_cbranch_vccnz", InstrClass.Branch),
   (0x08, 0x08, 0x08, 0x08, 0x08, "s_cbranch_execz", InstrClass.Branch),
   (0x09, 0x09, 0x09, 0x09, 0x09, "s_cbranch_execnz", InstrClass.Branch),
   (0x0a, 0x0a, 0x0a, 0x0a, 0x0a, "s_barrier", InstrClass.Barrier),
   (  -1, 0x0b, 0x0b, 0x0b, 0x0b, "s_setkill"),
   (0x0c, 0x0c, 0x0c, 0x0c, 0x0c, "s_waitcnt", InstrClass.Waitcnt),
   (0x0d, 0x0d, 0x0d, 0x0d, 0x0d, "s_sethalt"),
   (0x0e, 0x0e, 0x0e, 0x0e, 0x0e, "s_sleep"),
   (0x0f, 0x0f, 0x0f, 0x0f, 0x0f, "s_setprio"),
   (0x10, 0x10, 0x10, 0x10, 0x10, "s_sendmsg", InstrClass.Sendmsg),
   (0x11, 0x11, 0x11, 0x11, 0x11, "s_sendmsghalt", InstrClass.Sendmsg),
   (0x12, 0x12, 0x12, 0x12, 0x12, "s_trap", InstrClass.Branch),
   (0x13, 0x13, 0x13, 0x13, 0x13, "s_icache_inv"),
   (0x14, 0x14, 0x14, 0x14, 0x14, "s_incperflevel"),
   (0x15, 0x15, 0x15, 0x15, 0x15, "s_decperflevel"),
   (0x16, 0x16, 0x16, 0x16, 0x16, "s_ttracedata"),
   (  -1, 0x17, 0x17, 0x17, 0x17, "s_cbranch_cdbgsys", InstrClass.Branch),
   (  -1, 0x18, 0x18, 0x18, 0x18, "s_cbranch_cdbguser", InstrClass.Branch),
   (  -1, 0x19, 0x19, 0x19, 0x19, "s_cbranch_cdbgsys_or_user", InstrClass.Branch),
   (  -1, 0x1a, 0x1a, 0x1a, 0x1a, "s_cbranch_cdbgsys_and_user", InstrClass.Branch),
   (  -1,   -1, 0x1b, 0x1b, 0x1b, "s_endpgm_saved"),
   (  -1,   -1, 0x1c, 0x1c,   -1, "s_set_gpr_idx_off"),
   (  -1,   -1, 0x1d, 0x1d,   -1, "s_set_gpr_idx_mode"),
   (  -1,   -1,   -1, 0x1e, 0x1e, "s_endpgm_ordered_ps_done"),
   (  -1,   -1,   -1,   -1, 0x1f, "s_code_end"),
   (  -1,   -1,   -1,   -1, 0x20, "s_inst_prefetch"),
   (  -1,   -1,   -1,   -1, 0x21, "s_clause"),
   (  -1,   -1,   -1,   -1, 0x22, "s_wait_idle"),
   (  -1,   -1,   -1,   -1, 0x23, "s_waitcnt_depctr"),
   (  -1,   -1,   -1,   -1, 0x24, "s_round_mode"),
   (  -1,   -1,   -1,   -1, 0x25, "s_denorm_mode"),
   (  -1,   -1,   -1,   -1, 0x26, "s_ttracedata_imm"),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name, cls) in default_class(SOPP, InstrClass.Salu):
   opcode(name, gfx7, gfx9, gfx10, Format.SOPP, cls)


# SMEM instructions: sbase input (2 sgpr), potentially 2 offset inputs, 1 sdata input/output
# Unlike GFX10, GFX10.3 does not have SMEM store, atomic or scratch instructions
SMEM = {
  # GFX6, GFX7, GFX8, GFX9, GFX10, name
   (0x00, 0x00, 0x00, 0x00, 0x00, "s_load_dword"),
   (0x01, 0x01, 0x01, 0x01, 0x01, "s_load_dwordx2"),
   (0x02, 0x02, 0x02, 0x02, 0x02, "s_load_dwordx4"),
   (0x03, 0x03, 0x03, 0x03, 0x03, "s_load_dwordx8"),
   (0x04, 0x04, 0x04, 0x04, 0x04, "s_load_dwordx16"),
   (  -1,   -1,   -1, 0x05, 0x05, "s_scratch_load_dword"),
   (  -1,   -1,   -1, 0x06, 0x06, "s_scratch_load_dwordx2"),
   (  -1,   -1,   -1, 0x07, 0x07, "s_scratch_load_dwordx4"),
   (0x08, 0x08, 0x08, 0x08, 0x08, "s_buffer_load_dword"),
   (0x09, 0x09, 0x09, 0x09, 0x09, "s_buffer_load_dwordx2"),
   (0x0a, 0x0a, 0x0a, 0x0a, 0x0a, "s_buffer_load_dwordx4"),
   (0x0b, 0x0b, 0x0b, 0x0b, 0x0b, "s_buffer_load_dwordx8"),
   (0x0c, 0x0c, 0x0c, 0x0c, 0x0c, "s_buffer_load_dwordx16"),
   (  -1,   -1, 0x10, 0x10, 0x10, "s_store_dword"),
   (  -1,   -1, 0x11, 0x11, 0x11, "s_store_dwordx2"),
   (  -1,   -1, 0x12, 0x12, 0x12, "s_store_dwordx4"),
   (  -1,   -1,   -1, 0x15, 0x15, "s_scratch_store_dword"),
   (  -1,   -1,   -1, 0x16, 0x16, "s_scratch_store_dwordx2"),
   (  -1,   -1,   -1, 0x17, 0x17, "s_scratch_store_dwordx4"),
   (  -1,   -1, 0x18, 0x18, 0x18, "s_buffer_store_dword"),
   (  -1,   -1, 0x19, 0x19, 0x19, "s_buffer_store_dwordx2"),
   (  -1,   -1, 0x1a, 0x1a, 0x1a, "s_buffer_store_dwordx4"),
   (  -1,   -1, 0x1f, 0x1f, 0x1f, "s_gl1_inv"),
   (0x1f, 0x1f, 0x20, 0x20, 0x20, "s_dcache_inv"),
   (  -1,   -1, 0x21, 0x21, 0x21, "s_dcache_wb"),
   (  -1, 0x1d, 0x22, 0x22,   -1, "s_dcache_inv_vol"),
   (  -1,   -1, 0x23, 0x23,   -1, "s_dcache_wb_vol"),
   (0x1e, 0x1e, 0x24, 0x24, 0x24, "s_memtime"), #GFX6-GFX10
   (  -1,   -1, 0x25, 0x25, 0x25, "s_memrealtime"),
   (  -1,   -1, 0x26, 0x26, 0x26, "s_atc_probe"),
   (  -1,   -1, 0x27, 0x27, 0x27, "s_atc_probe_buffer"),
   (  -1,   -1,   -1, 0x28, 0x28, "s_dcache_discard"),
   (  -1,   -1,   -1, 0x29, 0x29, "s_dcache_discard_x2"),
   (  -1,   -1,   -1,   -1, 0x2a, "s_get_waveid_in_workgroup"),
   (  -1,   -1,   -1, 0x40, 0x40, "s_buffer_atomic_swap"),
   (  -1,   -1,   -1, 0x41, 0x41, "s_buffer_atomic_cmpswap"),
   (  -1,   -1,   -1, 0x42, 0x42, "s_buffer_atomic_add"),
   (  -1,   -1,   -1, 0x43, 0x43, "s_buffer_atomic_sub"),
   (  -1,   -1,   -1, 0x44, 0x44, "s_buffer_atomic_smin"),
   (  -1,   -1,   -1, 0x45, 0x45, "s_buffer_atomic_umin"),
   (  -1,   -1,   -1, 0x46, 0x46, "s_buffer_atomic_smax"),
   (  -1,   -1,   -1, 0x47, 0x47, "s_buffer_atomic_umax"),
   (  -1,   -1,   -1, 0x48, 0x48, "s_buffer_atomic_and"),
   (  -1,   -1,   -1, 0x49, 0x49, "s_buffer_atomic_or"),
   (  -1,   -1,   -1, 0x4a, 0x4a, "s_buffer_atomic_xor"),
   (  -1,   -1,   -1, 0x4b, 0x4b, "s_buffer_atomic_inc"),
   (  -1,   -1,   -1, 0x4c, 0x4c, "s_buffer_atomic_dec"),
   (  -1,   -1,   -1, 0x60, 0x60, "s_buffer_atomic_swap_x2"),
   (  -1,   -1,   -1, 0x61, 0x61, "s_buffer_atomic_cmpswap_x2"),
   (  -1,   -1,   -1, 0x62, 0x62, "s_buffer_atomic_add_x2"),
   (  -1,   -1,   -1, 0x63, 0x63, "s_buffer_atomic_sub_x2"),
   (  -1,   -1,   -1, 0x64, 0x64, "s_buffer_atomic_smin_x2"),
   (  -1,   -1,   -1, 0x65, 0x65, "s_buffer_atomic_umin_x2"),
   (  -1,   -1,   -1, 0x66, 0x66, "s_buffer_atomic_smax_x2"),
   (  -1,   -1,   -1, 0x67, 0x67, "s_buffer_atomic_umax_x2"),
   (  -1,   -1,   -1, 0x68, 0x68, "s_buffer_atomic_and_x2"),
   (  -1,   -1,   -1, 0x69, 0x69, "s_buffer_atomic_or_x2"),
   (  -1,   -1,   -1, 0x6a, 0x6a, "s_buffer_atomic_xor_x2"),
   (  -1,   -1,   -1, 0x6b, 0x6b, "s_buffer_atomic_inc_x2"),
   (  -1,   -1,   -1, 0x6c, 0x6c, "s_buffer_atomic_dec_x2"),
   (  -1,   -1,   -1, 0x80, 0x80, "s_atomic_swap"),
   (  -1,   -1,   -1, 0x81, 0x81, "s_atomic_cmpswap"),
   (  -1,   -1,   -1, 0x82, 0x82, "s_atomic_add"),
   (  -1,   -1,   -1, 0x83, 0x83, "s_atomic_sub"),
   (  -1,   -1,   -1, 0x84, 0x84, "s_atomic_smin"),
   (  -1,   -1,   -1, 0x85, 0x85, "s_atomic_umin"),
   (  -1,   -1,   -1, 0x86, 0x86, "s_atomic_smax"),
   (  -1,   -1,   -1, 0x87, 0x87, "s_atomic_umax"),
   (  -1,   -1,   -1, 0x88, 0x88, "s_atomic_and"),
   (  -1,   -1,   -1, 0x89, 0x89, "s_atomic_or"),
   (  -1,   -1,   -1, 0x8a, 0x8a, "s_atomic_xor"),
   (  -1,   -1,   -1, 0x8b, 0x8b, "s_atomic_inc"),
   (  -1,   -1,   -1, 0x8c, 0x8c, "s_atomic_dec"),
   (  -1,   -1,   -1, 0xa0, 0xa0, "s_atomic_swap_x2"),
   (  -1,   -1,   -1, 0xa1, 0xa1, "s_atomic_cmpswap_x2"),
   (  -1,   -1,   -1, 0xa2, 0xa2, "s_atomic_add_x2"),
   (  -1,   -1,   -1, 0xa3, 0xa3, "s_atomic_sub_x2"),
   (  -1,   -1,   -1, 0xa4, 0xa4, "s_atomic_smin_x2"),
   (  -1,   -1,   -1, 0xa5, 0xa5, "s_atomic_umin_x2"),
   (  -1,   -1,   -1, 0xa6, 0xa6, "s_atomic_smax_x2"),
   (  -1,   -1,   -1, 0xa7, 0xa7, "s_atomic_umax_x2"),
   (  -1,   -1,   -1, 0xa8, 0xa8, "s_atomic_and_x2"),
   (  -1,   -1,   -1, 0xa9, 0xa9, "s_atomic_or_x2"),
   (  -1,   -1,   -1, 0xaa, 0xaa, "s_atomic_xor_x2"),
   (  -1,   -1,   -1, 0xab, 0xab, "s_atomic_inc_x2"),
   (  -1,   -1,   -1, 0xac, 0xac, "s_atomic_dec_x2"),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name) in SMEM:
   opcode(name, gfx7, gfx9, gfx10, Format.SMEM, InstrClass.SMem, is_atomic = "atomic" in name)


# VOP2 instructions: 2 inputs, 1 output (+ optional vcc)
# TODO: misses some GFX6_7 opcodes which were shifted to VOP3 in GFX8
VOP2 = {
  # GFX6, GFX7, GFX8, GFX9, GFX10, name, input/output modifiers
   (0x01, 0x01,   -1,   -1,   -1, "v_readlane_b32", False),
   (0x02, 0x02,   -1,   -1,   -1, "v_writelane_b32", False),
   (0x03, 0x03, 0x01, 0x01, 0x03, "v_add_f32", True),
   (0x04, 0x04, 0x02, 0x02, 0x04, "v_sub_f32", True),
   (0x05, 0x05, 0x03, 0x03, 0x05, "v_subrev_f32", True),
   (0x06, 0x06,   -1,   -1, 0x06, "v_mac_legacy_f32", True),
   (0x07, 0x07, 0x04, 0x04, 0x07, "v_mul_legacy_f32", True),
   (0x08, 0x08, 0x05, 0x05, 0x08, "v_mul_f32", True),
   (0x09, 0x09, 0x06, 0x06, 0x09, "v_mul_i32_i24", False),
   (0x0a, 0x0a, 0x07, 0x07, 0x0a, "v_mul_hi_i32_i24", False),
   (0x0b, 0x0b, 0x08, 0x08, 0x0b, "v_mul_u32_u24", False),
   (0x0c, 0x0c, 0x09, 0x09, 0x0c, "v_mul_hi_u32_u24", False),
   (0x0d, 0x0d,   -1,   -1,   -1, "v_min_legacy_f32", True),
   (0x0e, 0x0e,   -1,   -1,   -1, "v_max_legacy_f32", True),
   (0x0f, 0x0f, 0x0a, 0x0a, 0x0f, "v_min_f32", True),
   (0x10, 0x10, 0x0b, 0x0b, 0x10, "v_max_f32", True),
   (0x11, 0x11, 0x0c, 0x0c, 0x11, "v_min_i32", False),
   (0x12, 0x12, 0x0d, 0x0d, 0x12, "v_max_i32", False),
   (0x13, 0x13, 0x0e, 0x0e, 0x13, "v_min_u32", False),
   (0x14, 0x14, 0x0f, 0x0f, 0x14, "v_max_u32", False),
   (0x15, 0x15,   -1,   -1,   -1, "v_lshr_b32", False),
   (0x16, 0x16, 0x10, 0x10, 0x16, "v_lshrrev_b32", False),
   (0x17, 0x17,   -1,   -1,   -1, "v_ashr_i32", False),
   (0x18, 0x18, 0x11, 0x11, 0x18, "v_ashrrev_i32", False),
   (0x19, 0x19,   -1,   -1,   -1, "v_lshl_b32", False),
   (0x1a, 0x1a, 0x12, 0x12, 0x1a, "v_lshlrev_b32", False),
   (0x1b, 0x1b, 0x13, 0x13, 0x1b, "v_and_b32", False),
   (0x1c, 0x1c, 0x14, 0x14, 0x1c, "v_or_b32", False),
   (0x1d, 0x1d, 0x15, 0x15, 0x1d, "v_xor_b32", False),
   (  -1,   -1,   -1,   -1, 0x1e, "v_xnor_b32", False),
   (0x1f, 0x1f, 0x16, 0x16, 0x1f, "v_mac_f32", True),
   (0x20, 0x20, 0x17, 0x17, 0x20, "v_madmk_f32", False),
   (0x21, 0x21, 0x18, 0x18, 0x21, "v_madak_f32", False),
   (0x24, 0x24,   -1,   -1,   -1, "v_mbcnt_hi_u32_b32", False),
   (0x25, 0x25, 0x19, 0x19,   -1, "v_add_co_u32", False), # VOP3B only in RDNA
   (0x26, 0x26, 0x1a, 0x1a,   -1, "v_sub_co_u32", False), # VOP3B only in RDNA
   (0x27, 0x27, 0x1b, 0x1b,   -1, "v_subrev_co_u32", False), # VOP3B only in RDNA
   (0x28, 0x28, 0x1c, 0x1c, 0x28, "v_addc_co_u32", False), # v_add_co_ci_u32 in RDNA
   (0x29, 0x29, 0x1d, 0x1d, 0x29, "v_subb_co_u32", False), # v_sub_co_ci_u32 in RDNA
   (0x2a, 0x2a, 0x1e, 0x1e, 0x2a, "v_subbrev_co_u32", False), # v_subrev_co_ci_u32 in RDNA
   (  -1,   -1,   -1,   -1, 0x2b, "v_fmac_f32", True),
   (  -1,   -1,   -1,   -1, 0x2c, "v_fmamk_f32", True),
   (  -1,   -1,   -1,   -1, 0x2d, "v_fmaak_f32", True),
   (0x2f, 0x2f,   -1,   -1, 0x2f, "v_cvt_pkrtz_f16_f32", True),
   (  -1,   -1, 0x1f, 0x1f, 0x32, "v_add_f16", True),
   (  -1,   -1, 0x20, 0x20, 0x33, "v_sub_f16", True),
   (  -1,   -1, 0x21, 0x21, 0x34, "v_subrev_f16", True),
   (  -1,   -1, 0x22, 0x22, 0x35, "v_mul_f16", True),
   (  -1,   -1, 0x23, 0x23,   -1, "v_mac_f16", True),
   (  -1,   -1, 0x24, 0x24,   -1, "v_madmk_f16", False),
   (  -1,   -1, 0x25, 0x25,   -1, "v_madak_f16", False),
   (  -1,   -1, 0x26, 0x26,   -1, "v_add_u16", False),
   (  -1,   -1, 0x27, 0x27,   -1, "v_sub_u16", False),
   (  -1,   -1, 0x28, 0x28,   -1, "v_subrev_u16", False),
   (  -1,   -1, 0x29, 0x29,   -1, "v_mul_lo_u16", False),
   (  -1,   -1, 0x2a, 0x2a,   -1, "v_lshlrev_b16", False),
   (  -1,   -1, 0x2b, 0x2b,   -1, "v_lshrrev_b16", False),
   (  -1,   -1, 0x2c, 0x2c,   -1, "v_ashrrev_i16", False),
   (  -1,   -1, 0x2d, 0x2d, 0x39, "v_max_f16", True),
   (  -1,   -1, 0x2e, 0x2e, 0x3a, "v_min_f16", True),
   (  -1,   -1, 0x2f, 0x2f,   -1, "v_max_u16", False),
   (  -1,   -1, 0x30, 0x30,   -1, "v_max_i16", False),
   (  -1,   -1, 0x31, 0x31,   -1, "v_min_u16", False),
   (  -1,   -1, 0x32, 0x32,   -1, "v_min_i16", False),
   (  -1,   -1, 0x33, 0x33, 0x3b, "v_ldexp_f16", False),
   (  -1,   -1,   -1, 0x34, 0x25, "v_add_u32", False), # use v_add_co_u32 on GFX8, called v_add_nc_u32 in RDNA
   (  -1,   -1,   -1, 0x35, 0x26, "v_sub_u32", False), # use v_sub_co_u32 on GFX8, called v_sub_nc_u32 in RDNA
   (  -1,   -1,   -1, 0x36, 0x27, "v_subrev_u32", False), # use v_subrev_co_u32 on GFX8, called v_subrev_nc_u32 in RDNA
   (  -1,   -1,   -1,   -1, 0x36, "v_fmac_f16", False),
   (  -1,   -1,   -1,   -1, 0x37, "v_fmamk_f16", False),
   (  -1,   -1,   -1,   -1, 0x38, "v_fmaak_f16", False),
   (  -1,   -1,   -1,   -1, 0x3c, "v_pk_fmac_f16", False),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name, modifiers) in VOP2:
   opcode(name, gfx7, gfx9, gfx10, Format.VOP2, InstrClass.Valu32, modifiers, modifiers)

if True:
    # v_cndmask_b32 can use input modifiers but not output modifiers
    (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x00, 0x00, 0x00, 0x00, 0x01, "v_cndmask_b32")
    opcode(name, gfx7, gfx9, gfx10, Format.VOP2, InstrClass.Valu32, True, False)


# VOP1 instructions: instructions with 1 input and 1 output
VOP1 = {
  # GFX6, GFX7, GFX8, GFX9, GFX10, name, input_modifiers, output_modifiers
   (0x00, 0x00, 0x00, 0x00, 0x00, "v_nop", False, False),
   (0x01, 0x01, 0x01, 0x01, 0x01, "v_mov_b32", False, False),
   (0x02, 0x02, 0x02, 0x02, 0x02, "v_readfirstlane_b32", False, False),
   (0x03, 0x03, 0x03, 0x03, 0x03, "v_cvt_i32_f64", True, False, InstrClass.ValuDoubleConvert),
   (0x04, 0x04, 0x04, 0x04, 0x04, "v_cvt_f64_i32", False, True, InstrClass.ValuDoubleConvert),
   (0x05, 0x05, 0x05, 0x05, 0x05, "v_cvt_f32_i32", False, True),
   (0x06, 0x06, 0x06, 0x06, 0x06, "v_cvt_f32_u32", False, True),
   (0x07, 0x07, 0x07, 0x07, 0x07, "v_cvt_u32_f32", True, False),
   (0x08, 0x08, 0x08, 0x08, 0x08, "v_cvt_i32_f32", True, False),
   (0x09, 0x09,   -1,   -1, 0x09, "v_mov_fed_b32", True, False), # LLVM mentions it for GFX8_9
   (0x0a, 0x0a, 0x0a, 0x0a, 0x0a, "v_cvt_f16_f32", True, True),
   (  -1,   -1,   -1,   -1,   -1, "p_cvt_f16_f32_rtne", True, True),
   (0x0b, 0x0b, 0x0b, 0x0b, 0x0b, "v_cvt_f32_f16", True, True),
   (0x0c, 0x0c, 0x0c, 0x0c, 0x0c, "v_cvt_rpi_i32_f32", True, False),
   (0x0d, 0x0d, 0x0d, 0x0d, 0x0d, "v_cvt_flr_i32_f32", True, False),
   (0x0e, 0x0e, 0x0e, 0x0e, 0x0e, "v_cvt_off_f32_i4", False, True),
   (0x0f, 0x0f, 0x0f, 0x0f, 0x0f, "v_cvt_f32_f64", True, True, InstrClass.ValuDoubleConvert),
   (0x10, 0x10, 0x10, 0x10, 0x10, "v_cvt_f64_f32", True, True, InstrClass.ValuDoubleConvert),
   (0x11, 0x11, 0x11, 0x11, 0x11, "v_cvt_f32_ubyte0", False, True),
   (0x12, 0x12, 0x12, 0x12, 0x12, "v_cvt_f32_ubyte1", False, True),
   (0x13, 0x13, 0x13, 0x13, 0x13, "v_cvt_f32_ubyte2", False, True),
   (0x14, 0x14, 0x14, 0x14, 0x14, "v_cvt_f32_ubyte3", False, True),
   (0x15, 0x15, 0x15, 0x15, 0x15, "v_cvt_u32_f64", True, False, InstrClass.ValuDoubleConvert),
   (0x16, 0x16, 0x16, 0x16, 0x16, "v_cvt_f64_u32", False, True, InstrClass.ValuDoubleConvert),
   (  -1, 0x17, 0x17, 0x17, 0x17, "v_trunc_f64", True, True, InstrClass.ValuDouble),
   (  -1, 0x18, 0x18, 0x18, 0x18, "v_ceil_f64", True, True, InstrClass.ValuDouble),
   (  -1, 0x19, 0x19, 0x19, 0x19, "v_rndne_f64", True, True, InstrClass.ValuDouble),
   (  -1, 0x1a, 0x1a, 0x1a, 0x1a, "v_floor_f64", True, True, InstrClass.ValuDouble),
   (  -1,   -1,   -1,   -1, 0x1b, "v_pipeflush", False, False),
   (0x20, 0x20, 0x1b, 0x1b, 0x20, "v_fract_f32", True, True),
   (0x21, 0x21, 0x1c, 0x1c, 0x21, "v_trunc_f32", True, True),
   (0x22, 0x22, 0x1d, 0x1d, 0x22, "v_ceil_f32", True, True),
   (0x23, 0x23, 0x1e, 0x1e, 0x23, "v_rndne_f32", True, True),
   (0x24, 0x24, 0x1f, 0x1f, 0x24, "v_floor_f32", True, True),
   (0x25, 0x25, 0x20, 0x20, 0x25, "v_exp_f32", True, True, InstrClass.ValuTranscendental32),
   (0x26, 0x26,   -1,   -1,   -1, "v_log_clamp_f32", True, True, InstrClass.ValuTranscendental32),
   (0x27, 0x27, 0x21, 0x21, 0x27, "v_log_f32", True, True, InstrClass.ValuTranscendental32),
   (0x28, 0x28,   -1,   -1,   -1, "v_rcp_clamp_f32", True, True, InstrClass.ValuTranscendental32),
   (0x29, 0x29,   -1,   -1,   -1, "v_rcp_legacy_f32", True, True, InstrClass.ValuTranscendental32),
   (0x2a, 0x2a, 0x22, 0x22, 0x2a, "v_rcp_f32", True, True, InstrClass.ValuTranscendental32),
   (0x2b, 0x2b, 0x23, 0x23, 0x2b, "v_rcp_iflag_f32", True, True, InstrClass.ValuTranscendental32),
   (0x2c, 0x2c,   -1,   -1,   -1, "v_rsq_clamp_f32", True, True, InstrClass.ValuTranscendental32),
   (0x2d, 0x2d,   -1,   -1,   -1, "v_rsq_legacy_f32", True, True, InstrClass.ValuTranscendental32),
   (0x2e, 0x2e, 0x24, 0x24, 0x2e, "v_rsq_f32", True, True, InstrClass.ValuTranscendental32),
   (0x2f, 0x2f, 0x25, 0x25, 0x2f, "v_rcp_f64", True, True, InstrClass.ValuDoubleTranscendental),
   (0x30, 0x30,   -1,   -1,   -1, "v_rcp_clamp_f64", True, True, InstrClass.ValuDoubleTranscendental),
   (0x31, 0x31, 0x26, 0x26, 0x31, "v_rsq_f64", True, True, InstrClass.ValuDoubleTranscendental),
   (0x32, 0x32,   -1,   -1,   -1, "v_rsq_clamp_f64", True, True, InstrClass.ValuDoubleTranscendental),
   (0x33, 0x33, 0x27, 0x27, 0x33, "v_sqrt_f32", True, True, InstrClass.ValuTranscendental32),
   (0x34, 0x34, 0x28, 0x28, 0x34, "v_sqrt_f64", True, True, InstrClass.ValuDoubleTranscendental),
   (0x35, 0x35, 0x29, 0x29, 0x35, "v_sin_f32", True, True, InstrClass.ValuTranscendental32),
   (0x36, 0x36, 0x2a, 0x2a, 0x36, "v_cos_f32", True, True, InstrClass.ValuTranscendental32),
   (0x37, 0x37, 0x2b, 0x2b, 0x37, "v_not_b32", False, False),
   (0x38, 0x38, 0x2c, 0x2c, 0x38, "v_bfrev_b32", False, False),
   (0x39, 0x39, 0x2d, 0x2d, 0x39, "v_ffbh_u32", False, False),
   (0x3a, 0x3a, 0x2e, 0x2e, 0x3a, "v_ffbl_b32", False, False),
   (0x3b, 0x3b, 0x2f, 0x2f, 0x3b, "v_ffbh_i32", False, False),
   (0x3c, 0x3c, 0x30, 0x30, 0x3c, "v_frexp_exp_i32_f64", True, False, InstrClass.ValuDouble),
   (0x3d, 0x3d, 0x31, 0x31, 0x3d, "v_frexp_mant_f64", True, False, InstrClass.ValuDouble),
   (0x3e, 0x3e, 0x32, 0x32, 0x3e, "v_fract_f64", True, True, InstrClass.ValuDouble),
   (0x3f, 0x3f, 0x33, 0x33, 0x3f, "v_frexp_exp_i32_f32", True, False),
   (0x40, 0x40, 0x34, 0x34, 0x40, "v_frexp_mant_f32", True, False),
   (0x41, 0x41, 0x35, 0x35, 0x41, "v_clrexcp", False, False),
   (0x42, 0x42, 0x36,   -1, 0x42, "v_movreld_b32", False, False),
   (0x43, 0x43, 0x37,   -1, 0x43, "v_movrels_b32", False, False),
   (0x44, 0x44, 0x38,   -1, 0x44, "v_movrelsd_b32", False, False),
   (  -1,   -1,   -1,   -1, 0x48, "v_movrelsd_2_b32", False, False),
   (  -1,   -1,   -1, 0x37,   -1, "v_screen_partition_4se_b32", False, False),
   (  -1,   -1, 0x39, 0x39, 0x50, "v_cvt_f16_u16", False, True),
   (  -1,   -1, 0x3a, 0x3a, 0x51, "v_cvt_f16_i16", False, True),
   (  -1,   -1, 0x3b, 0x3b, 0x52, "v_cvt_u16_f16", True, False),
   (  -1,   -1, 0x3c, 0x3c, 0x53, "v_cvt_i16_f16", True, False),
   (  -1,   -1, 0x3d, 0x3d, 0x54, "v_rcp_f16", True, True, InstrClass.ValuTranscendental32),
   (  -1,   -1, 0x3e, 0x3e, 0x55, "v_sqrt_f16", True, True, InstrClass.ValuTranscendental32),
   (  -1,   -1, 0x3f, 0x3f, 0x56, "v_rsq_f16", True, True, InstrClass.ValuTranscendental32),
   (  -1,   -1, 0x40, 0x40, 0x57, "v_log_f16", True, True, InstrClass.ValuTranscendental32),
   (  -1,   -1, 0x41, 0x41, 0x58, "v_exp_f16", True, True, InstrClass.ValuTranscendental32),
   (  -1,   -1, 0x42, 0x42, 0x59, "v_frexp_mant_f16", True, False),
   (  -1,   -1, 0x43, 0x43, 0x5a, "v_frexp_exp_i16_f16", True, False),
   (  -1,   -1, 0x44, 0x44, 0x5b, "v_floor_f16", True, True),
   (  -1,   -1, 0x45, 0x45, 0x5c, "v_ceil_f16", True, True),
   (  -1,   -1, 0x46, 0x46, 0x5d, "v_trunc_f16", True, True),
   (  -1,   -1, 0x47, 0x47, 0x5e, "v_rndne_f16", True, True),
   (  -1,   -1, 0x48, 0x48, 0x5f, "v_fract_f16", True, True),
   (  -1,   -1, 0x49, 0x49, 0x60, "v_sin_f16", True, True, InstrClass.ValuTranscendental32),
   (  -1,   -1, 0x4a, 0x4a, 0x61, "v_cos_f16", True, True, InstrClass.ValuTranscendental32),
   (  -1, 0x46, 0x4b, 0x4b,   -1, "v_exp_legacy_f32", True, True, InstrClass.ValuTranscendental32),
   (  -1, 0x45, 0x4c, 0x4c,   -1, "v_log_legacy_f32", True, True, InstrClass.ValuTranscendental32),
   (  -1,   -1,   -1, 0x4f, 0x62, "v_sat_pk_u8_i16", False, False),
   (  -1,   -1,   -1, 0x4d, 0x63, "v_cvt_norm_i16_f16", True, False),
   (  -1,   -1,   -1, 0x4e, 0x64, "v_cvt_norm_u16_f16", True, False),
   (  -1,   -1,   -1, 0x51, 0x65, "v_swap_b32", False, False),
   (  -1,   -1,   -1,   -1, 0x68, "v_swaprel_b32", False, False),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name, in_mod, out_mod, cls) in default_class(VOP1, InstrClass.Valu32):
   opcode(name, gfx7, gfx9, gfx10, Format.VOP1, cls, in_mod, out_mod)


# VOPC instructions:

VOPC_CLASS = {
   (0x88, 0x88, 0x10, 0x10, 0x88, "v_cmp_class_f32"),
   (  -1,   -1, 0x14, 0x14, 0x8f, "v_cmp_class_f16"),
   (0x98, 0x98, 0x11, 0x11, 0x98, "v_cmpx_class_f32"),
   (  -1,   -1, 0x15, 0x15, 0x9f, "v_cmpx_class_f16"),
   (0xa8, 0xa8, 0x12, 0x12, 0xa8, "v_cmp_class_f64", InstrClass.ValuDouble),
   (0xb8, 0xb8, 0x13, 0x13, 0xb8, "v_cmpx_class_f64", InstrClass.ValuDouble),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name, cls) in default_class(VOPC_CLASS, InstrClass.Valu32):
    opcode(name, gfx7, gfx9, gfx10, Format.VOPC, cls, True, False)

COMPF = ["f", "lt", "eq", "le", "gt", "lg", "ge", "o", "u", "nge", "nlg", "ngt", "nle", "neq", "nlt", "tru"]

for i in range(8):
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0x20+i, 0x20+i, 0xc8+i, "v_cmp_"+COMPF[i]+"_f16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32, True, False)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0x30+i, 0x30+i, 0xd8+i, "v_cmpx_"+COMPF[i]+"_f16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32, True, False)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0x28+i, 0x28+i, 0xe8+i, "v_cmp_"+COMPF[i+8]+"_f16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32, True, False)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0x38+i, 0x38+i, 0xf8+i, "v_cmpx_"+COMPF[i+8]+"_f16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32, True, False)

for i in range(16):
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x00+i, 0x00+i, 0x40+i, 0x40+i, 0x00+i, "v_cmp_"+COMPF[i]+"_f32")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32, True, False)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x10+i, 0x10+i, 0x50+i, 0x50+i, 0x10+i, "v_cmpx_"+COMPF[i]+"_f32")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32, True, False)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x20+i, 0x20+i, 0x60+i, 0x60+i, 0x20+i, "v_cmp_"+COMPF[i]+"_f64")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.ValuDouble, True, False)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x30+i, 0x30+i, 0x70+i, 0x70+i, 0x30+i, "v_cmpx_"+COMPF[i]+"_f64")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.ValuDouble, True, False)
   # GFX_6_7
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x40+i, 0x40+i, -1, -1, -1, "v_cmps_"+COMPF[i]+"_f32")
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x50+i, 0x50+i, -1, -1, -1, "v_cmpsx_"+COMPF[i]+"_f32")
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x60+i, 0x60+i, -1, -1, -1, "v_cmps_"+COMPF[i]+"_f64")
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x70+i, 0x70+i, -1, -1, -1, "v_cmpsx_"+COMPF[i]+"_f64")

COMPI = ["f", "lt", "eq", "le", "gt", "lg", "ge", "tru"]

# GFX_8_9
for i in [0,7]: # only 0 and 7
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0xa0+i, 0xa0+i, -1, "v_cmp_"+COMPI[i]+"_i16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0xb0+i, 0xb0+i, -1, "v_cmpx_"+COMPI[i]+"_i16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0xa8+i, 0xa8+i, -1, "v_cmp_"+COMPI[i]+"_u16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0xb8+i, 0xb8+i, -1, "v_cmpx_"+COMPI[i]+"_u16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)

for i in range(1, 7): # [1..6]
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0xa0+i, 0xa0+i, 0x88+i, "v_cmp_"+COMPI[i]+"_i16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0xb0+i, 0xb0+i, 0x98+i, "v_cmpx_"+COMPI[i]+"_i16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0xa8+i, 0xa8+i, 0xa8+i, "v_cmp_"+COMPI[i]+"_u16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, 0xb8+i, 0xb8+i, 0xb8+i, "v_cmpx_"+COMPI[i]+"_u16")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)

for i in range(8):
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x80+i, 0x80+i, 0xc0+i, 0xc0+i, 0x80+i, "v_cmp_"+COMPI[i]+"_i32")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0x90+i, 0x90+i, 0xd0+i, 0xd0+i, 0x90+i, "v_cmpx_"+COMPI[i]+"_i32")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0xa0+i, 0xa0+i, 0xe0+i, 0xe0+i, 0xa0+i, "v_cmp_"+COMPI[i]+"_i64")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu64)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0xb0+i, 0xb0+i, 0xf0+i, 0xf0+i, 0xb0+i, "v_cmpx_"+COMPI[i]+"_i64")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu64)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0xc0+i, 0xc0+i, 0xc8+i, 0xc8+i, 0xc0+i, "v_cmp_"+COMPI[i]+"_u32")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0xd0+i, 0xd0+i, 0xd8+i, 0xd8+i, 0xd0+i, "v_cmpx_"+COMPI[i]+"_u32")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu32)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0xe0+i, 0xe0+i, 0xe8+i, 0xe8+i, 0xe0+i, "v_cmp_"+COMPI[i]+"_u64")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu64)
   (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (0xf0+i, 0xf0+i, 0xf8+i, 0xf8+i, 0xf0+i, "v_cmpx_"+COMPI[i]+"_u64")
   opcode(name, gfx7, gfx9, gfx10, Format.VOPC, InstrClass.Valu64)


# VOPP instructions: packed 16bit instructions - 1 or 2 inputs and 1 output
VOPP = {
   # opcode, name, input/output modifiers
   (0x00, "v_pk_mad_i16", False),
   (0x01, "v_pk_mul_lo_u16", False),
   (0x02, "v_pk_add_i16", False),
   (0x03, "v_pk_sub_i16", False),
   (0x04, "v_pk_lshlrev_b16", False),
   (0x05, "v_pk_lshrrev_b16", False),
   (0x06, "v_pk_ashrrev_i16", False),
   (0x07, "v_pk_max_i16", False),
   (0x08, "v_pk_min_i16", False),
   (0x09, "v_pk_mad_u16", False),
   (0x0a, "v_pk_add_u16", False),
   (0x0b, "v_pk_sub_u16", False),
   (0x0c, "v_pk_max_u16", False),
   (0x0d, "v_pk_min_u16", False),
   (0x0e, "v_pk_fma_f16", True),
   (0x0f, "v_pk_add_f16", True),
   (0x10, "v_pk_mul_f16", True),
   (0x11, "v_pk_min_f16", True),
   (0x12, "v_pk_max_f16", True),
   (0x20, "v_fma_mix_f32", True), # v_mad_mix_f32 in VEGA ISA, v_fma_mix_f32 in RDNA ISA
   (0x21, "v_fma_mixlo_f16", True), # v_mad_mixlo_f16 in VEGA ISA, v_fma_mixlo_f16 in RDNA ISA
   (0x22, "v_fma_mixhi_f16", True), # v_mad_mixhi_f16 in VEGA ISA, v_fma_mixhi_f16 in RDNA ISA
}
# note that these are only supported on gfx9+ so we'll need to distinguish between gfx8 and gfx9 here
# (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (-1, -1, -1, code, code, name)
for (code, name, modifiers) in VOPP:
   opcode(name, -1, code, code, Format.VOP3P, InstrClass.Valu32, modifiers, modifiers)


# VINTERP instructions: 
VINTRP = {
   (0x00, "v_interp_p1_f32"),
   (0x01, "v_interp_p2_f32"),
   (0x02, "v_interp_mov_f32"),
}
# (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (code, code, code, code, code, name)
for (code, name) in VINTRP:
   opcode(name, code, code, code, Format.VINTRP, InstrClass.Valu32)

# VOP3 instructions: 3 inputs, 1 output
# VOP3b instructions: have a unique scalar output, e.g. VOP2 with vcc out
VOP3 = {
   (0x140, 0x140, 0x1c0, 0x1c0, 0x140, "v_mad_legacy_f32", True, True), # GFX6-GFX10
   (0x141, 0x141, 0x1c1, 0x1c1, 0x141, "v_mad_f32", True, True),
   (0x142, 0x142, 0x1c2, 0x1c2, 0x142, "v_mad_i32_i24", False, False),
   (0x143, 0x143, 0x1c3, 0x1c3, 0x143, "v_mad_u32_u24", False, False),
   (0x144, 0x144, 0x1c4, 0x1c4, 0x144, "v_cubeid_f32", True, True),
   (0x145, 0x145, 0x1c5, 0x1c5, 0x145, "v_cubesc_f32", True, True),
   (0x146, 0x146, 0x1c6, 0x1c6, 0x146, "v_cubetc_f32", True, True),
   (0x147, 0x147, 0x1c7, 0x1c7, 0x147, "v_cubema_f32", True, True),
   (0x148, 0x148, 0x1c8, 0x1c8, 0x148, "v_bfe_u32", False, False),
   (0x149, 0x149, 0x1c9, 0x1c9, 0x149, "v_bfe_i32", False, False),
   (0x14a, 0x14a, 0x1ca, 0x1ca, 0x14a, "v_bfi_b32", False, False),
   (0x14b, 0x14b, 0x1cb, 0x1cb, 0x14b, "v_fma_f32", True, True, InstrClass.ValuFma),
   (0x14c, 0x14c, 0x1cc, 0x1cc, 0x14c, "v_fma_f64", True, True, InstrClass.ValuDouble),
   (0x14d, 0x14d, 0x1cd, 0x1cd, 0x14d, "v_lerp_u8", False, False),
   (0x14e, 0x14e, 0x1ce, 0x1ce, 0x14e, "v_alignbit_b32", False, False),
   (0x14f, 0x14f, 0x1cf, 0x1cf, 0x14f, "v_alignbyte_b32", False, False),
   (0x150, 0x150,    -1,    -1, 0x150, "v_mullit_f32", True, True),
   (0x151, 0x151, 0x1d0, 0x1d0, 0x151, "v_min3_f32", True, True),
   (0x152, 0x152, 0x1d1, 0x1d1, 0x152, "v_min3_i32", False, False),
   (0x153, 0x153, 0x1d2, 0x1d2, 0x153, "v_min3_u32", False, False),
   (0x154, 0x154, 0x1d3, 0x1d3, 0x154, "v_max3_f32", True, True),
   (0x155, 0x155, 0x1d4, 0x1d4, 0x155, "v_max3_i32", False, False),
   (0x156, 0x156, 0x1d5, 0x1d5, 0x156, "v_max3_u32", False, False),
   (0x157, 0x157, 0x1d6, 0x1d6, 0x157, "v_med3_f32", True, True),
   (0x158, 0x158, 0x1d7, 0x1d7, 0x158, "v_med3_i32", False, False),
   (0x159, 0x159, 0x1d8, 0x1d8, 0x159, "v_med3_u32", False, False),
   (0x15a, 0x15a, 0x1d9, 0x1d9, 0x15a, "v_sad_u8", False, False),
   (0x15b, 0x15b, 0x1da, 0x1da, 0x15b, "v_sad_hi_u8", False, False),
   (0x15c, 0x15c, 0x1db, 0x1db, 0x15c, "v_sad_u16", False, False),
   (0x15d, 0x15d, 0x1dc, 0x1dc, 0x15d, "v_sad_u32", False, False),
   (0x15e, 0x15e, 0x1dd, 0x1dd, 0x15e, "v_cvt_pk_u8_f32", True, False),
   (0x15f, 0x15f, 0x1de, 0x1de, 0x15f, "v_div_fixup_f32", True, True),
   (0x160, 0x160, 0x1df, 0x1df, 0x160, "v_div_fixup_f64", True, True),
   (0x161, 0x161,    -1,    -1,    -1, "v_lshl_b64", False, False, InstrClass.Valu64),
   (0x162, 0x162,    -1,    -1,    -1, "v_lshr_b64", False, False, InstrClass.Valu64),
   (0x163, 0x163,    -1,    -1,    -1, "v_ashr_i64", False, False, InstrClass.Valu64),
   (0x164, 0x164, 0x280, 0x280, 0x164, "v_add_f64", True, True, InstrClass.ValuDoubleAdd),
   (0x165, 0x165, 0x281, 0x281, 0x165, "v_mul_f64", True, True, InstrClass.ValuDouble),
   (0x166, 0x166, 0x282, 0x282, 0x166, "v_min_f64", True, True, InstrClass.ValuDouble),
   (0x167, 0x167, 0x283, 0x283, 0x167, "v_max_f64", True, True, InstrClass.ValuDouble),
   (0x168, 0x168, 0x284, 0x284, 0x168, "v_ldexp_f64", False, True, InstrClass.ValuDouble), # src1 can take input modifiers
   (0x169, 0x169, 0x285, 0x285, 0x169, "v_mul_lo_u32", False, False, InstrClass.ValuQuarterRate32),
   (0x16a, 0x16a, 0x286, 0x286, 0x16a, "v_mul_hi_u32", False, False, InstrClass.ValuQuarterRate32),
   (0x16b, 0x16b, 0x285, 0x285, 0x16b, "v_mul_lo_i32", False, False, InstrClass.ValuQuarterRate32), # identical to v_mul_lo_u32
   (0x16c, 0x16c, 0x287, 0x287, 0x16c, "v_mul_hi_i32", False, False, InstrClass.ValuQuarterRate32),
   (0x16d, 0x16d, 0x1e0, 0x1e0, 0x16d, "v_div_scale_f32", True, True), # writes to VCC
   (0x16e, 0x16e, 0x1e1, 0x1e1, 0x16e, "v_div_scale_f64", True, True, InstrClass.ValuDouble), # writes to VCC
   (0x16f, 0x16f, 0x1e2, 0x1e2, 0x16f, "v_div_fmas_f32", True, True), # takes VCC input
   (0x170, 0x170, 0x1e3, 0x1e3, 0x170, "v_div_fmas_f64", True, True, InstrClass.ValuDouble), # takes VCC input
   (0x171, 0x171, 0x1e4, 0x1e4, 0x171, "v_msad_u8", False, False),
   (0x172, 0x172, 0x1e5, 0x1e5, 0x172, "v_qsad_pk_u16_u8", False, False),
   (0x172,    -1,    -1,    -1,    -1, "v_qsad_u8", False, False), # what's the difference?
   (0x173, 0x173, 0x1e6, 0x1e6, 0x173, "v_mqsad_pk_u16_u8", False, False),
   (0x173,    -1,    -1,    -1,    -1, "v_mqsad_u8", False, False), # what's the difference?
   (0x174, 0x174, 0x292, 0x292, 0x174, "v_trig_preop_f64", False, False, InstrClass.ValuDouble),
   (   -1, 0x175, 0x1e7, 0x1e7, 0x175, "v_mqsad_u32_u8", False, False),
   (   -1, 0x176, 0x1e8, 0x1e8, 0x176, "v_mad_u64_u32", False, False, InstrClass.Valu64),
   (   -1, 0x177, 0x1e9, 0x1e9, 0x177, "v_mad_i64_i32", False, False, InstrClass.Valu64),
   (   -1,    -1, 0x1ea, 0x1ea,    -1, "v_mad_legacy_f16", True, True),
   (   -1,    -1, 0x1eb, 0x1eb,    -1, "v_mad_legacy_u16", False, False),
   (   -1,    -1, 0x1ec, 0x1ec,    -1, "v_mad_legacy_i16", False, False),
   (   -1,    -1, 0x1ed, 0x1ed, 0x344, "v_perm_b32", False, False),
   (   -1,    -1, 0x1ee, 0x1ee,    -1, "v_fma_legacy_f16", True, True, InstrClass.ValuFma),
   (   -1,    -1, 0x1ef, 0x1ef,    -1, "v_div_fixup_legacy_f16", True, True),
   (0x12c, 0x12c, 0x1f0, 0x1f0,    -1, "v_cvt_pkaccum_u8_f32", True, False),
   (   -1,    -1,    -1, 0x1f1, 0x373, "v_mad_u32_u16", False, False),
   (   -1,    -1,    -1, 0x1f2, 0x375, "v_mad_i32_i16", False, False),
   (   -1,    -1,    -1, 0x1f3, 0x345, "v_xad_u32", False, False),
   (   -1,    -1,    -1, 0x1f4, 0x351, "v_min3_f16", True, True),
   (   -1,    -1,    -1, 0x1f5, 0x352, "v_min3_i16", False, False),
   (   -1,    -1,    -1, 0x1f6, 0x353, "v_min3_u16", False, False),
   (   -1,    -1,    -1, 0x1f7, 0x354, "v_max3_f16", True, True),
   (   -1,    -1,    -1, 0x1f8, 0x355, "v_max3_i16", False, False),
   (   -1,    -1,    -1, 0x1f9, 0x356, "v_max3_u16", False, False),
   (   -1,    -1,    -1, 0x1fa, 0x357, "v_med3_f16", True, True),
   (   -1,    -1,    -1, 0x1fb, 0x358, "v_med3_i16", False, False),
   (   -1,    -1,    -1, 0x1fc, 0x359, "v_med3_u16", False, False),
   (   -1,    -1,    -1, 0x1fd, 0x346, "v_lshl_add_u32", False, False),
   (   -1,    -1,    -1, 0x1fe, 0x347, "v_add_lshl_u32", False, False),
   (   -1,    -1,    -1, 0x1ff, 0x36d, "v_add3_u32", False, False),
   (   -1,    -1,    -1, 0x200, 0x36f, "v_lshl_or_b32", False, False),
   (   -1,    -1,    -1, 0x201, 0x371, "v_and_or_b32", False, False),
   (   -1,    -1,    -1, 0x202, 0x372, "v_or3_b32", False, False),
   (   -1,    -1,    -1, 0x203,    -1, "v_mad_f16", True, True),
   (   -1,    -1,    -1, 0x204, 0x340, "v_mad_u16", False, False),
   (   -1,    -1,    -1, 0x205, 0x35e, "v_mad_i16", False, False),
   (   -1,    -1,    -1, 0x206, 0x34b, "v_fma_f16", True, True),
   (   -1,    -1,    -1, 0x207, 0x35f, "v_div_fixup_f16", True, True),
   (   -1,    -1, 0x274, 0x274, 0x342, "v_interp_p1ll_f16", True, True),
   (   -1,    -1, 0x275, 0x275, 0x343, "v_interp_p1lv_f16", True, True),
   (   -1,    -1, 0x276, 0x276,    -1, "v_interp_p2_legacy_f16", True, True),
   (   -1,    -1,    -1, 0x277, 0x35a, "v_interp_p2_f16", True, True),
   (0x12b, 0x12b, 0x288, 0x288, 0x362, "v_ldexp_f32", False, True),
   (   -1,    -1, 0x289, 0x289, 0x360, "v_readlane_b32_e64", False, False),
   (   -1,    -1, 0x28a, 0x28a, 0x361, "v_writelane_b32_e64", False, False),
   (0x122, 0x122, 0x28b, 0x28b, 0x364, "v_bcnt_u32_b32", False, False),
   (0x123, 0x123, 0x28c, 0x28c, 0x365, "v_mbcnt_lo_u32_b32", False, False),
   (   -1,    -1, 0x28d, 0x28d, 0x366, "v_mbcnt_hi_u32_b32_e64", False, False),
   (   -1,    -1, 0x28f, 0x28f, 0x2ff, "v_lshlrev_b64", False, False, InstrClass.Valu64),
   (   -1,    -1, 0x290, 0x290, 0x300, "v_lshrrev_b64", False, False, InstrClass.Valu64),
   (   -1,    -1, 0x291, 0x291, 0x301, "v_ashrrev_i64", False, False, InstrClass.Valu64),
   (0x11e, 0x11e, 0x293, 0x293, 0x363, "v_bfm_b32", False, False),
   (0x12d, 0x12d, 0x294, 0x294, 0x368, "v_cvt_pknorm_i16_f32", True, False),
   (0x12e, 0x12e, 0x295, 0x295, 0x369, "v_cvt_pknorm_u16_f32", True, False),
   (0x12f, 0x12f, 0x296, 0x296, 0x12f, "v_cvt_pkrtz_f16_f32_e64", True, False), # GFX6_7_10 is VOP2 with opcode 0x02f
   (0x130, 0x130, 0x297, 0x297, 0x36a, "v_cvt_pk_u16_u32", False, False),
   (0x131, 0x131, 0x298, 0x298, 0x36b, "v_cvt_pk_i16_i32", False, False),
   (   -1,    -1,    -1, 0x299, 0x312, "v_cvt_pknorm_i16_f16", True, False),
   (   -1,    -1,    -1, 0x29a, 0x313, "v_cvt_pknorm_u16_f16", True, False),
   (   -1,    -1,    -1, 0x29c, 0x37f, "v_add_i32", False, False),
   (   -1,    -1,    -1, 0x29d, 0x376, "v_sub_i32", False, False),
   (   -1,    -1,    -1, 0x29e, 0x30d, "v_add_i16", False, False),
   (   -1,    -1,    -1, 0x29f, 0x30e, "v_sub_i16", False, False),
   (   -1,    -1,    -1, 0x2a0, 0x311, "v_pack_b32_f16", True, False),
   (   -1,    -1,    -1,    -1, 0x178, "v_xor3_b32", False, False),
   (   -1,    -1,    -1,    -1, 0x377, "v_permlane16_b32", False, False),
   (   -1,    -1,    -1,    -1, 0x378, "v_permlanex16_b32", False, False),
   (   -1,    -1,    -1,    -1, 0x30f, "v_add_co_u32_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x310, "v_sub_co_u32_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x319, "v_subrev_co_u32_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x303, "v_add_u16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x304, "v_sub_u16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x305, "v_mul_lo_u16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x309, "v_max_u16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x30a, "v_max_i16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x30b, "v_min_u16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x30c, "v_min_i16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x307, "v_lshrrev_b16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x308, "v_ashrrev_i16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x314, "v_lshlrev_b16_e64", False, False),
   (   -1,    -1,    -1,    -1, 0x140, "v_fma_legacy_f32", True, True, InstrClass.ValuFma), #GFX10.3+
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name, in_mod, out_mod, cls) in default_class(VOP3, InstrClass.Valu32):
   opcode(name, gfx7, gfx9, gfx10, Format.VOP3, cls, in_mod, out_mod)


# DS instructions: 3 inputs (1 addr, 2 data), 1 output
DS = {
   (0x00, 0x00, 0x00, 0x00, 0x00, "ds_add_u32"),
   (0x01, 0x01, 0x01, 0x01, 0x01, "ds_sub_u32"),
   (0x02, 0x02, 0x02, 0x02, 0x02, "ds_rsub_u32"),
   (0x03, 0x03, 0x03, 0x03, 0x03, "ds_inc_u32"),
   (0x04, 0x04, 0x04, 0x04, 0x04, "ds_dec_u32"),
   (0x05, 0x05, 0x05, 0x05, 0x05, "ds_min_i32"),
   (0x06, 0x06, 0x06, 0x06, 0x06, "ds_max_i32"),
   (0x07, 0x07, 0x07, 0x07, 0x07, "ds_min_u32"),
   (0x08, 0x08, 0x08, 0x08, 0x08, "ds_max_u32"),
   (0x09, 0x09, 0x09, 0x09, 0x09, "ds_and_b32"),
   (0x0a, 0x0a, 0x0a, 0x0a, 0x0a, "ds_or_b32"),
   (0x0b, 0x0b, 0x0b, 0x0b, 0x0b, "ds_xor_b32"),
   (0x0c, 0x0c, 0x0c, 0x0c, 0x0c, "ds_mskor_b32"),
   (0x0d, 0x0d, 0x0d, 0x0d, 0x0d, "ds_write_b32"),
   (0x0e, 0x0e, 0x0e, 0x0e, 0x0e, "ds_write2_b32"),
   (0x0f, 0x0f, 0x0f, 0x0f, 0x0f, "ds_write2st64_b32"),
   (0x10, 0x10, 0x10, 0x10, 0x10, "ds_cmpst_b32"),
   (0x11, 0x11, 0x11, 0x11, 0x11, "ds_cmpst_f32"),
   (0x12, 0x12, 0x12, 0x12, 0x12, "ds_min_f32"),
   (0x13, 0x13, 0x13, 0x13, 0x13, "ds_max_f32"),
   (  -1, 0x14, 0x14, 0x14, 0x14, "ds_nop"),
   (  -1,   -1, 0x15, 0x15, 0x15, "ds_add_f32"),
   (  -1,   -1, 0x1d, 0x1d, 0xb0, "ds_write_addtid_b32"),
   (0x1e, 0x1e, 0x1e, 0x1e, 0x1e, "ds_write_b8"),
   (0x1f, 0x1f, 0x1f, 0x1f, 0x1f, "ds_write_b16"),
   (0x20, 0x20, 0x20, 0x20, 0x20, "ds_add_rtn_u32"),
   (0x21, 0x21, 0x21, 0x21, 0x21, "ds_sub_rtn_u32"),
   (0x22, 0x22, 0x22, 0x22, 0x22, "ds_rsub_rtn_u32"),
   (0x23, 0x23, 0x23, 0x23, 0x23, "ds_inc_rtn_u32"),
   (0x24, 0x24, 0x24, 0x24, 0x24, "ds_dec_rtn_u32"),
   (0x25, 0x25, 0x25, 0x25, 0x25, "ds_min_rtn_i32"),
   (0x26, 0x26, 0x26, 0x26, 0x26, "ds_max_rtn_i32"),
   (0x27, 0x27, 0x27, 0x27, 0x27, "ds_min_rtn_u32"),
   (0x28, 0x28, 0x28, 0x28, 0x28, "ds_max_rtn_u32"),
   (0x29, 0x29, 0x29, 0x29, 0x29, "ds_and_rtn_b32"),
   (0x2a, 0x2a, 0x2a, 0x2a, 0x2a, "ds_or_rtn_b32"),
   (0x2b, 0x2b, 0x2b, 0x2b, 0x2b, "ds_xor_rtn_b32"),
   (0x2c, 0x2c, 0x2c, 0x2c, 0x2c, "ds_mskor_rtn_b32"),
   (0x2d, 0x2d, 0x2d, 0x2d, 0x2d, "ds_wrxchg_rtn_b32"),
   (0x2e, 0x2e, 0x2e, 0x2e, 0x2e, "ds_wrxchg2_rtn_b32"),
   (0x2f, 0x2f, 0x2f, 0x2f, 0x2f, "ds_wrxchg2st64_rtn_b32"),
   (0x30, 0x30, 0x30, 0x30, 0x30, "ds_cmpst_rtn_b32"),
   (0x31, 0x31, 0x31, 0x31, 0x31, "ds_cmpst_rtn_f32"),
   (0x32, 0x32, 0x32, 0x32, 0x32, "ds_min_rtn_f32"),
   (0x33, 0x33, 0x33, 0x33, 0x33, "ds_max_rtn_f32"),
   (  -1, 0x34, 0x34, 0x34, 0x34, "ds_wrap_rtn_b32"),
   (  -1,   -1, 0x35, 0x35, 0x55, "ds_add_rtn_f32"),
   (0x36, 0x36, 0x36, 0x36, 0x36, "ds_read_b32"),
   (0x37, 0x37, 0x37, 0x37, 0x37, "ds_read2_b32"),
   (0x38, 0x38, 0x38, 0x38, 0x38, "ds_read2st64_b32"),
   (0x39, 0x39, 0x39, 0x39, 0x39, "ds_read_i8"),
   (0x3a, 0x3a, 0x3a, 0x3a, 0x3a, "ds_read_u8"),
   (0x3b, 0x3b, 0x3b, 0x3b, 0x3b, "ds_read_i16"),
   (0x3c, 0x3c, 0x3c, 0x3c, 0x3c, "ds_read_u16"),
   (0x35, 0x35, 0x3d, 0x3d, 0x35, "ds_swizzle_b32"), #data1 & offset, no addr/data2
   (  -1,   -1, 0x3e, 0x3e, 0xb2, "ds_permute_b32"),
   (  -1,   -1, 0x3f, 0x3f, 0xb3, "ds_bpermute_b32"),
   (0x40, 0x40, 0x40, 0x40, 0x40, "ds_add_u64"),
   (0x41, 0x41, 0x41, 0x41, 0x41, "ds_sub_u64"),
   (0x42, 0x42, 0x42, 0x42, 0x42, "ds_rsub_u64"),
   (0x43, 0x43, 0x43, 0x43, 0x43, "ds_inc_u64"),
   (0x44, 0x44, 0x44, 0x44, 0x44, "ds_dec_u64"),
   (0x45, 0x45, 0x45, 0x45, 0x45, "ds_min_i64"),
   (0x46, 0x46, 0x46, 0x46, 0x46, "ds_max_i64"),
   (0x47, 0x47, 0x47, 0x47, 0x47, "ds_min_u64"),
   (0x48, 0x48, 0x48, 0x48, 0x48, "ds_max_u64"),
   (0x49, 0x49, 0x49, 0x49, 0x49, "ds_and_b64"),
   (0x4a, 0x4a, 0x4a, 0x4a, 0x4a, "ds_or_b64"),
   (0x4b, 0x4b, 0x4b, 0x4b, 0x4b, "ds_xor_b64"),
   (0x4c, 0x4c, 0x4c, 0x4c, 0x4c, "ds_mskor_b64"),
   (0x4d, 0x4d, 0x4d, 0x4d, 0x4d, "ds_write_b64"),
   (0x4e, 0x4e, 0x4e, 0x4e, 0x4e, "ds_write2_b64"),
   (0x4f, 0x4f, 0x4f, 0x4f, 0x4f, "ds_write2st64_b64"),
   (0x50, 0x50, 0x50, 0x50, 0x50, "ds_cmpst_b64"),
   (0x51, 0x51, 0x51, 0x51, 0x51, "ds_cmpst_f64"),
   (0x52, 0x52, 0x52, 0x52, 0x52, "ds_min_f64"),
   (0x53, 0x53, 0x53, 0x53, 0x53, "ds_max_f64"),
   (  -1,   -1, 0x54, 0x54, 0xa0, "ds_write_b8_d16_hi"),
   (  -1,   -1, 0x55, 0x55, 0xa1, "ds_write_b16_d16_hi"),
   (  -1,   -1, 0x56, 0x56, 0xa2, "ds_read_u8_d16"),
   (  -1,   -1, 0x57, 0x57, 0xa3, "ds_read_u8_d16_hi"),
   (  -1,   -1, 0x58, 0x58, 0xa4, "ds_read_i8_d16"),
   (  -1,   -1, 0x59, 0x59, 0xa5, "ds_read_i8_d16_hi"),
   (  -1,   -1, 0x5a, 0x5a, 0xa6, "ds_read_u16_d16"),
   (  -1,   -1, 0x5b, 0x5b, 0xa7, "ds_read_u16_d16_hi"),
   (0x60, 0x60, 0x60, 0x60, 0x60, "ds_add_rtn_u64"),
   (0x61, 0x61, 0x61, 0x61, 0x61, "ds_sub_rtn_u64"),
   (0x62, 0x62, 0x62, 0x62, 0x62, "ds_rsub_rtn_u64"),
   (0x63, 0x63, 0x63, 0x63, 0x63, "ds_inc_rtn_u64"),
   (0x64, 0x64, 0x64, 0x64, 0x64, "ds_dec_rtn_u64"),
   (0x65, 0x65, 0x65, 0x65, 0x65, "ds_min_rtn_i64"),
   (0x66, 0x66, 0x66, 0x66, 0x66, "ds_max_rtn_i64"),
   (0x67, 0x67, 0x67, 0x67, 0x67, "ds_min_rtn_u64"),
   (0x68, 0x68, 0x68, 0x68, 0x68, "ds_max_rtn_u64"),
   (0x69, 0x69, 0x69, 0x69, 0x69, "ds_and_rtn_b64"),
   (0x6a, 0x6a, 0x6a, 0x6a, 0x6a, "ds_or_rtn_b64"),
   (0x6b, 0x6b, 0x6b, 0x6b, 0x6b, "ds_xor_rtn_b64"),
   (0x6c, 0x6c, 0x6c, 0x6c, 0x6c, "ds_mskor_rtn_b64"),
   (0x6d, 0x6d, 0x6d, 0x6d, 0x6d, "ds_wrxchg_rtn_b64"),
   (0x6e, 0x6e, 0x6e, 0x6e, 0x6e, "ds_wrxchg2_rtn_b64"),
   (0x6f, 0x6f, 0x6f, 0x6f, 0x6f, "ds_wrxchg2st64_rtn_b64"),
   (0x70, 0x70, 0x70, 0x70, 0x70, "ds_cmpst_rtn_b64"),
   (0x71, 0x71, 0x71, 0x71, 0x71, "ds_cmpst_rtn_f64"),
   (0x72, 0x72, 0x72, 0x72, 0x72, "ds_min_rtn_f64"),
   (0x73, 0x73, 0x73, 0x73, 0x73, "ds_max_rtn_f64"),
   (0x76, 0x76, 0x76, 0x76, 0x76, "ds_read_b64"),
   (0x77, 0x77, 0x77, 0x77, 0x77, "ds_read2_b64"),
   (0x78, 0x78, 0x78, 0x78, 0x78, "ds_read2st64_b64"),
   (  -1, 0x7e, 0x7e, 0x7e, 0x7e, "ds_condxchg32_rtn_b64"),
   (0x80, 0x80, 0x80, 0x80, 0x80, "ds_add_src2_u32"),
   (0x81, 0x81, 0x81, 0x81, 0x81, "ds_sub_src2_u32"),
   (0x82, 0x82, 0x82, 0x82, 0x82, "ds_rsub_src2_u32"),
   (0x83, 0x83, 0x83, 0x83, 0x83, "ds_inc_src2_u32"),
   (0x84, 0x84, 0x84, 0x84, 0x84, "ds_dec_src2_u32"),
   (0x85, 0x85, 0x85, 0x85, 0x85, "ds_min_src2_i32"),
   (0x86, 0x86, 0x86, 0x86, 0x86, "ds_max_src2_i32"),
   (0x87, 0x87, 0x87, 0x87, 0x87, "ds_min_src2_u32"),
   (0x88, 0x88, 0x88, 0x88, 0x88, "ds_max_src2_u32"),
   (0x89, 0x89, 0x89, 0x89, 0x89, "ds_and_src2_b32"),
   (0x8a, 0x8a, 0x8a, 0x8a, 0x8a, "ds_or_src2_b32"),
   (0x8b, 0x8b, 0x8b, 0x8b, 0x8b, "ds_xor_src2_b32"),
   (0x8d, 0x8d, 0x8d, 0x8d, 0x8d, "ds_write_src2_b32"),
   (0x92, 0x92, 0x92, 0x92, 0x92, "ds_min_src2_f32"),
   (0x93, 0x93, 0x93, 0x93, 0x93, "ds_max_src2_f32"),
   (  -1,   -1, 0x95, 0x95, 0x95, "ds_add_src2_f32"),
   (  -1, 0x18, 0x98, 0x98, 0x18, "ds_gws_sema_release_all"),
   (0x19, 0x19, 0x99, 0x99, 0x19, "ds_gws_init"),
   (0x1a, 0x1a, 0x9a, 0x9a, 0x1a, "ds_gws_sema_v"),
   (0x1b, 0x1b, 0x9b, 0x9b, 0x1b, "ds_gws_sema_br"),
   (0x1c, 0x1c, 0x9c, 0x9c, 0x1c, "ds_gws_sema_p"),
   (0x1d, 0x1d, 0x9d, 0x9d, 0x1d, "ds_gws_barrier"),
   (  -1,   -1, 0xb6, 0xb6, 0xb1, "ds_read_addtid_b32"),
   (0x3d, 0x3d, 0xbd, 0xbd, 0x3d, "ds_consume"),
   (0x3e, 0x3e, 0xbe, 0xbe, 0x3e, "ds_append"),
   (0x3f, 0x3f, 0xbf, 0xbf, 0x3f, "ds_ordered_count"),
   (0xc0, 0xc0, 0xc0, 0xc0, 0xc0, "ds_add_src2_u64"),
   (0xc1, 0xc1, 0xc1, 0xc1, 0xc1, "ds_sub_src2_u64"),
   (0xc2, 0xc2, 0xc2, 0xc2, 0xc2, "ds_rsub_src2_u64"),
   (0xc3, 0xc3, 0xc3, 0xc3, 0xc3, "ds_inc_src2_u64"),
   (0xc4, 0xc4, 0xc4, 0xc4, 0xc4, "ds_dec_src2_u64"),
   (0xc5, 0xc5, 0xc5, 0xc5, 0xc5, "ds_min_src2_i64"),
   (0xc6, 0xc6, 0xc6, 0xc6, 0xc6, "ds_max_src2_i64"),
   (0xc7, 0xc7, 0xc7, 0xc7, 0xc7, "ds_min_src2_u64"),
   (0xc8, 0xc8, 0xc8, 0xc8, 0xc8, "ds_max_src2_u64"),
   (0xc9, 0xc9, 0xc9, 0xc9, 0xc9, "ds_and_src2_b64"),
   (0xca, 0xca, 0xca, 0xca, 0xca, "ds_or_src2_b64"),
   (0xcb, 0xcb, 0xcb, 0xcb, 0xcb, "ds_xor_src2_b64"),
   (0xcd, 0xcd, 0xcd, 0xcd, 0xcd, "ds_write_src2_b64"),
   (0xd2, 0xd2, 0xd2, 0xd2, 0xd2, "ds_min_src2_f64"),
   (0xd3, 0xd3, 0xd3, 0xd3, 0xd3, "ds_max_src2_f64"),
   (  -1, 0xde, 0xde, 0xde, 0xde, "ds_write_b96"),
   (  -1, 0xdf, 0xdf, 0xdf, 0xdf, "ds_write_b128"),
   (  -1, 0xfd, 0xfd,   -1,   -1, "ds_condxchg32_rtn_b128"),
   (  -1, 0xfe, 0xfe, 0xfe, 0xfe, "ds_read_b96"),
   (  -1, 0xff, 0xff, 0xff, 0xff, "ds_read_b128"),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name) in DS:
    opcode(name, gfx7, gfx9, gfx10, Format.DS, InstrClass.DS)

# MUBUF instructions:
MUBUF = {
   (0x00, 0x00, 0x00, 0x00, 0x00, "buffer_load_format_x"),
   (0x01, 0x01, 0x01, 0x01, 0x01, "buffer_load_format_xy"),
   (0x02, 0x02, 0x02, 0x02, 0x02, "buffer_load_format_xyz"),
   (0x03, 0x03, 0x03, 0x03, 0x03, "buffer_load_format_xyzw"),
   (0x04, 0x04, 0x04, 0x04, 0x04, "buffer_store_format_x"),
   (0x05, 0x05, 0x05, 0x05, 0x05, "buffer_store_format_xy"),
   (0x06, 0x06, 0x06, 0x06, 0x06, "buffer_store_format_xyz"),
   (0x07, 0x07, 0x07, 0x07, 0x07, "buffer_store_format_xyzw"),
   (  -1,   -1, 0x08, 0x08, 0x80, "buffer_load_format_d16_x"),
   (  -1,   -1, 0x09, 0x09, 0x81, "buffer_load_format_d16_xy"),
   (  -1,   -1, 0x0a, 0x0a, 0x82, "buffer_load_format_d16_xyz"),
   (  -1,   -1, 0x0b, 0x0b, 0x83, "buffer_load_format_d16_xyzw"),
   (  -1,   -1, 0x0c, 0x0c, 0x84, "buffer_store_format_d16_x"),
   (  -1,   -1, 0x0d, 0x0d, 0x85, "buffer_store_format_d16_xy"),
   (  -1,   -1, 0x0e, 0x0e, 0x86, "buffer_store_format_d16_xyz"),
   (  -1,   -1, 0x0f, 0x0f, 0x87, "buffer_store_format_d16_xyzw"),
   (0x08, 0x08, 0x10, 0x10, 0x08, "buffer_load_ubyte"),
   (0x09, 0x09, 0x11, 0x11, 0x09, "buffer_load_sbyte"),
   (0x0a, 0x0a, 0x12, 0x12, 0x0a, "buffer_load_ushort"),
   (0x0b, 0x0b, 0x13, 0x13, 0x0b, "buffer_load_sshort"),
   (0x0c, 0x0c, 0x14, 0x14, 0x0c, "buffer_load_dword"),
   (0x0d, 0x0d, 0x15, 0x15, 0x0d, "buffer_load_dwordx2"),
   (  -1, 0x0f, 0x16, 0x16, 0x0f, "buffer_load_dwordx3"),
   (0x0f, 0x0e, 0x17, 0x17, 0x0e, "buffer_load_dwordx4"),
   (0x18, 0x18, 0x18, 0x18, 0x18, "buffer_store_byte"),
   (  -1,   -1,   -1, 0x19, 0x19, "buffer_store_byte_d16_hi"),
   (0x1a, 0x1a, 0x1a, 0x1a, 0x1a, "buffer_store_short"),
   (  -1,   -1,   -1, 0x1b, 0x1b, "buffer_store_short_d16_hi"),
   (0x1c, 0x1c, 0x1c, 0x1c, 0x1c, "buffer_store_dword"),
   (0x1d, 0x1d, 0x1d, 0x1d, 0x1d, "buffer_store_dwordx2"),
   (  -1, 0x1f, 0x1e, 0x1e, 0x1f, "buffer_store_dwordx3"),
   (0x1e, 0x1e, 0x1f, 0x1f, 0x1e, "buffer_store_dwordx4"),
   (  -1,   -1,   -1, 0x20, 0x20, "buffer_load_ubyte_d16"),
   (  -1,   -1,   -1, 0x21, 0x21, "buffer_load_ubyte_d16_hi"),
   (  -1,   -1,   -1, 0x22, 0x22, "buffer_load_sbyte_d16"),
   (  -1,   -1,   -1, 0x23, 0x23, "buffer_load_sbyte_d16_hi"),
   (  -1,   -1,   -1, 0x24, 0x24, "buffer_load_short_d16"),
   (  -1,   -1,   -1, 0x25, 0x25, "buffer_load_short_d16_hi"),
   (  -1,   -1,   -1, 0x26, 0x26, "buffer_load_format_d16_hi_x"),
   (  -1,   -1,   -1, 0x27, 0x27, "buffer_store_format_d16_hi_x"),
   (  -1,   -1, 0x3d, 0x3d,   -1, "buffer_store_lds_dword"),
   (0x71, 0x71, 0x3e, 0x3e,   -1, "buffer_wbinvl1"),
   (0x70, 0x70, 0x3f, 0x3f,   -1, "buffer_wbinvl1_vol"),
   (0x30, 0x30, 0x40, 0x40, 0x30, "buffer_atomic_swap"),
   (0x31, 0x31, 0x41, 0x41, 0x31, "buffer_atomic_cmpswap"),
   (0x32, 0x32, 0x42, 0x42, 0x32, "buffer_atomic_add"),
   (0x33, 0x33, 0x43, 0x43, 0x33, "buffer_atomic_sub"),
   (0x34,   -1,   -1,   -1,   -1, "buffer_atomic_rsub"),
   (0x35, 0x35, 0x44, 0x44, 0x35, "buffer_atomic_smin"),
   (0x36, 0x36, 0x45, 0x45, 0x36, "buffer_atomic_umin"),
   (0x37, 0x37, 0x46, 0x46, 0x37, "buffer_atomic_smax"),
   (0x38, 0x38, 0x47, 0x47, 0x38, "buffer_atomic_umax"),
   (0x39, 0x39, 0x48, 0x48, 0x39, "buffer_atomic_and"),
   (0x3a, 0x3a, 0x49, 0x49, 0x3a, "buffer_atomic_or"),
   (0x3b, 0x3b, 0x4a, 0x4a, 0x3b, "buffer_atomic_xor"),
   (0x3c, 0x3c, 0x4b, 0x4b, 0x3c, "buffer_atomic_inc"),
   (0x3d, 0x3d, 0x4c, 0x4c, 0x3d, "buffer_atomic_dec"),
   (0x3e, 0x3e,   -1,   -1, 0x3e, "buffer_atomic_fcmpswap"),
   (0x3f, 0x3f,   -1,   -1, 0x3f, "buffer_atomic_fmin"),
   (0x40, 0x40,   -1,   -1, 0x40, "buffer_atomic_fmax"),
   (0x50, 0x50, 0x60, 0x60, 0x50, "buffer_atomic_swap_x2"),
   (0x51, 0x51, 0x61, 0x61, 0x51, "buffer_atomic_cmpswap_x2"),
   (0x52, 0x52, 0x62, 0x62, 0x52, "buffer_atomic_add_x2"),
   (0x53, 0x53, 0x63, 0x63, 0x53, "buffer_atomic_sub_x2"),
   (0x54,   -1,   -1,   -1,   -1, "buffer_atomic_rsub_x2"),
   (0x55, 0x55, 0x64, 0x64, 0x55, "buffer_atomic_smin_x2"),
   (0x56, 0x56, 0x65, 0x65, 0x56, "buffer_atomic_umin_x2"),
   (0x57, 0x57, 0x66, 0x66, 0x57, "buffer_atomic_smax_x2"),
   (0x58, 0x58, 0x67, 0x67, 0x58, "buffer_atomic_umax_x2"),
   (0x59, 0x59, 0x68, 0x68, 0x59, "buffer_atomic_and_x2"),
   (0x5a, 0x5a, 0x69, 0x69, 0x5a, "buffer_atomic_or_x2"),
   (0x5b, 0x5b, 0x6a, 0x6a, 0x5b, "buffer_atomic_xor_x2"),
   (0x5c, 0x5c, 0x6b, 0x6b, 0x5c, "buffer_atomic_inc_x2"),
   (0x5d, 0x5d, 0x6c, 0x6c, 0x5d, "buffer_atomic_dec_x2"),
   (0x5e, 0x5e,   -1,   -1, 0x5e, "buffer_atomic_fcmpswap_x2"),
   (0x5f, 0x5f,   -1,   -1, 0x5f, "buffer_atomic_fmin_x2"),
   (0x60, 0x60,   -1,   -1, 0x60, "buffer_atomic_fmax_x2"),
   (  -1,   -1,   -1,   -1, 0x71, "buffer_gl0_inv"),
   (  -1,   -1,   -1,   -1, 0x72, "buffer_gl1_inv"),
   (  -1,   -1,   -1,   -1, 0x34, "buffer_atomic_csub"), #GFX10.3+. seems glc must be set
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name) in MUBUF:
    opcode(name, gfx7, gfx9, gfx10, Format.MUBUF, InstrClass.VMem, is_atomic = "atomic" in name)

MTBUF = {
   (0x00, 0x00, 0x00, 0x00, 0x00, "tbuffer_load_format_x"),
   (0x01, 0x01, 0x01, 0x01, 0x01, "tbuffer_load_format_xy"),
   (0x02, 0x02, 0x02, 0x02, 0x02, "tbuffer_load_format_xyz"),
   (0x03, 0x03, 0x03, 0x03, 0x03, "tbuffer_load_format_xyzw"),
   (0x04, 0x04, 0x04, 0x04, 0x04, "tbuffer_store_format_x"),
   (0x05, 0x05, 0x05, 0x05, 0x05, "tbuffer_store_format_xy"),
   (0x06, 0x06, 0x06, 0x06, 0x06, "tbuffer_store_format_xyz"),
   (0x07, 0x07, 0x07, 0x07, 0x07, "tbuffer_store_format_xyzw"),
   (  -1,   -1, 0x08, 0x08, 0x08, "tbuffer_load_format_d16_x"),
   (  -1,   -1, 0x09, 0x09, 0x09, "tbuffer_load_format_d16_xy"),
   (  -1,   -1, 0x0a, 0x0a, 0x0a, "tbuffer_load_format_d16_xyz"),
   (  -1,   -1, 0x0b, 0x0b, 0x0b, "tbuffer_load_format_d16_xyzw"),
   (  -1,   -1, 0x0c, 0x0c, 0x0c, "tbuffer_store_format_d16_x"),
   (  -1,   -1, 0x0d, 0x0d, 0x0d, "tbuffer_store_format_d16_xy"),
   (  -1,   -1, 0x0e, 0x0e, 0x0e, "tbuffer_store_format_d16_xyz"),
   (  -1,   -1, 0x0f, 0x0f, 0x0f, "tbuffer_store_format_d16_xyzw"),
}
for (gfx6, gfx7, gfx8, gfx9, gfx10, name) in MTBUF:
    opcode(name, gfx7, gfx9, gfx10, Format.MTBUF, InstrClass.VMem)


IMAGE = {
   (0x00, "image_load"),
   (0x01, "image_load_mip"),
   (0x02, "image_load_pck"),
   (0x03, "image_load_pck_sgn"),
   (0x04, "image_load_mip_pck"),
   (0x05, "image_load_mip_pck_sgn"),
   (0x08, "image_store"),
   (0x09, "image_store_mip"),
   (0x0a, "image_store_pck"),
   (0x0b, "image_store_mip_pck"),
   (0x0e, "image_get_resinfo"),
   (0x60, "image_get_lod"),
}
# (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (code, code, code, code, code, name)
for (code, name) in IMAGE:
   opcode(name, code, code, code, Format.MIMG, InstrClass.VMem)

opcode("image_msaa_load", -1, -1, 0x80, Format.MIMG, InstrClass.VMem) #GFX10.3+

IMAGE_ATOMIC = {
   (0x0f, 0x0f, 0x10, "image_atomic_swap"),
   (0x10, 0x10, 0x11, "image_atomic_cmpswap"),
   (0x11, 0x11, 0x12, "image_atomic_add"),
   (0x12, 0x12, 0x13, "image_atomic_sub"),
   (0x13,   -1,   -1, "image_atomic_rsub"),
   (0x14, 0x14, 0x14, "image_atomic_smin"),
   (0x15, 0x15, 0x15, "image_atomic_umin"),
   (0x16, 0x16, 0x16, "image_atomic_smax"),
   (0x17, 0x17, 0x17, "image_atomic_umax"),
   (0x18, 0x18, 0x18, "image_atomic_and"),
   (0x19, 0x19, 0x19, "image_atomic_or"),
   (0x1a, 0x1a, 0x1a, "image_atomic_xor"),
   (0x1b, 0x1b, 0x1b, "image_atomic_inc"),
   (0x1c, 0x1c, 0x1c, "image_atomic_dec"),
   (0x1d, 0x1d,   -1, "image_atomic_fcmpswap"),
   (0x1e, 0x1e,   -1, "image_atomic_fmin"),
   (0x1f, 0x1f,   -1, "image_atomic_fmax"),
}
# (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (gfx6, gfx7, gfx89, gfx89, ???, name)
# gfx7 and gfx10 opcodes are the same here
for (gfx6, gfx7, gfx89, name) in IMAGE_ATOMIC:
   opcode(name, gfx7, gfx89, gfx7, Format.MIMG, InstrClass.VMem, is_atomic = True)

IMAGE_SAMPLE = {
   (0x20, "image_sample"),
   (0x21, "image_sample_cl"),
   (0x22, "image_sample_d"),
   (0x23, "image_sample_d_cl"),
   (0x24, "image_sample_l"),
   (0x25, "image_sample_b"),
   (0x26, "image_sample_b_cl"),
   (0x27, "image_sample_lz"),
   (0x28, "image_sample_c"),
   (0x29, "image_sample_c_cl"),
   (0x2a, "image_sample_c_d"),
   (0x2b, "image_sample_c_d_cl"),
   (0x2c, "image_sample_c_l"),
   (0x2d, "image_sample_c_b"),
   (0x2e, "image_sample_c_b_cl"),
   (0x2f, "image_sample_c_lz"),
   (0x30, "image_sample_o"),
   (0x31, "image_sample_cl_o"),
   (0x32, "image_sample_d_o"),
   (0x33, "image_sample_d_cl_o"),
   (0x34, "image_sample_l_o"),
   (0x35, "image_sample_b_o"),
   (0x36, "image_sample_b_cl_o"),
   (0x37, "image_sample_lz_o"),
   (0x38, "image_sample_c_o"),
   (0x39, "image_sample_c_cl_o"),
   (0x3a, "image_sample_c_d_o"),
   (0x3b, "image_sample_c_d_cl_o"),
   (0x3c, "image_sample_c_l_o"),
   (0x3d, "image_sample_c_b_o"),
   (0x3e, "image_sample_c_b_cl_o"),
   (0x3f, "image_sample_c_lz_o"),
   (0x68, "image_sample_cd"),
   (0x69, "image_sample_cd_cl"),
   (0x6a, "image_sample_c_cd"),
   (0x6b, "image_sample_c_cd_cl"),
   (0x6c, "image_sample_cd_o"),
   (0x6d, "image_sample_cd_cl_o"),
   (0x6e, "image_sample_c_cd_o"),
   (0x6f, "image_sample_c_cd_cl_o"),
}
# (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (code, code, code, code, code, name)
for (code, name) in IMAGE_SAMPLE:
   opcode(name, code, code, code, Format.MIMG, InstrClass.VMem)

IMAGE_GATHER4 = {
   (0x40, "image_gather4"),
   (0x41, "image_gather4_cl"),
   #(0x42, "image_gather4h"), VEGA only?
   (0x44, "image_gather4_l"), # following instructions have different opcodes according to ISA sheet.
   (0x45, "image_gather4_b"),
   (0x46, "image_gather4_b_cl"),
   (0x47, "image_gather4_lz"),
   (0x48, "image_gather4_c"),
   (0x49, "image_gather4_c_cl"), # previous instructions have different opcodes according to ISA sheet.
   #(0x4a, "image_gather4h_pck"), VEGA only?
   #(0x4b, "image_gather8h_pck"), VGEA only?
   (0x4c, "image_gather4_c_l"),
   (0x4d, "image_gather4_c_b"),
   (0x4e, "image_gather4_c_b_cl"),
   (0x4f, "image_gather4_c_lz"),
   (0x50, "image_gather4_o"),
   (0x51, "image_gather4_cl_o"),
   (0x54, "image_gather4_l_o"),
   (0x55, "image_gather4_b_o"),
   (0x56, "image_gather4_b_cl_o"),
   (0x57, "image_gather4_lz_o"),
   (0x58, "image_gather4_c_o"),
   (0x59, "image_gather4_c_cl_o"),
   (0x5c, "image_gather4_c_l_o"),
   (0x5d, "image_gather4_c_b_o"),
   (0x5e, "image_gather4_c_b_cl_o"),
   (0x5f, "image_gather4_c_lz_o"),
}
# (gfx6, gfx7, gfx8, gfx9, gfx10, name) = (code, code, code, code, code, name)
for (code, name) in IMAGE_GATHER4:
   opcode(name, code, code, code, Format.MIMG, InstrClass.VMem)


FLAT = {
   #GFX7, GFX8_9, GFX10
   (0x08, 0x10, 0x08, "flat_load_ubyte"),
   (0x09, 0x11, 0x09, "flat_load_sbyte"),
   (0x0a, 0x12, 0x0a, "flat_load_ushort"),
   (0x0b, 0x13, 0x0b, "flat_load_sshort"),
   (0x0c, 0x14, 0x0c, "flat_load_dword"),
   (0x0d, 0x15, 0x0d, "flat_load_dwordx2"),
   (0x0f, 0x16, 0x0f, "flat_load_dwordx3"),
   (0x0e, 0x17, 0x0e, "flat_load_dwordx4"),
   (0x18, 0x18, 0x18, "flat_store_byte"),
   (  -1, 0x19, 0x19, "flat_store_byte_d16_hi"),
   (0x1a, 0x1a, 0x1a, "flat_store_short"),
   (  -1, 0x1b, 0x1b, "flat_store_short_d16_hi"),
   (0x1c, 0x1c, 0x1c, "flat_store_dword"),
   (0x1d, 0x1d, 0x1d, "flat_store_dwordx2"),
   (0x1f, 0x1e, 0x1f, "flat_store_dwordx3"),
   (0x1e, 0x1f, 0x1e, "flat_store_dwordx4"),
   (  -1, 0x20, 0x20, "flat_load_ubyte_d16"),
   (  -1, 0x21, 0x21, "flat_load_ubyte_d16_hi"),
   (  -1, 0x22, 0x22, "flat_load_sbyte_d16"),
   (  -1, 0x23, 0x23, "flat_load_sbyte_d16_hi"),
   (  -1, 0x24, 0x24, "flat_load_short_d16"),
   (  -1, 0x25, 0x25, "flat_load_short_d16_hi"),
   (0x30, 0x40, 0x30, "flat_atomic_swap"),
   (0x31, 0x41, 0x31, "flat_atomic_cmpswap"),
   (0x32, 0x42, 0x32, "flat_atomic_add"),
   (0x33, 0x43, 0x33, "flat_atomic_sub"),
   (0x35, 0x44, 0x35, "flat_atomic_smin"),
   (0x36, 0x45, 0x36, "flat_atomic_umin"),
   (0x37, 0x46, 0x37, "flat_atomic_smax"),
   (0x38, 0x47, 0x38, "flat_atomic_umax"),
   (0x39, 0x48, 0x39, "flat_atomic_and"),
   (0x3a, 0x49, 0x3a, "flat_atomic_or"),
   (0x3b, 0x4a, 0x3b, "flat_atomic_xor"),
   (0x3c, 0x4b, 0x3c, "flat_atomic_inc"),
   (0x3d, 0x4c, 0x3d, "flat_atomic_dec"),
   (0x3e,   -1, 0x3e, "flat_atomic_fcmpswap"),
   (0x3f,   -1, 0x3f, "flat_atomic_fmin"),
   (0x40,   -1, 0x40, "flat_atomic_fmax"),
   (0x50, 0x60, 0x50, "flat_atomic_swap_x2"),
   (0x51, 0x61, 0x51, "flat_atomic_cmpswap_x2"),
   (0x52, 0x62, 0x52, "flat_atomic_add_x2"),
   (0x53, 0x63, 0x53, "flat_atomic_sub_x2"),
   (0x55, 0x64, 0x55, "flat_atomic_smin_x2"),
   (0x56, 0x65, 0x56, "flat_atomic_umin_x2"),
   (0x57, 0x66, 0x57, "flat_atomic_smax_x2"),
   (0x58, 0x67, 0x58, "flat_atomic_umax_x2"),
   (0x59, 0x68, 0x59, "flat_atomic_and_x2"),
   (0x5a, 0x69, 0x5a, "flat_atomic_or_x2"),
   (0x5b, 0x6a, 0x5b, "flat_atomic_xor_x2"),
   (0x5c, 0x6b, 0x5c, "flat_atomic_inc_x2"),
   (0x5d, 0x6c, 0x5d, "flat_atomic_dec_x2"),
   (0x5e,   -1, 0x5e, "flat_atomic_fcmpswap_x2"),
   (0x5f,   -1, 0x5f, "flat_atomic_fmin_x2"),
   (0x60,   -1, 0x60, "flat_atomic_fmax_x2"),
}
for (gfx7, gfx8, gfx10, name) in FLAT:
    opcode(name, gfx7, gfx8, gfx10, Format.FLAT, InstrClass.VMem, is_atomic = "atomic" in name) #TODO: also LDS?

GLOBAL = {
   #GFX8_9, GFX10
   (0x10, 0x08, "global_load_ubyte"),
   (0x11, 0x09, "global_load_sbyte"),
   (0x12, 0x0a, "global_load_ushort"),
   (0x13, 0x0b, "global_load_sshort"),
   (0x14, 0x0c, "global_load_dword"),
   (0x15, 0x0d, "global_load_dwordx2"),
   (0x16, 0x0f, "global_load_dwordx3"),
   (0x17, 0x0e, "global_load_dwordx4"),
   (0x18, 0x18, "global_store_byte"),
   (0x19, 0x19, "global_store_byte_d16_hi"),
   (0x1a, 0x1a, "global_store_short"),
   (0x1b, 0x1b, "global_store_short_d16_hi"),
   (0x1c, 0x1c, "global_store_dword"),
   (0x1d, 0x1d, "global_store_dwordx2"),
   (0x1e, 0x1f, "global_store_dwordx3"),
   (0x1f, 0x1e, "global_store_dwordx4"),
   (0x20, 0x20, "global_load_ubyte_d16"),
   (0x21, 0x21, "global_load_ubyte_d16_hi"),
   (0x22, 0x22, "global_load_sbyte_d16"),
   (0x23, 0x23, "global_load_sbyte_d16_hi"),
   (0x24, 0x24, "global_load_short_d16"),
   (0x25, 0x25, "global_load_short_d16_hi"),
   (0x40, 0x30, "global_atomic_swap"),
   (0x41, 0x31, "global_atomic_cmpswap"),
   (0x42, 0x32, "global_atomic_add"),
   (0x43, 0x33, "global_atomic_sub"),
   (0x44, 0x35, "global_atomic_smin"),
   (0x45, 0x36, "global_atomic_umin"),
   (0x46, 0x37, "global_atomic_smax"),
   (0x47, 0x38, "global_atomic_umax"),
   (0x48, 0x39, "global_atomic_and"),
   (0x49, 0x3a, "global_atomic_or"),
   (0x4a, 0x3b, "global_atomic_xor"),
   (0x4b, 0x3c, "global_atomic_inc"),
   (0x4c, 0x3d, "global_atomic_dec"),
   (  -1, 0x3e, "global_atomic_fcmpswap"),
   (  -1, 0x3f, "global_atomic_fmin"),
   (  -1, 0x40, "global_atomic_fmax"),
   (0x60, 0x50, "global_atomic_swap_x2"),
   (0x61, 0x51, "global_atomic_cmpswap_x2"),
   (0x62, 0x52, "global_atomic_add_x2"),
   (0x63, 0x53, "global_atomic_sub_x2"),
   (0x64, 0x55, "global_atomic_smin_x2"),
   (0x65, 0x56, "global_atomic_umin_x2"),
   (0x66, 0x57, "global_atomic_smax_x2"),
   (0x67, 0x58, "global_atomic_umax_x2"),
   (0x68, 0x59, "global_atomic_and_x2"),
   (0x69, 0x5a, "global_atomic_or_x2"),
   (0x6a, 0x5b, "global_atomic_xor_x2"),
   (0x6b, 0x5c, "global_atomic_inc_x2"),
   (0x6c, 0x5d, "global_atomic_dec_x2"),
   (  -1, 0x5e, "global_atomic_fcmpswap_x2"),
   (  -1, 0x5f, "global_atomic_fmin_x2"),
   (  -1, 0x60, "global_atomic_fmax_x2"),
   (  -1, 0x16, "global_load_dword_addtid"), #GFX10.3+
   (  -1, 0x17, "global_store_dword_addtid"), #GFX10.3+
   (  -1, 0x34, "global_atomic_csub"), #GFX10.3+. seems glc must be set
}
for (gfx8, gfx10, name) in GLOBAL:
    opcode(name, -1, gfx8, gfx10, Format.GLOBAL, InstrClass.VMem, is_atomic = "atomic" in name)

SCRATCH = {
   #GFX8_9, GFX10
   (0x10, 0x08, "scratch_load_ubyte"),
   (0x11, 0x09, "scratch_load_sbyte"),
   (0x12, 0x0a, "scratch_load_ushort"),
   (0x13, 0x0b, "scratch_load_sshort"),
   (0x14, 0x0c, "scratch_load_dword"),
   (0x15, 0x0d, "scratch_load_dwordx2"),
   (0x16, 0x0f, "scratch_load_dwordx3"),
   (0x17, 0x0e, "scratch_load_dwordx4"),
   (0x18, 0x18, "scratch_store_byte"),
   (0x19, 0x19, "scratch_store_byte_d16_hi"),
   (0x1a, 0x1a, "scratch_store_short"),
   (0x1b, 0x1b, "scratch_store_short_d16_hi"),
   (0x1c, 0x1c, "scratch_store_dword"),
   (0x1d, 0x1d, "scratch_store_dwordx2"),
   (0x1e, 0x1f, "scratch_store_dwordx3"),
   (0x1f, 0x1e, "scratch_store_dwordx4"),
   (0x20, 0x20, "scratch_load_ubyte_d16"),
   (0x21, 0x21, "scratch_load_ubyte_d16_hi"),
   (0x22, 0x22, "scratch_load_sbyte_d16"),
   (0x23, 0x23, "scratch_load_sbyte_d16_hi"),
   (0x24, 0x24, "scratch_load_short_d16"),
   (0x25, 0x25, "scratch_load_short_d16_hi"),
}
for (gfx8, gfx10, name) in SCRATCH:
    opcode(name, -1, gfx8, gfx10, Format.SCRATCH, InstrClass.VMem)

# check for duplicate opcode numbers
for ver in ['gfx9', 'gfx10']:
    op_to_name = {}
    for op in opcodes.values():
        if op.format in [Format.PSEUDO, Format.PSEUDO_BRANCH, Format.PSEUDO_BARRIER, Format.PSEUDO_REDUCTION]:
            continue

        num = getattr(op, 'opcode_' + ver)
        if num == -1:
            continue

        key = (op.format, num)

        if key in op_to_name:
            # exceptions
            names = set([op_to_name[key], op.name])
            if ver in ['gfx8', 'gfx9'] and names == set(['v_mul_lo_i32', 'v_mul_lo_u32']):
                continue
            # v_mad_legacy_f32 is replaced with v_fma_legacy_f32 on GFX10.3
            if ver == 'gfx10' and names == set(['v_mad_legacy_f32', 'v_fma_legacy_f32']):
                continue

            print('%s and %s share the same opcode number (%s)' % (op_to_name[key], op.name, ver))
            sys.exit(1)
        else:
            op_to_name[key] = op.name
