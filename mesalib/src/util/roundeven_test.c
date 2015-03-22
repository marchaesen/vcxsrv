/*
 * Copyright © 2015 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "macros.h"
#include "rounding.h"

int main(int argc, char *argv[])
{
   const struct {
      float input, expected;
   } float_data[] = {
      { 0.0,                  0.0 },
      { nextafterf(0.5, 0.0), 0.0 },
      { 0.5,                  0.0 },
      { nextafterf(0.5, 1.0), 1.0 },
      { 1.0,                  1.0 },
      { nextafterf(1.5, 1.0), 1.0 },
      { 1.5,                  2.0 },
      { nextafterf(1.5, 2.0), 2.0 },
      { 2.0,                  2.0 },
      { nextafterf(2.5, 2.0), 2.0 },
      { 2.5,                  2.0 },
      { nextafterf(2.5, 3.0), 3.0 },
   };

   const struct {
      double input, expected;
   } double_data[] = {
      { 0.0,                 0.0 },
      { nextafter(0.5, 0.0), 0.0 },
      { 0.5,                 0.0 },
      { nextafter(0.5, 1.0), 1.0 },
      { 1.0,                 1.0 },
      { nextafter(1.5, 1.0), 1.0 },
      { 1.5,                 2.0 },
      { nextafter(1.5, 2.0), 2.0 },
      { 2.0,                 2.0 },
      { nextafter(2.5, 2.0), 2.0 },
      { 2.5,                 2.0 },
      { nextafter(2.5, 3.0), 3.0 },
   };

   bool failed = false;
   int i;

   for (i = 0; i < ARRAY_SIZE(float_data); i++) {
      float output = _mesa_roundevenf(float_data[i].input);
      if (memcmp(&float_data[i].expected, &output, sizeof(float))) {
         fprintf(stderr, "%d float: expected %f (%a) from "
                         "_mesa_roundevenf(%f (%a)) but got %f (%a)\n",
                 i,
                 float_data[i].expected,
                 float_data[i].expected,
                 float_data[i].input,
                 float_data[i].input,
                 output,
                 output);
         failed = true;
      }
   }

   /* Test negated values */
   for (i = 0; i < ARRAY_SIZE(float_data); i++) {
      float output = _mesa_roundevenf(-float_data[i].input);
      float negated_expected = -float_data[i].expected;
      if (memcmp(&negated_expected, &output, sizeof(float))) {
         fprintf(stderr, "%d float: expected %f (%a) from "
                         "_mesa_roundevenf(%f (%a)) but got %f (%a)\n",
                 i,
                 negated_expected,
                 negated_expected,
                 -float_data[i].input,
                 -float_data[i].input,
                 output,
                 output);
         failed = true;
      }
   }

   for (i = 0; i < ARRAY_SIZE(double_data); i++) {
      double output = _mesa_roundeven(double_data[i].input);
      if (memcmp(&double_data[i].expected, &output, sizeof(double))) {
         fprintf(stderr, "%d double: expected %f (%a) from "
                         "_mesa_roundeven(%f (%a)) but got %f (%a)\n",
                 i,
                 double_data[i].expected,
                 double_data[i].expected,
                 double_data[i].input,
                 double_data[i].input,
                 output,
                 output);
         failed = true;
      }
   }

   /* Test negated values */
   for (i = 0; i < ARRAY_SIZE(double_data); i++) {
      double output = _mesa_roundeven(-double_data[i].input);
      double negated_expected = -double_data[i].expected;
      if (memcmp(&negated_expected, &output, sizeof(double))) {
         fprintf(stderr, "%d double: expected %f (%a) from "
                         "_mesa_roundeven(%f (%a)) but got %f (%a)\n",
                 i,
                 negated_expected,
                 negated_expected,
                 -double_data[i].input,
                 -double_data[i].input,
                 output,
                 output);
         failed = true;
      }
   }

   return failed;
}
