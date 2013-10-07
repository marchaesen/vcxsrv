/*
 * Copyright 1997 by UCHIYAMA Yasushi
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

#include<mach.h>
#include<device/device.h>
#include<mach/machine/mach_i386.h>
#include <hurd.h>

#include <X11/X.h>

#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"

#define BIOS_SIZE 0x20000

int
xf86ReadBIOS(unsigned long Base, unsigned long Offset, unsigned char *Buf,
             int Len)
{
    mach_port_t device, mem_dev;
    memory_object_t mem_obj;
    vm_address_t addr = (vm_address_t) 0;       /* serach starting address */
    kern_return_t err;

    err = get_privileged_ports(NULL, &device);
    if (err) {
        errno = err;
        FatalError("xf86ReadBIOS() can't get_privileged_ports. (%s)\n",
                   strerror(errno));
    }
    err = device_open(device, D_READ | D_WRITE, "mem", &mem_dev);
    mach_port_deallocate(mach_task_self(), device);
    if (err) {
        errno = err;
        FatalError("xf86ReadBIOS() can't device_open. (%s)\n", strerror(errno));
    }
    err =
        device_map(mem_dev, VM_PROT_READ | VM_PROT_WRITE, Base, BIOS_SIZE,
                   &mem_obj, 0);
    if (err) {
        errno = err;
        FatalError("xf86ReadBIOS() can't device_map. (%s)\n", strerror(errno));
    }
    err = vm_map(mach_task_self(),
                 &addr,
                 BIOS_SIZE,
                 0,
                 TRUE,
                 mem_obj,
                 Base,
                 FALSE,
                 VM_PROT_READ | VM_PROT_WRITE,
                 VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_SHARE);
    mach_port_deallocate(mach_task_self(), mem_obj);
    if (err) {
        errno = err;
        FatalError("xf86ReadBIOS() can't vm_map. (%s)\n", strerror(errno));
    }

    memcpy(Buf, (void *) ((int) addr + Offset), Len);

    err = vm_deallocate(mach_task_self(), addr, BIOS_SIZE);
    if (err) {
        errno = err;
        FatalError("xf86ReadBIOS() can't vm_deallocate. (%s)\n",
                   strerror(errno));
    }

    return Len;
}
