#! /usr/bin/env python
#
# Copyright (C) 2014 Intel Corporation
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
#    Jason Ekstrand (jason@jlekstrand.net)

from __future__ import print_function
import ast
import itertools
import struct
import sys
import mako.template
import re
import traceback

from nir_opcodes import opcodes

_type_re = re.compile(r"(?P<type>int|uint|bool|float)?(?P<bits>\d+)?")

def type_bits(type_str):
   m = _type_re.match(type_str)
   assert m.group('type')

   if m.group('bits') is None:
      return 0
   else:
      return int(m.group('bits'))

# Represents a set of variables, each with a unique id
class VarSet(object):
   def __init__(self):
      self.names = {}
      self.ids = itertools.count()
      self.immutable = False;

   def __getitem__(self, name):
      if name not in self.names:
         assert not self.immutable, "Unknown replacement variable: " + name
         self.names[name] = self.ids.next()

      return self.names[name]

   def lock(self):
      self.immutable = True

class Value(object):
   @staticmethod
   def create(val, name_base, varset):
      if isinstance(val, tuple):
         return Expression(val, name_base, varset)
      elif isinstance(val, Expression):
         return val
      elif isinstance(val, (str, unicode)):
         return Variable(val, name_base, varset)
      elif isinstance(val, (bool, int, long, float)):
         return Constant(val, name_base)

   __template = mako.template.Template("""
#include "compiler/nir/nir_search_helpers.h"
static const ${val.c_type} ${val.name} = {
   { ${val.type_enum}, ${val.bit_size} },
% if isinstance(val, Constant):
   ${val.type()}, { ${hex(val)} /* ${val.value} */ },
% elif isinstance(val, Variable):
   ${val.index}, /* ${val.var_name} */
   ${'true' if val.is_constant else 'false'},
   ${val.type() or 'nir_type_invalid' },
   ${val.cond if val.cond else 'NULL'},
% elif isinstance(val, Expression):
   ${'true' if val.inexact else 'false'},
   nir_op_${val.opcode},
   { ${', '.join(src.c_ptr for src in val.sources)} },
% endif
};""")

   def __init__(self, name, type_str):
      self.name = name
      self.type_str = type_str

   @property
   def type_enum(self):
      return "nir_search_value_" + self.type_str

   @property
   def c_type(self):
      return "nir_search_" + self.type_str

   @property
   def c_ptr(self):
      return "&{0}.value".format(self.name)

   def render(self):
      return self.__template.render(val=self,
                                    Constant=Constant,
                                    Variable=Variable,
                                    Expression=Expression)

_constant_re = re.compile(r"(?P<value>[^@\(]+)(?:@(?P<bits>\d+))?")

class Constant(Value):
   def __init__(self, val, name):
      Value.__init__(self, name, "constant")

      if isinstance(val, (str)):
         m = _constant_re.match(val)
         self.value = ast.literal_eval(m.group('value'))
         self.bit_size = int(m.group('bits')) if m.group('bits') else 0
      else:
         self.value = val
         self.bit_size = 0

      if isinstance(self.value, bool):
         assert self.bit_size == 0 or self.bit_size == 32
         self.bit_size = 32

   def __hex__(self):
      if isinstance(self.value, (bool)):
         return 'NIR_TRUE' if self.value else 'NIR_FALSE'
      if isinstance(self.value, (int, long)):
         return hex(self.value)
      elif isinstance(self.value, float):
         return hex(struct.unpack('Q', struct.pack('d', self.value))[0])
      else:
         assert False

   def type(self):
      if isinstance(self.value, (bool)):
         return "nir_type_bool32"
      elif isinstance(self.value, (int, long)):
         return "nir_type_int"
      elif isinstance(self.value, float):
         return "nir_type_float"

_var_name_re = re.compile(r"(?P<const>#)?(?P<name>\w+)"
                          r"(?:@(?P<type>int|uint|bool|float)?(?P<bits>\d+)?)?"
                          r"(?P<cond>\([^\)]+\))?")

class Variable(Value):
   def __init__(self, val, name, varset):
      Value.__init__(self, name, "variable")

      m = _var_name_re.match(val)
      assert m and m.group('name') is not None

      self.var_name = m.group('name')
      self.is_constant = m.group('const') is not None
      self.cond = m.group('cond')
      self.required_type = m.group('type')
      self.bit_size = int(m.group('bits')) if m.group('bits') else 0

      if self.required_type == 'bool':
         assert self.bit_size == 0 or self.bit_size == 32
         self.bit_size = 32

      if self.required_type is not None:
         assert self.required_type in ('float', 'bool', 'int', 'uint')

      self.index = varset[self.var_name]

   def type(self):
      if self.required_type == 'bool':
         return "nir_type_bool32"
      elif self.required_type in ('int', 'uint'):
         return "nir_type_int"
      elif self.required_type == 'float':
         return "nir_type_float"

