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

from collections import OrderedDict
import nir_algebraic
from nir_opcodes import type_sizes
import itertools
from math import pi

# Convenience variables
a = 'a'
b = 'b'
c = 'c'
d = 'd'
e = 'e'

# Written in the form (<search>, <replace>) where <search> is an expression
# and <replace> is either an expression or a value.  An expression is
# defined as a tuple of the form ([~]<op>, <src0>, <src1>, <src2>, <src3>)
# where each source is either an expression or a value.  A value can be
# either a numeric constant or a string representing a variable name.
#
# If the opcode in a search expression is prefixed by a '~' character, this
# indicates that the operation is inexact.  Such operations will only get
# applied to SSA values that do not have the exact bit set.  This should be
# used by by any optimizations that are not bit-for-bit exact.  It should not,
# however, be used for backend-requested lowering operations as those need to
# happen regardless of precision.
#
# Variable names are specified as "[#]name[@type][(cond)][.swiz]" where:
# "#" indicates that the given variable will only match constants,
# type indicates that the given variable will only match values from ALU
#    instructions with the given output type,
# (cond) specifies an additional condition function (see nir_search_helpers.h),
# swiz is a swizzle applied to the variable (only in the <replace> expression)
#
# For constants, you have to be careful to make sure that it is the right
# type because python is unaware of the source and destination types of the
# opcodes.
#
# All expression types can have a bit-size specified.  For opcodes, this
# looks like "op@32", for variables it is "a@32" or "a@uint32" to specify a
# type and size.  In the search half of the expression this indicates that it
# should only match that particular bit-size.  In the replace half of the
# expression this indicates that the constructed value should have that
# bit-size.
#
# A special condition "many-comm-expr" can be used with expressions to note
# that the expression and its subexpressions have more commutative expressions
# than nir_replace_instr can handle.  If this special condition is needed with
# another condition, the two can be separated by a comma (e.g.,
# "(many-comm-expr,is_used_once)").

# based on https://web.archive.org/web/20180105155939/http://forum.devmaster.net/t/fast-and-accurate-sine-cosine/9648
def lowered_sincos(c):
    x = ('fsub', ('fmul', 2.0, ('ffract', ('fadd', ('fmul', 0.5 / pi, a), c))), 1.0)
    x = ('fmul', ('fsub', x, ('fmul', x, ('fabs', x))), 4.0)
    return ('ffma', ('ffma', x, ('fabs', x), ('fneg', x)), 0.225, x)

