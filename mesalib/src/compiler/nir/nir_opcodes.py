#
# Copyright (C) 2014 Connor Abbott
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
#    Connor Abbott (cwabbott0@gmail.com)


# Class that represents all the information we have about the opcode
# NOTE: this must be kept in sync with nir_op_info

class Opcode(object):
   """Class that represents all the information we have about the opcode
   NOTE: this must be kept in sync with nir_op_info
   """
   def __init__(self, name, output_size, output_type, input_sizes,
                input_types, algebraic_properties, const_expr):
      """Parameters:

      - name is the name of the opcode (prepend nir_op_ for the enum name)
      - all types are strings that get nir_type_ prepended to them
      - input_types is a list of types
      - algebraic_properties is a space-seperated string, where nir_op_is_ is
        prepended before each entry
      - const_expr is an expression or series of statements that computes the
        constant value of the opcode given the constant values of its inputs.

      Constant expressions are formed from the variables src0, src1, ...,
      src(N-1), where N is the number of arguments.  The output of the
      expression should be stored in the dst variable.  Per-component input
      and output variables will be scalars and non-per-component input and
      output variables will be a struct with fields named x, y, z, and w
      all of the correct type.  Input and output variables can be assumed
      to already be of the correct type and need no conversion.  In
      particular, the conversion from the C bool type to/from  NIR_TRUE and
      NIR_FALSE happens automatically.

      For per-component instructions, the entire expression will be
      executed once for each component.  For non-per-component
      instructions, the expression is expected to store the correct values
      in dst.x, dst.y, etc.  If "dst" does not exist anywhere in the
      constant expression, an assignment to dst will happen automatically
      and the result will be equivalent to "dst = <expression>" for
      per-component instructions and "dst.x = dst.y = ... = <expression>"
      for non-per-component instructions.
      """
      assert isinstance(name, str)
      assert isinstance(output_size, int)
      assert isinstance(output_type, str)
      assert isinstance(input_sizes, list)
      assert isinstance(input_sizes[0], int)
      assert isinstance(input_types, list)
      assert isinstance(input_types[0], str)
      assert isinstance(algebraic_properties, str)
      assert isinstance(const_expr, str)
      assert len(input_sizes) == len(input_types)
      assert 0 <= output_size <= 4
      for size in input_sizes:
         assert 0 <= size <= 4
         if output_size != 0:
            assert size != 0
      self.name = name
      self.num_inputs = len(input_sizes)
      self.output_size = output_size
      self.output_type = output_type
      self.input_sizes = input_sizes
      self.input_types = input_types
      self.algebraic_properties = algebraic_properties
      self.const_expr = const_expr

# helper variables for strings
tfloat = "float"
tint = "int"
tbool = "bool32"
tuint = "uint"
tfloat32 = "float32"
tint32 = "int32"
tuint32 = "uint32"
tint64 = "int64"
tuint64 = "uint64"
tfloat64 = "float64"

commutative = "commutative "
associative = "associative "

# global dictionary of opcodes
opcodes = {}

def opcode(name, output_size, output_type, input_sizes, input_types,
           algebraic_properties, const_expr):
   assert name not in opcodes
   opcodes[name] = Opcode(name, output_size, output_type, input_sizes,
                          input_types, algebraic_properties, const_expr)

def unop_convert(name, out_type, in_type, const_expr):
   opcode(name, 0, out_type, [0], [in_type], "", const_expr)

def unop(name, ty, const_expr):
   opcode(name, 0, ty, [0], [ty], "", const_expr)

def unop_horiz(name, output_size, output_type, input_size, input_type,
               const_expr):
   opcode(name, output_size, output_type, [input_size], [input_type], "",
          const_expr)

def unop_reduce(name, output_size, output_type, input_type, prereduce_expr,
                reduce_expr, final_expr):
   def prereduce(src):
      return "(" + prereduce_expr.format(src=src) + ")"
   def final(src):
      return final_expr.format(src="(" + src + ")")
   def reduce_(src0, src1):
      return reduce_expr.format(src0=src0, src1=src1)
   src0 = prereduce("src0.x")
   src1 = prereduce("src0.y")
   src2 = prereduce("src0.z")
   src3 = prereduce("src0.w")
   unop_horiz(name + "2", output_size, output_type, 2, input_type,
              final(reduce_(src0, src1)))
   unop_horiz(name + "3", output_size, output_type, 3, input_type,
              final(reduce_(reduce_(src0, src1), src2)))
   unop_horiz(name + "4", output_size, output_type, 4, input_type,
              final(reduce_(reduce_(src0, src1), reduce_(src2, src3))))


