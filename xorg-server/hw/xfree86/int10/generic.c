/*
 *                   XFree86 int10 module
 *   execute BIOS int 10h calls in x86 real mode environment
 *                 Copyright 1999 Egbert Eich
 */
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <string.h>
#include <unistd.h>

#include "xf86.h"
#include "xf86_OSproc.h"
#include "compiler.h"
#define _INT10_PRIVATE
#include "xf86int10.h"
#include "int10Defines.h"
#include "Pci.h"

#define ALLOC_ENTRIES(x) ((V_RAM / x) - 1)

static CARD8 read_b(xf86Int10InfoPtr pInt, int addr);
static CARD16 read_w(xf86Int10InfoPtr pInt, int addr);
static CARD32 read_l(xf86Int10InfoPtr pInt, int addr);
static void write_b(xf86Int10InfoPtr pInt, int addr, CARD8 val);
static void write_w(xf86Int10InfoPtr pInt, int addr, CARD16 val);
static void write_l(xf86Int10InfoPtr pInt, int addr, CARD32 val);

/*
 * the emulator cannot pass a pointer to the current xf86Int10InfoRec
 * to the memory access functions therefore store it here.
 */

typedef struct {
    int shift;
    int entries;
    void *base;
    void *vRam;
    int highMemory;
    void *sysMem;
    char *alloc;
} genericInt10Priv;

#define INTPriv(x) ((genericInt10Priv*)x->private)

int10MemRec genericMem = {
    read_b,
    read_w,
    read_l,
    write_b,
    write_w,
    write_l
};

static void MapVRam(xf86Int10InfoPtr pInt);
static void UnmapVRam(xf86Int10InfoPtr pInt);

#ifdef _PC
#define GET_HIGH_BASE(x) (((V_BIOS + (x) + getpagesize() - 1)/getpagesize()) \
                              * getpagesize())
#endif

static void *sysMem = NULL;

xf86Int10InfoPtr
xf86ExtendedInitInt10(int entityIndex, int Flags)
{
    xf86Int10InfoPtr pInt;
    void *base = 0;
    void *vbiosMem = 0;
    void *options = NULL;
    legacyVGARec vga;
    ScrnInfoPtr pScrn;

    pScrn = xf86FindScreenForEntity(entityIndex);

    options = xf86HandleInt10Options(pScrn, entityIndex);

    if (int10skip(options)) {
        free(options);
        return NULL;
    }

    pInt = (xf86Int10InfoPtr) xnfcalloc(1, sizeof(xf86Int10InfoRec));
    pInt->entityIndex = entityIndex;
    if (!xf86Int10ExecSetup(pInt))
        goto error0;
    pInt->mem = &genericMem;
    pInt->private = (pointer) xnfcalloc(1, sizeof(genericInt10Priv));
    INTPriv(pInt)->alloc = (pointer) xnfcalloc(1, ALLOC_ENTRIES(getpagesize()));
    pInt->pScrn = pScrn;
    base = INTPriv(pInt)->base = xnfalloc(SYS_BIOS);

    /* FIXME: Shouldn't this be a failure case?  Leaving dev as NULL seems like
     * FIXME: an error
     */
    pInt->dev = xf86GetPciInfoForEntity(entityIndex);

    /*
     * we need to map video RAM MMIO as some chipsets map mmio
     * registers into this range.
     */
    MapVRam(pInt);
#ifdef _PC
    if (!sysMem)
        pci_device_map_legacy(pInt->dev, V_BIOS, BIOS_SIZE + SYS_BIOS - V_BIOS,
                              PCI_DEV_MAP_FLAG_WRITABLE, &sysMem);
    INTPriv(pInt)->sysMem = sysMem;

    if (xf86ReadBIOS(0, 0, base, LOW_PAGE_SIZE) < 0) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Cannot read int vect\n");
        goto error1;
    }

    /*
     * Retrieve everything between V_BIOS and SYS_BIOS as some system BIOSes
     * have executable code there.
     */
    memset((char *) base + V_BIOS, 0, SYS_BIOS - V_BIOS);
    INTPriv(pInt)->highMemory = V_BIOS;

    if (xf86IsEntityPrimary(entityIndex) && !(initPrimary(options))) {
        if (!xf86int10GetBiosSegment(pInt, (unsigned char *) sysMem - V_BIOS))
            goto error1;

        set_return_trap(pInt);

        pInt->Flags = Flags & (SET_BIOS_SCRATCH | RESTORE_BIOS_SCRATCH);
        if (!(pInt->Flags & SET_BIOS_SCRATCH))
            pInt->Flags &= ~RESTORE_BIOS_SCRATCH;
        xf86Int10SaveRestoreBIOSVars(pInt, TRUE);

    }
    else {
        const BusType location_type = xf86int10GetBiosLocationType(pInt);
        int bios_location = V_BIOS;

        reset_int_vect(pInt);
        set_return_trap(pInt);

        switch (location_type) {
        case BUS_PCI:{
            int err;
            struct pci_device *rom_device =
                xf86GetPciInfoForEntity(pInt->entityIndex);

            vbiosMem = (unsigned char *) base + bios_location;
            err = pci_device_read_rom(rom_device, vbiosMem);
            if (err) {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Cannot read V_BIOS (3) %s\n",
                           strerror(err));
                goto error1;
            }
            INTPriv(pInt)->highMemory = GET_HIGH_BASE(rom_device->rom_size);
            break;
        }
        default:
            goto error1;
        }
        pInt->BIOSseg = V_BIOS >> 4;
        pInt->num = 0xe6;
        LockLegacyVGA(pInt, &vga);
        xf86ExecX86int10(pInt);
        UnlockLegacyVGA(pInt, &vga);
    }