optimizations = [

   (('imul', a, '#b@32(is_pos_power_of_two)'), ('ishl', a, ('find_lsb', b)), '!options->lower_bitshift'),
   (('imul', a, '#b@32(is_neg_power_of_two)'), ('ineg', ('ishl', a, ('find_lsb', ('iabs', b)))), '!options->lower_bitshift'),
   (('ishl', a, '#b@32'), ('imul', a, ('ishl', 1, b)), 'options->lower_bitshift'),

   (('unpack_64_2x32_split_x', ('imul_2x32_64(is_used_once)', a, b)), ('imul', a, b)),
   (('unpack_64_2x32_split_x', ('umul_2x32_64(is_used_once)', a, b)), ('imul', a, b)),
   (('imul_2x32_64', a, b), ('pack_64_2x32_split', ('imul', a, b), ('imul_high', a, b)), 'options->lower_mul_2x32_64'),
   (('umul_2x32_64', a, b), ('pack_64_2x32_split', ('imul', a, b), ('umul_high', a, b)), 'options->lower_mul_2x32_64'),
   (('udiv', a, 1), a),
   (('idiv', a, 1), a),
   (('umod', a, 1), 0),
   (('imod', a, 1), 0),
   (('udiv', a, '#b@32(is_pos_power_of_two)'), ('ushr', a, ('find_lsb', b)), '!options->lower_bitshift'),
   (('idiv', a, '#b@32(is_pos_power_of_two)'), ('imul', ('isign', a), ('ushr', ('iabs', a), ('find_lsb', b))), 'options->lower_idiv'),
   (('idiv', a, '#b@32(is_neg_power_of_two)'), ('ineg', ('imul', ('isign', a), ('ushr', ('iabs', a), ('find_lsb', ('iabs', b))))), 'options->lower_idiv'),
   (('umod', a, '#b(is_pos_power_of_two)'),    ('iand', a, ('isub', b, 1))),

   (('fneg', ('fneg', a)), a),
   (('ineg', ('ineg', a)), a),
   (('fabs', ('fabs', a)), ('fabs', a)),
   (('fabs', ('fneg', a)), ('fabs', a)),
   (('fabs', ('u2f', a)), ('u2f', a)),
   (('iabs', ('iabs', a)), ('iabs', a)),
   (('iabs', ('ineg', a)), ('iabs', a)),
   (('f2b', ('fneg', a)), ('f2b', a)),
   (('i2b', ('ineg', a)), ('i2b', a)),
   (('~fadd', a, 0.0), a),
   (('iadd', a, 0), a),
   (('usadd_4x8', a, 0), a),
   (('usadd_4x8', a, ~0), ~0),
   (('~fadd', ('fmul', a, b), ('fmul', a, c)), ('fmul', a, ('fadd', b, c))),
   (('iadd', ('imul', a, b), ('imul', a, c)), ('imul', a, ('iadd', b, c))),
   (('~fadd', ('fneg', a), a), 0.0),
   (('iadd', ('ineg', a), a), 0),
   (('iadd', ('ineg', a), ('iadd', a, b)), b),
   (('iadd', a, ('iadd', ('ineg', a), b)), b),
   (('~fadd', ('fneg', a), ('fadd', a, b)), b),
   (('~fadd', a, ('fadd', ('fneg', a), b)), b),
   (('fadd', ('fsat', a), ('fsat', ('fneg', a))), ('fsat', ('fabs', a))),
   (('~fmul', a, 0.0), 0.0),
   (('imul', a, 0), 0),
   (('umul_unorm_4x8', a, 0), 0),
   (('umul_unorm_4x8', a, ~0), a),
   (('fmul', a, 1.0), a),
   (('imul', a, 1), a),
   (('fmul', a, -1.0), ('fneg', a)),
   (('imul', a, -1), ('ineg', a)),
   # If a < 0: fsign(a)*a*a => -1*a*a => -a*a => abs(a)*a
   # If a > 0: fsign(a)*a*a => 1*a*a => a*a => abs(a)*a
   # If a == 0: fsign(a)*a*a => 0*0*0 => abs(0)*0
   (('fmul', ('fsign', a), ('fmul', a, a)), ('fmul', ('fabs', a), a)),
   (('fmul', ('fmul', ('fsign', a), a), a), ('fmul', ('fabs', a), a)),
   (('~ffma', 0.0, a, b), b),
   (('~ffma', a, b, 0.0), ('fmul', a, b)),
   (('ffma', 1.0, a, b), ('fadd', a, b)),
   (('ffma', -1.0, a, b), ('fadd', ('fneg', a), b)),
   (('~flrp', a, b, 0.0), a),
   (('~flrp', a, b, 1.0), b),
   (('~flrp', a, a, b), a),
   (('~flrp', 0.0, a, b), ('fmul', a, b)),

   # flrp(a, a + b, c) => a + flrp(0, b, c) => a + (b * c)
   (('~flrp', a, ('fadd(is_used_once)', a, b), c), ('fadd', ('fmul', b, c), a)),
   (('~flrp@32', a, ('fadd', a, b), c), ('fadd', ('fmul', b, c), a), 'options->lower_flrp32'),
   (('~flrp@64', a, ('fadd', a, b), c), ('fadd', ('fmul', b, c), a), 'options->lower_flrp64'),

   (('~flrp@32', ('fadd', a, b), ('fadd', a, c), d), ('fadd', ('flrp', b, c, d), a), 'options->lower_flrp32'),
   (('~flrp@64', ('fadd', a, b), ('fadd', a, c), d), ('fadd', ('flrp', b, c, d), a), 'options->lower_flrp64'),

   (('~flrp@32', a, ('fmul(is_used_once)', a, b), c), ('fmul', ('flrp', 1.0, b, c), a), 'options->lower_flrp32'),
   (('~flrp@64', a, ('fmul(is_used_once)', a, b), c), ('fmul', ('flrp', 1.0, b, c), a), 'options->lower_flrp64'),

   (('~flrp', ('fmul(is_used_once)', a, b), ('fmul(is_used_once)', a, c), d), ('fmul', ('flrp', b, c, d), a)),

   (('~flrp', a, b, ('b2f', 'c@1')), ('bcsel', c, b, a), 'options->lower_flrp32'),
   (('~flrp', a, 0.0, c), ('fadd', ('fmul', ('fneg', a), c), a)),
   (('ftrunc', a), ('bcsel', ('flt', a, 0.0), ('fneg', ('ffloor', ('fabs', a))), ('ffloor', ('fabs', a))), 'options->lower_ftrunc'),
   (('ffloor', a), ('fsub', a, ('ffract', a)), 'options->lower_ffloor'),
   (('fadd', a, ('fneg', ('ffract', a))), ('ffloor', a), '!options->lower_ffloor'),
   (('ffract', a), ('fsub', a, ('ffloor', a)), 'options->lower_ffract'),
   (('fceil', a), ('fneg', ('ffloor', ('fneg', a))), 'options->lower_fceil'),
   (('~fadd',    ('fmul', a,          ('fadd', 1.0, ('fneg', ('b2f', 'c@1')))), ('fmul', b, ('b2f',  c))), ('bcsel', c, b, a), 'options->lower_flrp32'),
   (('~fadd@32', ('fmul', a,          ('fadd', 1.0, ('fneg',          c   ) )), ('fmul', b,          c )), ('flrp', a, b, c), '!options->lower_flrp32'),
   (('~fadd@64', ('fmul', a,          ('fadd', 1.0, ('fneg',          c   ) )), ('fmul', b,          c )), ('flrp', a, b, c), '!options->lower_flrp64'),
   # These are the same as the previous three rules, but it depends on
   # 1-fsat(x) <=> fsat(1-x).  See below.
   (('~fadd@32', ('fmul', a, ('fsat', ('fadd', 1.0, ('fneg',          c   )))), ('fmul', b, ('fsat', c))), ('flrp', a, b, ('fsat', c)), '!options->lower_flrp32'),
   (('~fadd@64', ('fmul', a, ('fsat', ('fadd', 1.0, ('fneg',          c   )))), ('fmul', b, ('fsat', c))), ('flrp', a, b, ('fsat', c)), '!options->lower_flrp64'),

   (('~fadd', a, ('fmul', ('b2f', 'c@1'), ('fadd', b, ('fneg', a)))), ('bcsel', c, b, a), 'options->lower_flrp32'),
   (('~fadd@32', a, ('fmul',         c , ('fadd', b, ('fneg', a)))), ('flrp', a, b, c), '!options->lower_flrp32'),
   (('~fadd@64', a, ('fmul',         c , ('fadd', b, ('fneg', a)))), ('flrp', a, b, c), '!options->lower_flrp64'),
   (('ffma', a, b, c), ('fadd', ('fmul', a, b), c), 'options->lower_ffma'),
   (('~fadd', ('fmul', a, b), c), ('ffma', a, b, c), 'options->fuse_ffma'),

   (('~fmul', ('fadd', ('iand', ('ineg', ('b2i32', 'a@bool')), ('fmul', b, c)), '#d'), '#e'),
    ('bcsel', a, ('fmul', ('fadd', ('fmul', b, c), d), e), ('fmul', d, e))),

   (('fdph', a, b), ('fdot4', ('vec4', 'a.x', 'a.y', 'a.z', 1.0), b), 'options->lower_fdph'),

   (('fdot4', ('vec4', a, b,   c,   1.0), d), ('fdph',  ('vec3', a, b, c), d), '!options->lower_fdph'),
   (('fdot4', ('vec4', a, 0.0, 0.0, 0.0), b), ('fmul', a, b)),
   (('fdot4', ('vec4', a, b,   0.0, 0.0), c), ('fdot2', ('vec2', a, b), c)),
   (('fdot4', ('vec4', a, b,   c,   0.0), d), ('fdot3', ('vec3', a, b, c), d)),

   (('fdot3', ('vec3', a, 0.0, 0.0), b), ('fmul', a, b)),
   (('fdot3', ('vec3', a, b,   0.0), c), ('fdot2', ('vec2', a, b), c)),

   (('fdot2', ('vec2', a, 0.0), b), ('fmul', a, b)),
   (('fdot2', a, 1.0), ('fadd', 'a.x', 'a.y')),

   # If x >= 0 and x <= 1: fsat(1 - x) == 1 - fsat(x) trivially
   # If x < 0: 1 - fsat(x) => 1 - 0 => 1 and fsat(1 - x) => fsat(> 1) => 1
   # If x > 1: 1 - fsat(x) => 1 - 1 => 0 and fsat(1 - x) => fsat(< 0) => 0
   (('~fadd', ('fneg(is_used_once)', ('fsat(is_used_once)', 'a(is_not_fmul)')), 1.0), ('fsat', ('fadd', 1.0, ('fneg', a)))),

   # 1 - ((1 - a) * (1 - b))
   # 1 - (1 - a - b + a*b)
   # 1 - 1 + a + b - a*b
   # a + b - a*b
   # a + b*(1 - a)
   # b*(1 - a) + 1*a
   # flrp(b, 1, a)
   (('~fadd@32', 1.0, ('fneg', ('fmul', ('fadd', 1.0, ('fneg', a)), ('fadd', 1.0, ('fneg', b))))),
    ('flrp', b, 1.0, a), '!options->lower_flrp32'),

   # (a * #b + #c) << #d
   # ((a * #b) << #d) + (#c << #d)
   # (a * (#b << #d)) + (#c << #d)
   (('ishl', ('iadd', ('imul', a, '#b'), '#c'), '#d'),
    ('iadd', ('imul', a, ('ishl', b, d)), ('ishl', c, d))),

   # (a * #b) << #c
   # a * (#b << #c)
   (('ishl', ('imul', a, '#b'), '#c'), ('imul', a, ('ishl', b, c))),

   # Comparison simplifications
   (('~inot', ('flt', a, b)), ('fge', a, b)),
   (('~inot', ('fge', a, b)), ('flt', a, b)),
   (('inot', ('feq', a, b)), ('fne', a, b)),
   (('inot', ('fne', a, b)), ('feq', a, b)),
   (('inot', ('ilt', a, b)), ('ige', a, b)),
   (('inot', ('ult', a, b)), ('uge', a, b)),
   (('inot', ('ige', a, b)), ('ilt', a, b)),
   (('inot', ('uge', a, b)), ('ult', a, b)),
   (('inot', ('ieq', a, b)), ('ine', a, b)),
   (('inot', ('ine', a, b)), ('ieq', a, b)),

   (('iand', ('feq', a, b), ('fne', a, b)), False),
   (('iand', ('flt', a, b), ('flt', b, a)), False),
   (('iand', ('ieq', a, b), ('ine', a, b)), False),
   (('iand', ('ilt', a, b), ('ilt', b, a)), False),
   (('iand', ('ult', a, b), ('ult', b, a)), False),

   # This helps some shaders because, after some optimizations, they end up
   # with patterns like (-a < -b) || (b < a).  In an ideal world, this sort of
   # matching would be handled by CSE.
   (('flt', ('fneg', a), ('fneg', b)), ('flt', b, a)),
   (('fge', ('fneg', a), ('fneg', b)), ('fge', b, a)),
   (('feq', ('fneg', a), ('fneg', b)), ('feq', b, a)),
   (('fne', ('fneg', a), ('fneg', b)), ('fne', b, a)),
   (('flt', ('fneg', a), -1.0), ('flt', 1.0, a)),
   (('flt', -1.0, ('fneg', a)), ('flt', a, 1.0)),
   (('fge', ('fneg', a), -1.0), ('fge', 1.0, a)),
   (('fge', -1.0, ('fneg', a)), ('fge', a, 1.0)),
   (('fne', ('fneg', a), -1.0), ('fne', 1.0, a)),
   (('feq', -1.0, ('fneg', a)), ('feq', a, 1.0)),

   (('flt', ('fsat(is_used_once)', a), '#b(is_gt_0_and_lt_1)'), ('flt', a, b)),
   (('flt', '#b(is_gt_0_and_lt_1)', ('fsat(is_used_once)', a)), ('flt', b, a)),
   (('fge', ('fsat(is_used_once)', a), '#b(is_gt_0_and_lt_1)'), ('fge', a, b)),
   (('fge', '#b(is_gt_0_and_lt_1)', ('fsat(is_used_once)', a)), ('fge', b, a)),
   (('feq', ('fsat(is_used_once)', a), '#b(is_gt_0_and_lt_1)'), ('feq', a, b)),
   (('fne', ('fsat(is_used_once)', a), '#b(is_gt_0_and_lt_1)'), ('fne', a, b)),

   (('fge', ('fsat(is_used_once)', a), 1.0), ('fge', a, 1.0)),
   (('flt', ('fsat(is_used_once)', a), 1.0), ('flt', a, 1.0)),
   (('fge', 0.0, ('fsat(is_used_once)', a)), ('fge', 0.0, a)),
   (('flt', 0.0, ('fsat(is_used_once)', a)), ('flt', 0.0, a)),

   # 0.0 >= b2f(a)
   # b2f(a) <= 0.0
   # b2f(a) == 0.0 because b2f(a) can only be 0 or 1
   # inot(a)
   (('fge', 0.0, ('b2f', 'a@1')), ('inot', a)),

   (('fge', ('fneg', ('b2f', 'a@1')), 0.0), ('inot', a)),

   (('fne', ('fadd', ('b2f', 'a@1'), ('b2f', 'b@1')), 0.0), ('ior', a, b)),
   (('fne', ('fmax', ('b2f', 'a@1'), ('b2f', 'b@1')), 0.0), ('ior', a, b)),
   (('fne', ('bcsel', a, 1.0, ('b2f', 'b@1'))   , 0.0), ('ior', a, b)),
   (('fne', ('b2f', 'a@1'), ('fneg', ('b2f', 'b@1'))),      ('ior', a, b)),
   (('fne', ('fmul', ('b2f', 'a@1'), ('b2f', 'b@1')), 0.0), ('iand', a, b)),
   (('fne', ('fmin', ('b2f', 'a@1'), ('b2f', 'b@1')), 0.0), ('iand', a, b)),
   (('fne', ('bcsel', a, ('b2f', 'b@1'), 0.0)   , 0.0), ('iand', a, b)),
   (('fne', ('fadd', ('b2f', 'a@1'), ('fneg', ('b2f', 'b@1'))), 0.0), ('ixor', a, b)),
   (('fne',          ('b2f', 'a@1') ,          ('b2f', 'b@1') ),      ('ixor', a, b)),
   (('fne', ('fneg', ('b2f', 'a@1')), ('fneg', ('b2f', 'b@1'))),      ('ixor', a, b)),
   (('feq', ('fadd', ('b2f', 'a@1'), ('b2f', 'b@1')), 0.0), ('inot', ('ior', a, b))),
   (('feq', ('fmax', ('b2f', 'a@1'), ('b2f', 'b@1')), 0.0), ('inot', ('ior', a, b))),
   (('feq', ('bcsel', a, 1.0, ('b2f', 'b@1'))   , 0.0), ('inot', ('ior', a, b))),
   (('feq', ('b2f', 'a@1'), ('fneg', ('b2f', 'b@1'))),      ('inot', ('ior', a, b))),
   (('feq', ('fmul', ('b2f', 'a@1'), ('b2f', 'b@1')), 0.0), ('inot', ('iand', a, b))),
   (('feq', ('fmin', ('b2f', 'a@1'), ('b2f', 'b@1')), 0.0), ('inot', ('iand', a, b))),
   (('feq', ('bcsel', a, ('b2f', 'b@1'), 0.0)   , 0.0), ('inot', ('iand', a, b))),
   (('feq', ('fadd', ('b2f', 'a@1'), ('fneg', ('b2f', 'b@1'))), 0.0), ('ieq', a, b)),
   (('feq',          ('b2f', 'a@1') ,          ('b2f', 'b@1') ),      ('ieq', a, b)),
   (('feq', ('fneg', ('b2f', 'a@1')), ('fneg', ('b2f', 'b@1'))),      ('ieq', a, b)),

   # -(b2f(a) + b2f(b)) < 0
   # 0 < b2f(a) + b2f(b)
   # 0 != b2f(a) + b2f(b)       b2f must be 0 or 1, so the sum is non-negative
   # a || b
   (('flt', ('fneg', ('fadd', ('b2f', 'a@1'), ('b2f', 'b@1'))), 0.0), ('ior', a, b)),
   (('flt', 0.0, ('fadd', ('b2f', 'a@1'), ('b2f', 'b@1'))), ('ior', a, b)),

   # -(b2f(a) + b2f(b)) >= 0
   # 0 >= b2f(a) + b2f(b)
   # 0 == b2f(a) + b2f(b)       b2f must be 0 or 1, so the sum is non-negative
   # !(a || b)
   (('fge', ('fneg', ('fadd', ('b2f', 'a@1'), ('b2f', 'b@1'))), 0.0), ('inot', ('ior', a, b))),
   (('fge', 0.0, ('fadd', ('b2f', 'a@1'), ('b2f', 'b@1'))), ('inot', ('ior', a, b))),

   (('flt', a, ('fneg', a)), ('flt', a, 0.0)),
   (('fge', a, ('fneg', a)), ('fge', a, 0.0)),

   # Some optimizations (below) convert things like (a < b || c < b) into
   # (min(a, c) < b).  However, this interfers with the previous optimizations
   # that try to remove comparisons with negated sums of b2f.  This just
   # breaks that apart.
   (('flt', ('fmin', c, ('fneg', ('fadd', ('b2f', 'a@1'), ('b2f', 'b@1')))), 0.0),
    ('ior', ('flt', c, 0.0), ('ior', a, b))),

   (('~flt', ('fadd', a, b), a), ('flt', b, 0.0)),
   (('~fge', ('fadd', a, b), a), ('fge', b, 0.0)),
   (('~feq', ('fadd', a, b), a), ('feq', b, 0.0)),
   (('~fne', ('fadd', a, b), a), ('fne', b, 0.0)),

   # Cannot remove the addition from ilt or ige due to overflow.
   (('ieq', ('iadd', a, b), a), ('ieq', b, 0)),
   (('ine', ('iadd', a, b), a), ('ine', b, 0)),

   # fmin(-b2f(a), b) >= 0.0
   # -b2f(a) >= 0.0 && b >= 0.0
   # -b2f(a) == 0.0 && b >= 0.0    -b2f can only be 0 or -1, never >0
   # b2f(a) == 0.0 && b >= 0.0
   # a == False && b >= 0.0
   # !a && b >= 0.0
   #
   # The fge in the second replacement is not a typo.  I leave the proof that
   # "fmin(-b2f(a), b) >= 0 <=> fmin(-b2f(a), b) == 0" as an exercise for the
   # reader.
   (('fge', ('fmin', ('fneg', ('b2f', 'a@1')), 'b@1'), 0.0), ('iand', ('inot', a), ('fge', b, 0.0))),
   (('feq', ('fmin', ('fneg', ('b2f', 'a@1')), 'b@1'), 0.0), ('iand', ('inot', a), ('fge', b, 0.0))),

   (('feq', ('b2f', 'a@1'), 0.0), ('inot', a)),
   (('fne', ('b2f', 'a@1'), 0.0), a),
   (('ieq', ('b2i', 'a@1'), 0),   ('inot', a)),
   (('ine', ('b2i', 'a@1'), 0),   a),

   (('fne', ('u2f', a), 0.0), ('ine', a, 0)),
   (('feq', ('u2f', a), 0.0), ('ieq', a, 0)),
   (('fge', ('u2f', a), 0.0), True),
   (('fge', 0.0, ('u2f', a)), ('uge', 0, a)),    # ieq instead?
   (('flt', ('u2f', a), 0.0), False),
   (('flt', 0.0, ('u2f', a)), ('ult', 0, a)),    # ine instead?
   (('fne', ('i2f', a), 0.0), ('ine', a, 0)),
   (('feq', ('i2f', a), 0.0), ('ieq', a, 0)),
   (('fge', ('i2f', a), 0.0), ('ige', a, 0)),
   (('fge', 0.0, ('i2f', a)), ('ige', 0, a)),
   (('flt', ('i2f', a), 0.0), ('ilt', a, 0)),
   (('flt', 0.0, ('i2f', a)), ('ilt', 0, a)),

   # 0.0 < fabs(a)
   # fabs(a) > 0.0
   # fabs(a) != 0.0 because fabs(a) must be >= 0
   # a != 0.0
   (('~flt', 0.0, ('fabs', a)), ('fne', a, 0.0)),

   # -fabs(a) < 0.0
   # fabs(a) > 0.0
   (('~flt', ('fneg', ('fabs', a)), 0.0), ('fne', a, 0.0)),

   # 0.0 >= fabs(a)
   # 0.0 == fabs(a)   because fabs(a) must be >= 0
   # 0.0 == a
   (('fge', 0.0, ('fabs', a)), ('feq', a, 0.0)),

   # -fabs(a) >= 0.0
   # 0.0 >= fabs(a)
   (('fge', ('fneg', ('fabs', a)), 0.0), ('feq', a, 0.0)),

   (('fmax',                        ('b2f(is_used_once)', 'a@1'),           ('b2f', 'b@1')),           ('b2f', ('ior', a, b))),
   (('fmax', ('fneg(is_used_once)', ('b2f(is_used_once)', 'a@1')), ('fneg', ('b2f', 'b@1'))), ('fneg', ('b2f', ('ior', a, b)))),
   (('fmin',                        ('b2f(is_used_once)', 'a@1'),           ('b2f', 'b@1')),           ('b2f', ('iand', a, b))),
   (('fmin', ('fneg(is_used_once)', ('b2f(is_used_once)', 'a@1')), ('fneg', ('b2f', 'b@1'))), ('fneg', ('b2f', ('iand', a, b)))),

   # fmin(b2f(a), b)
   # bcsel(a, fmin(b2f(a), b), fmin(b2f(a), b))
   # bcsel(a, fmin(b2f(True), b), fmin(b2f(False), b))
   # bcsel(a, fmin(1.0, b), fmin(0.0, b))
   #
   # Since b is a constant, constant folding will eliminate the fmin and the
   # fmax.  If b is > 1.0, the bcsel will be replaced with a b2f.
   (('fmin', ('b2f', 'a@1'), '#b'), ('bcsel', a, ('fmin', b, 1.0), ('fmin', b, 0.0))),

   (('flt', ('fadd(is_used_once)', a, ('fneg', b)), 0.0), ('flt', a, b)),

   (('fge', ('fneg', ('fabs', a)), 0.0), ('feq', a, 0.0)),
   (('~bcsel', ('flt', b, a), b, a), ('fmin', a, b)),
   (('~bcsel', ('flt', a, b), b, a), ('fmax', a, b)),
   (('~bcsel', ('fge', a, b), b, a), ('fmin', a, b)),
   (('~bcsel', ('fge', b, a), b, a), ('fmax', a, b)),
   (('bcsel', ('i2b', a), b, c), ('bcsel', ('ine', a, 0), b, c)),
   (('bcsel', ('inot', a), b, c), ('bcsel', a, c, b)),
   (('bcsel', a, ('bcsel', a, b, c), d), ('bcsel', a, b, d)),
   (('bcsel', a, b, ('bcsel', a, c, d)), ('bcsel', a, b, d)),
   (('bcsel', a, ('bcsel', b, c, d), ('bcsel(is_used_once)', b, c, 'e')), ('bcsel', b, c, ('bcsel', a, d, 'e'))),
   (('bcsel', a, ('bcsel(is_used_once)', b, c, d), ('bcsel', b, c, 'e')), ('bcsel', b, c, ('bcsel', a, d, 'e'))),
   (('bcsel', a, ('bcsel', b, c, d), ('bcsel(is_used_once)', b, 'e', d)), ('bcsel', b, ('bcsel', a, c, 'e'), d)),
   (('bcsel', a, ('bcsel(is_used_once)', b, c, d), ('bcsel', b, 'e', d)), ('bcsel', b, ('bcsel', a, c, 'e'), d)),
   (('bcsel', a, True, b), ('ior', a, b)),
   (('bcsel', a, a, b), ('ior', a, b)),
   (('bcsel', a, b, False), ('iand', a, b)),
   (('bcsel', a, b, a), ('iand', a, b)),
   (('fmin', a, a), a),
   (('fmax', a, a), a),
   (('imin', a, a), a),
   (('imax', a, a), a),
   (('umin', a, a), a),
   (('umax', a, a), a),
   (('fmax', ('fmax', a, b), b), ('fmax', a, b)),
   (('umax', ('umax', a, b), b), ('umax', a, b)),
   (('imax', ('imax', a, b), b), ('imax', a, b)),
   (('fmin', ('fmin', a, b), b), ('fmin', a, b)),
   (('umin', ('umin', a, b), b), ('umin', a, b)),
   (('imin', ('imin', a, b), b), ('imin', a, b)),
   (('fmax', a, ('fneg', a)), ('fabs', a)),
   (('imax', a, ('ineg', a)), ('iabs', a)),
   (('fmin', a, ('fneg', a)), ('fneg', ('fabs', a))),
   (('imin', a, ('ineg', a)), ('ineg', ('iabs', a))),
   (('fmin', a, ('fneg', ('fabs', a))), ('fneg', ('fabs', a))),
   (('imin', a, ('ineg', ('iabs', a))), ('ineg', ('iabs', a))),
   (('fmin', a, ('fabs', a)), a),
   (('imin', a, ('iabs', a)), a),
   (('fmax', a, ('fneg', ('fabs', a))), a),
   (('imax', a, ('ineg', ('iabs', a))), a),
   (('fmax', a, ('fabs', a)), ('fabs', a)),
   (('imax', a, ('iabs', a)), ('iabs', a)),
   (('fmax', a, ('fneg', a)), ('fabs', a)),
   (('imax', a, ('ineg', a)), ('iabs', a)),
   (('~fmax', ('fabs', a), 0.0), ('fabs', a)),
   (('~fmin', ('fmax', a, 0.0), 1.0), ('fsat', a), '!options->lower_fsat'),
   (('~fmax', ('fmin', a, 1.0), 0.0), ('fsat', a), '!options->lower_fsat'),
   (('~fmin', ('fmax', a, -1.0),  0.0), ('fneg', ('fsat', ('fneg', a))), '!options->lower_negate && !options->lower_fsat'),
   (('~fmax', ('fmin', a,  0.0), -1.0), ('fneg', ('fsat', ('fneg', a))), '!options->lower_negate && !options->lower_fsat'),
   (('fsat', ('fsign', a)), ('b2f', ('flt', 0.0, a))),
   (('fsat', ('b2f', a)), ('b2f', a)),
   (('fsat', a), ('fmin', ('fmax', a, 0.0), 1.0), 'options->lower_fsat'),
   (('fsat', ('fsat', a)), ('fsat', a)),
   (('fsat', ('fneg(is_used_once)', ('fadd(is_used_once)', a, b))), ('fsat', ('fadd', ('fneg', a), ('fneg', b))), '!options->lower_negate && !options->lower_fsat'),
   (('fsat', ('fneg(is_used_once)', ('fmul(is_used_once)', a, b))), ('fsat', ('fmul', ('fneg', a), b)), '!options->lower_negate && !options->lower_fsat'),
   (('fsat', ('fabs(is_used_once)', ('fmul(is_used_once)', a, b))), ('fsat', ('fmul', ('fabs', a), ('fabs', b))), '!options->lower_fsat'),
   (('fmin', ('fmax', ('fmin', ('fmax', a, b), c), b), c), ('fmin', ('fmax', a, b), c)),
   (('imin', ('imax', ('imin', ('imax', a, b), c), b), c), ('imin', ('imax', a, b), c)),
   (('umin', ('umax', ('umin', ('umax', a, b), c), b), c), ('umin', ('umax', a, b), c)),
   (('fmax', ('fsat', a), '#b@32(is_zero_to_one)'), ('fsat', ('fmax', a, b))),
   (('fmin', ('fsat', a), '#b@32(is_zero_to_one)'), ('fsat', ('fmin', a, b))),
   (('extract_u8', ('imin', ('imax', a, 0), 0xff), 0), ('imin', ('imax', a, 0), 0xff)),
   (('~ior', ('flt(is_used_once)', a, b), ('flt', a, c)), ('flt', a, ('fmax', b, c))),
   (('~ior', ('flt(is_used_once)', a, c), ('flt', b, c)), ('flt', ('fmin', a, b), c)),
   (('~ior', ('fge(is_used_once)', a, b), ('fge', a, c)), ('fge', a, ('fmin', b, c))),
   (('~ior', ('fge(is_used_once)', a, c), ('fge', b, c)), ('fge', ('fmax', a, b), c)),
   (('~ior', ('flt', a, '#b'), ('flt', a, '#c')), ('flt', a, ('fmax', b, c))),
   (('~ior', ('flt', '#a', c), ('flt', '#b', c)), ('flt', ('fmin', a, b), c)),
   (('~ior', ('fge', a, '#b'), ('fge', a, '#c')), ('fge', a, ('fmin', b, c))),
   (('~ior', ('fge', '#a', c), ('fge', '#b', c)), ('fge', ('fmax', a, b), c)),
   (('~iand', ('flt(is_used_once)', a, b), ('flt', a, c)), ('flt', a, ('fmin', b, c))),
   (('~iand', ('flt(is_used_once)', a, c), ('flt', b, c)), ('flt', ('fmax', a, b), c)),
   (('~iand', ('fge(is_used_once)', a, b), ('fge', a, c)), ('fge', a, ('fmax', b, c))),
   (('~iand', ('fge(is_used_once)', a, c), ('fge', b, c)), ('fge', ('fmin', a, b), c)),
   (('~iand', ('flt', a, '#b'), ('flt', a, '#c')), ('flt', a, ('fmin', b, c))),
   (('~iand', ('flt', '#a', c), ('flt', '#b', c)), ('flt', ('fmax', a, b), c)),
   (('~iand', ('fge', a, '#b'), ('fge', a, '#c')), ('fge', a, ('fmax', b, c))),
   (('~iand', ('fge', '#a', c), ('fge', '#b', c)), ('fge', ('fmin', a, b), c)),

   (('ior', ('ilt(is_used_once)', a, b), ('ilt', a, c)), ('ilt', a, ('imax', b, c))),
   (('ior', ('ilt(is_used_once)', a, c), ('ilt', b, c)), ('ilt', ('imin', a, b), c)),
   (('ior', ('ige(is_used_once)', a, b), ('ige', a, c)), ('ige', a, ('imin', b, c))),
   (('ior', ('ige(is_used_once)', a, c), ('ige', b, c)), ('ige', ('imax', a, b), c)),
   (('ior', ('ult(is_used_once)', a, b), ('ult', a, c)), ('ult', a, ('umax', b, c))),
   (('ior', ('ult(is_used_once)', a, c), ('ult', b, c)), ('ult', ('umin', a, b), c)),
   (('ior', ('uge(is_used_once)', a, b), ('uge', a, c)), ('uge', a, ('umin', b, c))),
   (('ior', ('uge(is_used_once)', a, c), ('uge', b, c)), ('uge', ('umax', a, b), c)),
   (('iand', ('ilt(is_used_once)', a, b), ('ilt', a, c)), ('ilt', a, ('imin', b, c))),
   (('iand', ('ilt(is_used_once)', a, c), ('ilt', b, c)), ('ilt', ('imax', a, b), c)),
   (('iand', ('ige(is_used_once)', a, b), ('ige', a, c)), ('ige', a, ('imax', b, c))),
   (('iand', ('ige(is_used_once)', a, c), ('ige', b, c)), ('ige', ('imin', a, b), c)),
   (('iand', ('ult(is_used_once)', a, b), ('ult', a, c)), ('ult', a, ('umin', b, c))),
   (('iand', ('ult(is_used_once)', a, c), ('ult', b, c)), ('ult', ('umax', a, b), c)),
   (('iand', ('uge(is_used_once)', a, b), ('uge', a, c)), ('uge', a, ('umax', b, c))),
   (('iand', ('uge(is_used_once)', a, c), ('uge', b, c)), ('uge', ('umin', a, b), c)),

   # Common pattern like 'if (i == 0 || i == 1 || ...)'
   (('ior', ('ieq', a, 0), ('ieq', a, 1)), ('uge', 1, a)),
   (('ior', ('uge', 1, a), ('ieq', a, 2)), ('uge', 2, a)),
   (('ior', ('uge', 2, a), ('ieq', a, 3)), ('uge', 3, a)),

   # The (i2f32, ...) part is an open-coded fsign.  When that is combined with
   # the bcsel, it's basically copysign(1.0, a).  There is no copysign in NIR,
   # so emit an open-coded version of that.
   (('bcsel@32', ('feq', a, 0.0), 1.0, ('i2f32', ('iadd', ('b2i32', ('flt', 0.0, 'a@32')), ('ineg', ('b2i32', ('flt', 'a@32', 0.0)))))),
    ('ior', 0x3f800000, ('iand', a, 0x80000000))),

   (('ior', a, ('ieq', a, False)), True),
   (('ior', a, ('inot', a)), -1),

   (('ine', ('ineg', ('b2i32', 'a@1')), ('ineg', ('b2i32', 'b@1'))), ('ine', a, b)),
   (('b2i32', ('ine', 'a@1', 'b@1')), ('b2i32', ('ixor', a, b))),

   (('iand', ('ieq', 'a@32', 0), ('ieq', 'b@32', 0)), ('ieq', ('ior', 'a@32', 'b@32'), 0)),

   # These patterns can result when (a < b || a < c) => (a < min(b, c))
   # transformations occur before constant propagation and loop-unrolling.
   (('~flt', a, ('fmax', b, a)), ('flt', a, b)),
   (('~flt', ('fmin', a, b), a), ('flt', b, a)),
   (('~fge', a, ('fmin', b, a)), True),
   (('~fge', ('fmax', a, b), a), True),
   (('~flt', a, ('fmin', b, a)), False),
   (('~flt', ('fmax', a, b), a), False),
   (('~fge', a, ('fmax', b, a)), ('fge', a, b)),
   (('~fge', ('fmin', a, b), a), ('fge', b, a)),

   (('ilt', a, ('imax', b, a)), ('ilt', a, b)),
   (('ilt', ('imin', a, b), a), ('ilt', b, a)),
   (('ige', a, ('imin', b, a)), True),
   (('ige', ('imax', a, b), a), True),
   (('ult', a, ('umax', b, a)), ('ult', a, b)),
   (('ult', ('umin', a, b), a), ('ult', b, a)),
   (('uge', a, ('umin', b, a)), True),
   (('uge', ('umax', a, b), a), True),
   (('ilt', a, ('imin', b, a)), False),
   (('ilt', ('imax', a, b), a), False),
   (('ige', a, ('imax', b, a)), ('ige', a, b)),
   (('ige', ('imin', a, b), a), ('ige', b, a)),
   (('ult', a, ('umin', b, a)), False),
   (('ult', ('umax', a, b), a), False),
   (('uge', a, ('umax', b, a)), ('uge', a, b)),
   (('uge', ('umin', a, b), a), ('uge', b, a)),
   (('ult', a, ('iand', b, a)), False),
   (('ult', ('ior', a, b), a), False),
   (('uge', a, ('iand', b, a)), True),
   (('uge', ('ior', a, b), a), True),

   (('ilt', '#a', ('imax', '#b', c)), ('ior', ('ilt', a, b), ('ilt', a, c))),
   (('ilt', ('imin', '#a', b), '#c'), ('ior', ('ilt', a, c), ('ilt', b, c))),
   (('ige', '#a', ('imin', '#b', c)), ('ior', ('ige', a, b), ('ige', a, c))),
   (('ige', ('imax', '#a', b), '#c'), ('ior', ('ige', a, c), ('ige', b, c))),
   (('ult', '#a', ('umax', '#b', c)), ('ior', ('ult', a, b), ('ult', a, c))),
   (('ult', ('umin', '#a', b), '#c'), ('ior', ('ult', a, c), ('ult', b, c))),
   (('uge', '#a', ('umin', '#b', c)), ('ior', ('uge', a, b), ('uge', a, c))),
   (('uge', ('umax', '#a', b), '#c'), ('ior', ('uge', a, c), ('uge', b, c))),
   (('ilt', '#a', ('imin', '#b', c)), ('iand', ('ilt', a, b), ('ilt', a, c))),
   (('ilt', ('imax', '#a', b), '#c'), ('iand', ('ilt', a, c), ('ilt', b, c))),
   (('ige', '#a', ('imax', '#b', c)), ('iand', ('ige', a, b), ('ige', a, c))),
   (('ige', ('imin', '#a', b), '#c'), ('iand', ('ige', a, c), ('ige', b, c))),
   (('ult', '#a', ('umin', '#b', c)), ('iand', ('ult', a, b), ('ult', a, c))),
   (('ult', ('umax', '#a', b), '#c'), ('iand', ('ult', a, c), ('ult', b, c))),
   (('uge', '#a', ('umax', '#b', c)), ('iand', ('uge', a, b), ('uge', a, c))),
   (('uge', ('umin', '#a', b), '#c'), ('iand', ('uge', a, c), ('uge', b, c))),

   # Thanks to sign extension, the ishr(a, b) is negative if and only if a is
   # negative.
   (('bcsel', ('ilt', a, 0), ('ineg', ('ishr', a, b)), ('ishr', a, b)),
    ('iabs', ('ishr', a, b))),
   (('iabs', ('ishr', ('iabs', a), b)), ('ishr', ('iabs', a), b)),

   (('fabs', ('slt', a, b)), ('slt', a, b)),
   (('fabs', ('sge', a, b)), ('sge', a, b)),
   (('fabs', ('seq', a, b)), ('seq', a, b)),
   (('fabs', ('sne', a, b)), ('sne', a, b)),
   (('slt', a, b), ('b2f', ('flt', a, b)), 'options->lower_scmp'),
   (('sge', a, b), ('b2f', ('fge', a, b)), 'options->lower_scmp'),
   (('seq', a, b), ('b2f', ('feq', a, b)), 'options->lower_scmp'),
   (('sne', a, b), ('b2f', ('fne', a, b)), 'options->lower_scmp'),
   (('seq', ('seq', a, b), 1.0), ('seq', a, b)),
   (('seq', ('sne', a, b), 1.0), ('sne', a, b)),
   (('seq', ('slt', a, b), 1.0), ('slt', a, b)),
   (('seq', ('sge', a, b), 1.0), ('sge', a, b)),
   (('sne', ('seq', a, b), 0.0), ('seq', a, b)),
   (('sne', ('sne', a, b), 0.0), ('sne', a, b)),
   (('sne', ('slt', a, b), 0.0), ('slt', a, b)),
   (('sne', ('sge', a, b), 0.0), ('sge', a, b)),
   (('seq', ('seq', a, b), 0.0), ('sne', a, b)),
   (('seq', ('sne', a, b), 0.0), ('seq', a, b)),
   (('seq', ('slt', a, b), 0.0), ('sge', a, b)),
   (('seq', ('sge', a, b), 0.0), ('slt', a, b)),
   (('sne', ('seq', a, b), 1.0), ('sne', a, b)),
   (('sne', ('sne', a, b), 1.0), ('seq', a, b)),
   (('sne', ('slt', a, b), 1.0), ('sge', a, b)),
   (('sne', ('sge', a, b), 1.0), ('slt', a, b)),
   (('fall_equal2', a, b), ('fmin', ('seq', 'a.x', 'b.x'), ('seq', 'a.y', 'b.y')), 'options->lower_vector_cmp'),
   (('fall_equal3', a, b), ('seq', ('fany_nequal3', a, b), 0.0), 'options->lower_vector_cmp'),
   (('fall_equal4', a, b), ('seq', ('fany_nequal4', a, b), 0.0), 'options->lower_vector_cmp'),
   (('fany_nequal2', a, b), ('fmax', ('sne', 'a.x', 'b.x'), ('sne', 'a.y', 'b.y')), 'options->lower_vector_cmp'),
   (('fany_nequal3', a, b), ('fsat', ('fdot3', ('sne', a, b), ('sne', a, b))), 'options->lower_vector_cmp'),
   (('fany_nequal4', a, b), ('fsat', ('fdot4', ('sne', a, b), ('sne', a, b))), 'options->lower_vector_cmp'),
   (('fne', ('fneg', a), a), ('fne', a, 0.0)),
   (('feq', ('fneg', a), a), ('feq', a, 0.0)),
   # Emulating booleans
   (('imul', ('b2i', 'a@1'), ('b2i', 'b@1')), ('b2i', ('iand', a, b))),
   (('fmul', ('b2f', 'a@1'), ('b2f', 'b@1')), ('b2f', ('iand', a, b))),
   (('fsat', ('fadd', ('b2f', 'a@1'), ('b2f', 'b@1'))), ('b2f', ('ior', a, b))),
   (('iand', 'a@bool32', 1.0), ('b2f', a)),
   # True/False are ~0 and 0 in NIR.  b2i of True is 1, and -1 is ~0 (True).
   (('ineg', ('b2i32', 'a@32')), a),
   (('flt', ('fneg', ('b2f', 'a@1')), 0), a), # Generated by TGSI KILL_IF.
   (('flt', ('fsub', 0.0, ('b2f', 'a@1')), 0), a), # Generated by TGSI KILL_IF.
   # Comparison with the same args.  Note that these are not done for
   # the float versions because NaN always returns false on float
   # inequalities.
   (('ilt', a, a), False),
   (('ige', a, a), True),
   (('ieq', a, a), True),
   (('ine', a, a), False),
   (('ult', a, a), False),
   (('uge', a, a), True),
   # Logical and bit operations
   (('iand', a, a), a),
   (('iand', a, ~0), a),
   (('iand', a, 0), 0),
   (('ior', a, a), a),
   (('ior', a, 0), a),
   (('ior', a, True), True),
   (('ixor', a, a), 0),
   (('ixor', a, 0), a),
   (('inot', ('inot', a)), a),
   (('ior', ('iand', a, b), b), b),
   (('ior', ('ior', a, b), b), ('ior', a, b)),
   (('iand', ('ior', a, b), b), b),
   (('iand', ('iand', a, b), b), ('iand', a, b)),
   # DeMorgan's Laws
   (('iand', ('inot', a), ('inot', b)), ('inot', ('ior',  a, b))),
   (('ior',  ('inot', a), ('inot', b)), ('inot', ('iand', a, b))),
   # Shift optimizations
   (('ishl', 0, a), 0),
   (('ishl', a, 0), a),
   (('ishr', 0, a), 0),
   (('ishr', a, 0), a),
   (('ushr', 0, a), 0),
   (('ushr', a, 0), a),
   (('iand', 0xff, ('ushr@32', a, 24)), ('ushr', a, 24)),
   (('iand', 0xffff, ('ushr@32', a, 16)), ('ushr', a, 16)),
   (('ior', ('ishl@16', a, b), ('ushr@16', a, ('iadd', 16, ('ineg', b)))), ('urol', a, b), '!options->lower_rotate'),
   (('ior', ('ishl@16', a, b), ('ushr@16', a, ('isub', 16, b))), ('urol', a, b), '!options->lower_rotate'),
   (('ior', ('ishl@32', a, b), ('ushr@32', a, ('iadd', 32, ('ineg', b)))), ('urol', a, b), '!options->lower_rotate'),
   (('ior', ('ishl@32', a, b), ('ushr@32', a, ('isub', 32, b))), ('urol', a, b), '!options->lower_rotate'),
   (('ior', ('ushr@16', a, b), ('ishl@16', a, ('iadd', 16, ('ineg', b)))), ('uror', a, b), '!options->lower_rotate'),
   (('ior', ('ushr@16', a, b), ('ishl@16', a, ('isub', 16, b))), ('uror', a, b), '!options->lower_rotate'),
   (('ior', ('ushr@32', a, b), ('ishl@32', a, ('iadd', 32, ('ineg', b)))), ('uror', a, b), '!options->lower_rotate'),
   (('ior', ('ushr@32', a, b), ('ishl@32', a, ('isub', 32, b))), ('uror', a, b), '!options->lower_rotate'),
   (('urol@16', a, b), ('ior', ('ishl', a, b), ('ushr', a, ('isub', 16, b))), 'options->lower_rotate'),
   (('urol@32', a, b), ('ior', ('ishl', a, b), ('ushr', a, ('isub', 32, b))), 'options->lower_rotate'),
   (('uror@16', a, b), ('ior', ('ushr', a, b), ('ishl', a, ('isub', 16, b))), 'options->lower_rotate'),
   (('uror@32', a, b), ('ior', ('ushr', a, b), ('ishl', a, ('isub', 32, b))), 'options->lower_rotate'),
   # Exponential/logarithmic identities
   (('~fexp2', ('flog2', a)), a), # 2^lg2(a) = a
   (('~flog2', ('fexp2', a)), a), # lg2(2^a) = a
   (('fpow', a, b), ('fexp2', ('fmul', ('flog2', a), b)), 'options->lower_fpow'), # a^b = 2^(lg2(a)*b)
   (('~fexp2', ('fmul', ('flog2', a), b)), ('fpow', a, b), '!options->lower_fpow'), # 2^(lg2(a)*b) = a^b
   (('~fexp2', ('fadd', ('fmul', ('flog2', a), b), ('fmul', ('flog2', c), d))),
    ('~fmul', ('fpow', a, b), ('fpow', c, d)), '!options->lower_fpow'), # 2^(lg2(a) * b + lg2(c) + d) = a^b * c^d
   (('~fexp2', ('fmul', ('flog2', a), 2.0)), ('fmul', a, a)),
   (('~fexp2', ('fmul', ('flog2', a), 4.0)), ('fmul', ('fmul', a, a), ('fmul', a, a))),
   (('~fpow', a, 1.0), a),
   (('~fpow', a, 2.0), ('fmul', a, a)),
   (('~fpow', a, 4.0), ('fmul', ('fmul', a, a), ('fmul', a, a))),
   (('~fpow', 2.0, a), ('fexp2', a)),
   (('~fpow', ('fpow', a, 2.2), 0.454545), a),
   (('~fpow', ('fabs', ('fpow', a, 2.2)), 0.454545), ('fabs', a)),
   (('~fsqrt', ('fexp2', a)), ('fexp2', ('fmul', 0.5, a))),
   (('~frcp', ('fexp2', a)), ('fexp2', ('fneg', a))),
   (('~frsq', ('fexp2', a)), ('fexp2', ('fmul', -0.5, a))),
   (('~flog2', ('fsqrt', a)), ('fmul', 0.5, ('flog2', a))),
   (('~flog2', ('frcp', a)), ('fneg', ('flog2', a))),
   (('~flog2', ('frsq', a)), ('fmul', -0.5, ('flog2', a))),
   (('~flog2', ('fpow', a, b)), ('fmul', b, ('flog2', a))),
   (('~fmul', ('fexp2(is_used_once)', a), ('fexp2(is_used_once)', b)), ('fexp2', ('fadd', a, b))),
   (('bcsel', ('flt', a, 0.0), 0.0, ('fsqrt', a)), ('fsqrt', ('fmax', a, 0.0))),
   # Division and reciprocal
   (('~fdiv', 1.0, a), ('frcp', a)),
   (('fdiv', a, b), ('fmul', a, ('frcp', b)), 'options->lower_fdiv'),
   (('~frcp', ('frcp', a)), a),
   (('~frcp', ('fsqrt', a)), ('frsq', a)),
   (('fsqrt', a), ('frcp', ('frsq', a)), 'options->lower_fsqrt'),
   (('~frcp', ('frsq', a)), ('fsqrt', a), '!options->lower_fsqrt'),
   # Trig
   (('fsin', a), lowered_sincos(0.5), 'options->lower_sincos'),
   (('fcos', a), lowered_sincos(0.75), 'options->lower_sincos'),
   # Boolean simplifications
   (('i2b32(is_used_by_if)', a), ('ine32', a, 0)),
   (('i2b1(is_used_by_if)', a), ('ine', a, 0)),
   (('ieq', a, True), a),
   (('ine(is_not_used_by_if)', a, True), ('inot', a)),
   (('ine', a, False), a),
   (('ieq(is_not_used_by_if)', a, False), ('inot', 'a')),
   (('bcsel', a, True, False), a),
   (('bcsel', a, False, True), ('inot', a)),
   (('bcsel@32', a, 1.0, 0.0), ('b2f', a)),
   (('bcsel@32', a, 0.0, 1.0), ('b2f', ('inot', a))),
   (('bcsel@32', a, -1.0, -0.0), ('fneg', ('b2f', a))),
   (('bcsel@32', a, -0.0, -1.0), ('fneg', ('b2f', ('inot', a)))),
   (('bcsel', True, b, c), b),
   (('bcsel', False, b, c), c),
   (('bcsel', a, ('b2f(is_used_once)', 'b@32'), ('b2f', 'c@32')), ('b2f', ('bcsel', a, b, c))),

   (('bcsel', a, b, b), b),
   (('fcsel', a, b, b), b),

   # D3D Boolean emulation
   (('bcsel', a, -1, 0), ('ineg', ('b2i', 'a@1'))),
   (('bcsel', a, 0, -1), ('ineg', ('b2i', ('inot', a)))),
   (('iand', ('ineg', ('b2i', 'a@1')), ('ineg', ('b2i', 'b@1'))),
    ('ineg', ('b2i', ('iand', a, b)))),
   (('ior', ('ineg', ('b2i','a@1')), ('ineg', ('b2i', 'b@1'))),
    ('ineg', ('b2i', ('ior', a, b)))),
   (('ieq', ('ineg', ('b2i', 'a@1')), 0), ('inot', a)),
   (('ieq', ('ineg', ('b2i', 'a@1')), -1), a),
   (('ine', ('ineg', ('b2i', 'a@1')), 0), a),
   (('ine', ('ineg', ('b2i', 'a@1')), -1), ('inot', a)),
   (('iand', ('ineg', ('b2i', a)), 1.0), ('b2f', a)),

   # SM5 32-bit shifts are defined to use the 5 least significant bits
   (('ishl', 'a@32', ('iand', 31, b)), ('ishl', a, b)),
   (('ishr', 'a@32', ('iand', 31, b)), ('ishr', a, b)),
   (('ushr', 'a@32', ('iand', 31, b)), ('ushr', a, b)),

   # Conversions
   (('i2b32', ('b2i', 'a@32')), a),
   (('f2i', ('ftrunc', a)), ('f2i', a)),
   (('f2u', ('ftrunc', a)), ('f2u', a)),
   (('i2b', ('ineg', a)), ('i2b', a)),
   (('i2b', ('iabs', a)), ('i2b', a)),
   (('fabs', ('b2f', a)), ('b2f', a)),
   (('iabs', ('b2i', a)), ('b2i', a)),
   (('inot', ('f2b1', a)), ('feq', a, 0.0)),

   # Ironically, mark these as imprecise because removing the conversions may
   # preserve more precision than doing the conversions (e.g.,
   # uint(float(0x81818181u)) == 0x81818200).
   (('~f2i32', ('i2f', 'a@32')), a),
   (('~f2i32', ('u2f', 'a@32')), a),
   (('~f2u32', ('i2f', 'a@32')), a),
   (('~f2u32', ('u2f', 'a@32')), a),

   # Section 5.4.1 (Conversion and Scalar Constructors) of the GLSL 4.60 spec
   # says:
   #
   #    It is undefined to convert a negative floating-point value to an
   #    uint.
   #
   # Assuming that (uint)some_float behaves like (uint)(int)some_float allows
   # some optimizations in the i965 backend to proceed.
   (('ige', ('f2u', a), b), ('ige', ('f2i', a), b)),
   (('ige', b, ('f2u', a)), ('ige', b, ('f2i', a))),
   (('ilt', ('f2u', a), b), ('ilt', ('f2i', a), b)),
   (('ilt', b, ('f2u', a)), ('ilt', b, ('f2i', a))),

   (('~fmin', ('fabs', a), 1.0), ('fsat', ('fabs', a)), '!options->lower_fsat'),

   # The result of the multiply must be in [-1, 0], so the result of the ffma
   # must be in [0, 1].
   (('flt', ('fadd', ('fmul', ('fsat', a), ('fneg', ('fsat', a))), 1.0), 0.0), False),
   (('flt', ('fadd', ('fneg', ('fmul', ('fsat', a), ('fsat', a))), 1.0), 0.0), False),
   (('fmax', ('fadd', ('fmul', ('fsat', a), ('fneg', ('fsat', a))), 1.0), 0.0), ('fadd', ('fmul', ('fsat', a), ('fneg', ('fsat', a))), 1.0)),
   (('fmax', ('fadd', ('fneg', ('fmul', ('fsat', a), ('fsat', a))), 1.0), 0.0), ('fadd', ('fneg', ('fmul', ('fsat', a), ('fsat', a))), 1.0)),

   # Packing and then unpacking does nothing
   (('unpack_64_2x32_split_x', ('pack_64_2x32_split', a, b)), a),
   (('unpack_64_2x32_split_y', ('pack_64_2x32_split', a, b)), b),
   (('pack_64_2x32_split', ('unpack_64_2x32_split_x', a),
                           ('unpack_64_2x32_split_y', a)), a),

   # Comparing two halves of an unpack separately.  While this optimization
   # should be correct for non-constant values, it's less obvious that it's
   # useful in that case.  For constant values, the pack will fold and we're
   # guaranteed to reduce the whole tree to one instruction.
   (('iand', ('ieq', ('unpack_32_2x16_split_x', a), '#b'),
             ('ieq', ('unpack_32_2x16_split_y', a), '#c')),
    ('ieq', a, ('pack_32_2x16_split', b, c))),

   # Byte extraction
   (('ushr', 'a@16',  8), ('extract_u8', a, 1), '!options->lower_extract_byte'),
   (('ushr', 'a@32', 24), ('extract_u8', a, 3), '!options->lower_extract_byte'),
   (('ushr', 'a@64', 56), ('extract_u8', a, 7), '!options->lower_extract_byte'),
   (('ishr', 'a@16',  8), ('extract_i8', a, 1), '!options->lower_extract_byte'),
   (('ishr', 'a@32', 24), ('extract_i8', a, 3), '!options->lower_extract_byte'),
   (('ishr', 'a@64', 56), ('extract_i8', a, 7), '!options->lower_extract_byte'),
   (('iand', 0xff, a), ('extract_u8', a, 0), '!options->lower_extract_byte')
]