# These two move instructions differ in what modifiers they support and what
# the negate modifier means. Otherwise, they are identical.
unop("fmov", tfloat, "src0")
unop("imov", tint, "src0")

unop("ineg", tint, "-src0")
unop("fneg", tfloat, "-src0")
unop("inot", tint, "~src0") # invert every bit of the integer
unop("fnot", tfloat, ("bit_size == 64 ? ((src0 == 0.0) ? 1.0 : 0.0f) : " +
                      "((src0 == 0.0f) ? 1.0f : 0.0f)"))
unop("fsign", tfloat, ("bit_size == 64 ? " +
                       "((src0 == 0.0) ? 0.0 : ((src0 > 0.0) ? 1.0 : -1.0)) : " +
                       "((src0 == 0.0f) ? 0.0f : ((src0 > 0.0f) ? 1.0f : -1.0f))"))
unop("isign", tint, "(src0 == 0) ? 0 : ((src0 > 0) ? 1 : -1)")
unop("iabs", tint, "(src0 < 0) ? -src0 : src0")
unop("fabs", tfloat, "fabs(src0)")
unop("fsat", tfloat, ("bit_size == 64 ? " +
                      "((src0 > 1.0) ? 1.0 : ((src0 <= 0.0) ? 0.0 : src0)) : " +
                      "((src0 > 1.0f) ? 1.0f : ((src0 <= 0.0f) ? 0.0f : src0))"))
unop("frcp", tfloat, "bit_size == 64 ? 1.0 / src0 : 1.0f / src0")
unop("frsq", tfloat, "bit_size == 64 ? 1.0 / sqrt(src0) : 1.0f / sqrtf(src0)")
unop("fsqrt", tfloat, "bit_size == 64 ? sqrt(src0) : sqrtf(src0)")
unop("fexp2", tfloat, "exp2f(src0)")
unop("flog2", tfloat, "log2f(src0)")

# Generate all of the numeric conversion opcodes
for src_t in [tint, tuint, tfloat]:
   if src_t in (tint, tuint):
      dst_types = [tfloat, src_t]
   elif src_t == tfloat:
      dst_types = [tint, tuint, tfloat]

   for dst_t in dst_types:
      if dst_t == tfloat:
         bit_sizes = [16, 32, 64]
      else:
         bit_sizes = [8, 16, 32, 64]
      for bit_size in bit_sizes:
          if bit_size == 16 and dst_t == tfloat and src_t == tfloat:
              rnd_modes = ['rtne', 'rtz', 'undef']
              for rnd_mode in rnd_modes:
                  unop_convert("{0}2{1}{2}_{3}".format(src_t[0], dst_t[0],
                                                       bit_size, rnd_mode),
                               dst_t + str(bit_size), src_t, "src0")
          else:
              unop_convert("{0}2{1}{2}".format(src_t[0], dst_t[0], bit_size),
                           dst_t + str(bit_size), src_t, "src0")

# We'll hand-code the to/from bool conversion opcodes.  Because bool doesn't
# have multiple bit-sizes, we can always infer the size from the other type.
unop_convert("f2b", tbool, tfloat, "src0 != 0.0")
unop_convert("i2b", tbool, tint, "src0 != 0")
unop_convert("b2f", tfloat, tbool, "src0 ? 1.0 : 0.0")
unop_convert("b2i", tint, tbool, "src0 ? 1 : 0")


# Unary floating-point rounding operations.


unop("ftrunc", tfloat, "bit_size == 64 ? trunc(src0) : truncf(src0)")
unop("fceil", tfloat, "bit_size == 64 ? ceil(src0) : ceilf(src0)")
unop("ffloor", tfloat, "bit_size == 64 ? floor(src0) : floorf(src0)")
unop("ffract", tfloat, "src0 - (bit_size == 64 ? floor(src0) : floorf(src0))")
unop("fround_even", tfloat, "bit_size == 64 ? _mesa_roundeven(src0) : _mesa_roundevenf(src0)")

