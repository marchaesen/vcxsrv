# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from enum import Enum, auto

_ = None
VARIABLE = ~0

prefix = 'pco'

def val_fits_in_bits(val, num_bits):
   return val < pow(2, num_bits)

class BaseType(Enum):
   bool = auto()
   uint = auto()
   enum = auto()

class Type(object):
   def __init__(self, name, tname, base_type, num_bits, dec_bits, check, encode, nzdefault, print_early, unset, enum):
      self.name = name
      self.tname = tname
      self.base_type = base_type
      self.num_bits = num_bits
      self.dec_bits = dec_bits
      self.check = check
      self.encode = encode
      self.nzdefault = nzdefault
      self.print_early = print_early
      self.unset = unset
      self.enum = enum

types = {}

def type(name, base_type, num_bits=None, dec_bits=None, check=None, encode=None, nzdefault=None, print_early=False, unset=False, enum=None):
   assert name not in types.keys(), f'Duplicate type "{name}".'

   if base_type == BaseType.bool:
      _name = 'bool'
   elif base_type == BaseType.uint:
      _name = 'unsigned'
   elif base_type == BaseType.enum:
      _name = f'enum {prefix}_{name}'
   else:
      assert False, f'Invalid base type for type {name}.'

   t = Type(_name, name, base_type, num_bits, dec_bits, check, encode, nzdefault, print_early, unset, enum)
   types[name] = t
   return t

# Enum types.
class EnumType(object):
   def __init__(self, name, ename, elems, valid, unique_count, is_bitset, parent):
      self.name = name
      self.ename = ename
      self.elems = elems
      self.valid = valid
      self.unique_count = unique_count
      self.is_bitset = is_bitset
      self.parent = parent

class EnumElem(object):
   def __init__(self, cname, value, string):
      self.cname = cname
      self.value = value
      self.string = string

enums = {}
def enum_type(name, elems, is_bitset=False, num_bits=None, *args, **kwargs):
   assert name not in enums.keys(), f'Duplicate enum "{name}".'

   _elems = {}
   _valid_vals = set()
   _valid_valmask = 0
   next_value = 0
   for e in elems:
      if isinstance(e, str):
         elem = e
         value = (1 << next_value) if is_bitset else next_value
         next_value += 1
         string = elem
      else:
         assert isinstance(e, tuple) and len(e) > 1
         elem = e[0]
         if isinstance(e[1], str) and len(e) == 2:
            value = (1 << next_value) if is_bitset else next_value
            next_value += 1
            string = e[1]
         elif isinstance(e[1], int):
            value = e[1]
            string = e[2] if len(e) == 3 else elem
         else:
            assert False, f'Invalid defintion for element "{elem}" in elem "{name}".'

      assert isinstance(elem, str) and isinstance(value, int) and isinstance(string, str)

      assert not num_bits or val_fits_in_bits(value, num_bits), f'Element "{elem}" in elem "{name}" with value "{value}" does not fit into {num_bits} bits.'
      # Collect valid values, ensure that elements with repeated values only have one string set.
      if is_bitset:
         if (_valid_valmask & value) != 0:
            string = None
         _valid_valmask |= value
      else:
         if value in _valid_vals:
            string = None
         _valid_vals.add(value)

      assert elem not in _elems.keys(), f'Duplicate element "{elem}" in enum "".'
      cname = f'{prefix}_{name}_{elem}'.upper()
      _elems[elem] = EnumElem(cname, value, string)

   _name = f'{prefix}_{name}'
   _valid = _valid_valmask if is_bitset else _valid_vals
   _unique_count = bin(_valid_valmask).count('1') if is_bitset else len(_valid_vals)
   enum = EnumType(_name, name, _elems, _valid, _unique_count, is_bitset, parent=None)
   enums[name] = enum

   return type(name, BaseType.enum, num_bits, *args, **kwargs, enum=enum)

