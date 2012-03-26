/*
 * Copyright 1992 by Rich Murphey <Rich@Rice.edu>
 * Copyright 1993 by David Wexelblat <dwex@goblin.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of Rich Murphey and David Wexelblat 
 * not be used in advertising or publicity pertaining to distribution of 
 * the software without specific, written prior permission.  Rich Murphey and
 * David Wexelblat make no representations about the suitability of this 
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 *
 * RICH MURPHEY AND DAVID WEXELBLAT DISCLAIM ALL WARRANTIES WITH REGARD TO 
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND 
 * FITNESS, IN NO EVENT SHALL RICH MURPHEY OR DAVID WEXELBLAT BE LIABLE FOR 
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER 
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF 
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN 
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "xf86.h"
#include "xf86Priv.h"

#include <errno.h>
#include <sys/mman.h>

#ifdef HAS_MTRR_SUPPORT
#ifndef __NetBSD__
#include <sys/types.h>
#include <sys/memrange.h>
#else
#include "memrange.h"
#endif
#define X_MTRR_ID "XFree86"
#endif

#if defined(HAS_MTRR_BUILTIN) && defined(__NetBSD__)
#include <machine/mtrr.h>
#include <machine/sysarch.h>
#include <sys/queue.h>
#ifdef __x86_64__
#define i386_set_mtrr x86_64_set_mtrr
#define i386_get_mtrr x86_64_get_mtrr
#define i386_iopl x86_64_iopl
#endif
#endif

#include "xf86_OSlib.h"
#include "xf86OSpriv.h"

#if defined(__NetBSD__) && !defined(MAP_FILE)
#define MAP_FLAGS MAP_SHARED
#else
#define MAP_FLAGS (MAP_FILE | MAP_SHARED)
#endif

#ifdef __OpenBSD__
#define SYSCTL_MSG "\tCheck that you have set 'machdep.allowaperture=1'\n"\
		   "\tin /etc/sysctl.conf and reboot your machine\n" \
		   "\trefer to xf86(4) for details"
#define SYSCTL_MSG2 \
		"Check that you have set 'machdep.allowaperture=2'\n" \
		"\tin /etc/sysctl.conf and reboot your machine\n" \
		"\trefer to xf86(4) for details"
#endif

/***************************************************************************/
/* Video Memory Mapping section                                            */
/***************************************************************************/

static Bool useDevMem = FALSE;
static int devMemFd = -1;

#ifdef HAS_APERTURE_DRV
#define DEV_APERTURE "/dev/xf86"
#endif

static pointer mapVidMem(int, unsigned long, unsigned long, int);
static void unmapVidMem(int, pointer, unsigned long);

#ifdef HAS_MTRR_SUPPORT
static pointer setWC(int, unsigned long, unsigned long, Bool, MessageType);
static void undoWC(int, pointer);
static Bool cleanMTRR(void);
#endif
#if defined(HAS_MTRR_BUILTIN) && defined(__NetBSD__)
static pointer NetBSDsetWC(int, unsigned long, unsigned long, Bool,
                           MessageType);
static void NetBSDundoWC(int, pointer);
#endif

/*
 * Check if /dev/mem can be mmap'd.  If it can't print a warning when
 * "warn" is TRUE.
 */
