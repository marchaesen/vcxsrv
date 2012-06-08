/*
 *                   XFree86 int10 module
 *   execute BIOS int 10h calls in x86 real mode environment
 *                 Copyright 1999 Egbert Eich
 *
 *   Part of this code was inspired  by the VBIOS POSTing code in DOSEMU
 *   developed by the "DOSEMU-Development-Team"
 */

/*
 * To debug port accesses define PRINT_PORT to 1.
 * Note! You also have to comment out ioperm()
 * in xf86EnableIO(). Otherwise we won't trap
 * on PIO.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#define PRINT_PORT 0

#include <unistd.h>

#include <X11/Xos.h>
#include "xf86.h"
#include "xf86_OSproc.h"
#include "compiler.h"
#define _INT10_PRIVATE
#include "int10Defines.h"
#include "xf86int10.h"
#include "Pci.h"
#ifdef _X86EMU
#include "x86emu/x86emui.h"
#else
#define DEBUG_IO_TRACE() 0
#endif
#include <pciaccess.h>

static int pciCfg1in(CARD16 addr, CARD32 *val);
static int pciCfg1out(CARD16 addr, CARD32 val);
static int pciCfg1inw(CARD16 addr, CARD16 *val);
static int pciCfg1outw(CARD16 addr, CARD16 val);
static int pciCfg1inb(CARD16 addr, CARD8 *val);
static int pciCfg1outb(CARD16 addr, CARD8 val);

#if defined (_PC)
static void SetResetBIOSVars(xf86Int10InfoPtr pInt, Bool set);
#endif

#define REG pInt

int
setup_int(xf86Int10InfoPtr pInt)
{
    if (pInt != Int10Current) {
        if (!MapCurrentInt10(pInt))
            return -1;
        Int10Current = pInt;
    }
    X86_EAX = (CARD32) pInt->ax;
    X86_EBX = (CARD32) pInt->bx;
    X86_ECX = (CARD32) pInt->cx;
    X86_EDX = (CARD32) pInt->dx;
    X86_ESI = (CARD32) pInt->si;
    X86_EDI = (CARD32) pInt->di;
    X86_EBP = (CARD32) pInt->bp;
    X86_ESP = 0x1000;
    X86_SS = pInt->stackseg >> 4;
    X86_EIP = 0x0600;
    X86_CS = 0x0;               /* address of 'hlt' */
    X86_DS = 0x40;              /* standard pc ds */
    X86_ES = pInt->es;
    X86_FS = 0;
    X86_GS = 0;
    X86_EFLAGS = X86_IF_MASK | X86_IOPL_MASK;
#if defined (_PC)
    if (pInt->Flags & SET_BIOS_SCRATCH)
        SetResetBIOSVars(pInt, TRUE);
#endif
    OsBlockSignals();
    return 0;
}

void
finish_int(xf86Int10InfoPtr pInt, int sig)
{
    OsReleaseSignals();
    pInt->ax = (CARD32) X86_EAX;
    pInt->bx = (CARD32) X86_EBX;
    pInt->cx = (CARD32) X86_ECX;
    pInt->dx = (CARD32) X86_EDX;
    pInt->si = (CARD32) X86_ESI;
    pInt->di = (CARD32) X86_EDI;
    pInt->es = (CARD16) X86_ES;
    pInt->bp = (CARD32) X86_EBP;
    pInt->flags = (CARD32) X86_FLAGS;
#if defined (_PC)
    if (pInt->Flags & RESTORE_BIOS_SCRATCH)
        SetResetBIOSVars(pInt, FALSE);
#endif
}

/* general software interrupt handler */
CARD32
getIntVect(xf86Int10InfoPtr pInt, int num)
{
    return MEM_RW(pInt, num << 2) + (MEM_RW(pInt, (num << 2) + 2) << 4);
}

