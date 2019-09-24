/*
 * Copyright (C) 2018 Ryan Houdek <Sonicadvance1@gmail.com>
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

#ifndef __bifrost_compile_h__
#define __bifrost_compile_h__

#include "compiler/nir/nir.h"
#include "util/u_dynarray.h"

struct bifrost_program {
        struct util_dynarray compiled;
};

int
bifrost_compile_shader_nir(nir_shader *nir, struct bifrost_program *program);

static const nir_shader_compiler_options bifrost_nir_options = {
        .fuse_ffma = true,
        .lower_flrp16 = true,
        .lower_flrp32 = true,
        .lower_flrp64 = true,
        .lower_fmod = true,
        .lower_bitfield_extract = true,
        .lower_bitfield_extract_to_shifts = true,
        .lower_bitfield_insert = true,
        .lower_bitfield_insert_to_shifts = true,
        .lower_bitfield_reverse = true,
        .lower_idiv = true,
        .lower_isign = true,
        .lower_fsign = true,
        .lower_ffract = true,
        .lower_fdph = true,
        .lower_pack_half_2x16 = true,
        .lower_pack_unorm_2x16 = true,
        .lower_pack_snorm_2x16 = true,
        .lower_pack_unorm_4x8 = true,
        .lower_pack_snorm_4x8 = true,
        .lower_unpack_half_2x16 = true,
        .lower_unpack_unorm_2x16 = true,
        .lower_unpack_snorm_2x16 = true,
        .lower_unpack_unorm_4x8 = true,
        .lower_unpack_snorm_4x8 = true,
        .lower_extract_byte = true,
        .lower_extract_word = true,
        .lower_all_io_to_temps = true,
        .lower_all_io_to_elements = true,
        .vertex_id_zero_based = true,
};

#endif
