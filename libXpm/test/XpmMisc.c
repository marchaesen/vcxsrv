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

/*
 * XpmAttributesSize - report size of XpmAttributes structure
 */

static void
test_XpmAttributesSize(void)
{
    int size = XpmAttributesSize();
    g_assert_cmpint(size, ==, sizeof(XpmAttributes));
}

/*
 * XpmGetErrorString - return string describing error code
 */

static void
test_XpmGetErrorString(void)
{
    const char *es;

#define TestErrorString(num, str) \
    es = XpmGetErrorString(num); \
    g_assert_nonnull(es); \
    g_assert_cmpstr(es, ==, str)

    TestErrorString(XpmColorError, "XpmColorError");
    TestErrorString(XpmSuccess, "XpmSuccess");
    TestErrorString(XpmOpenFailed, "XpmOpenFailed");
    TestErrorString(XpmFileInvalid, "XpmFileInvalid");
    TestErrorString(XpmNoMemory, "XpmNoMemory");
    TestErrorString(XpmColorFailed, "XpmColorFailed");
    TestErrorString(128, "Invalid XpmError");
    TestErrorString(-42, "Invalid XpmError");

#undef TestErrorString
}

/*
 * XpmLibraryVersion - report version of library
 */

static void
test_XpmLibraryVersion(void)
{
    int version = XpmLibraryVersion();
    g_assert_cmpint(version, ==, XpmIncludeVersion);
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    g_test_add_func("/XpmMisc/XpmAttributesSize",
                    test_XpmAttributesSize);
    g_test_add_func("/XpmMisc/XpmGetErrorString",
                    test_XpmGetErrorString);
    g_test_add_func("/XpmMisc/XpmLibraryVersion",
                    test_XpmLibraryVersion);

    return g_test_run();
}
