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
from collections import defaultdict
import itertools
import struct
import sys
import mako.template
import re
import traceback

from nir_opcodes import opcodes, type_sizes

# These opcodes are only employed by nir_search.  This provides a mapping from
# opcode to destination type.
conv_opcode_types = {
    'i2f' : 'float',
    'u2f' : 'float',
    'f2f' : 'float',
    'f2u' : 'uint',
    'f2i' : 'int',
    'u2u' : 'uint',
    'i2i' : 'int',
    'b2f' : 'float',
    'b2i' : 'int',
    'i2b' : 'bool',
    'f2b' : 'bool',
}

if sys.version_info < (3, 0):
    integer_types = (int, long)
    string_type = unicode

else:
    integer_types = (int, )
    string_type = str

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
         self.names[name] = next(self.ids)

      return self.names[name]

   def lock(self):
      self.immutable = True

class Value(object):
   @staticmethod
   def create(val, name_base, varset):
      if isinstance(val, bytes):
         val = val.decode('utf-8')

      if isinstance(val, tuple):
         return Expression(val, name_base, varset)
      elif isinstance(val, Expression):
         return val
      elif isinstance(val, string_type):
         return Variable(val, name_base, varset)
      elif isinstance(val, (bool, float) + integer_types):
         return Constant(val, name_base)

   __template = mako.template.Template("""
static const ${val.c_type} ${val.name} = {
   { ${val.type_enum}, ${val.c_bit_size} },
% if isinstance(val, Constant):
   ${val.type()}, { ${val.hex()} /* ${val.value} */ },
% elif isinstance(val, Variable):
   ${val.index}, /* ${val.var_name} */
   ${'true' if val.is_constant else 'false'},
   ${val.type() or 'nir_type_invalid' },
   ${val.cond if val.cond else 'NULL'},
% elif isinstance(val, Expression):
   ${'true' if val.inexact else 'false'},
   ${val.c_opcode()},
   { ${', '.join(src.c_ptr for src in val.sources)} },
   ${val.cond if val.cond else 'NULL'},
% endif
};""")

   def __init__(self, val, name, type_str):
      self.in_val = str(val)
      self.name = name
      self.type_str = type_str

   def __str__(self):
      return self.in_val

   def get_bit_size(self):
      """Get the physical bit-size that has been chosen for this value, or if
      there is none, the canonical value which currently represents this
      bit-size class. Variables will be preferred, i.e. if there are any
      variables in the equivalence class, the canonical value will be a
      variable. We do this since we'll need to know which variable each value
      is equivalent to when constructing the replacement expression. This is
      the "find" part of the union-find algorithm.
      """
      bit_size = self

      while isinstance(bit_size, Value):
         if bit_size._bit_size is None:
            break
         bit_size = bit_size._bit_size

      if bit_size is not self:
         self._bit_size = bit_size
      return bit_size

   def set_bit_size(self, other):
      """Make self.get_bit_size() return what other.get_bit_size() return
      before calling this, or just "other" if it's a concrete bit-size. This is
      the "union" part of the union-find algorithm.
      """

      self_bit_size = self.get_bit_size()
      other_bit_size = other if isinstance(other, int) else other.get_bit_size()

      if self_bit_size == other_bit_size:
         return

      self_bit_size._bit_size = other_bit_size

   @property
   def type_enum(self):
      return "nir_search_value_" + self.type_str

   @property
   def c_type(self):
      return "nir_search_" + self.type_str

   @property
   def c_ptr(self):
      return "&{0}.value".format(self.name)

   @property
   def c_bit_size(self):
      bit_size = self.get_bit_size()
      if isinstance(bit_size, int):
         return bit_size
      elif isinstance(bit_size, Variable):
         return -bit_size.index - 1
      else:
         # If the bit-size class is neither a variable, nor an actual bit-size, then
         # - If it's in the search expression, we don't need to check anything
         # - If it's in the replace expression, either it's ambiguous (in which
         # case we'd reject it), or it equals the bit-size of the search value
         # We represent these cases with a 0 bit-size.
         return 0

   def render(self):
      return self.__template.render(val=self,
                                    Constant=Constant,
                                    Variable=Variable,
                                    Expression=Expression)

