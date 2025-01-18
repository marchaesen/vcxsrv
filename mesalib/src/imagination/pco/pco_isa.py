# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from pco_pygen_common import *

OP_PHASE = enum_type('op_phase', [
   ('0', 0, 'p0'),
   ('ctrl', 0),
   ('1', 1, 'p1'),
   ('2_pck', 2, 'p2_pck'),
   ('2', 2, 'p2'),
   ('2_tst', 3, 'p2_tst'),
   ('2_mov', 4, 'p2_mov'),
   ('backend', 5),
])

SR = enum_type('sr', [
   ('pixout0', 32),
   ('pixout1', 33),
   ('pixout2', 34),
   ('pixout3', 35),

   ('intl0', 36),
   ('intl1', 37),
   ('intl2', 38),
   ('intl3', 39),
   ('intl4', 40),
   ('intl5', 41),
   ('intl6', 42),
   ('intl7', 43),

   ('slot7', 36),
   ('slot6', 37),
   ('slot5', 38),
   ('slot4', 39),
   ('slot3', 40),
   ('slot2', 41),
   ('slot1', 42),
   ('slot0', 43),

   ('face_orient', 44),
   ('back_face', 44),
   ('cluster_num', 45),
   ('output_part', 46),
   ('task_id', 47),
   ('slot_num', 48),
   ('tile_x_pix', 49),
   ('tile_y_pix', 50),
   ('inst_num', 51),
   ('dm_task_type', 52),
   ('samp_num', 53),

   ('tiled_ld_comp0', 54),
   ('tiled_ld_comp1', 55),
   ('tiled_ld_comp2', 56),
   ('tiled_ld_comp3', 57),

   ('tiled_st_comp0', 58),
   ('tiled_st_comp1', 59),
   ('tiled_st_comp2', 60),
   ('tiled_st_comp3', 61),

   ('batch_num', 62),
   ('inst_valid', 63),

   ('tile_xy', 96),

   ('x_p', 97),
   ('x_s', 98),

   ('y_p', 100),
   ('y_s', 101),

   ('wg_id', 102),

   ('sh_alloc_size', 103),

   ('global0', 104),
   ('global1', 105),
   ('global2', 106),
   ('global3', 107),
   ('global4', 108),
   ('global5', 109),
   ('global6', 110),
   ('global7', 111),

   ('local_addr_inst_num', 112),

   ('tile_x_p', 114),
   ('tile_x_s', 115),
   ('tile_y_p', 116),
   ('tile_y_s', 117),

   ('render_tgt_id', 118),

   ('tiled_ld_comp4', 119),
   ('tiled_ld_comp5', 120),
   ('tiled_ld_comp6', 121),
   ('tiled_ld_comp7', 122),

   ('tiled_st_comp4', 123),
   ('tiled_st_comp5', 124),
   ('tiled_st_comp6', 125),
   ('tiled_st_comp7', 126),

   ('gpu_offset', 127),

   ('timer_80ns', 160),

   ('pixout4', 164),
   ('pixout5', 165),
   ('pixout6', 166),
   ('pixout7', 167),
])

# Common field types.

## Basic types.
F_BOOL = field_type('bool', BaseType.bool, 1)
F_UINT1 = field_type('uint1', BaseType.uint, 1)
F_UINT2 = field_type('uint2', BaseType.uint, 2)
F_UINT3 = field_type('uint3', BaseType.uint, 3)
F_UINT4 = field_type('uint4', BaseType.uint, 4)
F_UINT5 = field_type('uint5', BaseType.uint, 5)
F_UINT6 = field_type('uint6', BaseType.uint, 6)
F_UINT7 = field_type('uint7', BaseType.uint, 7)
F_UINT8 = field_type('uint8', BaseType.uint, 8)
F_UINT11 = field_type('uint11', BaseType.uint, 11)
F_UINT16 = field_type('uint16', BaseType.uint, 16)
F_UINT32 = field_type('uint32', BaseType.uint, 32)

## [1..4] -> [0..3]
F_UINT2_POS_INC = field_type('uint2_pos_inc', BaseType.uint, 2, dec_bits=3, check='{0} >= 1 && {0} <= 4', encode='{} - 1')
## [1..16] -> [1..15,0]
F_UINT4_POS_WRAP = field_type('uint4_pos_wrap', BaseType.uint, 4, dec_bits=5, check='{0} >= 1 && {0} <= 16', encode='{} % 16')
## 32-bit offset; lowest bit == 0b0.
F_OFFSET31 = field_type('offset31', BaseType.uint, 31, dec_bits=32, check='!({} & 0b1)', encode='{} >> 1')
## 8-bit value; lowest 2 bits == 0b00.
F_UINT6MUL4 = field_type('uint6mul4', BaseType.uint, 6, dec_bits=8, check='!({} & 0b11)', encode='{} >> 2')

# Instruction group header definitions.
F_OPORG = field_enum_type(
name='oporg', num_bits=3,
elems=[
   ('p0', 0b000),
   ('p2', 0b001),
   ('be', 0b010),
   ('p0_p1', 0b011),
   ('p0_p2', 0b100),
   ('p0_p1_p2', 0b101),
   ('p0_p2_be', 0b110),
   ('p0_p1_p2_be', 0b111),
])

F_OPCNT = field_enum_type(
name='opcnt', num_bits=3,
elems=[
   ('p0', 0b001),
   ('p1', 0b010),
   ('p2', 0b100),
], is_bitset=True)

F_CC = field_enum_type(
name='cc', num_bits=2,
elems=[
   ('e1_zx', 0b00, ''),
   ('e1_z1', 0b01, 'if(p0)'),
   ('ex_zx', 0b10, '(ignorepe)'),
   ('e1_z0', 0b11, 'if(!p0)'),
])

F_CC1 = field_enum_subtype(name='cc1', parent=F_CC, num_bits=1)

F_ALUTYPE = field_enum_type(
name='alutype', num_bits=2,
elems=[
   ('main', 0b00),
   ('bitwise', 0b10),
   ('control', 0b11),
])

F_CTRLOP = field_enum_type(
name='ctrlop', num_bits=4,
elems=[
   ('b', 0b0000),
   ('lapc', 0b0001),
   ('savl', 0b0010),
   ('cnd', 0b0011),
   ('wop', 0b0100),
   ('wdf', 0b0101),
   ('mutex', 0b0110),
   ('nop', 0b0111),
   ('itrsmp', 0b1000),
   ('sbo', 0b1011),
   ('ditr', 0b1100),
])

I_IGRP_HDR = bit_set(
name='igrp_hdr',
pieces=[
   ('da', (0, '7:4')),
   ('length', (0, '3:0')),
   ('ext', (1, '7')),
   ('oporg', (1, '6:4')),
   ('opcnt', (1, '6:4')),
   ('olchk', (1, '3')),
   ('w1p', (1, '2')),
   ('w0p', (1, '1')),
   ('cc', (1, '0')),

   ('end', (2, '7')),
   ('alutype', (2, '6:5')),
   ('rsvd', (2, '4')),
   ('atom', (2, '3')),
   ('rpt', (2, '2:1')),
   ('ccext', (2, '0')),

   ('miscctl', (2, '7')),
   ('ctrlop', (2, '4:1')),
],
fields=[
   ('da', (F_UINT4, ['da'])),
   ('length', (F_UINT4_POS_WRAP, ['length'])),
   ('ext', (F_BOOL, ['ext'])),
   ('oporg', (F_OPORG, ['oporg'])),
   ('opcnt', (F_OPCNT, ['opcnt'])),
   ('olchk', (F_BOOL, ['olchk'])),
   ('w1p', (F_BOOL, ['w1p'])),
   ('w0p', (F_BOOL, ['w0p'])),
   ('cc1', (F_CC1, ['cc'])),

   ('end', (F_BOOL, ['end'])),
   ('alutype', (F_ALUTYPE, ['alutype'])),
   ('rsvd', (F_UINT1, ['rsvd'], 0)),
   ('atom', (F_BOOL, ['atom'])),
   ('rpt', (F_UINT2_POS_INC, ['rpt'])),
   ('cc', (F_CC, ['ccext', 'cc'])),

   ('miscctl', (F_UINT1, ['miscctl'])),
   ('ctrlop', (F_CTRLOP, ['ctrlop'])),
])

I_IGRP_HDR_MAIN_BRIEF = bit_struct(
name='main_brief',
bit_set=I_IGRP_HDR,
field_mappings=[
   'da',
   'length',
   ('ext', 'ext', False),
   'oporg',
   'olchk',
   'w1p',
   'w0p',
   ('cc', 'cc1'),
])

I_IGRP_HDR_MAIN = bit_struct(
name='main',
bit_set=I_IGRP_HDR,
field_mappings=[
   'da',
   'length',
   ('ext', 'ext', True),
   'oporg',
   'olchk',
   'w1p',
   'w0p',
   'cc',
   'end',
   ('alutype', 'alutype', 'main'),
   'rsvd',
   'atom',
   'rpt',
])

I_IGRP_HDR_BITWISE = bit_struct(
name='bitwise',
bit_set=I_IGRP_HDR,
field_mappings=[
   'da',
   'length',
   ('ext', 'ext', True),
   'opcnt',
   'olchk',
   'w1p',
   'w0p',
   'cc',
   'end',
   ('alutype', 'alutype', 'bitwise'),
   'rsvd',
   'atom',
   'rpt',
])

I_IGRP_HDR_CONTROL = bit_struct(
name='control',
bit_set=I_IGRP_HDR,
field_mappings=[
   'da',
   'length',
   ('ext', 'ext', True),
   ('opcnt', 'opcnt', 0),
   'olchk',
   'w1p',
   'w0p',
   'cc',
   'miscctl',
   ('alutype', 'alutype', 'control'),
   'ctrlop',
])

# Upper/lower source definitions.
F_IS0_SEL = field_enum_type(
name='is0_sel', num_bits=3,
elems=[
   ('s0', 0b000),
   ('s3', 0b001),
   ('s4', 0b010),
   ('s5', 0b011),
   ('s1', 0b100),
   ('s2', 0b101),
])

F_IS0_SEL2 = field_enum_subtype(name='is0_sel2', parent=F_IS0_SEL, num_bits=2)

F_REGBANK = field_enum_type(
name='regbank', num_bits=3,
elems=[
   ('special', 0b000),
   ('temp', 0b001),
   ('vtxin', 0b010),
   ('coeff', 0b011),
   ('shared', 0b100),
   ('coeff_alt', 0b101),
   ('idx0', 0b110),
   ('idx1', 0b111),
])

F_IDXBANK = field_enum_type(
name='idxbank', num_bits=3,
elems=[
   ('temp', 0b000),
   ('vtxin', 0b001),
   ('coeff', 0b010),
   ('shared', 0b011),
   ('idx', 0b101),
   ('coeff_alt', 0b110),
   ('pixout', 0b111),
])

F_REGBANK2 = field_enum_subtype(name='regbank2', parent=F_REGBANK, num_bits=2)
F_REGBANK1 = field_enum_subtype(name='regbank1', parent=F_REGBANK, num_bits=1)

