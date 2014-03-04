/**
 * Copyright © 2012 Red Hat, Inc.
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

#include <signal.h>
#include "os.h"

static int last_signal = 0;
static int expect_signal = 0;

static void sighandler(int signal)
{
    assert(expect_signal);
    expect_signal = 0;
    if (!last_signal)
        raise(signal);
    OsBlockSignals();
    OsReleaseSignals();
    last_signal = 1;
    expect_signal = 1;
}

static int
sig_is_blocked(int sig)
{
    sigset_t current;

    sigemptyset(&current);
    assert(sigprocmask(SIG_BLOCK, NULL, &current) == 0);
    return sigismember(&current, sig);
}

static void block_sigio_test(void)
{
#ifdef SIG_BLOCK
    sigset_t current;

    sigemptyset(&current);
    assert(!sig_is_blocked(SIGIO));

    /* block once */
    OsBlockSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSIGIO();
    assert(!sig_is_blocked(SIGIO));

    /* block twice, nested */
    OsBlockSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsBlockSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSIGIO();
    assert(!sig_is_blocked(SIGIO));

    /* block all */
    OsBlockSignals();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSignals();
    assert(!sig_is_blocked(SIGIO));

    /* block all nested */
    OsBlockSignals();
    assert(sig_is_blocked(SIGIO));
    OsBlockSignals();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSignals();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSignals();
    assert(!sig_is_blocked(SIGIO));

    /* mix the two */
    /* ABBA */
    OsBlockSignals();
    assert(sig_is_blocked(SIGIO));
    OsBlockSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSignals();
    assert(!sig_is_blocked(SIGIO));

    /* ABAB */
    OsBlockSignals();
    assert(sig_is_blocked(SIGIO));
    OsBlockSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSignals();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSIGIO();
    assert(!sig_is_blocked(SIGIO));

    /* BAAB */
    OsBlockSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsBlockSignals();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSignals();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSIGIO();
    assert(!sig_is_blocked(SIGIO));

    /* BABA */
    OsBlockSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsBlockSignals();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSIGIO();
    assert(sig_is_blocked(SIGIO));
    OsReleaseSignals();
    assert(!sig_is_blocked(SIGIO));
#endif
}

static void block_sigio_test_nested(void)
{
#ifdef SIG_BLOCK
    /* Check for bug releasing SIGIO during SIGIO signal handling.
       test case:
           raise signal
           → in signal handler:
                raise signal
                OsBlockSignals()
                OsReleaseSignals()
                tail guard
       tail guard must be hit.
     */
    void (*old_handler)(int);
    old_handler = OsSignal(SIGIO, sighandler);
    expect_signal = 1;
    assert(raise(SIGIO) == 0);
    assert(OsSignal(SIGIO, old_handler) == sighandler);
#endif
}

int
main(int argc, char **argv)
{
    block_sigio_test();
    block_sigio_test_nested();
    return 0;
}
