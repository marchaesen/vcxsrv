# SPDX-License-Identifier: MIT

import argparse
import sys

a = 'a'

has_unpack_sat = 'c && v3d_device_has_unpack_sat(c->devinfo)'
has_unpack_max0 = 'c && v3d_device_has_unpack_max0(c->devinfo)'

lower_alu = [
    (('f2i8', a), ('i2i8', ('f2i32', a))),
    (('f2i16', a), ('i2i16', ('f2i32', a))),

    (('f2u8', a), ('u2u8', ('f2u32', a))),
    (('f2u16', a), ('u2u16', ('f2u32', a))),

    (('i2f32', 'a@8'), ('i2f32', ('i2i32', a))),
    (('i2f32', 'a@16'), ('i2f32', ('i2i32', a))),

    (('u2f32', 'a@8'), ('u2f32', ('u2u32', a))),
    (('u2f32', 'a@16'), ('u2f32', ('u2u32', a))),

    (('fmin', ('fmax', a, -1.0), 1.0), ('fsat_signed', a), has_unpack_sat),
    (('fmax', ('fmin', a, 1.0), -1.0), ('fsat_signed', a), has_unpack_sat),
    (('fmax', a, 0.0), ('fclamp_pos', a), has_unpack_max0),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()

def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "v3d_compiler.h"')

    print(nir_algebraic.AlgebraicPass("v3d_nir_lower_algebraic",
                                      lower_alu,
                                      [
                                          ("const struct v3d_compile *", "c")
                                      ]).render())

if __name__ == '__main__':
    main()
