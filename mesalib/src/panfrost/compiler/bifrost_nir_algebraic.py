# Copyright (C) 2021 Collabora, Ltd.
# Copyright (C) 2016 Intel Corporation
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

import argparse
import sys
import math

a = 'a'
b = 'b'
c = 'c'
d = 'd'

# In general, bcsel is cheaper than bitwise arithmetic on Mali. On
# Bifrost, we can implement bcsel as either CSEL or MUX to schedule to either
# execution unit. On Valhall, bitwise arithmetic may be on the SFU whereas MUX
# is on the higher throughput CVT unit. We get a zero argument for free relative
# to the bitwise op, which would be LSHIFT_* internally taking a zero anyway.
#
# As such, it's beneficial to reexpress bitwise arithmetic of booleans as bcsel.
opt_bool_bitwise = [
    (('iand', 'a@1', 'b@1'), ('bcsel', a, b, False)),
    (('ior', 'a@1', 'b@1'), ('bcsel', a, a, b)),
    (('iand', 'a@1', ('inot', 'b@1')), ('bcsel', b, 0, a)),
    (('ior', 'a@1', ('inot', 'b@1')), ('bcsel', b, a, True)),
]

algebraic_late = [
    (('pack_32_4x8_split', a, b, c, d),
     ('pack_32_2x16_split', ('ior', ('u2u16', a), ('ishl', ('u2u16', b), 8)),
                            ('ior', ('u2u16', c), ('ishl', ('u2u16', d), 8)))),

    # Canonical form. The scheduler will convert back if it makes sense.
    (('fmul', a, 2.0), ('fadd', a, a)),

    # Fuse Mali-specific clamps
    (('fmin', ('fmax', a, -1.0), 1.0), ('fsat_signed', a)),
    (('fmax', ('fmin', a, 1.0), -1.0), ('fsat_signed', a)),
    (('fmax', a, 0.0), ('fclamp_pos', a)),

    (('b32csel', 'b@32', ('iadd', 'a@32', 1), a), ('iadd', a, ('b2i32', b))),

    # We don't have an 8-bit CSEL, so this is the best we can do.
    # Note that we use 8-bit booleans internally to preserve vectorization.
    (('imin', 'a@8', 'b@8'), ('b8csel', ('ilt8', a, b), a, b)),
    (('imax', 'a@8', 'b@8'), ('b8csel', ('ilt8', a, b), b, a)),
    (('umin', 'a@8', 'b@8'), ('b8csel', ('ult8', a, b), a, b)),
    (('umax', 'a@8', 'b@8'), ('b8csel', ('ult8', a, b), b, a)),

    # Floats are at minimum 16-bit, which means when converting to an 8-bit
    # integer, the vectorization changes. So there's no one-shot hardware
    # instruction for f2i8. Instead, lower to two NIR instructions that map
    # directly to the hardware.
    (('f2i8', a), ('i2i8', ('f2i16', a))),
    (('f2u8', a), ('u2u8', ('f2u16', a))),

    # XXX: Duplicate of nir_lower_pack
    (('unpack_64_2x32', a), ('vec2', ('unpack_64_2x32_split_x', a),
                                     ('unpack_64_2x32_split_y', a))),

    # On v11+, all non integer variant to convert to F32 are gone except for S32_TO_F32.
    (('i2f32', 'a@8'), ('i2f32', ('i2i32', a)), 'gpu_arch >= 11'),
    (('i2f32', 'a@16'), ('i2f32', ('i2i32', a)), 'gpu_arch >= 11'),
    (('u2f32', 'a@8'), ('u2f32', ('u2u32', a)), 'gpu_arch >= 11'),
    (('u2f32', 'a@16'), ('u2f32', ('u2u32', a)), 'gpu_arch >= 11'),

    # On v11+, all non integer variant to convert to F16 are gone except for S32_TO_F32.
    (('i2f16', 'a'), ('f2f16', ('i2f32', ('i2i32', a))), 'gpu_arch >= 11'),
    (('u2f16', 'a'), ('f2f16', ('u2f32', ('u2u32', a))), 'gpu_arch >= 11'),

    # We don't have S32_TO_F16 on any arch
    (('i2f16', 'a@32'), ('f2f16', ('i2f32', a))),
    (('u2f16', 'a@32'), ('f2f16', ('u2f32', a))),

    # On v11+, V2F16_TO_V2S16 / V2F16_TO_V2U16 are gone
    (('f2i16', 'a@16'), ('f2i16', ('f2f32', a)), 'gpu_arch >= 11'),
    (('f2u16', 'a@16'), ('f2u16', ('f2f32', a)), 'gpu_arch >= 11'),

    # On v11+, F16_TO_S32/F16_TO_U32 is gone but we still have F32_TO_S32/F32_TO_U32
    (('f2i32', 'a@16'), ('f2i32', ('f2f32', a)), 'gpu_arch >= 11'),
    (('f2u32', 'a@16'), ('f2u32', ('f2f32', a)), 'gpu_arch >= 11'),

    # On v11+, IABS.v4s8 is gone
    (('iabs', 'a@8'), ('i2i8', ('iabs', ('i2i16', a))), 'gpu_arch >= 11'),

    # On v11+, ISUB.v4s8 is gone
    (('ineg', 'a@8'), ('i2i8', ('ineg', ('i2i16', a))), 'gpu_arch >= 11'),
    (('isub', 'a@8', 'b@8'), ('i2i8', ('isub', ('i2i16', a), ('i2i16', b))), 'gpu_arch >= 11'),
    (('isub_sat', 'a@8', 'b@8'), ('i2i8', ('isub_sat', ('i2i16', a), ('i2i16', b))), 'gpu_arch >= 11'),
    (('usub_sat', 'a@8', 'b@8'), ('u2u8', ('usub_sat', ('u2u16', a), ('u2u16', b))), 'gpu_arch >= 11'),
]

# On v11+, ICMP_OR.v4u8 was removed
for cond in ['ilt', 'ige', 'ieq', 'ine', 'ult', 'uge']:
    convert_8bit = 'u2u8'
    convert_16bit = 'u2u16'

    if cond[0] == 'i':
        convert_8bit = 'i2i8'
        convert_16bit = 'i2i16'

    algebraic_late += [
        ((f'{cond}8', a, b), (convert_8bit, (f'{cond}16', (convert_16bit, a), (convert_16bit, b))), 'gpu_arch >= 11'),
    ]

# Handling all combinations of boolean and float sizes for b2f is nontrivial.
# bcsel has the same problem in more generality; lower b2f to bcsel in NIR to
# reuse the efficient implementations of bcsel. This includes special handling
# to allow vectorization in places the hardware does not directly.
#
# Because this lowering must happen late, NIR won't squash inot in
# automatically. Do so explicitly. (The more specific pattern must be first.)
for bsz in [8, 16, 32]:
    for fsz in [16, 32]:
        algebraic_late += [
                ((f'b2f{fsz}', ('inot', f'a@{bsz}')), (f'b{bsz}csel', a, 0.0, 1.0)),
                ((f'b2f{fsz}', f'a@{bsz}'), (f'b{bsz}csel', a, 1.0, 0.0)),
        ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()


def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "bifrost_nir.h"')

    print(nir_algebraic.AlgebraicPass("bifrost_nir_opt_boolean_bitwise",
                                      opt_bool_bitwise).render())
    print(nir_algebraic.AlgebraicPass("bifrost_nir_lower_algebraic_late",
                                      algebraic_late,
                                      [
                                          ("unsigned ", "gpu_arch")
                                      ]).render())

if __name__ == '__main__':
    main()
