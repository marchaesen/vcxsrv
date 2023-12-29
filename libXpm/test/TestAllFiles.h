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

#include <glib.h>

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"

/* g_pattern_spec_match_string is available in glib 2.70 and later,
   to replace the deprecated g_pattern_match_string */
#ifdef HAVE_G_PATTERN_SPEC_MATCH_STRING
#define g_pattern_match_string g_pattern_spec_match_string
#endif

#define DEFAULT_TIMEOUT 10 /* maximum seconds for each file */

static sigjmp_buf jump_env;

static void sigalrm (int sig)
{
    siglongjmp(jump_env, 1);
}

typedef int (*testfilefunc)(const gchar *filepath);

/*
 * Test all files in a given subdir of either the build or source directory
 */
static void
TestAllFilesByType(GTestFileType file_type, gboolean compressed,
                   const char *subdir, int expected, testfilefunc testfunc)
{
    const gchar *datadir_path, *filename;
    GDir *datadir;
    GError *err = NULL;
    int timeout = DEFAULT_TIMEOUT;
    char *timeout_env;

    GPatternSpec *xpm_pattern = g_pattern_spec_new("*.xpm");
#ifndef NO_ZPIPE
    GPatternSpec *z_pattern = compressed ? g_pattern_spec_new("*.xpm.Z") : NULL;
    GPatternSpec *gz_pattern = compressed ? g_pattern_spec_new("*.xpm.gz") : NULL;
#endif

    /* Allow override when debugging tests */
    timeout_env = getenv("XPM_TEST_TIMEOUT");
    if (timeout_env != NULL) {
        int from_env = atoi(timeout_env);

        if (from_env >= 0)
            timeout = from_env;
    }

    datadir_path = g_test_get_filename(file_type, "pixmaps", subdir,
                       (file_type == G_TEST_BUILT) ? "generated" : NULL, NULL);
    g_assert_nonnull(datadir_path);
    g_test_message("Reading files from %s", datadir_path);

    datadir = g_dir_open(datadir_path, 0, &err);
    g_assert_no_error(err);

    errno = 0;
    while ((filename = g_dir_read_name(datadir)) != NULL) {

        if (!g_pattern_match_string(xpm_pattern, filename)) {
#ifndef NO_ZPIPE
                if (!compressed ||
                    (!g_pattern_match_string(z_pattern, filename) &&
                     !g_pattern_match_string(gz_pattern, filename)))
#endif
                {
                    g_test_message("skipping \"%s\"", filename);
                    continue;
                }
        }

        /*
         * Assumes the test function should complete in less than "timeout"
         * seconds and fails if they don't, in order to catch runaway loops.
         */
        if (timeout > 0) {
            struct sigaction sa = {
                .sa_handler = sigalrm,
                .sa_flags = SA_RESTART
            };
            sigemptyset (&sa.sa_mask);
            sigaction(SIGALRM, &sa, NULL);
        }

        if (sigsetjmp(jump_env, 1) == 0) {
            int status;
            gchar *filepath;

            filepath = g_build_filename(datadir_path, filename, NULL);

            g_test_message("testing \"%s\", should return %d",
                           filename, expected);
            if (timeout > 0)
                alarm(timeout);
            status = testfunc(filepath);
            g_assert_cmpint(status, ==, expected);

            if (timeout > 0) {
                status = alarm(0); /* cancel alarm */
                g_test_message("%d seconds left on %d second timer",
                               status, timeout);
            }

            g_free(filepath);
        }
        else {
            g_test_message("timed out reading %s", filename);
            g_assertion_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                "test timed out");
        }

        errno = 0;
    }
    // g_assert_cmpint(errno, ==, 0); - not sure why this sometimes fails

    g_dir_close(datadir);
}

/*
 * Test all non-compressed files in a given subdir
 */
static void
TestAllNormalFiles(const char *subdir, int expected, testfilefunc testfunc)
{
    TestAllFilesByType(G_TEST_DIST, FALSE, subdir, expected, testfunc);
}

/*
 * Test all compressed files in a given subdir
 */
static void
TestAllCompressedFiles(const char *subdir, int expected, testfilefunc testfunc)
{
#ifdef NO_ZPIPE
    g_test_message("compression disabled, skipping compressed file tests");
#else
    TestAllFilesByType(G_TEST_BUILT, TRUE, subdir, expected, testfunc);
#endif
}