# After the ('extract_u8', a, 0) pattern, above, triggers, there will be
# patterns like those below.
for op in ('ushr', 'ishr'):
   optimizations.extend([(('extract_u8', (op, 'a@16',  8),     0), ('extract_u8', a, 1))])
   optimizations.extend([(('extract_u8', (op, 'a@32',  8 * i), 0), ('extract_u8', a, i)) for i in range(1, 4)])
   optimizations.extend([(('extract_u8', (op, 'a@64',  8 * i), 0), ('extract_u8', a, i)) for i in range(1, 8)])

optimizations.extend([(('extract_u8', ('extract_u16', a, 1), 0), ('extract_u8', a, 2))])

# After the ('extract_[iu]8', a, 3) patterns, above, trigger, there will be
# patterns like those below.
for op in ('extract_u8', 'extract_i8'):
   optimizations.extend([((op, ('ishl', 'a@16',      8),     1), (op, a, 0))])
   optimizations.extend([((op, ('ishl', 'a@32', 24 - 8 * i), 3), (op, a, i)) for i in range(2, -1, -1)])
   optimizations.extend([((op, ('ishl', 'a@64', 56 - 8 * i), 7), (op, a, i)) for i in range(6, -1, -1)])

optimizations.extend([
    # Word extraction
   (('ushr', ('ishl', 'a@32', 16), 16), ('extract_u16', a, 0), '!options->lower_extract_word'),
   (('ushr', 'a@32', 16), ('extract_u16', a, 1), '!options->lower_extract_word'),
   (('ishr', ('ishl', 'a@32', 16), 16), ('extract_i16', a, 0), '!options->lower_extract_word'),
   (('ishr', 'a@32', 16), ('extract_i16', a, 1), '!options->lower_extract_word'),
   (('iand', 0xffff, a), ('extract_u16', a, 0), '!options->lower_extract_word'),

   # Subtracts
   (('~fsub', a, ('fsub', 0.0, b)), ('fadd', a, b)),
   (('isub', a, ('isub', 0, b)), ('iadd', a, b)),
   (('ussub_4x8', a, 0), a),
   (('ussub_4x8', a, ~0), 0),
   (('fsub', a, b), ('fadd', a, ('fneg', b)), 'options->lower_sub'),
   (('isub', a, b), ('iadd', a, ('ineg', b)), 'options->lower_sub'),
   (('fneg', a), ('fsub', 0.0, a), 'options->lower_negate'),
   (('ineg', a), ('isub', 0, a), 'options->lower_negate'),
   (('~fadd', a, ('fsub', 0.0, b)), ('fsub', a, b)),
   (('iadd', a, ('isub', 0, b)), ('isub', a, b)),
   (('fabs', ('fsub', 0.0, a)), ('fabs', a)),
   (('iabs', ('isub', 0, a)), ('iabs', a)),

   # Propagate negation up multiplication chains
   (('fmul(is_used_by_non_fsat)', ('fneg', a), b), ('fneg', ('fmul', a, b))),
   (('imul', ('ineg', a), b), ('ineg', ('imul', a, b))),

   # Propagate constants up multiplication chains
   (('~fmul(is_used_once)', ('fmul(is_used_once)', 'a(is_not_const)', 'b(is_not_const)'), '#c'), ('fmul', ('fmul', a, c), b)),
   (('imul(is_used_once)', ('imul(is_used_once)', 'a(is_not_const)', 'b(is_not_const)'), '#c'), ('imul', ('imul', a, c), b)),
   (('~fadd(is_used_once)', ('fadd(is_used_once)', 'a(is_not_const)', 'b(is_not_const)'), '#c'), ('fadd', ('fadd', a, c), b)),
   (('iadd(is_used_once)', ('iadd(is_used_once)', 'a(is_not_const)', 'b(is_not_const)'), '#c'), ('iadd', ('iadd', a, c), b)),

   # Reassociate constants in add/mul chains so they can be folded together.
   # For now, we mostly only handle cases where the constants are separated by
   # a single non-constant.  We could do better eventually.
   (('~fmul', '#a', ('fmul', 'b(is_not_const)', '#c')), ('fmul', ('fmul', a, c), b)),
   (('imul', '#a', ('imul', 'b(is_not_const)', '#c')), ('imul', ('imul', a, c), b)),
   (('~fadd', '#a',          ('fadd', 'b(is_not_const)', '#c')),  ('fadd', ('fadd', a,          c),           b)),
   (('~fadd', '#a', ('fneg', ('fadd', 'b(is_not_const)', '#c'))), ('fadd', ('fadd', a, ('fneg', c)), ('fneg', b))),
   (('iadd', '#a', ('iadd', 'b(is_not_const)', '#c')), ('iadd', ('iadd', a, c), b)),

   # Drop mul-div by the same value when there's no wrapping.
   (('idiv', ('imul(no_signed_wrap)', a, b), b), a),

   # By definition...
   (('bcsel', ('ige', ('find_lsb', a), 0), ('find_lsb', a), -1), ('find_lsb', a)),
   (('bcsel', ('ige', ('ifind_msb', a), 0), ('ifind_msb', a), -1), ('ifind_msb', a)),
   (('bcsel', ('ige', ('ufind_msb', a), 0), ('ufind_msb', a), -1), ('ufind_msb', a)),

   (('bcsel', ('ine', a, 0), ('find_lsb', a), -1), ('find_lsb', a)),
   (('bcsel', ('ine', a, 0), ('ifind_msb', a), -1), ('ifind_msb', a)),
   (('bcsel', ('ine', a, 0), ('ufind_msb', a), -1), ('ufind_msb', a)),

   (('bcsel', ('ine', a, -1), ('ifind_msb', a), -1), ('ifind_msb', a)),

   # Misc. lowering
   (('fmod@16', a, b), ('fsub', a, ('fmul', b, ('ffloor', ('fdiv', a, b)))), 'options->lower_fmod'),
   (('fmod@32', a, b), ('fsub', a, ('fmul', b, ('ffloor', ('fdiv', a, b)))), 'options->lower_fmod'),
   (('frem', a, b), ('fsub', a, ('fmul', b, ('ftrunc', ('fdiv', a, b)))), 'options->lower_fmod'),
   (('uadd_carry@32', a, b), ('b2i', ('ult', ('iadd', a, b), a)), 'options->lower_uadd_carry'),
   (('usub_borrow@32', a, b), ('b2i', ('ult', a, b)), 'options->lower_usub_borrow'),

   (('bitfield_insert', 'base', 'insert', 'offset', 'bits'),
    ('bcsel', ('ult', 31, 'bits'), 'insert',
              ('bfi', ('bfm', 'bits', 'offset'), 'insert', 'base')),
    'options->lower_bitfield_insert'),
   (('ihadd', a, b), ('iadd', ('iand', a, b), ('ishr', ('ixor', a, b), 1)), 'options->lower_hadd'),
   (('uhadd', a, b), ('iadd', ('iand', a, b), ('ushr', ('ixor', a, b), 1)), 'options->lower_hadd'),
   (('irhadd', a, b), ('isub', ('ior', a, b), ('ishr', ('ixor', a, b), 1)), 'options->lower_hadd'),
   (('urhadd', a, b), ('isub', ('ior', a, b), ('ushr', ('ixor', a, b), 1)), 'options->lower_hadd'),
   (('uadd_sat', a, b), ('bcsel', ('ult', ('iadd', a, b), a), -1, ('iadd', a, b)), 'options->lower_add_sat'),
   (('usub_sat', a, b), ('bcsel', ('ult', a, b), 0, ('isub', a, b)), 'options->lower_add_sat'),

   # Alternative lowering that doesn't rely on bfi.
   (('bitfield_insert', 'base', 'insert', 'offset', 'bits'),
    ('bcsel', ('ult', 31, 'bits'),
     'insert',
    (('ior',
     ('iand', 'base', ('inot', ('ishl', ('isub', ('ishl', 1, 'bits'), 1), 'offset'))),
     ('iand', ('ishl', 'insert', 'offset'), ('ishl', ('isub', ('ishl', 1, 'bits'), 1), 'offset'))))),
    'options->lower_bitfield_insert_to_shifts'),

   # Alternative lowering that uses bitfield_select.
   (('bitfield_insert', 'base', 'insert', 'offset', 'bits'),
    ('bcsel', ('ult', 31, 'bits'), 'insert',
              ('bitfield_select', ('bfm', 'bits', 'offset'), ('ishl', 'insert', 'offset'), 'base')),
    'options->lower_bitfield_insert_to_bitfield_select'),

   (('ibitfield_extract', 'value', 'offset', 'bits'),
    ('bcsel', ('ult', 31, 'bits'), 'value',
              ('ibfe', 'value', 'offset', 'bits')),
    'options->lower_bitfield_extract'),

   (('ubitfield_extract', 'value', 'offset', 'bits'),
    ('bcsel', ('ult', 31, 'bits'), 'value',
              ('ubfe', 'value', 'offset', 'bits')),
    'options->lower_bitfield_extract'),

   # Note that these opcodes are defined to only use the five least significant bits of 'offset' and 'bits'
   (('ubfe', 'value', 'offset', ('iand', 31, 'bits')), ('ubfe', 'value', 'offset', 'bits')),
   (('ubfe', 'value', ('iand', 31, 'offset'), 'bits'), ('ubfe', 'value', 'offset', 'bits')),
   (('ibfe', 'value', 'offset', ('iand', 31, 'bits')), ('ibfe', 'value', 'offset', 'bits')),
   (('ibfe', 'value', ('iand', 31, 'offset'), 'bits'), ('ibfe', 'value', 'offset', 'bits')),
   (('bfm', 'bits', ('iand', 31, 'offset')), ('bfm', 'bits', 'offset')),
   (('bfm', ('iand', 31, 'bits'), 'offset'), ('bfm', 'bits', 'offset')),

   (('ibitfield_extract', 'value', 'offset', 'bits'),
    ('bcsel', ('ieq', 0, 'bits'),
     0,
     ('ishr',
       ('ishl', 'value', ('isub', ('isub', 32, 'bits'), 'offset')),
       ('isub', 32, 'bits'))),
    'options->lower_bitfield_extract_to_shifts'),

   (('ubitfield_extract', 'value', 'offset', 'bits'),
    ('iand',
     ('ushr', 'value', 'offset'),
     ('bcsel', ('ieq', 'bits', 32),
      0xffffffff,
      ('isub', ('ishl', 1, 'bits'), 1))),
    'options->lower_bitfield_extract_to_shifts'),

   (('ifind_msb', 'value'),
    ('ufind_msb', ('bcsel', ('ilt', 'value', 0), ('inot', 'value'), 'value')),
    'options->lower_ifind_msb'),

   (('find_lsb', 'value'),
    ('ufind_msb', ('iand', 'value', ('ineg', 'value'))),
    'options->lower_find_lsb'),

   (('extract_i8', a, 'b@32'),
    ('ishr', ('ishl', a, ('imul', ('isub', 3, b), 8)), 24),
    'options->lower_extract_byte'),

   (('extract_u8', a, 'b@32'),
    ('iand', ('ushr', a, ('imul', b, 8)), 0xff),
    'options->lower_extract_byte'),

   (('extract_i16', a, 'b@32'),
    ('ishr', ('ishl', a, ('imul', ('isub', 1, b), 16)), 16),
    'options->lower_extract_word'),

   (('extract_u16', a, 'b@32'),
    ('iand', ('ushr', a, ('imul', b, 16)), 0xffff),
    'options->lower_extract_word'),

    (('pack_unorm_2x16', 'v'),
     ('pack_uvec2_to_uint',
        ('f2u32', ('fround_even', ('fmul', ('fsat', 'v'), 65535.0)))),
     'options->lower_pack_unorm_2x16'),

    (('pack_unorm_4x8', 'v'),
     ('pack_uvec4_to_uint',
        ('f2u32', ('fround_even', ('fmul', ('fsat', 'v'), 255.0)))),
     'options->lower_pack_unorm_4x8'),

    (('pack_snorm_2x16', 'v'),
     ('pack_uvec2_to_uint',
        ('f2i32', ('fround_even', ('fmul', ('fmin', 1.0, ('fmax', -1.0, 'v')), 32767.0)))),
     'options->lower_pack_snorm_2x16'),

    (('pack_snorm_4x8', 'v'),
     ('pack_uvec4_to_uint',
        ('f2i32', ('fround_even', ('fmul', ('fmin', 1.0, ('fmax', -1.0, 'v')), 127.0)))),
     'options->lower_pack_snorm_4x8'),

    (('unpack_unorm_2x16', 'v'),
     ('fdiv', ('u2f32', ('vec2', ('extract_u16', 'v', 0),
                                  ('extract_u16', 'v', 1))),
              65535.0),
     'options->lower_unpack_unorm_2x16'),

    (('unpack_unorm_4x8', 'v'),
     ('fdiv', ('u2f32', ('vec4', ('extract_u8', 'v', 0),
                                  ('extract_u8', 'v', 1),
                                  ('extract_u8', 'v', 2),
                                  ('extract_u8', 'v', 3))),
              255.0),
     'options->lower_unpack_unorm_4x8'),

    (('unpack_snorm_2x16', 'v'),
     ('fmin', 1.0, ('fmax', -1.0, ('fdiv', ('i2f', ('vec2', ('extract_i16', 'v', 0),
                                                            ('extract_i16', 'v', 1))),
                                           32767.0))),
     'options->lower_unpack_snorm_2x16'),

    (('unpack_snorm_4x8', 'v'),
     ('fmin', 1.0, ('fmax', -1.0, ('fdiv', ('i2f', ('vec4', ('extract_i8', 'v', 0),
                                                            ('extract_i8', 'v', 1),
                                                            ('extract_i8', 'v', 2),
                                                            ('extract_i8', 'v', 3))),
                                           127.0))),
     'options->lower_unpack_snorm_4x8'),

   (('isign', a), ('imin', ('imax', a, -1), 1), 'options->lower_isign'),
   (('fsign', a), ('fsub', ('b2f', ('flt', 0.0, a)), ('b2f', ('flt', a, 0.0))), 'options->lower_fsign'),
])

