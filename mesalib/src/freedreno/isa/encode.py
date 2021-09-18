#
# Copyright © 2020 Google, Inc.
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

from mako.template import Template
from isa import ISA, BitSetDerivedField, BitSetAssertField
import sys
import re

# Encoding is driven by the display template that would be used
# to decode any given instruction, essentially working backwards
# from the decode case.  (Or put another way, the decoded bitset
# should contain enough information to re-encode it again.)
#
# In the xml, we can have multiple override cases per bitset,
# which can override display template and/or fields.  Iterating
# all this from within the template is messy, so use helpers
# outside of the template for this.
#
# The hierarchy of iterators for encoding is:
#
#   // First level - Case()  (s.bitset_cases() iterator)
#   if (caseA.expression()) {  // maps to <override/> in xml
#      // Second level - DisplayField()  (case.display_fields() iterator)
#      ... encode field A ...
#      ... encode field B ...
#
#      // Third level - each display field can be resolved in potentially
#      // resolved by multiple different overrides, you can end up with
#      // an if/else ladder for an individual display field
#      if (field_c_case1.expression()) {
#         ... encode field C ...
#      } else if (field_c_case2.expression() {
#         ... encode field C ...
#      } else {
#      }
#
#   } else if (caseB.expression())(
#   } else {  // maps to the default case in bitset, ie. outside <override/>
#   }


# Represents a concrete field, ie. a field can be overriden
# by an override, so the exact choice to encode a given field
# in a bitset may be conditional
class FieldCase(object):
    def __init__(self, field, case):
        self.field = field
        self.expr  = None
        if case.expr is not None:
            self.expr = isa.expressions[case.expr]

class AssertField(object):
    def __init__(self, field, case):
        self.field = field
        self.expr  = None
        if case.expr is not None:
            self.expr = isa.expressions[case.expr]

# Represents a field to be encoded:
class DisplayField(object):
    def __init__(self, bitset, case, name):
        self.bitset = bitset   # leaf bitset
        self.case = case
        self.name = name

    def fields(self, bitset=None):
        if bitset is None:
            bitset = self.bitset
        # resolving the various cases for encoding a given
        # field is similar to resolving the display template
        # string
        for case in bitset.cases:
            if case.expr is not None:
                expr = bitset.isa.expressions[case.expr]
                self.case.append_expr_fields(expr)
            if self.name in case.fields:
                field = case.fields[self.name]
                # For bitset fields, the bitset type could reference
                # fields in this (the containing) bitset, in addition
                # to the ones which are directly used to encode the
                # field itself.
                if field.get_c_typename() == 'TYPE_BITSET':
                    for param in field.params:
                        self.case.append_field(param[0])
                # For derived fields, we want to consider any other
                # fields that are referenced by the expr
                if isinstance(field, BitSetDerivedField):
                    expr = bitset.isa.expressions[field.expr]
                    self.case.append_expr_fields(expr)
                elif not isinstance(field, BitSetAssertField):
                    yield FieldCase(field, case)
                # if we've found an unconditional case specifying
                # the named field, we are done
                if case.expr is None:
                    return
        if bitset.extends is not None:
            yield from self.fields(isa.bitsets[bitset.extends])

# Represents an if/else case in bitset encoding which has a display
# template string:
class Case(object):
    def __init__(self, bitset, case):
        self.bitset = bitset   # leaf bitset
        self.case = case
        self.expr = None
        if case.expr is not None:
            self.expr = isa.expressions[case.expr]
        self.fieldnames = re.findall(r"{([a-zA-Z0-9_]+)}", case.display)
        self.append_forced(bitset)

    # Handle fields which don't appear in display template but have
    # force="true"
    def append_forced(self, bitset):
        if bitset.encode is not None:
            for name, val in bitset.encode.forced.items():
                self.append_field(name)
        if bitset.extends is not None:
            self.append_forced(isa.bitsets[bitset.extends])

    # In the process of resolving a field, we might discover additional
    # fields that need resolving:
    #
    # a) a derived field which maps to one or more other concrete fields
    # b) a bitset field, which may be "parameterized".. for example a
    #    #multisrc field which refers back to SRC1_R/SRC2_R outside of
    #    the range of bits covered by the #multisrc field itself
    def append_field(self, fieldname):
        if fieldname not in self.fieldnames:
            self.fieldnames.append(fieldname)

    def append_expr_fields(self, expr):
        for fieldname in expr.fieldnames:
            self.append_field(fieldname)

    def display_fields(self):
        for fieldname in self.fieldnames:
            yield DisplayField(self.bitset, self, fieldname)

    def assert_cases(self, bitset=None):
        if bitset is None:
            bitset = self.bitset
        for case in bitset.cases:
            for name, field in case.fields.items():
                if field.get_c_typename() == 'TYPE_ASSERT':
                    yield AssertField(field, case)
        if bitset.extends is not None:
            yield from self.assert_cases(isa.bitsets[bitset.extends])

