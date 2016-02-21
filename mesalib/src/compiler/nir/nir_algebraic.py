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

import itertools
import struct
import sys
import mako.template
import re

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
static const ${val.c_type} ${val.name} = {
   { ${val.type_enum} },
% if isinstance(val, Constant):
   { ${hex(val)} /* ${val.value} */ },
% elif isinstance(val, Variable):
   ${val.index}, /* ${val.var_name} */
   ${'true' if val.is_constant else 'false'},
   nir_type_${ val.required_type or 'invalid' },
% elif isinstance(val, Expression):
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

class Constant(Value):
   def __init__(self, val, name):
      Value.__init__(self, name, "constant")
      self.value = val

   def __hex__(self):
      if isinstance(self.value, (bool)):
         return 'NIR_TRUE' if self.value else 'NIR_FALSE'
      if isinstance(self.value, (int, long)):
         return hex(self.value)
      elif isinstance(self.value, float):
         return hex(struct.unpack('I', struct.pack('f', self.value))[0])
      else:
         assert False

_var_name_re = re.compile(r"(?P<const>#)?(?P<name>\w+)(?:@(?P<type>\w+))?")

class Variable(Value):
   def __init__(self, val, name, varset):
      Value.__init__(self, name, "variable")

      m = _var_name_re.match(val)
      assert m and m.group('name') is not None

      self.var_name = m.group('name')
      self.is_constant = m.group('const') is not None
      self.required_type = m.group('type')

      if self.required_type is not None:
         assert self.required_type in ('float', 'bool', 'int', 'unsigned')

      self.index = varset[self.var_name]

class Expression(Value):
   def __init__(self, expr, name_base, varset):
      Value.__init__(self, name_base, "expression")
      assert isinstance(expr, tuple)

      self.opcode = expr[0]
      self.sources = [ Value.create(src, "{0}_{1}".format(name_base, i), varset)
                       for (i, src) in enumerate(expr[1:]) ]

   def render(self):
      srcs = "\n".join(src.render() for src in self.sources)
      return srcs + super(Expression, self).render()

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

struct opt_state {
   void *mem_ctx;
   bool progress;
   const bool *condition_flags;
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
${pass_name}_block(nir_block *block, void *void_state)
{
   struct opt_state *state = void_state;

   nir_foreach_instr_reverse_safe(block, instr) {
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
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      % endfor
      default:
         break;
      }
   }

   return true;
}

static bool
${pass_name}_impl(nir_function_impl *impl, const bool *condition_flags)
{
   struct opt_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.progress = false;
   state.condition_flags = condition_flags;

   nir_foreach_block_reverse(impl, ${pass_name}_block, &state);

   if (state.progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return state.progress;
}


bool
${pass_name}(nir_shader *shader)
{
   bool progress = false;
   bool condition_flags[${len(condition_list)}];
   const nir_shader_compiler_options *options = shader->options;

   % for index, condition in enumerate(condition_list):
   condition_flags[${index}] = ${condition};
   % endfor

   nir_foreach_function(shader, function) {
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

      for xform in transforms:
         if not isinstance(xform, SearchAndReplace):
            xform = SearchAndReplace(xform)

         if xform.search.opcode not in self.xform_dict:
            self.xform_dict[xform.search.opcode] = []

         self.xform_dict[xform.search.opcode].append(xform)

   def render(self):
      return _algebraic_pass_template.render(pass_name=self.pass_name,
                                             xform_dict=self.xform_dict,
                                             condition_list=condition_list)
