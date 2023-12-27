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

/* Test code for XmuReadBitmapData functions in src/RdBitF.c */
#include "config.h"
#include <X11/Xmu/Drawing.h>
#include <glib.h>
#include <string.h>

struct BitmapData {
    unsigned int        width;
    unsigned int        height;
    unsigned char *     datap;
    int                 xhot;
    int                 yhot;
};

#include "bitmaps/plaid"

static const struct BitmapData plaid_expected = {
    plaid_width, plaid_height, (unsigned char *) plaid_bits,
    plaid_x_hot, plaid_y_hot
};

#include "bitmaps/star"

static const struct BitmapData star_expected = {
    star_width, star_height, (unsigned char *) star_bits,
    star_x_hot, star_y_hot
};

#include "bitmaps/xlogo64"
#define xlogo64_x_hot -1
#define xlogo64_y_hot -1

static const struct BitmapData xlogo64_expected = {
    xlogo64_width, xlogo64_height, xlogo64_bits,
    xlogo64_x_hot, xlogo64_y_hot
};

struct TestData {
    const char *filename;
    const struct BitmapData *data;
};

static struct TestData testdata[] = {
    { "plaid",		&plaid_expected },
    { "star", 		&star_expected },
    { "xlogo64", 	&xlogo64_expected },
};

#define testcount (sizeof(testdata) / sizeof(testdata[0]))

static void
CompareBitmapData(const struct BitmapData *readin,
                  const struct BitmapData *expected)
{
    size_t bytes_per_line;
    size_t total_bytes;

    g_assert_cmpuint(readin->width, ==, expected->width);
    g_assert_cmpuint(readin->height, ==, expected->height);
    g_assert_cmpint(readin->xhot, ==, expected->xhot);
    g_assert_cmpint(readin->yhot, ==, expected->yhot);

    bytes_per_line = (readin->width + 7) / 8;
    total_bytes = bytes_per_line * readin->height;
    g_assert_cmpmem(readin->datap, total_bytes, expected->datap, total_bytes);
}


static void
test_ReadBitmapData(void)
{
    for (unsigned int i = 0; i < testcount; i++) {
        const gchar *filename;
        FILE *fp;
        int status;
        struct BitmapData readin;

        filename = g_test_get_filename(G_TEST_DIST, "bitmaps",
                                       testdata[i].filename, NULL);

        g_test_message("Testing XmuReadBitmapDataFromFile(\"%s\")", filename);

        status = XmuReadBitmapDataFromFile(filename,
                                           &readin.width, &readin.height,
                                           &readin.datap,
                                           &readin.xhot, &readin.yhot);
        g_assert_cmpint(status, ==, Success);
        CompareBitmapData(&readin, testdata[i].data);


        g_test_message("Testing XmuReadBitmapData on \"%s\"", filename);
        fp = fopen(filename, "r");
        g_assert_nonnull(fp);

        status = XmuReadBitmapData(fp,
                                   &readin.width, &readin.height,
                                   &readin.datap,
                                   &readin.xhot, &readin.yhot);
        g_assert_cmpint(status, ==, Success);
        CompareBitmapData(&readin, testdata[i].data);
        fclose(fp);
    }
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    g_test_add_func("/RdBitF/ReadBitmapData",
                    test_ReadBitmapData);

    return g_test_run();
}