# bit_size dependent lowerings
for bit_size in [8, 16, 32, 64]:
   # convenience constants
   intmax = (1 << (bit_size - 1)) - 1
   intmin = 1 << (bit_size - 1)

   optimizations += [
      (('iadd_sat@' + str(bit_size), a, b),
       ('bcsel', ('ige', b, 1), ('bcsel', ('ilt', ('iadd', a, b), a), intmax, ('iadd', a, b)),
                                ('bcsel', ('ilt', a, ('iadd', a, b)), intmin, ('iadd', a, b))), 'options->lower_add_sat'),
      (('isub_sat@' + str(bit_size), a, b),
       ('bcsel', ('ilt', b, 0), ('bcsel', ('ilt', ('isub', a, b), a), intmax, ('isub', a, b)),
                                ('bcsel', ('ilt', a, ('isub', a, b)), intmin, ('isub', a, b))), 'options->lower_add_sat'),
   ]

invert = OrderedDict([('feq', 'fne'), ('fne', 'feq'), ('fge', 'flt'), ('flt', 'fge')])

for left, right in itertools.combinations_with_replacement(invert.keys(), 2):
   optimizations.append((('inot', ('ior(is_used_once)', (left, a, b), (right, c, d))),
                         ('iand', (invert[left], a, b), (invert[right], c, d))))
   optimizations.append((('inot', ('iand(is_used_once)', (left, a, b), (right, c, d))),
                         ('ior', (invert[left], a, b), (invert[right], c, d))))

