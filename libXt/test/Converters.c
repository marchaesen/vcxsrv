/*
 * Copyright (c) 2011, 2022, Oracle and/or its affiliates.
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

#include <X11/Intrinsic.h>
#include <glib.h>

/* Test for Solaris bug 4163152 XtCvtIntToPixmap() gets a SIGBUS in 64-bit
   Fixed by libXt commit 16d9941f3aa38dde115cbff639e131761c1b36d0
 */
static void test_XtCvtIntToPixmap(void)
{
    Display         *display = NULL; /* not actually used */
    Boolean         status;
    XrmValue        args[2];
    Cardinal        num_args;
    XrmValue        fromVal;
    XrmValue        toVal;
    Pixmap          res;
    XtPointer       *closure_ret = NULL;
    int             num[2];


    XtToolkitInitialize();

    num[0] = 7;
    num[1] = -1;

    num_args = 0;
    fromVal.addr = (XtPointer) num;
    fromVal.size = sizeof(int);
    toVal.addr = (XtPointer) &res;
    toVal.size = sizeof(Pixmap);

    status = XtCvtIntToPixmap(display, &args[0], &num_args,
			      &fromVal, &toVal, closure_ret);

    g_assert_cmpint(status, ==, True);
    g_assert_cmpint(res, ==, num[0]);


    num[0] = -1;
    num[1] = 7;

    num_args = 0;
    fromVal.addr = (XtPointer) (&num[1]);
    fromVal.size = sizeof(int);
    toVal.addr = (XtPointer) &res;
    toVal.size = sizeof(Pixmap);

    status = XtCvtIntToPixmap(display, &args[0], &num_args,
			      &fromVal, &toVal, closure_ret);

    g_assert_cmpint(status, ==, True);
    g_assert_cmpint(res, ==, num[1]);
}

int main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base("https://gitlab.freedesktop.org/xorg/lib/libxt/-/issues/");

    g_test_add_func("/Converters/XtCvtIntToPixmap", test_XtCvtIntToPixmap);

    return g_test_run();
}