static void
checkDevMem(Bool warn)
{
    static Bool devMemChecked = FALSE;
    int fd;
    pointer base;

    if (devMemChecked)
        return;
    devMemChecked = TRUE;

    if ((fd = open(DEV_MEM, O_RDWR)) >= 0) {
        /* Try to map a page at the VGA address */
        base = mmap((caddr_t) 0, 4096, PROT_READ | PROT_WRITE,
                    MAP_FLAGS, fd, (off_t) 0xA0000);

        if (base != MAP_FAILED) {
            munmap((caddr_t) base, 4096);
            devMemFd = fd;
            useDevMem = TRUE;
            return;
        }
        else {
            /* This should not happen */
            if (warn) {
                xf86Msg(X_WARNING, "checkDevMem: failed to mmap %s (%s)\n",
                        DEV_MEM, strerror(errno));
            }
            useDevMem = FALSE;
            return;
        }
    }
#ifndef HAS_APERTURE_DRV
    if (warn) {
        xf86Msg(X_WARNING, "checkDevMem: failed to open %s (%s)\n",
                DEV_MEM, strerror(errno));
    }
    useDevMem = FALSE;
    return;
#else
    /* Failed to open /dev/mem, try the aperture driver */
    if ((fd = open(DEV_APERTURE, O_RDWR)) >= 0) {
        /* Try to map a page at the VGA address */
        base = mmap((caddr_t) 0, 4096, PROT_READ | PROT_WRITE,
                    MAP_FLAGS, fd, (off_t) 0xA0000);

        if (base != MAP_FAILED) {
            munmap((caddr_t) base, 4096);
            devMemFd = fd;
            useDevMem = TRUE;
            xf86Msg(X_INFO, "checkDevMem: using aperture driver %s\n",
                    DEV_APERTURE);
            return;
        }
        else {

            if (warn) {
                xf86Msg(X_WARNING, "checkDevMem: failed to mmap %s (%s)\n",
                        DEV_APERTURE, strerror(errno));
            }
        }
    }
    else {
        if (warn) {
#ifndef __OpenBSD__
            xf86Msg(X_WARNING, "checkDevMem: failed to open %s and %s\n"
                    "\t(%s)\n", DEV_MEM, DEV_APERTURE, strerror(errno));
#else                           /* __OpenBSD__ */
            xf86Msg(X_WARNING, "checkDevMem: failed to open %s and %s\n"
                    "\t(%s)\n%s", DEV_MEM, DEV_APERTURE, strerror(errno),
                    SYSCTL_MSG);
#endif                          /* __OpenBSD__ */
        }
    }

    useDevMem = FALSE;
    return;

#endif
}

void
xf86OSInitVidMem(VidMemInfoPtr pVidMem)
{
    checkDevMem(TRUE);
    pVidMem->linearSupported = useDevMem;
    pVidMem->mapMem = mapVidMem;
    pVidMem->unmapMem = unmapVidMem;

    if (useDevMem)
        pci_system_init_dev_mem(devMemFd);

#ifdef HAS_MTRR_SUPPORT
    if (useDevMem) {
        if (cleanMTRR()) {
            pVidMem->setWC = setWC;
            pVidMem->undoWC = undoWC;
        }
    }
#endif
#if defined(HAS_MTRR_BUILTIN) && defined(__NetBSD__)
    pVidMem->setWC = NetBSDsetWC;
    pVidMem->undoWC = NetBSDundoWC;
#endif
    pVidMem->initialised = TRUE;
}

static pointer
mapVidMem(int ScreenNum, unsigned long Base, unsigned long Size, int flags)
{
    pointer base;

    checkDevMem(FALSE);

    if (useDevMem) {
        if (devMemFd < 0) {
            FatalError("xf86MapVidMem: failed to open %s (%s)",
                       DEV_MEM, strerror(errno));
        }
        base = mmap((caddr_t) 0, Size,
                    (flags & VIDMEM_READONLY) ?
                    PROT_READ : (PROT_READ | PROT_WRITE),
                    MAP_FLAGS, devMemFd, (off_t) Base);
        if (base == MAP_FAILED) {
            FatalError("%s: could not mmap %s [s=%lx,a=%lx] (%s)",
                       "xf86MapVidMem", DEV_MEM, Size, Base, strerror(errno));
        }
        return base;
    }

    /* else, mmap /dev/vga */
    if ((unsigned long) Base < 0xA0000 || (unsigned long) Base >= 0xC0000) {
        FatalError("%s: Address 0x%lx outside allowable range",
                   "xf86MapVidMem", Base);
    }
    base = mmap(0, Size,
                (flags & VIDMEM_READONLY) ?
                PROT_READ : (PROT_READ | PROT_WRITE),
                MAP_FLAGS, xf86Info.consoleFd, (unsigned long) Base - 0xA0000);
    if (base == MAP_FAILED) {
        FatalError("xf86MapVidMem: Could not mmap /dev/vga (%s)",
                   strerror(errno));
    }
    return base;
}

static void
unmapVidMem(int ScreenNum, pointer Base, unsigned long Size)
{
    munmap((caddr_t) Base, Size);
}

/*
 * Read BIOS via mmap()ing DEV_MEM
 */

