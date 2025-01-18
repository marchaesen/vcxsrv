# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from mako.template import Template, exceptions
from pco_pygen_common import *
from pco_ops import *
from pco_isa import *

template = """/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_COMMON_H
#define PCO_COMMON_H

/**
 * \\file pco_common.h
 *
 * \\brief PCO common definitions.
 */

#include "util/macros.h"

#include <stdbool.h>

/** Enums. */
% for enum in [enum for enum in enums.values() if enum.parent is None]:
#define _${enum.name.upper()}_COUNT ${enum.unique_count}U
enum ${enum.name} {
   % for elem in enum.elems.values():
   ${elem.cname} = ${hex(elem.value) if enum.is_bitset else elem.value},
   % endfor
};

static inline
const char *${enum.name}_str(uint64_t val) {
   switch (val) {
   % for elem in enum.elems.values():
      % if elem.string is not None:
   case ${elem.cname}:
      return "${elem.string}";
      % endif
   % endfor

   default:
      break;
   }

   unreachable();
}

% endfor
/** Enum validation. */
% for enum in enums.values():
static bool ${enum.name}_valid(uint64_t val)
{
   % if enum.is_bitset:
   return !(val & ~(${enum.valid}ULL));
   % else:
   return ${' || '.join([f'val == {val}' for val in enum.valid])};
   % endif
}

% endfor
/** Bit set variants. */
% for bit_set in bit_sets.values():
#define _${bit_set.name.upper()}_VARIANT_COUNT ${len(bit_set.variants) + 1}U
enum ${bit_set.name}_variant {
   ${bit_set.name.upper()}_NONE,
   % for variant in bit_set.variants:
   ${variant.cname},
   % endfor
};

% endfor
#endif /* PCO_COMMON_H */"""

def main():
   try:
      print(Template(template).render(enums=enums, bit_sets=bit_sets))
   except:
       raise Exception(exceptions.text_error_template().render())

if __name__ == '__main__':
   main()
