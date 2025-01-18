# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from pco_isa import *
from pco_ops import *

# Enum mappings.

class EnumMap(object):
   def __init__(self, name, type_from, type_to, mappings):
      self.name = name
      self.type_from = type_from
      self.type_to = type_to
      self.mappings = mappings

enum_maps = {}

def enum_map(enum_from, enum_to, mappings):
   key = (enum_from, enum_to)
   assert key not in enum_maps.keys(), f'Duplicate enum mapping for "{enum_from.tname}" to "{enum_to.tname}".'

   assert enum_from.base_type == BaseType.enum
   assert enum_to.base_type == BaseType.enum

   # Ensure the validity of the enum_from elements.
   assert set([from_elem for from_elem, to_elem in mappings]).issubset(set(enum_from.enum.elems.keys())), f'Invalid enum_from spec in enum mapping for "{enum_from.tname}" to "{enum_to.tname}".'

   # Ensure the validity of the enum_to elements.
   assert set([to_elem for from_elem, to_elem in mappings]).issubset(set(enum_to.enum.elems.keys())), f'Invalid enum_to spec in enum mapping for "{enum_from.tname}" to "{enum_to.tname}".'

   _mappings = []
   for elem_from, elem_to in mappings:
      _mappings.append((enum_from.enum.elems[elem_from].cname, enum_to.enum.elems[elem_to].cname))

   name = f'{prefix}_map_{enum_from.tname}_to_{enum_to.tname}'
   enum_maps[key] = EnumMap(name, enum_from.name, enum_to.name, _mappings)

enum_map(OM_EXEC_CND.t, F_CC, [
   ('e1_zx', 'e1_zx'),
   ('e1_z1', 'e1_z1'),
   ('ex_zx', 'ex_zx'),
   ('e1_z0', 'e1_z0'),
])

enum_map(REG_CLASS, F_REGBANK, [
   ('temp', 'temp'),
   ('vtxin', 'vtxin'),
   ('coeff', 'coeff'),
   ('shared', 'shared'),
   ('index', 'idx0'),
   ('spec', 'special'),
   ('intern', 'special'),
   ('const', 'special'),
   ('pixout', 'special'),
   ('global', 'special'),
   ('slot', 'special'),
])

enum_map(REG_CLASS, F_IDXBANK, [
   ('temp', 'temp'),
   ('vtxin', 'vtxin'),
   ('coeff', 'coeff'),
   ('shared', 'shared'),
   ('index', 'idx'),
   ('pixout', 'pixout'),
])

enum_map(IO, F_IS0_SEL, [
   ('s0', 's0'),
   ('s1', 's1'),
   ('s2', 's2'),
   ('s3', 's3'),
   ('s4', 's4'),
   ('s5', 's5'),
])

enum_map(IO, F_IS1_SEL, [
   ('ft0', 'ft0'),
   ('fte', 'fte'),
])

enum_map(IO, F_IS2_SEL, [
   ('ft1', 'ft1'),
   ('fte', 'fte'),
])

enum_map(IO, F_IS3_SEL, [
   ('ft0', 'ft0'),
   ('ft1', 'ft1'),
   ('fte', 'fte'),
])

enum_map(IO, F_IS4_SEL, [
   ('ft0', 'ft0'),
   ('ft1', 'ft1'),
   ('ft2', 'ft2'),
   ('fte', 'fte'),
])

enum_map(IO, F_IS5_SEL, [
   ('ft0', 'ft0'),
   ('ft1', 'ft1'),
   ('ft2', 'ft2'),
   ('fte', 'fte'),
])

enum_map(OM_ITR_MODE.t, F_ITER_MODE, [
   ('pixel', 'pixel'),
   ('sample', 'sample'),
   ('centroid', 'centroid'),
])

