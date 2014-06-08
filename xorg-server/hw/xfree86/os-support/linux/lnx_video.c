/*
 * Copyright 1992 by Orest Zborowski <obz@Kodak.com>
 * Copyright 1993 by David Wexelblat <dwex@goblin.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of Orest Zborowski and David Wexelblat
 * not be used in advertising or publicity pertaining to distribution of
 * the software without specific, written prior permission.  Orest Zborowski
 * and David Wexelblat make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * OREST ZBOROWSKI AND DAVID WEXELBLAT DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL OREST ZBOROWSKI OR DAVID WEXELBLAT BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <errno.h>
#include <string.h>

#include <X11/X.h>
#include "input.h"
#include "scrnintstr.h"

#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"
#include "xf86OSpriv.h"
#ifdef __alpha__
#include "shared/xf86Axp.h"
#endif

#ifdef HAS_MTRR_SUPPORT
#include <asm/mtrr.h>
#endif

static Bool ExtendedEnabled = FALSE;

#ifdef __ia64__

#include "compiler.h"
#include <sys/io.h>

#elif !defined(__powerpc__) && \
      !defined(__mc68000__) && \
      !defined(__sparc__) && \
      !defined(__mips__) && \
      !defined(__nds32__) && \
      !defined(__arm__) && \
      !defined(__aarch64__) && \
      !defined(__arc__) && \
      !defined(__xtensa__)

/*
 * Due to conflicts with "compiler.h", don't rely on <sys/io.h> to declare
 * these.
 */
extern int ioperm(unsigned long __from, unsigned long __num, int __turn_on);
extern int iopl(int __level);

#endif

#ifdef __alpha__
#define BUS_BASE bus_base
#else
#define BUS_BASE (0)
#endif                          /*  __alpha__ */

/***************************************************************************/
/* Video Memory Mapping section                                            */
/***************************************************************************/

static void *mapVidMem(int, unsigned long, unsigned long, int);
static void unmapVidMem(int, void *, unsigned long);

#if defined (__alpha__)
extern void sethae(unsigned long hae);
extern unsigned long _bus_base __P((void)) __attribute__ ((const));
extern unsigned long _bus_base_sparse __P((void)) __attribute__ ((const));

static void *mapVidMemSparse(int, unsigned long, unsigned long, int);
extern axpDevice lnxGetAXP(void);
static void unmapVidMemSparse(int, void *, unsigned long);
static axpDevice axpSystem = -1;
static Bool needSparse;
static unsigned long hae_thresh;
static unsigned long hae_mask;
static unsigned long bus_base;
#endif

#ifdef HAS_MTRR_SUPPORT

#define SPLIT_WC_REGIONS 1

static void *setWC(int, unsigned long, unsigned long, Bool, MessageType);
static void undoWC(int, void *);

/* The file desc for /proc/mtrr. Once opened, left opened, and the mtrr
   driver will clean up when we exit. */
#define MTRR_FD_UNOPENED (-1)   /* We have yet to open /proc/mtrr */
#define MTRR_FD_PROBLEM (-2)    /* We tried to open /proc/mtrr, but had
                                   a problem. */
static int mtrr_fd = MTRR_FD_UNOPENED;

/* Open /proc/mtrr. FALSE on failure. Will always fail on Linux 2.0, 
   and will fail on Linux 2.2 with MTRR support configured out,
   so verbosity should be chosen appropriately. */
static Bool
mtrr_open(int verbosity)
{
    /* Only report absence of /proc/mtrr once. */
    static Bool warned = FALSE;

    if (mtrr_fd == MTRR_FD_UNOPENED) {
        mtrr_fd = open("/proc/mtrr", O_WRONLY);

        if (mtrr_fd < 0)
            mtrr_fd = MTRR_FD_PROBLEM;
    }

    if (mtrr_fd == MTRR_FD_PROBLEM) {
        /* To make sure we only ever warn once, need to check
           verbosity outside xf86MsgVerb */
        if (!warned && verbosity <= xf86GetVerbosity()) {
            xf86MsgVerb(X_WARNING, verbosity,
                        "System lacks support for changing MTRRs\n");
            warned = TRUE;
        }

        return FALSE;
    }
    else
        return TRUE;
}

