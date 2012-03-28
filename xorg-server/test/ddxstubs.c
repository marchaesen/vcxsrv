/**
 * Copyright © 2012 Apple Inc.
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

/* This file contains stubs for some symbols which are usually provided by a
 * DDX.  These stubs should allow the unit tests to build on platforms with
 * stricter linkers (eg: darwin) when the Xorg DDX is not built.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "input.h"
#include "mi.h"

void
DDXRingBell(int volume, int pitch, int duration)
{
}

void
ProcessInputEvents(void)
{
    mieqProcessInputEvents();
}

void
OsVendorInit(void)
{
}

void
OsVendorFatalError(const char *f, va_list args)
{
}

void
AbortDDX(enum ExitCode error)
{
    OsAbort();
}

void
ddxUseMsg(void)
{
}

int
ddxProcessArgument(int argc, char *argv[], int i)
{
    return 0;
}

void
ddxGiveUp(enum ExitCode error)
{
}

Bool
LegalModifier(unsigned int key, DeviceIntPtr pDev)
{
    return TRUE;
}

#ifdef XQUARTZ
#include <pthread.h>

BOOL serverRunning = TRUE;
pthread_mutex_t serverRunningMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t serverRunningCond = PTHREAD_COND_INITIALIZER;

int darwinMainScreenX = 0;
int darwinMainScreenY = 0;

BOOL no_configure_window = FALSE;

void
darwinEvents_lock(void)
{
}

void
darwinEvents_unlock(void)
{
}
#endif

#ifdef DDXBEFORERESET
void
ddxBeforeReset(void)
{
}
#endif
