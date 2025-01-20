# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from pco_pygen_common import *

OP_TYPE = enum_type('op_type', [
   'pseudo',
   'hw',
   'hw_direct',
])

MOD_TYPE = enum_type('mod_type', [
   'bool',
   'uint',
   'enum',
])

REF_TYPE = enum_type('ref_type', [
   ('null', '_'),
   ('ssa', '%'),
   ('reg', ''),
   ('idx_reg', ''),
   ('imm', ''),
   ('io', ''),
   ('pred', ''),
   ('drc', 'drc'),
])

FUNC_TYPE = enum_type('func_type', [
   'callable',
   'preamble',
   'entrypoint',
   'phase_change',
])

DTYPE = enum_type('dtype', [
   ('any', ''),
   ('unsigned', 'u'),
   ('signed', 'i'),
   ('float', 'f'),
])

BITS = enum_type('bits', [
   ('1', '1'),
   ('8', '8'),
   ('16', '16'),
   ('32', '32'),
   ('64', '64'),
])

REG_CLASS = enum_type('reg_class', [
   ('virt', '$'),
   ('temp', 'r'),
   ('vtxin', 'vi'),
   ('coeff', 'cf'),
   ('shared', 'sh'),
   ('index', 'idx'),
   ('spec', 'sr'),
   ('intern', 'i'),
   ('const', 'sc'),
   ('pixout', 'po'),
   ('global', 'g'),
   ('slot', 'sl'),
])

IO = enum_type('io', [
   ('s0', 's0'),
   ('s1', 's1'),
   ('s2', 's2'),

   ('s3', 's3'),
   ('s4', 's4'),
   ('s5', 's5'),

   ('w0', 'w0'),
   ('w1', 'w1'),

   ('is0', 'is0'),
   ('is1', 'is1'),
   ('is2', 'is2'),
   ('is3', 'is3'),
   ('is4', 'is4'),
   ('is5', 'is5'),

   ('ft0', 'ft0'),
   ('ft0h', 'ft0h'),
   ('ft1', 'ft1'),
   ('ft2', 'ft2'),
   ('fte', 'fte'),

   ('ft1_invert', '~ft1'),
   ('ft3', 'ft3'),
   ('ft4', 'ft4'),
   ('ft5', 'ft5'),

   ('ftt', 'ftt'),

   ('cout', 'cout'),
])

PRED = enum_type('pred', [
   ('pe', 'pe'),
   ('p0', 'p0'),

   ('always', 'if(1)'),
   ('p0_true', 'if(p0)'),
   ('never', 'if(0)'),
   ('p0_false', 'if(!p0)'),
])

DRC = enum_type('drc', [
   ('0', '0'),
   ('1', '1'),
   ('pending', '?'),
])

# Ref mods.
RM_ONEMINUS = ref_mod('oneminus', BaseType.bool)
RM_CLAMP = ref_mod('clamp', BaseType.bool)
RM_ABS = ref_mod('abs', BaseType.bool)
RM_NEG = ref_mod('neg', BaseType.bool)
RM_FLR = ref_mod('flr', BaseType.bool)

RM_ELEM = ref_mod_enum('elem', [
   'e0',
   'e1',
   'e2',
   'e3',
], is_bitset=True)

