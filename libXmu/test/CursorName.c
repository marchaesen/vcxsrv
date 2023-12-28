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

/* Test code for XmuCursorNameToIndex() in src/CursorName.c */
#include "config.h"
#include <X11/Xmu/CurUtil.h>
#include <X11/Xmu/CharSet.h>
#include <glib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_WHITESPACE(p)  while (isspace(*p)) p++

/* Looks up each entry from <X11/cursorfont.h> to verify value returned */
static void
test_CursorNameToIndex_goodnames(void)
{
    FILE *cursorfont;
    char line[256];
    int cursorschecked = 0;
    int cursorsexpected = 0;

    cursorfont = fopen("/usr/include/X11/cursorfont.h", "r");
    if (cursorfont == NULL) {
        g_test_skip("Could not open /usr/include/X11/cursorfont.h");
        return;
    }

    while (fgets(line, sizeof(line), cursorfont) != NULL) {
        char *p = line;

        /* skip lines that don't start with "#define" */
        if (strncmp(p, "#define", 7) != 0)
            continue;
        else
            p += 7;

        /* skip over whitespace after #define */
        SKIP_WHITESPACE(p);

        /* skip #define _X11_CURSORFONT_H_ */
        if (strncmp(p, "XC_", 3) != 0)
            continue;
        else
            p += 3;

        if (strncmp(p, "num_glyphs", 10) == 0) {
            /* Use #define XC_num_glyphs to record the number we expect */
            g_assert_cmpint(cursorsexpected, ==, 0);
            p += strlen("num_glyphs");
            SKIP_WHITESPACE(p);
            cursorsexpected = (int) strtol(p, NULL, 0) / 2;
            g_test_message("cursors expected = %d", cursorsexpected);
            continue;
        }
        else {
            /* Should be a cursor name then */
            char *name = p;
            int expected_id, returned_id;
            char upper_name[32];

            while (!isspace(*p))
                p++;
            *p++ = '\0';
            SKIP_WHITESPACE(p);
            expected_id = (int) strtol(p, NULL, 0);

            g_test_message("%s = %d", name, expected_id);

            returned_id = XmuCursorNameToIndex(name);
            g_assert_cmpint(returned_id, ==, expected_id);

            XmuNCopyISOLatin1Uppered(upper_name, name, sizeof(upper_name));
            returned_id = XmuCursorNameToIndex(upper_name);
            g_assert_cmpint(returned_id, ==, expected_id);

            cursorschecked++;
        }
    }

    fclose(cursorfont);

    g_assert_cmpint(cursorschecked, ==, cursorsexpected);
}

static void
test_CursorNameToIndex_badnames(void)
{
    const char *badnames[] = {
        "does-not-exist",
        "starts-with-a-good-name", /* starts with "star" */
        "num_glyphs",
        ""
    };
#define NUM_BAD_NAMES (sizeof(badnames) / sizeof(badnames[0]))

    for (unsigned int i = 0; i < NUM_BAD_NAMES; i++) {
        int returned_id = XmuCursorNameToIndex(badnames[i]);
        g_test_message("%s", badnames[i]);
        g_assert_cmpint(returned_id, ==, -1);
    }
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    g_test_add_func("/CursorName/XmuCursorNameToIndex/good-names",
                    test_CursorNameToIndex_goodnames);

    g_test_add_func("/CursorName/XmuCursorNameToIndex/bad-names",
                    test_CursorNameToIndex_badnames);

    return g_test_run();
}