I_SRC = bit_set(
name='src',
pieces=[
   ('ext0', (0, '7')),
   ('sbA_0_b0', (0, '6')),
   ('sA_5_0_b0', (0, '5:0')),

   ('sel', (1, '7')),
   ('ext1', (1, '6')),

   ('mux_1_0_b1', (1, '5:4')),
   ('sbA_2_1_b1', (1, '3:2')),
   ('sA_7_6_b1', (1, '1:0')),

   ('sbB_0_b1', (1, '5')),
   ('sB_4_0_b1', (1, '4:0')),

   ('rsvd2', (2, '7:3')),
   ('sA_10_8_b2', (2, '2:0')),

   ('ext2', (2, '7')),
   ('mux_1_0_b2', (2, '6:5')),
   ('sbA_1_b2', (2, '4')),
   ('sbB_1_b2', (2, '3')),
   ('sA_6_b2', (2, '2')),
   ('sB_6_5_b2', (2, '1:0')),

   ('sA_10_8_b3', (3, '7:5')),
   ('mux_2_b3', (3, '4')),
   ('sbA_2_b3', (3, '3')),
   ('rsvd3', (3, '2')),
   ('sA_7_b3', (3, '1')),
   ('sB_7_b3', (3, '0')),

   ('sbC_1_0_b3', (3, '7:6')),
   ('sC_5_0_b3', (3, '5:0')),

   ('sbC_2_b4', (4, '7')),
   ('sC_7_6_b4', (4, '6:5')),
   ('mux_2_b4', (4, '4')),
   ('sbA_2_b4', (4, '3')),
   ('ext4', (4, '2')),
   ('sA_7_b4', (4, '1')),
   ('sB_7_b4', (4, '0')),

   ('rsvd5', (5, '7:6')),
   ('sC_10_8_b5', (5, '5:3')),
   ('sA_10_8_b5', (5, '2:0')),
],
fields=[
   ('ext0', (F_BOOL, ['ext0'])),
   ('sbA_1bit_b0', (F_REGBANK1, ['sbA_0_b0'])),
   ('sA_6bit_b0', (F_UINT6, ['sA_5_0_b0'])),

   ('sel', (F_BOOL, ['sel'])),
   ('ext1', (F_BOOL, ['ext1'])),
   ('mux_2bit_b1', (F_IS0_SEL2, ['mux_1_0_b1'])),
   ('sbA_3bit_b1', (F_REGBANK, ['sbA_2_1_b1', 'sbA_0_b0'])),
   ('sA_11bit_b2', (F_UINT11, ['sA_10_8_b2', 'sA_7_6_b1', 'sA_5_0_b0'])),
   ('rsvd2', (F_UINT5, ['rsvd2'], 0)),

   ('sbB_1bit_b1', (F_REGBANK1, ['sbB_0_b1'])),
   ('sB_5bit_b1', (F_UINT5, ['sB_4_0_b1'])),

   ('ext2', (F_BOOL, ['ext2'])),
   ('mux_2bit_b2', (F_IS0_SEL2, ['mux_1_0_b2'])),
   ('sbA_2bit_b2', (F_REGBANK2, ['sbA_1_b2', 'sbA_0_b0'])),
   ('sbB_2bit_b2', (F_REGBANK2, ['sbB_1_b2', 'sbB_0_b1'])),
   ('sA_7bit_b2', (F_UINT7, ['sA_6_b2', 'sA_5_0_b0'])),
   ('sB_7bit_b2', (F_UINT7, ['sB_6_5_b2', 'sB_4_0_b1'])),

   ('sA_11bit_b3', (F_UINT11, ['sA_10_8_b3', 'sA_7_b3', 'sA_6_b2', 'sA_5_0_b0'])),
   ('mux_3bit_b3', (F_IS0_SEL, ['mux_2_b3', 'mux_1_0_b2'])),
   ('sbA_3bit_b3', (F_REGBANK, ['sbA_2_b3', 'sbA_1_b2', 'sbA_0_b0'])),
   ('rsvd3', (F_UINT1, ['rsvd3'], 0)),
   ('sB_8bit_b3', (F_UINT8, ['sB_7_b3', 'sB_6_5_b2', 'sB_4_0_b1'])),

   ('sbC_2bit_b3', (F_REGBANK2, ['sbC_1_0_b3'])),
   ('sC_6bit_b3', (F_UINT6, ['sC_5_0_b3'])),

   ('rsvd4', (F_UINT1, ['sbC_2_b4'], 0)),
   ('sbC_3bit_b4', (F_REGBANK, ['sbC_2_b4', 'sbC_1_0_b3'])),
   ('sC_8bit_b4', (F_UINT8, ['sC_7_6_b4', 'sC_5_0_b3'])),
   ('mux_3bit_b4', (F_IS0_SEL, ['mux_2_b4', 'mux_1_0_b2'])),
   ('sbA_3bit_b4', (F_REGBANK, ['sbA_2_b4', 'sbA_1_b2', 'sbA_0_b0'])),
   ('ext4', (F_BOOL, ['ext4'])),
   ('sA_8bit_b4', (F_UINT8, ['sA_7_b4', 'sA_6_b2', 'sA_5_0_b0'])),
   ('sB_8bit_b4', (F_UINT8, ['sB_7_b4', 'sB_6_5_b2', 'sB_4_0_b1'])),

   ('rsvd5', (F_UINT2, ['rsvd5'], 0)),
   ('rsvd5_', (F_UINT3, ['sC_10_8_b5'], 0)),
   ('sC_11bit_b5', (F_UINT11, ['sC_10_8_b5', 'sC_7_6_b4', 'sC_5_0_b3'])),
   ('sA_11bit_b5', (F_UINT11, ['sA_10_8_b5', 'sA_7_b4', 'sA_6_b2', 'sA_5_0_b0'])),
])

class SrcSpec(object):
   def __init__(self, is_upper, sbA_bits, sA_bits, sbB_bits, sB_bits, sbC_bits, sC_bits, mux_bits):
      self.is_upper = is_upper
      self.sbA_bits = sbA_bits
      self.sA_bits  = sA_bits
      self.sbB_bits = sbB_bits
      self.sB_bits  = sB_bits
      self.sbC_bits = sbC_bits
      self.sC_bits  = sC_bits
      self.mux_bits = mux_bits

# Lower sources.
I_ONE_LO_1B6I = bit_struct(
name='1lo_1b6i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 0),

   ('sb0', 'sbA_1bit_b0'),
   ('s0', 'sA_6bit_b0'),
], data=SrcSpec(False, 1, 6, -1, -1, -1, -1, -1))

I_ONE_LO_3B11I_2M = bit_struct(
name='1lo_3b11i_2m',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 0),
   ('ext1', 'ext1', 0),
   ('rsvd2', 'rsvd2'),

   ('sb0', 'sbA_3bit_b1'),
   ('s0', 'sA_11bit_b2'),
   ('is0', 'mux_2bit_b1'),
], data=SrcSpec(False, 3, 11, -1, -1, -1, -1, 2))

I_TWO_LO_1B6I_1B5I = bit_struct(
name='2lo_1b6i_1b5i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 1),
   ('ext1', 'ext1', 0),

   ('sb0', 'sbA_1bit_b0'),
   ('s0', 'sA_6bit_b0'),
   ('sb1', 'sbB_1bit_b1'),
   ('s1', 'sB_5bit_b1'),
], data=SrcSpec(False, 1, 6, 1, 5, -1, -1, -1))

I_TWO_LO_2B7I_2B7I_2M = bit_struct(
name='2lo_2b7i_2b7i_2m',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 1),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 0),

   ('sb0', 'sbA_2bit_b2'),
   ('s0', 'sA_7bit_b2'),
   ('sb1', 'sbB_2bit_b2'),
   ('s1', 'sB_7bit_b2'),
   ('is0', 'mux_2bit_b2'),
], data=SrcSpec(False, 2, 7, 2, 7, -1, -1, 2))

I_TWO_LO_3B11I_2B8I_3M = bit_struct(
name='2lo_3b11i_2b8i_3m',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 1),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 1),
   ('rsvd3', 'rsvd3'),

   ('sb0', 'sbA_3bit_b3'),
   ('s0', 'sA_11bit_b3'),
   ('sb1', 'sbB_2bit_b2'),
   ('s1', 'sB_8bit_b3'),
   ('is0', 'mux_3bit_b3'),
], data=SrcSpec(False, 3, 11, 2, 8, -1, -1, 3))

I_THREE_LO_2B7I_2B7I_2B6I_2M = bit_struct(
name='3lo_2b7i_2b7i_2b6i_2m',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 0),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 0),

   ('sb0', 'sbA_2bit_b2'),
   ('s0', 'sA_7bit_b2'),
   ('sb1', 'sbB_2bit_b2'),
   ('s1', 'sB_7bit_b2'),
   ('sb2', 'sbC_2bit_b3'),
   ('s2', 'sC_6bit_b3'),
   ('is0', 'mux_2bit_b2'),
], data=SrcSpec(False, 2, 7, 2, 7, 2, 6, 2))

I_THREE_LO_3B8I_2B8I_3B8I_3M = bit_struct(
name='3lo_3b8i_2b8i_3b8i_3m',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 0),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 1),
   ('ext4', 'ext4', 0),

   ('sb0', 'sbA_3bit_b4'),
   ('s0', 'sA_8bit_b4'),
   ('sb1', 'sbB_2bit_b2'),
   ('s1', 'sB_8bit_b4'),
   ('sb2', 'sbC_3bit_b4'),
   ('s2', 'sC_8bit_b4'),
   ('is0', 'mux_3bit_b4'),
], data=SrcSpec(False, 3, 8, 2, 8, 3, 8, 3))

I_THREE_LO_3B11I_2B8I_3B11I_3M = bit_struct(
name='3lo_3b11i_2b8i_3b11i_3m',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 0),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 1),
   ('ext4', 'ext4', 1),
   ('rsvd5', 'rsvd5'),

   ('sb0', 'sbA_3bit_b4'),
   ('s0', 'sA_11bit_b5'),
   ('sb1', 'sbB_2bit_b2'),
   ('s1', 'sB_8bit_b4'),
   ('sb2', 'sbC_3bit_b4'),
   ('s2', 'sC_11bit_b5'),
   ('is0', 'mux_3bit_b4'),
], data=SrcSpec(False, 3, 11, 2, 8, 3, 11, 3))

# Upper sources.
I_ONE_UP_1B6I = bit_struct(
name='1up_1b6i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 0),

   ('sb3', 'sbA_1bit_b0'),
   ('s3', 'sA_6bit_b0'),
], data=SrcSpec(True, 1, 6, -1, -1, -1, -1, -1))

I_ONE_UP_3B11I = bit_struct(
name='1up_3b11i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 0),
   ('ext1', 'ext1', 0),
   ('rsvd1', 'mux_2bit_b1', 0),
   ('rsvd2', 'rsvd2'),

   ('sb3', 'sbA_3bit_b1'),
   ('s3', 'sA_11bit_b2'),
], data=SrcSpec(True, 3, 11, -1, -1, -1, -1, -1))

I_TWO_UP_1B6I_1B5I = bit_struct(
name='2up_1b6i_1b5i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 1),
   ('ext1', 'ext1', 0),

   ('sb3', 'sbA_1bit_b0'),
   ('s3', 'sA_6bit_b0'),
   ('sb4', 'sbB_1bit_b1'),
   ('s4', 'sB_5bit_b1'),
], data=SrcSpec(True, 1, 6, 1, 5, -1, -1, -1))

I_TWO_UP_2B7I_2B7I = bit_struct(
name='2up_2b7i_2b7i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 1),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 0),
   ('rsvd2', 'mux_2bit_b2', 0),

   ('sb3', 'sbA_2bit_b2'),
   ('s3', 'sA_7bit_b2'),
   ('sb4', 'sbB_2bit_b2'),
   ('s4', 'sB_7bit_b2'),
], data=SrcSpec(True, 2, 7, 2, 7, -1, -1, -1))

I_TWO_UP_3B11I_2B8I = bit_struct(
name='2up_3b11i_2b8i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 1),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 1),
   ('rsvd3', 'rsvd3'),
   ('rsvd3_', 'mux_3bit_b3', 0),

   ('sb3', 'sbA_3bit_b3'),
   ('s3', 'sA_11bit_b3'),
   ('sb4', 'sbB_2bit_b2'),
   ('s4', 'sB_8bit_b3'),
], data=SrcSpec(True, 3, 11, 2, 8, -1, -1, -1))

I_THREE_UP_2B7I_2B7I_2B6I = bit_struct(
name='3up_2b7i_2b7i_2b6i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 0),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 0),
   ('rsvd2', 'mux_2bit_b2', 0),

   ('sb3', 'sbA_2bit_b2'),
   ('s3', 'sA_7bit_b2'),
   ('sb4', 'sbB_2bit_b2'),
   ('s4', 'sB_7bit_b2'),
   ('sb5', 'sbC_2bit_b3'),
   ('s5', 'sC_6bit_b3'),
], data=SrcSpec(True, 2, 7, 2, 7, 2, 6, -1))

I_THREE_UP_3B8I_2B8I_2B8I = bit_struct(
name='3up_3b8i_2b8i_2b8i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 0),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 1),
   ('ext4', 'ext4', 0),
   ('rsvd4', 'mux_3bit_b4', 0),
   ('rsvd4_', 'rsvd4'),

   ('sb3', 'sbA_3bit_b4'),
   ('s3', 'sA_8bit_b4'),
   ('sb4', 'sbB_2bit_b2'),
   ('s4', 'sB_8bit_b4'),
   ('sb5', 'sbC_2bit_b3'),
   ('s5', 'sC_8bit_b4'),
], data=SrcSpec(True, 3, 8, 2, 8, 2, 8, -1))

I_THREE_UP_3B11I_2B8I_2B8I = bit_struct(
name='3up_3b11i_2b8i_2b8i',
bit_set=I_SRC,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('sel', 'sel', 0),
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 1),
   ('ext4', 'ext4', 1),
   ('rsvd4', 'mux_3bit_b4', 0),
   ('rsvd4_', 'rsvd4'),
   ('rsvd5', 'rsvd5'),
   ('rsvd5_', 'rsvd5_'),

   ('sb3', 'sbA_3bit_b4'),
   ('s3', 'sA_11bit_b5'),
   ('sb4', 'sbB_2bit_b2'),
   ('s4', 'sB_8bit_b4'),
   ('sb5', 'sbC_2bit_b3'),
   ('s5', 'sC_8bit_b4'),
], data=SrcSpec(True, 3, 11, 2, 8, 2, 8, -1))