def enum_subtype(name, parent, num_bits):
   assert name not in enums.keys(), f'Duplicate enum "{name}".'

   assert parent.enum is not None
   assert not parent.enum.is_bitset
   assert parent.num_bits is not None and parent.num_bits > num_bits

   _name = f'{prefix}_{name}'
   # Validation of subtype - values that will fit in the smaller bit size.
   _valid = {val for val in parent.enum.valid if val_fits_in_bits(val, num_bits)}
   enum = EnumType(_name, name, None, _valid, None, False, parent)
   enums[name] = enum
   return type(name, BaseType.enum, num_bits, enum=enum)

# Type specializations.

field_types = {}
field_enum_types = {}
def field_type(name, *args, **kwargs):
   assert name not in field_types.keys(), f'Duplicate field type "{name}".'
   t = type(name, *args, **kwargs)
   field_types[name] = t
   return t

def field_enum_type(name, *args, **kwargs):
   assert name not in field_types.keys() and name not in field_enum_types.keys(), f'Duplicate field enum type "{name}".'
   t = enum_type(name, *args, **kwargs)
   field_types[name] = t
   field_enum_types[name] = enums[name]
   return t

def field_enum_subtype(name, *args, **kwargs):
   assert name not in field_types.keys() and name not in field_enum_types.keys(), f'Duplicate field enum (sub)type "{name}".'
   t = enum_subtype(name, *args, **kwargs)
   field_types[name] = t
   field_enum_types[name] = enums[name]
   return t

class OpMod(object):
   def __init__(self, t, cname, ctype):
      self.t = t
      self.cname = cname
      self.ctype = ctype

op_mods = {}
op_mod_enums = {}
def op_mod(name, *args, **kwargs):
   assert name not in op_mods.keys(), f'Duplicate op mod "{name}".'
   t = type(name, *args, **kwargs)
   cname = f'{prefix}_op_mod_{name}'.upper()
   ctype = f'{prefix}_mod_type_{t.base_type.name.upper()}'.upper()
   om = op_mods[name] = OpMod(t, cname, ctype)
   assert len(op_mods) <= 64, f'Too many op mods ({len(op_mods)})!'
   return om

def op_mod_enum(name, *args, **kwargs):
   assert name not in op_mods.keys() and name not in op_mod_enums.keys(), f'Duplicate op mod enum "{name}".'
   t = enum_type(name, *args, **kwargs)
   cname = f'{prefix}_op_mod_{name}'.upper()
   ctype = f'{prefix}_mod_type_{t.base_type.name.upper()}'.upper()
   om = op_mods[name] = OpMod(t, cname, ctype)
   op_mod_enums[name] = enums[name]
   assert len(op_mods) <= 64, f'Too many op mods ({len(op_mods)})!'
   return om

class RefMod(object):
   def __init__(self, t, cname, ctype):
      self.t = t
      self.cname = cname
      self.ctype = ctype

ref_mods = {}
ref_mod_enums = {}
def ref_mod(name, *args, **kwargs):
   assert name not in ref_mods.keys(), f'Duplicate ref mod "{name}".'
   t = type(name, *args, **kwargs)
   cname = f'{prefix}_ref_mod_{name}'.upper()
   ctype = f'{prefix}_mod_type_{t.base_type.name.upper()}'.upper()
   rm = ref_mods[name] = RefMod(t, cname, ctype)
   assert len(ref_mods) <= 64, f'Too many ref mods ({len(ref_mods)})!'
   return rm

def ref_mod_enum(name, *args, **kwargs):
   assert name not in ref_mods.keys() and name not in ref_mod_enums.keys(), f'Duplicate ref mod enum "{name}".'
   t = enum_type(name, *args, **kwargs)
   cname = f'{prefix}_ref_mod_{name}'.upper()
   ctype = f'{prefix}_mod_type_{t.base_type.name.upper()}'.upper()
   rm = ref_mods[name] = RefMod(t, cname, ctype)
   ref_mod_enums[name] = enums[name]
   assert len(ref_mods) <= 64, f'Too many ref mods ({len(ref_mods)})!'
   return rm

# Bit encoding definition helpers.

