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
#include "util/list.h"
#include "util/ralloc.h"

#include "broadcom/cle/v3d_decoder.h"

#define __gen_user_data void
#define __gen_address_type uint32_t
#define __gen_address_offset(reloc) (*reloc)
#define __gen_emit_reloc(cl, reloc)
#define __gen_unpack_address(cl, s, e) (__gen_unpack_uint(cl, s, e) << (31 - (e - s)))

enum reloc_worklist_type {
        reloc_gl_shader_state,
};

struct reloc_worklist_entry {
        struct list_head link;

        enum reloc_worklist_type type;
        uint32_t addr;

        union {
                struct {
                        uint32_t num_attrs;
                } shader_state;
        };
};

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

static void
out(struct clif_dump *clif, const char *fmt, ...)
{
        va_list args;

        va_start(args, fmt);
        vfprintf(clif->out, fmt, args);
        va_end(args);
}

#include "broadcom/cle/v3d_packet_v33_pack.h"

static struct reloc_worklist_entry *
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
        struct v3d_group *inst = v3d_spec_find_instruction(clif->spec, cl);
        if (!inst) {
                out(clif, "0x%08x: Unknown packet %d!\n", offset, *cl);
                return false;
        }

        *size = v3d_group_get_length(inst);

        out(clif, "%s\n", v3d_group_get_name(inst));
        v3d_print_group(clif->out, inst, 0, cl, "");

        switch (*cl) {
        case V3D33_GL_SHADER_STATE_opcode: {
                struct V3D33_GL_SHADER_STATE values;
                V3D33_GL_SHADER_STATE_unpack(cl, &values);

                struct reloc_worklist_entry *reloc =
                        clif_dump_add_address_to_worklist(clif,
                                                          reloc_gl_shader_state,
                                                          values.address);
                if (reloc) {
                        reloc->shader_state.num_attrs =
                                values.number_of_attribute_arrays;
                }
                return true;
        }

        case V3D33_STORE_MULTI_SAMPLE_RESOLVED_TILE_COLOR_BUFFER_EXTENDED_opcode: {
                struct V3D33_STORE_MULTI_SAMPLE_RESOLVED_TILE_COLOR_BUFFER_EXTENDED values;
                V3D33_STORE_MULTI_SAMPLE_RESOLVED_TILE_COLOR_BUFFER_EXTENDED_unpack(cl, &values);

                if (values.last_tile_of_frame)
                        return false;
                break;
        }

        case V3D33_TRANSFORM_FEEDBACK_ENABLE_opcode: {
                struct V3D33_TRANSFORM_FEEDBACK_ENABLE values;
                V3D33_TRANSFORM_FEEDBACK_ENABLE_unpack(cl, &values);
                struct v3d_group *spec = v3d_spec_find_struct(clif->spec,
                                                              "Transform Feedback Output Data Spec");
                struct v3d_group *addr = v3d_spec_find_struct(clif->spec,
                                                              "Transform Feedback Output Address");
                assert(spec);
                assert(addr);

                cl += *size;

                for (int i = 0; i < values.number_of_16_bit_output_data_specs_following; i++) {
                        v3d_print_group(clif->out, spec, 0, cl, "");
                        cl += v3d_group_get_length(spec);
                        *size += v3d_group_get_length(spec);
                }

                for (int i = 0; i < values.number_of_32_bit_output_buffer_address_following; i++) {
                        v3d_print_group(clif->out, addr, 0, cl, "");
                        cl += v3d_group_get_length(addr);
                        *size += v3d_group_get_length(addr);
                }
                break;
        }

        case V3D33_HALT_opcode:
                return false;
        }

        return true;
}

static void
clif_dump_gl_shader_state_record(struct clif_dump *clif,
                                 struct reloc_worklist_entry *reloc, void *vaddr)
{
        struct v3d_group *state = v3d_spec_find_struct(clif->spec,
                                                       "GL Shader State Record");
        struct v3d_group *attr = v3d_spec_find_struct(clif->spec,
                                                      "GL Shader State Attribute Record");
        assert(state);
        assert(attr);

        out(clif, "GL Shader State Record at 0x%08x\n", reloc->addr);
        v3d_print_group(clif->out, state, 0, vaddr, "");
        vaddr += v3d_group_get_length(state);

        for (int i = 0; i < reloc->shader_state.num_attrs; i++) {
                out(clif, "  Attribute %d\n", i);
                v3d_print_group(clif->out, attr, 0, vaddr, "");
                vaddr += v3d_group_get_length(attr);
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
                        clif_dump_gl_shader_state_record(clif, reloc, vaddr);
                        break;
                }
                out(clif, "\n");
        }
}

void
clif_dump_add_cl(struct clif_dump *clif, uint32_t start, uint32_t end)
{
        uint32_t size;

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

        uint8_t *cl = start_vaddr;
        while (clif_dump_packet(clif, start, cl, &size)) {
                cl += size;
                start += size;

                if (cl == end_vaddr)
                        break;
        }

        out(clif, "\n");

        clif_process_worklist(clif);
}