# Optimize x2bN(b2x(x)) -> x
for size in type_sizes('bool'):
    aN = 'a@' + str(size)
    f2bN = 'f2b' + str(size)
    i2bN = 'i2b' + str(size)
    optimizations.append(((f2bN, ('b2f', aN)), a))
    optimizations.append(((i2bN, ('b2i', aN)), a))

# Optimize x2yN(b2x(x)) -> b2y
for x, y in itertools.product(['f', 'u', 'i'], ['f', 'u', 'i']):
   if x != 'f' and y != 'f' and x != y:
      continue

   b2x = 'b2f' if x == 'f' else 'b2i'
   b2y = 'b2f' if y == 'f' else 'b2i'
   x2yN = '{}2{}'.format(x, y)
   optimizations.append(((x2yN, (b2x, a)), (b2y, a)))

# Optimize away x2xN(a@N)
for t in ['int', 'uint', 'float']:
   for N in type_sizes(t):
      x2xN = '{0}2{0}{1}'.format(t[0], N)
      aN = 'a@{0}'.format(N)
      optimizations.append(((x2xN, aN), a))

# Optimize x2xN(y2yM(a@P)) -> y2yN(a) for integers
# In particular, we can optimize away everything except upcast of downcast and
# upcasts where the type differs from the other cast
for N, M in itertools.product(type_sizes('uint'), type_sizes('uint')):
   if N < M:
      # The outer cast is a down-cast.  It doesn't matter what the size of the
      # argument of the inner cast is because we'll never been in the upcast
      # of downcast case.  Regardless of types, we'll always end up with y2yN
      # in the end.
      for x, y in itertools.product(['i', 'u'], ['i', 'u']):
         x2xN = '{0}2{0}{1}'.format(x, N)
         y2yM = '{0}2{0}{1}'.format(y, M)
         y2yN = '{0}2{0}{1}'.format(y, N)
         optimizations.append(((x2xN, (y2yM, a)), (y2yN, a)))
   elif N > M:
      # If the outer cast is an up-cast, we have to be more careful about the
      # size of the argument of the inner cast and with types.  In this case,
      # the type is always the type of type up-cast which is given by the
      # outer cast.
      for P in type_sizes('uint'):
         # We can't optimize away up-cast of down-cast.
         if M < P:
            continue

         # Because we're doing down-cast of down-cast, the types always have
         # to match between the two casts
         for x in ['i', 'u']:
            x2xN = '{0}2{0}{1}'.format(x, N)
            x2xM = '{0}2{0}{1}'.format(x, M)
            aP = 'a@{0}'.format(P)
            optimizations.append(((x2xN, (x2xM, aP)), (x2xN, a)))
   else:
      # The N == M case is handled by other optimizations
      pass