int
xf86ReadBIOS(unsigned long Base, unsigned long Offset, unsigned char *Buf,
             int Len)
{
    unsigned char *ptr;
    int psize;
    int mlen;

    checkDevMem(TRUE);
    if (devMemFd == -1) {
        return -1;
    }

    psize = getpagesize();
    Offset += Base & (psize - 1);
    Base &= ~(psize - 1);
    mlen = (Offset + Len + psize - 1) & ~(psize - 1);
    ptr = (unsigned char *) mmap((caddr_t) 0, mlen, PROT_READ,
                                 MAP_SHARED, devMemFd, (off_t) Base);
    if ((long) ptr == -1) {
        xf86Msg(X_WARNING,
                "xf86ReadBIOS: %s mmap[s=%x,a=%lx,o=%lx] failed (%s)\n",
                DEV_MEM, Len, Base, Offset, strerror(errno));
#ifdef __OpenBSD__
        if (Base < 0xa0000) {
            xf86Msg(X_WARNING, SYSCTL_MSG2);
        }
#endif
        return -1;
    }
#ifdef DEBUG
    ErrorF("xf86ReadBIOS: BIOS at 0x%08x has signature 0x%04x\n",
           Base, ptr[0] | (ptr[1] << 8));
#endif
    (void) memcpy(Buf, (void *) (ptr + Offset), Len);
    (void) munmap((caddr_t) ptr, mlen);
#ifdef DEBUG
    xf86MsgVerb(X_INFO, 3, "xf86ReadBIOS(%x, %x, Buf, %x)"
                "-> %02x %02x %02x %02x...\n",
                Base, Offset, Len, Buf[0], Buf[1], Buf[2], Buf[3]);
#endif
    return Len;
}

#ifdef USE_I386_IOPL
/***************************************************************************/
/* I/O Permissions section                                                 */
/***************************************************************************/

static Bool ExtendedEnabled = FALSE;

Bool
xf86EnableIO()
{
    if (ExtendedEnabled)
        return TRUE;

    if (i386_iopl(TRUE) < 0) {
#ifndef __OpenBSD__
        xf86Msg(X_WARNING, "%s: Failed to set IOPL for extended I/O",
                "xf86EnableIO");
#else
        xf86Msg(X_WARNING, "%s: Failed to set IOPL for extended I/O\n%s",
                "xf86EnableIO", SYSCTL_MSG);
#endif
        return FALSE;
    }
    ExtendedEnabled = TRUE;

    return TRUE;
}

void
xf86DisableIO()
{
    if (!ExtendedEnabled)
        return;

    i386_iopl(FALSE);
    ExtendedEnabled = FALSE;

    return;
}

#endif                          /* USE_I386_IOPL */

#ifdef USE_AMD64_IOPL
/***************************************************************************/
/* I/O Permissions section                                                 */
/***************************************************************************/

static Bool ExtendedEnabled = FALSE;

Bool
xf86EnableIO()
{
    if (ExtendedEnabled)
        return TRUE;

    if (amd64_iopl(TRUE) < 0) {
#ifndef __OpenBSD__
        xf86Msg(X_WARNING, "%s: Failed to set IOPL for extended I/O",
                "xf86EnableIO");
#else
        xf86Msg(X_WARNING, "%s: Failed to set IOPL for extended I/O\n%s",
                "xf86EnableIO", SYSCTL_MSG);
#endif
        return FALSE;
    }
    ExtendedEnabled = TRUE;

    return TRUE;
}

void
xf86DisableIO()
{
    if (!ExtendedEnabled)
        return;

    if (amd64_iopl(FALSE) == 0) {
        ExtendedEnabled = FALSE;
    }
    /* Otherwise, the X server has revoqued its root uid, 
       and thus cannot give up IO privileges any more */

    return;
}

#endif                          /* USE_AMD64_IOPL */

#ifdef USE_DEV_IO
static int IoFd = -1;

Bool
xf86EnableIO()
{
    if (IoFd >= 0)
        return TRUE;

    if ((IoFd = open("/dev/io", O_RDWR)) == -1) {
        xf86Msg(X_WARNING, "xf86EnableIO: "
                "Failed to open /dev/io for extended I/O");
        return FALSE;
    }
    return TRUE;
}

void
xf86DisableIO()
{
    if (IoFd < 0)
        return;

    close(IoFd);
    IoFd = -1;
    return;
}

#endif

#ifdef __NetBSD__
/***************************************************************************/
/* Set TV output mode                                                      */
/***************************************************************************/
void
xf86SetTVOut(int mode)
{
    switch (xf86Info.consType) {
#ifdef PCCONS_SUPPORT
    case PCCONS:{

        if (ioctl(xf86Info.consoleFd, CONSOLE_X_TV_ON, &mode) < 0) {
            xf86Msg(X_WARNING,
                    "xf86SetTVOut: Could not set console to TV output, %s\n",
                    strerror(errno));
        }
    }
        break;
#endif                          /* PCCONS_SUPPORT */

    default:
        FatalError("Xf86SetTVOut: Unsupported console");
        break;
    }
    return;
}