/*
 * We maintain a list of WC regions for each physical mapping so they can
 * be undone when unmapping.
 */

struct mtrr_wc_region {
    struct mtrr_sentry sentry;
    Bool added;                 /* added WC or removed it */
    struct mtrr_wc_region *next;
};

static struct mtrr_wc_region *
mtrr_cull_wc_region(int screenNum, unsigned long base, unsigned long size,
                    MessageType from)
{
    /* Some BIOS writers thought that setting wc over the mmio
       region of a graphics devices was a good idea. Try to fix
       it. */

    struct mtrr_gentry gent;
    struct mtrr_wc_region *wcreturn = NULL, *wcr;
    int count, ret = 0;

    /* Linux 2.0 users should not get a warning without -verbose */
    if (!mtrr_open(2))
        return NULL;

    for (gent.regnum = 0;
         ioctl(mtrr_fd, MTRRIOC_GET_ENTRY, &gent) >= 0; gent.regnum++) {
        if (gent.type != MTRR_TYPE_WRCOMB
            || gent.base + gent.size <= base || base + size <= gent.base)
            continue;

        /* Found an overlapping region. Delete it. */

        wcr = malloc(sizeof(*wcr));
        if (!wcr)
            return NULL;
        wcr->sentry.base = gent.base;
        wcr->sentry.size = gent.size;
        wcr->sentry.type = MTRR_TYPE_WRCOMB;
        wcr->added = FALSE;

        count = 3;
        while (count-- &&
               (ret = ioctl(mtrr_fd, MTRRIOC_KILL_ENTRY, &(wcr->sentry))) < 0);

        if (ret >= 0) {
            xf86DrvMsg(screenNum, from,
                       "Removed MMIO write-combining range "
                       "(0x%lx,0x%lx)\n",
                       (unsigned long) gent.base, (unsigned long) gent.size);
            wcr->next = wcreturn;
            wcreturn = wcr;
            gent.regnum--;
        }
        else {
            free(wcr);
            xf86DrvMsgVerb(screenNum, X_WARNING, 0,
                           "Failed to remove MMIO "
                           "write-combining range (0x%lx,0x%lx)\n",
                           (unsigned long)gent.base, (unsigned long) gent.size);
        }
    }
    return wcreturn;
}

static struct mtrr_wc_region *
mtrr_remove_offending(int screenNum, unsigned long base, unsigned long size,
                      MessageType from)
{
    struct mtrr_gentry gent;
    struct mtrr_wc_region *wcreturn = NULL, **wcr;

    if (!mtrr_open(2))
        return NULL;

    wcr = &wcreturn;
    for (gent.regnum = 0;
         ioctl(mtrr_fd, MTRRIOC_GET_ENTRY, &gent) >= 0; gent.regnum++) {
        if (gent.type == MTRR_TYPE_WRCOMB
            && ((gent.base >= base && gent.base + gent.size < base + size) ||
                (gent.base > base && gent.base + gent.size <= base + size))) {
            *wcr = mtrr_cull_wc_region(screenNum, gent.base, gent.size, from);
            if (*wcr)
                gent.regnum--;
            while (*wcr) {
                wcr = &((*wcr)->next);
            }
        }
    }
    return wcreturn;
}