# State and helpers used by the template:
class State(object):
    def __init__(self, isa):
        self.isa = isa
        self.warned_missing_extractors = []

    def bitset_cases(self, bitset, leaf_bitset=None):
        if leaf_bitset is None:
            leaf_bitset = bitset;
        for case in bitset.cases:
            if case.display is None:
                # if this is the last case (ie. case.expr is None)
                # then we need to go up the inheritance chain:
                if case.expr is None and bitset.extends is not None:
                    parent_bitset = isa.bitsets[bitset.extends]
                    yield from self.bitset_cases(parent_bitset, leaf_bitset)
                continue;
            yield Case(leaf_bitset, case)

    # Find unique bitset remap/parameter names, to generate a struct
    # used to pass "parameters" to bitset fields:
    def unique_param_names(self):
        unique_names = []
        for root in self.encode_roots():
            for leaf in self.encode_leafs(root):
                for case in s.bitset_cases(leaf):
                    for df in case.display_fields():
                        for f in df.fields():
                            if f.field.get_c_typename() == 'TYPE_BITSET':
                                for param in f.field.params:
                                    target_name = param[1]
                                    if target_name not in unique_names:
                                        yield target_name
                                        unique_names.append(target_name)

    def case_name(self, bitset, name):
       return bitset.encode.case_prefix + name.upper().replace('.', '_').replace('-', '_').replace('#', '')

    def encode_roots(self):
       for name, root in self.isa.roots.items():
          if root.encode is None:
             continue
          yield root

    def encode_leafs(self, root):
       for name, leaf in self.isa.leafs.items():
          if leaf.get_root() != root:
             continue
          yield leaf

    # expressions used in a bitset (case or field or recursively parent bitsets)
    def bitset_used_exprs(self, bitset):
       for case in bitset.cases:
          if case.expr:
             yield self.isa.expressions[case.expr]
          for name, field in case.fields.items():
             if isinstance(field, BitSetDerivedField):
                yield self.isa.expressions[field.expr]
       if bitset.extends is not None:
          yield from self.bitset_used_exprs(self.isa.bitsets[bitset.extends])

    def extractor_impl(self, bitset, name):
        if bitset.encode is not None:
            if name in bitset.encode.maps:
                return bitset.encode.maps[name]
        if bitset.extends is not None:
            return self.extractor_impl(self.isa.bitsets[bitset.extends], name)
        return None

    # Default fallback when no mapping is defined, simply to avoid
    # having to deal with encoding at the same time as r/e new
    # instruction decoding.. but we can at least print warnings:
    def extractor_fallback(self, bitset, name):
        extr_name = bitset.name + '.' + name
        if extr_name not in self.warned_missing_extractors:
            print('WARNING: no encode mapping for {}.{}'.format(bitset.name, name))
            self.warned_missing_extractors.append(extr_name)
        return '0 /* XXX */'

    def extractor(self, bitset, name):
        extr = self.extractor_impl(bitset, name)
        if extr is not None:
            return extr
        return self.extractor_fallback(bitset, name)

    # In the special case of needing to access a field with bitset type
    # for an expr, we need to encode the field so we end up with an
    # integer, and not some pointer to a thing that will be encoded to
    # an integer
    def expr_extractor(self, bitset, name, p):
        extr = self.extractor_impl(bitset, name)
        field = self.resolve_simple_field(bitset, name)
        if isinstance(field, BitSetDerivedField):
            expr = self.isa.expressions[field.expr]
            return self.expr_name(bitset.get_root(), expr) + '(s, p, src)'
        if extr is None:
            if name in self.unique_param_names():
                extr = 'p->' + name
            else:
                extr = self.extractor_fallback(bitset, name)
        if field and field.get_c_typename() == 'TYPE_BITSET':
            extr = 'encode' + isa.roots[field.type].get_c_name() + '(s, ' + p + ', ' + extr + ')'
        return extr

    # A limited resolver for field type which doesn't properly account for
    # overrides.  In particular, if a field is defined differently in multiple
    # different cases, this just blindly picks the last one.
    #
    # TODO to do this properly, I don't think there is an alternative than
    # to emit code which evaluates the case.expr
    def resolve_simple_field(self, bitset, name):
        field = None
        for case in bitset.cases:
            if name in case.fields:
                field = case.fields[name]
        if field is not None:
            return field
        if bitset.extends is not None:
            return self.resolve_simple_field(isa.bitsets[bitset.extends], name)
        return None

    def encode_type(self, bitset):
        if bitset.encode is not None:
            if bitset.encode.type is not None:
                return bitset.encode.type
        if bitset.extends is not None:
            return self.encode_type(isa.bitsets[bitset.extends])
        return None

    def expr_name(self, root, expr):
       return root.get_c_name() + '_' + expr.get_c_name()

    def has_jmp(self, instructions):
        # I'm sure there is some clever more pythony way to do this:
        for instr in instructions:
            if instr[0] == 'JMP':
                return True
        return False