enum_map(OM_PCK_FMT.t, F_PCK_FORMAT, [
   ('u8888', 'u8888'),
   ('s8888', 's8888'),
   ('o8888', 'o8888'),
   ('u1616', 'u1616'),
   ('s1616', 's1616'),
   ('o1616', 'o1616'),
   ('u32', 'u32'),
   ('s32', 's32'),
   ('u1010102', 'u1010102'),
   ('s1010102', 's1010102'),
   ('u111110', 'u111110'),
   ('s111110', 's111110'),
   ('f111110', 'f111110'),
   ('f16f16', 'f16f16'),
   ('f32', 'f32'),
   ('cov', 'cov'),
   ('u565u565', 'u565u565'),
   ('d24s8', 'd24s8'),
   ('s8d24', 's8d24'),
   ('f32_mask', 'f32_mask'),
   ('2f10f10f10', '2f10f10f10'),
   ('s8888ogl', 's8888ogl'),
   ('s1616ogl', 's1616ogl'),
   ('zero', 'zero'),
   ('one', 'one'),
])

enum_map(OM_SCHED.t, F_SCHED_CTRL, [
   ('none', 'none'),
   ('swap', 'swap'),
   ('wdf', 'wdf'),
])

# Op/ISA mapping.
class OpMap(object):
   def __init__(self, name, cop_name, igrp_mappings, encode_variants):
      self.name = name
      self.cop_name = cop_name
      self.igrp_mappings = igrp_mappings
      self.encode_variants = encode_variants

op_maps = {}
encode_maps = {}