static struct mtrr_wc_region *
mtrr_add_wc_region(int screenNum, unsigned long base, unsigned long size,
                   MessageType from)
{
    struct mtrr_wc_region **wcr, *wcreturn, *curwcr;

    /*
     * There can be only one....
     */

    wcreturn = mtrr_remove_offending(screenNum, base, size, from);
    wcr = &wcreturn;
    while (*wcr) {
        wcr = &((*wcr)->next);
    }

    /* Linux 2.0 should not warn, unless the user explicitly asks for
       WC. */

    if (!mtrr_open(from == X_CONFIG ? 0 : 2))
        return wcreturn;

    *wcr = curwcr = malloc(sizeof(**wcr));
    if (!curwcr)
        return wcreturn;

    curwcr->sentry.base = base;
    curwcr->sentry.size = size;
    curwcr->sentry.type = MTRR_TYPE_WRCOMB;
    curwcr->added = TRUE;
    curwcr->next = NULL;

#if SPLIT_WC_REGIONS
    /*
     * Splits up the write-combining region if it is not aligned on a
     * size boundary.
     */

    {
        unsigned long lbase, d_size = 1;
        unsigned long n_size = size;
        unsigned long n_base = base;

        for (lbase = n_base, d_size = 1; !(lbase & 1);
             lbase = lbase >> 1, d_size <<= 1);
        while (d_size > n_size)
            d_size = d_size >> 1;
        DebugF("WC_BASE: 0x%lx WC_END: 0x%lx\n", base, base + d_size - 1);
        n_base += d_size;
        n_size -= d_size;
        if (n_size) {
            xf86DrvMsgVerb(screenNum, X_INFO, 3, "Splitting WC range: "
                           "base: 0x%lx, size: 0x%lx\n", base, size);
            curwcr->next = mtrr_add_wc_region(screenNum, n_base, n_size, from);
        }
        curwcr->sentry.size = d_size;
    }

        /*****************************************************************/
#endif                          /* SPLIT_WC_REGIONS */

    if (ioctl(mtrr_fd, MTRRIOC_ADD_ENTRY, &curwcr->sentry) >= 0) {
        /* Avoid printing on every VT switch */
        if (xf86ServerIsInitialising()) {
            xf86DrvMsg(screenNum, from,
                       "Write-combining range (0x%lx,0x%lx)\n", base, size);
        }
        return wcreturn;
    }
    else {
        *wcr = curwcr->next;
        free(curwcr);

        /* Don't complain about the VGA region: MTRR fixed
           regions aren't currently supported, but might be in
           the future. */
        if ((unsigned long) base >= 0x100000) {
            xf86DrvMsgVerb(screenNum, X_WARNING, 0,
                           "Failed to set up write-combining range "
                           "(0x%lx,0x%lx)\n", base, size);
        }
        return wcreturn;
    }
}

static void
mtrr_undo_wc_region(int screenNum, struct mtrr_wc_region *wcr)
{
    struct mtrr_wc_region *p, *prev;

    if (mtrr_fd >= 0) {
        p = wcr;
        while (p) {
            if (p->added)
                ioctl(mtrr_fd, MTRRIOC_DEL_ENTRY, &p->sentry);
            prev = p;
            p = p->next;
            free(prev);
        }
    }
}

static void *
setWC(int screenNum, unsigned long base, unsigned long size, Bool enable,
      MessageType from)
{
    if (enable)
        return mtrr_add_wc_region(screenNum, base, size, from);
    else
        return mtrr_cull_wc_region(screenNum, base, size, from);
}

static void
undoWC(int screenNum, void *regioninfo)
{
    mtrr_undo_wc_region(screenNum, regioninfo);
}

#endif                          /* HAS_MTRR_SUPPORT */

void
xf86OSInitVidMem(VidMemInfoPtr pVidMem)
{
    pVidMem->linearSupported = TRUE;
#ifdef __alpha__
    if (axpSystem == -1) {
        axpSystem = lnxGetAXP();
        if ((needSparse = (_bus_base_sparse() > 0))) {
            hae_thresh = xf86AXPParams[axpSystem].hae_thresh;
            hae_mask = xf86AXPParams[axpSystem].hae_mask;
        }
        bus_base = _bus_base();
    }
    if (needSparse) {
        xf86Msg(X_INFO, "Machine needs sparse mapping\n");
        pVidMem->mapMem = mapVidMemSparse;
        pVidMem->unmapMem = unmapVidMemSparse;
    }
    else {
        xf86Msg(X_INFO, "Machine type has 8/16 bit access\n");
        pVidMem->mapMem = mapVidMem;
        pVidMem->unmapMem = unmapVidMem;
    }
#else
    pVidMem->mapMem = mapVidMem;
    pVidMem->unmapMem = unmapVidMem;
#endif                          /* __alpha__ */

#ifdef HAS_MTRR_SUPPORT
    pVidMem->setWC = setWC;
    pVidMem->undoWC = undoWC;
#endif
    pVidMem->initialised = TRUE;
}

