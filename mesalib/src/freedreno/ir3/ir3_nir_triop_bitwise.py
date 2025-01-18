#
# Copyright © 2024 Igalia S.L.
#
# SPDX-License-Identifier: MIT

import argparse
import sys

triops_lowering = [
    (('ior',  ('ushr(is_only_used_by_ior)',  'a', 'b'), 'c'), ('shrg_ir3', 'a', 'b', 'c')),
    (('ior',  ('ishl(is_only_used_by_ior)',  'a', 'b'), 'c'), ('shlg_ir3', 'a', 'b', 'c')),
    (('iand', ('ushr(is_only_used_by_iand)', 'a', 'b'), 'c'), ('shrm_ir3', 'a', 'b', 'c')),
    (('iand', ('ishl(is_only_used_by_iand)', 'a', 'b'), 'c'), ('shlm_ir3', 'a', 'b', 'c')),
    (('ior',  ('iand(is_only_used_by_ior)',  'a', 'b'), 'c'), ('andg_ir3', 'a', 'b', 'c')),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()


def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "ir3_nir.h"')
    print(nir_algebraic.AlgebraicPass("ir3_nir_opt_triops_bitwise",
                                      triops_lowering).render())


if __name__ == '__main__':
    main()
