/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __MIDGARD_H_
#define __MIDGARD_H_

#include "compiler/nir/nir.h"
#include "util/u_dynarray.h"
#include "panfrost-job.h"
#include "panfrost/util/pan_ir.h"

int
midgard_compile_shader_nir(nir_shader *nir, panfrost_program *program, bool is_blend, unsigned blend_rt, unsigned gpu_id, bool shaderdb);

/* NIR options are shared between the standalone compiler and the online
 * compiler. Defining it here is the simplest, though maybe not the Right
 * solution. */

static const nir_shader_compiler_options midgard_nir_options = {
        .lower_ffma = true,
        .lower_sub = true,
        .lower_scmp = true,
        .lower_flrp32 = true,
        .lower_flrp64 = true,
        .lower_ffract = true,
        .lower_fmod = true,
        .lower_fdiv = true,
        .lower_idiv = true,
        .lower_isign = true,
        .lower_fpow = true,
        .lower_find_lsb = true,
        .lower_fdph = true,

        .lower_wpos_pntc = true,

        /* TODO: We have native ops to help here, which we'll want to look into
         * eventually */
        .lower_fsign = true,

        .lower_extract_byte = true,
        .lower_extract_word = true,
        .lower_rotate = true,

        .lower_pack_half_2x16 = true,
        .lower_pack_half_2x16_split = true,
        .lower_pack_unorm_2x16 = true,
        .lower_pack_snorm_2x16 = true,
        .lower_pack_unorm_4x8 = true,
        .lower_pack_snorm_4x8 = true,
        .lower_unpack_half_2x16 = true,
        .lower_unpack_half_2x16_split = true,
        .lower_unpack_unorm_2x16 = true,
        .lower_unpack_snorm_2x16 = true,
        .lower_unpack_unorm_4x8 = true,
        .lower_unpack_snorm_4x8 = true,

        .lower_doubles_options = nir_lower_dmod,

        .lower_bitfield_extract_to_shifts = true,
        .vectorize_io = true,
        .use_interpolated_input_intrinsics = true
};

#endif
