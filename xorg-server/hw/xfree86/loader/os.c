/*
 * Copyright (c) 1999-2002 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "loaderProcs.h"

/*
 * OSNAME is a standard form of the OS name that may be used by the
 * loader and by OS-specific modules.  OSNAME here is different from what's in
 * dix-config.h
 */

#undef OSNAME
#if defined(__linux__)
#define OSNAME "linux"
#elif defined(__FreeBSD__)
#define OSNAME "freebsd"
#elif defined(__DragonFly__)
#define OSNAME "dragonfly"
#elif defined(__NetBSD__)
#define OSNAME "netbsd"
#elif defined(__OpenBSD__)
#define OSNAME "openbsd"
#elif defined(__GNU__)
#define OSNAME "hurd"
#elif defined(SVR4) && defined(sun)
#define OSNAME "solaris"
#elif defined(SVR5)
#define OSNAME "svr5"
#elif defined(SVR4)
#define OSNAME "svr4"
#else
#define OSNAME "unknown"
#endif

/* Return the OS name, and run-time OS version */

void
LoaderGetOS(const char **name, int *major, int *minor, int *teeny)
{
    if (name)
        *name = OSNAME;

    /* reporting runtime versions isn't supported yet */
}