_opcode_re = re.compile(r"(?P<inexact>~)?(?P<opcode>\w+)(?:@(?P<bits>\d+))?")

class Expression(Value):
   def __init__(self, expr, name_base, varset):
      Value.__init__(self, name_base, "expression")
      assert isinstance(expr, tuple)

      m = _opcode_re.match(expr[0])
      assert m and m.group('opcode') is not None

      self.opcode = m.group('opcode')
      self.bit_size = int(m.group('bits')) if m.group('bits') else 0
      self.inexact = m.group('inexact') is not None
      self.sources = [ Value.create(src, "{0}_{1}".format(name_base, i), varset)
                       for (i, src) in enumerate(expr[1:]) ]

   def render(self):
      srcs = "\n".join(src.render() for src in self.sources)
      return srcs + super(Expression, self).render()

class IntEquivalenceRelation(object):
   """A class representing an equivalence relation on integers.

   Each integer has a canonical form which is the maximum integer to which it
   is equivalent.  Two integers are equivalent precisely when they have the
   same canonical form.

   The convention of maximum is explicitly chosen to make using it in
   BitSizeValidator easier because it means that an actual bit_size (if any)
   will always be the canonical form.
   """
   def __init__(self):
      self._remap = {}

   def get_canonical(self, x):
      """Get the canonical integer corresponding to x."""
      if x in self._remap:
         return self.get_canonical(self._remap[x])
      else:
         return x

   def add_equiv(self, a, b):
      """Add an equivalence and return the canonical form."""
      c = max(self.get_canonical(a), self.get_canonical(b))
      if a != c:
         assert a < c
         self._remap[a] = c

      if b != c:
         assert b < c
         self._remap[b] = c

      return c