#ifdef __sparc__
/* Basically, you simply cannot do this on Sparc.  You have to do something portable
 * like use /dev/fb* or mmap() on /proc/bus/pci/X/Y nodes. -DaveM
 */
static void *
mapVidMem(int ScreenNum, unsigned long Base, unsigned long Size, int flags)
{
    return NULL;
}
#else
static void *
mapVidMem(int ScreenNum, unsigned long Base, unsigned long Size, int flags)
{
    void *base;
    int fd;
    int mapflags = MAP_SHARED;
    int prot;
    memType realBase, alignOff;

    realBase = Base & ~(getpagesize() - 1);
    alignOff = Base - realBase;
    DebugF("base: %lx, realBase: %lx, alignOff: %lx \n",
           Base, realBase, alignOff);

#if defined(__ia64__) || defined(__arm__) || defined(__s390__)
#ifndef MAP_WRITECOMBINED
#define MAP_WRITECOMBINED 0x00010000
#endif
#ifndef MAP_NONCACHED
#define MAP_NONCACHED 0x00020000
#endif
    if (flags & VIDMEM_FRAMEBUFFER)
        mapflags |= MAP_WRITECOMBINED;
    else
        mapflags |= MAP_NONCACHED;
#endif

#if 0
    /* this will disappear when people upgrade their kernels */
    fd = open(DEV_MEM,
              ((flags & VIDMEM_READONLY) ? O_RDONLY : O_RDWR) | O_SYNC);
#else
    fd = open(DEV_MEM, (flags & VIDMEM_READONLY) ? O_RDONLY : O_RDWR);
#endif
    if (fd < 0) {
        FatalError("xf86MapVidMem: failed to open " DEV_MEM " (%s)\n",
                   strerror(errno));
    }

    if (flags & VIDMEM_READONLY)
        prot = PROT_READ;
    else
        prot = PROT_READ | PROT_WRITE;

    /* This requires linux-0.99.pl10 or above */
    base = mmap((caddr_t) 0, Size + alignOff, prot, mapflags, fd,
                (off_t) realBase + BUS_BASE);
    close(fd);
    if (base == MAP_FAILED) {
        FatalError("xf86MapVidMem: Could not mmap framebuffer"
                   " (0x%08lx,0x%lx) (%s)\n", Base, Size, strerror(errno));
    }
    DebugF("base: %lx aligned base: %lx\n", base, (char *) base + alignOff);
    return (char *) base + alignOff;
}
#endif                          /* !(__sparc__) */

static void
unmapVidMem(int ScreenNum, void *Base, unsigned long Size)
{
    uintptr_t alignOff = (uintptr_t) Base
        - ((uintptr_t) Base & ~(getpagesize() - 1));

    DebugF("alignment offset: %lx\n", (unsigned long) alignOff);
    munmap((void *) ((uintptr_t) Base - alignOff), (Size + alignOff));
}

/***************************************************************************/
/* I/O Permissions section                                                 */
/***************************************************************************/

#if defined(__powerpc__)
volatile unsigned char *ioBase = NULL;

#ifndef __NR_pciconfig_iobase
#define __NR_pciconfig_iobase	200
#endif

static Bool
hwEnableIO(void)
{
    int fd;
    unsigned int ioBase_phys = syscall(__NR_pciconfig_iobase, 2, 0, 0);

    fd = open("/dev/mem", O_RDWR);
    if (ioBase == NULL) {
        ioBase = (volatile unsigned char *) mmap(0, 0x20000,
                                                 PROT_READ | PROT_WRITE,
                                                 MAP_SHARED, fd, ioBase_phys);
    }
    close(fd);

    return ioBase != MAP_FAILED;
}

static void
hwDisableIO(void)
{
    munmap(ioBase, 0x20000);
    ioBase = NULL;
}

#elif defined(__i386__) || defined(__x86_64__) || defined(__ia64__) || \
      defined(__alpha__)