# Internal source selector definitions.
F_IS5_SEL = field_enum_type(
name='is5_sel', num_bits=2,
elems=[
   ('ft0', 0b00),
   ('ft1', 0b01),
   ('ft2', 0b10),
   ('fte', 0b11),
])

F_IS4_SEL = field_enum_type(
name='is4_sel', num_bits=2,
elems=[
   ('ft0', 0b00),
   ('ft1', 0b01),
   ('ft2', 0b10),
   ('fte', 0b11),
])

F_IS3_SEL = field_enum_type(
name='is3_sel', num_bits=2,
elems=[
   ('ft0', 0b00),
   ('ft1', 0b01),
   ('fte', 0b11),
])

F_IS2_SEL = field_enum_type(
name='is2_sel', num_bits=1,
elems=[
   ('ft1', 0b0),
   ('fte', 0b1),
])

F_IS1_SEL = field_enum_type(
name='is1_sel', num_bits=1,
elems=[
   ('ft0', 0b0),
   ('fte', 0b1),
])

I__ISS = bit_set(
name='iss',
pieces=[
   ('is5', (0, '7:6')),
   ('is4', (0, '5:4')),
   ('is3', (0, '3:2')),
   ('is2', (0, '1')),
   ('is1', (0, '0')),
],
fields=[
   ('is5', (F_IS5_SEL, ['is5'])),
   ('is4', (F_IS4_SEL, ['is4'])),
   ('is3', (F_IS3_SEL, ['is3'])),
   ('is2', (F_IS2_SEL, ['is2'])),
   ('is1', (F_IS1_SEL, ['is1'])),
])

I_ISS = bit_struct(
name='iss',
bit_set=I__ISS,
field_mappings=[
   ('is5', 'is5'),
   ('is4', 'is4'),
   ('is3', 'is3'),
   ('is2', 'is2'),
   ('is1', 'is1'),
])

# Destination definitions.
I_DST = bit_set(
name='dst',
pieces=[
   ('ext0', (0, '7')),
   ('dbN_0_b0', (0, '6')),
   ('dN_5_0_b0', (0, '5:0')),

   ('rsvd1', (1, '7')),
   ('dN_10_8_b1', (1, '6:4')),
   ('dbN_2_1_b1', (1, '3:2')),
   ('dN_7_6_b1', (1, '1:0')),

   ('db0_0_b0', (0, '7')),
   ('d0_6_0_b0', (0, '6:0')),

   ('ext1', (1, '7')),
   ('db1_0_b1', (1, '6')),
   ('d1_5_0_b1', (1, '5:0')),

   ('ext2', (2, '7')),
   ('db1_2_1_b2', (2, '6:5')),
   ('d1_7_6_b2', (2, '4:3')),
   ('db0_2_1_b2', (2, '2:1')),
   ('d0_7_b2', (2, '0')),

   ('rsvd3', (3, '7:6')),
   ('d1_10_8_b3', (3, '5:3')),
   ('d0_10_8_b3', (3, '2:0')),
],
fields=[
   ('ext0', (F_BOOL, ['ext0'])),
   ('dbN_1bit_b0', (F_REGBANK1, ['dbN_0_b0'])),
   ('dN_6bit_b0', (F_UINT6, ['dN_5_0_b0'])),

   ('rsvd1', (F_UINT1, ['rsvd1'], 0)),
   ('dbN_3bit_b1', (F_REGBANK, ['dbN_2_1_b1', 'dbN_0_b0'])),
   ('dN_11bit_b1', (F_UINT11, ['dN_10_8_b1', 'dN_7_6_b1', 'dN_5_0_b0'])),

   ('db0_1bit_b0', (F_REGBANK1, ['db0_0_b0'])),
   ('d0_7bit_b0', (F_UINT7, ['d0_6_0_b0'])),

   ('ext1', (F_BOOL, ['ext1'])),
   ('db1_1bit_b1', (F_REGBANK1, ['db1_0_b1'])),
   ('d1_6bit_b1', (F_UINT6, ['d1_5_0_b1'])),

   ('ext2', (F_BOOL, ['ext2'])),
   ('db0_3bit_b2', (F_REGBANK, ['db0_2_1_b2', 'db0_0_b0'])),
   ('d0_8bit_b2', (F_UINT8, ['d0_7_b2', 'd0_6_0_b0'])),
   ('db1_3bit_b2', (F_REGBANK, ['db1_2_1_b2', 'db1_0_b1'])),
   ('d1_8bit_b2', (F_UINT8, ['d1_7_6_b2', 'd1_5_0_b1'])),

   ('rsvd3', (F_UINT2, ['rsvd3'], 0)),
   ('d0_11bit_b3', (F_UINT11, ['d0_10_8_b3', 'd0_7_b2', 'd0_6_0_b0'])),
   ('d1_11bit_b3', (F_UINT11, ['d1_10_8_b3', 'd1_7_6_b2', 'd1_5_0_b1'])),
])

class DstSpec(object):
   def __init__(self, dual_dsts, db0_bits, d0_bits, db1_bits, d1_bits):
      self.dual_dsts = dual_dsts
      self.db0_bits = db0_bits
      self.d0_bits  = d0_bits
      self.db1_bits = db1_bits
      self.d1_bits  = d1_bits

I_ONE_1B6I = bit_struct(
name='1_1b6i',
bit_set=I_DST,
field_mappings=[
   ('ext0', 'ext0', 0),

   ('dbN', 'dbN_1bit_b0'),
   ('dN', 'dN_6bit_b0'),
], data=DstSpec(False, 1, 6, -1, -1))

I_ONE_3B11I = bit_struct(
name='1_3b11i',
bit_set=I_DST,
field_mappings=[
   ('ext0', 'ext0', 1),
   ('rsvd1', 'rsvd1'),

   ('dbN', 'dbN_3bit_b1'),
   ('dN', 'dN_11bit_b1'),
], data=DstSpec(False, 3, 11, -1, -1))

I_TWO_1B7I_1B6I = bit_struct(
name='2_1b7i_1b6i',
bit_set=I_DST,
field_mappings=[
   ('ext1', 'ext1', 0),

   ('db0', 'db0_1bit_b0'),
   ('d0', 'd0_7bit_b0'),
   ('db1', 'db1_1bit_b1'),
   ('d1', 'd1_6bit_b1'),
], data=DstSpec(True, 1, 7, 1, 6))

I_TWO_3B8I_3B8I = bit_struct(
name='2_3b8i_3b8i',
bit_set=I_DST,
field_mappings=[
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 0),

   ('db0', 'db0_3bit_b2'),
   ('d0', 'd0_8bit_b2'),
   ('db1', 'db1_3bit_b2'),
   ('d1', 'd1_8bit_b2'),
], data=DstSpec(True, 3, 8, 3, 8))

I_TWO_3B11I_3B11I = bit_struct(
name='2_3b11i_3b11i',
bit_set=I_DST,
field_mappings=[
   ('ext1', 'ext1', 1),
   ('ext2', 'ext2', 1),
   ('rsvd3', 'rsvd3'),

   ('db0', 'db0_3bit_b2'),
   ('d0', 'd0_11bit_b3'),
   ('db1', 'db1_3bit_b2'),
   ('d1', 'd1_11bit_b3'),
], data=DstSpec(True, 3, 11, 3, 11))

# Main ALU ops.
F_MAIN_OP = field_enum_type(
name='main_op', num_bits=3,
elems=[
   ('fadd', 0b000),
   ('fadd_lp', 0b001),
   ('fmul', 0b010),
   ('fmul_lp', 0b011),
   ('sngl', 0b100),
   ('int8_16', 0b101),
   ('fmad_movc', 0b110),
   ('int32_64_tst', 0b111),
])

F_SNGL_OP = field_enum_type(
name='sngl_op', num_bits=4,
elems=[
   ('rcp', 0b0000),
   ('rsq', 0b0001),
   ('log', 0b0010),
   ('exp', 0b0011),
   ('f16sop', 0b0100),
   ('logcn', 0b0101),
   ('gamma', 0b0110),
   ('byp', 0b0111),
   ('dsx', 0b1000),
   ('dsy', 0b1001),
   ('dsxf', 0b1010),
   ('dsyf', 0b1011),
   ('pck', 0b1100),
   ('red', 0b1101),
   ('sinc', 0b1110),
   ('arctanc', 0b1111),
])

F_RED_PART = field_enum_type(
name='red_part', num_bits=1,
elems=[
   ('a', 0b0),
   ('b', 0b1),
])

F_RED_TYPE = field_enum_type(
name='red_type', num_bits=1,
elems=[
   ('sin', 0b0),
   ('cos', 0b1),
])

F_GAMMA_OP = field_enum_type(
name='gamma_op', num_bits=1,
elems=[
   ('cmp', 0b0),
   ('exp', 0b1),
])

F_PCK_FORMAT = field_enum_type(
name='pck_format', num_bits=5,
elems=[
   ('u8888', 0b00000),
   ('s8888', 0b00001),
   ('o8888', 0b00010),
   ('u1616', 0b00011),
   ('s1616', 0b00100),
   ('o1616', 0b00101),
   ('u32', 0b00110),
   ('s32', 0b00111),
   ('u1010102', 0b01000),
   ('s1010102', 0b01001),
   ('u111110', 0b01010),
   ('s111110', 0b01011),
   ('f111110', 0b01100),
   ('f16f16', 0b01110),
   ('f32', 0b01111),
   ('cov', 0b10000),
   ('u565u565', 0b10001),
   ('d24s8', 0b10010),
   ('s8d24', 0b10011),
   ('f32_mask', 0b10100),
   ('2f10f10f10', 0b10101),
   ('s8888ogl', 0b10110),
   ('s1616ogl', 0b10111),
   ('zero', 0b11110),
   ('one', 0b11111),
])

F_INT8_16_OP = field_enum_type(
name='int8_16_op', num_bits=2,
elems=[
   ('add', 0b00),
   ('mul', 0b01),
   ('mad_0_1', 0b10),
   ('mad_2_3', 0b11),
])

F_INT8_16_FMT = field_enum_type(
name='int8_16_fmt', num_bits=1,
elems=[
   ('8bit', 0b0),
   ('16bit', 0b1),
])

F_S2CH = field_enum_type(
name='s2ch', num_bits=1,
elems=[
   ('elo', 0b0),
   ('ehi', 0b1),
])

F_S01CH = field_enum_type(
name='s01ch', num_bits=2,
elems=[
   ('e0', 0b00),
   ('e1', 0b01),
   ('e2', 0b10),
   ('e3', 0b11),
])

F_MOVW01 = field_enum_type(
name='movw01', num_bits=2,
elems=[
   ('ft0', 0b00),
   ('ft1', 0b01),
   ('ft2', 0b10),
   ('fte', 0b11),
])

F_MASKW0 = field_enum_type(
name='maskw0', num_bits=4, is_bitset=True,
elems=[
   ('e0', 0b0001),
   ('e1', 0b0010),
   ('e2', 0b0100),
   ('e3', 0b1000),
   ('eall', 0b1111),
])

F_INT32_64_OP = field_enum_type(
name='int32_64_op', num_bits=2,
elems=[
   ('add6432', 0b00),
   ('add64', 0b01),
   ('madd32', 0b10),
   ('madd64', 0b11),
])

F_TST_OP = field_enum_type(
name='tst_op', num_bits=4,
elems=[
   ('z', 0b0000),
   ('gz', 0b0001),
   ('gez', 0b0010),
   ('c', 0b0011),
   ('e', 0b0100),
   ('g', 0b0101),
   ('ge', 0b0110),
   ('ne', 0b0111),
   ('l', 0b1000),
   ('le', 0b1001),
])

F_TST_OP3 = field_enum_subtype(name='tst_op3', parent=F_TST_OP, num_bits=3)

F_TST_TYPE = field_enum_type(
name='tst_type', num_bits=3,
elems=[
   ('f32', 0b000),
   ('u16', 0b001),
   ('s16', 0b010),
   ('u8', 0b011),
   ('s8', 0b100),
   ('u32', 0b101),
   ('s32', 0b110),
])

