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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <X11/Xfuncproto.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <sys/resource.h>
#include <assert.h>
#ifdef HAVE_MALLOC_H
# include <malloc.h>
#endif

/* Tell gcc not to warn that we're asking for impossible sizes in some tests */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Walloc-size-larger-than="

/* to silence missing prototype warning */
void *Xmureallocarray(void *optr, size_t nmemb, size_t size);

/*
 * To ensure we're testing our Xmureallocarray and not any system-provided
 * reallocarray, we directly include code being tested, since it won't be
 * built into libXmu on platforms with a system-provided function.
 */
#include "../src/reallocarray.c"


#ifndef g_assert_no_errno /* defined in glib 2.66 & later*/
#define g_assert_no_errno(expr) g_assert_cmpint((expr), >=, 0)
#endif

/*
 * Check that allocations point to properly aligned memory.
 * For libXmu, we consider that to be aligned to an 8-byte (64-bit) boundary.
 */
#define EXPECTED_ALIGNMENT 8

#define CHECK_ALIGNMENT(ptr) \
    g_assert_cmpint(((uintptr_t)ptr) % EXPECTED_ALIGNMENT, ==, 0)

/* Check that allocations point to expected amounts of memory, as best we can. */
#ifdef HAVE_MALLOC_USABLE_SIZE
# define CHECK_SIZE(ptr, size) do {		\
    size_t ps = malloc_usable_size(ptr);	\
    g_assert_cmpint(ps, >=, (size));		\
} while (0)
#else
# define CHECK_SIZE(ptr, size) *(((char *)ptr) + ((size) - 1)) = 0
#endif

/* Limit we set for memory allocation to be able to test failure cases */
#define ALLOC_LIMIT (INT_MAX / 4)

/* Square root of SIZE_MAX+1 */
#define SQRT_SIZE_MAX ((size_t)1 << (sizeof (size_t) * 4))


/* Make sure Xmureallocarray() works for a non-zero amount of memory */
static void test_Xmureallocarray_normal(void)
{
    void *p, *p2;
    char *p3;

    errno = 0;

    /* Make sure reallocarray with a NULL pointer acts as malloc */
    p = Xmureallocarray(NULL, 8, 14);
    g_assert_nonnull(p);
    CHECK_ALIGNMENT(p);
    CHECK_SIZE(p, 8 * 14);

    /* make sure we can write to all the allocated memory */
    memset(p, 'A', 8 * 14);

    /* create another block after the first */
    p2 = Xmureallocarray(NULL, 1, 73);
    g_assert_nonnull(p2);
    CHECK_ALIGNMENT(p2);
    CHECK_SIZE(p2, 73);

    /* now resize the first */
    p3 = Xmureallocarray(p, 73, 14);
    g_assert_nonnull(p3);
    CHECK_ALIGNMENT(p3);
    CHECK_SIZE(p3, 73 * 14);
    /* verify previous values are still present */
    for (int i = 0; i < (8 * 14); i++) {
        g_assert_cmpint(p3[i], ==, 'A');
    }

    free(p3);
    free(p2);
    g_assert_cmpint(errno, ==, 0);
}

/* Make sure Xmureallocarray(0) returns a valid pointer as expected */
static void test_Xmureallocarray_zero(void)
{
    void *p, *p2;

    errno = 0;

    p = Xmureallocarray(NULL, 0, 0);
    g_assert_nonnull(p);

    p2 = Xmureallocarray(p, 0, 0);
#ifdef MALLOC_0_RETURNS_NULL
    g_assert_null(p);
#else
    g_assert_nonnull(p);
#endif

    free(p2);
    g_assert_cmpint(errno, ==, 0);
}

/* Make sure sizes larger than the limit we set in main() fail */
static void test_Xmureallocarray_oversize(void)
{
    void *p, *p2;

    /* Pick a number of elements between 1 & 16K */
    guint32 num = g_test_rand_int_range(1, (16 * 1024));
    /* Pick a size between 1 & 16K */
    guint32 size = g_test_rand_int_range(1, (16 * 1024));

    p = Xmureallocarray(NULL, num, size);
    g_assert_nonnull(p);
    CHECK_ALIGNMENT(p);
    CHECK_SIZE(p, num * size);

    p2 = Xmureallocarray(p, 2, ALLOC_LIMIT);
    g_assert_null(p2);
    /* Unfortunately, g_assert_null has a test_nonfatal_assertions option that
     * provides a code path that can get here even if p2 is not NULL, thus
     * leading gcc to issue a -Wuse-after-free warning if we don't assert
     * again that p2 is NULL and thus p is still valid.
     */
    assert(p2 == NULL);
    g_assert_cmpint(errno, ==, ENOMEM);

    errno = 0;
    /* Free p, since we forced the realloc to fail, leaving it valid */
    free(p);
    g_assert_cmpint(errno, ==, 0);
}

/* Make sure Xmureallocarray catches integer overflow if possible, by requesting
 * sizes that are so large that they cause overflows when either adding the
 * reallocarray data block overhead or aligning.
 */
static void test_Xmureallocarray_overflow(void)
{
    void *p, *p2;

    /* Pick a number of elements between 1 & 16K */
    guint32 num = g_test_rand_int_range(1, (16 * 1024));
    /* Pick a size between 1 & 16K */
    guint32 size = g_test_rand_int_range(1, (16 * 1024));

    p = Xmureallocarray(NULL, num, size);
    g_assert_nonnull(p);
    CHECK_ALIGNMENT(p);
    CHECK_SIZE(p, num * size);

    p2 = Xmureallocarray(p, 1, SIZE_MAX);
    g_assert_null(p2);
    /* See above about why we assert this again */
    assert(p2 == NULL);
    g_assert_cmpint(errno, ==, ENOMEM);

    /* SQRT_SIZE_MAX * SQRT_SIZE_MAX == 0 due to overflow */
    p2 = Xmureallocarray(p, SQRT_SIZE_MAX, SQRT_SIZE_MAX);
    g_assert_null(p2);
    assert(p2 == NULL);
    g_assert_cmpint(errno, ==, ENOMEM);


    /* Overflows to a small positive number */
    p2 = Xmureallocarray(p, SQRT_SIZE_MAX + 1, SQRT_SIZE_MAX);
    g_assert_null(p2);
    assert(p2 == NULL);
    g_assert_cmpint(errno, ==, ENOMEM);

    errno = 0;
    /* Free p, since we forced the reallocs to fail, leaving it valid */
    free(p);
    g_assert_cmpint(errno, ==, 0);
}
#pragma GCC diagnostic pop

int main(int argc, char** argv)
{
    struct rlimit lim;

    g_test_init(&argc, &argv, NULL);
    g_test_bug_base(PACKAGE_BUGREPORT);

    /* Set a memory limit so we can test allocations over that size fail */
    g_assert_no_errno(getrlimit(RLIMIT_AS, &lim));
    if (lim.rlim_cur > ALLOC_LIMIT) {
        lim.rlim_cur = ALLOC_LIMIT;
        g_assert_no_errno(setrlimit(RLIMIT_AS, &lim));
    }

    g_test_add_func("/reallocarray/Xmureallocarray/normal",
                    test_Xmureallocarray_normal);
    g_test_add_func("/reallocarray/Xmureallocarray/zero",
                    test_Xmureallocarray_zero);
    g_test_add_func("/reallocarray/Xmureallocarray/oversize",
                    test_Xmureallocarray_oversize);
    g_test_add_func("/reallocarray/Xmureallocarray/overflow",
                    test_Xmureallocarray_overflow);

    return g_test_run();
}