unop("fquantize2f16", tfloat, "(fabs(src0) < ldexpf(1.0, -14)) ? copysignf(0.0f, src0) : _mesa_half_to_float(_mesa_float_to_half(src0))")

# Trigonometric operations.


unop("fsin", tfloat, "bit_size == 64 ? sin(src0) : sinf(src0)")
unop("fcos", tfloat, "bit_size == 64 ? cos(src0) : cosf(src0)")

# dfrexp
unop_convert("frexp_exp", tint32, tfloat64, "frexp(src0, &dst);")
unop_convert("frexp_sig", tfloat64, tfloat64, "int n; dst = frexp(src0, &n);")

# Partial derivatives.


unop("fddx", tfloat, "0.0") # the derivative of a constant is 0.
unop("fddy", tfloat, "0.0")
unop("fddx_fine", tfloat, "0.0")
unop("fddy_fine", tfloat, "0.0")
unop("fddx_coarse", tfloat, "0.0")
unop("fddy_coarse", tfloat, "0.0")


# Floating point pack and unpack operations.

def pack_2x16(fmt):
   unop_horiz("pack_" + fmt + "_2x16", 1, tuint32, 2, tfloat32, """
dst.x = (uint32_t) pack_fmt_1x16(src0.x);
dst.x |= ((uint32_t) pack_fmt_1x16(src0.y)) << 16;
""".replace("fmt", fmt))

def pack_4x8(fmt):
   unop_horiz("pack_" + fmt + "_4x8", 1, tuint32, 4, tfloat32, """
dst.x = (uint32_t) pack_fmt_1x8(src0.x);
dst.x |= ((uint32_t) pack_fmt_1x8(src0.y)) << 8;
dst.x |= ((uint32_t) pack_fmt_1x8(src0.z)) << 16;
dst.x |= ((uint32_t) pack_fmt_1x8(src0.w)) << 24;
""".replace("fmt", fmt))

def unpack_2x16(fmt):
   unop_horiz("unpack_" + fmt + "_2x16", 2, tfloat32, 1, tuint32, """
dst.x = unpack_fmt_1x16((uint16_t)(src0.x & 0xffff));
dst.y = unpack_fmt_1x16((uint16_t)(src0.x << 16));
""".replace("fmt", fmt))

def unpack_4x8(fmt):
   unop_horiz("unpack_" + fmt + "_4x8", 4, tfloat32, 1, tuint32, """
dst.x = unpack_fmt_1x8((uint8_t)(src0.x & 0xff));
dst.y = unpack_fmt_1x8((uint8_t)((src0.x >> 8) & 0xff));
dst.z = unpack_fmt_1x8((uint8_t)((src0.x >> 16) & 0xff));
dst.w = unpack_fmt_1x8((uint8_t)(src0.x >> 24));
""".replace("fmt", fmt))


pack_2x16("snorm")
pack_4x8("snorm")
pack_2x16("unorm")
pack_4x8("unorm")
pack_2x16("half")
unpack_2x16("snorm")
unpack_4x8("snorm")
unpack_2x16("unorm")
unpack_4x8("unorm")
unpack_2x16("half")

unop_horiz("pack_uvec2_to_uint", 1, tuint32, 2, tuint32, """
dst.x = (src0.x & 0xffff) | (src0.y << 16);
""")

unop_horiz("pack_uvec4_to_uint", 1, tuint32, 4, tuint32, """
dst.x = (src0.x <<  0) |
        (src0.y <<  8) |
        (src0.z << 16) |
        (src0.w << 24);
""")

unop_horiz("pack_64_2x32", 1, tuint64, 2, tuint32,
           "dst.x = src0.x | ((uint64_t)src0.y << 32);")

unop_horiz("unpack_64_2x32", 2, tuint32, 1, tuint64,
           "dst.x = src0.x; dst.y = src0.x >> 32;")

# Lowered floating point unpacking operations.