# Op mods.
OM_EXEC_CND = op_mod_enum('exec_cnd', [
   ('e1_zx', ''),
   ('e1_z1', 'if(p0)'),
   ('ex_zx', '(ignorepe)'),
   ('e1_z0', 'if(!p0)'),
], print_early=True, unset=True)
OM_RPT = op_mod('rpt', BaseType.uint, print_early=True, nzdefault=1, unset=True)
OM_SAT = op_mod('sat', BaseType.bool)
OM_LP = op_mod('lp', BaseType.bool)
OM_SCALE = op_mod('scale', BaseType.bool)
OM_ROUNDZERO = op_mod('roundzero', BaseType.bool)
OM_S = op_mod('s', BaseType.bool)
OM_TST_OP_MAIN = op_mod_enum('tst_op_main', [
   ('zero', 'z'),
   ('gzero', 'gz'),
   ('gezero', 'gez'),
   ('carry', 'c'),
   ('equal', 'e'),
   ('greater', 'g'),
   ('gequal', 'ge'),
   ('notequal', 'ne'),
   ('less', 'l'),
   ('lequal', 'le'),
])
OM_TST_OP_BITWISE = op_mod_enum('tst_op_bitwise', [
   ('zero', 'z'),
   ('nonzero', 'nz'),
])
OM_SIGNPOS = op_mod_enum('signpos', [
   'twb',
   'pwb',
   'mtb',
   'ftb',
])
OM_DIM = op_mod('dim', BaseType.uint)
OM_PROJ = op_mod('proj', BaseType.bool)
OM_FCNORM = op_mod('fcnorm', BaseType.bool)
OM_NNCOORDS = op_mod('nncoords', BaseType.bool)
OM_LOD_MODE = op_mod_enum('lod_mode', [
   ('normal', ''),
   ('bias', '.bias'),
   ('replace', '.replace'),
   ('gradient', '.gradient'),
])
OM_PPLOD = op_mod('pplod', BaseType.bool)
OM_TAO = op_mod('tao', BaseType.bool)
OM_SOO = op_mod('soo', BaseType.bool)
OM_SNO = op_mod('sno', BaseType.bool)
OM_WRT = op_mod('wrt', BaseType.bool)
OM_SB_MODE = op_mod_enum('sb_mode', [
   ('none', ''),
   ('data', '.data'),
   ('info', '.info'),
   ('both', '.both'),
])
OM_ARRAY = op_mod('array', BaseType.bool)
OM_INTEGER = op_mod('integer', BaseType.bool)
OM_SCHEDSWAP = op_mod('schedswap', BaseType.bool)
OM_F16 = op_mod('f16', BaseType.bool)
OM_TILED = op_mod('tiled', BaseType.bool)
OM_FREEP = op_mod('freep', BaseType.bool)
OM_SM = op_mod('sm', BaseType.bool)
OM_SAVMSK_MODE = op_mod_enum('savmsk_mode', [
   'vm',
   'icm',
   'icmoc',
   'icmi',
   'caxy',
])
OM_ATOM_OP = op_mod_enum('atom_op', [
   'add',
   'sub',
   'xchg',
   'umin',
   'imin',
   'umax',
   'imax',
   'and',
   'or',
   'xor',
])
OM_MCU_CACHE_MODE_LD = op_mod_enum('mcu_cache_mode_ld', [
   ('normal', ''),
   ('bypass', '.bypass'),
   ('force_line_fill', '.forcelinefill'),
])
OM_MCU_CACHE_MODE_ST = op_mod_enum('mcu_cache_mode_st', [
   ('write_through', '.writethrough'),
   ('write_back', '.writeback'),
   ('lazy_write_back', '.lazywriteback'),
])
OM_BRANCH_CND = op_mod_enum('branch_cnd', [
   ('exec_cond', ''),
   ('allinst', '.allinst'),
   ('anyinst', '.anyinst'),
])
OM_LINK = op_mod('link', BaseType.bool)
OM_PCK_FMT = op_mod_enum('pck_fmt', [
   'u8888',
   's8888',
   'o8888',
   'u1616',
   's1616',
   'o1616',
   'u32',
   's32',
   'u1010102',
   's1010102',
   'u111110',
   's111110',
   'f111110',
   'f16f16',
   'f32',
   'cov',
   'u565u565',
   'd24s8',
   's8d24',
   'f32_mask',
   '2f10f10f10',
   's8888ogl',
   's1616ogl',
   'zero',
   'one',
])
OM_PHASE2END = op_mod('phase2end', BaseType.bool)
OM_ITR_MODE = op_mod_enum('itr_mode', [
   'pixel',
   'sample',
   'centroid',
])
OM_SCHED = op_mod_enum('sched', [
   'none',
   'swap',
   'wdf',
])
OM_ATOM = op_mod('atom', BaseType.bool, unset=True)
OM_OLCHK = op_mod('olchk', BaseType.bool, unset=True)
OM_END = op_mod('end', BaseType.bool, unset=True)

