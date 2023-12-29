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

#include <X11/xpm.h>
#include <glib.h>

#include "TestAllFiles.h"


/*
 * XpmCreateXpmImageFromData - parse an XPM from data strings
 *
 * Todo:
 *  - actually check the returned info/image
 *  - check with data other than read from XPM files
 */
static int
TestCreateXpmImageFromData(const gchar *filepath)
{
    char **data = NULL;
    int status;

    status = XpmReadFileToData(filepath, &data);

    if (status == XpmSuccess) {
        XpmImage image;
        XpmInfo info;

        g_assert_nonnull(data);

        status = XpmCreateXpmImageFromData(data, &image, &info);
        g_assert_cmpint(status, ==, XpmSuccess);

        XpmFreeXpmImage(&image);
        XpmFreeXpmInfo(&info);
        XpmFree(data);
    }

    return status;
}

static void
test_XpmCreateXpmImageFromData(void)
{
    TestAllNormalFiles("good", XpmSuccess, TestCreateXpmImageFromData);
    TestAllNormalFiles("invalid", XpmFileInvalid, TestCreateXpmImageFromData);
    TestAllNormalFiles("no-mem", XpmNoMemory, TestCreateXpmImageFromData);
    /* XpmReadFileToData calls XpmReadFileToXpmImage so it
       supports compressed files */
    TestAllCompressedFiles("good", XpmSuccess, TestCreateXpmImageFromData);
    TestAllCompressedFiles("invalid", XpmFileInvalid, TestCreateXpmImageFromData);
    TestAllCompressedFiles("no-mem", XpmNoMemory, TestCreateXpmImageFromData);
}


/*
 * XpmCreateXpmImageFromBuffer - parse an XPM from data strings
 *
 * Todo:
 *  - actually check the returned info/image
 *  - check with data other than read from XPM files
 */
static int
TestCreateXpmImageFromBuffer(const gchar *filepath)
{
    char *buffer = NULL;
    XpmImage image;
    XpmInfo info;
    int status;

    status = XpmReadFileToBuffer(filepath, &buffer);
    g_assert_cmpint(status, ==, XpmSuccess);

    status = XpmCreateXpmImageFromBuffer(buffer, &image, &info);

    if (status == XpmSuccess) {
        XpmFreeXpmImage(&image);
        XpmFreeXpmInfo(&info);
    }

    XpmFree(buffer);

    return status;
}

static void
test_XpmCreateXpmImageFromBuffer(void)
{
    TestAllNormalFiles("good", XpmSuccess, TestCreateXpmImageFromBuffer);
    TestAllNormalFiles("invalid", XpmFileInvalid, TestCreateXpmImageFromBuffer);
    TestAllNormalFiles("no-mem", XpmNoMemory, TestCreateXpmImageFromBuffer);
    /* XpmReadFileToBuffer does not support compressed files */
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    g_test_add_func("/XpmCreate/XpmCreateXpmImageFromData",
                    test_XpmCreateXpmImageFromData);
    g_test_add_func("/XpmCreate/XpmCreateXpmImageFromBuffer",
                    test_XpmCreateXpmImageFromBuffer);

    return g_test_run();
}
