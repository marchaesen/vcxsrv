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

/*
 * The ARM32 code here carries the following copyright:
 *
 * Copyright 1997
 * Digital Equipment Corporation. All rights reserved.
 * This software is furnished under license and may be used and copied only in 
 * accordance with the following terms and conditions.  Subject to these
 * conditions, you may download, copy, install, use, modify and distribute
 * this software in source and/or binary form. No title or ownership is
 * transferred hereby.
 *
 * 1) Any source code used, modified or distributed must reproduce and retain
 *    this copyright notice and list of conditions as they appear in the
 *    source file.
 *
 * 2) No right is granted to use any trade name, trademark, or logo of Digital 
 *    Equipment Corporation. Neither the "Digital Equipment Corporation"
 *    name nor any trademark or logo of Digital Equipment Corporation may be
 *    used to endorse or promote products derived from this software without
 *    the prior written permission of Digital Equipment Corporation.
 *
 * 3) This software is provided "AS-IS" and any express or implied warranties,
 *    including but not limited to, any implied warranties of merchantability,
 *    fitness for a particular purpose, or non-infringement are disclaimed.
 *    In no event shall DIGITAL be liable for any damages whatsoever, and in
 *    particular, DIGITAL shall not be liable for special, indirect,
 *    consequential, or incidental damages or damages for lost profits, loss
 *    of revenue or loss of use, whether such damages arise in contract, 
 *    negligence, tort, under statute, in equity, at law or otherwise, even
 *    if advised of the possibility of such damage. 
 *
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"
#include "xf86OSpriv.h"

#ifdef __arm32__
#include "machine/devmap.h"
struct memAccess {
    int ioctl;
    struct map_info memInfo;
    pointer regionVirtBase;
    Bool Checked;
    Bool OK;
};

static pointer xf86MapInfoMap();
static void xf86MapInfoUnmap();
static struct memAccess *checkMapInfo();
extern int vgaPhysLinearBase;

/* A memAccess structure is needed for each possible region */
struct memAccess vgaMemInfo = { CONSOLE_GET_MEM_INFO, NULL, NULL,
    FALSE, FALSE
};

struct memAccess linearMemInfo = { CONSOLE_GET_LINEAR_INFO, NULL, NULL,
    FALSE, FALSE
};

struct memAccess ioMemInfo = { CONSOLE_GET_IO_INFO, NULL, NULL,
    FALSE, FALSE
};
#endif                          /* __arm32__ */

#if defined(__NetBSD__) && !defined(MAP_FILE)
#define MAP_FLAGS MAP_SHARED
#else
#define MAP_FLAGS (MAP_FILE | MAP_SHARED)
#endif

#define BUS_BASE	0L
#define BUS_BASE_BWX	0L

/***************************************************************************/
/* Video Memory Mapping section                                            */
/***************************************************************************/

static Bool useDevMem = FALSE;
static int devMemFd = -1;

static pointer mapVidMem(int, unsigned long, unsigned long, int);
static void unmapVidMem(int, pointer, unsigned long);

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
                    MAP_FLAGS, fd, (off_t) 0xA0000 + BUS_BASE);

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
    if (warn) {
        xf86Msg(X_WARNING, "checkDevMem: failed to open %s (%s)\n",
                DEV_MEM, strerror(errno));
    }
    useDevMem = FALSE;
    return;
}

void
xf86OSInitVidMem(VidMemInfoPtr pVidMem)
{

    checkDevMem(TRUE);
    pVidMem->linearSupported = useDevMem;
    pVidMem->mapMem = armMapVidMem;
    pVidMem->unmapVidMem = armUnmapVidMem;

    pVidMem->initialised = TRUE;
}

