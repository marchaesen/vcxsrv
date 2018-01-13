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

#include "v3d_compiler.h"

/* We don't do any address packing. */
#define __gen_user_data void
#define __gen_address_type uint32_t
#define __gen_address_offset(reloc) (*reloc)
#define __gen_emit_reloc(cl, reloc)
#include "broadcom/cle/v3d_packet_v33_pack.h"

void
v3d33_vir_vpm_read_setup(struct v3d_compile *c, int num_components)
{
        struct V3D33_VPM_GENERIC_BLOCK_READ_SETUP unpacked = {
                V3D33_VPM_GENERIC_BLOCK_READ_SETUP_header,

                .horiz = true,
                .laned = false,
                /* If the field is 0, that means a read count of 32. */
                .num = num_components & 31,
                .segs = true,
                .stride = 1,
                .size = VPM_SETUP_SIZE_32_BIT,
                .addr = c->num_inputs,
        };

        uint32_t packed;
        V3D33_VPM_GENERIC_BLOCK_READ_SETUP_pack(NULL,
                                                (uint8_t *)&packed,
                                                &unpacked);
        vir_VPMSETUP(c, vir_uniform_ui(c, packed));
}

void
v3d33_vir_vpm_write_setup(struct v3d_compile *c)
{
        uint32_t packed;
        struct V3D33_VPM_GENERIC_BLOCK_WRITE_SETUP unpacked = {
                V3D33_VPM_GENERIC_BLOCK_WRITE_SETUP_header,

                .horiz = true,
                .laned = false,
                .segs = true,
                .stride = 1,
                .size = VPM_SETUP_SIZE_32_BIT,
                .addr = 0,
        };

        V3D33_VPM_GENERIC_BLOCK_WRITE_SETUP_pack(NULL,
                                                (uint8_t *)&packed,
                                                &unpacked);
        vir_VPMSETUP(c, vir_uniform_ui(c, packed));
}