static Bool
hwEnableIO(void)
{
    if (ioperm(0, 1024, 1) || iopl(3)) {
        ErrorF("xf86EnableIOPorts: failed to set IOPL for I/O (%s)\n",
               strerror(errno));
        return FALSE;
    }
#if !defined(__alpha__)
    /* XXX: this is actually not trapping anything because of iopl(3)
     * above */
    ioperm(0x40, 4, 0);         /* trap access to the timer chip */
    ioperm(0x60, 4, 0);         /* trap access to the keyboard controller */
#endif

    return TRUE;
}

static void
hwDisableIO(void)
{
    iopl(0);
    ioperm(0, 1024, 0);
}

#else /* non-IO architectures */

#define hwEnableIO() TRUE
#define hwDisableIO() do {} while (0)

#endif

Bool
xf86EnableIO(void)
{
    if (ExtendedEnabled)
        return TRUE;

    ExtendedEnabled = hwEnableIO();

    return ExtendedEnabled;
}

void
xf86DisableIO(void)
{
    if (!ExtendedEnabled)
        return;

    hwDisableIO();

    ExtendedEnabled = FALSE;
}

#if defined (__alpha__)

#define vuip    volatile unsigned int *

extern int readDense8(void *Base, register unsigned long Offset);
extern int readDense16(void *Base, register unsigned long Offset);
extern int readDense32(void *Base, register unsigned long Offset);
extern void
 writeDenseNB8(int Value, void *Base, register unsigned long Offset);
extern void
 writeDenseNB16(int Value, void *Base, register unsigned long Offset);
extern void
 writeDenseNB32(int Value, void *Base, register unsigned long Offset);
extern void
 writeDense8(int Value, void *Base, register unsigned long Offset);
extern void
 writeDense16(int Value, void *Base, register unsigned long Offset);
extern void
 writeDense32(int Value, void *Base, register unsigned long Offset);

static int readSparse8(void *Base, register unsigned long Offset);
static int readSparse16(void *Base, register unsigned long Offset);
static int readSparse32(void *Base, register unsigned long Offset);
static void
 writeSparseNB8(int Value, void *Base, register unsigned long Offset);
static void
 writeSparseNB16(int Value, void *Base, register unsigned long Offset);
static void
 writeSparseNB32(int Value, void *Base, register unsigned long Offset);
static void
 writeSparse8(int Value, void *Base, register unsigned long Offset);
static void
 writeSparse16(int Value, void *Base, register unsigned long Offset);
static void
 writeSparse32(int Value, void *Base, register unsigned long Offset);

#define DENSE_BASE	0x2ff00000000UL
#define SPARSE_BASE	0x30000000000UL

static unsigned long msb_set = 0;

