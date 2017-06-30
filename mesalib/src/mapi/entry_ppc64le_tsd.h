/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2017 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Ben Crocker <bcrocker@redhat.com>
 */

#ifdef HAVE_FUNC_ATTRIBUTE_VISIBILITY
#define HIDDEN __attribute__((visibility("hidden")))
#else
#define HIDDEN
#endif

// NOTE: These must be powers of two:
#define PPC64LE_ENTRY_SIZE 256
#define PPC64LE_PAGE_ALIGN 65536
#if ((PPC64LE_ENTRY_SIZE & (PPC64LE_ENTRY_SIZE - 1)) != 0)
#error PPC64LE_ENTRY_SIZE must be a power of two!
#endif
#if ((PPC64LE_PAGE_ALIGN & (PPC64LE_PAGE_ALIGN - 1)) != 0)
#error PPC64LE_PAGE_ALIGN must be a power of two!
#endif

__asm__(".text\n"
        ".balign " U_STRINGIFY(PPC64LE_ENTRY_SIZE) "\n"
        "ppc64le_entry_start:");

#define STUB_ASM_ENTRY(func)                            \
   ".globl " func "\n"                                  \
   ".type " func ", @function\n"                        \
   ".balign " U_STRINGIFY(PPC64LE_ENTRY_SIZE) "\n"        \
   func ":\n\t"                                         \
   "  addis  2, 12, .TOC.-" func "@ha\n\t"              \
   "  addi   2, 2, .TOC.-" func "@l\n\t"                \
   "  .localentry  " func ", .-" func "\n\t"

#define STUB_ASM_CODE(slot)                                         \
   "  addis  11, 2, " ENTRY_CURRENT_TABLE "@got@ha\n\t"             \
   "  ld     11, " ENTRY_CURRENT_TABLE "@got@l(11)\n\t"             \
   "  ld     11, 0(11)\n\t"                                         \
   "  cmpldi 11, 0\n\t"                                             \
   "  beq    2000f\n"                                               \
   "1050:\n\t"                                                      \
   "  ld     12, " slot "*8(11)\n\t"                                \
   "  mtctr  12\n\t"                                                \
   "  bctr\n"                                                       \
   "2000:\n\t"                                                      \
   "  mflr   0\n\t"                                                 \
   "  std    0, 16(1)\n\t"                                          \
   "  std    2, 40(1)\n\t"                                          \
   "  stdu   1, -144(1)\n\t"                                        \
   "  std    3, 56(1)\n\t"                                          \
   "  std    4, 64(1)\n\t"                                          \
   "  std    5, 72(1)\n\t"                                          \
   "  std    6, 80(1)\n\t"                                          \
   "  std    7, 88(1)\n\t"                                          \
   "  std    8, 96(1)\n\t"                                          \
   "  std    9, 104(1)\n\t"                                         \
   "  std    10, 112(1)\n\t"                                        \
   "  std    12, 128(1)\n\t"                                        \
   "  addis  12, 2, " ENTRY_CURRENT_TABLE_GET "@got@ha\n\t"         \
   "  ld     12, " ENTRY_CURRENT_TABLE_GET "@got@l(12)\n\t"         \
   "  mtctr  12\n\t"                                                \
   "  bctrl\n\t"                                                    \
   "  ld     2, 144+40(1)\n\t"                                      \
   "  mr     11, 3\n\t"                                             \
   "  ld     3, 56(1)\n\t"                                          \
   "  ld     4, 64(1)\n\t"                                          \
   "  ld     5, 72(1)\n\t"                                          \
   "  ld     6, 80(1)\n\t"                                          \
   "  ld     7, 88(1)\n\t"                                          \
   "  ld     8, 96(1)\n\t"                                          \
   "  ld     9, 104(1)\n\t"                                         \
   "  ld     10, 112(1)\n\t"                                        \
   "  ld     12, 128(1)\n\t"                                        \
   "  addi   1, 1, 144\n\t"                                         \
   "  ld     0, 16(1)\n\t"                                          \
   "  mtlr   0\n\t"                                                 \
   "  b      1050b\n"

#define MAPI_TMP_STUB_ASM_GCC
#include "mapi_tmp.h"

#ifndef MAPI_MODE_BRIDGE

#include <string.h>
#include "u_execmem.h"

void
entry_patch_public(void)
{
}

extern char
ppc64le_entry_start[] HIDDEN;

mapi_func
entry_get_public(int slot)
{
   return (mapi_func) (ppc64le_entry_start + slot * PPC64LE_ENTRY_SIZE);
}