# Optimize comparisons with up-casts
for t in ['int', 'uint', 'float']:
    for N, M in itertools.product(type_sizes(t), repeat=2):
        if N == 1 or N >= M:
            continue

        x2xM = '{0}2{0}{1}'.format(t[0], M)
        x2xN = '{0}2{0}{1}'.format(t[0], N)
        aN = 'a@' + str(N)
        bN = 'b@' + str(N)
        xeq = 'feq' if t == 'float' else 'ieq'
        xne = 'fne' if t == 'float' else 'ine'
        xge = '{0}ge'.format(t[0])
        xlt = '{0}lt'.format(t[0])

        # Up-casts are lossless so for correctly signed comparisons of
        # up-casted values we can do the comparison at the largest of the two
        # original sizes and drop one or both of the casts.  (We have
        # optimizations to drop the no-op casts which this may generate.)
        for P in type_sizes(t):
            if P == 1 or P > N:
                continue

            bP = 'b@' + str(P)
            optimizations += [
                ((xeq, (x2xM, aN), (x2xM, bP)), (xeq, a, (x2xN, b))),
                ((xne, (x2xM, aN), (x2xM, bP)), (xne, a, (x2xN, b))),
                ((xge, (x2xM, aN), (x2xM, bP)), (xge, a, (x2xN, b))),
                ((xlt, (x2xM, aN), (x2xM, bP)), (xlt, a, (x2xN, b))),
                ((xge, (x2xM, bP), (x2xM, aN)), (xge, (x2xN, b), a)),
                ((xlt, (x2xM, bP), (x2xM, aN)), (xlt, (x2xN, b), a)),
            ]

        # The next bit doesn't work on floats because the range checks would
        # get way too complicated.
        if t in ['int', 'uint']:
            if t == 'int':
                xN_min = -(1 << (N - 1))
                xN_max = (1 << (N - 1)) - 1
            elif t == 'uint':
                xN_min = 0
                xN_max = (1 << N) - 1
            else:
                assert False

            # If we're up-casting and comparing to a constant, we can unfold
            # the comparison into a comparison with the shrunk down constant
            # and a check that the constant fits in the smaller bit size.
            optimizations += [
                ((xeq, (x2xM, aN), '#b'),
                 ('iand', (xeq, a, (x2xN, b)), (xeq, (x2xM, (x2xN, b)), b))),
                ((xne, (x2xM, aN), '#b'),
                 ('ior', (xne, a, (x2xN, b)), (xne, (x2xM, (x2xN, b)), b))),
                ((xlt, (x2xM, aN), '#b'),
                 ('iand', (xlt, xN_min, b),
                          ('ior', (xlt, xN_max, b), (xlt, a, (x2xN, b))))),
                ((xlt, '#a', (x2xM, bN)),
                 ('iand', (xlt, a, xN_max),
                          ('ior', (xlt, a, xN_min), (xlt, (x2xN, a), b)))),
                ((xge, (x2xM, aN), '#b'),
                 ('iand', (xge, xN_max, b),
                          ('ior', (xge, xN_min, b), (xge, a, (x2xN, b))))),
                ((xge, '#a', (x2xM, bN)),
                 ('iand', (xge, a, xN_min),
                          ('ior', (xge, a, xN_max), (xge, (x2xN, a), b)))),
            ]

