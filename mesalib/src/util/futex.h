/*
 * Copyright © 2015 Intel
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef UTIL_FUTEX_H
#define UTIL_FUTEX_H

#if defined(HAVE_LINUX_FUTEX_H)

#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>

static inline long sys_futex(void *addr1, int op, int val1, const struct timespec *timeout, void *addr2, int val3)
{
   return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

static inline int futex_wake(uint32_t *addr, int count)
{
   return sys_futex(addr, FUTEX_WAKE, count, NULL, NULL, 0);
}

static inline int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   /* FUTEX_WAIT_BITSET with FUTEX_BITSET_MATCH_ANY is equivalent to
    * FUTEX_WAIT, except that it treats the timeout as absolute. */
   return sys_futex(addr, FUTEX_WAIT_BITSET, value, timeout, NULL,
                    FUTEX_BITSET_MATCH_ANY);
}

#endif

#endif /* UTIL_FUTEX_H */
