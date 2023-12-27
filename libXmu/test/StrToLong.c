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

/* Test code for functions in src/StrToLong.c */
#include "config.h"
#include <X11/Xmu/Converters.h>
#include <X11/Xmd.h>
#include <glib.h>

struct TestData {
    const char *name;
    long value;
};

static const struct TestData data[] = {
    { "0", 0 },
    { "12345678", 12345678 },
    { "2147483647", 2147483647 },
    { "-2147483647", -2147483647 },
#ifdef LONG64
    { "9223372036854775807", 9223372036854775807L },
    { "-9223372036854775807", -9223372036854775807L }
#endif
};
#define DATA_ENTRIES (sizeof(data) / sizeof(data[0]))

static int warning_count;

static void
xt_warning_handler(String message)
{
    g_test_message("Caught warning: %s", message ? message : "<NULL>");
    warning_count++;
}



static void
test_XmuCvtStringToLong(void)
{
    XrmValue from, to, args;
    Cardinal nargs = 0;

    char namebuf[32];

    for (unsigned int i = 0; i < DATA_ENTRIES; i++) {
        g_test_message("StringToLong(%s)", data[i].name);

        strncpy(namebuf, data[i].name, sizeof(namebuf) - 1);
        namebuf[sizeof(namebuf) - 1] = 0;
        from.addr = namebuf;
        from.size = sizeof(char *);
        XmuCvtStringToLong(NULL, &nargs, &from, &to);
        g_assert_cmpint(*(long *)to.addr, ==, data[i].value);
        g_assert_cmpint(to.size, ==, sizeof(long));
    }

    /* Verify warning is issued for unused args */
    warning_count = 0;
    nargs = 1;
    g_test_message("StringToLong with extra args");
    XmuCvtStringToLong(&args, &nargs, &from, &to);
    g_assert_cmpint(warning_count, >, 0);

    /* Verify warning issued for non-numeric string */
    warning_count = 0;
    from.addr = (char *) "DoesNotExist";
    nargs = 0;
    g_test_message("StringToLong(%s)", from.addr);
    XmuCvtStringToLong(NULL, &nargs, &from, &to);
    g_assert_cmpint(warning_count, >, 0);

    /* Verify warning issued for empty string */
    warning_count = 0;
    from.addr = (char *) "";
    nargs = 0;
    g_test_message("StringToLong(%s)", from.addr);
    XmuCvtStringToLong(NULL, &nargs, &from, &to);
    g_assert_cmpint(warning_count, >, 0);
}

static void
test_XmuCvtLongToString(void)
{
    XrmValue from, to, args;
    long value;
    Cardinal nargs = 0;
    Boolean ret;
    char namebuf[32];


    for (unsigned int i = 0; i < DATA_ENTRIES; i++) {
        g_test_message("LongToString(%ld)", data[i].value);

        value = data[i].value;
        from.addr = (XPointer) &value;
        from.size = sizeof(long *);

        /* First test without providing a buffer to copy the string into */
        to.addr = NULL;
        to.size = 0;
        ret = XmuCvtLongToString(NULL, NULL, &nargs, &from, &to, NULL);
        g_assert_cmpint(ret, ==, True);
        g_assert_cmpstr(to.addr, ==, data[i].name);
        g_assert_cmpint(to.size, ==, sizeof(char *));

        /* Then test with a buffer that's too small to copy the string into */
        to.addr = namebuf;
        to.size = 1;
        ret = XmuCvtLongToString(NULL, NULL, &nargs, &from, &to, NULL);
        g_assert_cmpint(ret, ==, False);
        g_assert_cmpint(to.size, ==, strlen(data[i].name) + 1);

        /* Then test with a buffer that's big enough to copy the string into */
        to.addr = namebuf;
        to.size = sizeof(namebuf);
        ret = XmuCvtLongToString(NULL, NULL, &nargs, &from, &to, NULL);
        g_assert_cmpint(ret, ==, True);
        g_assert_cmpstr(to.addr, ==, data[i].name);
        g_assert_cmpint(to.size, ==, sizeof(char *));
    }

    /* Verify warning is issued for unused args */
    warning_count = 0;
    nargs = 1;
    g_test_message("LongToString with extra args");
    XmuCvtLongToString(NULL, &args, &nargs, &from, &to, NULL);
    g_assert_cmpint(warning_count, >, 0);
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    XtSetWarningHandler(xt_warning_handler);

    g_test_add_func("/StrToLong/XmuCvtStringToLong",
                    test_XmuCvtStringToLong);
    g_test_add_func("/StrToLong/XmuCvtLongToString",
                    test_XmuCvtLongToString);


    return g_test_run();
}
