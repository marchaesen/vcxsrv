/*
 * Copyright 2002-2003 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 *
 * Provides an interface for handling SIGIO signals for input devices. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "inputstr.h"
#include "dmxinputinit.h"
#include "dmxsigio.h"
#include "dmxevents.h"
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

static int dmxFdCount = 0;
static Bool dmxInputEnabled = TRUE;

/* Define equivalents for non-POSIX systems (e.g., SGI IRIX, Solaris) */
#ifndef O_ASYNC
#ifdef FASYNC
#define O_ASYNC FASYNC
#else
#define O_ASYNC 0
#endif
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK FNONBLK
#endif

static void
dmxSigioHandler(int sig)
{
    int i, j;
    DMXInputInfo *dmxInput;

    for (i = 0, dmxInput = &dmxInputs[0]; i < dmxNumInputs; i++, dmxInput++) {
        if (dmxInput->sigioState == DMX_ACTIVESIGIO) {
            for (j = 0; j < dmxInput->numDevs; j++) {
                DMXLocalInputInfoPtr dmxLocal = dmxInput->devs[j];

                if (dmxLocal->collect_events) {
                    dmxLocal->collect_events(&dmxLocal->pDevice->public,
                                             dmxMotion,
                                             dmxEnqueue,
                                             dmxCheckSpecialKeys, DMX_NO_BLOCK);
                }
            }
        }
    }
}

static void
dmxSigioHook(void)
{
    struct sigaction a;
    sigset_t s;

    memset(&a, 0, sizeof(a));
    a.sa_handler = dmxSigioHandler;
    sigemptyset(&a.sa_mask);
    sigaddset(&a.sa_mask, SIGIO);
    sigaddset(&a.sa_mask, SIGALRM);
    sigaddset(&a.sa_mask, SIGVTALRM);
    sigaction(SIGIO, &a, 0);

    sigemptyset(&s);
    sigprocmask(SIG_SETMASK, &s, 0);
}

static void
dmxSigioUnhook(void)
{
    struct sigaction a;

    memset(&a, 0, sizeof(a));
    a.sa_handler = SIG_IGN;
    sigemptyset(&a.sa_mask);
    sigaction(SIGIO, &a, 0);
}

static void
dmxSigioAdd(DMXInputInfo * dmxInput)
{
    int flags;
    int i;

    switch (dmxInput->sigioState) {
    case DMX_NOSIGIO:
        return;
    case DMX_USESIGIO:
        dmxInput->sigioState = DMX_ACTIVESIGIO;
        break;
    case DMX_ACTIVESIGIO:
        return;
    }

    for (i = 0; i < dmxInput->sigioFdCount; i++) {
        if (!dmxInput->sigioAdded[i]) {
            int fd = dmxInput->sigioFd[i];

            fcntl(fd, F_SETOWN, getpid());
            flags = fcntl(fd, F_GETFL);
            flags |= O_ASYNC | O_NONBLOCK;
            fcntl(fd, F_SETFL, flags);

            AddEnabledDevice(fd);
            dmxInput->sigioAdded[i] = TRUE;

            if (++dmxFdCount == 1)
                dmxSigioHook();
        }
    }
}

static void
dmxSigioRemove(DMXInputInfo * dmxInput)
{
    int flags;
    int i;

    switch (dmxInput->sigioState) {
    case DMX_NOSIGIO:
        return;
    case DMX_USESIGIO:
        return;
    case DMX_ACTIVESIGIO:
        dmxInput->sigioState = DMX_USESIGIO;
        break;
    }

    for (i = 0; i < dmxInput->sigioFdCount; i++) {
        if (dmxInput->sigioAdded[i]) {
            int fd = dmxInput->sigioFd[i];

            dmxInput->sigioAdded[i] = FALSE;
            RemoveEnabledDevice(fd);

            flags = fcntl(fd, F_GETFL);
            flags &= ~(O_ASYNC | O_NONBLOCK);
            fcntl(fd, F_SETFL, flags);

            if (!--dmxFdCount)
                dmxSigioUnhook();
        }
    }
}

/** Enable SIGIO handling.  This instantiates the handler with the OS. */
void
dmxSigioEnableInput(void)
{
    int i;
    DMXInputInfo *dmxInput;

    dmxInputEnabled = TRUE;
    for (i = 0, dmxInput = &dmxInputs[0]; i < dmxNumInputs; i++, dmxInput++)
        dmxSigioAdd(dmxInput);
}

/** Disable SIGIO handling.  This removes the hanlder from the OS. */
void
dmxSigioDisableInput(void)
{
    int i;
    DMXInputInfo *dmxInput;

    dmxInputEnabled = FALSE;
    for (i = 0, dmxInput = &dmxInputs[0]; i < dmxNumInputs; i++, dmxInput++)
        dmxSigioRemove(dmxInput);
}

/** Make a note that the input device described in \a dmxInput will be
 * using the file descriptor \a fd for SIGIO signals.  Calls
 * AddEnabledDevice ifi SIGIO handling has been enabled with
 * #dmxSigioEnableInput(). */
void
dmxSigioRegister(DMXInputInfo * dmxInput, int fd)
{
    dmxInput->sigioState = DMX_USESIGIO;
    if (dmxInput->sigioFdCount >= DMX_MAX_SIGIO_FDS)
        dmxLog(dmxFatal, "Too many SIGIO file descriptors (%d >= %d)\n",
               dmxInput->sigioFdCount, DMX_MAX_SIGIO_FDS);

    dmxInput->sigioFd[dmxInput->sigioFdCount++] = fd;
    if (dmxInputEnabled)
        dmxSigioAdd(dmxInput);
}

/** Remove the notes that \a dmxInput is using any file descriptors for
 * SIGIO signals.  Calls RemoveEnabledDevice. */
void
dmxSigioUnregister(DMXInputInfo * dmxInput)
{
    if (dmxInput->sigioState == DMX_NOSIGIO)
        return;
    dmxSigioRemove(dmxInput);
    dmxInput->sigioState = DMX_NOSIGIO;
    dmxInput->sigioFdCount = 0;
}
