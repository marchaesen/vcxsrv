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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "TestAllFiles.h"

#ifndef O_CLOEXEC
# define O_CLOEXEC 0
#endif

#ifndef g_assert_no_errno /* defined in glib 2.66 & later */
#define g_assert_no_errno(n) g_assert_cmpint(n, >=, 0)
#endif

/*
 * XpmReadFileToXpmImage - Read XPM files without requiring an X Display
 *
 * Todo: actually check the returned image/info.
  */
static int
TestReadFileToXpmImage(const gchar *filepath)
{
    XpmImage image;
    XpmInfo info;
    int status;

    status = XpmReadFileToXpmImage(filepath, &image, &info);

    if (status == XpmSuccess) {
        XpmFreeXpmImage(&image);
        XpmFreeXpmInfo(&info);
    }

    return status;
}

static void
test_XpmReadFileToXpmImage(void)
{
    int status;

    status = TestReadFileToXpmImage("no-such-file.xpm");
    g_assert_cmpint(status, ==, XpmOpenFailed);

    TestAllNormalFiles("good", XpmSuccess, TestReadFileToXpmImage);
    TestAllNormalFiles("invalid", XpmFileInvalid, TestReadFileToXpmImage);
    TestAllNormalFiles("no-mem", XpmNoMemory, TestReadFileToXpmImage);
    /* XpmReadFileToXpmImage supports compressed files */
    TestAllCompressedFiles("good", XpmSuccess, TestReadFileToXpmImage);
    TestAllCompressedFiles("invalid", XpmFileInvalid, TestReadFileToXpmImage);
    TestAllCompressedFiles("no-mem", XpmNoMemory, TestReadFileToXpmImage);
}

/*
 * XpmReadFileToData - wrapper around XpmReadFileToXpmImage that
 * converts the image into a list of strings.
 *
 * Todo: actually check the returned data.
 */
static int
TestReadFileToData(const gchar *filepath)
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
test_XpmReadFileToData(void)
{
    int status;

    status = TestReadFileToData("no-such-file.xpm");
    g_assert_cmpint(status, ==, XpmOpenFailed);

    TestAllNormalFiles("good", XpmSuccess, TestReadFileToData);
    TestAllNormalFiles("invalid", XpmFileInvalid, TestReadFileToData);
    TestAllNormalFiles("no-mem", XpmNoMemory,  TestReadFileToData);
    /* XpmReadFileToData calls XpmReadFileToXpmImage so it
       supports compressed files */
    TestAllCompressedFiles("good", XpmSuccess, TestReadFileToData);
    TestAllCompressedFiles("invalid", XpmFileInvalid, TestReadFileToData);
    TestAllCompressedFiles("no-mem", XpmNoMemory, TestReadFileToData);
}


/*
 * XpmReadFileToBuffer - helper function that just reads the file
 * into memory and doesn't try to parse it.
 */
static int
TestReadFileToBuffer(const gchar *filepath)
{
    char *buffer = NULL;
    int status;

    status = XpmReadFileToBuffer(filepath, &buffer);

    if (status == XpmSuccess) {
        char readbuf[8192];
        char *b = buffer;
        int fd;
        ssize_t rd;

        g_assert_nonnull(buffer);

        /* Read file ourselves and verify the data matches */
        g_assert_no_errno(fd = open(filepath, O_RDONLY | O_CLOEXEC));
        while ((rd = read(fd, readbuf, sizeof(readbuf))) > 0) {
            g_assert_cmpmem(b, rd, readbuf, rd);
            b += rd;
        }
        /* Verify a nil terminator was added to the end */
        g_assert_cmpint(b[0], ==, '\0');
        g_assert_no_errno(close(fd));

        XpmFree(buffer);
    }

    return status;
}

static void
test_XpmReadFileToBuffer(void)
{
    int status;

    status = TestReadFileToBuffer("no-such-file.xpm");
    g_assert_cmpint(status, ==, XpmOpenFailed);

    TestAllNormalFiles("good", XpmSuccess, TestReadFileToBuffer);
    /* Since this test just reads the file from disk without parsing,
       we expect Success even from files with bad fields in. */
    TestAllNormalFiles("invalid", XpmSuccess, TestReadFileToBuffer);
    TestAllNormalFiles("no-mem", XpmSuccess, TestReadFileToBuffer);
    /* XpmReadFileToBuffer does not support compressed files */
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    g_test_add_func("/XpmRead/XpmReadFileToXpmImage",
                    test_XpmReadFileToXpmImage);
    g_test_add_func("/XpmRead/XpmReadFileToData",
                    test_XpmReadFileToData);
    g_test_add_func("/XpmRead/XpmReadFileToBuffer",
                    test_XpmReadFileToBuffer);

    return g_test_run();
}