class BitPiece(object):
   def __init__(self, name, byte, hi_bit, lo_bit, num_bits):
      self.name = name
      self.byte = byte
      self.hi_bit = hi_bit
      self.lo_bit = lo_bit
      self.num_bits = num_bits

def bit_piece(name, byte, bit_range):
   assert bit_range.count(':') <= 1, f'Invalid bit range specification in bit piece {name}.'
   is_one_bit = not bit_range.count(':')

   split_range = [bit_range, bit_range] if is_one_bit else bit_range.split(':', 1)
   (hi_bit, lo_bit) = list(map(int, split_range))
   assert hi_bit < 8 and hi_bit >= 0 and lo_bit < 8 and lo_bit >= 0 and hi_bit >= lo_bit

   _num_bits = hi_bit - lo_bit + 1
   return BitPiece(name, byte, hi_bit, lo_bit, _num_bits)

class BitField(object):
   def __init__(self, name, cname, field_type, pieces, reserved, validate, encoding, encoded_bits):
      self.name = name
      self.cname = cname
      self.field_type = field_type
      self.pieces = pieces
      self.reserved = reserved
      self.validate = validate
      self.encoding = encoding
      self.encoded_bits = encoded_bits

class Encoding(object):
   def __init__(self, clear, set):
      self.clear = clear
      self.set = set

def bit_field(bit_set_name, name, bit_set_pieces, field_type, pieces, reserved=None):
   _pieces = [bit_set_pieces[p] for p in pieces]

   total_bits = sum([p.num_bits for p in _pieces])
   assert total_bits == field_type.num_bits, f'Expected {field_type.num_bits}, got {total_bits} in bit field {name}.'

   if reserved is not None:
      assert val_fits_in_bits(reserved, total_bits), f'Reserved value for bit field {name} is too large.'

   cname = f'{bit_set_name}_{name}'.upper()
   if field_type.base_type == BaseType.enum:
      validate = f'{prefix}_{field_type.enum.ename}_valid({{}})'
   else:
      validate = f'{{}} < (1ULL << {field_type.num_bits})'

   encoding = []
   bits_consumed = 0
   for i, piece in enumerate(reversed(_pieces)):
      enc_clear = f'{{}}[{piece.byte}] &= {hex((((1 << piece.num_bits) - 1) << piece.lo_bit) ^ 0xff)}'

      enc_set = f'{{}}[{piece.byte}] |= ('
      enc_set += f'({{}} >> {bits_consumed})' if bits_consumed > 0 else '{}'
      enc_set += f' & {hex((1 << piece.num_bits) - 1)})'
      enc_set += f' << {piece.lo_bit}' if piece.lo_bit > 0 else ''
      encoding.append(Encoding(enc_clear, enc_set))

      bits_consumed += piece.num_bits

   return BitField(name, cname, field_type, _pieces, reserved, validate, encoding, bits_consumed)

class BitSet(object):
   def __init__(self, name, bsname, pieces, fields):
      self.name = name
      self.bsname = bsname
      self.pieces = pieces
      self.fields = fields
      self.bit_structs = {}
      self.variants = []

bit_sets = {}

def bit_set(name, pieces, fields):
   assert name not in bit_sets.keys(), f'Duplicate bit set "{name}".'
   _name = f'{prefix}_{name}'

   _pieces = {}
   for (piece, spec) in pieces:
      assert piece not in _pieces.keys(), f'Duplicate bit piece "{piece}" in bit set "{name}".'
      _pieces[piece] = bit_piece(piece, *spec)

   _fields = {}
   for (field, spec) in fields:
      assert field not in _fields.keys(), f'Duplicate bit field "{field}" in bit set "{name}".'
      _fields[field] = bit_field(_name, field, _pieces, *spec)

   bs = BitSet(_name, name, _pieces, _fields)
   bit_sets[name] = bs
   return bs

class BitStruct(object):
   def __init__(self, name, bsname, struct_fields, encode_fields, num_bytes, data, bit_set):
      self.name = name
      self.bsname = bsname
      self.struct_fields = struct_fields
      self.encode_fields = encode_fields
      self.num_bytes = num_bytes
      self.data = data
      self.bit_set = bit_set