unop_horiz("unpack_half_2x16_split_x", 1, tfloat32, 1, tuint32,
           "unpack_half_1x16((uint16_t)(src0.x & 0xffff))")
unop_horiz("unpack_half_2x16_split_y", 1, tfloat32, 1, tuint32,
           "unpack_half_1x16((uint16_t)(src0.x >> 16))")

unop_convert("unpack_64_2x32_split_x", tuint32, tuint64, "src0")
unop_convert("unpack_64_2x32_split_y", tuint32, tuint64, "src0 >> 32")

# Bit operations, part of ARB_gpu_shader5.


unop("bitfield_reverse", tuint32, """
/* we're not winning any awards for speed here, but that's ok */
dst = 0;
for (unsigned bit = 0; bit < 32; bit++)
   dst |= ((src0 >> bit) & 1) << (31 - bit);
""")
unop("bit_count", tuint32, """
dst = 0;
for (unsigned bit = 0; bit < 32; bit++) {
   if ((src0 >> bit) & 1)
      dst++;
}
""")

unop_convert("ufind_msb", tint32, tuint32, """
dst = -1;
for (int bit = 31; bit >= 0; bit--) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}
""")

unop("ifind_msb", tint32, """
dst = -1;
for (int bit = 31; bit >= 0; bit--) {
   /* If src0 < 0, we're looking for the first 0 bit.
    * if src0 >= 0, we're looking for the first 1 bit.
    */
   if ((((src0 >> bit) & 1) && (src0 >= 0)) ||
      (!((src0 >> bit) & 1) && (src0 < 0))) {
      dst = bit;
      break;
   }
}
""")

unop("find_lsb", tint32, """
dst = -1;
for (unsigned bit = 0; bit < 32; bit++) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}
""")


for i in xrange(1, 5):
   for j in xrange(1, 5):
      unop_horiz("fnoise{0}_{1}".format(i, j), i, tfloat, j, tfloat, "0.0f")


# AMD_gcn_shader extended instructions
unop_horiz("cube_face_coord", 2, tfloat32, 3, tfloat32, """
dst.x = dst.y = 0.0;
float absX = fabs(src0.x);
float absY = fabs(src0.y);
float absZ = fabs(src0.z);
if (src0.x >= 0 && absX >= absY && absX >= absZ) { dst.x = -src0.y; dst.y = -src0.z; }
if (src0.x < 0 && absX >= absY && absX >= absZ) { dst.x = -src0.y; dst.y = src0.z; }
if (src0.y >= 0 && absY >= absX && absY >= absZ) { dst.x = src0.z; dst.y = src0.x; }
if (src0.y < 0 && absY >= absX && absY >= absZ) { dst.x = -src0.z; dst.y = src0.x; }
if (src0.z >= 0 && absZ >= absX && absZ >= absY) { dst.x = -src0.y; dst.y = src0.x; }
if (src0.z < 0 && absZ >= absX && absZ >= absY) { dst.x = -src0.y; dst.y = -src0.x; }
""")

unop_horiz("cube_face_index", 1, tfloat32, 3, tfloat32, """
float absX = fabs(src0.x);
float absY = fabs(src0.y);
float absZ = fabs(src0.z);
if (src0.x >= 0 && absX >= absY && absX >= absZ) dst.x = 0;
if (src0.x < 0 && absX >= absY && absX >= absZ) dst.x = 1;
if (src0.y >= 0 && absY >= absX && absY >= absZ) dst.x = 2;
if (src0.y < 0 && absY >= absX && absY >= absZ) dst.x = 3;
if (src0.z >= 0 && absZ >= absX && absZ >= absY) dst.x = 4;
if (src0.z < 0 && absZ >= absX && absZ >= absY) dst.x = 5;
""")


def binop_convert(name, out_type, in_type, alg_props, const_expr):
   opcode(name, 0, out_type, [0, 0], [in_type, in_type], alg_props, const_expr)

def binop(name, ty, alg_props, const_expr):
   binop_convert(name, ty, ty, alg_props, const_expr)

def binop_compare(name, ty, alg_props, const_expr):
   binop_convert(name, tbool, ty, alg_props, const_expr)

