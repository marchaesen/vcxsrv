
import re

type_split_re = re.compile(r'(?P<type>[a-z]+)(?P<bits>\d+)')

def type_has_size(type_):
    return type_[-1:].isdigit()

def type_size(type_):
    assert type_has_size(type_)
    return int(type_split_re.match(type_).group('bits'))

def type_sizes(type_):
    if type_has_size(type_):
        return [type_size(type_)]
    elif type_ == 'float':
        return [16, 32, 64]
    else:
        return [8, 16, 32, 64]

def type_add_size(type_, size):
    if type_has_size(type_):
        return type_
    return type_ + str(size)

def op_bit_sizes(op):
    sizes = None
    if not type_has_size(op.output_type):
        sizes = set(type_sizes(op.output_type))

    for input_type in op.input_types:
        if not type_has_size(input_type):
            if sizes is None:
                sizes = set(type_sizes(input_type))
            else:
                sizes = sizes.intersection(set(type_sizes(input_type)))

    return sorted(list(sizes)) if sizes is not None else None

def get_const_field(type_):
    if type_ == "bool32":
        return "u32"
    elif type_ == "float16":
        return "u16"
    else:
        m = type_split_re.match(type_)
        if not m:
            raise Exception(str(type_))
        return m.group('type')[0] + m.group('bits')

