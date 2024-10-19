#
# Copyright © 2019 Igalia S.L.
#
# SPDX-License-Identifier: MIT

import argparse
import sys

imul_lowering = [
	(('imul', 'a@32', 'b@32'), ('imadsh_mix16', 'b', 'a', ('imadsh_mix16', 'a', 'b', ('umul_low', 'a', 'b')))),
        # We want to run the imad24 rule late so that it doesn't fight
        # with constant folding the (imul24, a, b).  Since this pass is
        # run late, and this is kinda imul related, this seems like a
        # good place for it:
        (('iadd', ('imul24', 'a', 'b'), 'c'), ('imad24_ir3', 'a', 'b', 'c')),
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
    print(nir_algebraic.AlgebraicPass("ir3_nir_lower_imul",
                                      imul_lowering).render())


if __name__ == '__main__':
    main()