def fexp2i(exp, bits):
   # We assume that exp is already in the right range.
   if bits == 16:
      return ('i2i16', ('ishl', ('iadd', exp, 15), 10))
   elif bits == 32:
      return ('ishl', ('iadd', exp, 127), 23)
   elif bits == 64:
      return ('pack_64_2x32_split', 0, ('ishl', ('iadd', exp, 1023), 20))
   else:
      assert False

def ldexp(f, exp, bits):
   # First, we clamp exp to a reasonable range.  The maximum possible range
   # for a normal exponent is [-126, 127] and, throwing in denormals, you get
   # a maximum range of [-149, 127].  This means that we can potentially have
   # a swing of +-276.  If you start with FLT_MAX, you actually have to do
   # ldexp(FLT_MAX, -278) to get it to flush all the way to zero.  The GLSL
   # spec, on the other hand, only requires that we handle an exponent value
   # in the range [-126, 128].  This implementation is *mostly* correct; it
   # handles a range on exp of [-252, 254] which allows you to create any
   # value (including denorms if the hardware supports it) and to adjust the
   # exponent of any normal value to anything you want.
   if bits == 16:
      exp = ('imin', ('imax', exp, -28), 30)
   elif bits == 32:
      exp = ('imin', ('imax', exp, -252), 254)
   elif bits == 64:
      exp = ('imin', ('imax', exp, -2044), 2046)
   else:
      assert False

   # Now we compute two powers of 2, one for exp/2 and one for exp-exp/2.
   # (We use ishr which isn't the same for -1, but the -1 case still works
   # since we use exp-exp/2 as the second exponent.)  While the spec
   # technically defines ldexp as f * 2.0^exp, simply multiplying once doesn't
   # work with denormals and doesn't allow for the full swing in exponents
   # that you can get with normalized values.  Instead, we create two powers
   # of two and multiply by them each in turn.  That way the effective range
   # of our exponent is doubled.
   pow2_1 = fexp2i(('ishr', exp, 1), bits)
   pow2_2 = fexp2i(('isub', exp, ('ishr', exp, 1)), bits)
   return ('fmul', ('fmul', f, pow2_1), pow2_2)

optimizations += [
   (('ldexp@16', 'x', 'exp'), ldexp('x', 'exp', 16), 'options->lower_ldexp'),
   (('ldexp@32', 'x', 'exp'), ldexp('x', 'exp', 32), 'options->lower_ldexp'),
   (('ldexp@64', 'x', 'exp'), ldexp('x', 'exp', 64), 'options->lower_ldexp'),
]

# Unreal Engine 4 demo applications open-codes bitfieldReverse()
def bitfield_reverse(u):
    step1 = ('ior', ('ishl', u, 16), ('ushr', u, 16))
    step2 = ('ior', ('ishl', ('iand', step1, 0x00ff00ff), 8), ('ushr', ('iand', step1, 0xff00ff00), 8))
    step3 = ('ior', ('ishl', ('iand', step2, 0x0f0f0f0f), 4), ('ushr', ('iand', step2, 0xf0f0f0f0), 4))
    step4 = ('ior', ('ishl', ('iand', step3, 0x33333333), 2), ('ushr', ('iand', step3, 0xcccccccc), 2))
    step5 = ('ior(many-comm-expr)', ('ishl', ('iand', step4, 0x55555555), 1), ('ushr', ('iand', step4, 0xaaaaaaaa), 1))

    return step5

optimizations += [(bitfield_reverse('x@32'), ('bitfield_reverse', 'x'))]

# For any float comparison operation, "cmp", if you have "a == a && a cmp b"
# then the "a == a" is redundant because it's equivalent to "a is not NaN"
# and, if a is a NaN then the second comparison will fail anyway.
for op in ['flt', 'fge', 'feq']:
   optimizations += [
      (('iand', ('feq', a, a), (op, a, b)), (op, a, b)),
      (('iand', ('feq', a, a), (op, b, a)), (op, b, a)),
   ]

# Add optimizations to handle the case where the result of a ternary is
# compared to a constant.  This way we can take things like
#
# (a ? 0 : 1) > 0
#
# and turn it into
#
# a ? (0 > 0) : (1 > 0)
#
# which constant folding will eat for lunch.  The resulting ternary will
# further get cleaned up by the boolean reductions above and we will be
# left with just the original variable "a".
for op in ['flt', 'fge', 'feq', 'fne',
           'ilt', 'ige', 'ieq', 'ine', 'ult', 'uge']:
   optimizations += [
      ((op, ('bcsel', 'a', '#b', '#c'), '#d'),
       ('bcsel', 'a', (op, 'b', 'd'), (op, 'c', 'd'))),
      ((op, '#d', ('bcsel', a, '#b', '#c')),
       ('bcsel', 'a', (op, 'd', 'b'), (op, 'd', 'c'))),
   ]


# For example, this converts things like
#
#    1 + mix(0, a - 1, condition)
#
# into
#
#    mix(1, (a-1)+1, condition)
#
# Other optimizations will rearrange the constants.
for op in ['fadd', 'fmul', 'iadd', 'imul']:
   optimizations += [
      ((op, ('bcsel(is_used_once)', a, '#b', c), '#d'), ('bcsel', a, (op, b, d), (op, c, d)))
   ]

# For derivatives in compute shaders, GLSL_NV_compute_shader_derivatives
# states:
#
#     If neither layout qualifier is specified, derivatives in compute shaders
#     return zero, which is consistent with the handling of built-in texture
#     functions like texture() in GLSL 4.50 compute shaders.
for op in ['fddx', 'fddx_fine', 'fddx_coarse',
           'fddy', 'fddy_fine', 'fddy_coarse']:
   optimizations += [
      ((op, 'a'), 0.0, 'info->stage == MESA_SHADER_COMPUTE && info->cs.derivative_group == DERIVATIVE_GROUP_NONE')
]