def op_map(op, hdr, isa_ops, srcs=[], iss=[], dests=[]):
   assert op not in op_maps.keys(), f'Duplicate op mapping for op "{op.name}".'

   hdr_variant, hdr_fields = hdr

   # Ensure we're speccing everything in the header except length and da (need to be set later).
   assert set(hdr_variant.struct_fields.keys()) == set([hdr_field for hdr_field, val_spec in hdr_fields] + ['length', 'da']), f'Invalid field spec in hdr mapping for op "{op.name}".'

   # Add alutype setting after the check above, as it's actually a fixed value.
   hdr_fields.append(('alutype', hdr_variant.bsname))

   igrp_mappings = []
   encode_variants = []

   hdr_mappings = []
   for hdr_field, val_spec in hdr_fields:
      field = hdr_variant.bit_set.fields[hdr_field]
      if isinstance(val_spec, bool):
         value = str(val_spec).lower()
         hdr_mappings.append(f'{{}}->hdr.{hdr_field} = {str(val_spec).lower()};')
      elif isinstance(val_spec, int):
         value = val_spec
         hdr_mappings.append(f'{{}}->hdr.{hdr_field} = {val_spec};')
      elif isinstance(val_spec, str):
         assert field.field_type.base_type == BaseType.enum

         enum = field.field_type.enum
         assert enum.parent is None
         assert val_spec in enum.elems.keys(), f'Invalid enum element "{val_spec}" in field "{hdr_field}" in hdr mapping for op "{op.name}".'

         hdr_mappings.append(f'{{}}->hdr.{hdr_field} = {enum.elems[val_spec].cname};')
      elif isinstance(val_spec, OpMod):
         assert val_spec in op.op_mods, f'Op mod "{val_spec.t.tname}" was specified but not valid in hdr mapping for op "{op.name}".'

         if field.field_type.base_type == BaseType.enum:
            enum_map_key = (val_spec.t, field.field_type)
            assert enum_map_key in enum_maps.keys(), f'Op mod enum "{val_spec.t.tname}" was specified but no mapping from enum "{field.field_type.tname}" was found in hdr mapping for op "{op.name}".'
            enum_mapping = enum_maps[enum_map_key]
            hdr_mappings.append(f'{{}}->hdr.{hdr_field} = {enum_mapping.name}(pco_instr_get_mod({{}}, {val_spec.cname}));')
         else:
            hdr_mappings.append(f'{{}}->hdr.{hdr_field} = pco_instr_get_mod({{}}, {val_spec.cname});')

         if val_spec.t.unset:
            reset_val = 0 if val_spec.t.nzdefault is None else val_spec.t.nzdefault
            hdr_mappings.append(f'pco_instr_set_mod({{1}}, {val_spec.cname}, {reset_val});')
      elif isinstance(val_spec, tuple):
         mod, origin = val_spec
         assert isinstance(mod, str) and isinstance(origin, str)
         hdr_mappings.append(f'{{}}->hdr.{hdr_field} = {mod}({{}}->{origin});')
      else:
         assert False, f'Invalid value spec for field "{hdr_field}" in hdr mapping for op "{op.name}".'

   if bool(hdr_mappings):
      igrp_mappings.append(hdr_mappings);

   repl_op_mappings = []
   op_mappings = []
   for phase, isa_op_map_variants, *_repl_op in isa_ops:
      assert phase in OP_PHASE.enum.elems.keys()
      default_variant = isa_op_map_variants[0]
      first_variant = isa_op_map_variants[1] if len(isa_op_map_variants) > 1 else None
      repl_op_spec = _repl_op[0] if len(_repl_op) > 0 else None

      op_mappings.append(f'list_del(&{{1}}->link);')

      if repl_op_spec is None:
         op_mappings.append(f'{{}}->instrs[{OP_PHASE.enum.elems[phase].cname}] = {{}};')
         op_mappings.append(f'ralloc_steal(igrp, {{1}});')

      for isa_op_map_variant in isa_op_map_variants:
         isa_op, isa_op_fields, *_ = isa_op_map_variant

         # Ensure we're speccing everything in the op fields.
         assert set(isa_op.struct_fields.keys()) == set([field for field, val_spec in isa_op_fields]), f'Invalid field spec in isa op "{isa_op.bsname}" mapping for op "{op.name}".'

         variant = isa_op.name.upper()
         variant_set = ''
         if isa_op_map_variant != default_variant:
            variant_set = '   '
            isa_op_conds = isa_op_map_variant[2]
            if_str = 'if (' if isa_op_map_variant == first_variant else 'else if('
            for i, isa_op_cond in enumerate(isa_op_conds):
               if i > 0:
                  if_str += ' && '

               if isinstance(isa_op_cond, tuple) and len(isa_op_cond) == 3:
                  mod, origin, cond = isa_op_cond
                  assert isinstance(mod, RefMod)
                  if_str += f'{{1}}->{origin}.{mod.t.tname} {cond}'
               elif isinstance(isa_op_cond, tuple) and len(isa_op_cond) == 2:
                  mod, cond = isa_op_cond
                  assert isinstance(mod, OpMod)
                  if_str += f'pco_instr_get_mod({{1}}, {mod.cname}) {cond}'
               else:
                  assert False
            if_str += ')'
            op_mappings.append(if_str)

         variant_set += f'{{}}->variant.instr[{OP_PHASE.enum.elems[phase].cname}].{isa_op.bit_set.bsname} = {variant};'
         op_mappings.append(variant_set)

         encode_variant = f'{isa_op.name}_encode({{0}}'
         for isa_op_field, val_spec in isa_op_fields:
            struct_field = isa_op.struct_fields[isa_op_field]
            encode_variant += f', .{isa_op_field} = '
            if isinstance(val_spec, bool):
               encode_variant += str(val_spec).lower()
            elif isinstance(val_spec, int):
               encode_variant += str(val_spec)
            elif isinstance(val_spec, str):
               assert struct_field.type.base_type == BaseType.enum

               enum = struct_field.type.enum
               assert enum.parent is None
               assert val_spec in enum.elems.keys(), f'Invalid enum element "{val_spec}" in field "{isa_op_field}" in isa op "{isa_op.bsname}" mapping for op "{op.name}".'

               encode_variant += enum.elems[val_spec].cname
            elif isinstance(val_spec, OpMod):
               assert val_spec in op.op_mods, f'Op mod "{val_spec.t.tname}" was specified but not valid in isa op "{isa_op.bsname}" mapping for op "{op.name}".'

               if struct_field.type.base_type == BaseType.enum:
                  enum_map_key = (val_spec.t, struct_field.type)
                  assert enum_map_key in enum_maps.keys(), f'Op mod enum "{val_spec.t.tname}" was specified but no mapping from enum "{struct_field.type.tname}" was found in hdr mapping for op "{op.name}".'
                  enum_mapping = enum_maps[enum_map_key]
                  encode_variant += f'{enum_mapping.name}(pco_instr_get_mod({{1}}, {val_spec.cname}))'
               else:
                  encode_variant += f'pco_instr_get_mod({{1}}, {val_spec.cname})'

            elif isinstance(val_spec, tuple):
               mod, origin = val_spec
               if isinstance(mod, RefMod):
                  encode_variant += f'{{1}}->{origin}.{mod.t.tname}'
               elif isinstance(mod, str):
                  encode_variant += f'{mod}({{1}}->{origin})'
               else:
                  assert False
            else:
               assert False, f'Invalid value spec for field "{isa_op_field}" in isa op "{isa_op.bsname}" mapping for op "{op.name}".'
         encode_variant += ')'
         encode_variants.append((variant, encode_variant))

      enc_bname = op.bname
      enc_cname = op.cname
      if repl_op_spec is not None:
         repl_op, repl_dests, repl_srcs = repl_op_spec

         enc_bname = repl_op.bname
         enc_cname = repl_op.cname

         assert len(repl_dests) == repl_op.num_dests
         assert len(repl_srcs) == repl_op.num_srcs
         repl_set = f'{{0}}->instrs[{OP_PHASE.enum.elems[phase].cname}] = {repl_op.bname}({{1}}->parent_func'

         for ref in repl_dests + repl_srcs:
            if ref in IO.enum.elems.keys():
               io = IO.enum.elems[ref]
               repl_set += f', pco_ref_io({io.cname})'
            else:
               repl_set += f', {{1}}->{ref}'

         repl_set += ');'

         repl_op_mappings.append(repl_set)

         repl_op_mappings.append(f'ralloc_free({{1}});')

      encode_maps[enc_bname] = OpMap(enc_bname, enc_cname.upper(), None, encode_variants)

      igrp_mappings.append(op_mappings);

   src_mappings = []
   for src, val_spec, _io in srcs:
      assert _io in IO.enum.elems.keys()
      io = IO.enum.elems[_io]
      src_mappings.append(f'{{}}->srcs.{src} = {{}}->{val_spec};')
      src_mappings.append(f'{{1}}->{val_spec} = pco_ref_io({io.cname});')
      src_mappings.append(f'pco_ref_xfer_mods(&{{1}}->{val_spec}, &{{0}}->srcs.{src}, true);')

   if bool(src_mappings):
      igrp_mappings.append(src_mappings);

   iss_mappings = []
   for iss, _io in iss:
      assert _io in IO.enum.elems.keys()
      io = IO.enum.elems[_io]
      iss_mappings.append(f'{{}}->iss.{iss} = pco_ref_io({io.cname});')

   if bool(iss_mappings):
      igrp_mappings.append(iss_mappings);

   dest_mappings = []
   for dest, val_spec, _io in dests:
      assert _io in IO.enum.elems.keys()
      io = IO.enum.elems[_io]
      dest_mappings.append(f'{{}}->dests.{dest} = {{}}->{val_spec};')
      dest_mappings.append(f'{{1}}->{val_spec} = pco_ref_io({io.cname});')
      dest_mappings.append(f'pco_ref_xfer_mods(&{{1}}->{val_spec}, &{{0}}->dests.{dest}, true);')

   if bool(dest_mappings):
      igrp_mappings.append(dest_mappings);

   if bool(repl_op_mappings):
      igrp_mappings.append(repl_op_mappings);

   name = op.bname
   cop_name = op.cname.upper()
   op_maps[name] = OpMap(name, cop_name, igrp_mappings, None)

