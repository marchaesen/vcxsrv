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
#include <glib/gstdio.h>

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "TestAllFiles.h"
#include "CompareXpmImage.h"

#ifndef O_CLOEXEC
# define O_CLOEXEC 0
#endif

#ifndef g_assert_no_errno /* defined in glib 2.66 & later */
#define g_assert_no_errno(n) g_assert_cmpint(n, >=, 0)
#endif

/*
 * Check if a filename ends in ".Z" or ".gz"
 */
static inline gboolean
is_compressed(const char *filepath)
{
    const char *ext = strrchr(filepath, '.');

    if ((ext != NULL) &&
        (((ext[1] == 'Z') && (ext[2] == 0)) ||
         ((ext[1] == 'g') && (ext[2] == 'z') && (ext[3] == 0)))) {
        return TRUE;
    }

    return FALSE;
}

/*
 * If a filename ends in ".Z" or ".gz", remove that extension to avoid
 * confusing libXpm into applying compression when not desired.
 */
static inline void
strip_compress_ext(char *filepath)
{
    char *ext = strrchr(filepath, '.');

    if ((ext != NULL) &&
        (((ext[1] == 'Z') && (ext[2] == 0)) ||
         ((ext[1] == 'g') && (ext[2] == 'z') && (ext[3] == 0)))) {
        *ext = '\0';
    }
}

/*
 * XpmWriteFileFromXpmImage - Write XPM files without requiring an X Display
  */
static void
test_WFFXI_helper(const gchar *newfilepath, XpmImage *imageA, XpmInfo *infoA)
{
    XpmImage imageB;
    XpmInfo infoB;
    int status;

    g_test_message("...writing %s", newfilepath);

    status = XpmWriteFileFromXpmImage(newfilepath, imageA, infoA);
    g_assert_cmpint(status, ==, XpmSuccess);

    if (is_compressed(newfilepath)) {
         /* Wait a moment for the compression command to finish writing,
          * since OpenWriteFile() does a double fork so we can't just wait
          * for the child command to exit.
          */
         usleep(10000);
    }

    status = XpmReadFileToXpmImage(newfilepath, &imageB, &infoB);
    g_assert_cmpint(status, ==, XpmSuccess);

    CompareXpmImage(imageA, &imageB);
    XpmFreeXpmImage(&imageB);
    XpmFreeXpmInfo(&infoB);

    status = remove(newfilepath);
    g_assert_no_errno(status);

}

static int
TestWriteFileFromXpmImage(const gchar *filepath)
{
    XpmImage imageA;
    XpmInfo infoA;
    int status;
    gchar *testdir, *filename, *newfilepath;
    GError *err = NULL;

#ifndef NO_ZPIPE
    gchar *cmpfilepath;
#endif

    status = XpmReadFileToXpmImage(filepath, &imageA, &infoA);
    g_assert_cmpint(status, ==, XpmSuccess);

    testdir = g_dir_make_tmp("XpmWrite-test-XXXXXX", &err);
    g_assert_no_error(err);

    filename = g_path_get_basename(filepath);
    strip_compress_ext(filename);
    newfilepath = g_build_filename(testdir, filename, NULL);

    test_WFFXI_helper(newfilepath, &imageA, &infoA);

#ifndef NO_ZPIPE
    cmpfilepath = g_strdup_printf("%s.gz", newfilepath);
    test_WFFXI_helper(cmpfilepath, &imageA, &infoA);
    g_free(cmpfilepath);

#ifdef XPM_PATH_COMPRESS
    cmpfilepath = g_strdup_printf("%s.Z", newfilepath);
    test_WFFXI_helper(cmpfilepath, &imageA, &infoA);
    g_free(cmpfilepath);
#endif
#endif

    XpmFreeXpmImage(&imageA);
    XpmFreeXpmInfo(&infoA);

    g_assert_no_errno(g_rmdir(testdir));

    g_free(newfilepath);
    g_free(filename);
    g_free(testdir);

    return status;
}

static void
test_XpmWriteFileFromXpmImage(void)
{
    /* Todo: verify trying to write to an unwritable file fails */

    TestAllNormalFiles("good", XpmSuccess, TestWriteFileFromXpmImage);
    /* XpmReadFileToXpmImage supports compressed files */
    TestAllCompressedFiles("good", XpmSuccess, TestWriteFileFromXpmImage);
}

/*
 * XpmWriteFileFromData - wrapper around XpmWriteFileFromXpmImage that
 * converts the image into a list of strings.
 */