static pointer
mapVidMem(int ScreenNum, unsigned long Base, unsigned long Size, int flags)
{
    pointer base;

    checkDevMem(FALSE);

    if (useDevMem) {
        if (devMemFd < 0) {
            FatalError("xf86MapVidMem: failed to open %s (%s)\n",
                       DEV_MEM, strerror(errno));
        }
        base = mmap((caddr_t) 0, Size,
                    (flags & VIDMEM_READONLY) ?
                    PROT_READ : (PROT_READ | PROT_WRITE),
                    MAP_FLAGS, devMemFd, (off_t) Base + BUS_BASE_BWX);
        if (base == MAP_FAILED) {
            FatalError("%s: could not mmap %s [s=%x,a=%x] (%s)\n",
                       "xf86MapVidMem", DEV_MEM, Size, Base, strerror(errno));
        }
        return base;
    }

    /* else, mmap /dev/vga */
    if ((unsigned long) Base < 0xA0000 || (unsigned long) Base >= 0xC0000) {
        FatalError("%s: Address 0x%x outside allowable range\n",
                   "xf86MapVidMem", Base);
    }
    base = mmap(0, Size,
                (flags & VIDMEM_READONLY) ?
                PROT_READ : (PROT_READ | PROT_WRITE),
                MAP_FLAGS, xf86Info.consoleFd, (unsigned long) Base - 0xA0000);
    if (base == MAP_FAILED) {
        FatalError("xf86MapVidMem: Could not mmap /dev/vga (%s)\n",
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
                                 MAP_SHARED, devMemFd, (off_t) Base + BUS_BASE);
    if ((long) ptr == -1) {
        xf86Msg(X_WARNING,
                "xf86ReadBIOS: %s mmap[s=%x,a=%x,o=%x] failed (%s)\n",
                DEV_MEM, Len, Base, Offset, strerror(errno));
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

/* XXX This needs to be updated for the ND */

/*
** Find out whether the console driver provides memory mapping information 
** for the specified region and return the map_info pointer. Print a warning if required.
*/
static struct memAccess *
checkMapInfo(Bool warn, int Region)
{
    struct memAccess *memAccP;

    switch (Region) {
    case VGA_REGION:
        memAccP = &vgaMemInfo;
        break;

    case LINEAR_REGION:
        memAccP = &linearMemInfo;
        break;

    case MMIO_REGION:
        memAccP = &ioMemInfo;
        break;

    default:
        return NULL;
        break;
    }

    if (!memAccP->Checked) {
        if (ioctl(xf86Info.consoleFd, memAccP->ioctl, &(memAccP->memInfo)) ==
            -1) {
            if (warn) {
                xf86Msg(X_WARNING,
                        "checkMapInfo: failed to get map info for region %d\n\t(%s)\n",
                        Region, strerror(errno));
            }
        }
        else {
            if (memAccP->memInfo.u.map_info_mmap.map_offset != MAP_INFO_UNKNOWN)
                memAccP->OK = TRUE;
        }
        memAccP->Checked = TRUE;
    }
    if (memAccP->OK) {
        return memAccP;
    }
    else {
        return NULL;
    }
}

static pointer
xf86MapInfoMap(struct memAccess *memInfoP, pointer Base, unsigned long Size)
{
    struct map_info *mapInfoP = &(memInfoP->memInfo);

    if (mapInfoP->u.map_info_mmap.map_size == MAP_INFO_UNKNOWN) {
        Size = (unsigned long) Base + Size;
    }
    else {
        Size = mapInfoP->u.map_info_mmap.map_size;
    }

    switch (mapInfoP->method) {
    case MAP_MMAP:
        /* Need to remap if size is unknown because we may not have
           mapped the whole region initially */
        if (memInfoP->regionVirtBase == NULL ||
            mapInfoP->u.map_info_mmap.map_size == MAP_INFO_UNKNOWN) {
            if ((memInfoP->regionVirtBase =
                 mmap((caddr_t) 0,
                      Size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      xf86Info.consoleFd,
                      (unsigned long) mapInfoP->u.map_info_mmap.map_offset))
                == (pointer) -1) {
                FatalError
                    ("xf86MapInfoMap: Failed to map memory at 0x%x\n\t%s\n",
                     mapInfoP->u.map_info_mmap.map_offset, strerror(errno));
            }
            if (mapInfoP->u.map_info_mmap.internal_offset > 0)
                memInfoP->regionVirtBase +=
                    mapInfoP->u.map_info_mmap.internal_offset;
        }
        break;

    default:
        FatalError("xf86MapInfoMap: Unsuported mapping method\n");
        break;
    }

    return (pointer) ((int) memInfoP->regionVirtBase + (int) Base);
}

static void
xf86MapInfoUnmap(struct memAccess *memInfoP, unsigned long Size)
{
    struct map_info *mapInfoP = &(memInfoP->memInfo);

    switch (mapInfoP->method) {
    case MAP_MMAP:
        if (memInfoP->regionVirtBase != NULL) {
            if (mapInfoP->u.map_info_mmap.map_size != MAP_INFO_UNKNOWN)
                Size = mapInfoP->u.map_info_mmap.map_size;
            munmap((caddr_t) memInfoP->regionVirtBase, Size);
            memInfoP->regionVirtBase = NULL;
        }
        break;
    default:
        FatalError("xf86MapInfoMap: Unsuported mapping method\n");
        break;
    }
}

static pointer
armMapVidMem(int ScreenNum, unsigned long Base, unsigned long Size, int flags)
{
    struct memAccess *memInfoP;

    if ((memInfoP = checkMapInfo(FALSE, Region)) != NULL) {
        /*
         ** xf86 passes in a physical address offset from the start
         ** of physical memory, but xf86MapInfoMap expects an 
         ** offset from the start of the specified region - it gets 
         ** the physical address of the region from the display driver.
         */
        switch (Region) {
        case LINEAR_REGION:
            if (vgaPhysLinearBase) {
                Base -= vgaPhysLinearBase;
            }
            break;
        case VGA_REGION:
            Base -= 0xA0000;
            break;
        }

        base = xf86MapInfoMap(memInfoP, Base, Size);
        return base;
    }
    return mapVidMem(ScreenNum, Base, Size, flags);
}

static void
armUnmapVidMem(int ScreenNum, pointer Base, unsigned long Size)
{
    struct memAccess *memInfoP;

    if ((memInfoP = checkMapInfo(FALSE, Region)) != NULL) {
        xf86MapInfoUnmap(memInfoP, Base, Size);
    }
    unmapVidMem(ScreenNum, Base, Size);
}

#ifdef USE_DEV_IO
static int IoFd = -1;

Bool
xf86EnableIO()
{
    if (IoFd >= 0)
        return TRUE;

    if ((IoFd = open("/dev/io", O_RDWR)) == -1) {
        xf86Msg(X_WARNING, "xf86EnableIO: "
                "Failed to open /dev/io for extended I/O\n");
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

#if defined(USE_ARC_MMAP) || defined(__arm32__)

Bool
xf86EnableIO()
{
    int fd;
    pointer base;

    if (ExtendedEnabled)
        return TRUE;

    if ((fd = open("/dev/ttyC0", O_RDWR)) >= 0) {
        /* Try to map a page at the pccons I/O space */
        base = (pointer) mmap((caddr_t) 0, 65536, PROT_READ | PROT_WRITE,
                              MAP_FLAGS, fd, (off_t) 0x0000);

        if (base != (pointer) -1) {
            IOPortBase = base;
        }
        else {
            xf86Msg(X_WARNING, "EnableIO: failed to mmap %s (%s)\n",
                    "/dev/ttyC0", strerror(errno));
            return FALSE;
        }
    }
    else {
        xf86Msg("EnableIO: failed to open %s (%s)\n",
                "/dev/ttyC0", strerror(errno));
        return FALSE;
    }

    ExtendedEnabled = TRUE;

    return TRUE;
}

void
xf86DisableIO()
{
    return;
}

#endif                          /* USE_ARC_MMAP */

#if 0
/*
 * XXX This is here for reference.  It needs to be handled differently for the
 * ND.
 */
#if defined(USE_ARC_MMAP) || defined(__arm32__)

#ifdef USE_ARM32_MMAP
#define	DEV_MEM_IOBASE	0x43000000
#endif

static Bool ScreenEnabled[MAXSCREENS];
static Bool ExtendedEnabled = FALSE;
static Bool InitDone = FALSE;

Bool
xf86EnableIOPorts(ScreenNum)
int ScreenNum;
{
    int i;
    int fd;
    pointer base;

#ifdef __arm32__
    struct memAccess *memInfoP;
    int *Size;
#endif

    ScreenEnabled[ScreenNum] = TRUE;

    if (ExtendedEnabled)
        return TRUE;

#ifdef USE_ARC_MMAP
    if ((fd = open("/dev/ttyC0", O_RDWR)) >= 0) {
        /* Try to map a page at the pccons I/O space */
        base = (pointer) mmap((caddr_t) 0, 65536, PROT_READ | PROT_WRITE,
                              MAP_FLAGS, fd, (off_t) 0x0000);

        if (base != (pointer) -1) {
            IOPortBase = base;
        }
        else {
            xf86Msg(X_ERROR,
                    "EnableIOPorts: failed to mmap %s (%s)\n",
                    "/dev/ttyC0", strerror(errno));
        }
    }
    else {
        xf86Msg(X_ERROR, "EnableIOPorts: failed to open %s (%s)\n",
                "/dev/ttyC0", strerror(errno));
    }
#endif

#ifdef __arm32__
    IOPortBase = (unsigned int) -1;

    if ((memInfoP = checkMapInfo(TRUE, MMIO_REGION)) != NULL) {
        /* 
         * xf86MapInfoMap maps an offset from the start of video IO
         * space (e.g. 0x3B0), but IOPortBase is expected to map to
         * physical address 0x000, so subtract the start of video I/O
         * space from the result.  This is safe for now becase we
         * actually mmap the start of the page, then the start of video
         * I/O space is added as an internal offset.
         */
        IOPortBase = (unsigned int) xf86MapInfoMap(memInfoP, (caddr_t) 0x0, 0L)
            - memInfoP->memInfo.u.map_info_mmap.internal_offset;
        ExtendedEnabled = TRUE;
        return TRUE;
    }
#ifdef USE_ARM32_MMAP
    checkDevMem(TRUE);

    if (devMemFd >= 0 && useDevMem) {
        base = (pointer) mmap((caddr_t) 0, 0x400, PROT_READ | PROT_WRITE,
                              MAP_FLAGS, devMemFd, (off_t) DEV_MEM_IOBASE);

        if (base != (pointer) -1)
            IOPortBase = (unsigned int) base;
    }

    if (IOPortBase == (unsigned int) -1) {
        xf86Msg(X_WARNING,
                "xf86EnableIOPorts: failed to open mem device or map IO base. \n\
Make sure you have the Aperture Driver installed, or a kernel built with the INSECURE option\n");
        return FALSE;
    }
#else
    /* We don't have the IOBASE, so we can't map the address */
    xf86Msg(X_WARNING,
            "xf86EnableIOPorts: failed to open mem device or map IO base. \n\
Try building the server with USE_ARM32_MMAP defined\n");
    return FALSE;
#endif
#endif

    ExtendedEnabled = TRUE;

    return TRUE;
}

void
xf86DisableIOPorts(ScreenNum)
int ScreenNum;
{
    int i;

#ifdef __arm32__
    struct memAccess *memInfoP;
#endif

    ScreenEnabled[ScreenNum] = FALSE;

#ifdef __arm32__
    if ((memInfoP = checkMapInfo(FALSE, MMIO_REGION)) != NULL) {
        xf86MapInfoUnmap(memInfoP, 0);
    }
#endif

#ifdef USE_ARM32_MMAP
    if (!ExtendedEnabled)
        return;

    for (i = 0; i < MAXSCREENS; i++)
        if (ScreenEnabled[i])
            return;

    munmap((caddr_t) IOPortBase, 0x400);
    IOPortBase = (unsigned int) -1;
    ExtendedEnabled = FALSE;
#endif

    return;
}

#endif                          /* USE_ARC_MMAP || USE_ARM32_MMAP */
#endif