void
xf86SetRGBOut()
{
    switch (xf86Info.consType) {
#ifdef PCCONS_SUPPORT
    case PCCONS:{

        if (ioctl(xf86Info.consoleFd, CONSOLE_X_TV_OFF, 0) < 0) {
            xf86Msg(X_WARNING,
                    "xf86SetTVOut: Could not set console to RGB output, %s\n",
                    strerror(errno));
        }
    }
        break;
#endif                          /* PCCONS_SUPPORT */

    default:
        FatalError("Xf86SetTVOut: Unsupported console");
        break;
    }
    return;
}
#endif

#ifdef HAS_MTRR_SUPPORT
/* memory range (MTRR) support for FreeBSD */

/*
 * This code is experimental.  Some parts may be overkill, and other parts
 * may be incomplete.
 */

/*
 * getAllRanges returns the full list of memory ranges with attributes set.
 */

static struct mem_range_desc *
getAllRanges(int *nmr)
{
    struct mem_range_desc *mrd;
    struct mem_range_op mro;

    /*
     * Find how many ranges there are.  If this fails, then the kernel
     * probably doesn't have MTRR support.
     */
    mro.mo_arg[0] = 0;
    if (ioctl(devMemFd, MEMRANGE_GET, &mro))
        return NULL;
    *nmr = mro.mo_arg[0];
    mrd = xnfalloc(*nmr * sizeof(struct mem_range_desc));
    mro.mo_arg[0] = *nmr;
    mro.mo_desc = mrd;
    if (ioctl(devMemFd, MEMRANGE_GET, &mro)) {
        free(mrd);
        return NULL;
    }
    return mrd;
}

/*
 * cleanMTRR removes any memory attribute that may be left by a previous
 * X server.  Normally there won't be any, but this takes care of the
 * case where a server crashed without being able finish cleaning up.
 */

static Bool
cleanMTRR()
{
    struct mem_range_desc *mrd;
    struct mem_range_op mro;
    int nmr, i;

    /* This shouldn't happen */
    if (devMemFd < 0)
        return FALSE;

    if (!(mrd = getAllRanges(&nmr)))
        return FALSE;

    for (i = 0; i < nmr; i++) {
        if (strcmp(mrd[i].mr_owner, X_MTRR_ID) == 0 &&
            (mrd[i].mr_flags & MDF_ACTIVE)) {
#ifdef DEBUG
            ErrorF("Clean for (0x%lx,0x%lx)\n",
                   (unsigned long) mrd[i].mr_base,
                   (unsigned long) mrd[i].mr_len);
#endif
            if (mrd[i].mr_flags & MDF_FIXACTIVE) {
                mro.mo_arg[0] = MEMRANGE_SET_UPDATE;
                mrd[i].mr_flags = MDF_UNCACHEABLE;
            }
            else {
                mro.mo_arg[0] = MEMRANGE_SET_REMOVE;
            }
            mro.mo_desc = mrd + i;
            ioctl(devMemFd, MEMRANGE_SET, &mro);
        }
    }
#ifdef DEBUG
    sleep(10);
#endif
    free(mrd);
    return TRUE;
}

typedef struct x_RangeRec {
    struct mem_range_desc mrd;
    Bool wasWC;
    struct x_RangeRec *next;
} RangeRec, *RangePtr;

static void
freeRangeList(RangePtr range)
{
    RangePtr rp;

    while (range) {
        rp = range;
        range = rp->next;
        free(rp);
    }
}

static RangePtr
dupRangeList(RangePtr list)
{
    RangePtr new = NULL, rp, p;

    rp = list;
    while (rp) {
        p = xnfalloc(sizeof(RangeRec));
        *p = *rp;
        p->next = new;
        new = p;
        rp = rp->next;
    }
    return new;
}