I_MAIN = bit_set(
name='main',
pieces=[
   ('main_op', (0, '7:5')),
   ('ext0', (0, '4')),

   # fadd/fadd.lp/fmul/fmul.lp
   ('sat_fam', (0, '4')),
   ('s0neg_fam', (0, '3')),
   ('s0abs_fam', (0, '2')),
   ('s1abs_fam', (0, '1')),
   ('s0flr_fam', (0, '0')),

   # sngl
   ('sngl_op', (0, '3:0')),

   ## RED
   ('red_part', (1, '7')),
   ('iter', (1, '6:4')),
   ('red_type', (1, '3')),
   ('pwen_red', (1, '2')),

   ## Gamma
   ('gammaop', (1, '2')),

   ## Common
   ('s0neg_sngl', (1, '1')),
   ('s0abs_sngl', (1, '0')),

   ## PCK/UPCK
   ('upck_elem', (1, '7:6')),
   ('scale_rtz', (1, '5')),

   ('prog', (1, '7')),
   ('rtz', (1, '6')),
   ('scale', (1, '5')),

   ('pck_format', (1, '4:0')),

   # int8_16
   ('int8_16_op', (0, '3:2')),
   ('s_i816', (0, '1')),
   ('f_i816', (0, '0')),

   ('s2ch', (1, '7')),
   ('rsvd1_i816', (1, '6')),
   ('s2neg_i816', (1, '5')),
   ('s2abs_i816', (1, '4')),
   ('s1abs_i816', (1, '3')),
   ('s0neg_i816', (1, '2')),
   ('s0abs_i816', (1, '1')),
   ('sat_i816', (1, '0')),

   ('rsvd2_i816', (2, '7:4')),
   ('s1ch', (2, '3:2')),
   ('s0ch', (2, '1:0')),

   # fmad
   ('s0neg_fma', (0, '3')),
   ('s0abs_fma', (0, '2')),
   ('s2neg_fma', (0, '1')),
   ('sat_fma', (0, '0')),

   ('rsvd1_fma', (1, '7:5')),
   ('lp_fma', (1, '4')),
   ('s1abs_fma', (1, '3')),
   ('s1neg_fma', (1, '2')),
   ('s2flr_fma', (1, '1')),
   ('s2abs_fma', (1, '0')),

   # int32_64
   ('s_i3264', (0, '3')),
   ('s2neg_i3264', (0, '2')),
   ('int32_64_op', (0, '1:0')),

   ('rsvd1_i3264', (1, '7')),
   ('cin_i3264', (1, '6')),
   ('s1neg_i3264', (1, '5')),
   ('s0neg_i3264', (1, '4')),
   ('rsvd1_i3264_', (1, '3')),
   ('s0abs_i3264', (1, '2')),
   ('s1abs_i3264', (1, '1')),
   ('s2abs_i3264', (1, '0')),

   # movc
   ('movw1', (0, '3:2')),
   ('movw0', (0, '1:0')),

   ('rsvd1_movc', (1, '7:6')),
   ('maskw0', (1, '5:2')),
   ('aw', (1, '1')),
   ('p2end_movc', (1, '0')),

   # tst
   ('tst_op_2_0', (0, '3:1')),
   ('pwen_tst', (0, '0')),

   ('tst_type', (1, '7:5')),
   ('p2end_tst', (1, '4')),
   ('tst_elem', (1, '3:2')),
   ('rsvd1_tst', (1, '1')),
   ('tst_op_3', (1, '0')),
],
fields=[
   ('main_op', (F_MAIN_OP, ['main_op'])),
   ('ext0', (F_BOOL, ['ext0'])),

   # fadd/fadd.lp/fmul/fmul.lp
   ('sat_fam', (F_BOOL, ['sat_fam'])),
   ('s0neg_fam', (F_BOOL, ['s0neg_fam'])),
   ('s0abs_fam', (F_BOOL, ['s0abs_fam'])),
   ('s1abs_fam', (F_BOOL, ['s1abs_fam'])),
   ('s0flr_fam', (F_BOOL, ['s0flr_fam'])),

   # sngl
   ('sngl_op', (F_SNGL_OP, ['sngl_op'])),

   ('rsvd1_sngl', (F_UINT5, ['red_part', 'iter', 'red_type'], 0)),
   ('rsvd1_sngl_', (F_UINT1, ['pwen_red'], 0)),

   ## RED
   ('red_part', (F_RED_PART, ['red_part'])),
   ('iter', (F_UINT3, ['iter'])),
   ('red_type', (F_RED_TYPE, ['red_type'])),
   ('pwen_red', (F_BOOL, ['pwen_red'])),

   ## Gamma
   ('gammaop', (F_GAMMA_OP, ['gammaop'])),

   ## Common
   ('s0neg_sngl', (F_BOOL, ['s0neg_sngl'])),
   ('s0abs_sngl', (F_BOOL, ['s0abs_sngl'])),

   ## PCK/UPCK
   ('upck_elem', (F_UINT2, ['upck_elem'])),
   ('scale_rtz', (F_BOOL, ['scale_rtz'])),

   ('prog', (F_BOOL, ['prog'])),
   ('rtz', (F_BOOL, ['rtz'])),
   ('scale', (F_BOOL, ['scale'])),

   ('pck_format', (F_PCK_FORMAT, ['pck_format'])),

   # int8_16
   ('int8_16_op', (F_INT8_16_OP, ['int8_16_op'])),
   ('s_i816', (F_BOOL, ['s_i816'])),
   ('f_i816', (F_INT8_16_FMT, ['f_i816'])),

   ('s2ch', (F_S2CH, ['s2ch'])),
   ('rsvd1_i816', (F_UINT1, ['rsvd1_i816'], 0)),
   ('s2neg_i816', (F_BOOL, ['s2neg_i816'])),
   ('s2abs_i816', (F_BOOL, ['s2abs_i816'])),
   ('s1abs_i816', (F_BOOL, ['s1abs_i816'])),
   ('s0neg_i816', (F_BOOL, ['s0neg_i816'])),
   ('s0abs_i816', (F_BOOL, ['s0abs_i816'])),
   ('sat_i816', (F_BOOL, ['sat_i816'])),

   ('rsvd2_i816', (F_UINT4, ['rsvd2_i816'], 0)),
   ('s1ch', (F_S01CH, ['s1ch'])),
   ('s0ch', (F_S01CH, ['s0ch'])),

   # fmad
   ('s0neg_fma', (F_BOOL, ['s0neg_fma'])),
   ('s0abs_fma', (F_BOOL, ['s0abs_fma'])),
   ('s2neg_fma', (F_BOOL, ['s2neg_fma'])),
   ('sat_fma', (F_BOOL, ['sat_fma'])),

   ('rsvd1_fma', (F_UINT3, ['rsvd1_fma'], 0)),
   ('lp_fma', (F_BOOL, ['lp_fma'])),
   ('s1abs_fma', (F_BOOL, ['s1abs_fma'])),
   ('s1neg_fma', (F_BOOL, ['s1neg_fma'])),
   ('s2flr_fma', (F_BOOL, ['s2flr_fma'])),
   ('s2abs_fma', (F_BOOL, ['s2abs_fma'])),

   # int32_64
   ('s_i3264', (F_BOOL, ['s_i3264'])),
   ('s2neg_i3264', (F_BOOL, ['s2neg_i3264'])),
   ('int32_64_op', (F_INT32_64_OP, ['int32_64_op'])),

   ('rsvd1_i3264', (F_UINT2, ['rsvd1_i3264_', 'rsvd1_i3264'], 0)),
   ('cin_i3264', (F_BOOL, ['cin_i3264'])),
   ('s1neg_i3264', (F_BOOL, ['s1neg_i3264'])),
   ('s0neg_i3264', (F_BOOL, ['s0neg_i3264'])),
   ('s0abs_i3264', (F_BOOL, ['s0abs_i3264'])),
   ('s1abs_i3264', (F_BOOL, ['s1abs_i3264'])),
   ('s2abs_i3264', (F_BOOL, ['s2abs_i3264'])),

   # movc
   ('movw1', (F_MOVW01, ['movw1'])),
   ('movw0', (F_MOVW01, ['movw0'])),

   ('rsvd1_movc', (F_UINT2, ['rsvd1_movc'], 0)),
   ('maskw0', (F_MASKW0, ['maskw0'])),
   ('aw', (F_BOOL, ['aw'])),
   ('p2end_movc', (F_BOOL, ['p2end_movc'])),

   # tst
   ('tst_op_3bit', (F_TST_OP3, ['tst_op_2_0'])),
   ('pwen_tst', (F_BOOL, ['pwen_tst'])),

   ('tst_type', (F_TST_TYPE, ['tst_type'])),
   ('p2end_tst', (F_BOOL, ['p2end_tst'])),
   ('tst_elem', (F_UINT2, ['tst_elem'])),
   ('rsvd1_tst', (F_UINT1, ['rsvd1_tst'], 0)),
   ('tst_op_4bit', (F_TST_OP, ['tst_op_3', 'tst_op_2_0'])),
])

I_FADD = bit_struct(
name='fadd',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'fadd'),

   ('sat', 'sat_fam'),
   ('s0neg', 's0neg_fam'),
   ('s0abs', 's0abs_fam'),
   ('s1abs', 's1abs_fam'),
   ('s0flr', 's0flr_fam'),
])

I_FADD_LP = bit_struct(
name='fadd_lp',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'fadd_lp'),

   ('sat', 'sat_fam'),
   ('s0neg', 's0neg_fam'),
   ('s0abs', 's0abs_fam'),
   ('s1abs', 's1abs_fam'),
   ('s0flr', 's0flr_fam'),
])

I_FMUL = bit_struct(
name='fmul',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'fmul'),

   ('sat', 'sat_fam'),
   ('s0neg', 's0neg_fam'),
   ('s0abs', 's0abs_fam'),
   ('s1abs', 's1abs_fam'),
   ('s0flr', 's0flr_fam'),
])

I_FMUL_LP = bit_struct(
name='fmul_lp',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'fmul_lp'),

   ('sat', 'sat_fam'),
   ('s0neg', 's0neg_fam'),
   ('s0abs', 's0abs_fam'),
   ('s1abs', 's1abs_fam'),
   ('s0flr', 's0flr_fam'),
])

# Covers FRCP, FRSQ, FLOG, FEXP, FLOGCN, BYP,
# FDSX, FDSY, FDSXF, FDSYF, FSINC, FARCTANC
I_SNGL = bit_struct(
name='sngl',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'sngl'),
   ('ext0', 'ext0', 0),
   ('sngl_op', 'sngl_op'),
])

I_SNGL_EXT = bit_struct(
name='sngl_ext',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'sngl'),
   ('ext0', 'ext0', 1),
   ('sngl_op', 'sngl_op'),

   ('rsvd1', 'rsvd1_sngl'),
   ('rsvd1_', 'rsvd1_sngl_'),
   ('s0neg', 's0neg_sngl'),
   ('s0abs', 's0abs_sngl'),
])

# F16SOP
# TODO

# GCMP
I_GCMP = bit_struct(
name='gcmp',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'sngl'),
   ('ext0', 'ext0', 0),
   ('sngl_op', 'sngl_op', 'gamma'),
])

I_GCMP_EXT = bit_struct(
name='gcmp_ext',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'sngl'),
   ('ext0', 'ext0', 1),
   ('sngl_op', 'sngl_op', 'gamma'),

   ('rsvd1', 'rsvd1_sngl'),
   ('gammaop', 'gammaop', 'cmp'),
   ('s0neg', 's0neg_sngl'),
   ('s0abs', 's0abs_sngl'),
])

# GEXP
I_GEXP = bit_struct(
name='gexp',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'sngl'),
   ('ext0', 'ext0', 1),
   ('sngl_op', 'sngl_op', 'gamma'),

   ('rsvd1', 'rsvd1_sngl'),
   ('gammaop', 'gammaop', 'exp'),
   ('s0neg', 's0neg_sngl'),
   ('s0abs', 's0abs_sngl'),
])

# PCK
I_PCK = bit_struct(
name='pck',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'sngl'),
   ('ext0', 'ext0', 1),
   ('sngl_op', 'sngl_op', 'pck'),

   ('prog', 'prog'),
   ('rtz', 'rtz'),
   ('scale', 'scale'),
   ('pck_format', 'pck_format'),
])

# UPCK
I_UPCK = bit_struct(
name='upck',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'sngl'),
   ('ext0', 'ext0', 1),
   ('sngl_op', 'sngl_op', 'pck'),

   ('elem', 'upck_elem'),
   ('scale_rtz', 'scale_rtz'),
   ('pck_format', 'pck_format'),
])

# FRED
I_FRED = bit_struct(
name='fred',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'sngl'),
   ('ext0', 'ext0', 1),
   ('sngl_op', 'sngl_op', 'red'),

   ('red_part', 'red_part'),
   ('iter', 'iter'),
   ('red_type', 'red_type'),
   ('pwen', 'pwen_red'),
   ('s0neg', 's0neg_sngl'),
   ('s0abs', 's0abs_sngl'),
])

I_INT8_16 = bit_struct(
name='int8_16',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'int8_16'),
   ('ext0', 'ext0', 0),
   ('int8_16_op', 'int8_16_op'),
   ('s', 's_i816'),
   ('f', 'f_i816'),
])

