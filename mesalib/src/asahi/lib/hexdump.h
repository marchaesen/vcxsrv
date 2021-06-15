/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __HEXDUMP_H
#define __HEXDUMP_H

static void
hexdump(FILE *fp, const uint8_t *hex, size_t cnt, bool with_strings)
{
   for (unsigned i = 0; i < cnt; ++i) {
      if ((i & 0xF) == 0)
         fprintf(fp, "%06X  ", i);

      uint8_t v = hex[i];

      if (v == 0 && (i & 0xF) == 0) {
         /* Check if we're starting an aligned run of zeroes */
         unsigned zero_count = 0;

         for (unsigned j = i; j < cnt; ++j) {
            if (hex[j] == 0)
               zero_count++;
            else
               break;
         }

         if (zero_count >= 32) {
            fprintf(fp, "*\n");
            i += (zero_count & ~0xF) - 1;
            continue;
         }
      }

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