def binop_horiz(name, out_size, out_type, src1_size, src1_type, src2_size,
                src2_type, const_expr):
   opcode(name, out_size, out_type, [src1_size, src2_size], [src1_type, src2_type],
          "", const_expr)

def binop_reduce(name, output_size, output_type, src_type, prereduce_expr,
                 reduce_expr, final_expr):
   def final(src):
      return final_expr.format(src= "(" + src + ")")
   def reduce_(src0, src1):
      return reduce_expr.format(src0=src0, src1=src1)
   def prereduce(src0, src1):
      return "(" + prereduce_expr.format(src0=src0, src1=src1) + ")"
   src0 = prereduce("src0.x", "src1.x")
   src1 = prereduce("src0.y", "src1.y")
   src2 = prereduce("src0.z", "src1.z")
   src3 = prereduce("src0.w", "src1.w")
   opcode(name + "2", output_size, output_type,
          [2, 2], [src_type, src_type], commutative,
          final(reduce_(src0, src1)))
   opcode(name + "3", output_size, output_type,
          [3, 3], [src_type, src_type], commutative,
          final(reduce_(reduce_(src0, src1), src2)))
   opcode(name + "4", output_size, output_type,
          [4, 4], [src_type, src_type], commutative,
          final(reduce_(reduce_(src0, src1), reduce_(src2, src3))))

binop("fadd", tfloat, commutative + associative, "src0 + src1")
binop("iadd", tint, commutative + associative, "src0 + src1")
binop("fsub", tfloat, "", "src0 - src1")
binop("isub", tint, "", "src0 - src1")

binop("fmul", tfloat, commutative + associative, "src0 * src1")
# low 32-bits of signed/unsigned integer multiply
binop("imul", tint, commutative + associative, "src0 * src1")
# high 32-bits of signed integer multiply
binop("imul_high", tint32, commutative,
      "(int32_t)(((int64_t) src0 * (int64_t) src1) >> 32)")
# high 32-bits of unsigned integer multiply
binop("umul_high", tuint32, commutative,
      "(uint32_t)(((uint64_t) src0 * (uint64_t) src1) >> 32)")

binop("fdiv", tfloat, "", "src0 / src1")
binop("idiv", tint, "", "src1 == 0 ? 0 : (src0 / src1)")
binop("udiv", tuint, "", "src1 == 0 ? 0 : (src0 / src1)")

# returns a boolean representing the carry resulting from the addition of
# the two unsigned arguments.

binop_convert("uadd_carry", tuint, tuint, commutative, "src0 + src1 < src0")

# returns a boolean representing the borrow resulting from the subtraction
# of the two unsigned arguments.

binop_convert("usub_borrow", tuint, tuint, "", "src0 < src1")

binop("umod", tuint, "", "src1 == 0 ? 0 : src0 % src1")

# For signed integers, there are several different possible definitions of
# "modulus" or "remainder".  We follow the conventions used by LLVM and
# SPIR-V.  The irem opcode implements the standard C/C++ signed "%"
# operation while the imod opcode implements the more mathematical
# "modulus" operation.  For details on the difference, see
#
# http://mathforum.org/library/drmath/view/52343.html

binop("irem", tint, "", "src1 == 0 ? 0 : src0 % src1")
binop("imod", tint, "",
      "src1 == 0 ? 0 : ((src0 % src1 == 0 || (src0 >= 0) == (src1 >= 0)) ?"
      "                 src0 % src1 : src0 % src1 + src1)")
binop("fmod", tfloat, "", "src0 - src1 * floorf(src0 / src1)")
binop("frem", tfloat, "", "src0 - src1 * truncf(src0 / src1)")

#
# Comparisons
#


# these integer-aware comparisons return a boolean (0 or ~0)

binop_compare("flt", tfloat, "", "src0 < src1")
binop_compare("fge", tfloat, "", "src0 >= src1")
binop_compare("feq", tfloat, commutative, "src0 == src1")
binop_compare("fne", tfloat, commutative, "src0 != src1")
binop_compare("ilt", tint, "", "src0 < src1")
binop_compare("ige", tint, "", "src0 >= src1")
binop_compare("ieq", tint, commutative, "src0 == src1")
binop_compare("ine", tint, commutative, "src0 != src1")
binop_compare("ult", tuint, "", "src0 < src1")
binop_compare("uge", tuint, "", "src0 >= src1")