I_INT8_16_EXT = bit_struct(
name='int8_16_ext',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'int8_16'),
   ('ext0', 'ext0', 1),
   ('int8_16_op', 'int8_16_op'),
   ('s', 's_i816'),
   ('f', 'f_i816'),

   ('s2ch', 's2ch'),
   ('rsvd1', 'rsvd1_i816'),
   ('s2neg', 's2neg_i816'),
   ('s2abs', 's2abs_i816'),
   ('s1abs', 's1abs_i816'),
   ('s0neg', 's0neg_i816'),
   ('s0abs', 's0abs_i816'),
   ('sat', 'sat_i816'),
])

I_INT8_16_EXT_SEL = bit_struct(
name='int8_16_ext_sel',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'int8_16'),
   ('ext0', 'ext0', 1),
   ('int8_16_op', 'int8_16_op'),
   ('s', 's_i816'),
   ('f', 'f_i816'),

   ('s2ch', 's2ch'),
   ('rsvd1', 'rsvd1_i816'),
   ('s2neg', 's2neg_i816'),
   ('s2abs', 's2abs_i816'),
   ('s1abs', 's1abs_i816'),
   ('s0neg', 's0neg_i816'),
   ('s0abs', 's0abs_i816'),
   ('sat', 'sat_i816'),

   ('rsvd2', 'rsvd2_i816'),
   ('s1ch', 's1ch'),
   ('s0ch', 's0ch'),
])

I_FMAD = bit_struct(
name='fmad',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'fmad_movc'),
   ('ext0', 'ext0', 0),

   ('s0neg', 's0neg_fma'),
   ('s0abs', 's0abs_fma'),
   ('s2neg', 's2neg_fma'),
   ('sat', 'sat_fma'),
])

I_FMAD_EXT = bit_struct(
name='fmad_ext',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'fmad_movc'),
   ('ext0', 'ext0', 1),

   ('s0neg', 's0neg_fma'),
   ('s0abs', 's0abs_fma'),
   ('s2neg', 's2neg_fma'),
   ('sat', 'sat_fma'),

   ('rsvd1', 'rsvd1_fma'),
   ('lp', 'lp_fma'),
   ('s1abs', 's1abs_fma'),
   ('s1neg', 's1neg_fma'),
   ('s2flr', 's2flr_fma'),
   ('s2abs', 's2abs_fma'),
])

I_INT32_64 = bit_struct(
name='int32_64',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'int32_64_tst'),
   ('ext0', 'ext0', 0),

   ('s', 's_i3264'),
   ('s2neg', 's2neg_i3264'),
   ('int32_64_op', 'int32_64_op'),
])

I_INT32_64_EXT = bit_struct(
name='int32_64_ext',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'int32_64_tst'),
   ('ext0', 'ext0', 1),

   ('s', 's_i3264'),
   ('s2neg', 's2neg_i3264'),
   ('int32_64_op', 'int32_64_op'),

   ('rsvd1', 'rsvd1_i3264'),

   ('cin', 'cin_i3264'),
   ('s1neg', 's1neg_i3264'),
   ('s0neg', 's0neg_i3264'),
   ('s0abs', 's0abs_i3264'),
   ('s1abs', 's1abs_i3264'),
   ('s2abs', 's2abs_i3264'),
])

I_MOVC = bit_struct(
name='movc',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'fmad_movc'),
   ('ext0', 'ext0', 0),

   ('movw1', 'movw1'),
   ('movw0', 'movw0'),
])

I_MOVC_EXT = bit_struct(
name='movc_ext',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'fmad_movc'),
   ('ext0', 'ext0', 1),

   ('movw1', 'movw1'),
   ('movw0', 'movw0'),

   ('rsvd1', 'rsvd1_movc'),
   ('maskw0', 'maskw0'),
   ('aw', 'aw'),
   ('p2end', 'p2end_movc'),
])

I_TST = bit_struct(
name='tst',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'int32_64_tst'),
   ('ext0', 'ext0', 0),

   ('tst_op', 'tst_op_3bit'),
   ('pwen', 'pwen_tst'),
])

I_TST_EXT = bit_struct(
name='tst_ext',
bit_set=I_MAIN,
field_mappings=[
   ('main_op', 'main_op', 'int32_64_tst'),
   ('ext0', 'ext0', 1),

   ('tst_op', 'tst_op_4bit'),
   ('pwen', 'pwen_tst'),

   ('type', 'tst_type'),
   ('p2end', 'p2end_tst'),
   ('elem', 'tst_elem'),
   ('rsvd1', 'rsvd1_tst'),
])

# Backend ALU ops.
F_BACKEND_OP = field_enum_type(
name='backend_op', num_bits=3,
elems=[
   ('uvsw', 0b000),
   ('msk', 0b001),
   ('phas', 0b010),
   ('setl', 0b011),
   ('vistest', 0b100),
   ('fitr', 0b101),
   ('emit', 0b110),
   ('dma', 0b111),
])

F_DSEL = field_enum_type(
name='dsel', num_bits=1,
elems=[
   ('w0', 0b0),
   ('w1', 0b1),
])

F_UVSW_OP = field_enum_type(
name='uvsw_op', num_bits=3,
elems=[
   ('write', 0b000),
   ('emit', 0b001),
   ('cut', 0b010),
   ('emit_cut', 0b011),
   ('endtask', 0b100),
   ('emit_endtask', 0b101),
   ('write_emit_endtask', 0b110),
])

F_SRCSEL = field_enum_type(
name='srcsel', num_bits=3,
elems=[
   ('s0', 0b000),
   ('s1', 0b001),
   ('s2', 0b010),
   ('s3', 0b011),
   ('s4', 0b100),
   ('s5', 0b101),
])

F_MSK_MODE = field_enum_type(
name='msk_mode', num_bits=3,
elems=[
   ('vm', 0b000),
   ('icm', 0b001),
   ('icmoc', 0b010),
   ('icmi', 0b011),
   ('caxy', 0b100),
])

F_MSK_OP = field_enum_type(
name='msk_op', num_bits=1,
elems=[
   ('sav', 0b0),
   ('mov', 0b1),
])

F_PHAS_TYPE = field_enum_type(
name='phas_type', num_bits=1,
elems=[
   ('reg', 0b0),
   ('imm', 0b1),
])

F_PHAS_RATE = field_enum_type(
name='phas_rate', num_bits=2,
elems=[
   ('inst', 0b00),
   ('smp_sel', 0b01),
   ('smp_full', 0b10),
])

F_VISTEST_OP = field_enum_type(
name='vistest_op', num_bits=1,
elems=[
   ('depthf', 0b0),
   ('atst', 0b1),
])

F_ITER_MODE = field_enum_type(
name='iter_mode', num_bits=2,
elems=[
   ('pixel', 0b00),
   ('sample', 0b01),
   ('centroid', 0b10),
])

F_ITER_MODE1 = field_enum_subtype(name='iter_mode1', parent=F_ITER_MODE, num_bits=1)

F_PERSP_CTL = field_enum_type(
name='persp_ctl', num_bits=2,
elems=[
   ('none', 0b00),
   ('iter_mul', 0b01),
   ('iter_mul_store', 0b10),
   ('mul_stored', 0b11),
])

F_PERSP_CTL1 = field_enum_subtype(name='persp_ctl1', parent=F_PERSP_CTL, num_bits=1)

F_SCHED_CTRL = field_enum_type(
name='sched_ctrl', num_bits=2,
elems=[
   ('none', 0b00),
   ('swap', 0b01),
   ('wdf', 0b10),
])

F_SCHED_CTRL1 = field_enum_subtype(name='sched_ctrl1', parent=F_SCHED_CTRL, num_bits=1)

F_IDX_CTRL = field_enum_type(
name='idx_ctrl', num_bits=2,
elems=[
   ('none', 0b00),
   ('idx0', 0b01),
   ('idx1', 0b10),
])

F_DMA_OP = field_enum_type(
name='dma_op', num_bits=3,
elems=[
   ('idf', 0b000),
   ('ld', 0b001),
   ('st', 0b010),
   ('smp', 0b100),
   ('atomic', 0b101),
])

F_CACHEMODE_LD = field_enum_type(
name='cachemode_ld', num_bits=2,
elems=[
   ('normal', 0b00),
   ('bypass', 0b01),
   ('force_line_fill', 0b10),
])

F_CACHEMODE_ST = field_enum_type(
name='cachemode_st', num_bits=2,
elems=[
   ('write_through', 0b00),
   ('write_back', 0b01),
   ('write_back_lazy', 0b10),
])

F_DSIZE = field_enum_type(
name='dsize', num_bits=2,
elems=[
   ('8bit', 0b00),
   ('16bit', 0b01),
   ('32bit', 0b10),
])

F_DMN = field_enum_type(
name='dmn', num_bits=2,
elems=[
   ('1d', 0b01),
   ('2d', 0b10),
   ('3d', 0b11),
])

F_LODM = field_enum_type(
name='lodm', num_bits=2,
elems=[
   ('normal', 0b00),
   ('bias', 0b01),
   ('replace', 0b10),
   ('gradients', 0b11),
])

F_SBMODE = field_enum_type(
name='sbmode', num_bits=2,
elems=[
   ('none', 0b00),
   ('data', 0b01),
   ('info', 0b10),
   ('both', 0b11),
])

F_ATOMIC_OP = field_enum_type(
name='atomic_op', num_bits=4,
elems=[
   ('add', 0b0000),
   ('sub', 0b0001),
   ('xchg', 0b0010),
   ('umin', 0b0100),
   ('imin', 0b0101),
   ('umax', 0b0110),
   ('imax', 0b0111),
   ('and', 0b1000),
   ('or', 0b1001),
   ('xor', 0b1010),
])