# Ops.

OM_ALU = [OM_OLCHK, OM_EXEC_CND, OM_END, OM_ATOM, OM_RPT]
OM_ALU_RPT1 = [OM_OLCHK, OM_EXEC_CND, OM_END, OM_ATOM, OM_RPT]

## Main.
O_FADD = hw_op('fadd', OM_ALU + [OM_SAT], 1, 2, [], [[RM_ABS, RM_NEG, RM_FLR], [RM_ABS]])
O_FMUL = hw_op('fmul', OM_ALU + [OM_SAT], 1, 2, [], [[RM_ABS, RM_NEG, RM_FLR], [RM_ABS]])
O_FMAD = hw_op('fmad', OM_ALU + [OM_SAT, OM_LP], 1, 3, [], [[RM_ABS, RM_NEG], [RM_ABS, RM_NEG], [RM_ABS, RM_NEG, RM_FLR]])
O_MBYP0 = hw_op('mbyp0', OM_ALU, 1, 1, [], [[RM_ABS, RM_NEG]])
O_PCK = hw_op('pck', OM_ALU + [OM_PCK_FMT, OM_ROUNDZERO, OM_SCALE], 1, 1)

# TODO
# O_PCK_ELEM = pseudo_op('pck.elem', OM_ALU_RPT1 + [OM_PCK_FMT, OM_ROUNDZERO, OM_SCALE], 1, 2)

## Backend.
O_UVSW_WRITE = hw_op('uvsw.write', [OM_EXEC_CND, OM_RPT], 0, 2)
O_UVSW_EMIT = hw_op('uvsw.emit', [OM_EXEC_CND])
O_UVSW_ENDTASK = hw_op('uvsw.endtask', [OM_END])
O_UVSW_EMIT_ENDTASK = hw_op('uvsw.emit.endtask', [OM_END])
O_UVSW_WRITE_EMIT_ENDTASK = hw_op('uvsw.write.emit.endtask', [OM_END], 0, 2)

O_FITR = hw_op('fitr', OM_ALU + [OM_ITR_MODE, OM_SAT], 1, 3)
O_FITRP = hw_op('fitrp', OM_ALU + [OM_ITR_MODE, OM_SAT], 1, 4)

## Bitwise.
O_BBYP0BM = hw_direct_op('bbyp0bm', 2, 2)

O_MOVI32 = pseudo_op('movi32', OM_ALU, 1, 1)

## Control.
O_WOP = hw_op('wop')
O_WDF = hw_op('wdf', [], 0, 1)
O_NOP = hw_op('nop', [OM_EXEC_CND])
O_NOP_END = hw_op('nop.end', [OM_EXEC_CND])

# TODO NEXT: gate usage of OM_F16!
O_DITR = hw_op('ditr', [OM_EXEC_CND, OM_ITR_MODE, OM_SAT, OM_SCHED, OM_F16], 1, 3)
O_DITRP = hw_op('ditrp', [OM_EXEC_CND, OM_ITR_MODE, OM_SAT, OM_SCHED, OM_F16], 1, 4)
O_DITRP_WRITE = hw_op('ditrp.write', [OM_EXEC_CND, OM_ITR_MODE, OM_SAT, OM_SCHED, OM_F16], 1, 4)
O_DITRP_READ = hw_op('ditrp.read', [OM_EXEC_CND, OM_ITR_MODE, OM_SAT, OM_SCHED, OM_F16], 1, 3)

# Pseudo-ops (unmapped).
O_NEG = pseudo_op('neg', OM_ALU, 1, 1)
O_ABS = pseudo_op('abs', OM_ALU, 1, 1)
O_FLR = pseudo_op('flr', OM_ALU, 1, 1)
O_MOV = pseudo_op('mov', OM_ALU, 1, 1)
O_VEC = pseudo_op('vec', [], 1, VARIABLE, [], [[RM_ABS, RM_NEG]])
O_COMP = pseudo_op('comp', [], 1, 2)