# Main.
op_map(O_FADD,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'p0'),
      ('olchk', OM_OLCHK),
      ('w1p', False),
      ('w0p', True),
      ('cc', OM_EXEC_CND),
      ('end', OM_END),
      ('atom', OM_ATOM),
      ('rpt', OM_RPT),
   ]),
   isa_ops=[
      ('0', [
         (I_FADD, [
            ('sat', OM_SAT),
            ('s0neg', (RM_NEG, 'src[0]')),
            ('s0abs', (RM_ABS, 'src[0]')),
            ('s1abs', (RM_ABS, 'src[1]')),
            ('s0flr', (RM_FLR, 'src[0]')),
         ])
      ])
   ],
   srcs=[
      ('s[0]', 'src[0]', 's0'),
      ('s[1]', 'src[1]', 's1'),
   ],
   iss=[
      ('is[4]', 'ft0'),
   ],
   dests=[
      ('w[0]', 'dest[0]', 'ft0'),
   ]
)

op_map(O_FMUL,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'p0'),
      ('olchk', OM_OLCHK),
      ('w1p', False),
      ('w0p', True),
      ('cc', OM_EXEC_CND),
      ('end', OM_END),
      ('atom', OM_ATOM),
      ('rpt', OM_RPT),
   ]),
   isa_ops=[
      ('0', [
         (I_FMUL, [
            ('sat', OM_SAT),
            ('s0neg', (RM_NEG, 'src[0]')),
            ('s0abs', (RM_ABS, 'src[0]')),
            ('s1abs', (RM_ABS, 'src[1]')),
            ('s0flr', (RM_FLR, 'src[0]')),
         ])
      ])
   ],
   srcs=[
      ('s[0]', 'src[0]', 's0'),
      ('s[1]', 'src[1]', 's1'),
   ],
   iss=[
      ('is[4]', 'ft0'),
   ],
   dests=[
      ('w[0]', 'dest[0]', 'ft0'),
   ]
)

