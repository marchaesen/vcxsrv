#! /usr/bin/python2
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
#include "main/core.h"
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
% for type in ["float", "int", "uint", "bool"]:
struct ${type}_vec {
   ${type} x;
   ${type} y;
   ${type} z;
   ${type} w;
};
% endfor

% for name, op in sorted(opcodes.iteritems()):
static nir_const_value
evaluate_${name}(unsigned num_components, nir_const_value *_src)
{
   nir_const_value _dst_val = { { {0, 0, 0, 0} } };

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

      struct ${op.input_types[j]}_vec src${j} = {
      % for k in range(op.input_sizes[j]):
         % if op.input_types[j] == "bool":
            _src[${j}].u[${k}] != 0,
         % else:
            _src[${j}].${op.input_types[j][:1]}[${k}],
         % endif
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
            % elif op.input_types[j] == "bool":
               bool src${j} = _src[${j}].u[_i] != 0;
            % else:
               ${op.input_types[j]} src${j} = _src[${j}].${op.input_types[j][:1]}[_i];
            % endif
         % endfor

         ## Create an appropriately-typed variable dst and assign the
         ## result of the const_expr to it.  If const_expr already contains
         ## writes to dst, just include const_expr directly.
         % if "dst" in op.const_expr:
            ${op.output_type} dst;
            ${op.const_expr}
         % else:
            ${op.output_type} dst = ${op.const_expr};
         % endif

         ## Store the current component of the actual destination to the
         ## value of dst.
         % if op.output_type == "bool":
            ## Sanitize the C value to a proper NIR bool
            _dst_val.u[_i] = dst ? NIR_TRUE : NIR_FALSE;
         % else:
            _dst_val.${op.output_type[:1]}[_i] = dst;
         % endif
      }
   % else:
      ## In the non-per-component case, create a struct dst with
      ## appropriately-typed elements x, y, z, and w and assign the result
      ## of the const_expr to all components of dst, or include the
      ## const_expr directly if it writes to dst already.
      struct ${op.output_type}_vec dst;

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
         % if op.output_type == "bool":
            ## Sanitize the C value to a proper NIR bool
            _dst_val.u[${k}] = dst.${"xyzw"[k]} ? NIR_TRUE : NIR_FALSE;
         % else:
            _dst_val.${op.output_type[:1]}[${k}] = dst.${"xyzw"[k]};
         % endif
      % endfor
   % endif

   return _dst_val;
}
% endfor

nir_const_value
nir_eval_const_opcode(nir_op op, unsigned num_components,
                      nir_const_value *src)
{
   switch (op) {
% for name in sorted(opcodes.iterkeys()):
   case nir_op_${name}: {
      return evaluate_${name}(num_components, src);
      break;
   }
% endfor
   default:
      unreachable("shouldn't get here");
   }
}"""

from nir_opcodes import opcodes
from mako.template import Template

print Template(template).render(opcodes=opcodes)
