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

#include <string.h>
#include "broadcom/cle/v3d_decoder.h"
#include "clif_dump.h"
#include "clif_private.h"

#define __gen_user_data void
#define __gen_address_type uint32_t
#define __gen_address_offset(reloc) (*reloc)
#define __gen_emit_reloc(cl, reloc)
#define __gen_unpack_address(cl, s, e) (__gen_unpack_uint(cl, s, e) << (31 - (e - s)))
#include "broadcom/cle/v3dx_pack.h"
#include "broadcom/common/v3d_macros.h"

bool
v3dX(clif_dump_packet)(struct clif_dump *clif, uint32_t offset,
                       const uint8_t *cl, uint32_t *size)
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
        case V3DX(GL_SHADER_STATE_opcode): {
                struct V3DX(GL_SHADER_STATE) values;
                V3DX(GL_SHADER_STATE_unpack)(cl, &values);

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

#if V3D_VERSION < 40
        case V3DX(STORE_MULTI_SAMPLE_RESOLVED_TILE_COLOR_BUFFER_EXTENDED_opcode): {
                struct V3DX(STORE_MULTI_SAMPLE_RESOLVED_TILE_COLOR_BUFFER_EXTENDED) values;
                V3DX(STORE_MULTI_SAMPLE_RESOLVED_TILE_COLOR_BUFFER_EXTENDED_unpack)(cl, &values);

                if (values.last_tile_of_frame)
                        return false;
                break;
        }
#endif /* V3D_VERSION < 40 */

#if V3D_VERSION > 40
        case V3DX(TRANSFORM_FEEDBACK_SPECS_opcode): {
                struct V3DX(TRANSFORM_FEEDBACK_SPECS) values;
                V3DX(TRANSFORM_FEEDBACK_SPECS_unpack)(cl, &values);
                struct v3d_group *spec = v3d_spec_find_struct(clif->spec,
                                                              "Transform Feedback Output Data Spec");
                assert(spec);

                cl += *size;

                for (int i = 0; i < values.number_of_16_bit_output_data_specs_following; i++) {
                        v3d_print_group(clif->out, spec, 0, cl, "");
                        cl += v3d_group_get_length(spec);
                        *size += v3d_group_get_length(spec);
                }
                break;
        }
#else /* V3D_VERSION < 40 */
        case V3DX(TRANSFORM_FEEDBACK_ENABLE_opcode): {
                struct V3DX(TRANSFORM_FEEDBACK_ENABLE) values;
                V3DX(TRANSFORM_FEEDBACK_ENABLE_unpack)(cl, &values);
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
#endif /* V3D_VERSION < 40 */

        case V3DX(START_ADDRESS_OF_GENERIC_TILE_LIST_opcode): {
                struct V3DX(START_ADDRESS_OF_GENERIC_TILE_LIST) values;
                V3DX(START_ADDRESS_OF_GENERIC_TILE_LIST_unpack)(cl, &values);
                struct reloc_worklist_entry *reloc =
                        clif_dump_add_address_to_worklist(clif,
                                                          reloc_generic_tile_list,
                                                          values.start);
                reloc->generic_tile_list.end = values.end;
                break;
        }

        case V3DX(HALT_opcode):
                return false;
        }

        return true;
}

void
v3dX(clif_dump_gl_shader_state_record)(struct clif_dump *clif,
                                       struct reloc_worklist_entry *reloc,
                                       void *vaddr)
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