I_BACKEND = bit_set(
name='backend',
pieces=[
   ('backend_op', (0, '7:5')),

   # uvsw
   ('dsel', (0, '4')),
   ('imm_uvsw', (0, '3')),
   ('uvsw_op', (0, '2:0')),

   ('rsvd1', (1, '7:3')), # Common
   ('srcsel', (1, '2:0')), # Common

   ('imm_addr_uvsw', (1, '7:0')),

   ('rsvd1_uvsw_', (1, '7:2')),
   ('stream_id', (1, '1:0')),

   # movmsk
   ('msk_op', (0, '4')),
   ('sm', (0, '3')),
   ('msk_mode', (0, '2:0')),

   # phas
   ('exeaddrsrc', (0, '4:2')),
   ('phas_end', (0, '1')),
   ('phas_type', (0, '0')),

   ('phas_rate', (1, '7:6')),
   ('commontmp', (1, '5:0')),

   # setl
   ('rsvd0_setl', (0, '4:1')),
   ('ressel', (0, '0')),

   # vistest
   ('rsvd0_vistest', (0, '4:3')),
   ('pwen_vistest', (0, '2')),
   ('vistest_op', (0, '1')),
   ('ifb', (0, '0')),

   # fitr
   ('p_fitr', (0, '4')),
   ('drc', (0, '3')), # Common
   ('rsvd0_fitr', (0, '2')),
   ('fitr_mode', (0, '1:0')),

   ('rsvd1_fitr', (1, '7:5')),
   ('sat_fitr', (1, '4')),
   ('count_fitr', (1, '3:0')),

   # emitpix
   ('rsvd0_emitpix', (0, '4:2')),
   ('freep', (0, '1')),
   ('rsvd0_emitpix_', (0, '0')),

   # dma
   ('dma_op', (0, '2:0')),
   ('rsvd0_dma', (0, '4')),

   ## ld/st
   ('immbl', (0, '4')),

   ('srcseladd_ldst', (1, '7:5')),
   ('srcselbl_ldst', (1, '4:2')),
   ('burstlen_2_0', (1, '4:2')),
   ('cachemode_ldst', (1, '1:0')),

   ('tiled', (2, '7')),
   ('srcseldata', (2, '6:4')),
   ('dsize', (2, '3:2')),
   ('rsvd2_ldst', (2, '1')),
   ('burstlen_3', (2, '0')),

   ('rsvd3_st', (3, '7:3')),
   ('srcmask', (3, '2:0')),

   ## smp
   ('fcnorm', (0, '4')),

   ('extb', (1, '7')),
   ('dmn', (1, '6:5')),
   ('exta', (1, '4')),
   ('chan', (1, '3:2')),
   ('lodm', (1, '1:0')),

   ('pplod', (2, '7')),
   ('proj', (2, '6')),
   ('sbmode', (2, '5:4')),
   ('nncoords', (2, '3')),
   ('sno', (2, '2')),
   ('soo', (2, '1')),
   ('tao', (2, '0')),

   ('rsvd3_smp', (3, '7:5')),
   ('f16', (3, '4')),
   ('swap', (3, '3')),
   ('cachemode_smp', (3, '2:1')),
   ('smp_w', (3, '0')),

   ## atomic
   ('atomic_op', (1, '7:4')),
   ('rsvd1_atomic', (1, '3')),

   ('rsvd2_atomic', (2, '7:3')),
   ('dstsel', (2, '2:0')),
],
fields=[
   ('backend_op', (F_BACKEND_OP, ['backend_op'])),

   # uvsw
   ('dsel', (F_DSEL, ['dsel'])),
   ('imm_uvsw', (F_BOOL, ['imm_uvsw'])),
   ('uvsw_op', (F_UVSW_OP, ['uvsw_op'])),

   ('rsvd1', (F_UINT5, ['rsvd1'], 0)),
   ('srcsel', (F_SRCSEL, ['srcsel'])),

   ('imm_addr_uvsw', (F_UINT8, ['imm_addr_uvsw'])),

   ('rsvd1_uvsw_', (F_UINT6, ['rsvd1_uvsw_'], 0)),
   ('stream_id', (F_UINT2, ['stream_id'])),

   # movmsk
   ('msk_op', (F_MSK_OP, ['msk_op'])),
   ('sm', (F_BOOL, ['sm'])),
   ('msk_mode', (F_MSK_MODE, ['msk_mode'])),

   # phas
   ('exeaddrsrc', (F_SRCSEL, ['exeaddrsrc'])),
   ('phas_end', (F_BOOL, ['phas_end'])),
   ('phas_type', (F_PHAS_TYPE, ['phas_type'])),

   ('phas_rate', (F_PHAS_RATE, ['phas_rate'])),
   ('commontmp', (F_UINT6MUL4, ['commontmp'])),

   # setl
   ('rsvd0_setl', (F_UINT4, ['rsvd0_setl'], 0)),
   ('ressel', (F_DSEL, ['ressel'])),

   # vistest
   ('rsvd0_vistest', (F_UINT2, ['rsvd0_vistest'], 0)),
   ('pwen_vistest', (F_BOOL, ['pwen_vistest'])),
   ('vistest_op', (F_VISTEST_OP, ['vistest_op'])),
   ('ifb', (F_BOOL, ['ifb'])),

   # fitr
   ('p_fitr', (F_PERSP_CTL1, ['p_fitr'])),
   ('drc', (F_UINT1, ['drc'])),
   ('rsvd0_fitr', (F_UINT1, ['rsvd0_fitr'], 0)),
   ('iter_mode', (F_ITER_MODE, ['fitr_mode'])),

   ('rsvd1_fitr', (F_UINT3, ['rsvd1_fitr'], 0)),
   ('sat_fitr', (F_BOOL, ['sat_fitr'])),
   ('count_fitr', (F_UINT4_POS_WRAP, ['count_fitr'])),

   # emitpix
   ('rsvd0_emitpix', (F_UINT3, ['rsvd0_emitpix'], 0)),
   ('freep', (F_BOOL, ['freep'])),
   ('rsvd0_emitpix_', (F_UINT1, ['rsvd0_emitpix_'], 0)),

   # dma
   ('rsvd0_dma', (F_UINT1, ['rsvd0_dma'], 0)),
   ('dma_op', (F_DMA_OP, ['dma_op'])),

   ## ld/st
   ('immbl', (F_BOOL, ['immbl'])),

   ('srcseladd_ldst', (F_SRCSEL, ['srcseladd_ldst'])),
   ('srcselbl_ldst', (F_SRCSEL, ['srcselbl_ldst'])),

   ('cachemode_ld', (F_CACHEMODE_LD, ['cachemode_ldst'])),
   ('cachemode_st', (F_CACHEMODE_ST, ['cachemode_ldst'])),

   ('rsvd2_ld', (F_UINT7, ['tiled', 'srcseldata', 'dsize', 'rsvd2_ldst'], 0)),
   ('rsvd2_st', (F_UINT1, ['rsvd2_ldst'], 0)),

   ('tiled', (F_BOOL, ['tiled'])),
   ('srcseldata', (F_SRCSEL, ['srcseldata'])),
   ('dsize', (F_DSIZE, ['dsize'])),

   ('burstlen', (F_UINT4_POS_WRAP, ['burstlen_3', 'burstlen_2_0'])),
   ('rsvd2_st_', (F_UINT1, ['burstlen_3'], 0)),

   ('rsvd3_st', (F_UINT5, ['rsvd3_st'], 0)),
   ('srcmask', (F_SRCSEL, ['srcmask'])),

   ## smp
   ('fcnorm', (F_BOOL, ['fcnorm'])),

   ('extb', (F_BOOL, ['extb'])),
   ('dmn', (F_DMN, ['dmn'])),
   ('exta', (F_BOOL, ['exta'])),
   ('chan', (F_UINT2_POS_INC, ['chan'])),
   ('lodm', (F_LODM, ['lodm'])),

   ('pplod', (F_BOOL, ['pplod'])),
   ('proj', (F_BOOL, ['proj'])),
   ('sbmode', (F_SBMODE, ['sbmode'])),
   ('nncoords', (F_BOOL, ['nncoords'])),
   ('sno', (F_BOOL, ['sno'])),
   ('soo', (F_BOOL, ['soo'])),
   ('tao', (F_BOOL, ['tao'])),

   ('rsvd3_smp', (F_UINT3, ['rsvd3_smp'], 0)),
   ('f16', (F_BOOL, ['f16'])),
   ('swap', (F_SCHED_CTRL1, ['swap'])),
   ('cachemode_smp_ld', (F_CACHEMODE_LD, ['cachemode_smp'])),
   ('cachemode_smp_st', (F_CACHEMODE_ST, ['cachemode_smp'])),
   ('smp_w', (F_BOOL, ['smp_w'])),

   ## atomic
   ('atomic_op', (F_ATOMIC_OP, ['atomic_op'])),
   ('rsvd1_atomic', (F_UINT1, ['rsvd1_atomic'], 0)),

   ('rsvd2_atomic', (F_UINT5, ['rsvd2_atomic'], 0)),
   ('dstsel', (F_SRCSEL, ['dstsel'])),
])

I_UVSW_WRITE_REG = bit_struct(
name='uvsw_write_reg',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel'),
   ('imm', 'imm_uvsw', 0),
   ('uvsw_op', 'uvsw_op', 'write'),

   ('rsvd1', 'rsvd1'),
   ('srcsel', 'srcsel'),
])

I_UVSW_WRITE_IMM = bit_struct(
name='uvsw_write_imm',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel'),
   ('imm', 'imm_uvsw', 1),
   ('uvsw_op', 'uvsw_op', 'write'),

   ('imm_addr', 'imm_addr_uvsw'),
])

I_UVSW_EMIT = bit_struct(
name='uvsw_emit',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel', 0),
   ('imm', 'imm_uvsw', 0),
   ('uvsw_op', 'uvsw_op', 'emit'),
])

I_UVSW_EMIT_STREAM = bit_struct(
name='uvsw_emit_imm',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel', 0),
   ('imm', 'imm_uvsw', 1),
   ('uvsw_op', 'uvsw_op', 'emit'),

   ('rsvd1', 'rsvd1_uvsw_'),
   ('stream_id', 'stream_id'),
])

I_UVSW_CUT = bit_struct(
name='uvsw_cut',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel', 0),
   ('imm', 'imm_uvsw', 0),
   ('uvsw_op', 'uvsw_op', 'cut'),
])

I_UVSW_CUT_STREAM = bit_struct(
name='uvsw_cut_imm',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel', 0),
   ('imm', 'imm_uvsw', 1),
   ('uvsw_op', 'uvsw_op', 'cut'),

   ('rsvd1', 'rsvd1_uvsw_'),
   ('stream_id', 'stream_id'),
])

I_UVSW_EMIT_CUT = bit_struct(
name='uvsw_emit_cut',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel', 0),
   ('imm', 'imm_uvsw', 0),
   ('uvsw_op', 'uvsw_op', 'emit_cut'),
])

I_UVSW_EMIT_CUT_STREAM = bit_struct(
name='uvsw_emit_cut_imm',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel', 0),
   ('imm', 'imm_uvsw', 1),
   ('uvsw_op', 'uvsw_op', 'emit_cut'),

   ('rsvd1', 'rsvd1_uvsw_'),
   ('stream_id', 'stream_id'),
])

I_UVSW_ENDTASK = bit_struct(
name='uvsw_endtask',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel', 0),
   ('imm', 'imm_uvsw', 0),
   ('uvsw_op', 'uvsw_op', 'endtask'),
])

I_UVSW_EMIT_ENDTASK = bit_struct(
name='uvsw_emit_endtask',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel', 0),
   ('imm', 'imm_uvsw', 0),
   ('uvsw_op', 'uvsw_op', 'emit_endtask'),
])

I_UVSW_WRITE_EMIT_ENDTASK_REG = bit_struct(
name='uvsw_write_emit_endtask_reg',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel'),
   ('imm', 'imm_uvsw', 0),
   ('uvsw_op', 'uvsw_op', 'write_emit_endtask'),

   ('rsvd1', 'rsvd1'),
   ('srcsel', 'srcsel'),
])

I_UVSW_WRITE_EMIT_ENDTASK_IMM = bit_struct(
name='uvsw_write_emit_endtask_imm',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'uvsw'),

   ('dsel', 'dsel'),
   ('imm', 'imm_uvsw', 1),
   ('uvsw_op', 'uvsw_op', 'write_emit_endtask'),

   ('imm_addr', 'imm_addr_uvsw'),
])

I_MOVMSK = bit_struct(
name='movmsk',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'msk'),

   ('msk_op', 'msk_op', 'mov'),
   ('sm', 'sm', 0),
   ('msk_mode', 'msk_mode', 'icm'),
])

I_MOVMSK_SM = bit_struct(
name='movmsk_sm',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'msk'),

   ('msk_op', 'msk_op', 'mov'),
   ('sm', 'sm', 1),
   ('msk_mode', 'msk_mode', 'icm'),

   ('rsvd1', 'rsvd1'),
   ('srcsel', 'srcsel'),
])

I_SAVMSK = bit_struct(
name='savmsk',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'msk'),

   ('msk_op', 'msk_op', 'sav'),
   ('sm', 'sm', 0),
   ('msk_mode', 'msk_mode'),
])

I_PHAS_REG = bit_struct(
name='phas_reg',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'phas'),

   ('exeaddrsrc', 'exeaddrsrc'),
   ('end', 'phas_end', 0),
   ('type', 'phas_type', 'reg'),

   ('rsvd1', 'rsvd1'),
   ('paramsrc', 'srcsel'),
])

I_PHAS_IMM = bit_struct(
name='phas_imm',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'phas'),

   ('exeaddrsrc', 'exeaddrsrc'),
   ('end', 'phas_end'),
   ('type', 'phas_type', 'imm'),

   ('rate', 'phas_rate'),
   ('commontmp', 'commontmp'),
])

I_SETL = bit_struct(
name='setl',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'setl'),

   ('rsvd0', 'rsvd0_setl'),
   ('ressel', 'ressel'),
])

I_VISTEST_DEPTHF = bit_struct(
name='vistest_depthf',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'vistest'),

   ('rsvd0', 'rsvd0_vistest'),
   ('pwen', 'pwen_vistest', 0),
   ('vistest_op', 'vistest_op', 'depthf'),
   ('ifb', 'ifb', 0),
])

I_VISTEST_ATST = bit_struct(
name='vistest_atst',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'vistest'),

   ('rsvd0', 'rsvd0_vistest'),
   ('pwen', 'pwen_vistest'),
   ('vistest_op', 'vistest_op', 'atst'),
   ('ifb', 'ifb'),
])

I_FITR = bit_struct(
name='fitr',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'fitr'),

   ('p', 'p_fitr'),
   ('drc', 'drc'),
   ('rsvd0', 'rsvd0_fitr'),
   ('iter_mode', 'iter_mode'),

   ('rsvd1', 'rsvd1_fitr'),
   ('sat', 'sat_fitr'),
   ('count', 'count_fitr'),

])

I_EMITPIX = bit_struct(
name='emitpix',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'emit'),

   ('rsvd0', 'rsvd0_emitpix'),
   ('freep', 'freep'),
   ('rsvd0_', 'rsvd0_emitpix_'),
])

I_IDF = bit_struct(
name='idf',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('rsvd0', 'rsvd0_dma'),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'idf'),

   ('rsvd1', 'rsvd1'),
   ('srcseladd', 'srcsel'),
])

I_LD_IMMBL = bit_struct(
name='ld_immbl',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('immbl', 'immbl', 1),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'ld'),

   ('srcseladd', 'srcseladd_ldst'),
   ('burstlen', 'burstlen'),
   ('cachemode_ld', 'cachemode_ld'),

   ('rsvd2', 'rsvd2_ld'),
])

I_LD_REGBL = bit_struct(
name='ld_regbl',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('immbl', 'immbl', 0),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'ld'),

   ('srcseladd', 'srcseladd_ldst'),
   ('srcselbl', 'srcselbl_ldst'),
   ('cachemode_ld', 'cachemode_ld'),
])

