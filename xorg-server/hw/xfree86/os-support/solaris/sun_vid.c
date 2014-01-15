/*
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany
 * Copyright 1993 by David Wexelblat <dwex@goblin.org>
 * Copyright 1999 by David Holland <davidh@iquest.net>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the names of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 */
/* Copyright (c) 2008, Oracle and/or its affiliates. All rights reserved.
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

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <sys/types.h>          /* get __x86 definition if not set by compiler */

#if defined(__i386__) || defined(__i386) || defined(__x86)
#define _NEED_SYSI86
#endif
#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"
#include "xf86OSpriv.h"
#include <sys/mman.h>

/***************************************************************************/
/* Video Memory Mapping section 					   */
/***************************************************************************/

static char *apertureDevName = NULL;
static int apertureDevFD_ro = -1;
static int apertureDevFD_rw = -1;

static Bool
solOpenAperture(void)
{
    if (apertureDevName == NULL) {
        apertureDevName = "/dev/xsvc";
        if ((apertureDevFD_rw = open(apertureDevName, O_RDWR)) < 0) {
            xf86MsgVerb(X_WARNING, 0,
                        "solOpenAperture: failed to open %s (%s)\n",
                        apertureDevName, strerror(errno));
            apertureDevName = "/dev/fbs/aperture";
            apertureDevFD_rw = open(apertureDevName, O_RDWR);
        }
        apertureDevFD_ro = open(apertureDevName, O_RDONLY);

        if ((apertureDevFD_rw < 0) || (apertureDevFD_ro < 0)) {
            xf86MsgVerb(X_WARNING, 0,
                        "solOpenAperture: failed to open %s (%s)\n",
                        apertureDevName, strerror(errno));
            xf86MsgVerb(X_WARNING, 0,
                        "solOpenAperture: either /dev/fbs/aperture"
                        " or /dev/xsvc required\n");

            apertureDevName = NULL;

            if (apertureDevFD_rw >= 0) {
                close(apertureDevFD_rw);
            }
            apertureDevFD_rw = -1;

            if (apertureDevFD_ro >= 0) {
                close(apertureDevFD_ro);
            }
            apertureDevFD_ro = -1;

            return FALSE;
        }
    }
    return TRUE;
}

static void *
solMapVidMem(int ScreenNum, unsigned long Base, unsigned long Size, int Flags)
{
    void *base;
    int fd;
    int prot;

    if (Flags & VIDMEM_READONLY) {
        fd = apertureDevFD_ro;
        prot = PROT_READ;
    }
    else {
        fd = apertureDevFD_rw;
        prot = PROT_READ | PROT_WRITE;
    }

    if (fd < 0) {
        xf86DrvMsg(ScreenNum, X_ERROR,
                   "solMapVidMem: failed to open %s (%s)\n",
                   apertureDevName, strerror(errno));
        return NULL;
    }

    base = mmap(NULL, Size, prot, MAP_SHARED, fd, (off_t) Base);

    if (base == MAP_FAILED) {
        xf86DrvMsg(ScreenNum, X_ERROR,
                   "solMapVidMem: failed to mmap %s (0x%08lx,0x%lx) (%s)\n",
                   apertureDevName, Base, Size, strerror(errno));
        return NULL;
    }

    return base;
}

/* ARGSUSED */
static void
solUnMapVidMem(int ScreenNum, void *Base, unsigned long Size)
{
    if (munmap(Base, Size) != 0) {
        xf86DrvMsgVerb(ScreenNum, X_WARNING, 0,
                       "solUnMapVidMem: failed to unmap %s"
                       " (0x%p,0x%lx) (%s)\n",
                       apertureDevName, Base, Size, strerror(errno));
    }
}

_X_HIDDEN void
xf86OSInitVidMem(VidMemInfoPtr pVidMem)
{
    pVidMem->linearSupported = solOpenAperture();
    if (pVidMem->linearSupported) {
        pVidMem->mapMem = solMapVidMem;
        pVidMem->unmapMem = solUnMapVidMem;
    }
    else {
        xf86MsgVerb(X_WARNING, 0,
                    "xf86OSInitVidMem: linear memory access disabled\n");
    }
    pVidMem->initialised = TRUE;
}

/*
 * Read BIOS via mmap()ing physical memory.
 */
int
xf86ReadBIOS(unsigned long Base, unsigned long Offset, unsigned char *Buf,
             int Len)
{
    unsigned char *ptr;
    int psize;
    int mlen;

    psize = getpagesize();
    Offset += Base & (psize - 1);
    Base &= ~(psize - 1);
    mlen = (Offset + Len + psize - 1) & ~(psize - 1);

    if (solOpenAperture() == FALSE) {
        xf86Msg(X_WARNING,
                "xf86ReadBIOS: Failed to open aperture to read BIOS\n");
        return -1;
    }

    ptr = (unsigned char *) mmap(NULL, mlen, PROT_READ,
                                 MAP_SHARED, apertureDevFD_ro, (off_t) Base);
    if (ptr == MAP_FAILED) {
        xf86Msg(X_WARNING, "xf86ReadBIOS: %s mmap failed [0x%08lx, 0x%04x]\n",
                apertureDevName, Base, mlen);
        return -1;
    }

    (void) memcpy(Buf, (void *) (ptr + Offset), Len);
    if (munmap((caddr_t) ptr, mlen) != 0) {
        xf86MsgVerb(X_WARNING, 0,
                    "xf86ReadBIOS: failed to unmap %s (0x%p,0x%x) (%s)\n",
                    apertureDevName, ptr, mlen, strerror(errno));
    }

    return Len;
}

/***************************************************************************/
/* I/O Permissions section						   */
/***************************************************************************/

#if defined(__i386__) || defined(__i386) || defined(__x86)
static Bool ExtendedEnabled = FALSE;
#endif

Bool
xf86EnableIO(void)
{
#if defined(__i386__) || defined(__i386) || defined(__x86)
    if (ExtendedEnabled)
        return TRUE;

    if (sysi86(SI86V86, V86SC_IOPL, PS_IOPL) < 0) {
        xf86Msg(X_WARNING, "xf86EnableIOPorts: Failed to set IOPL for I/O\n");
        return FALSE;
    }
    ExtendedEnabled = TRUE;
#endif                          /* i386 */
    return TRUE;
}

void
xf86DisableIO(void)
{
#if defined(__i386__) || defined(__i386) || defined(__x86)
    if (!ExtendedEnabled)
        return;

    sysi86(SI86V86, V86SC_IOPL, 0);

    ExtendedEnabled = FALSE;
#endif                          /* i386 */
}