static const uint32_t code_templ[] = {
   // This should be functionally the same code as would be generated from
   // the STUB_ASM_CODE macro, but defined as a buffer.
   // This is used to generate new dispatch stubs. Mesa will copy this
   // data to the dispatch stub, and then it will patch the slot number and
   // any addresses that it needs to.
   // NOTE!!!  NOTE!!!  NOTE!!!
   // This representation is correct for both little- and big-endian systems.
   // However, more work needs to be done for big-endian Linux because it
   // adheres to an older, AIX-compatible ABI that uses function descriptors.
   // 1000:
   0x7C0802A6,    // <ENTRY+000>:    mflr   0
   0xF8010010,    // <ENTRY+004>:    std    0, 16(1)
   0xE96C0098,    // <ENTRY+008>:    ld     11, 9000f-1000b+0(12)
   0xE96B0000,    // <ENTRY+012>:    ld     11, 0(11)
   0x282B0000,    // <ENTRY+016>:    cmpldi 11, 0
   0x41820014,    // <ENTRY+020>:    beq    2000f
   // 1050:
   0xE80C00A8,    // <ENTRY+024>:    ld     0, 9000f-1000b+16(12)
   0x7D8B002A,    // <ENTRY+028>:    ldx    12, 11, 0
   0x7D8903A6,    // <ENTRY+032>:    mtctr  12
   0x4E800420,    // <ENTRY+036>:    bctr
   // 2000:
   0xF8410028,    // <ENTRY+040>:    std    2, 40(1)
   0xF821FF71,    // <ENTRY+044>:    stdu   1, -144(1)
   0xF8610038,    // <ENTRY+048>:    std    3, 56(1)
   0xF8810040,    // <ENTRY+052>:    std    4, 64(1)
   0xF8A10048,    // <ENTRY+056>:    std    5, 72(1)
   0xF8C10050,    // <ENTRY+060>:    std    6, 80(1)
   0xF8E10058,    // <ENTRY+064>:    std    7, 88(1)
   0xF9010060,    // <ENTRY+068>:    std    8, 96(1)
   0xF9210068,    // <ENTRY+072>:    std    9, 104(1)
   0xF9410070,    // <ENTRY+076>:    std    10, 112(1)
   0xF9810080,    // <ENTRY+080>:    std    12, 128(1)
   0xE98C00A0,    // <ENTRY+084>:    ld     12, 9000f-1000b+8(12)
   0x7D8903A6,    // <ENTRY+088>:    mtctr  12
   0x4E800421,    // <ENTRY+092>:    bctrl
   0x7C6B1B78,    // <ENTRY+096>:    mr     11, 3
   0xE8610038,    // <ENTRY+100>:    ld     3, 56(1)
   0xE8810040,    // <ENTRY+104>:    ld     4, 64(1)
   0xE8A10048,    // <ENTRY+108>:    ld     5, 72(1)
   0xE8C10050,    // <ENTRY+112>:    ld     6, 80(1)
   0xE8E10058,    // <ENTRY+116>:    ld     7, 88(1)
   0xE9010060,    // <ENTRY+120>:    ld     8, 96(1)
   0xE9210068,    // <ENTRY+124>:    ld     9, 104(1)
   0xE9410070,    // <ENTRY+128>:    ld     10, 112(1)
   0xE9810080,    // <ENTRY+132>:    ld     12, 128(1)
   0x38210090,    // <ENTRY+136>:    addi   1, 1, 144
   0xE8010010,    // <ENTRY+140>:    ld     0, 16(1)
   0x7C0803A6,    // <ENTRY+144>:    mtlr   0
   0x4BFFFF84,    // <ENTRY+148>:    b      1050b
   // 9000:
   0, 0,          // <ENTRY+152>:    .quad ENTRY_CURRENT_TABLE
   0, 0,          // <ENTRY+160>:    .quad ENTRY_CURRENT_TABLE_GET
   0, 0           // <ENTRY+168>:    .quad <slot>*8
};
static const uint64_t TEMPLATE_OFFSET_CURRENT_TABLE = sizeof(code_templ) - 3*8;
static const uint64_t TEMPLATE_OFFSET_CURRENT_TABLE_GET = sizeof(code_templ) - 2*8;
static const uint64_t TEMPLATE_OFFSET_SLOT = sizeof(code_templ) - 1*8;

void
entry_patch(mapi_func entry, int slot)
{
   char *code = (char *) entry;
   *((uint64_t *) (code + TEMPLATE_OFFSET_CURRENT_TABLE)) = (uint64_t) ENTRY_CURRENT_TABLE;
   *((uint64_t *) (code + TEMPLATE_OFFSET_CURRENT_TABLE_GET)) = (uint64_t) ENTRY_CURRENT_TABLE_GET;
   *((uint64_t *) (code + TEMPLATE_OFFSET_SLOT)) = slot * sizeof(mapi_func);
}

mapi_func
entry_generate(int slot)
{
   char *code;
   mapi_func entry;

   code = u_execmem_alloc(sizeof(code_templ));
   if (!code)
      return NULL;

   memcpy(code, code_templ, sizeof(code_templ));

   entry = (mapi_func) code;
   entry_patch(entry, slot);

   return entry;
}

#endif /* MAPI_MODE_BRIDGE */
