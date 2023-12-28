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

/* Test code for functions in src/StrToGrav.c */
#include "config.h"
#include <X11/Xmu/Converters.h>
#include <X11/Xmu/CharSet.h>
#include <glib.h>
#include <setjmp.h>

struct TestData {
    const char *name;
    int value;
    Bool dupvalue; /* value maps to a different string already */
};

static const struct TestData data[] = {
    { XtEForget,        ForgetGravity,          False },
    { XtENorthWest,     NorthWestGravity,       False },
    { XtENorth,         NorthGravity,           False },
    { XtENorthEast,     NorthEastGravity,       False },
    { XtEWest,          WestGravity,            False },
    { XtECenter,        CenterGravity,          False },
    { XtEEast,          EastGravity,            False },
    { XtESouthWest,     SouthWestGravity,       False },
    { XtESouth,         SouthGravity,           False },
    { XtESouthEast,     SouthEastGravity,       False },
    { XtEStatic,        StaticGravity,          False },
    { XtEUnmap,         UnmapGravity,           True },
    { XtEleft,          WestGravity,            True },
    { XtEtop,           NorthGravity,           True },
    { XtEright,         EastGravity,            True },
    { XtEbottom,        SouthGravity,           True }
};
#define DATA_ENTRIES (sizeof(data) / sizeof(data[0]))

static int warning_count;

static void
xt_warning_handler(String message)
{
    g_test_message("Caught warning: %s", message ? message : "<NULL>");
    warning_count++;
}

/* Environment saved by setjmp() */
static jmp_buf jmp_env;

static void _X_NORETURN
xt_error_handler(String message)
{
    g_test_message("Caught error: %s", message ? message : "<NULL>");
    warning_count++;

    /* Avoid exit() in XtErrorMsg() */
    longjmp(jmp_env, 1);
}


static void
test_XmuCvtStringToGravity(void)
{
    XrmValue from, to, args;
    Cardinal nargs = 0;

    char namebuf[16];

    for (unsigned int i = 0; i < DATA_ENTRIES; i++) {
        g_test_message("StringToGravity(%s)", data[i].name);

        strncpy(namebuf, data[i].name, sizeof(namebuf) - 1);
        namebuf[sizeof(namebuf) - 1] = 0;
        from.addr = namebuf;
        from.size = sizeof(char *);
        XmuCvtStringToGravity(NULL, &nargs, &from, &to);
        g_assert_cmpint(*(int *)to.addr, ==, data[i].value);
        g_assert_cmpint(to.size, ==, sizeof(int));


        XmuNCopyISOLatin1Uppered(namebuf, data[i].name, sizeof(namebuf));
        from.addr = namebuf;
        from.size = sizeof(char *);
        XmuCvtStringToGravity(NULL, &nargs, &from, &to);
        g_assert_cmpint(*(int *)to.addr, ==, data[i].value);
        g_assert_cmpint(to.size, ==, sizeof(int));
    }

    /* Verify warning issued for unused args */
    warning_count = 0;
    nargs = 1;
    g_test_message("StringToGravity with extra args");
    XmuCvtStringToGravity(&args, &nargs, &from, &to);
    g_assert_cmpint(warning_count, >, 0);

    /* Verify warning issued for unknown string */
    warning_count = 0;
    from.addr = (char *) "DoesNotExist";
    nargs = 0;
    g_test_message("StringToGravity(%s)", from.addr);
    XmuCvtStringToGravity(NULL, &nargs, &from, &to);
    g_assert_cmpint(warning_count, >, 0);
}

static void
test_XmuCvtGravityToString(void)
{
    XrmValue from, to;
    int value;
    Cardinal nargs = 0;
    Boolean ret;
    char namebuf[16];


    for (unsigned int i = 0; i < DATA_ENTRIES; i++) {
        if (data[i].dupvalue == True) {
            /* skip values that map to different names */
            continue;
        }

        g_test_message("GravityToString(%d)", data[i].value);

        value = data[i].value;
        from.addr = (XPointer) &value;
        from.size = sizeof(int *);

        /* First test without providing a buffer to copy the string into */
        to.addr = NULL;
        to.size = 0;
        ret = XmuCvtGravityToString(NULL, NULL, &nargs, &from, &to, NULL);
        g_assert_cmpint(ret, ==, True);
        g_assert_cmpstr(to.addr, ==, data[i].name);
        /* Unlike StrtoBS, this always returns string length in to.size */
        g_assert_cmpint(to.size, ==, strlen(data[i].name) + 1);

        /* Then test with a buffer that's too small to copy the string into */
        to.addr = namebuf;
        to.size = 4;
        ret = XmuCvtGravityToString(NULL, NULL, &nargs, &from, &to, NULL);
        g_assert_cmpint(ret, ==, False);
        g_assert_cmpint(to.size, ==, strlen(data[i].name) + 1);

        /* Then test with a buffer that's big enough to copy the string into */
        to.addr = namebuf;
        to.size = sizeof(namebuf);
        ret = XmuCvtGravityToString(NULL, NULL, &nargs, &from, &to, NULL);
        g_assert_cmpint(ret, ==, True);
        g_assert_cmpstr(to.addr, ==, data[i].name);
        g_assert_cmpint(to.size, ==, strlen(data[i].name) + 1);
    }

    /* Verify warning and return of False for invalid value */
    warning_count = 0;
    value = 1984;
    from.addr = (XPointer) &value;
    g_test_message("GravityToString(%d)", value);
    if (setjmp(jmp_env) == 0) {
        ret = XmuCvtGravityToString(NULL, NULL, &nargs, &from, &to, NULL);
        g_assert_cmpint(ret, ==, False);
    } else {
        /* We jumped here from error handler as expected. */
    }
    g_assert_cmpint(warning_count, >, 0);
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    XtSetWarningHandler(xt_warning_handler);
    XtSetErrorHandler(xt_error_handler);

    g_test_add_func("/StrToGrav/XmuCvtStringToGravity",
                    test_XmuCvtStringToGravity);
    g_test_add_func("/StrToGrav/XmuCvtGravityToString",
                    test_XmuCvtGravityToString);

    return g_test_run();
}
