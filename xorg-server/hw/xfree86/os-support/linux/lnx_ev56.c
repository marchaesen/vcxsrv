/* This file has to be built with -mcpu=ev56 */
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86.h"
#include "compiler.h"

int readDense8(pointer Base, register unsigned long Offset);
int readDense16(pointer Base, register unsigned long Offset);
int readDense32(pointer Base, register unsigned long Offset);
void
 writeDenseNB8(int Value, pointer Base, register unsigned long Offset);
void
 writeDenseNB16(int Value, pointer Base, register unsigned long Offset);
void
 writeDenseNB32(int Value, pointer Base, register unsigned long Offset);
void
 writeDense8(int Value, pointer Base, register unsigned long Offset);
void
 writeDense16(int Value, pointer Base, register unsigned long Offset);
void
 writeDense32(int Value, pointer Base, register unsigned long Offset);

int
readDense8(pointer Base, register unsigned long Offset)
{
    mem_barrier();
    return *(volatile CARD8 *) ((unsigned long) Base + (Offset));
}

int
readDense16(pointer Base, register unsigned long Offset)
{
    mem_barrier();
    return *(volatile CARD16 *) ((unsigned long) Base + (Offset));
}

int
readDense32(pointer Base, register unsigned long Offset)
{
    mem_barrier();
    return *(volatile CARD32 *) ((unsigned long) Base + (Offset));
}

void
writeDenseNB8(int Value, pointer Base, register unsigned long Offset)
{
    *(volatile CARD8 *) ((unsigned long) Base + (Offset)) = Value;
}

void
writeDenseNB16(int Value, pointer Base, register unsigned long Offset)
{
    *(volatile CARD16 *) ((unsigned long) Base + (Offset)) = Value;
}

void
writeDenseNB32(int Value, pointer Base, register unsigned long Offset)
{
    *(volatile CARD32 *) ((unsigned long) Base + (Offset)) = Value;
}

void
writeDense8(int Value, pointer Base, register unsigned long Offset)
{
    write_mem_barrier();
    *(volatile CARD8 *) ((unsigned long) Base + (Offset)) = Value;
}

void
writeDense16(int Value, pointer Base, register unsigned long Offset)
{
    write_mem_barrier();
    *(volatile CARD16 *) ((unsigned long) Base + (Offset)) = Value;
}

void
writeDense32(int Value, pointer Base, register unsigned long Offset)
{
    write_mem_barrier();
    *(volatile CARD32 *) ((unsigned long) Base + (Offset)) = Value;
}
