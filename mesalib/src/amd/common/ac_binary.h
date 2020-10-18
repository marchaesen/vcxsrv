/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef AC_BINARY_H
#define AC_BINARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct radeon_info;

struct ac_shader_config {
   unsigned num_sgprs;
   unsigned num_vgprs;
   unsigned num_shared_vgprs; /* GFX10: number of VGPRs shared between half-waves */
   unsigned spilled_sgprs;
   unsigned spilled_vgprs;
   unsigned lds_size; /* in HW allocation units; i.e 256 bytes on SI, 512 bytes on CI+ */
   unsigned spi_ps_input_ena;
   unsigned spi_ps_input_addr;
   unsigned float_mode;
   unsigned scratch_bytes_per_wave;
   unsigned rsrc1;
   unsigned rsrc2;
   unsigned rsrc3;
};

void ac_parse_shader_binary_config(const char *data, size_t nbytes, unsigned wave_size,
                                   bool really_needs_scratch, const struct radeon_info *info,
                                   struct ac_shader_config *conf);

#ifdef __cplusplus
}
#endif

#endif /* AC_BINARY_H */