static void *
mapVidMemSparse(int ScreenNum, unsigned long Base, unsigned long Size,
                int flags)
{
    int fd, prot;
    unsigned long ret, rets = 0;

    static Bool was_here = FALSE;

    if (!was_here) {
        was_here = TRUE;

        xf86WriteMmio8 = writeSparse8;
        xf86WriteMmio16 = writeSparse16;
        xf86WriteMmio32 = writeSparse32;
        xf86WriteMmioNB8 = writeSparseNB8;
        xf86WriteMmioNB16 = writeSparseNB16;
        xf86WriteMmioNB32 = writeSparseNB32;
        xf86ReadMmio8 = readSparse8;
        xf86ReadMmio16 = readSparse16;
        xf86ReadMmio32 = readSparse32;
    }

    fd = open(DEV_MEM, (flags & VIDMEM_READONLY) ? O_RDONLY : O_RDWR);
    if (fd < 0) {
        FatalError("xf86MapVidMem: failed to open " DEV_MEM " (%s)\n",
                   strerror(errno));
    }

#if 0
    xf86Msg(X_INFO, "mapVidMemSparse: try Base 0x%lx size 0x%lx flags 0x%x\n",
            Base, Size, flags);
#endif

    if (flags & VIDMEM_READONLY)
        prot = PROT_READ;
    else
        prot = PROT_READ | PROT_WRITE;

    /* This requirers linux-0.99.pl10 or above */

    /*
     * Always do DENSE mmap, since read32/write32 currently require it.
     */
    ret = (unsigned long) mmap((caddr_t) (DENSE_BASE + Base), Size,
                               prot, MAP_SHARED, fd, (off_t) (bus_base + Base));

    /*
     * Do SPARSE mmap only when MMIO and not MMIO_32BIT, or FRAMEBUFFER
     * and SPARSE (which should require the use of read/write macros).
     *
     * By not SPARSE mmapping an 8MB framebuffer, we can save approx. 256K
     * bytes worth of pagetable (32 pages).
     */
    if (((flags & VIDMEM_MMIO) && !(flags & VIDMEM_MMIO_32BIT)) ||
        ((flags & VIDMEM_FRAMEBUFFER) && (flags & VIDMEM_SPARSE))) {
        rets = (unsigned long) mmap((caddr_t) (SPARSE_BASE + (Base << 5)),
                                    Size << 5, prot, MAP_SHARED, fd,
                                    (off_t) _bus_base_sparse() + (Base << 5));
    }

    close(fd);

    if (ret == (unsigned long) MAP_FAILED) {
        FatalError("xf86MapVidMemSparse: Could not (dense) mmap fb (%s)\n",
                   strerror(errno));
    }

    if (((flags & VIDMEM_MMIO) && !(flags & VIDMEM_MMIO_32BIT)) ||
        ((flags & VIDMEM_FRAMEBUFFER) && (flags & VIDMEM_SPARSE))) {
        if (rets == (unsigned long) MAP_FAILED ||
            rets != (SPARSE_BASE + (Base << 5))) {
            FatalError("mapVidMemSparse: Could not (sparse) mmap fb (%s)\n",
                       strerror(errno));
        }
    }

#if 1
    if (rets)
        xf86Msg(X_INFO, "mapVidMemSparse: mapped Base 0x%lx size 0x%lx"
                " to DENSE at 0x%lx and SPARSE at 0x%lx\n",
                Base, Size, ret, rets);
    else
        xf86Msg(X_INFO, "mapVidMemSparse: mapped Base 0x%lx size 0x%lx"
                " to DENSE only at 0x%lx\n", Base, Size, ret);

#endif
    return (void *) ret;
}

static void
unmapVidMemSparse(int ScreenNum, void *Base, unsigned long Size)
{
    unsigned long Offset = (unsigned long) Base - DENSE_BASE;

#if 1
    xf86Msg(X_INFO, "unmapVidMemSparse: unmapping Base 0x%lx Size 0x%lx\n",
            Base, Size);
#endif
    /* Unmap DENSE always. */
    munmap((caddr_t) Base, Size);

    /* Unmap SPARSE always, and ignore error in case we did not map it. */
    munmap((caddr_t) (SPARSE_BASE + (Offset << 5)), Size << 5);
}

static int
readSparse8(void *Base, register unsigned long Offset)
{
    register unsigned long result, shift;
    register unsigned long msb;

    mem_barrier();
    Offset += (unsigned long) Base - DENSE_BASE;
    shift = (Offset & 0x3) << 3;
    if (Offset >= (hae_thresh)) {
        msb = Offset & hae_mask;
        Offset -= msb;
        if (msb_set != msb) {
            sethae(msb);
            msb_set = msb;
        }
    }

    mem_barrier();
    result = *(vuip) (SPARSE_BASE + (Offset << 5));
    result >>= shift;
    return 0xffUL & result;
}

static int
readSparse16(void *Base, register unsigned long Offset)
{
    register unsigned long result, shift;
    register unsigned long msb;

    mem_barrier();
    Offset += (unsigned long) Base - DENSE_BASE;
    shift = (Offset & 0x2) << 3;
    if (Offset >= hae_thresh) {
        msb = Offset & hae_mask;
        Offset -= msb;
        if (msb_set != msb) {
            sethae(msb);
            msb_set = msb;
        }
    }

    mem_barrier();
    result = *(vuip) (SPARSE_BASE + (Offset << 5) + (1 << (5 - 2)));
    result >>= shift;
    return 0xffffUL & result;
}