void
pushw(xf86Int10InfoPtr pInt, CARD16 val)
{
    X86_ESP -= 2;
    MEM_WW(pInt, ((CARD32) X86_SS << 4) + X86_SP, val);
}

int
run_bios_int(int num, xf86Int10InfoPtr pInt)
{
    CARD32 eflags;

#ifndef _PC
    /* check if bios vector is initialized */
    if (MEM_RW(pInt, (num << 2) + 2) == (SYS_BIOS >> 4)) {      /* SYS_BIOS_SEG ? */

        if (num == 21 && X86_AH == 0x4e) {
            xf86DrvMsg(pInt->pScrn->scrnIndex, X_NOTICE,
                       "Failing Find-Matching-File on non-PC"
                       " (int 21, func 4e)\n");
            X86_AX = 2;
            SET_FLAG(F_CF);
            return 1;
        }
        else {
            xf86DrvMsgVerb(pInt->pScrn->scrnIndex, X_NOT_IMPLEMENTED, 2,
                           "Ignoring int 0x%02x call\n", num);
            if (xf86GetVerbosity() > 3) {
                dump_registers(pInt);
                stack_trace(pInt);
            }
            return 1;
        }
    }
#endif
#ifdef PRINT_INT
    ErrorF("calling card BIOS at: ");
#endif
    eflags = X86_EFLAGS;
#if 0
    eflags = eflags | IF_MASK;
    X86_EFLAGS = X86_EFLAGS & ~(VIF_MASK | TF_MASK | IF_MASK | NT_MASK);
#endif
    pushw(pInt, eflags);
    pushw(pInt, X86_CS);
    pushw(pInt, X86_IP);
    X86_CS = MEM_RW(pInt, (num << 2) + 2);
    X86_IP = MEM_RW(pInt, num << 2);
#ifdef PRINT_INT
    ErrorF("0x%x:%lx\n", X86_CS, X86_EIP);
#endif
    return 1;
}

/* Debugging stuff */
void
dump_code(xf86Int10InfoPtr pInt)
{
    int i;
    CARD32 lina = SEG_ADR((CARD32), X86_CS, IP);

    xf86DrvMsgVerb(pInt->pScrn->scrnIndex, X_INFO, 3, "code at 0x%8.8" PRIx32 ":\n",
                   lina);
    for (i = 0; i < 0x10; i++)
        xf86ErrorFVerb(3, " %2.2x", MEM_RB(pInt, lina + i));
    xf86ErrorFVerb(3, "\n");
    for (; i < 0x20; i++)
        xf86ErrorFVerb(3, " %2.2x", MEM_RB(pInt, lina + i));
    xf86ErrorFVerb(3, "\n");
}

void
dump_registers(xf86Int10InfoPtr pInt)
{
    xf86DrvMsgVerb(pInt->pScrn->scrnIndex, X_INFO, 3,
                   "EAX=0x%8.8lx, EBX=0x%8.8lx, ECX=0x%8.8lx, EDX=0x%8.8lx\n",
                   (unsigned long) X86_EAX, (unsigned long) X86_EBX,
                   (unsigned long) X86_ECX, (unsigned long) X86_EDX);
    xf86DrvMsgVerb(pInt->pScrn->scrnIndex, X_INFO, 3,
                   "ESP=0x%8.8lx, EBP=0x%8.8lx, ESI=0x%8.8lx, EDI=0x%8.8lx\n",
                   (unsigned long) X86_ESP, (unsigned long) X86_EBP,
                   (unsigned long) X86_ESI, (unsigned long) X86_EDI);
    xf86DrvMsgVerb(pInt->pScrn->scrnIndex, X_INFO, 3,
                   "CS=0x%4.4x, SS=0x%4.4x,"
                   " DS=0x%4.4x, ES=0x%4.4x, FS=0x%4.4x, GS=0x%4.4x\n",
                   X86_CS, X86_SS, X86_DS, X86_ES, X86_FS, X86_GS);
    xf86DrvMsgVerb(pInt->pScrn->scrnIndex, X_INFO, 3,
                   "EIP=0x%8.8lx, EFLAGS=0x%8.8lx\n",
                   (unsigned long) X86_EIP, (unsigned long) X86_EFLAGS);
}