# integer-aware GLSL-style comparisons that compare floats and ints

binop_reduce("ball_fequal",  1, tbool, tfloat, "{src0} == {src1}",
             "{src0} && {src1}", "{src}")
binop_reduce("bany_fnequal", 1, tbool, tfloat, "{src0} != {src1}",
             "{src0} || {src1}", "{src}")
binop_reduce("ball_iequal",  1, tbool, tint, "{src0} == {src1}",
             "{src0} && {src1}", "{src}")
binop_reduce("bany_inequal", 1, tbool, tint, "{src0} != {src1}",
             "{src0} || {src1}", "{src}")

# non-integer-aware GLSL-style comparisons that return 0.0 or 1.0

binop_reduce("fall_equal",  1, tfloat32, tfloat32, "{src0} == {src1}",
             "{src0} && {src1}", "{src} ? 1.0f : 0.0f")
binop_reduce("fany_nequal", 1, tfloat32, tfloat32, "{src0} != {src1}",
             "{src0} || {src1}", "{src} ? 1.0f : 0.0f")

# These comparisons for integer-less hardware return 1.0 and 0.0 for true
# and false respectively

binop("slt", tfloat32, "", "(src0 < src1) ? 1.0f : 0.0f") # Set on Less Than
binop("sge", tfloat, "", "(src0 >= src1) ? 1.0f : 0.0f") # Set on Greater or Equal
binop("seq", tfloat32, commutative, "(src0 == src1) ? 1.0f : 0.0f") # Set on Equal
binop("sne", tfloat32, commutative, "(src0 != src1) ? 1.0f : 0.0f") # Set on Not Equal


opcode("ishl", 0, tint, [0, 0], [tint, tuint32], "", "src0 << src1")
opcode("ishr", 0, tint, [0, 0], [tint, tuint32], "", "src0 >> src1")
opcode("ushr", 0, tuint, [0, 0], [tuint, tuint32], "", "src0 >> src1")

# bitwise logic operators
#
# These are also used as boolean and, or, xor for hardware supporting
# integers.


binop("iand", tuint, commutative + associative, "src0 & src1")
binop("ior", tuint, commutative + associative, "src0 | src1")
binop("ixor", tuint, commutative + associative, "src0 ^ src1")


# floating point logic operators
#
# These use (src != 0.0) for testing the truth of the input, and output 1.0
# for true and 0.0 for false

binop("fand", tfloat32, commutative,
      "((src0 != 0.0f) && (src1 != 0.0f)) ? 1.0f : 0.0f")
binop("for", tfloat32, commutative,
      "((src0 != 0.0f) || (src1 != 0.0f)) ? 1.0f : 0.0f")
binop("fxor", tfloat32, commutative,
      "(src0 != 0.0f && src1 == 0.0f) || (src0 == 0.0f && src1 != 0.0f) ? 1.0f : 0.0f")

binop_reduce("fdot", 1, tfloat, tfloat, "{src0} * {src1}", "{src0} + {src1}",
             "{src}")

binop_reduce("fdot_replicated", 4, tfloat, tfloat,
             "{src0} * {src1}", "{src0} + {src1}", "{src}")

opcode("fdph", 1, tfloat, [3, 4], [tfloat, tfloat], "",
       "src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w")
opcode("fdph_replicated", 4, tfloat, [3, 4], [tfloat, tfloat], "",
       "src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w")

binop("fmin", tfloat, "", "fminf(src0, src1)")
binop("imin", tint, commutative + associative, "src1 > src0 ? src0 : src1")
binop("umin", tuint, commutative + associative, "src1 > src0 ? src0 : src1")
binop("fmax", tfloat, "", "fmaxf(src0, src1)")
binop("imax", tint, commutative + associative, "src1 > src0 ? src1 : src0")
binop("umax", tuint, commutative + associative, "src1 > src0 ? src1 : src0")

