/*
 * Copyright 1997, 1998 by UCHIYAMA Yasushi
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of UCHIYAMA Yasushi not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  UCHIYAMA Yasushi makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * UCHIYAMA YASUSHI DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL UCHIYAMA YASUSHI BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <mach.h>
#include <device/device.h>
#include <mach/machine/mach_i386.h>

#include <X11/X.h>
#include "input.h"
#include "scrnintstr.h"

#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"
#include "xf86OSpriv.h"

/**************************************************************************
 * Video Memory Mapping section                                            
 ***************************************************************************/
static pointer
mapVidMem(int ScreenNum, unsigned long Base, unsigned long Size, int Flags)
{
    mach_port_t device, mem_dev;
    memory_object_t mem_obj;
    kern_return_t err;
    vm_address_t addr = (vm_address_t) 0;

    err = get_privileged_ports(NULL, &device);
    if (err) {
        errno = err;
        FatalError("xf86MapVidMem() can't get_privileged_ports. (%s)\n",
                   strerror(errno));
    }
    err = device_open(device, D_READ | D_WRITE, "mem", &mem_dev);
    mach_port_deallocate(mach_task_self(), device);
    if (err) {
        errno = err;
        FatalError("xf86MapVidMem() can't device_open. (%s)\n",
                   strerror(errno));
    }

    err =
        device_map(mem_dev, VM_PROT_READ | VM_PROT_WRITE, Base, Size, &mem_obj,
                   0);
    if (err) {
        errno = err;
        FatalError("xf86MapVidMem() can't device_map. (%s)\n", strerror(errno));
    }
    err = vm_map(mach_task_self(), &addr, Size, 0,      /* mask */
                 TRUE,          /* anywhere */
                 mem_obj, (vm_offset_t) Base, FALSE,    /* copy on write */
                 VM_PROT_READ | VM_PROT_WRITE,
                 VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_SHARE);
    mach_port_deallocate(mach_task_self(), mem_obj);
    if (err) {
        errno = err;
        FatalError("xf86MapVidMem() can't vm_map.(mem_obj) (%s)\n",
                   strerror(errno));
    }
    mach_port_deallocate(mach_task_self(), mem_dev);
    if (err) {
        errno = err;
        FatalError
            ("xf86MapVidMem() can't mach_port_deallocate.(mem_dev) (%s)\n",
             strerror(errno));
    }
    return (pointer) addr;
}

static void
unmapVidMem(int ScreenNum, pointer Base, unsigned long Size)
{
    kern_return_t err = vm_deallocate(mach_task_self(), (int) Base, Size);

    if (err) {
        errno = err;
        ErrorF("xf86UnMapVidMem: can't dealloc framebuffer space (%s)\n",
               strerror(errno));
    }
    return;
}

/**************************************************************************
 * I/O Permissions section                                                 
 ***************************************************************************/

/*
 * Due to conflicts with "compiler.h", don't rely on <sys/io.h> to declare
 * this.
 */
extern int ioperm(unsigned long __from, unsigned long __num, int __turn_on);

Bool
xf86EnableIO()
{
    if (ioperm(0, 0x10000, 1)) {
        FatalError("xf86EnableIO: ioperm() failed (%s)\n", strerror(errno));
        return FALSE;
    }
#if 0
    /*
     * Trapping disabled for now, as some VBIOSes (mga-g450 notably) use these
     * ports, and the int10 wrapper is not emulating them. (Note that it's
     * effectively what happens in the Linux variant too, as iopl() is used
     * there, making the ioperm() meaningless.)
     *
     * Reenable this when int10 gets fixed.  */
    ioperm(0x40, 4, 0);         /* trap access to the timer chip */
    ioperm(0x60, 4, 0);         /* trap access to the keyboard controller */
#endif
    return TRUE;
}

void
xf86DisableIO()
{
    ioperm(0, 0x10000, 0);
    return;
}

void
xf86OSInitVidMem(VidMemInfoPtr pVidMem)
{
    pVidMem->linearSupported = TRUE;
    pVidMem->mapMem = mapVidMem;
    pVidMem->unmapMem = unmapVidMem;
    pVidMem->initialised = TRUE;
}