_constant_re = re.compile(r"(?P<value>[^@\(]+)(?:@(?P<bits>\d+))?")

class Constant(Value):
   def __init__(self, val, name):
      Value.__init__(self, val, name, "constant")

      if isinstance(val, (str)):
         m = _constant_re.match(val)
         self.value = ast.literal_eval(m.group('value'))
         self._bit_size = int(m.group('bits')) if m.group('bits') else None
      else:
         self.value = val
         self._bit_size = None

      if isinstance(self.value, bool):
         assert self._bit_size is None or self._bit_size == 1
         self._bit_size = 1

   def hex(self):
      if isinstance(self.value, (bool)):
         return 'NIR_TRUE' if self.value else 'NIR_FALSE'
      if isinstance(self.value, integer_types):
         return hex(self.value)
      elif isinstance(self.value, float):
         i = struct.unpack('Q', struct.pack('d', self.value))[0]
         h = hex(i)

         # On Python 2 this 'L' suffix is automatically added, but not on Python 3
         # Adding it explicitly makes the generated file identical, regardless
         # of the Python version running this script.
         if h[-1] != 'L' and i > sys.maxsize:
            h += 'L'

         return h
      else:
         assert False

   def type(self):
      if isinstance(self.value, (bool)):
         return "nir_type_bool"
      elif isinstance(self.value, integer_types):
         return "nir_type_int"
      elif isinstance(self.value, float):
         return "nir_type_float"

_var_name_re = re.compile(r"(?P<const>#)?(?P<name>\w+)"
                          r"(?:@(?P<type>int|uint|bool|float)?(?P<bits>\d+)?)?"
                          r"(?P<cond>\([^\)]+\))?")

class Variable(Value):
   def __init__(self, val, name, varset):
      Value.__init__(self, val, name, "variable")

      m = _var_name_re.match(val)
      assert m and m.group('name') is not None

      self.var_name = m.group('name')

      # Prevent common cases where someone puts quotes around a literal
      # constant.  If we want to support names that have numeric or
      # punctuation characters, we can me the first assertion more flexible.
      assert self.var_name.isalpha()
      assert self.var_name is not 'True'
      assert self.var_name is not 'False'

      self.is_constant = m.group('const') is not None
      self.cond = m.group('cond')
      self.required_type = m.group('type')
      self._bit_size = int(m.group('bits')) if m.group('bits') else None

      if self.required_type == 'bool':
         if self._bit_size is not None:
            assert self._bit_size in type_sizes(self.required_type)
         else:
            self._bit_size = 1

      if self.required_type is not None:
         assert self.required_type in ('float', 'bool', 'int', 'uint')

      self.index = varset[self.var_name]

   def type(self):
      if self.required_type == 'bool':
         return "nir_type_bool"
      elif self.required_type in ('int', 'uint'):
         return "nir_type_int"
      elif self.required_type == 'float':
         return "nir_type_float"

_opcode_re = re.compile(r"(?P<inexact>~)?(?P<opcode>\w+)(?:@(?P<bits>\d+))?"
                        r"(?P<cond>\([^\)]+\))?")

