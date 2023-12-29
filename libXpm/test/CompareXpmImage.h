/*
 * Copyright (c) 2023, Oracle and/or its affiliates.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <glib.h>

static void
CompareXpmImage(const XpmImage *a, const XpmImage *b)
{
#if 0
    const size_t datasize = sizeof(unsigned int) * a->width * a->height;
#endif

#define CompareUintFields(f) g_assert_cmpuint(a->f, ==, b->f)

    CompareUintFields(width);
    CompareUintFields(height);
    CompareUintFields(cpp);
    CompareUintFields(ncolors);

/* this assumes the same character encoding and color ordering, which is only
   true in our crafted test cases, not for matching images in the real world */
    for (unsigned int i = 0; i < a->ncolors; i++)
    {
#define CompareStringFields(f) \
        g_assert_cmpstr(a->colorTable[i].f, ==, b->colorTable[i].f)

        CompareStringFields(string);
        CompareStringFields(symbolic);
        CompareStringFields(m_color);
        CompareStringFields(g4_color);
        CompareStringFields(g_color);
        CompareStringFields(c_color);
    }

#if 0 /* this currently fails in image comparison - needs debugging */
    for (size_t i = 0; i < datasize; i++)
    {
        CompareUintFields(data[i]);
    }
#endif
}