# Some optimizations for ir3-specific instructions.
optimizations += [
   # 'al * bl': If either 'al' or 'bl' is zero, return zero.
   (('umul_low', '#a(is_lower_half_zero)', 'b'), (0)),
   # '(ah * bl) << 16 + c': If either 'ah' or 'bl' is zero, return 'c'.
   (('imadsh_mix16', '#a@32(is_lower_half_zero)', 'b@32', 'c@32'), ('c')),
   (('imadsh_mix16', 'a@32', '#b@32(is_upper_half_zero)', 'c@32'), ('c')),
]

# This section contains "late" optimizations that should be run before
# creating ffmas and calling regular optimizations for the final time.
# Optimizations should go here if they help code generation and conflict
# with the regular optimizations.
before_ffma_optimizations = [
   # Propagate constants down multiplication chains
   (('~fmul(is_used_once)', ('fmul(is_used_once)', 'a(is_not_const)', '#b'), 'c(is_not_const)'), ('fmul', ('fmul', a, c), b)),
   (('imul(is_used_once)', ('imul(is_used_once)', 'a(is_not_const)', '#b'), 'c(is_not_const)'), ('imul', ('imul', a, c), b)),
   (('~fadd(is_used_once)', ('fadd(is_used_once)', 'a(is_not_const)', '#b'), 'c(is_not_const)'), ('fadd', ('fadd', a, c), b)),
   (('iadd(is_used_once)', ('iadd(is_used_once)', 'a(is_not_const)', '#b'), 'c(is_not_const)'), ('iadd', ('iadd', a, c), b)),

   (('~fadd', ('fmul', a, b), ('fmul', a, c)), ('fmul', a, ('fadd', b, c))),
   (('iadd', ('imul', a, b), ('imul', a, c)), ('imul', a, ('iadd', b, c))),
   (('~fadd', ('fneg', a), a), 0.0),
   (('iadd', ('ineg', a), a), 0),
   (('iadd', ('ineg', a), ('iadd', a, b)), b),
   (('iadd', a, ('iadd', ('ineg', a), b)), b),
   (('~fadd', ('fneg', a), ('fadd', a, b)), b),
   (('~fadd', a, ('fadd', ('fneg', a), b)), b),

   (('~flrp@32', ('fadd(is_used_once)', a, -1.0), ('fadd(is_used_once)', a,  1.0), d), ('fadd', ('flrp', -1.0,  1.0, d), a)),
   (('~flrp@32', ('fadd(is_used_once)', a,  1.0), ('fadd(is_used_once)', a, -1.0), d), ('fadd', ('flrp',  1.0, -1.0, d), a)),
   (('~flrp@32', ('fadd(is_used_once)', a, '#b'), ('fadd(is_used_once)', a, '#c'), d), ('fadd', ('fmul', d, ('fadd', c, ('fneg', b))), ('fadd', a, b))),
]

# This section contains "late" optimizations that should be run after the
# regular optimizations have finished.  Optimizations should go here if
# they help code generation but do not necessarily produce code that is
# more easily optimizable.
late_optimizations = [
   # Most of these optimizations aren't quite safe when you get infinity or
   # Nan involved but the first one should be fine.
   (('flt',          ('fadd', a, b),  0.0), ('flt',          a, ('fneg', b))),
   (('flt', ('fneg', ('fadd', a, b)), 0.0), ('flt', ('fneg', a),         b)),
   (('~fge',          ('fadd', a, b),  0.0), ('fge',          a, ('fneg', b))),
   (('~fge', ('fneg', ('fadd', a, b)), 0.0), ('fge', ('fneg', a),         b)),
   (('~feq', ('fadd', a, b), 0.0), ('feq', a, ('fneg', b))),
   (('~fne', ('fadd', a, b), 0.0), ('fne', a, ('fneg', b))),

   # nir_lower_to_source_mods will collapse this, but its existence during the
   # optimization loop can prevent other optimizations.
   (('fneg', ('fneg', a)), a),

   # These are duplicated from the main optimizations table.  The late
   # patterns that rearrange expressions like x - .5 < 0 to x < .5 can create
   # new patterns like these.  The patterns that compare with zero are removed
   # because they are unlikely to be created in by anything in
   # late_optimizations.
   (('flt', ('fsat(is_used_once)', a), '#b(is_gt_0_and_lt_1)'), ('flt', a, b)),
   (('flt', '#b(is_gt_0_and_lt_1)', ('fsat(is_used_once)', a)), ('flt', b, a)),
   (('fge', ('fsat(is_used_once)', a), '#b(is_gt_0_and_lt_1)'), ('fge', a, b)),
   (('fge', '#b(is_gt_0_and_lt_1)', ('fsat(is_used_once)', a)), ('fge', b, a)),
   (('feq', ('fsat(is_used_once)', a), '#b(is_gt_0_and_lt_1)'), ('feq', a, b)),
   (('fne', ('fsat(is_used_once)', a), '#b(is_gt_0_and_lt_1)'), ('fne', a, b)),

   (('fge', ('fsat(is_used_once)', a), 1.0), ('fge', a, 1.0)),
   (('flt', ('fsat(is_used_once)', a), 1.0), ('flt', a, 1.0)),

   (('~fge', ('fmin(is_used_once)', ('fadd(is_used_once)', a, b), ('fadd', c, d)), 0.0), ('iand', ('fge', a, ('fneg', b)), ('fge', c, ('fneg', d)))),

   (('flt', ('fneg', a), ('fneg', b)), ('flt', b, a)),
   (('fge', ('fneg', a), ('fneg', b)), ('fge', b, a)),
   (('feq', ('fneg', a), ('fneg', b)), ('feq', b, a)),
   (('fne', ('fneg', a), ('fneg', b)), ('fne', b, a)),
   (('flt', ('fneg', a), -1.0), ('flt', 1.0, a)),
   (('flt', -1.0, ('fneg', a)), ('flt', a, 1.0)),
   (('fge', ('fneg', a), -1.0), ('fge', 1.0, a)),
   (('fge', -1.0, ('fneg', a)), ('fge', a, 1.0)),
   (('fne', ('fneg', a), -1.0), ('fne', 1.0, a)),
   (('feq', -1.0, ('fneg', a)), ('feq', a, 1.0)),

   (('ior', a, a), a),
   (('iand', a, a), a),

   (('~fadd', ('fneg(is_used_once)', ('fsat(is_used_once)', 'a(is_not_fmul)')), 1.0), ('fsat', ('fadd', 1.0, ('fneg', a)))),

   (('fdot2', a, b), ('fdot_replicated2', a, b), 'options->fdot_replicates'),
   (('fdot3', a, b), ('fdot_replicated3', a, b), 'options->fdot_replicates'),
   (('fdot4', a, b), ('fdot_replicated4', a, b), 'options->fdot_replicates'),
   (('fdph', a, b), ('fdph_replicated', a, b), 'options->fdot_replicates'),

   (('~flrp@32', ('fadd(is_used_once)', a, b), ('fadd(is_used_once)', a, c), d), ('fadd', ('flrp', b, c, d), a)),
   (('~flrp@64', ('fadd(is_used_once)', a, b), ('fadd(is_used_once)', a, c), d), ('fadd', ('flrp', b, c, d), a)),

   (('~fadd@32', 1.0, ('fmul(is_used_once)', c , ('fadd', b, -1.0 ))), ('fadd', ('fadd', 1.0, ('fneg', c)), ('fmul', b, c)), 'options->lower_flrp32'),
   (('~fadd@64', 1.0, ('fmul(is_used_once)', c , ('fadd', b, -1.0 ))), ('fadd', ('fadd', 1.0, ('fneg', c)), ('fmul', b, c)), 'options->lower_flrp64'),

   # A similar operation could apply to any ffma(#a, b, #(-a/2)), but this
   # particular operation is common for expanding values stored in a texture
   # from [0,1] to [-1,1].
   (('~ffma@32', a,  2.0, -1.0), ('flrp', -1.0,  1.0,          a ), '!options->lower_flrp32'),
   (('~ffma@32', a, -2.0, -1.0), ('flrp', -1.0,  1.0, ('fneg', a)), '!options->lower_flrp32'),
   (('~ffma@32', a, -2.0,  1.0), ('flrp',  1.0, -1.0,          a ), '!options->lower_flrp32'),
   (('~ffma@32', a,  2.0,  1.0), ('flrp',  1.0, -1.0, ('fneg', a)), '!options->lower_flrp32'),
   (('~fadd@32', ('fmul(is_used_once)',  2.0, a), -1.0), ('flrp', -1.0,  1.0,          a ), '!options->lower_flrp32'),
   (('~fadd@32', ('fmul(is_used_once)', -2.0, a), -1.0), ('flrp', -1.0,  1.0, ('fneg', a)), '!options->lower_flrp32'),
   (('~fadd@32', ('fmul(is_used_once)', -2.0, a),  1.0), ('flrp',  1.0, -1.0,          a ), '!options->lower_flrp32'),
   (('~fadd@32', ('fmul(is_used_once)',  2.0, a),  1.0), ('flrp',  1.0, -1.0, ('fneg', a)), '!options->lower_flrp32'),

    # flrp(a, b, a)
    # a*(1-a) + b*a
    # a + -a*a + a*b    (1)
    # a + a*(b - a)
    # Option 1: ffma(a, (b-a), a)
    #
    # Alternately, after (1):
    # a*(1+b) + -a*a
    # a*((1+b) + -a)
    #
    # Let b=1
    #
    # Option 2: ffma(a, 2, -(a*a))
    # Option 3: ffma(a, 2, (-a)*a)
    # Option 4: ffma(a, -a, (2*a)
    # Option 5: a * (2 - a)
    #
    # There are a lot of other possible combinations.
   (('~ffma@32', ('fadd', b, ('fneg', a)), a, a), ('flrp', a, b, a), '!options->lower_flrp32'),
   (('~ffma@32', a, 2.0, ('fneg', ('fmul', a, a))), ('flrp', a, 1.0, a), '!options->lower_flrp32'),
   (('~ffma@32', a, 2.0, ('fmul', ('fneg', a), a)), ('flrp', a, 1.0, a), '!options->lower_flrp32'),
   (('~ffma@32', a, ('fneg', a), ('fmul', 2.0, a)), ('flrp', a, 1.0, a), '!options->lower_flrp32'),
   (('~fmul@32', a, ('fadd', 2.0, ('fneg', a))),    ('flrp', a, 1.0, a), '!options->lower_flrp32'),

   # we do these late so that we don't get in the way of creating ffmas
   (('fmin', ('fadd(is_used_once)', '#c', a), ('fadd(is_used_once)', '#c', b)), ('fadd', c, ('fmin', a, b))),
   (('fmax', ('fadd(is_used_once)', '#c', a), ('fadd(is_used_once)', '#c', b)), ('fadd', c, ('fmax', a, b))),

   (('bcsel', a, 0, ('b2f32', ('inot', 'b@bool'))), ('b2f32', ('inot', ('ior', a, b)))),

   # Things that look like DPH in the source shader may get expanded to
   # something that looks like dot(v1.xyz, v2.xyz) + v1.w by the time it gets
   # to NIR.  After FFMA is generated, this can look like:
   #
   #    fadd(ffma(v1.z, v2.z, ffma(v1.y, v2.y, fmul(v1.x, v2.x))), v1.w)
   #
   # Reassociate the last addition into the first multiplication.
   (('~fadd', ('ffma(is_used_once)', a, b, ('ffma', c, d, ('fmul', 'e(is_not_const_and_not_fsign)', 'f(is_not_const_and_not_fsign)'))), 'g(is_not_const)'),
    ('ffma', a, b, ('ffma', c, d, ('ffma', e, 'f', 'g'))), '!options->intel_vec4'),
   (('~fadd', ('ffma(is_used_once)', a, b,                ('fmul', 'e(is_not_const_and_not_fsign)', 'f(is_not_const_and_not_fsign)') ), 'g(is_not_const)'),
    ('ffma', a, b,                ('ffma', e, 'f', 'g') ), '!options->intel_vec4'),
]

print(nir_algebraic.AlgebraicPass("nir_opt_algebraic", optimizations).render())
print(nir_algebraic.AlgebraicPass("nir_opt_algebraic_before_ffma",
                                  before_ffma_optimizations).render())
print(nir_algebraic.AlgebraicPass("nir_opt_algebraic_late",
                                  late_optimizations).render())
