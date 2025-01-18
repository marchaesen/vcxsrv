/*
 * Copyright 2021 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#ifndef U_HEXDUMP_H
#define U_HEXDUMP_H

#include <stdio.h>
#include <stdbool.h>

static inline void
u_hexdump(FILE *fp, const uint8_t *hex, size_t cnt, bool with_strings)
{
   for (unsigned i = 0; i < cnt; ++i) {
      if ((i & 0xF) == 0 && i >= 0x10) {
         unsigned j;

         for (j = i; j + 0x10 < cnt; j += 0x10) {
            if (memcmp(&hex[j], &hex[j - 0x10], 0x10))
               break;
         }

         if (j > i) {
            fprintf(fp, "*\n");
            i = j - 1;
            continue;
         }
      }

      if ((i & 0xF) == 0)
         fprintf(fp, "%06X  ", i);

      fprintf(fp, "%02X ", hex[i]);
      if ((i & 0xF) == 0xF && with_strings) {
         fprintf(fp, " | ");
         for (unsigned j = i & ~0xF; j <= i; ++j) {
            uint8_t c = hex[j];
            fputc((c < 32 || c > 128) ? '.' : c, fp);
         }
      }

      if ((i & 0xF) == 0xF)
         fprintf(fp, "\n");
   }

   fprintf(fp, "\n");
}

#endif