op_map(O_FMAD,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'p0'),
      ('olchk', OM_OLCHK),
      ('w1p', False),
      ('w0p', True),
      ('cc', OM_EXEC_CND),
      ('end', OM_END),
      ('atom', OM_ATOM),
      ('rpt', OM_RPT),
   ]),
   isa_ops=[
      ('0', [
         (I_FMAD_EXT, [
            ('s0neg', (RM_NEG, 'src[0]')),
            ('s0abs', (RM_ABS, 'src[0]')),
            ('s2neg', (RM_NEG, 'src[2]')),
            ('sat', OM_SAT),

            ('lp', OM_LP),
            ('s1abs', (RM_ABS, 'src[1]')),
            ('s1neg', (RM_NEG, 'src[1]')),
            ('s2flr', (RM_FLR, 'src[2]')),
            ('s2abs', (RM_ABS, 'src[2]')),
         ]),
         (I_FMAD, [
            ('s0neg', (RM_NEG, 'src[0]')),
            ('s0abs', (RM_ABS, 'src[0]')),
            ('s2neg', (RM_NEG, 'src[2]')),
            ('sat', OM_SAT),
         ], [
            (OM_LP, '== false'),
            (RM_ABS, 'src[1]', '== false'),
            (RM_NEG, 'src[1]', '== false'),
            (RM_FLR, 'src[2]', '== false'),
            (RM_ABS, 'src[2]', '== false'),
         ])
      ])
   ],
   srcs=[
      ('s[0]', 'src[0]', 's0'),
      ('s[1]', 'src[1]', 's1'),
      ('s[2]', 'src[2]', 's2'),
   ],
   iss=[
      ('is[4]', 'ft0'),
   ],
   dests=[
      ('w[0]', 'dest[0]', 'ft0'),
   ]
)

op_map(O_MBYP0,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'p0'),
      ('olchk', OM_OLCHK),
      ('w1p', False),
      ('w0p', True),
      ('cc', OM_EXEC_CND),
      ('end', OM_END),
      ('atom', OM_ATOM),
      ('rpt', OM_RPT),
   ]),
   isa_ops=[
      ('0', [
         (I_SNGL_EXT, [
            ('sngl_op', 'byp'),
            ('s0neg', (RM_NEG, 'src[0]')),
            ('s0abs', (RM_ABS, 'src[0]')),
         ]),
         (I_SNGL, [('sngl_op', 'byp')], [
            (RM_NEG, 'src[0]', '== false'),
            (RM_ABS, 'src[0]', '== false'),
         ])
      ])
   ],
   srcs=[
      ('s[0]', 'src[0]', 's0'),
   ],
   iss=[
      ('is[4]', 'ft0'),
   ],
   dests=[
      ('w[0]', 'dest[0]', 'ft0'),
   ]
)

