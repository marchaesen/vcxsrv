//
// Copyright 2020 Serge Martin
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// Extract from Serge's printf clover code by airlied.

#include "u_printf.h"

size_t util_printf_next_spec_pos(const std::string &s, size_t pos)
{
   size_t next_tok, spec_pos;
   do {
      pos = s.find_first_of('%', pos);

      if (pos == std::string::npos)
         return -1;

      if (s[pos + 1] == '%') {
         pos += 2;
         continue;
      }

      next_tok = s.find_first_of('%', pos + 1);
      spec_pos = s.find_first_of("cdieEfFgGaAosuxXp", pos + 1);
      if (spec_pos != std::string::npos)
         if (spec_pos < next_tok)
            return spec_pos;

      pos++;
   } while (1);
}

size_t util_printf_next_spec_pos(const char *str, size_t pos)
{
   return util_printf_next_spec_pos(std::string(str), pos);
}
