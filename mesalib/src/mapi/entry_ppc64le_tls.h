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
#define PPC64LE_ENTRY_SIZE 64
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

#define STUB_ASM_CODE(slot)                                     \
   "  addis  11, 2, " ENTRY_CURRENT_TABLE "@got@tprel@ha\n\t"   \
   "  ld     11, " ENTRY_CURRENT_TABLE "@got@tprel@l(11)\n\t"   \
   "  add    11, 11," ENTRY_CURRENT_TABLE "@tls\n\t"            \
   "  ld     11, 0(11)\n\t"                                     \
   "  ld     12, " slot "*8(11)\n\t"                            \
   "  mtctr  12\n\t"                                            \
   "  bctr\n"                                                   \

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

__asm__(".text\n");

__asm__("ppc64le_dispatch_tls:\n\t"
        "  addis  3, 2, " ENTRY_CURRENT_TABLE "@got@tprel@ha\n\t"
        "  ld     3, " ENTRY_CURRENT_TABLE "@got@tprel@l(3)\n\t"
        "  blr\n"
        );

extern uint64_t ppc64le_dispatch_tls();

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
   0x7C0802A6,    // <ENTRY+00>:   mflr   0
   0xF8010010,    // <ENTRY+04>:   std    0, 16(1)
   0xE96C0028,    // <ENTRY+08>:   ld     11, 9000f-1000b+0(12)
   0x7D6B6A14,    // <ENTRY+12>:   add    11, 11, 13
   0xE96B0000,    // <ENTRY+16>:   ld     11, 0(11)
   0xE80C0030,    // <ENTRY+20>:   ld     0, 9000f-1000b+8(12)
   0x7D8B002A,    // <ENTRY+24>:   ldx    12, 11, 0
   0x7D8903A6,    // <ENTRY+28>:   mtctr  12
   0x4E800420,    // <ENTRY+32>:   bctr
   0x60000000,    // <ENTRY+36>:   nop
   // 9000:
   0, 0,          // <ENTRY+40>:    .quad _glapi_tls_Dispatch
   0, 0           // <ENTRY+48>:    .quad <slot>*8
};
static const uint64_t TEMPLATE_OFFSET_TLS_ADDR = sizeof(code_templ) - 2*8;
static const uint64_t TEMPLATE_OFFSET_SLOT = sizeof(code_templ) - 1*8;

void
entry_patch(mapi_func entry, int slot)
{
   char *code = (char *) entry;
   *((uint64_t *) (code + TEMPLATE_OFFSET_TLS_ADDR)) = ppc64le_dispatch_tls();
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