void
stack_trace(xf86Int10InfoPtr pInt)
{
    int i = 0;
    unsigned long stack = SEG_ADR((CARD32), X86_SS, SP);
    unsigned long tail = (CARD32) ((X86_SS << 4) + 0x1000);

    if (stack >= tail)
        return;

    xf86MsgVerb(X_INFO, 3, "stack at 0x%8.8lx:\n", stack);
    for (; stack < tail; stack++) {
        xf86ErrorFVerb(3, " %2.2x", MEM_RB(pInt, stack));
        i = (i + 1) % 0x10;
        if (!i)
            xf86ErrorFVerb(3, "\n");
    }
    if (i)
        xf86ErrorFVerb(3, "\n");
}

int
port_rep_inb(xf86Int10InfoPtr pInt,
             CARD16 port, CARD32 base, int d_f, CARD32 count)
{
    register int inc = d_f ? -1 : 1;
    CARD32 dst = base;

    if (PRINT_PORT && DEBUG_IO_TRACE())
        ErrorF(" rep_insb(%#x) %" PRIu32 " bytes at %8.8" PRIx32 " %s\n",
               port, count, base, d_f ? "up" : "down");
    while (count--) {
        MEM_WB(pInt, dst, x_inb(port));
        dst += inc;
    }
    return dst - base;
}

int
port_rep_inw(xf86Int10InfoPtr pInt,
             CARD16 port, CARD32 base, int d_f, CARD32 count)
{
    register int inc = d_f ? -2 : 2;
    CARD32 dst = base;

    if (PRINT_PORT && DEBUG_IO_TRACE())
        ErrorF(" rep_insw(%#x) %" PRIu32 " bytes at %8.8" PRIx32 " %s\n",
               port, count, base, d_f ? "up" : "down");
    while (count--) {
        MEM_WW(pInt, dst, x_inw(port));
        dst += inc;
    }
    return dst - base;
}

int
port_rep_inl(xf86Int10InfoPtr pInt,
             CARD16 port, CARD32 base, int d_f, CARD32 count)
{
    register int inc = d_f ? -4 : 4;
    CARD32 dst = base;

    if (PRINT_PORT && DEBUG_IO_TRACE())
        ErrorF(" rep_insl(%#x) %" PRIu32 " bytes at %8.8" PRIx32 " %s\n",
               port, count, base, d_f ? "up" : "down");
    while (count--) {
        MEM_WL(pInt, dst, x_inl(port));
        dst += inc;
    }
    return dst - base;
}

int
port_rep_outb(xf86Int10InfoPtr pInt,
              CARD16 port, CARD32 base, int d_f, CARD32 count)
{
    register int inc = d_f ? -1 : 1;
    CARD32 dst = base;

    if (PRINT_PORT && DEBUG_IO_TRACE())
        ErrorF(" rep_outb(%#x) %" PRIu32 " bytes at %8.8" PRIx32 " %s\n",
               port, count, base, d_f ? "up" : "down");
    while (count--) {
        x_outb(port, MEM_RB(pInt, dst));
        dst += inc;
    }
    return dst - base;
}

int
port_rep_outw(xf86Int10InfoPtr pInt,
              CARD16 port, CARD32 base, int d_f, CARD32 count)
{
    register int inc = d_f ? -2 : 2;
    CARD32 dst = base;

    if (PRINT_PORT && DEBUG_IO_TRACE())
        ErrorF(" rep_outw(%#x) %" PRIu32 " bytes at %8.8" PRIx32 " %s\n",
               port, count, base, d_f ? "up" : "down");
    while (count--) {
        x_outw(port, MEM_RW(pInt, dst));
        dst += inc;
    }
    return dst - base;
}

