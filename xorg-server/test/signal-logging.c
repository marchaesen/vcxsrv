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
#include <unistd.h>
#include "assert.h"
#include "misc.h"

struct number_format_test {
    uint64_t number;
    char string[21];
    char hex_string[17];
};

struct signed_number_format_test {
    int64_t number;
    char string[21];
};

static Bool
check_signed_number_format_test(const struct signed_number_format_test *test)
{
    char string[21];

    FormatInt64(test->number, string);
    if(strncmp(string, test->string, 21) != 0) {
        fprintf(stderr, "Failed to convert %jd to decimal string (%s vs %s)\n",
                test->number, test->string, string);
        return FALSE;
    }

    return TRUE;
}

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

static void
number_formatting(void)
{
    int i;
    struct number_format_test unsigned_tests[] = {
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

    struct signed_number_format_test signed_tests[] = {
        { /* Zero */
            0,
            "0",
        },
        { /* Single digit number */
            5,
            "5",
        },
        { /* Two digit decimal number */
            12,
            "12",
        },
        { /* Two digit hex number */
            37,
            "37",
        },
        { /* Large < 32 bit number */
            0xC90B2,
            "823474",
        },
        { /* Large > 32 bit number */
            0x15D027BF211B37A,
            "98237498237498234",
        },
        { /* Maximum 64-bit signed number */
            0x7FFFFFFFFFFFFFFF,
            "9223372036854775807",
        },
        { /* Single digit number */
            -1,
            "-1",
        },
        { /* Two digit decimal number */
            -12,
            "-12",
        },
        { /* Large < 32 bit number */
            -0xC90B2,
            "-823474",
        },
        { /* Large > 32 bit number */
            -0x15D027BF211B37A,
            "-98237498237498234",
        },
        { /* Maximum 64-bit number */
            -0x7FFFFFFFFFFFFFFF,
            "-9223372036854775807",
        },
    };

    for (i = 0; i < sizeof(unsigned_tests) / sizeof(unsigned_tests[0]); i++)
        assert(check_number_format_test(unsigned_tests + i));

    for (i = 0; i < sizeof(unsigned_tests) / sizeof(signed_tests[0]); i++)
        assert(check_signed_number_format_test(signed_tests + i));
}

static void logging_format(void)
{
    const char *log_file_path = "/tmp/Xorg-logging-test.log";
    const char *str = "%s %d %u %% %p %i";
    char buf[1024];
    int i;
    unsigned int ui;
    FILE *f;
    char read_buf[2048];
    char *logmsg;
    uintptr_t ptr;

    /* set up buf to contain ".....end" */
    memset(buf, '.', sizeof(buf));
    strcpy(&buf[sizeof(buf) - 4], "end");

    LogInit(log_file_path, NULL);
    assert(f = fopen(log_file_path, "r"));

#define read_log_msg(msg) \
    fgets(read_buf, sizeof(read_buf), f); \
    msg = strchr(read_buf, ']') + 2; /* advance past [time.stamp] */

    /* boring test message */
    LogMessageVerbSigSafe(X_ERROR, -1, "test message\n");
    read_log_msg(logmsg);
    assert(strcmp(logmsg, "(EE) test message\n") == 0);

    /* long buf is truncated to "....en\n" */
#pragma GCC diagnostic ignored "-Wformat-security"
    LogMessageVerbSigSafe(X_ERROR, -1, buf);
#pragma GCC diagnostic pop "-Wformat-security"
    read_log_msg(logmsg);
    assert(strcmp(&logmsg[strlen(logmsg) - 3], "en\n") == 0);

    /* same thing, this time as string substitution */
    LogMessageVerbSigSafe(X_ERROR, -1, "%s", buf);
    read_log_msg(logmsg);
    assert(strcmp(&logmsg[strlen(logmsg) - 3], "en\n") == 0);

    /* strings containing placeholders should just work */
    LogMessageVerbSigSafe(X_ERROR, -1, "%s\n", str);
    read_log_msg(logmsg);
    assert(strcmp(logmsg, "(EE) %s %d %u %% %p %i\n") == 0);

    /* string substitution */
    LogMessageVerbSigSafe(X_ERROR, -1, "%s\n", "substituted string");
    read_log_msg(logmsg);
    assert(strcmp(logmsg, "(EE) substituted string\n") == 0);

    /* number substitution */
    ui = 0;
    do {
        char expected[30];
        sprintf(expected, "(EE) %u\n", ui);
        LogMessageVerbSigSafe(X_ERROR, -1, "%u\n", ui);
        read_log_msg(logmsg);
        assert(strcmp(logmsg, expected) == 0);
        if (ui == 0)
            ui = 1;
        else
            ui <<= 1;
    } while(ui);

    /* signed number substitution */
    i = 0;
    do {
        char expected[30];
        sprintf(expected, "(EE) %d\n", i);
        LogMessageVerbSigSafe(X_ERROR, -1, "%d\n", i);
        read_log_msg(logmsg);
        assert(strcmp(logmsg, expected) == 0);


        sprintf(expected, "(EE) %d\n", i | INT_MIN);
        LogMessageVerbSigSafe(X_ERROR, -1, "%d\n", i | INT_MIN);
        read_log_msg(logmsg);
        assert(strcmp(logmsg, expected) == 0);

        if (i == 0)
            i = 1;
        else
            i <<= 1;
    } while(i > INT_MIN);

    /* hex number substitution */
    ui = 0;
    do {
        char expected[30];
        sprintf(expected, "(EE) %x\n", ui);
        LogMessageVerbSigSafe(X_ERROR, -1, "%x\n", ui);
        read_log_msg(logmsg);
        assert(strcmp(logmsg, expected) == 0);
        if (ui == 0)
            ui = 1;
        else
            ui <<= 1;
    } while(ui);

    /* pointer substitution */
    /* we print a null-pointer differently to printf */
    LogMessageVerbSigSafe(X_ERROR, -1, "%p\n", NULL);
    read_log_msg(logmsg);
    assert(strcmp(logmsg, "(EE) 0x0\n") == 0);

    ptr = 1;
    do {
        char expected[30];
        sprintf(expected, "(EE) %p\n", (void*)ptr);
        LogMessageVerbSigSafe(X_ERROR, -1, "%p\n", (void*)ptr);
        read_log_msg(logmsg);
        assert(strcmp(logmsg, expected) == 0);
        ptr <<= 1;
    } while(ptr);

    LogClose(EXIT_NO_ERROR);
    unlink(log_file_path);

#undef read_log_msg
}

int
main(int argc, char **argv)
{
    number_formatting();
    logging_format();

    return 0;
}
