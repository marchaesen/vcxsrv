#
# Copyright (C) 2018 Alyssa Rosenzweig
#
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

algebraic_late = [
    # ineg must be lowered late, but only for integers; floats will try to
    # have modifiers attached... hence why this has to be here rather than
    # a more standard lower_negate approach

    (('ineg', a), ('isub', 0, a)),

    # These two special-cases save space/an op than the actual csel op +
    # scheduler flexibility

    (('b32csel', a, 'b@32', 0), ('iand', a, b)),
    (('b32csel', a, 0, 'b@32'), ('iand', ('inot', a), b)),
]


# Midgard is able to type convert down by only one "step" per instruction; if
# NIR wants more than one step, we need to break up into multiple instructions

converts = [
    (('i2i8', 'a@32'), ('i2i8', ('i2i16', a))),
    (('u2u8', 'a@32'), ('u2u8', ('u2u16', a))),

    (('i2i32', 'a@8'), ('i2i32', ('i2i16', a))),
    (('u2u32', 'a@8'), ('u2u32', ('u2u16', a))),

    (('f2i32', 'a@16'), ('f2i32', ('f2f32', a))),
    (('f2u32', 'a@16'), ('f2u32', ('f2f32', a))),

    # Totally redundant
    (('~f2f16', ('f2f32', 'a@16')), a),
]

# Midgard scales fsin/fcos arguments by pi.
# Pass must be run only once, after the main loop

scale_trig = [
        (('fsin', a), ('fsin', ('fdiv', a, math.pi))),
        (('fcos', a), ('fcos', ('fdiv', a, math.pi))),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()


def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "midgard_nir.h"')

    print(nir_algebraic.AlgebraicPass("midgard_nir_lower_algebraic_late",
                                      algebraic_late + converts).render())

    print(nir_algebraic.AlgebraicPass("midgard_nir_scale_trig",
                                      scale_trig).render())


if __name__ == '__main__':
    main()