op_map(O_PCK,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'p2'),
      ('olchk', OM_OLCHK),
      ('w1p', False),
      ('w0p', True),
      ('cc', OM_EXEC_CND),
      ('end', OM_END),
      ('atom', OM_ATOM),
      ('rpt', OM_RPT),
   ]),
   isa_ops=[
      ('2_pck', [
         (I_PCK, [
            ('prog', False),
            ('rtz', OM_ROUNDZERO),
            ('scale', OM_SCALE),
            ('pck_format', OM_PCK_FMT),
         ])
      ])
   ],
   srcs=[
      ('s[0]', 'src[0]', 'is3'),
   ],
   iss=[
      ('is[0]', 's0'),
      ('is[3]', 'fte'),
      ('is[4]', 'ft2'),
   ],
   dests=[
      ('w[0]', 'dest[0]', 'ft2'),
   ]
)

# Backend.
op_map(O_UVSW_WRITE,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'be'),
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', OM_EXEC_CND),
      ('end', False),
      ('atom', False),
      ('rpt', OM_RPT),
   ]),
   isa_ops=[
      ('backend', [
         (I_UVSW_WRITE_IMM, [
            ('dsel', 'w0'),
            ('imm_addr', ('pco_ref_get_imm', 'src[1]')),
         ])
      ])
   ],
   srcs=[
      ('s[0]', 'src[0]', 'w0'),
   ],
   iss=[
      ('is[0]', 's0'),
      ('is[4]', 'fte'),
   ]
)

op_map(O_UVSW_EMIT,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'be'),
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', OM_EXEC_CND),
      ('end', False),
      ('atom', False),
      ('rpt', 1),
   ]),
   isa_ops=[
      ('backend', [(I_UVSW_EMIT, [])])
   ],
)

op_map(O_UVSW_EMIT_ENDTASK,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'be'),
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', 'e1_zx'),
      ('end', OM_END),
      ('atom', False),
      ('rpt', 1),
   ]),
   isa_ops=[
      ('backend', [(I_UVSW_EMIT_ENDTASK, [])])
   ],
)

op_map(O_UVSW_ENDTASK,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'be'),
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', 'e1_zx'),
      ('end', OM_END),
      ('atom', False),
      ('rpt', 1),
   ]),
   isa_ops=[
      ('backend', [(I_UVSW_ENDTASK, [])])
   ],
)

op_map(O_UVSW_WRITE_EMIT_ENDTASK,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'be'),
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', 'e1_zx'),
      ('end', OM_END),
      ('atom', False),
      ('rpt', 1),
   ]),
   isa_ops=[
      ('backend', [
         (I_UVSW_WRITE_EMIT_ENDTASK_IMM, [
            ('dsel', 'w0'),
            ('imm_addr', ('pco_ref_get_imm', 'src[1]')),
         ])
      ])
   ],
   srcs=[
      ('s[0]', 'src[0]', 'w0'),
   ],
   iss=[
      ('is[0]', 's0'),
      ('is[4]', 'fte'),
   ]
)

op_map(O_FITRP,
   hdr=(I_IGRP_HDR_MAIN, [
      ('oporg', 'be'),
      ('olchk', OM_OLCHK),
      ('w1p', False),
      ('w0p', False),
      ('cc', OM_EXEC_CND),
      ('end', OM_END),
      ('atom', OM_ATOM),
      ('rpt', OM_RPT),
   ]),
   isa_ops=[
      ('backend', [
         (I_FITR, [
            ('p', True),
            ('drc', ('pco_ref_get_drc', 'src[0]')),
            ('iter_mode', OM_ITR_MODE),
            ('sat', OM_SAT),
            ('count', ('pco_ref_get_imm', 'src[3]')),
         ])
      ])
   ],
   srcs=[
      ('s[0]', 'src[1]', 's0'),
      ('s[2]', 'src[2]', 's2'),
      ('s[3]', 'dest[0]', 's3'),
   ]
)