int
port_rep_outl(xf86Int10InfoPtr pInt,
              CARD16 port, CARD32 base, int d_f, CARD32 count)
{
    register int inc = d_f ? -4 : 4;
    CARD32 dst = base;

    if (PRINT_PORT && DEBUG_IO_TRACE())
        ErrorF(" rep_outl(%#x) %" PRIu32 " bytes at %8.8" PRIx32 " %s\n",
               port, count, base, d_f ? "up" : "down");
    while (count--) {
        x_outl(port, MEM_RL(pInt, dst));
        dst += inc;
    }
    return dst - base;
}

CARD8
x_inb(CARD16 port)
{
    CARD8 val;

    if (port == 0x40) {
        Int10Current->inb40time++;
        val = (CARD8) (Int10Current->inb40time >>
                       ((Int10Current->inb40time & 1) << 3));
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" inb(%#x) = %2.2x\n", port, val);
#ifdef __NOT_YET__
    }
    else if (port < 0x0100) {   /* Don't interfere with mainboard */
        val = 0;
        xf86DrvMsgVerb(Int10Current->pScrn->scrnIndex, X_NOT_IMPLEMENTED, 2,
                       "inb 0x%4.4x\n", port);
        if (xf86GetVerbosity() > 3) {
            dump_registers(Int10Current);
            stack_trace(Int10Current);
        }
#endif                          /* __NOT_YET__ */
    }
    else if (!pciCfg1inb(port, &val)) {
        val = pci_io_read8(Int10Current->io, port);
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" inb(%#x) = %2.2x\n", port, val);
    }
    return val;
}

CARD16
x_inw(CARD16 port)
{
    CARD16 val;

    if (port == 0x5c) {
        struct timeval tv;

        /*
         * Emulate a PC's timer.  Typical resolution is 3.26 usec.
         * Approximate this by dividing by 3.
         */
        X_GETTIMEOFDAY(&tv);
        val = (CARD16) (tv.tv_usec / 3);
    }
    else if (!pciCfg1inw(port, &val)) {
        val = pci_io_read16(Int10Current->io, port);
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" inw(%#x) = %4.4x\n", port, val);
    }
    return val;
}

void
x_outb(CARD16 port, CARD8 val)
{
    if ((port == 0x43) && (val == 0)) {
        struct timeval tv;

        /*
         * Emulate a PC's timer 0.  Such timers typically have a resolution of
         * some .838 usec per tick, but this can only provide 1 usec per tick.
         * (Not that this matters much, given inherent emulation delays.)  Use
         * the bottom bit as a byte select.  See inb(0x40) above.
         */
        X_GETTIMEOFDAY(&tv);
        Int10Current->inb40time = (CARD16) (tv.tv_usec | 1);
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" outb(%#x, %2.2x)\n", port, val);
#ifdef __NOT_YET__
    }
    else if (port < 0x0100) {   /* Don't interfere with mainboard */
        xf86DrvMsgVerb(Int10Current->pScrn->scrnIndex, X_NOT_IMPLEMENTED, 2,
                       "outb 0x%4.4x,0x%2.2x\n", port, val);
        if (xf86GetVerbosity() > 3) {
            dump_registers(Int10Current);
            stack_trace(Int10Current);
        }
#endif                          /* __NOT_YET__ */
    }
    else if (!pciCfg1outb(port, val)) {
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" outb(%#x, %2.2x)\n", port, val);
        pci_io_write8(Int10Current->io, port, val);
    }
}

void
x_outw(CARD16 port, CARD16 val)
{

    if (!pciCfg1outw(port, val)) {
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" outw(%#x, %4.4x)\n", port, val);
        pci_io_write16(Int10Current->io, port, val);
    }
}