static void
test_WFFXD_helper(const gchar *newfilepath, char **dataA)
{
    char **dataB;
    int status;

    g_test_message("...writing %s", newfilepath);

    status = XpmWriteFileFromData(newfilepath, dataA);
    g_assert_cmpint(status, ==, XpmSuccess);

    if (is_compressed(newfilepath)) {
         /* Wait a moment for the compression command to finish writing,
          * since OpenWriteFile() does a double fork so we can't just wait
          * for the child command to exit.
          */
         usleep(10000);
    }

    status = XpmReadFileToData(newfilepath, &dataB);
    g_assert_cmpint(status, ==, XpmSuccess);

    /* Todo: compare data fields */
    XpmFree(dataB);

    status = remove(newfilepath);
    g_assert_no_errno(status);

}

static int
TestWriteFileFromData(const gchar *filepath)
{
    char **data = NULL;
    int status;
    gchar *testdir, *filename, *newfilepath;
    GError *err = NULL;

#ifndef NO_ZPIPE
    gchar *cmpfilepath;
#endif

    status = XpmReadFileToData(filepath, &data);
    g_assert_cmpint(status, ==, XpmSuccess);

    testdir = g_dir_make_tmp("XpmWrite-test-XXXXXX", &err);
    g_assert_no_error(err);

    filename = g_path_get_basename(filepath);
    strip_compress_ext(filename);
    newfilepath = g_build_filename(testdir, filename, NULL);

    test_WFFXD_helper(newfilepath, data);

#ifndef NO_ZPIPE
    cmpfilepath = g_strdup_printf("%s.gz", newfilepath);
    test_WFFXD_helper(cmpfilepath, data);
    g_free(cmpfilepath);

#ifdef XPM_PATH_COMPRESS
    cmpfilepath = g_strdup_printf("%s.Z", newfilepath);
    test_WFFXD_helper(cmpfilepath, data);
    g_free(cmpfilepath);
#endif
#endif

    XpmFree(data);

    g_assert_no_errno(g_rmdir(testdir));

    g_free(newfilepath);
    g_free(filename);
    g_free(testdir);

    return status;
}

static void
test_XpmWriteFileFromData(void)
{
    /* Todo - verify trying to write to an unwritable file fails */

    TestAllNormalFiles("good", XpmSuccess, TestWriteFileFromData);
    /* XpmReadFileToData calls XpmReadFileToXpmImage so it
       supports compressed files */
    TestAllCompressedFiles("good", XpmSuccess, TestWriteFileFromData);
}

/*
 * XpmWriteFileFromBuffer - helper function to write files & read them back in
 * XpmWriteFileFromBuffer() does not support compressed files.
 */
static int
TestWriteFileFromBuffer(const gchar *filepath)
{
    char *buffer = NULL;
    gchar *testdir, *filename, *newfilepath;
    GError *err = NULL;
    int status;

    status = XpmReadFileToBuffer(filepath, &buffer);
    g_assert_cmpint(status, ==, XpmSuccess);
    g_assert_nonnull(buffer);

    testdir = g_dir_make_tmp("XpmWrite-test-XXXXXX", &err);
    g_assert_no_error(err);

    filename = g_path_get_basename(filepath);
    strip_compress_ext(filename);
    newfilepath = g_build_filename(testdir, filename, NULL);
    g_test_message("...writing %s", newfilepath);

    status = XpmWriteFileFromBuffer(newfilepath, buffer);
    g_assert_cmpint(status, ==, XpmSuccess);

    if (status == XpmSuccess) {
        char readbuf[8192];
        char *b = buffer;
        int fd;
        ssize_t rd;

        /* Read file ourselves and verify the data matches */
        g_assert_no_errno(fd = open(newfilepath, O_RDONLY | O_CLOEXEC));
        while ((rd = read(fd, readbuf, sizeof(readbuf))) > 0) {
            g_assert_cmpmem(b, rd, readbuf, rd);
            b += rd;
        }
        /* Verify we're at the end of the buffer */
        g_assert_cmpint(b[0], ==, '\0');

        g_assert_no_errno(close(fd));
        g_assert_no_errno(remove(newfilepath));
    }
    XpmFree(buffer);

    g_assert_no_errno(g_rmdir(testdir));

    g_free(newfilepath);
    g_free(filename);
    g_free(testdir);

    return status;
}

static void
test_XpmWriteFileFromBuffer(void)
{
    /* Todo: verify trying to write to an unwritable file fails */

    TestAllNormalFiles("good", XpmSuccess, TestWriteFileFromBuffer);
    /* XpmReadFileToBuffer does not support compressed files */
}

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);


    g_test_add_func("/XpmRead/XpmWriteFileFromXpmImage",
                    test_XpmWriteFileFromXpmImage);
    g_test_add_func("/XpmRead/XpmWriteFileFromData",
                    test_XpmWriteFileFromData);
    g_test_add_func("/XpmRead/XpmWriteFileFromBuffer",
                    test_XpmWriteFileFromBuffer);

    return g_test_run();
}