I_ST_IMMBL = bit_struct(
name='st_immbl',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('immbl', 'immbl', 1),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'st'),

   ('srcseladd', 'srcseladd_ldst'),
   ('burstlen', 'burstlen'),
   ('cachemode_st', 'cachemode_st'),

   ('tiled', 'tiled', 0),
   ('srcseldata', 'srcseldata'),
   ('dsize', 'dsize'),
   ('rsvd2', 'rsvd2_st'),
])

I_ST_REGBL = bit_struct(
name='st_regbl',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('immbl', 'immbl', 0),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'st'),

   ('srcseladd', 'srcseladd_ldst'),
   ('srcselbl', 'srcselbl_ldst'),
   ('cachemode_st', 'cachemode_st'),

   ('tiled', 'tiled', 0),
   ('srcseldata', 'srcseldata'),
   ('dsize', 'dsize'),
   ('rsvd2', 'rsvd2_st'),
   ('rsvd2_', 'rsvd2_st_'),
])

I_ST_IMMBL_TILED = bit_struct(
name='st_immbl_tiled',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('immbl', 'immbl', 1),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'st'),

   ('srcseladd', 'srcseladd_ldst'),
   ('burstlen', 'burstlen'),
   ('cachemode_st', 'cachemode_st'),

   ('tiled', 'tiled', 1),
   ('srcseldata', 'srcseldata'),
   ('dsize', 'dsize'),
   ('rsvd2', 'rsvd2_st'),

   ('rsvd3', 'rsvd3_st'),
   ('srcmask', 'srcmask'),
])

I_ST_REGBL_TILED = bit_struct(
name='st_regbl_tiled',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('immbl', 'immbl', 0),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'st'),

   ('srcseladd', 'srcseladd_ldst'),
   ('srcselbl', 'srcselbl_ldst'),
   ('cachemode_st', 'cachemode_st'),

   ('tiled', 'tiled', 1),
   ('srcseldata', 'srcseldata'),
   ('dsize', 'dsize'),
   ('rsvd2', 'rsvd2_st'),
   ('rsvd2_', 'rsvd2_st_'),

   ('rsvd3', 'rsvd3_st'),
   ('srcmask', 'srcmask'),
])

I_SMP_BRIEF = bit_struct(
name='smp_brief',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('fcnorm', 'fcnorm'),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'smp'),

   ('extb', 'extb', 0),
   ('dmn', 'dmn'),
   ('exta', 'exta', 0),
   ('chan', 'chan'),
   ('lodm', 'lodm'),
])

I_SMP_EXTA = bit_struct(
name='smp_exta',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('fcnorm', 'fcnorm'),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'smp'),

   ('extb', 'extb', 0),
   ('dmn', 'dmn'),
   ('exta', 'exta', 1),
   ('chan', 'chan'),
   ('lodm', 'lodm'),

   ('pplod', 'pplod'),
   ('proj', 'proj'),
   ('sbmode', 'sbmode'),
   ('nncoords', 'nncoords'),
   ('sno', 'sno'),
   ('soo', 'soo'),
   ('tao', 'tao'),
])

I_SMP_EXTB = bit_struct(
name='smp_extb',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('fcnorm', 'fcnorm'),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'smp'),

   ('extb', 'extb', 1),
   ('dmn', 'dmn'),
   ('exta', 'exta', 0),
   ('chan', 'chan'),
   ('lodm', 'lodm'),

   ('rsvd3', 'rsvd3_smp'),
   ('f16', 'f16'),
   ('swap', 'swap'),
   ('cachemode_ld', 'cachemode_smp_ld'),
   ('w', 'smp_w', 0),
])

I_SMP_EXTB_W = bit_struct(
name='smp_extb_w',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('fcnorm', 'fcnorm'),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'smp'),

   ('extb', 'extb', 1),
   ('dmn', 'dmn'),
   ('exta', 'exta', 0),
   ('chan', 'chan'),
   ('lodm', 'lodm'),

   ('rsvd3', 'rsvd3_smp'),
   ('f16', 'f16'),
   ('swap', 'swap'),
   ('cachemode_st', 'cachemode_smp_st'),
   ('w', 'smp_w', 1),
])

I_SMP_EXTAB = bit_struct(
name='smp_extab',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('fcnorm', 'fcnorm'),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'smp'),

   ('extb', 'extb', 1),
   ('dmn', 'dmn'),
   ('exta', 'exta', 1),
   ('chan', 'chan'),
   ('lodm', 'lodm'),

   ('pplod', 'pplod'),
   ('proj', 'proj'),
   ('sbmode', 'sbmode'),
   ('nncoords', 'nncoords'),
   ('sno', 'sno'),
   ('soo', 'soo'),
   ('tao', 'tao'),

   ('rsvd3', 'rsvd3_smp'),
   ('f16', 'f16'),
   ('swap', 'swap'),
   ('cachemode_ld', 'cachemode_smp_ld'),
   ('w', 'smp_w', 0),
])

I_SMP_EXTAB_W = bit_struct(
name='smp_extab_w',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('fcnorm', 'fcnorm'),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'smp'),

   ('extb', 'extb', 1),
   ('dmn', 'dmn'),
   ('exta', 'exta', 1),
   ('chan', 'chan'),
   ('lodm', 'lodm'),

   ('pplod', 'pplod'),
   ('proj', 'proj'),
   ('sbmode', 'sbmode'),
   ('nncoords', 'nncoords'),
   ('sno', 'sno'),
   ('soo', 'soo'),
   ('tao', 'tao'),

   ('rsvd3', 'rsvd3_smp'),
   ('f16', 'f16'),
   ('swap', 'swap'),
   ('cachemode_st', 'cachemode_smp_st'),
   ('w', 'smp_w', 1),
])

I_ATOMIC = bit_struct(
name='atomic',
bit_set=I_BACKEND,
field_mappings=[
   ('backend_op', 'backend_op', 'dma'),

   ('rsvd0', 'rsvd0_dma'),
   ('drc', 'drc'),
   ('dma_op', 'dma_op', 'atomic'),

   ('atomic_op', 'atomic_op'),
   ('rsvd1', 'rsvd1_atomic'),
   ('srcsel', 'srcsel'),

   ('rsvd2', 'rsvd2_atomic'),
   ('dstsel', 'dstsel'),
])

# Bitwise ALU ops.
F_COUNT_SRC = field_enum_type(
name='count_src', num_bits=1,
elems=[
   ('s2', 0b0),
   ('ft2', 0b1),
])

F_COUNT_OP = field_enum_type(
name='count_op', num_bits=2,
elems=[
   ('cbs', 0b00),
   ('ftb', 0b01),
   ('byp', 0b10),
])

F_BITMASK_SRC_OP = field_enum_type(
name='bitmask_src_op', num_bits=1,
elems=[
   ('byp', 0b0),
   ('msk', 0b1),
])

F_BITMASK_IMM_OP = field_enum_type(
name='bitmask_imm_op', num_bits=1,
elems=[
   ('byp16', 0b0),
   ('byp32', 0b1),
])

F_SHIFT1_OP = field_enum_type(
name='shift1_op', num_bits=2,
elems=[
   ('byp', 0b00),
   ('shfl', 0b01),
   ('rev', 0b10),
   ('lsl', 0b11),
])

F_LOGICAL_OP = field_enum_type(
name='logical_op', num_bits=3,
elems=[
   ('or', 0b000),
   ('and', 0b001),
   ('xor', 0b010),
   ('nor', 0b100),
   ('nand', 0b101),
   ('xnor', 0b110),
   ('byp', 0b111),
])

F_BW_TST_SRC = field_enum_type(
name='bw_tst_src', num_bits=1,
elems=[
   ('ft5', 0b0),
   ('ft3', 0b1),
])

F_BW_TST_OP = field_enum_type(
name='bw_tst_op', num_bits=1,
elems=[
   ('z', 0b0),
   ('nz', 0b1),
])

F_SHIFT2_OP = field_enum_type(
name='shift2_op', num_bits=3,
elems=[
   ('lsl', 0b000),
   ('shr', 0b001),
   ('rol', 0b010),
   ('cps', 0b011),
   ('asr_twb', 0b100),
   ('asr_pwb', 0b101),
   ('asr_mtb', 0b110),
   ('asr_ftb', 0b111),
])

I_BITWISE = bit_set(
name='bitwise',
pieces=[
   ('ph0', (0, '7')),
   ('ph1', (0, '6')),

   # phase 0
   ('csrc', (0, '6')),
   ('cnt', (0, '5')),
   ('ext0', (0, '4')),
   ('shft1', (0, '3:2')),
   ('cnt_byp', (0, '1')),
   ('bm', (0, '0')),

   ('imm_7_0', (1, '7:0')),
   ('imm_15_8', (2, '7:0')),
   ('imm_23_16', (3, '7:0')),
   ('imm_31_24', (4, '7:0')),

   # phase 1
   ('mskb', (0, '5')),
   ('mska', (0, '3')),
   ('logical_op', (0, '2:0')),

   # phase 2
   ('pwen', (0, '5')),
   ('tsrc', (0, '4')),
   ('top', (0, '3')),
   ('shft2', (0, '2:0')),
],
fields=[
   ('ph0', (F_BOOL, ['ph0'])),
   ('ph1', (F_BOOL, ['ph1'])),

   # phase 0
   ('count_src', (F_COUNT_SRC, ['csrc'])),
   ('bitmask_imm', (F_BOOL, ['ext0'])),
   ('count_op', (F_COUNT_OP, ['cnt_byp', 'cnt'])),
   ('bitmask_src_op', (F_BITMASK_SRC_OP, ['bm'])),
   ('bitmask_imm_op', (F_BITMASK_IMM_OP, ['bm'])),
   ('shift1_op', (F_SHIFT1_OP, ['shft1'])),

   ('imm16', (F_UINT16, ['imm_15_8', 'imm_7_0'])),
   ('imm32', (F_UINT32, ['imm_31_24', 'imm_23_16', 'imm_15_8', 'imm_7_0'])),

   # phase 1
   ('mskb', (F_BOOL, ['mskb'])),
   ('rsvd0_ph1', (F_UINT1, ['ext0'], 0)),
   ('mska', (F_BOOL, ['mska'])),
   ('logical_op', (F_LOGICAL_OP, ['logical_op'])),

   # phase 2
   ('pwen', (F_BOOL, ['pwen'])),
   ('tst_src', (F_BW_TST_SRC, ['tsrc'])),
   ('tst_op', (F_BW_TST_OP, ['top'])),
   ('shift2_op', (F_SHIFT2_OP, ['shft2'])),
])

I_PHASE0_SRC = bit_struct(
name='phase0_src',
bit_set=I_BITWISE,
field_mappings=[
   ('ph0', 'ph0', 0),

   ('count_src', 'count_src'),
   ('bitmask_imm', 'bitmask_imm', 0),
   ('count_op', 'count_op'),
   ('bitmask_src_op', 'bitmask_src_op'),
   ('shift1_op', 'shift1_op'),
])

I_PHASE0_IMM16 = bit_struct(
name='phase0_imm16',
bit_set=I_BITWISE,
field_mappings=[
   ('ph0', 'ph0', 0),

   ('count_src', 'count_src'),
   ('bitmask_imm', 'bitmask_imm', 1),
   ('count_op', 'count_op'),
   ('bitmask_imm_op', 'bitmask_imm_op', 'byp16'),
   ('shift1_op', 'shift1_op'),
   ('imm16', 'imm16'),
])

I_PHASE0_IMM32 = bit_struct(
name='phase0_imm32',
bit_set=I_BITWISE,
field_mappings=[
   ('ph0', 'ph0', 0),

   ('count_src', 'count_src'),
   ('bitmask_imm', 'bitmask_imm', 1),
   ('count_op', 'count_op'),
   ('bitmask_imm_op', 'bitmask_imm_op', 'byp32'),
   ('shift1_op', 'shift1_op'),
   ('imm32', 'imm32'),
])

I_PHASE1 = bit_struct(
name='phase1',
bit_set=I_BITWISE,
field_mappings=[
   ('ph0', 'ph0', 0),
   ('ph1', 'ph1', 1),

   ('mskb', 'mskb'),
   ('rsvd0', 'rsvd0_ph1'),
   ('mska', 'mska'),
   ('logical_op', 'logical_op'),
])

I_PHASE2 = bit_struct(
name='phase2',
bit_set=I_BITWISE,
field_mappings=[
   ('ph0', 'ph0', 0),
   ('ph1', 'ph1', 0),

   ('pwen', 'pwen'),
   ('tst_src', 'tst_src'),
   ('bw_tst_op', 'tst_op'),
   ('shift2_op', 'shift2_op'),
])

# Control ALU ops.
F_PCND = field_enum_type(
name='pcnd', num_bits=2,
elems=[
   ('always', 0b00),
   ('p0_true', 0b01),
   ('never', 0b10),
   ('p0_false', 0b11),
])

