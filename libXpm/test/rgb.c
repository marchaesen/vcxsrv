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

#include "config.h"

#include "../src/rgb.c"
#include <glib.h>

/*
 * xpmReadRgbNames - reads a rgb text file
 */

struct rgbData {
    int r, g, b;
    const char *name;
};

/* Changes here must match those in rgb.txt file */
static const struct rgbData testdata[] = {
    { 255, 255, 255, "white" },
    {   0,   0,   0, "black" },
    { 255,   0,   0, "red" },
    {   0, 255,   0, "green" },
    {   0,   0, 255, "blue" },
    {   0,  50,  98, "berkeleyblue" }, /* names get lowercased */
    { 253, 181,  21, "californiagold" }
};
#define NUM_RGB (sizeof(testdata) / sizeof(testdata[0]))

static void
test_xpmReadRgbNames(void)
{
    const gchar *filename;
    xpmRgbName rgbn[MAX_RGBNAMES];
    int rgbn_max;

    /* Verify NULL is returned if file can't be read */
    rgbn_max = xpmReadRgbNames("non-existent-file.txt", rgbn);
    g_assert_cmpint(rgbn_max, ==, 0);

    /* Verify our test file is read properly & contains expected data */
    filename = g_test_get_filename(G_TEST_DIST, "rgb.txt", NULL);
    rgbn_max = xpmReadRgbNames(filename, rgbn);
    g_assert_cmpint(rgbn_max, ==, NUM_RGB);

    for (unsigned int i = 0; i < NUM_RGB; i++) {
        int r = testdata[i].r * 257;
        int g = testdata[i].g * 257;
        int b = testdata[i].b * 257;
        char *name = xpmGetRgbName(rgbn, rgbn_max, r, g, b);

        g_assert_cmpstr(name, ==, testdata[i].name);
    }

    g_assert_null(xpmGetRgbName(rgbn, rgbn_max, 11, 11, 11));

    xpmFreeRgbNames(rgbn, rgbn_max);
}


int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    g_test_add_func("/rgb/xpmReadRgbNames",
                    test_xpmReadRgbNames);

    return g_test_run();
}
