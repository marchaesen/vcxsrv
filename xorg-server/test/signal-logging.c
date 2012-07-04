/**
 * Copyright © 2012 Canonical, Ltd.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice (including the next
 *  paragraph) shall be included in all copies or substantial portions of the
 *  Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdint.h>
#include "assert.h"
#include "misc.h"

struct number_format_test {
    uint64_t number;
    char string[21];
    char hex_string[17];
};

static Bool
check_number_format_test(const struct number_format_test *test)
{
    char string[21];

    FormatUInt64(test->number, string);
    if(strncmp(string, test->string, 21) != 0) {
        fprintf(stderr, "Failed to convert %ju to decimal string (%s vs %s)\n",
                test->number, test->string, string);
        return FALSE;
    }
    FormatUInt64Hex(test->number, string);
    if(strncmp(string, test->hex_string, 17) != 0) {
        fprintf(stderr,
                "Failed to convert %ju to hexadecimal string (%s vs %s)\n",
                test->number, test->hex_string, string);
        return FALSE;
    }

    return TRUE;
}

static Bool
number_formatting(void)
{
    int i;
    struct number_format_test tests[] = {
        { /* Zero */
            0,
            "0",
            "0",
        },
        { /* Single digit number */
            5,
            "5",
            "5",
        },
        { /* Two digit decimal number */
            12,
            "12",
            "c",
        },
        { /* Two digit hex number */
            37,
            "37",
            "25",
        },
        { /* Large < 32 bit number */
            0xC90B2,
            "823474",
            "c90b2",
        },
        { /* Large > 32 bit number */
            0x15D027BF211B37A,
            "98237498237498234",
            "15d027bf211b37a",
        },
        { /* Maximum 64-bit number */
            0xFFFFFFFFFFFFFFFF,
            "18446744073709551615",
            "ffffffffffffffff",
        },
    };

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
        if (!check_number_format_test(tests + i))
            return FALSE;

    return TRUE;
}

int
main(int argc, char **argv)
{
    int ok = number_formatting();

    return ok ? 0 : 1;
}