class BitSizeValidator(object):
   """A class for validating bit sizes of expressions.

   NIR supports multiple bit-sizes on expressions in order to handle things
   such as fp64.  The source and destination of every ALU operation is
   assigned a type and that type may or may not specify a bit size.  Sources
   and destinations whose type does not specify a bit size are considered
   "unsized" and automatically take on the bit size of the corresponding
   register or SSA value.  NIR has two simple rules for bit sizes that are
   validated by nir_validator:

    1) A given SSA def or register has a single bit size that is respected by
       everything that reads from it or writes to it.

    2) The bit sizes of all unsized inputs/outputs on any given ALU
       instruction must match.  They need not match the sized inputs or
       outputs but they must match each other.

   In order to keep nir_algebraic relatively simple and easy-to-use,
   nir_search supports a type of bit-size inference based on the two rules
   above.  This is similar to type inference in many common programming
   languages.  If, for instance, you are constructing an add operation and you
   know the second source is 16-bit, then you know that the other source and
   the destination must also be 16-bit.  There are, however, cases where this
   inference can be ambiguous or contradictory.  Consider, for instance, the
   following transformation:

   (('usub_borrow', a, b), ('b2i', ('ult', a, b)))

   This transformation can potentially cause a problem because usub_borrow is
   well-defined for any bit-size of integer.  However, b2i always generates a
   32-bit result so it could end up replacing a 64-bit expression with one
   that takes two 64-bit values and produces a 32-bit value.  As another
   example, consider this expression:

   (('bcsel', a, b, 0), ('iand', a, b))

   In this case, in the search expression a must be 32-bit but b can
   potentially have any bit size.  If we had a 64-bit b value, we would end up
   trying to and a 32-bit value with a 64-bit value which would be invalid

   This class solves that problem by providing a validation layer that proves
   that a given search-and-replace operation is 100% well-defined before we
   generate any code.  This ensures that bugs are caught at compile time
   rather than at run time.

   The basic operation of the validator is very similar to the bitsize_tree in
   nir_search only a little more subtle.  Instead of simply tracking bit
   sizes, it tracks "bit classes" where each class is represented by an
   integer.  A value of 0 means we don't know anything yet, positive values
   are actual bit-sizes, and negative values are used to track equivalence
   classes of sizes that must be the same but have yet to receive an actual
   size.  The first stage uses the bitsize_tree algorithm to assign bit
   classes to each variable.  If it ever comes across an inconsistency, it
   assert-fails.  Then the second stage uses that information to prove that
   the resulting expression can always validly be constructed.
   """

   def __init__(self, varset):
      self._num_classes = 0
      self._var_classes = [0] * len(varset.names)
      self._class_relation = IntEquivalenceRelation()

   def validate(self, search, replace):
      dst_class = self._propagate_bit_size_up(search)
      if dst_class == 0:
         dst_class = self._new_class()
      self._propagate_bit_class_down(search, dst_class)

      validate_dst_class = self._validate_bit_class_up(replace)
      assert validate_dst_class == 0 or validate_dst_class == dst_class
      self._validate_bit_class_down(replace, dst_class)

   def _new_class(self):
      self._num_classes += 1
      return -self._num_classes

   def _set_var_bit_class(self, var_id, bit_class):
      assert bit_class != 0
      var_class = self._var_classes[var_id]
      if var_class == 0:
         self._var_classes[var_id] = bit_class
      else:
         canon_class = self._class_relation.get_canonical(var_class)
         assert canon_class < 0 or canon_class == bit_class
         var_class = self._class_relation.add_equiv(var_class, bit_class)
         self._var_classes[var_id] = var_class

   def _get_var_bit_class(self, var_id):
      return self._class_relation.get_canonical(self._var_classes[var_id])

   def _propagate_bit_size_up(self, val):
      if isinstance(val, (Constant, Variable)):
         return val.bit_size

      elif isinstance(val, Expression):
         nir_op = opcodes[val.opcode]
         val.common_size = 0
         for i in range(nir_op.num_inputs):
            src_bits = self._propagate_bit_size_up(val.sources[i])
            if src_bits == 0:
               continue

            src_type_bits = type_bits(nir_op.input_types[i])
            if src_type_bits != 0:
               assert src_bits == src_type_bits
            else:
               assert val.common_size == 0 or src_bits == val.common_size
               val.common_size = src_bits

         dst_type_bits = type_bits(nir_op.output_type)
         if dst_type_bits != 0:
            assert val.bit_size == 0 or val.bit_size == dst_type_bits
            return dst_type_bits
         else:
            if val.common_size != 0:
               assert val.bit_size == 0 or val.bit_size == val.common_size
            else:
               val.common_size = val.bit_size
            return val.common_size

   def _propagate_bit_class_down(self, val, bit_class):
      if isinstance(val, Constant):
         assert val.bit_size == 0 or val.bit_size == bit_class

      elif isinstance(val, Variable):
         assert val.bit_size == 0 or val.bit_size == bit_class
         self._set_var_bit_class(val.index, bit_class)

      elif isinstance(val, Expression):
         nir_op = opcodes[val.opcode]
         dst_type_bits = type_bits(nir_op.output_type)
         if dst_type_bits != 0:
            assert bit_class == 0 or bit_class == dst_type_bits
         else:
            assert val.common_size == 0 or val.common_size == bit_class
            val.common_size = bit_class

         if val.common_size:
            common_class = val.common_size
         elif nir_op.num_inputs:
            # If we got here then we have no idea what the actual size is.
            # Instead, we use a generic class
            common_class = self._new_class()

         for i in range(nir_op.num_inputs):
            src_type_bits = type_bits(nir_op.input_types[i])
            if src_type_bits != 0:
               self._propagate_bit_class_down(val.sources[i], src_type_bits)
            else:
               self._propagate_bit_class_down(val.sources[i], common_class)

   def _validate_bit_class_up(self, val):
      if isinstance(val, Constant):
         return val.bit_size

      elif isinstance(val, Variable):
         var_class = self._get_var_bit_class(val.index)
         # By the time we get to validation, every variable should have a class
         assert var_class != 0

         # If we have an explicit size provided by the user, the variable
         # *must* exactly match the search.  It cannot be implicitly sized
         # because otherwise we could end up with a conflict at runtime.
         assert val.bit_size == 0 or val.bit_size == var_class

         return var_class

      elif isinstance(val, Expression):
         nir_op = opcodes[val.opcode]
         val.common_class = 0
         for i in range(nir_op.num_inputs):
            src_class = self._validate_bit_class_up(val.sources[i])
            if src_class == 0:
               continue

            src_type_bits = type_bits(nir_op.input_types[i])
            if src_type_bits != 0:
               assert src_class == src_type_bits
            else:
               assert val.common_class == 0 or src_class == val.common_class
               val.common_class = src_class

         dst_type_bits = type_bits(nir_op.output_type)
         if dst_type_bits != 0:
            assert val.bit_size == 0 or val.bit_size == dst_type_bits
            return dst_type_bits
         else:
            if val.common_class != 0:
               assert val.bit_size == 0 or val.bit_size == val.common_class
            else:
               val.common_class = val.bit_size
            return val.common_class

   def _validate_bit_class_down(self, val, bit_class):
      # At this point, everything *must* have a bit class.  Otherwise, we have
      # a value we don't know how to define.
      assert bit_class != 0

      if isinstance(val, Constant):
         assert val.bit_size == 0 or val.bit_size == bit_class

      elif isinstance(val, Variable):
         assert val.bit_size == 0 or val.bit_size == bit_class

      elif isinstance(val, Expression):
         nir_op = opcodes[val.opcode]
         dst_type_bits = type_bits(nir_op.output_type)
         if dst_type_bits != 0:
            assert bit_class == dst_type_bits
         else:
            assert val.common_class == 0 or val.common_class == bit_class
            val.common_class = bit_class

         for i in range(nir_op.num_inputs):
            src_type_bits = type_bits(nir_op.input_types[i])
            if src_type_bits != 0:
               self._validate_bit_class_down(val.sources[i], src_type_bits)
            else:
               self._validate_bit_class_down(val.sources[i], val.common_class)