class Expression(Value):
   def __init__(self, expr, name_base, varset):
      Value.__init__(self, expr, name_base, "expression")
      assert isinstance(expr, tuple)

      m = _opcode_re.match(expr[0])
      assert m and m.group('opcode') is not None

      self.opcode = m.group('opcode')
      self._bit_size = int(m.group('bits')) if m.group('bits') else None
      self.inexact = m.group('inexact') is not None
      self.cond = m.group('cond')
      self.sources = [ Value.create(src, "{0}_{1}".format(name_base, i), varset)
                       for (i, src) in enumerate(expr[1:]) ]

      if self.opcode in conv_opcode_types:
         assert self._bit_size is None, \
                'Expression cannot use an unsized conversion opcode with ' \
                'an explicit size; that\'s silly.'


   def c_opcode(self):
      if self.opcode in conv_opcode_types:
         return 'nir_search_op_' + self.opcode
      else:
         return 'nir_op_' + self.opcode

   def render(self):
      srcs = "\n".join(src.render() for src in self.sources)
      return srcs + super(Expression, self).render()

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

   (('usub_borrow', a, b), ('b2i@32', ('ult', a, b)))

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

   Each value maintains a "bit-size class", which is either an actual bit size
   or an equivalence class with other values that must have the same bit size.
   The validator works by combining bit-size classes with each other according
   to the NIR rules outlined above, checking that there are no inconsistencies.
   When doing this for the replacement expression, we make sure to never change
   the equivalence class of any of the search values. We could make the example
   transforms above work by doing some extra run-time checking of the search
   expression, but we make the user specify those constraints themselves, to
   avoid any surprises. Since the replacement bitsizes can only be connected to
   the source bitsize via variables (variables must have the same bitsize in
   the source and replacment expressions) or the roots of the expression (the
   replacement expression must produce the same bit size as the search
   expression), we prevent merging a variable with anything when processing the
   replacement expression, or specializing the search bitsize
   with anything. The former prevents

   (('bcsel', a, b, 0), ('iand', a, b))

   from being allowed, since we'd have to merge the bitsizes for a and b due to
   the 'iand', while the latter prevents

   (('usub_borrow', a, b), ('b2i@32', ('ult', a, b)))

   from being allowed, since the search expression has the bit size of a and b,
   which can't be specialized to 32 which is the bitsize of the replace
   expression. It also prevents something like:

   (('b2i', ('i2b', a)), ('ineq', a, 0))

   since the bitsize of 'b2i', which can be anything, can't be specialized to
   the bitsize of a.

   After doing all this, we check that every subexpression of the replacement
   was assigned a constant bitsize, the bitsize of a variable, or the bitsize
   of the search expresssion, since those are the things that are known when
   constructing the replacement expresssion. Finally, we record the bitsize
   needed in nir_search_value so that we know what to do when building the
   replacement expression.
   """

   def __init__(self, varset):
      self._var_classes = [None] * len(varset.names)

   def compare_bitsizes(self, a, b):
      """Determines which bitsize class is a specialization of the other, or
      whether neither is. When we merge two different bitsizes, the
      less-specialized bitsize always points to the more-specialized one, so
      that calling get_bit_size() always gets you the most specialized bitsize.
      The specialization partial order is given by:
      - Physical bitsizes are always the most specialized, and a different
        bitsize can never specialize another.
      - In the search expression, variables can always be specialized to each
        other and to physical bitsizes. In the replace expression, we disallow
        this to avoid adding extra constraints to the search expression that
        the user didn't specify.
      - Expressions and constants without a bitsize can always be specialized to
        each other and variables, but not the other way around.

        We return -1 if a <= b (b can be specialized to a), 0 if a = b, 1 if a >= b,
        and None if they are not comparable (neither a <= b nor b <= a).
      """
      if isinstance(a, int):
         if isinstance(b, int):
            return 0 if a == b else None
         elif isinstance(b, Variable):
            return -1 if self.is_search else None
         else:
            return -1
      elif isinstance(a, Variable):
         if isinstance(b, int):
            return 1 if self.is_search else None
         elif isinstance(b, Variable):
            return 0 if self.is_search or a.index == b.index else None
         else:
            return -1
      else:
         if isinstance(b, int):
            return 1
         elif isinstance(b, Variable):
            return 1
         else:
            return 0

   def unify_bit_size(self, a, b, error_msg):
      """Record that a must have the same bit-size as b. If both
      have been assigned conflicting physical bit-sizes, call "error_msg" with
      the bit-sizes of self and other to get a message and raise an error.
      In the replace expression, disallow merging variables with other
      variables and physical bit-sizes as well.
      """
      a_bit_size = a.get_bit_size()
      b_bit_size = b if isinstance(b, int) else b.get_bit_size()

      cmp_result = self.compare_bitsizes(a_bit_size, b_bit_size)

      assert cmp_result is not None, \
         error_msg(a_bit_size, b_bit_size)

      if cmp_result < 0:
         b_bit_size.set_bit_size(a)
      elif not isinstance(a_bit_size, int):
         a_bit_size.set_bit_size(b)

   def merge_variables(self, val):
      """Perform the first part of type inference by merging all the different
      uses of the same variable. We always do this as if we're in the search
      expression, even if we're actually not, since otherwise we'd get errors
      if the search expression specified some constraint but the replace
      expression didn't, because we'd be merging a variable and a constant.
      """
      if isinstance(val, Variable):
         if self._var_classes[val.index] is None:
            self._var_classes[val.index] = val
         else:
            other = self._var_classes[val.index]
            self.unify_bit_size(other, val,
                  lambda other_bit_size, bit_size:
                     'Variable {} has conflicting bit size requirements: ' \
                     'it must have bit size {} and {}'.format(
                        val.var_name, other_bit_size, bit_size))
      elif isinstance(val, Expression):
         for src in val.sources:
            self.merge_variables(src)

   def validate_value(self, val):
      """Validate the an expression by performing classic Hindley-Milner
      type inference on bitsizes. This will detect if there are any conflicting
      requirements, and unify variables so that we know which variables must
      have the same bitsize. If we're operating on the replace expression, we
      will refuse to merge different variables together or merge a variable
      with a constant, in order to prevent surprises due to rules unexpectedly
      not matching at runtime.
      """
      if not isinstance(val, Expression):
         return

      # Generic conversion ops are special in that they have a single unsized
      # source and an unsized destination and the two don't have to match.
      # This means there's no validation or unioning to do here besides the
      # len(val.sources) check.
      if val.opcode in conv_opcode_types:
         assert len(val.sources) == 1, \
            "Expression {} has {} sources, expected 1".format(
               val, len(val.sources))
         self.validate_value(val.sources[0])
         return

      nir_op = opcodes[val.opcode]
      assert len(val.sources) == nir_op.num_inputs, \
         "Expression {} has {} sources, expected {}".format(
            val, len(val.sources), nir_op.num_inputs)

      for src in val.sources:
         self.validate_value(src)

      dst_type_bits = type_bits(nir_op.output_type)

      # First, unify all the sources. That way, an error coming up because two
      # sources have an incompatible bit-size won't produce an error message
      # involving the destination.
      first_unsized_src = None
      for src_type, src in zip(nir_op.input_types, val.sources):
         src_type_bits = type_bits(src_type)
         if src_type_bits == 0:
            if first_unsized_src is None:
               first_unsized_src = src
               continue

            if self.is_search:
               self.unify_bit_size(first_unsized_src, src,
                  lambda first_unsized_src_bit_size, src_bit_size:
                     'Source {} of {} must have bit size {}, while source {} ' \
                     'must have incompatible bit size {}'.format(
                        first_unsized_src, val, first_unsized_src_bit_size,
                        src, src_bit_size))
            else:
               self.unify_bit_size(first_unsized_src, src,
                  lambda first_unsized_src_bit_size, src_bit_size:
                     'Sources {} (bit size of {}) and {} (bit size of {}) ' \
                     'of {} may not have the same bit size when building the ' \
                     'replacement expression.'.format(
                        first_unsized_src, first_unsized_src_bit_size, src,
                        src_bit_size, val))
         else:
            if self.is_search:
               self.unify_bit_size(src, src_type_bits,
                  lambda src_bit_size, unused:
                     '{} must have {} bits, but as a source of nir_op_{} '\
                     'it must have {} bits'.format(
                        src, src_bit_size, nir_op.name, src_type_bits))
            else:
               self.unify_bit_size(src, src_type_bits,
                  lambda src_bit_size, unused:
                     '{} has the bit size of {}, but as a source of ' \
                     'nir_op_{} it must have {} bits, which may not be the ' \
                     'same'.format(
                        src, src_bit_size, nir_op.name, src_type_bits))

      if dst_type_bits == 0:
         if first_unsized_src is not None:
            if self.is_search:
               self.unify_bit_size(val, first_unsized_src,
                  lambda val_bit_size, src_bit_size:
                     '{} must have the bit size of {}, while its source {} ' \
                     'must have incompatible bit size {}'.format(
                        val, val_bit_size, first_unsized_src, src_bit_size))
            else:
               self.unify_bit_size(val, first_unsized_src,
                  lambda val_bit_size, src_bit_size:
                     '{} must have {} bits, but its source {} ' \
                     '(bit size of {}) may not have that bit size ' \
                     'when building the replacement.'.format(
                        val, val_bit_size, first_unsized_src, src_bit_size))
      else:
         self.unify_bit_size(val, dst_type_bits,
            lambda dst_bit_size, unused:
               '{} must have {} bits, but as a destination of nir_op_{} ' \
               'it must have {} bits'.format(
                  val, dst_bit_size, nir_op.name, dst_type_bits))

   def validate_replace(self, val, search):
      bit_size = val.get_bit_size()
      assert isinstance(bit_size, int) or isinstance(bit_size, Variable) or \
            bit_size == search.get_bit_size(), \
            'Ambiguous bit size for replacement value {}: ' \
            'it cannot be deduced from a variable, a fixed bit size ' \
            'somewhere, or the search expression.'.format(val)

      if isinstance(val, Expression):
         for src in val.sources:
            self.validate_replace(src, search)

   def validate(self, search, replace):
      self.is_search = True
      self.merge_variables(search)
      self.merge_variables(replace)
      self.validate_value(search)

      self.is_search = False
      self.validate_value(replace)

      # Check that search is always more specialized than replace. Note that
      # we're doing this in replace mode, disallowing merging variables.
      search_bit_size = search.get_bit_size()
      replace_bit_size = replace.get_bit_size()
      cmp_result = self.compare_bitsizes(search_bit_size, replace_bit_size)

      assert cmp_result is not None and cmp_result <= 0, \
         'The search expression bit size {} and replace expression ' \
         'bit size {} may not be the same'.format(
               search_bit_size, replace_bit_size)

      replace.set_bit_size(search)

      self.validate_replace(replace, search)

_optimization_ids = itertools.count()

condition_list = ['true']

class SearchAndReplace(object):
   def __init__(self, transform):
      self.id = next(_optimization_ids)

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
#include "nir_builder.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

#endif

% for xform in xforms:
   ${xform.search.render()}
   ${xform.replace.render()}
% endfor

% for (opcode, xform_list) in sorted(opcode_xforms.items()):
static const struct transform ${pass_name}_${opcode}_xforms[] = {
% for xform in xform_list:
   { &${xform.search.name}, ${xform.replace.c_ptr}, ${xform.condition_index} },
% endfor
};
% endfor

static bool
${pass_name}_block(nir_builder *build, nir_block *block,
                   const bool *condition_flags)
{
   bool progress = false;

   nir_foreach_instr_reverse_safe(instr, block) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (!alu->dest.dest.is_ssa)
         continue;

      switch (alu->op) {
      % for opcode in sorted(opcode_xforms.keys()):
      case nir_op_${opcode}:
         for (unsigned i = 0; i < ARRAY_SIZE(${pass_name}_${opcode}_xforms); i++) {
            const struct transform *xform = &${pass_name}_${opcode}_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(build, alu, xform->search, xform->replace)) {
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
   bool progress = false;

   nir_builder build;
   nir_builder_init(&build, impl);

   nir_foreach_block_reverse(block, impl) {
      progress |= ${pass_name}_block(&build, block, condition_flags);
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
    } else {
#ifndef NDEBUG
      impl->valid_metadata &= ~nir_metadata_not_properly_reset;
#endif
    }

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
      self.xforms = []
      self.opcode_xforms = defaultdict(lambda : [])
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

         self.xforms.append(xform)
         if xform.search.opcode in conv_opcode_types:
            dst_type = conv_opcode_types[xform.search.opcode]
            for size in type_sizes(dst_type):
               sized_opcode = xform.search.opcode + str(size)
               self.opcode_xforms[sized_opcode].append(xform)
         else:
            self.opcode_xforms[xform.search.opcode].append(xform)

      if error:
         sys.exit(1)


   def render(self):
      return _algebraic_pass_template.render(pass_name=self.pass_name,
                                             xforms=self.xforms,
                                             opcode_xforms=self.opcode_xforms,
                                             condition_list=condition_list)