class StructField(object):
   def __init__(self, type, field, bits):
      self.type = type
      self.field = field
      self.bits = bits

class EncodeField(object):
   def __init__(self, name, value):
      self.name = name
      self.value = value

class Variant(object):
   def __init__(self, cname, bytes):
      self.cname = cname
      self.bytes = bytes

def bit_struct(name, bit_set, field_mappings, data=None):
   assert name not in bit_set.bit_structs.keys(), f'Duplicate bit struct "{name}" in bit set "{bit_set.name}".'

   struct_fields = {}
   encode_fields = []
   all_pieces = []
   total_bits = 0
   for mapping in field_mappings:
      if isinstance(mapping, str):
         struct_field = mapping
         _field = mapping
         fixed_value = None
      else:
         assert isinstance(mapping, tuple)
         struct_field, _field, *fixed_value = mapping
         assert len(fixed_value) == 0 or len(fixed_value) == 1
         fixed_value = None if len(fixed_value) == 0 else fixed_value[0]

      assert struct_field not in struct_fields.keys(), f'Duplicate struct field "{struct_field}" in bit struct "{name}".'
      assert _field in bit_set.fields.keys(), f'Field "{_field}" in mapping for struct field "{name}.{struct_field}" not defined in bit set "{bit_set.name}".'
      field = bit_set.fields[_field]
      field_type = field.field_type
      is_enum = field_type.base_type == BaseType.enum

      if fixed_value is not None:
         assert field.reserved is None, f'Fixed value for field mapping "{struct_field}" using field "{_field}" cannot overwrite its reserved value.'

         if is_enum and isinstance(fixed_value, str):
            enum = field_type.enum
            assert fixed_value in enum.elems.keys(), f'Fixed value for field mapping "{struct_field}" using field "{_field}" is not an element of enum {field_type.name}.'
            fixed_value = enum.elems[fixed_value].cname.upper()
         else:
            if isinstance(fixed_value, bool):
               fixed_value = int(fixed_value)

            assert isinstance(fixed_value, int)
            assert val_fits_in_bits(fixed_value, field_type.num_bits), f'Fixed value for field mapping "{struct_field}" using field "{_field}" is too large.'

      all_pieces.extend([(piece.lo_bit + (8 * piece.byte), piece.hi_bit + (8 * piece.byte), piece.name) for piece in field.pieces])
      total_bits += field_type.num_bits

      # Describe how to encode the bit struct.
      encode_field = f'{bit_set.name}_{_field}'.upper()
      if fixed_value is not None:
         encode_value = fixed_value
      elif field.reserved is not None:
         encode_value = field.reserved
      else:
         encode_value = f's.{struct_field}'
      encode_fields.append(EncodeField(encode_field, encode_value))

      # Describe settable fields.
      if field.reserved is None and fixed_value is None:
         # Use parent enum for struct fields.
         if is_enum and field_type.enum.parent is not None:
            field_type = field_type.enum.parent

         struct_field_bits = field_type.dec_bits if field_type.dec_bits is not None else field_type.num_bits
         struct_fields[struct_field] = StructField(field_type, struct_field, struct_field_bits)

   # Check for overlapping pieces.
   for p0 in all_pieces:
      for p1 in all_pieces:
         if p0 == p1:
            continue
         assert p0[1] < p1[0] or p0[0] > p1[1], f'Pieces "{p0[2]}" and "{p1[2]}" overlap in bit struct "{name}".'

   # Check for byte-alignment.
   assert (total_bits % 8) == 0, f'Bit struct "{name}" has a non-byte-aligned number of bits ({total_bits}).'

   _name = f'{bit_set.name}_{name}'
   total_bytes = total_bits // 8
   bs = BitStruct(_name, name, struct_fields, encode_fields, total_bytes, data, bit_set)
   bit_set.bit_structs[name] = bs
   bit_set.variants.append(Variant(f'{bit_set.name}_{name}'.upper(), total_bytes))

   return bs