# Bitwise
op_map(O_MOVI32,
   hdr=(I_IGRP_HDR_BITWISE, [
      ('opcnt', 'p0'),
      ('olchk', OM_OLCHK),
      ('w1p', False),
      ('w0p', True),
      ('cc', OM_EXEC_CND),
      ('end', OM_END),
      ('atom', OM_ATOM),
      ('rpt', OM_RPT),
   ]),
   isa_ops=[
      ('0', [
         (I_PHASE0_IMM32, [
            ('count_src', 's2'),
            ('count_op', 'byp'),
            ('shift1_op', 'byp'),
            ('imm32', ('pco_ref_get_imm', 'src[1]')),
         ])
      ], (O_BBYP0BM, ('ft0', 'dest[0]'), ('s0', 'src[0]')))
   ],
   dests=[
      ('w[0]', 'dest[0]', 'ft1'),
   ]
)

# Control.
op_map(O_WOP,
   hdr=(I_IGRP_HDR_CONTROL, [
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', 'e1_zx'),
      ('miscctl', 0),
      ('ctrlop', 'wop'),
   ]),
   isa_ops=[
      ('ctrl', [(I_WOP, [])]),
   ]
)

op_map(O_WDF,
   hdr=(I_IGRP_HDR_CONTROL, [
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', 'e1_zx'),
      ('miscctl', ('pco_ref_get_drc', 'src[0]')),
      ('ctrlop', 'wdf'),
   ]),
   isa_ops=[
      ('ctrl', [(I_WDF, [])]),
   ]
)

op_map(O_NOP,
   hdr=(I_IGRP_HDR_CONTROL, [
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', OM_EXEC_CND),
      ('miscctl', False),
      ('ctrlop', 'nop'),
   ]),
   isa_ops=[
      ('ctrl', [(I_NOP, [])]),
   ]
)

op_map(O_NOP_END,
   hdr=(I_IGRP_HDR_CONTROL, [
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', OM_EXEC_CND),
      ('miscctl', True),
      ('ctrlop', 'nop'),
   ]),
   isa_ops=[
      ('ctrl', [(I_NOP, [])]),
   ]
)

op_map(O_DITR,
   hdr=(I_IGRP_HDR_CONTROL, [
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', OM_EXEC_CND),
      ('miscctl', False),
      ('ctrlop', 'ditr'),
   ]),
   isa_ops=[
      ('ctrl', [
         (I_DITR, [
            ('dest', ('pco_ref_get_temp', 'dest[0]')), # TODO NEXT: validate whether or not indexed registers can be used for srcs/dests.

            ('coff', ('pco_ref_get_coeff', 'src[1]')),
            ('p', 'none'),

            ('woff', 0),
            ('mode', OM_ITR_MODE),

            ('count', ('pco_ref_get_imm', 'src[2]')),
            ('coff_idx_ctrl', ('pco_ref_get_reg_idx_ctrl', 'src[1]')),
            ('woff_idx_ctrl', 'none'),

            ('f16', OM_F16),
            ('sched_ctrl', OM_SCHED),
            ('drc', ('pco_ref_get_drc', 'src[0]')),
            ('sat', OM_SAT),
         ])
      ])
   ],
)

op_map(O_DITRP,
   hdr=(I_IGRP_HDR_CONTROL, [
      ('olchk', False),
      ('w1p', False),
      ('w0p', False),
      ('cc', OM_EXEC_CND),
      ('miscctl', False),
      ('ctrlop', 'ditr'),
   ]),
   isa_ops=[
      ('ctrl', [
         (I_DITR, [
            ('dest', ('pco_ref_get_temp', 'dest[0]')), # TODO NEXT: validate whether or not indexed registers can be used for srcs/dests.

            ('coff', ('pco_ref_get_coeff', 'src[1]')),
            ('p', 'iter_mul'),

            ('woff', ('pco_ref_get_coeff', 'src[2]')),
            ('mode', OM_ITR_MODE),

            ('count', ('pco_ref_get_imm', 'src[3]')),
            ('coff_idx_ctrl', ('pco_ref_get_reg_idx_ctrl', 'src[1]')),
            ('woff_idx_ctrl', ('pco_ref_get_reg_idx_ctrl', 'src[2]')),

            ('f16', OM_F16),
            ('sched_ctrl', OM_SCHED),
            ('drc', ('pco_ref_get_drc', 'src[0]')),
            ('sat', OM_SAT),
         ])
      ])
   ],
)
