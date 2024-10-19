/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "helpers.h"

bool
util_lower_clearsize_to_dword(const void *clear_value, int *clear_value_size,
                              uint32_t *out)
{
   /* Reduce a large clear value size if possible. */
   if (*clear_value_size > 4) {
      bool clear_dword_duplicated = true;
      const uint32_t *value = clear_value;

      /* See if we can lower large fills to dword fills. */
      for (unsigned i = 1; i < *clear_value_size / 4; i++) {
         if (value[0] != value[i]) {
            clear_dword_duplicated = false;
            break;
         }
      }
      if (clear_dword_duplicated) {
         *out = *value;
         *clear_value_size = 4;
      }
      return clear_dword_duplicated;
   }

   /* Expand a small clear value size. */
   if (*clear_value_size <= 2) {
      if (*clear_value_size == 1) {
         *out = *(uint8_t *)clear_value;
         *out |=
            (*out << 8) | (*out << 16) | (*out << 24);
      } else {
         *out = *(uint16_t *)clear_value;
         *out |= *out << 16;
      }
      *clear_value_size = 4;
      return true;
   }
   return false;
}
