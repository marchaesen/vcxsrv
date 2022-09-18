#
# Copyright (C) 2019 Vasily Khoruzhick <anarsoul@gmail.com>
# Copyright (C) 2021 Pavel Ondračka
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
from math import pi

# Transform input to range [-PI, PI]:
#
# y = frac(x / 2PI + 0.5) * 2PI - PI
#
transform_trig_input_vs_r500 = [
        (('fsin', 'a'), ('fsin', ('fadd', ('fmul', ('ffract', ('fadd', ('fmul', 'a', 1 / (2 * pi)) , 0.5)), 2 * pi), -pi))),
        (('fcos', 'a'), ('fcos', ('fadd', ('fmul', ('ffract', ('fadd', ('fmul', 'a', 1 / (2 * pi)) , 0.5)), 2 * pi), -pi))),
]

# Transform input to range [-PI, PI]:
#
# y = frac(x / 2PI)
#
transform_trig_input_fs_r500 = [
        (('fsin', 'a'), ('fsin', ('ffract', ('fmul', 'a', 1 / (2 * pi))))),
        (('fcos', 'a'), ('fcos', ('ffract', ('fmul', 'a', 1 / (2 * pi))))),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('output')
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)

    import nir_algebraic  # pylint: disable=import-error

    with open(args.output, 'w') as f:
        f.write('#include "r300_vs.h"')

        f.write(nir_algebraic.AlgebraicPass("r300_transform_vs_trig_input",
                                            transform_trig_input_vs_r500).render())

        f.write(nir_algebraic.AlgebraicPass("r300_transform_fs_trig_input",
                                            transform_trig_input_fs_r500).render())


if __name__ == '__main__':
    main()