# Op definitions.
class Op(object):
   def __init__(self, name, cname, bname, op_type, op_mods, cop_mods, op_mod_map, num_dests, num_srcs, dest_mods, cdest_mods, src_mods, csrc_mods, has_target_cf_node, builder_params):
      self.name = name
      self.cname = cname
      self.bname = bname
      self.op_type = op_type
      self.op_mods = op_mods
      self.cop_mods = cop_mods
      self.op_mod_map = op_mod_map
      self.num_dests = num_dests
      self.num_srcs = num_srcs
      self.dest_mods = dest_mods
      self.cdest_mods = cdest_mods
      self.src_mods = src_mods
      self.csrc_mods = csrc_mods
      self.has_target_cf_node = has_target_cf_node
      self.builder_params = builder_params

ops = {}

def op(name, op_type, op_mods, num_dests, num_srcs, dest_mods, src_mods, has_target_cf_node):
   assert name not in ops.keys(), f'Duplicate op "{name}".'

   _name = name.replace('.', '_')
   cname = f'{prefix}_op_{_name}'
   bname = f'{prefix}_{_name}'
   cop_mods = 0 if not op_mods else ' | '.join([f'(1ULL << {op_mod.cname})' for op_mod in op_mods])
   op_mod_map = {op_mod.cname: index + 1 for index, op_mod in enumerate(op_mods)}
   cdest_mods = {i: 0 if not dest_mods else ' | '.join([f'(1ULL << {ref_mod.cname})' for ref_mod in destn_mods]) for i, destn_mods in enumerate(dest_mods)}
   csrc_mods = {i: 0 if not src_mods else ' | '.join([f'(1ULL << {ref_mod.cname})' for ref_mod in srcn_mods]) for i, srcn_mods in enumerate(src_mods)}

   builder_params = ['', '', '', '', '']

   if op_type != 'hw_direct':
      builder_params[0] = 'pco_builder *b'
      builder_params[1] = 'b'
      builder_params[4] = 'pco_cursor_func(b->cursor)'
   else:
      builder_params[0] = 'pco_func *func'
      builder_params[1] = 'func'
      builder_params[4] = 'func'

   if has_target_cf_node:
      builder_params[0] += ', pco_cf_node *target_cf_node'
      builder_params[1] += ', target_cf_node'

   if num_dests == VARIABLE:
      builder_params[0] += f', unsigned num_dests, pco_ref *dest'
      builder_params[1] += f', num_dests, dests'
   else:
      for d in range(num_dests):
         builder_params[0] += f', pco_ref dest{d}'
         builder_params[1] += f', dest{d}'

   if num_srcs == VARIABLE:
      builder_params[0] += f', unsigned num_srcs, pco_ref *src'
      builder_params[1] += f', num_srcs, srcs'
   else:
      for s in range(num_srcs):
         builder_params[0] += f', pco_ref src{s}'
         builder_params[1] += f', src{s}'

   if bool(op_mods):
      builder_params[0] += f', struct {prefix}_{_name}_mods mods'
      builder_params[2] = ', ...'
      builder_params[3] = f', (struct {bname}_mods){{0, ##__VA_ARGS__}}'

   op = Op(name, cname, bname, op_type, op_mods, cop_mods, op_mod_map, num_dests, num_srcs, dest_mods, cdest_mods, src_mods, csrc_mods, has_target_cf_node, builder_params)
   ops[name] = op
   return op

def pseudo_op(name, op_mods=[], num_dests=0, num_srcs=0, dest_mods=[], src_mods=[], has_target_cf_node=False):
   return op(name, 'pseudo', op_mods, num_dests, num_srcs, dest_mods, src_mods, has_target_cf_node)

def hw_op(name, op_mods=[], num_dests=0, num_srcs=0, dest_mods=[], src_mods=[], has_target_cf_node=False):
   return op(name, 'hw', op_mods, num_dests, num_srcs, dest_mods, src_mods, has_target_cf_node)

def hw_direct_op(name, num_dests=0, num_srcs=0, has_target_cf_node=False):
   return op(name, 'hw_direct', [], num_dests, num_srcs, [], [], has_target_cf_node)
