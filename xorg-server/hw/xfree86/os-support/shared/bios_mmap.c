/*
 * Copyright 1993 by David Wexelblat <dwex@goblin.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of David Wexelblat not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  David Wexelblat makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * DAVID WEXELBLAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL DAVID WEXELBLAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>

#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"

/*
 * Read BIOS via mmap()ing DEV_MEM
 */

#ifndef __alpha__
int
xf86ReadBIOS(unsigned long Base, unsigned long Offset, unsigned char *Buf,
             int Len)
{
    int fd;
    unsigned char *ptr;
    int psize;
    int mlen;

    if ((fd = open(DEV_MEM, O_RDONLY)) < 0) {
        xf86Msg(X_WARNING, "xf86ReadBIOS: Failed to open %s (%s)\n",
                DEV_MEM, strerror(errno));
        return -1;
    }
    psize = getpagesize();
    Offset += Base & (psize - 1);
    Base &= ~(psize - 1);
    mlen = (Offset + Len + psize - 1) & ~(psize - 1);
    ptr = (unsigned char *) mmap((caddr_t) 0, mlen, PROT_READ,
                                 MAP_SHARED, fd, (off_t) Base);
    if (ptr == MAP_FAILED) {
        xf86Msg(X_WARNING, "xf86ReadBIOS: %s mmap failed (%s)\n",
                DEV_MEM, strerror(errno));
        close(fd);
        return -1;
    }
    DebugF("xf86ReadBIOS: BIOS at 0x%08x has signature 0x%04x\n",
           Base, ptr[0] | (ptr[1] << 8));
    (void) memcpy(Buf, (void *) (ptr + Offset), Len);
    (void) munmap((caddr_t) ptr, mlen);
    (void) close(fd);
    return Len;
}

#else                           /* __alpha__ */

  /*
   *  We trick "mmap" into mapping BUS memory for us via BUS_BASE,
   *  which is the KSEG address of the start of the DENSE memory
   *  area.
   */

  /*
   * NOTE: there prolly ought to be more validity checks and all
   *  re: boundaries and sizes and such...
   */

#ifdef linux

extern unsigned long _bus_base(void);

#define BUS_BASE _bus_base()

#else

extern u_int64_t dense_base(void);

#define BUS_BASE dense_base()

#endif

int
xf86ReadBIOS(unsigned long Base, unsigned long Offset, unsigned char *Buf,
             int Len)
{
    caddr_t base;
    int fd;
    int psize;
    int mlen;

    if ((fd = open(DEV_MEM, O_RDONLY)) < 0) {
        xf86Msg(X_WARNING, "xf86ReadBIOS: Failed to open %s (%s)\n",
                DEV_MEM, strerror(errno));
        return -1;
    }

    psize = getpagesize();
    Offset += Base & (psize - 1);
    Base &= ~(psize - 1);
    mlen = (Offset + Len + psize - 1) & ~(psize - 1);
    base = mmap((caddr_t) 0, mlen, PROT_READ,
                MAP_SHARED, fd, (off_t) (Base + BUS_BASE));

    if (base == MAP_FAILED) {
        xf86Msg(X_WARNING, "xf86ReadBIOS: Failed to mmap %s (%s)\n",
                DEV_MEM, strerror(errno));
        return -1;
    }

    xf86SlowBCopyFromBus((unsigned char *) (base + Offset), Buf, Len);

    munmap((caddr_t) base, mlen);
    close(fd);
    return Len;
}

#endif                          /* __alpha__ */