# Saturated vector add for 4 8bit ints.
binop("usadd_4x8", tint32, commutative + associative, """
dst = 0;
for (int i = 0; i < 32; i += 8) {
   dst |= MIN2(((src0 >> i) & 0xff) + ((src1 >> i) & 0xff), 0xff) << i;
}
""")

# Saturated vector subtract for 4 8bit ints.
binop("ussub_4x8", tint32, "", """
dst = 0;
for (int i = 0; i < 32; i += 8) {
   int src0_chan = (src0 >> i) & 0xff;
   int src1_chan = (src1 >> i) & 0xff;
   if (src0_chan > src1_chan)
      dst |= (src0_chan - src1_chan) << i;
}
""")

# vector min for 4 8bit ints.
binop("umin_4x8", tint32, commutative + associative, """
dst = 0;
for (int i = 0; i < 32; i += 8) {
   dst |= MIN2((src0 >> i) & 0xff, (src1 >> i) & 0xff) << i;
}
""")

# vector max for 4 8bit ints.
binop("umax_4x8", tint32, commutative + associative, """
dst = 0;
for (int i = 0; i < 32; i += 8) {
   dst |= MAX2((src0 >> i) & 0xff, (src1 >> i) & 0xff) << i;
}
""")

# unorm multiply: (a * b) / 255.
binop("umul_unorm_4x8", tint32, commutative + associative, """
dst = 0;
for (int i = 0; i < 32; i += 8) {
   int src0_chan = (src0 >> i) & 0xff;
   int src1_chan = (src1 >> i) & 0xff;
   dst |= ((src0_chan * src1_chan) / 255) << i;
}
""")

binop("fpow", tfloat, "", "bit_size == 64 ? powf(src0, src1) : pow(src0, src1)")

binop_horiz("pack_half_2x16_split", 1, tuint32, 1, tfloat32, 1, tfloat32,
            "pack_half_1x16(src0.x) | (pack_half_1x16(src1.x) << 16)")

binop_convert("pack_64_2x32_split", tuint64, tuint32, "",
              "src0 | ((uint64_t)src1 << 32)")

# bfm implements the behavior of the first operation of the SM5 "bfi" assembly
# and that of the "bfi1" i965 instruction. That is, it has undefined behavior
# if either of its arguments are 32.
binop_convert("bfm", tuint32, tint32, "", """
int bits = src0, offset = src1;
if (offset < 0 || bits < 0 || offset > 31 || bits > 31 || offset + bits > 32)
   dst = 0; /* undefined */
else
   dst = ((1u << bits) - 1) << offset;
""")

opcode("ldexp", 0, tfloat, [0, 0], [tfloat, tint32], "", """
dst = (bit_size == 64) ? ldexp(src0, src1) : ldexpf(src0, src1);
/* flush denormals to zero. */
if (!isnormal(dst))
   dst = copysignf(0.0f, src0);
""")

# Combines the first component of each input to make a 2-component vector.

binop_horiz("vec2", 2, tuint, 1, tuint, 1, tuint, """
dst.x = src0.x;
dst.y = src1.x;
""")

# Byte extraction
binop("extract_u8", tuint, "", "(uint8_t)(src0 >> (src1 * 8))")
binop("extract_i8", tint, "", "(int8_t)(src0 >> (src1 * 8))")

# Word extraction
binop("extract_u16", tuint, "", "(uint16_t)(src0 >> (src1 * 16))")
binop("extract_i16", tint, "", "(int16_t)(src0 >> (src1 * 16))")


def triop(name, ty, const_expr):
   opcode(name, 0, ty, [0, 0, 0], [ty, ty, ty], "", const_expr)
def triop_horiz(name, output_size, src1_size, src2_size, src3_size, const_expr):
   opcode(name, output_size, tuint,
   [src1_size, src2_size, src3_size],
   [tuint, tuint, tuint], "", const_expr)

triop("ffma", tfloat, "src0 * src1 + src2")

triop("flrp", tfloat, "src0 * (1 - src2) + src1 * src2")

# Conditional Select
#
# A vector conditional select instruction (like ?:, but operating per-
# component on vectors). There are two versions, one for floating point
# bools (0.0 vs 1.0) and one for integer bools (0 vs ~0).


triop("fcsel", tfloat32, "(src0 != 0.0f) ? src1 : src2")

