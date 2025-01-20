# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

from mako.template import Template, exceptions
from pco_isa import *

template = """/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_ISA_H
#define PCO_ISA_H

/**
 * \\file pco_isa.h
 *
 * \\brief PCO ISA definitions.
 */

#include "pco_common.h"
#include "util/macros.h"

#include <stdbool.h>

% for _bit_set_name, bit_set in bit_sets.items():
/** ${_bit_set_name} */
enum ${bit_set.name}_field {
   % for field in bit_set.fields.values():
   ${field.cname},
   % endfor
};

static unsigned ${bit_set.name}_bytes(enum ${bit_set.name}_variant variant)
{
   switch (variant) {
   case ${bit_set.name.upper()}_NONE: return 0;
   % for variant in bit_set.variants:
   case ${variant.cname}: return ${variant.bytes};
   % endfor
   default: break;
   }

   unreachable();
}

static unsigned ${bit_set.name}_encode_field(uint8_t *bin, enum ${bit_set.name}_field field, uint64_t val)
{
   uint64_t enc_val;

   switch (field) {
   % for field in bit_set.fields.values():
   case ${field.cname}:
      % if field.field_type.check is not None:
      assert(${field.field_type.check.format('val')});
      % endif
      % if field.reserved is not None:
      assert(val == ${field.reserved});
      % endif
      enc_val = ${field.field_type.encode.format('val') if field.field_type.encode is not None else 'val'};
      assert(${field.validate.format('enc_val')});
         % for encoding in field.encoding:
      ${encoding.clear.format('bin')};
      ${encoding.set.format('bin', 'enc_val')};
         % endfor
      return ${field.encoded_bits};

   % endfor
   default:
      break;
   }

   unreachable();
}

   % for bit_struct in bit_set.bit_structs.values():
      % if len(bit_struct.struct_fields) > 0:
struct ${bit_struct.name} {
         % for struct_field in bit_struct.struct_fields.values():
   ${struct_field.type.name} ${struct_field.field} : ${struct_field.bits};
         % endfor
};
      % endif

   % if len(bit_struct.struct_fields) > 0:
#define ${bit_struct.name}_encode(bin, ...) _${bit_struct.name}_encode(bin, (const struct ${bit_struct.name}){0, ##__VA_ARGS__})
static unsigned _${bit_struct.name}_encode(uint8_t *bin, const struct ${bit_struct.name} s)
   % else:
static unsigned ${bit_struct.name}_encode(uint8_t *bin)
   % endif
{
   unsigned bits_encoded = 0;

      % for encode_field in bit_struct.encode_fields:
   bits_encoded += ${bit_set.name}_encode_field(bin, ${encode_field.name}, ${encode_field.value});
      % endfor

   assert(!(bits_encoded % 8));
   return bits_encoded / 8;
}

   % endfor
% endfor
#endif /* PCO_ISA_H */"""

def main():
   try:
      print(Template(template).render(bit_sets=bit_sets))
   except:
       raise Exception(exceptions.text_error_template().render())

if __name__ == '__main__':
   main()