_optimization_ids = itertools.count()

condition_list = ['true']

class SearchAndReplace(object):
   def __init__(self, transform):
      self.id = _optimization_ids.next()

      search = transform[0]
      replace = transform[1]
      if len(transform) > 2:
         self.condition = transform[2]
      else:
         self.condition = 'true'

      if self.condition not in condition_list:
         condition_list.append(self.condition)
      self.condition_index = condition_list.index(self.condition)

      varset = VarSet()
      if isinstance(search, Expression):
         self.search = search
      else:
         self.search = Expression(search, "search{0}".format(self.id), varset)

      varset.lock()

      if isinstance(replace, Value):
         self.replace = replace
      else:
         self.replace = Value.create(replace, "replace{0}".format(self.id), varset)

      BitSizeValidator(varset).validate(self.search, self.replace)

_algebraic_pass_template = mako.template.Template("""
#include "nir.h"
#include "nir_search.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

#endif

% for (opcode, xform_list) in xform_dict.iteritems():
% for xform in xform_list:
   ${xform.search.render()}
   ${xform.replace.render()}
% endfor

static const struct transform ${pass_name}_${opcode}_xforms[] = {
% for xform in xform_list:
   { &${xform.search.name}, ${xform.replace.c_ptr}, ${xform.condition_index} },
% endfor
};
% endfor

static bool
${pass_name}_block(nir_block *block, const bool *condition_flags,
                   void *mem_ctx)
{
   bool progress = false;

   nir_foreach_instr_reverse_safe(instr, block) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (!alu->dest.dest.is_ssa)
         continue;

      switch (alu->op) {
      % for opcode in xform_dict.keys():
      case nir_op_${opcode}:
         for (unsigned i = 0; i < ARRAY_SIZE(${pass_name}_${opcode}_xforms); i++) {
            const struct transform *xform = &${pass_name}_${opcode}_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      % endfor
      default:
         break;
      }
   }

   return progress;
}

static bool
${pass_name}_impl(nir_function_impl *impl, const bool *condition_flags)
{
   void *mem_ctx = ralloc_parent(impl);
   bool progress = false;

   nir_foreach_block_reverse(block, impl) {
      progress |= ${pass_name}_block(block, condition_flags, mem_ctx);
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return progress;
}


bool
${pass_name}(nir_shader *shader)
{
   bool progress = false;
   bool condition_flags[${len(condition_list)}];
   const nir_shader_compiler_options *options = shader->options;
   (void) options;

   % for index, condition in enumerate(condition_list):
   condition_flags[${index}] = ${condition};
   % endfor

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= ${pass_name}_impl(function->impl, condition_flags);
   }

   return progress;
}
""")

class AlgebraicPass(object):
   def __init__(self, pass_name, transforms):
      self.xform_dict = {}
      self.pass_name = pass_name

      error = False

      for xform in transforms:
         if not isinstance(xform, SearchAndReplace):
            try:
               xform = SearchAndReplace(xform)
            except:
               print("Failed to parse transformation:", file=sys.stderr)
               print("  " + str(xform), file=sys.stderr)
               traceback.print_exc(file=sys.stderr)
               print('', file=sys.stderr)
               error = True
               continue

         if xform.search.opcode not in self.xform_dict:
            self.xform_dict[xform.search.opcode] = []

         self.xform_dict[xform.search.opcode].append(xform)

      if error:
         sys.exit(1)

   def render(self):
      return _algebraic_pass_template.render(pass_name=self.pass_name,
                                             xform_dict=self.xform_dict,
                                             condition_list=condition_list)