static int
readSparse32(void *Base, register unsigned long Offset)
{
    /* NOTE: this is really using DENSE. */
    mem_barrier();
    return *(vuip) ((unsigned long) Base + (Offset));
}

static void
writeSparse8(int Value, void *Base, register unsigned long Offset)
{
    register unsigned long msb;
    register unsigned int b = Value & 0xffU;

    write_mem_barrier();
    Offset += (unsigned long) Base - DENSE_BASE;
    if (Offset >= hae_thresh) {
        msb = Offset & hae_mask;
        Offset -= msb;
        if (msb_set != msb) {
            sethae(msb);
            msb_set = msb;
        }
    }

    write_mem_barrier();
    *(vuip) (SPARSE_BASE + (Offset << 5)) = b * 0x01010101;
}

static void
writeSparse16(int Value, void *Base, register unsigned long Offset)
{
    register unsigned long msb;
    register unsigned int w = Value & 0xffffU;

    write_mem_barrier();
    Offset += (unsigned long) Base - DENSE_BASE;
    if (Offset >= hae_thresh) {
        msb = Offset & hae_mask;
        Offset -= msb;
        if (msb_set != msb) {
            sethae(msb);
            msb_set = msb;
        }
    }

    write_mem_barrier();
    *(vuip) (SPARSE_BASE + (Offset << 5) + (1 << (5 - 2))) = w * 0x00010001;
}

static void
writeSparse32(int Value, void *Base, register unsigned long Offset)
{
    /* NOTE: this is really using DENSE. */
    write_mem_barrier();
    *(vuip) ((unsigned long) Base + (Offset)) = Value;
    return;
}

static void
writeSparseNB8(int Value, void *Base, register unsigned long Offset)
{
    register unsigned long msb;
    register unsigned int b = Value & 0xffU;

    Offset += (unsigned long) Base - DENSE_BASE;
    if (Offset >= hae_thresh) {
        msb = Offset & hae_mask;
        Offset -= msb;
        if (msb_set != msb) {
            sethae(msb);
            msb_set = msb;
        }
    }
    *(vuip) (SPARSE_BASE + (Offset << 5)) = b * 0x01010101;
}

static void
writeSparseNB16(int Value, void *Base, register unsigned long Offset)
{
    register unsigned long msb;
    register unsigned int w = Value & 0xffffU;

    Offset += (unsigned long) Base - DENSE_BASE;
    if (Offset >= hae_thresh) {
        msb = Offset & hae_mask;
        Offset -= msb;
        if (msb_set != msb) {
            sethae(msb);
            msb_set = msb;
        }
    }
    *(vuip) (SPARSE_BASE + (Offset << 5) + (1 << (5 - 2))) = w * 0x00010001;
}

static void
writeSparseNB32(int Value, void *Base, register unsigned long Offset)
{
    /* NOTE: this is really using DENSE. */
    *(vuip) ((unsigned long) Base + (Offset)) = Value;
    return;
}

void (*xf86WriteMmio8) (int Value, void *Base, unsigned long Offset)
    = writeDense8;
void (*xf86WriteMmio16) (int Value, void *Base, unsigned long Offset)
    = writeDense16;
void (*xf86WriteMmio32) (int Value, void *Base, unsigned long Offset)
    = writeDense32;
void (*xf86WriteMmioNB8) (int Value, void *Base, unsigned long Offset)
    = writeDenseNB8;
void (*xf86WriteMmioNB16) (int Value, void *Base, unsigned long Offset)
    = writeDenseNB16;
void (*xf86WriteMmioNB32) (int Value, void *Base, unsigned long Offset)
    = writeDenseNB32;
int (*xf86ReadMmio8) (void *Base, unsigned long Offset)
    = readDense8;
int (*xf86ReadMmio16) (void *Base, unsigned long Offset)
    = readDense16;
int (*xf86ReadMmio32) (void *Base, unsigned long Offset)
    = readDense32;

#endif                          /* __alpha__ */