static RangePtr
sortRangeList(RangePtr list)
{
    RangePtr rp1, rp2, copy, sorted = NULL, minp, prev, minprev;
    unsigned long minBase;

    /* Sort by base address */
    rp1 = copy = dupRangeList(list);
    while (rp1) {
        minBase = rp1->mrd.mr_base;
        minp = rp1;
        minprev = NULL;
        prev = rp1;
        rp2 = rp1->next;
        while (rp2) {
            if (rp2->mrd.mr_base < minBase) {
                minBase = rp2->mrd.mr_base;
                minp = rp2;
                minprev = prev;
            }
            prev = rp2;
            rp2 = rp2->next;
        }
        if (minprev) {
            minprev->next = minp->next;
            rp1 = copy;
        }
        else {
            rp1 = minp->next;
        }
        minp->next = sorted;
        sorted = minp;
    }
    return sorted;
}

/*
 * findRanges returns a list of ranges that overlap the specified range.
 */

static void
findRanges(unsigned long base, unsigned long size, RangePtr * ucp,
           RangePtr * wcp)
{
    struct mem_range_desc *mrd;
    int nmr, i;
    RangePtr rp, *p;

    if (!(mrd = getAllRanges(&nmr)))
        return;

    for (i = 0; i < nmr; i++) {
        if ((mrd[i].mr_flags & MDF_ACTIVE) &&
            mrd[i].mr_base < base + size &&
            mrd[i].mr_base + mrd[i].mr_len > base) {
            if (mrd[i].mr_flags & MDF_WRITECOMBINE)
                p = wcp;
            else if (mrd[i].mr_flags & MDF_UNCACHEABLE)
                p = ucp;
            else
                continue;
            rp = xnfalloc(sizeof(RangeRec));
            rp->mrd = mrd[i];
            rp->next = *p;
            *p = rp;
        }
    }
    free(mrd);
}

/*
 * This checks if the existing overlapping ranges fully cover the requested
 * range.  Is this overkill?
 */

static Bool
fullCoverage(unsigned long base, unsigned long size, RangePtr overlap)
{
    RangePtr rp1, sorted = NULL;
    unsigned long end;

    sorted = sortRangeList(overlap);
    /* Look for gaps */
    rp1 = sorted;
    end = base + size;
    while (rp1) {
        if (rp1->mrd.mr_base > base) {
            freeRangeList(sorted);
            return FALSE;
        }
        else {
            base = rp1->mrd.mr_base + rp1->mrd.mr_len;
        }
        if (base >= end) {
            freeRangeList(sorted);
            return TRUE;
        }
        rp1 = rp1->next;
    }
    freeRangeList(sorted);
    return FALSE;
}

static pointer
addWC(int screenNum, unsigned long base, unsigned long size, MessageType from)
{
    RangePtr uc = NULL, wc = NULL, retlist = NULL;
    struct mem_range_desc mrd;
    struct mem_range_op mro;

    findRanges(base, size, &uc, &wc);

    /* See of the full range is already WC */
    if (!uc && fullCoverage(base, size, wc)) {
        xf86DrvMsg(screenNum, from,
                   "Write-combining range (0x%lx,0x%lx) was already set\n",
                   base, size);
        return NULL;
    }

    /* Otherwise, try to add the new range */
    mrd.mr_base = base;
    mrd.mr_len = size;
    strcpy(mrd.mr_owner, X_MTRR_ID);
    mrd.mr_flags = MDF_WRITECOMBINE;
    mro.mo_desc = &mrd;
    mro.mo_arg[0] = MEMRANGE_SET_UPDATE;
    if (ioctl(devMemFd, MEMRANGE_SET, &mro)) {
        xf86DrvMsg(screenNum, X_WARNING,
                   "Failed to set write-combining range "
                   "(0x%lx,0x%lx)\n", base, size);
        return NULL;
    }
    else {
        xf86DrvMsg(screenNum, from,
                   "Write-combining range (0x%lx,0x%lx)\n", base, size);
        retlist = xnfalloc(sizeof(RangeRec));
        retlist->mrd = mrd;
        retlist->wasWC = FALSE;
        retlist->next = NULL;
        return retlist;
    }
}