F_CNDINST = field_enum_type(
name='cndinst', num_bits=3,
elems=[
   ('st', 0b000),
   ('ef', 0b001),
   ('sm', 0b010),
   ('lt', 0b011),
   ('end', 0b100),
   ('setl_b', 0b101),
   ('lpc', 0b110),
   ('setl_a', 0b111),
])

F_LR = field_enum_type(
name='lr', num_bits=2,
elems=[
   ('release', 0b00),
   ('release_sleep', 0b01),
   ('release_wakeup', 0b10),
   ('lock', 0b11),
])

F_TGT = field_enum_type(
name='tgt', num_bits=1,
elems=[
   ('coeff', 0b0),
   ('shared', 0b1),
])

F_BPRED = field_enum_type(
name='bpred', num_bits=2,
elems=[
   ('cc', 0b00),
   ('allp', 0b01),
   ('anyp', 0b10),
])

I_CTRL = bit_set(
name='ctrl',
pieces=[
   # Branch
   ('link', (0, '4')),
   ('bpred', (0, '3:2')),
   ('abs', (0, '1')),
   ('rsvd0_branch_', (0, '0')),

   ('branch_offset_7_1', (1, '7:1')),
   ('rsvd1_branch', (1, '0')),

   ('branch_offset_15_8', (2, '7:0')),
   ('branch_offset_23_16', (3, '7:0')),
   ('branch_offset_31_24', (4, '7:0')),

   # Conditional.
   ('rsvd0_cnd', (0, '7')),
   ('adjust', (0, '6:5')),
   ('pcnd', (0, '4:3')),
   ('cndinst', (0, '2:0')),

   # Mutex.
   ('lr', (0, '7:6')),
   ('rsvd0_mutex', (0, '5:4')),
   ('id', (0, '3:0')),

   # NOP
   ('rsvd0_nop', (0, '7:0')),

   # SBO
   ('sbo_offset_6_0', (0, '7:1')),
   ('tgt', (0, '0')),

   ('rsvd1_sbo', (1, '7:1')),
   ('sbo_offset_7', (1, '0')),

   # ditr
   ('dest_7_0', (0, '7:0')),

   ('coff_7_2', (1, '7:2')),
   ('p_itr', (1, '1:0')),

   ('woff_b2_7_2', (2, '7:2')),
   ('ditr_mode', (2, '1:0')),

   ('itr_count_b3', (3, '7:4')),
   ('coff_idx_ctrl_b3', (3, '3:2')),
   ('woff_idx_ctrl_b3', (3, '1:0')),

   ('rsvd4_7_ditr', (4, '7')),
   ('f16_b4', (4, '6')),
   ('rsvd4_5_ditr', (4, '5:4')),
   ('sched_ctrl_b4', (4, '3:2')),
   ('drc_b4', (4, '1')),
   ('sat', (4, '0')),

   # itrsmp
   ('texoff_7_2', (2, '7:2')),
   ('drc_b2', (2, '1')),
   ('itrsmp_mode_0', (2, '0')),

   ('smpoff_7_2', (3, '7:2')),
   ('dmn', (3, '1:0')),

   ('woff_b4_7_2', (4, '7:2')),
   ('proj', (4, '1')),

   ('ext5', (5, '7')),
   ('f16_b5', (5, '6')),
   ('sched_ctrl_b5', (5, '5:4')),
   ('chan', (5, '3:2')),
   ('nncoords', (5, '1')),
   ('fcnorm', (5, '0')),

   ('ext6', (6, '7')),
   ('itr_count_b6', (6, '6:3')),
   ('comparison', (6, '2')),
   ('rsvd6_itrsmp', (6, '1:0')),

   ('rsvd7_itrsmp', (7, '7:5')),
   ('coff_idx_ctrl_b7', (7, '4:3')),
   ('woff_idx_ctrl_b7', (7, '2:1')),
   ('itrsmp_mode_1', (7, '0')),
],
fields=[
   # Branch
   ('rsvd_branch', (F_UINT5, ['rsvd1_branch', 'rsvd0_branch_', 'adjust', 'rsvd0_cnd'], 0)),
   ('link', (F_BOOL, ['link'])),
   ('bpred', (F_BPRED, ['bpred'])),
   ('abs', (F_BOOL, ['abs'])),
   ('branch_offset', (F_OFFSET31, ['branch_offset_31_24', 'branch_offset_23_16', 'branch_offset_15_8', 'branch_offset_7_1'])),

   # Conditional.
   ('rsvd_cnd', (F_UINT1, ['rsvd0_cnd'], 0)),
   ('adjust', (F_UINT2, ['adjust'])),
   ('pcnd', (F_PCND, ['pcnd'])),
   ('cndinst', (F_CNDINST, ['cndinst'])),

   # Mutex.
   ('lr', (F_LR, ['lr'])),
   ('rsvd_mutex', (F_UINT2, ['rsvd0_mutex'], 0)),
   ('id', (F_UINT4, ['id'])),

   # NOP
   ('rsvd_nop', (F_UINT8, ['rsvd0_nop'], 0)),

   # SBO
   ('rsvd_sbo', (F_UINT7, ['rsvd1_sbo'], 0)),
   ('tgt', (F_TGT, ['tgt'])),
   ('sbo_offset', (F_UINT8, ['sbo_offset_7', 'sbo_offset_6_0'])),

   # ditr
   ('dest', (F_UINT8, ['dest_7_0'])),

   ('coff', (F_UINT6MUL4, ['coff_7_2'])),
   ('p_itr', (F_PERSP_CTL, ['p_itr'])),

   ('woff_b2', (F_UINT6MUL4, ['woff_b2_7_2'])),
   ('ditr_mode', (F_ITER_MODE, ['ditr_mode'])),

   ('itr_count_b3', (F_UINT4_POS_WRAP, ['itr_count_b3'])),
   ('coff_idx_ctrl_b3', (F_IDX_CTRL, ['coff_idx_ctrl_b3'])),
   ('woff_idx_ctrl_b3', (F_IDX_CTRL, ['woff_idx_ctrl_b3'])),

   ('rsvd4_7_ditr', (F_UINT1, ['rsvd4_7_ditr'], 0)),
   ('f16_b4', (F_BOOL, ['f16_b4'])),
   ('rsvd4_5_ditr', (F_UINT2, ['rsvd4_5_ditr'], 0)),
   ('sched_ctrl_b4', (F_SCHED_CTRL, ['sched_ctrl_b4'])),
   ('drc_b4', (F_UINT1, ['drc_b4'])),
   ('sat', (F_BOOL, ['sat'])),

   # itrsmp
   ('texoff', (F_UINT6MUL4, ['texoff_7_2'])),
   ('drc_b2', (F_UINT1, ['drc_b2'])),
   ('itrsmp_mode1', (F_ITER_MODE1, ['itrsmp_mode_0'])),

   ('smpoff', (F_UINT6MUL4, ['smpoff_7_2'])),
   ('dmn', (F_DMN, ['dmn'])),

   ('woff_b4', (F_UINT6MUL4, ['woff_b4_7_2'])),
   ('proj', (F_BOOL, ['proj'])),

   ('ext5', (F_BOOL, ['ext5'])),
   ('f16_b5', (F_BOOL, ['f16_b5'])),
   ('sched_ctrl_b5', (F_SCHED_CTRL, ['sched_ctrl_b5'])),
   ('chan', (F_UINT2_POS_INC, ['chan'])),
   ('nncoords', (F_BOOL, ['nncoords'])),
   ('fcnorm', (F_BOOL, ['fcnorm'])),

   ('ext6', (F_BOOL, ['ext6'])),
   ('itr_count_b6', (F_UINT4_POS_WRAP, ['itr_count_b6'])),
   ('comparison', (F_BOOL, ['comparison'])),
   ('rsvd6_itrsmp', (F_UINT2, ['rsvd6_itrsmp'], 0)),

   ('rsvd7_itrsmp', (F_UINT3, ['rsvd7_itrsmp'], 0)),
   ('coff_idx_ctrl_b7', (F_IDX_CTRL, ['coff_idx_ctrl_b7'])),
   ('woff_idx_ctrl_b7', (F_IDX_CTRL, ['woff_idx_ctrl_b7'])),
   ('itrsmp_mode', (F_ITER_MODE, ['itrsmp_mode_1', 'itrsmp_mode_0'])),
])

I_BRANCH = bit_struct(
name='branch',
bit_set=I_CTRL,
field_mappings=[
   ('rsvd', 'rsvd_branch'),

   ('link', 'link'),
   ('bpred', 'bpred'),
   ('abs', 'abs'),
   ('offset', 'branch_offset'),
])

I_LAPC = bit_struct(
name='lapc',
bit_set=I_CTRL,
field_mappings=[])

I_SAVL = bit_struct(
name='savl',
bit_set=I_CTRL,
field_mappings=[])

I_CND = bit_struct(
name='cnd',
bit_set=I_CTRL,
field_mappings=[
   ('rsvd', 'rsvd_cnd'),

   ('adjust', 'adjust'),
   ('pcnd', 'pcnd'),
   ('cndinst', 'cndinst'),
])

I_WOP = bit_struct(
name='wop',
bit_set=I_CTRL,
field_mappings=[])

I_WDF = bit_struct(
name='wdf',
bit_set=I_CTRL,
field_mappings=[])

I_MUTEX = bit_struct(
name='mutex',
bit_set=I_CTRL,
field_mappings=[
   ('rsvd', 'rsvd_mutex'),

   ('lr', 'lr'),
   ('id', 'id'),
])

I_NOP = bit_struct(
name='nop',
bit_set=I_CTRL,
field_mappings=[
   ('rsvd', 'rsvd_nop'),
])

# SBO.
I_SBO = bit_struct(
name='sbo',
bit_set=I_CTRL,
field_mappings=[
   ('rsvd', 'rsvd_sbo'),
   ('tgt', 'tgt'),
   ('offset', 'sbo_offset'),
])

I_DITR = bit_struct(
name='ditr',
bit_set=I_CTRL,
field_mappings=[
   'dest',

   'coff',
   ('p', 'p_itr'),

   ('woff', 'woff_b2'),
   ('mode', 'ditr_mode'),

   ('count', 'itr_count_b3'),
   ('coff_idx_ctrl', 'coff_idx_ctrl_b3'),
   ('woff_idx_ctrl', 'woff_idx_ctrl_b3'),

   ('rsvd0', 'rsvd4_7_ditr'),
   ('f16', 'f16_b4'),
   ('rsvd1', 'rsvd4_5_ditr'),
   ('sched_ctrl', 'sched_ctrl_b4'),
   ('drc', 'drc_b4'),
   'sat',
])

I_ITRSMP = bit_struct(
name='itrsmp',
bit_set=I_CTRL,
field_mappings=[
   'dest',

   'coff',
   ('p', 'p_itr'),

   'texoff',
   ('drc', 'drc_b2'),
   ('mode', 'itrsmp_mode1'),

   'smpoff',
   'dmn',

   ('woff', 'woff_b4'),
   'proj',
   'sat',

   ('ext5', 'ext5', False),
   ('f16', 'f16_b5'),
   ('sched_ctrl', 'sched_ctrl_b5'),
   'chan',
   'nncoords',
   'fcnorm',
])

I_ITRSMP_EXT = bit_struct(
name='itrsmp_ext',
bit_set=I_CTRL,
field_mappings=[
   'dest',

   'coff',
   ('p', 'p_itr'),

   'texoff',
   ('drc', 'drc_b2'),
   ('mode', 'itrsmp_mode1'),

   'smpoff',
   'dmn',

   ('woff', 'woff_b4'),
   'proj',
   'sat',

   ('ext5', 'ext5', True),
   ('f16', 'f16_b5'),
   ('sched_ctrl', 'sched_ctrl_b5'),
   'chan',
   'nncoords',
   'fcnorm',

   ('ext6', 'ext6', False),
   ('count', 'itr_count_b6'),
   'comparison',
   'rsvd6_itrsmp',
])

I_ITRSMP_EXT2 = bit_struct(
name='itrsmp_ext2',
bit_set=I_CTRL,
field_mappings=[
   'dest',

   'coff',
   ('p', 'p_itr'),

   'texoff',
   ('drc', 'drc_b2'),

   'smpoff',
   'dmn',

   ('woff', 'woff_b4'),
   'proj',
   'sat',

   ('ext5', 'ext5', True),
   ('f16', 'f16_b5'),
   ('sched_ctrl', 'sched_ctrl_b5'),
   'chan',
   'nncoords',
   'fcnorm',

   ('ext6', 'ext6', True),
   ('count', 'itr_count_b6'),
   'comparison',
   'rsvd6_itrsmp',

   'rsvd7_itrsmp',
   ('coff_idx_ctrl', 'coff_idx_ctrl_b7'),
   ('woff_idx_ctrl', 'woff_idx_ctrl_b7'),
   ('mode', 'itrsmp_mode'),
])