template = """\
/*
 * Copyright (C) 2014 Intel Corporation
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
 *
 * Authors:
 *    Jason Ekstrand (jason@jlekstrand.net)
 */

#include <math.h>
#include "util/rounding.h" /* for _mesa_roundeven */
#include "util/half_float.h"
#include "nir_constant_expressions.h"

/**
 * Evaluate one component of packSnorm4x8.
 */
static uint8_t
pack_snorm_1x8(float x)
{
    /* From section 8.4 of the GLSL 4.30 spec:
     *
     *    packSnorm4x8
     *    ------------
     *    The conversion for component c of v to fixed point is done as
     *    follows:
     *
     *      packSnorm4x8: round(clamp(c, -1, +1) * 127.0)
     *
     * We must first cast the float to an int, because casting a negative
     * float to a uint is undefined.
     */
   return (uint8_t) (int)
          _mesa_roundevenf(CLAMP(x, -1.0f, +1.0f) * 127.0f);
}

/**
 * Evaluate one component of packSnorm2x16.
 */
static uint16_t
pack_snorm_1x16(float x)
{
    /* From section 8.4 of the GLSL ES 3.00 spec:
     *
     *    packSnorm2x16
     *    -------------
     *    The conversion for component c of v to fixed point is done as
     *    follows:
     *
     *      packSnorm2x16: round(clamp(c, -1, +1) * 32767.0)
     *
     * We must first cast the float to an int, because casting a negative
     * float to a uint is undefined.
     */
   return (uint16_t) (int)
          _mesa_roundevenf(CLAMP(x, -1.0f, +1.0f) * 32767.0f);
}

/**
 * Evaluate one component of unpackSnorm4x8.
 */
static float
unpack_snorm_1x8(uint8_t u)
{
    /* From section 8.4 of the GLSL 4.30 spec:
     *
     *    unpackSnorm4x8
     *    --------------
     *    The conversion for unpacked fixed-point value f to floating point is
     *    done as follows:
     *
     *       unpackSnorm4x8: clamp(f / 127.0, -1, +1)
     */
   return CLAMP((int8_t) u / 127.0f, -1.0f, +1.0f);
}

/**
 * Evaluate one component of unpackSnorm2x16.
 */
static float
unpack_snorm_1x16(uint16_t u)
{
    /* From section 8.4 of the GLSL ES 3.00 spec:
     *
     *    unpackSnorm2x16
     *    ---------------
     *    The conversion for unpacked fixed-point value f to floating point is
     *    done as follows:
     *
     *       unpackSnorm2x16: clamp(f / 32767.0, -1, +1)
     */
   return CLAMP((int16_t) u / 32767.0f, -1.0f, +1.0f);
}

/**
 * Evaluate one component packUnorm4x8.
 */
static uint8_t
pack_unorm_1x8(float x)
{
    /* From section 8.4 of the GLSL 4.30 spec:
     *
     *    packUnorm4x8
     *    ------------
     *    The conversion for component c of v to fixed point is done as
     *    follows:
     *
     *       packUnorm4x8: round(clamp(c, 0, +1) * 255.0)
     */
   return (uint8_t) (int)
          _mesa_roundevenf(CLAMP(x, 0.0f, 1.0f) * 255.0f);
}

/**
 * Evaluate one component packUnorm2x16.
 */
static uint16_t
pack_unorm_1x16(float x)
{
    /* From section 8.4 of the GLSL ES 3.00 spec:
     *
     *    packUnorm2x16
     *    -------------
     *    The conversion for component c of v to fixed point is done as
     *    follows:
     *
     *       packUnorm2x16: round(clamp(c, 0, +1) * 65535.0)
     */
   return (uint16_t) (int)
          _mesa_roundevenf(CLAMP(x, 0.0f, 1.0f) * 65535.0f);
}

/**
 * Evaluate one component of unpackUnorm4x8.
 */
static float
unpack_unorm_1x8(uint8_t u)
{
    /* From section 8.4 of the GLSL 4.30 spec:
     *
     *    unpackUnorm4x8
     *    --------------
     *    The conversion for unpacked fixed-point value f to floating point is
     *    done as follows:
     *
     *       unpackUnorm4x8: f / 255.0
     */
   return (float) u / 255.0f;
}

/**
 * Evaluate one component of unpackUnorm2x16.
 */
static float
unpack_unorm_1x16(uint16_t u)
{
    /* From section 8.4 of the GLSL ES 3.00 spec:
     *
     *    unpackUnorm2x16
     *    ---------------
     *    The conversion for unpacked fixed-point value f to floating point is
     *    done as follows:
     *
     *       unpackUnorm2x16: f / 65535.0
     */
   return (float) u / 65535.0f;
}

/**
 * Evaluate one component of packHalf2x16.
 */
static uint16_t
pack_half_1x16(float x)
{
   return _mesa_float_to_half(x);
}

/**
 * Evaluate one component of unpackHalf2x16.
 */
static float
unpack_half_1x16(uint16_t u)
{
   return _mesa_half_to_float(u);
}

/* Some typed vector structures to make things like src0.y work */
typedef float float16_t;
typedef float float32_t;
typedef double float64_t;
typedef bool bool32_t;
% for type in ["float", "int", "uint"]:
% for width in type_sizes(type):
struct ${type}${width}_vec {
   ${type}${width}_t x;
   ${type}${width}_t y;
   ${type}${width}_t z;
   ${type}${width}_t w;
};
% endfor
% endfor

struct bool32_vec {
    bool x;
    bool y;
    bool z;
    bool w;
};

<%def name="evaluate_op(op, bit_size)">
   <%
   output_type = type_add_size(op.output_type, bit_size)
   input_types = [type_add_size(type_, bit_size) for type_ in op.input_types]
   %>

   ## For each non-per-component input, create a variable srcN that
   ## contains x, y, z, and w elements which are filled in with the
   ## appropriately-typed values.
   % for j in range(op.num_inputs):
      % if op.input_sizes[j] == 0:
         <% continue %>
      % elif "src" + str(j) not in op.const_expr:
         ## Avoid unused variable warnings
         <% continue %>
      %endif

      const struct ${input_types[j]}_vec src${j} = {
      % for k in range(op.input_sizes[j]):
         % if input_types[j] == "bool32":
            _src[${j}].u32[${k}] != 0,
         % elif input_types[j] == "float16":
            _mesa_half_to_float(_src[${j}].u16[${k}]),
         % else:
            _src[${j}].${get_const_field(input_types[j])}[${k}],
         % endif
      % endfor
      % for k in range(op.input_sizes[j], 4):
         0,
      % endfor
      };
   % endfor

   % if op.output_size == 0:
      ## For per-component instructions, we need to iterate over the
      ## components and apply the constant expression one component
      ## at a time.
      for (unsigned _i = 0; _i < num_components; _i++) {
         ## For each per-component input, create a variable srcN that
         ## contains the value of the current (_i'th) component.
         % for j in range(op.num_inputs):
            % if op.input_sizes[j] != 0:
               <% continue %>
            % elif "src" + str(j) not in op.const_expr:
               ## Avoid unused variable warnings
               <% continue %>
            % elif input_types[j] == "bool32":
               const bool src${j} = _src[${j}].u32[_i] != 0;
            % elif input_types[j] == "float16":
               const float src${j} =
                  _mesa_half_to_float(_src[${j}].u16[_i]);
            % else:
               const ${input_types[j]}_t src${j} =
                  _src[${j}].${get_const_field(input_types[j])}[_i];
            % endif
         % endfor

         ## Create an appropriately-typed variable dst and assign the
         ## result of the const_expr to it.  If const_expr already contains
         ## writes to dst, just include const_expr directly.
         % if "dst" in op.const_expr:
            ${output_type}_t dst;

            ${op.const_expr}
         % else:
            ${output_type}_t dst = ${op.const_expr};
         % endif

         ## Store the current component of the actual destination to the
         ## value of dst.
         % if output_type == "bool32":
            ## Sanitize the C value to a proper NIR bool
            _dst_val.u32[_i] = dst ? NIR_TRUE : NIR_FALSE;
         % elif output_type == "float16":
            _dst_val.u16[_i] = _mesa_float_to_half(dst);
         % else:
            _dst_val.${get_const_field(output_type)}[_i] = dst;
         % endif
      }
   % else:
      ## In the non-per-component case, create a struct dst with
      ## appropriately-typed elements x, y, z, and w and assign the result
      ## of the const_expr to all components of dst, or include the
      ## const_expr directly if it writes to dst already.
      struct ${output_type}_vec dst;

      % if "dst" in op.const_expr:
         ${op.const_expr}
      % else:
         ## Splat the value to all components.  This way expressions which
         ## write the same value to all components don't need to explicitly
         ## write to dest.  One such example is fnoise which has a
         ## const_expr of 0.0f.
         dst.x = dst.y = dst.z = dst.w = ${op.const_expr};
      % endif

      ## For each component in the destination, copy the value of dst to
      ## the actual destination.
      % for k in range(op.output_size):
         % if output_type == "bool32":
            ## Sanitize the C value to a proper NIR bool
            _dst_val.u32[${k}] = dst.${"xyzw"[k]} ? NIR_TRUE : NIR_FALSE;
         % elif output_type == "float16":
            _dst_val.u16[${k}] = _mesa_float_to_half(dst.${"xyzw"[k]});
         % else:
            _dst_val.${get_const_field(output_type)}[${k}] = dst.${"xyzw"[k]};
         % endif
      % endfor
   % endif
</%def>

% for name, op in sorted(opcodes.iteritems()):
static nir_const_value
evaluate_${name}(MAYBE_UNUSED unsigned num_components,
                 ${"UNUSED" if op_bit_sizes(op) is None else ""} unsigned bit_size,
                 MAYBE_UNUSED nir_const_value *_src)
{
   nir_const_value _dst_val = { {0, } };

   % if op_bit_sizes(op) is not None:
      switch (bit_size) {
      % for bit_size in op_bit_sizes(op):
      case ${bit_size}: {
         ${evaluate_op(op, bit_size)}
         break;
      }
      % endfor

      default:
         unreachable("unknown bit width");
      }
   % else:
      ${evaluate_op(op, 0)}
   % endif

   return _dst_val;
}
% endfor

nir_const_value
nir_eval_const_opcode(nir_op op, unsigned num_components,
                      unsigned bit_width, nir_const_value *src)
{
   switch (op) {
% for name in sorted(opcodes.iterkeys()):
   case nir_op_${name}:
      return evaluate_${name}(num_components, bit_width, src);
% endfor
   default:
      unreachable("shouldn't get here");
   }
}"""

from nir_opcodes import opcodes
from mako.template import Template

print Template(template).render(opcodes=opcodes, type_sizes=type_sizes,
                                type_has_size=type_has_size,
                                type_add_size=type_add_size,
                                op_bit_sizes=op_bit_sizes,
                                get_const_field=get_const_field)