CARD32
x_inl(CARD16 port)
{
    CARD32 val;

    if (!pciCfg1in(port, &val)) {
        val = pci_io_read32(Int10Current->io, port);
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" inl(%#x) = %8.8" PRIx32 "\n", port, val);
    }
    return val;
}

void
x_outl(CARD16 port, CARD32 val)
{
    if (!pciCfg1out(port, val)) {
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" outl(%#x, %8.8" PRIx32 ")\n", port, val);
        pci_io_write32(Int10Current->io, port, val);
    }
}

CARD8
Mem_rb(CARD32 addr)
{
    return (*Int10Current->mem->rb) (Int10Current, addr);
}

CARD16
Mem_rw(CARD32 addr)
{
    return (*Int10Current->mem->rw) (Int10Current, addr);
}

CARD32
Mem_rl(CARD32 addr)
{
    return (*Int10Current->mem->rl) (Int10Current, addr);
}

void
Mem_wb(CARD32 addr, CARD8 val)
{
    (*Int10Current->mem->wb) (Int10Current, addr, val);
}

void
Mem_ww(CARD32 addr, CARD16 val)
{
    (*Int10Current->mem->ww) (Int10Current, addr, val);
}

void
Mem_wl(CARD32 addr, CARD32 val)
{
    (*Int10Current->mem->wl) (Int10Current, addr, val);
}

static CARD32 PciCfg1Addr = 0;

#define PCI_DOM_FROM_TAG(tag)  (((tag) >> 24) & (PCI_DOM_MASK))
#define PCI_BUS_FROM_TAG(tag)  (((tag) >> 16) & (PCI_DOMBUS_MASK))
#define PCI_DEV_FROM_TAG(tag)  (((tag) & 0x0000f800u) >> 11)
#define PCI_FUNC_FROM_TAG(tag) (((tag) & 0x00000700u) >> 8)

#define PCI_OFFSET(x) ((x) & 0x000000ff)
#define PCI_TAG(x)    ((x) & 0x7fffff00)

static struct pci_device *
pci_device_for_cfg_address(CARD32 addr)
{
    struct pci_device *dev = NULL;
    CARD32 tag = PCI_TAG(addr);

    struct pci_slot_match slot_match = {
        .domain = PCI_DOM_FROM_TAG(tag),
        .bus = PCI_BUS_NO_DOMAIN(PCI_BUS_FROM_TAG(tag)),
        .dev = PCI_DEV_FROM_TAG(tag),
        .func = PCI_FUNC_FROM_TAG(tag),
        .match_data = 0
    };

    struct pci_device_iterator *iter =
        pci_slot_match_iterator_create(&slot_match);

    if (iter)
        dev = pci_device_next(iter);

    pci_iterator_destroy(iter);

    return dev;
}

static int
pciCfg1in(CARD16 addr, CARD32 *val)
{
    if (addr == 0xCF8) {
        *val = PciCfg1Addr;
        return 1;
    }
    if (addr == 0xCFC) {
        pci_device_cfg_read_u32(pci_device_for_cfg_address(PciCfg1Addr),
                                (uint32_t *) val, PCI_OFFSET(PciCfg1Addr));
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" cfg_inl(%#" PRIx32 ") = %8.8" PRIx32 "\n", PciCfg1Addr,
                   *val);
        return 1;
    }
    return 0;
}

static int
pciCfg1out(CARD16 addr, CARD32 val)
{
    if (addr == 0xCF8) {
        PciCfg1Addr = val;
        return 1;
    }
    if (addr == 0xCFC) {
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" cfg_outl(%#" PRIx32 ", %8.8" PRIx32 ")\n", PciCfg1Addr,
                   val);
        pci_device_cfg_write_u32(pci_device_for_cfg_address(PciCfg1Addr), val,
                                 PCI_OFFSET(PciCfg1Addr));
        return 1;
    }
    return 0;
}