#else
    if (!sysMem) {
        sysMem = xnfalloc(BIOS_SIZE);
        setup_system_bios(sysMem);
    }
    INTPriv(pInt)->sysMem = sysMem;
    setup_int_vect(pInt);
    set_return_trap(pInt);

    /* Retrieve the entire legacy video BIOS segment.  This can be upto
     * 128KiB.
     */
    vbiosMem = (char *) base + V_BIOS;
    memset(vbiosMem, 0, 2 * V_BIOS_SIZE);
    if (pci_device_read_rom(pInt->dev, vbiosMem) < V_BIOS_SIZE) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Unable to retrieve all of segment 0x0C0000.\n");
    }

    /*
     * If this adapter is the primary, use its post-init BIOS (if we can find
     * it).
     */
    {
        int bios_location = V_BIOS;
        Bool done = FALSE;

        vbiosMem = (unsigned char *) base + bios_location;

        if (xf86IsEntityPrimary(entityIndex)) {
            if (int10_check_bios(pScrn->scrnIndex, bios_location >> 4, vbiosMem))
                done = TRUE;
            else
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                           "No legacy BIOS found -- trying PCI\n");
        }
        if (!done) {
            int err;
            struct pci_device *rom_device =
                xf86GetPciInfoForEntity(pInt->entityIndex);

            err = pci_device_read_rom(rom_device, vbiosMem);
            if (err) {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Cannot read V_BIOS (5) %s\n",
                           strerror(err));
                goto error1;
            }
        }
    }

    pInt->BIOSseg = V_BIOS >> 4;
    pInt->num = 0xe6;
    LockLegacyVGA(pInt, &vga);
    xf86ExecX86int10(pInt);
    UnlockLegacyVGA(pInt, &vga);
#endif
    free(options);
    return pInt;

 error1:
    free(base);
    UnmapVRam(pInt);
    free(INTPriv(pInt)->alloc);
    free(pInt->private);
 error0:
    free(pInt);
    free(options);

    return NULL;
}

static void
MapVRam(xf86Int10InfoPtr pInt)
{
    int pagesize = getpagesize();
    int size = ((VRAM_SIZE + pagesize - 1) / pagesize) * pagesize;

    pci_device_map_legacy(pInt->dev, V_RAM, size, PCI_DEV_MAP_FLAG_WRITABLE,
                          &(INTPriv(pInt)->vRam));
    pInt->io = pci_legacy_open_io(pInt->dev, 0, 64 * 1024);
}

static void
UnmapVRam(xf86Int10InfoPtr pInt)
{
    int pagesize = getpagesize();
    int size = ((VRAM_SIZE + pagesize - 1) / pagesize) * pagesize;

    pci_device_unmap_legacy(pInt->dev, INTPriv(pInt)->vRam, size);
    pci_device_close_io(pInt->dev, pInt->io);
    pInt->io = NULL;
}

Bool
MapCurrentInt10(xf86Int10InfoPtr pInt)
{
    /* nothing to do here */
    return TRUE;
}

void
xf86FreeInt10(xf86Int10InfoPtr pInt)
{
    if (!pInt)
        return;
#if defined (_PC)
    xf86Int10SaveRestoreBIOSVars(pInt, FALSE);
#endif
    if (Int10Current == pInt)
        Int10Current = NULL;
    free(INTPriv(pInt)->base);
    UnmapVRam(pInt);
    free(INTPriv(pInt)->alloc);
    free(pInt->private);
    free(pInt);
}

void *
xf86Int10AllocPages(xf86Int10InfoPtr pInt, int num, int *off)
{
    int pagesize = getpagesize();
    int num_pages = ALLOC_ENTRIES(pagesize);
    int i, j;

    for (i = 0; i < (num_pages - num); i++) {
        if (INTPriv(pInt)->alloc[i] == 0) {
            for (j = i; j < (num + i); j++)
                if (INTPriv(pInt)->alloc[j] != 0)
                    break;
            if (j == (num + i))
                break;
            i += num;
        }
    }
    if (i == (num_pages - num))
        return NULL;

    for (j = i; j < (i + num); j++)
        INTPriv(pInt)->alloc[j] = 1;

    *off = (i + 1) * pagesize;

    return (char *) INTPriv(pInt)->base + *off;
}