# 3 way min/max/med
triop("fmin3", tfloat, "fminf(src0, fminf(src1, src2))")
triop("imin3", tint, "MIN2(src0, MIN2(src1, src2))")
triop("umin3", tuint, "MIN2(src0, MIN2(src1, src2))")

triop("fmax3", tfloat, "fmaxf(src0, fmaxf(src1, src2))")
triop("imax3", tint, "MAX2(src0, MAX2(src1, src2))")
triop("umax3", tuint, "MAX2(src0, MAX2(src1, src2))")

triop("fmed3", tfloat, "fmaxf(fminf(fmaxf(src0, src1), src2), fminf(src0, src1))")
triop("imed3", tint, "MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1))")
triop("umed3", tuint, "MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1))")

opcode("bcsel", 0, tuint, [0, 0, 0],
      [tbool, tuint, tuint], "", "src0 ? src1 : src2")

# SM5 bfi assembly
triop("bfi", tuint32, """
unsigned mask = src0, insert = src1, base = src2;
if (mask == 0) {
   dst = base;
} else {
   unsigned tmp = mask;
   while (!(tmp & 1)) {
      tmp >>= 1;
      insert <<= 1;
   }
   dst = (base & ~mask) | (insert & mask);
}
""")

# SM5 ubfe/ibfe assembly
opcode("ubfe", 0, tuint32,
       [0, 0, 0], [tuint32, tint32, tint32], "", """
unsigned base = src0;
int offset = src1, bits = src2;
if (bits == 0) {
   dst = 0;
} else if (bits < 0 || offset < 0) {
   dst = 0; /* undefined */
} else if (offset + bits < 32) {
   dst = (base << (32 - bits - offset)) >> (32 - bits);
} else {
   dst = base >> offset;
}
""")
opcode("ibfe", 0, tint32,
       [0, 0, 0], [tint32, tint32, tint32], "", """
int base = src0;
int offset = src1, bits = src2;
if (bits == 0) {
   dst = 0;
} else if (bits < 0 || offset < 0) {
   dst = 0; /* undefined */
} else if (offset + bits < 32) {
   dst = (base << (32 - bits - offset)) >> (32 - bits);
} else {
   dst = base >> offset;
}
""")

# GLSL bitfieldExtract()
opcode("ubitfield_extract", 0, tuint32,
       [0, 0, 0], [tuint32, tint32, tint32], "", """
unsigned base = src0;
int offset = src1, bits = src2;
if (bits == 0) {
   dst = 0;
} else if (bits < 0 || offset < 0 || offset + bits > 32) {
   dst = 0; /* undefined per the spec */
} else {
   dst = (base >> offset) & ((1ull << bits) - 1);
}
""")
opcode("ibitfield_extract", 0, tint32,
       [0, 0, 0], [tint32, tint32, tint32], "", """
int base = src0;
int offset = src1, bits = src2;
if (bits == 0) {
   dst = 0;
} else if (offset < 0 || bits < 0 || offset + bits > 32) {
   dst = 0;
} else {
   dst = (base << (32 - offset - bits)) >> offset; /* use sign-extending shift */
}
""")

# Combines the first component of each input to make a 3-component vector.

triop_horiz("vec3", 3, 1, 1, 1, """
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;
""")

def quadop_horiz(name, output_size, src1_size, src2_size, src3_size,
                 src4_size, const_expr):
   opcode(name, output_size, tuint,
          [src1_size, src2_size, src3_size, src4_size],
          [tuint, tuint, tuint, tuint],
          "", const_expr)

opcode("bitfield_insert", 0, tuint32, [0, 0, 0, 0],
       [tuint32, tuint32, tint32, tint32], "", """
unsigned base = src0, insert = src1;
int offset = src2, bits = src3;
if (bits == 0) {
   dst = base;
} else if (offset < 0 || bits < 0 || bits + offset > 32) {
   dst = 0;
} else {
   unsigned mask = ((1ull << bits) - 1) << offset;
   dst = (base & ~mask) | ((insert << offset) & mask);
}
""")

quadop_horiz("vec4", 4, 1, 1, 1, 1, """
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;
dst.w = src3.x;
""")