static int
pciCfg1inw(CARD16 addr, CARD16 *val)
{
    int shift;

    if ((addr >= 0xCF8) && (addr <= 0xCFB)) {
        shift = (addr - 0xCF8) * 8;
        *val = (PciCfg1Addr >> shift) & 0xffff;
        return 1;
    }
    if ((addr >= 0xCFC) && (addr <= 0xCFF)) {
        const unsigned offset = addr - 0xCFC;

        pci_device_cfg_read_u16(pci_device_for_cfg_address(PciCfg1Addr),
                                val, PCI_OFFSET(PciCfg1Addr) + offset);
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" cfg_inw(%#" PRIx32 ") = %4.4x\n", PciCfg1Addr + offset,
                   *val);
        return 1;
    }
    return 0;
}

static int
pciCfg1outw(CARD16 addr, CARD16 val)
{
    int shift;

    if ((addr >= 0xCF8) && (addr <= 0xCFB)) {
        shift = (addr - 0xCF8) * 8;
        PciCfg1Addr &= ~(0xffff << shift);
        PciCfg1Addr |= ((CARD32) val) << shift;
        return 1;
    }
    if ((addr >= 0xCFC) && (addr <= 0xCFF)) {
        const unsigned offset = addr - 0xCFC;

        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" cfg_outw(%#" PRIx32 ", %4.4x)\n", PciCfg1Addr + offset,
                   val);
        pci_device_cfg_write_u16(pci_device_for_cfg_address(PciCfg1Addr), val,
                                 PCI_OFFSET(PciCfg1Addr) + offset);
        return 1;
    }
    return 0;
}

static int
pciCfg1inb(CARD16 addr, CARD8 *val)
{
    int shift;

    if ((addr >= 0xCF8) && (addr <= 0xCFB)) {
        shift = (addr - 0xCF8) * 8;
        *val = (PciCfg1Addr >> shift) & 0xff;
        return 1;
    }
    if ((addr >= 0xCFC) && (addr <= 0xCFF)) {
        const unsigned offset = addr - 0xCFC;

        pci_device_cfg_read_u8(pci_device_for_cfg_address(PciCfg1Addr),
                               val, PCI_OFFSET(PciCfg1Addr) + offset);
        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" cfg_inb(%#" PRIx32 ") = %2.2x\n", PciCfg1Addr + offset,
                   *val);
        return 1;
    }
    return 0;
}

static int
pciCfg1outb(CARD16 addr, CARD8 val)
{
    int shift;

    if ((addr >= 0xCF8) && (addr <= 0xCFB)) {
        shift = (addr - 0xCF8) * 8;
        PciCfg1Addr &= ~(0xff << shift);
        PciCfg1Addr |= ((CARD32) val) << shift;
        return 1;
    }
    if ((addr >= 0xCFC) && (addr <= 0xCFF)) {
        const unsigned offset = addr - 0xCFC;

        if (PRINT_PORT && DEBUG_IO_TRACE())
            ErrorF(" cfg_outb(%#" PRIx32 ", %2.2x)\n", PciCfg1Addr + offset,
                   val);
        pci_device_cfg_write_u8(pci_device_for_cfg_address(PciCfg1Addr), val,
                                PCI_OFFSET(PciCfg1Addr) + offset);
        return 1;
    }
    return 0;
}

CARD8
bios_checksum(const CARD8 *start, int size)
{
    CARD8 sum = 0;

    while (size-- > 0)
        sum += *start++;
    return sum;
}

/*
 * Lock/Unlock legacy VGA. Some Bioses try to be very clever and make
 * an attempt to detect a legacy ISA card. If they find one they might
 * act very strange: for example they might configure the card as a
 * monochrome card. This might cause some drivers to choke.
 * To avoid this we attempt legacy VGA by writing to all know VGA
 * disable registers before we call the BIOS initialization and
 * restore the original values afterwards. In beween we hold our
 * breath. To get to a (possibly exising) ISA card need to disable
 * our current PCI card.
 */
