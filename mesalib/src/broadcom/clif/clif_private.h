/*
 * Copyright © 2016-2018 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef CLIF_PRIVATE_H
#define CLIF_PRIVATE_H

#include <stdint.h>
#include <stdarg.h>
#include "util/list.h"

struct clif_dump {
        const struct v3d_device_info *devinfo;
        bool (*lookup_vaddr)(void *data, uint32_t addr, void **vaddr);
        FILE *out;
        /* Opaque data from the caller that is passed to the callbacks. */
        void *data;

        struct v3d_spec *spec;

        /* List of struct reloc_worklist_entry */
        struct list_head worklist;
};

enum reloc_worklist_type {
        reloc_gl_shader_state,
        reloc_generic_tile_list,
};

struct reloc_worklist_entry {
        struct list_head link;

        enum reloc_worklist_type type;
        uint32_t addr;

        union {
                struct {
                        uint32_t num_attrs;
                } shader_state;
                struct {
                        uint32_t end;
                } generic_tile_list;
        };
};

struct reloc_worklist_entry *
clif_dump_add_address_to_worklist(struct clif_dump *clif,
                                  enum reloc_worklist_type type,
                                  uint32_t addr);

bool v3d33_clif_dump_packet(struct clif_dump *clif, uint32_t offset,
                            const uint8_t *cl, uint32_t *size);
void v3d33_clif_dump_gl_shader_state_record(struct clif_dump *clif,
                                            struct reloc_worklist_entry *reloc,
                                            void *vaddr);
bool v3d41_clif_dump_packet(struct clif_dump *clif, uint32_t offset,
                            const uint8_t *cl, uint32_t *size);
void v3d41_clif_dump_gl_shader_state_record(struct clif_dump *clif,
                                            struct reloc_worklist_entry *reloc,
                                            void *vaddr);
bool v3d42_clif_dump_packet(struct clif_dump *clif, uint32_t offset,
                            const uint8_t *cl, uint32_t *size);
void v3d42_clif_dump_gl_shader_state_record(struct clif_dump *clif,
                                            struct reloc_worklist_entry *reloc,
                                            void *vaddr);

static inline void
out(struct clif_dump *clif, const char *fmt, ...)
{
        va_list args;

        va_start(args, fmt);
        vfprintf(clif->out, fmt, args);
        va_end(args);
}

#endif /* CLIF_PRIVATE_H */