void
xf86Int10FreePages(xf86Int10InfoPtr pInt, void *pbase, int num)
{
    int pagesize = getpagesize();
    int first =
        (((char *) pbase - (char *) INTPriv(pInt)->base) / pagesize) - 1;
    int i;

    for (i = first; i < (first + num); i++)
        INTPriv(pInt)->alloc[i] = 0;
}

#define OFF(addr) ((addr) & 0xffff)
#if defined _PC
#define HIGH_OFFSET (INTPriv(pInt)->highMemory)
#define HIGH_BASE   V_BIOS
#else
#define HIGH_OFFSET SYS_BIOS
#define HIGH_BASE   SYS_BIOS
#endif
#define SYS(addr) ((addr) >= HIGH_OFFSET)
#define V_ADDR(addr) \
	  (SYS(addr) ? ((char*)INTPriv(pInt)->sysMem) + (addr - HIGH_BASE) \
	   : (((char*)(INTPriv(pInt)->base) + addr)))
#define VRAM_ADDR(addr) (addr - V_RAM)
#define VRAM_BASE (INTPriv(pInt)->vRam)

#define VRAM(addr) ((addr >= V_RAM) && (addr < (V_RAM + VRAM_SIZE)))
#define V_ADDR_RB(addr) \
	(VRAM(addr)) ? MMIO_IN8((CARD8*)VRAM_BASE,VRAM_ADDR(addr)) \
	   : *(CARD8*) V_ADDR(addr)
#define V_ADDR_RW(addr) \
	(VRAM(addr)) ? MMIO_IN16((CARD16*)VRAM_BASE,VRAM_ADDR(addr)) \
	   : ldw_u((pointer)V_ADDR(addr))
#define V_ADDR_RL(addr) \
	(VRAM(addr)) ? MMIO_IN32((CARD32*)VRAM_BASE,VRAM_ADDR(addr)) \
	   : ldl_u((pointer)V_ADDR(addr))

#define V_ADDR_WB(addr,val) \
	if(VRAM(addr)) \
	    MMIO_OUT8((CARD8*)VRAM_BASE,VRAM_ADDR(addr),val); \
	else \
	    *(CARD8*) V_ADDR(addr) = val;
#define V_ADDR_WW(addr,val) \
	if(VRAM(addr)) \
	    MMIO_OUT16((CARD16*)VRAM_BASE,VRAM_ADDR(addr),val); \
	else \
	    stw_u((val),(pointer)(V_ADDR(addr)));

#define V_ADDR_WL(addr,val) \
	if (VRAM(addr)) \
	    MMIO_OUT32((CARD32*)VRAM_BASE,VRAM_ADDR(addr),val); \
	else \
	    stl_u(val,(pointer)(V_ADDR(addr)));

static CARD8
read_b(xf86Int10InfoPtr pInt, int addr)
{
    return V_ADDR_RB(addr);
}

static CARD16
read_w(xf86Int10InfoPtr pInt, int addr)
{
#if X_BYTE_ORDER == X_LITTLE_ENDIAN
    if (OFF(addr + 1) > 0)
        return V_ADDR_RW(addr);
#endif
    return V_ADDR_RB(addr) | (V_ADDR_RB(addr + 1) << 8);
}

static CARD32
read_l(xf86Int10InfoPtr pInt, int addr)
{
#if X_BYTE_ORDER == X_LITTLE_ENDIAN
    if (OFF(addr + 3) > 2)
        return V_ADDR_RL(addr);
#endif
    return V_ADDR_RB(addr) |
        (V_ADDR_RB(addr + 1) << 8) |
        (V_ADDR_RB(addr + 2) << 16) | (V_ADDR_RB(addr + 3) << 24);
}

static void
write_b(xf86Int10InfoPtr pInt, int addr, CARD8 val)
{
    V_ADDR_WB(addr, val);
}

static void
write_w(xf86Int10InfoPtr pInt, int addr, CARD16 val)
{
#if X_BYTE_ORDER == X_LITTLE_ENDIAN
    if (OFF(addr + 1) > 0) {
        V_ADDR_WW(addr, val);
    }
#endif
    V_ADDR_WB(addr, val);
    V_ADDR_WB(addr + 1, val >> 8);
}

static void
write_l(xf86Int10InfoPtr pInt, int addr, CARD32 val)
{
#if X_BYTE_ORDER == X_LITTLE_ENDIAN
    if (OFF(addr + 3) > 2) {
        V_ADDR_WL(addr, val);
    }
#endif
    V_ADDR_WB(addr, val);
    V_ADDR_WB(addr + 1, val >> 8);
    V_ADDR_WB(addr + 2, val >> 16);
    V_ADDR_WB(addr + 3, val >> 24);
}

pointer
xf86int10Addr(xf86Int10InfoPtr pInt, CARD32 addr)
{
    return V_ADDR(addr);
}