/*
 * This is just for booting: we just want to catch pure
 * legacy vga therefore we don't worry about mmio etc.
 * This stuff should really go into vgaHW.c. However then
 * the driver would have to load the vga-module prior to
 * doing int10.
 */
void
LockLegacyVGA(xf86Int10InfoPtr pInt, legacyVGAPtr vga)
{
    vga->save_msr = pci_io_read8(pInt->io, 0x03CC);
    vga->save_vse = pci_io_read8(pInt->io, 0x03C3);
#ifndef __ia64__
    vga->save_46e8 = pci_io_read8(pInt->io, 0x46E8);
#endif
    vga->save_pos102 = pci_io_read8(pInt->io, 0x0102);
    pci_io_write8(pInt->io, 0x03C2, ~(CARD8) 0x03 & vga->save_msr);
    pci_io_write8(pInt->io, 0x03C3, ~(CARD8) 0x01 & vga->save_vse);
#ifndef __ia64__
    pci_io_write8(pInt->io, 0x46E8, ~(CARD8) 0x08 & vga->save_46e8);
#endif
    pci_io_write8(pInt->io, 0x0102, ~(CARD8) 0x01 & vga->save_pos102);
}

void
UnlockLegacyVGA(xf86Int10InfoPtr pInt, legacyVGAPtr vga)
{
    pci_io_write8(pInt->io, 0x0102, vga->save_pos102);
#ifndef __ia64__
    pci_io_write8(pInt->io, 0x46E8, vga->save_46e8);
#endif
    pci_io_write8(pInt->io, 0x03C3, vga->save_vse);
    pci_io_write8(pInt->io, 0x03C2, vga->save_msr);
}

#if defined (_PC)
static void
SetResetBIOSVars(xf86Int10InfoPtr pInt, Bool set)
{
    int pagesize = getpagesize();
    unsigned char *base;
    int i;

    if (pci_device_map_legacy
        (pInt->dev, 0, pagesize, PCI_DEV_MAP_FLAG_WRITABLE, (void **) &base))
        return;                 /* eek */

    if (set) {
        for (i = BIOS_SCRATCH_OFF; i < BIOS_SCRATCH_END; i++)
            MEM_WW(pInt, i, *(base + i));
    }
    else {
        for (i = BIOS_SCRATCH_OFF; i < BIOS_SCRATCH_END; i++)
            *(base + i) = MEM_RW(pInt, i);
    }

    pci_device_unmap_legacy(pInt->dev, base, pagesize);
}

void
xf86Int10SaveRestoreBIOSVars(xf86Int10InfoPtr pInt, Bool save)
{
    int pagesize = getpagesize();
    unsigned char *base;
    int i;

    if (!xf86IsEntityPrimary(pInt->entityIndex)
        || (!save && !pInt->BIOSScratch))
        return;

    if (pci_device_map_legacy
        (pInt->dev, 0, pagesize, PCI_DEV_MAP_FLAG_WRITABLE, (void **) &base))
        return;                 /* eek */

    base += BIOS_SCRATCH_OFF;
    if (save) {
        if ((pInt->BIOSScratch = xnfalloc(BIOS_SCRATCH_LEN)))
            for (i = 0; i < BIOS_SCRATCH_LEN; i++)
                *(((char *) pInt->BIOSScratch + i)) = *(base + i);
    }
    else {
        if (pInt->BIOSScratch) {
            for (i = 0; i < BIOS_SCRATCH_LEN; i++)
                *(base + i) = *(pInt->BIOSScratch + i);
            free(pInt->BIOSScratch);
            pInt->BIOSScratch = NULL;
        }
    }

    pci_device_unmap_legacy(pInt->dev, base - BIOS_SCRATCH_OFF, pagesize);
}
#endif

xf86Int10InfoPtr
xf86InitInt10(int entityIndex)
{
    return xf86ExtendedInitInt10(entityIndex, 0);
}
