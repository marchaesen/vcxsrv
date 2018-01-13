/*
 * Copyright © 2016 Broadcom
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clif_dump.h"
#include "clif_private.h"
#include "util/list.h"
#include "util/ralloc.h"

#include "broadcom/cle/v3d_decoder.h"

struct reloc_worklist_entry *
clif_dump_add_address_to_worklist(struct clif_dump *clif,
                                  enum reloc_worklist_type type,
                                  uint32_t addr)
{
        struct reloc_worklist_entry *entry =
                rzalloc(clif, struct reloc_worklist_entry);
        if (!entry)
                return NULL;

        entry->type = type;
        entry->addr = addr;

        list_addtail(&entry->link, &clif->worklist);

        return entry;
}

struct clif_dump *
clif_dump_init(const struct v3d_device_info *devinfo,
               FILE *out,
               bool (*lookup_vaddr)(void *data, uint32_t addr, void **vaddr),
               void *data)
{
        struct clif_dump *clif = rzalloc(NULL, struct clif_dump);

        clif->devinfo = devinfo;
        clif->lookup_vaddr = lookup_vaddr;
        clif->out = out;
        clif->data = data;
        clif->spec = v3d_spec_load(devinfo);

        list_inithead(&clif->worklist);

        return clif;
}

void
clif_dump_destroy(struct clif_dump *clif)
{
        ralloc_free(clif);
}

#define out_uint(_clif, field) out(_clif, "    /* %s = */ %u\n",        \
                            #field,  values-> field);

static bool
clif_dump_packet(struct clif_dump *clif, uint32_t offset, const uint8_t *cl,
                 uint32_t *size)
{
        if (clif->devinfo->ver >= 41)
                return v3d41_clif_dump_packet(clif, offset, cl, size);
        else
                return v3d33_clif_dump_packet(clif, offset, cl, size);
}

static void
clif_dump_cl(struct clif_dump *clif, uint32_t start, uint32_t end)
{
        void *start_vaddr;
        if (!clif->lookup_vaddr(clif->data, start, &start_vaddr)) {
                out(clif, "Failed to look up address 0x%08x\n",
                    start);
                return;
        }

        /* The end address is optional (for example, a BRANCH instruction
         * won't set an end), but is used for BCL/RCL termination.
         */
        void *end_vaddr = NULL;
        if (end && !clif->lookup_vaddr(clif->data, end, &end_vaddr)) {
                out(clif, "Failed to look up address 0x%08x\n",
                    end);
                return;
        }

        uint32_t size;
        uint8_t *cl = start_vaddr;
        while (clif_dump_packet(clif, start, cl, &size)) {
                cl += size;
                start += size;

                if (cl == end_vaddr)
                        break;
        }
}

static void
clif_process_worklist(struct clif_dump *clif)
{
        while (!list_empty(&clif->worklist)) {
                struct reloc_worklist_entry *reloc =
                        list_first_entry(&clif->worklist,
                                         struct reloc_worklist_entry, link);
                list_del(&reloc->link);

                void *vaddr;
                if (!clif->lookup_vaddr(clif->data, reloc->addr, &vaddr)) {
                        out(clif, "Failed to look up address 0x%08x\n",
                            reloc->addr);
                        continue;
                }

                switch (reloc->type) {
                case reloc_gl_shader_state:
                        if (clif->devinfo->ver >= 41) {
                                v3d41_clif_dump_gl_shader_state_record(clif,
                                                                       reloc,
                                                                       vaddr);
                        } else {
                                v3d33_clif_dump_gl_shader_state_record(clif,
                                                                       reloc,
                                                                       vaddr);
                        }
                        break;
                case reloc_generic_tile_list:
                        clif_dump_cl(clif, reloc->addr,
                                     reloc->generic_tile_list.end);
                        break;
                }
                out(clif, "\n");
        }
}

void
clif_dump_add_cl(struct clif_dump *clif, uint32_t start, uint32_t end)
{
        clif_dump_cl(clif, start, end);
        out(clif, "\n");

        clif_process_worklist(clif);
}