template = """\
/* Copyright (C) 2020 Google, Inc.
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
 */

#include <stdbool.h>
#include <stdint.h>

<%
isa = s.isa
%>

/**
 * Opaque type from the PoV of generated code, but allows state to be passed
 * thru to the hand written helpers used by the generated code.
 */
struct encode_state;

struct bitset_params;

static uint64_t
pack_field(unsigned low, unsigned high, uint64_t val)
{
   val &= ((UINT64_C(1) << (1 + high - low)) - 1);
   return val << low;
}

/*
 * Forward-declarations (so we don't have to figure out which order to
 * emit various encoders when they have reference each other)
 */

%for root in s.encode_roots():
static uint64_t encode${root.get_c_name()}(struct encode_state *s, struct bitset_params *p, ${root.encode.type} src);
%endfor

## TODO before the expr evaluators, we should generate extract_FOO() for
## derived fields.. which probably also need to be in the context of the
## respective root so they take the correct src arg??

/*
 * Expression evaluators:
 */

struct bitset_params {
%for name in s.unique_param_names():
   int64_t ${name};
%endfor
};

#define push(v) do { \
            assert(sp < ARRAY_SIZE(stack)); \
            stack[sp] = (v); \
            sp++; \
        } while (0)
#define peek() ({ \
            assert(sp < ARRAY_SIZE(stack)); \
            stack[sp - 1]; \
        })
#define pop() ({ \
            assert(sp > 0); \
            --sp; \
            stack[sp]; \
        })

<%def name="render_expr(leaf, expr)">
static inline int64_t
${s.expr_name(leaf.get_root(), expr)}(struct encode_state *s, struct bitset_params *p, ${leaf.get_root().encode.type} src)
{
%   for fieldname in expr.fieldnames:
    int64_t ${fieldname};
%   endfor
%   for fieldname in expr.fieldnames:
<% field = s.resolve_simple_field(leaf, fieldname) %>
%      if field is not None and field.get_c_typename() == 'TYPE_BITSET':
          { ${encode_params(leaf, field)}
          ${fieldname} = ${s.expr_extractor(leaf, fieldname, '&bp')};
          }
%      else:
          ${fieldname} = ${s.expr_extractor(leaf, fieldname, 'p')};
%      endif
%   endfor
    return ${expr.expr};
}
</%def>

## note, we can't just iterate all the expressions, but we need to find
## the context in which they are used to know the correct src type

%for root in s.encode_roots():
<%
    rendered_exprs = []
%>
%   for leaf in s.encode_leafs(root):
%      for expr in s.bitset_used_exprs(leaf):
<%
          if expr in rendered_exprs:
             continue
          rendered_exprs.append(expr)
%>
          ${render_expr(leaf, expr)}
%      endfor
%   endfor
%endfor

#undef pop
#undef peek
#undef push


<%def name="case_pre(root, expr)">
%if expr is not None:
    if (${s.expr_name(root, expr)}(s, p, src)) {
%else:
    {
%endif
</%def>

<%def name="case_post(root, expr)">
%if expr is not None:
    } else
%else:
    }
%endif
</%def>

<%def name="encode_params(leaf, field)">
 struct bitset_params bp = {
%for param in field.params:
    .${param[1]} = ${s.expr_extractor(leaf, param[0], 'p')},  /* ${param[0]} */
%endfor
 };
</%def>

<%def name="encode_bitset(root, leaf)">
      uint64_t fld, val = ${hex(leaf.get_pattern().match)};
      (void)fld;
<% visited_exprs = [] %>
%for case in s.bitset_cases(leaf):
<%
    if case.expr is not None:
        visited_exprs.append(case.expr)
%>
    ${case_pre(root, case.expr)}
%   for df in case.display_fields():
%       for f in df.fields():
<%
          # simplify the control flow a bit to give the compiler a bit
          # less to clean up
          expr = f.expr
          if expr == case.expr:
              # Don't need to evaluate the same condition twice:
              expr = None
          elif expr in visited_exprs:
              # We are in an 'else'/'else-if' leg that we wouldn't
              # go down due to passing an earlier if()
              continue
%>
           ${case_pre(root, expr)}
%         if f.field.get_c_typename() == 'TYPE_BITSET':
             { ${encode_params(leaf, f.field)}
             fld = encode${isa.roots[f.field.type].get_c_name()}(s, &bp, ${s.extractor(leaf, f.field.name)}); }
%         else:
             fld = ${s.extractor(leaf, f.field.name)};
%         endif
          val |= pack_field(${f.field.low}, ${f.field.high}, fld);  /* ${f.field.name} */
           ${case_post(root, expr)}
%       endfor
%   endfor

%   for f in case.assert_cases():
<%
      # simplify the control flow a bit to give the compiler a bit
      # less to clean up
      expr = f.expr
      if expr == case.expr:
          # Don't need to evaluate the same condition twice:
          expr = None
      elif expr in visited_exprs:
          # We are in an 'else'/'else-if' leg that we wouldn't
          # go down due to passing an earlier if()
          continue
%>
       ${case_pre(root, expr)}
      val |= pack_field(${f.field.low}, ${f.field.high}, ${f.field.val});
       ${case_post(root, None)}
%   endfor
      {}  /* in case no unconditional field to close out last '} else' */
    ${case_post(root, case.expr)}
%endfor
      return val;
</%def>

/*
 * The actual encoder definitions
 */

%for root in s.encode_roots():

static uint64_t
encode${root.get_c_name()}(struct encode_state *s, struct bitset_params *p, ${root.encode.type} src)
{
%   if root.encode.case_prefix is not None:
   switch (${root.get_c_name()}_case(s, src)) {
%      for leaf in s.encode_leafs(root):
   case ${s.case_name(root, leaf.name)}: {
       ${encode_bitset(root, leaf)}
    }
%      endfor
   default:
      /* Note that we need the default case, because there are
       * instructions which we never expect to be encoded, (ie.
       * meta/macro instructions) as they are removed/replace
       * in earlier stages of the compiler.
       */
      break;
   }
   mesa_loge("Unhandled ${root.name} encode case: 0x%x\\n", ${root.get_c_name()}_case(s, src));
   return 0;
%   else: # single case bitset, no switch
%      for leaf in s.encode_leafs(root):
       ${encode_bitset(root, leaf)}
%      endfor
%   endif
}

%endfor

"""

xml = sys.argv[1]
dst = sys.argv[2]

isa = ISA(xml)
s = State(isa)

with open(dst, 'w') as f:
    f.write(Template(template).render(s=s))