static pointer
delWC(int screenNum, unsigned long base, unsigned long size, MessageType from)
{
    RangePtr uc = NULL, wc = NULL, retlist = NULL;
    struct mem_range_desc mrd;
    struct mem_range_op mro;

    findRanges(base, size, &uc, &wc);

    /*
     * See of the full range is already not WC, or if there is full
     * coverage from UC ranges.
     */
    if (!wc || fullCoverage(base, size, uc)) {
        xf86DrvMsg(screenNum, from,
                   "Write-combining range (0x%lx,0x%lx) was already clear\n",
                   base, size);
        return NULL;
    }

    /* Otherwise, try to add the new range */
    mrd.mr_base = base;
    mrd.mr_len = size;
    strcpy(mrd.mr_owner, X_MTRR_ID);
    mrd.mr_flags = MDF_UNCACHEABLE;
    mro.mo_desc = &mrd;
    mro.mo_arg[0] = MEMRANGE_SET_UPDATE;
    if (ioctl(devMemFd, MEMRANGE_SET, &mro)) {
        xf86DrvMsg(screenNum, X_WARNING,
                   "Failed to remove write-combining range "
                   "(0x%lx,0x%lx)\n", base, size);
        /* XXX Should then remove all of the overlapping WC ranges */
        return NULL;
    }
    else {
        xf86DrvMsg(screenNum, from,
                   "Removed Write-combining range (0x%lx,0x%lx)\n", base, size);
        retlist = xnfalloc(sizeof(RangeRec));
        retlist->mrd = mrd;
        retlist->wasWC = TRUE;
        retlist->next = NULL;
        return retlist;
    }
}

static pointer
setWC(int screenNum, unsigned long base, unsigned long size, Bool enable,
      MessageType from)
{
    if (enable)
        return addWC(screenNum, base, size, from);
    else
        return delWC(screenNum, base, size, from);
}

static void
undoWC(int screenNum, pointer list)
{
    RangePtr rp;
    struct mem_range_op mro;
    Bool failed;

    rp = list;
    while (rp) {
#ifdef DEBUG
        ErrorF("Undo for (0x%lx,0x%lx), %d\n",
               (unsigned long) rp->mrd.mr_base,
               (unsigned long) rp->mrd.mr_len, rp->wasWC);
#endif
        failed = FALSE;
        if (rp->wasWC) {
            mro.mo_arg[0] = MEMRANGE_SET_UPDATE;
            rp->mrd.mr_flags = MDF_WRITECOMBINE;
            strcpy(rp->mrd.mr_owner, "unknown");
        }
        else {
            mro.mo_arg[0] = MEMRANGE_SET_REMOVE;
        }
        mro.mo_desc = &rp->mrd;

        if (ioctl(devMemFd, MEMRANGE_SET, &mro)) {
            if (!rp->wasWC) {
                mro.mo_arg[0] = MEMRANGE_SET_UPDATE;
                rp->mrd.mr_flags = MDF_UNCACHEABLE;
                strcpy(rp->mrd.mr_owner, "unknown");
                if (ioctl(devMemFd, MEMRANGE_SET, &mro))
                    failed = TRUE;
            }
            else
                failed = TRUE;
        }
        if (failed) {
            xf86DrvMsg(screenNum, X_WARNING,
                       "Failed to restore MTRR range (0x%lx,0x%lx)\n",
                       (unsigned long) rp->mrd.mr_base,
                       (unsigned long) rp->mrd.mr_len);
        }
        rp = rp->next;
    }
}

#endif                          /* HAS_MTRR_SUPPORT */

#if defined(HAS_MTRR_BUILTIN) && defined(__NetBSD__)
static pointer
NetBSDsetWC(int screenNum, unsigned long base, unsigned long size, Bool enable,
            MessageType from)
{
    struct mtrr *mtrrp;
    int n;

    xf86DrvMsg(screenNum, X_WARNING,
               "%s MTRR %lx - %lx\n", enable ? "set" : "remove",
               base, (base + size));

    mtrrp = xnfalloc(sizeof(struct mtrr));
    mtrrp->base = base;
    mtrrp->len = size;
    mtrrp->type = MTRR_TYPE_WC;

    /*
     * MTRR_PRIVATE will make this MTRR get reset automatically
     * if this process exits, so we have no need for an explicit
     * cleanup operation when starting a new server.
     */

    if (enable)
        mtrrp->flags = MTRR_VALID | MTRR_PRIVATE;
    else
        mtrrp->flags = 0;
    n = 1;

    if (i386_set_mtrr(mtrrp, &n) < 0) {
        free(mtrrp);
        return NULL;
    }
    return mtrrp;
}

static void
NetBSDundoWC(int screenNum, pointer list)
{
    struct mtrr *mtrrp = (struct mtrr *) list;
    int n;

    if (mtrrp == NULL)
        return;
    n = 1;
    mtrrp->flags &= ~MTRR_VALID;
    i386_set_mtrr(mtrrp, &n);
    free(mtrrp);
}
#endif
