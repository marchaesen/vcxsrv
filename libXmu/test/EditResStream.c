/*
 * Copyright (c) 2022, 2023, Oracle and/or its affiliates.
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

/* Test code for ProtocolStream Get/Put functions in src/EditResCom.c */
#include "config.h"
#include <X11/Xmu/EditresP.h>
#include <glib.h>

static const char *test_string = "\tIt was a dark and stormy night...\n";

#define FillBuffer(d, v) memset(d, v, sizeof(d))

static void
test_EditResStream(void)
{
    ProtocolStream ps = { 0, 0, NULL, NULL, NULL };
    unsigned char c;
    unsigned short s;
    unsigned long l;
    Bool res;
    char *str;
    unsigned long ids[] = { 1, 10, 0xbabe, 0xbabeface, 0xffffffff };
    WidgetInfo i = {
        .num_widgets = sizeof(ids) / sizeof(ids[0]),
        .ids = ids,
        .real_widget = 0
    };
    WidgetInfo out = { 0, NULL, 0 };

    _XEditResResetStream(&ps);

    _XEditResPut8(&ps, 8);
    _XEditResPut16(&ps, 16);
    _XEditResPut16(&ps, 0xface);
    _XEditResPut32(&ps, 32);
    _XEditResPut32(&ps, 0xbabeface);
    _XEditResPutString8(&ps, test_string);
    _XEditResPutWidgetInfo(&ps, &i);

    /* current is a pointer to the next byte to read from or write to,
       so we need to reset it to the beginning to read the data we wrote */
    ps.current = ps.top;

    res = _XEditResGet8(&ps, &c);
    g_assert_cmpint(res, ==, True);
    g_assert_cmpint(c, ==, 8);

    res = _XEditResGet16(&ps, &s);
    g_assert_cmpint(res, ==, True);
    g_assert_cmpint(s, ==, 16);

    res = _XEditResGet16(&ps, &s);
    g_assert_cmpint(res, ==, True);
    g_assert_cmpint(s, ==, 0xface);

    /* set the full value so we can make sure that in 64-bit mode we
       write to the full long value, not just 32-bits of it. */
    memset(&l, 0x0f, sizeof(l));
    res = _XEditResGet32(&ps, &l);
    g_assert_cmpint(res, ==, True);
    g_assert_cmpint(l, ==, 32);

    memset(&l, 0x0f, sizeof(l));
    res = _XEditResGet32(&ps, &l);
    g_assert_cmpint(res, ==, True);
    g_assert_cmpint(l, ==, 0xbabeface);

    res = _XEditResGetString8(&ps, &str);
    g_assert_cmpint(res, ==, True);
    g_assert_cmpstr(str, ==, test_string);
    XtFree(str);
    str = NULL;

    res = _XEditResGetWidgetInfo(&ps, &out);
    g_assert_cmpint(res, ==, True);
    g_assert_cmpmem(ids, sizeof(ids), out.ids, out.num_widgets * sizeof(long));
    XtFree((char *) out.ids);
    out.ids = NULL;
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    g_test_add_func("/EditResCom/ProtocolStream",
                    test_EditResStream);

    return g_test_run();
}
