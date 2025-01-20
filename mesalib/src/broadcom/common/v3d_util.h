/*
 * Copyright © 2021 Raspberry Pi Ltd
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

#ifndef V3D_UTIL_H
#define V3D_UTIL_H

#include "util/macros.h"
#include "common/v3d_device_info.h"
#include "compiler/shader_enums.h"
#include "util/format/u_formats.h"

#if USE_V3D_SIMULATOR
#  include "simulator/v3d_simulator.h"
#else
#  include "xf86drm.h"
#endif

/* Helper to call hw ver specific functions */
#define v3d_X(devinfo, thing) ({                                \
        __typeof(&v3d42_##thing) v3d_X_thing;                   \
        switch (devinfo->ver) {                                 \
        case 42:                                                \
                v3d_X_thing = &v3d42_##thing;                   \
                break;                                          \
        case 71:                                                \
                v3d_X_thing = &v3d71_##thing;                   \
                break;                                          \
        default:                                                \
                unreachable("Unsupported hardware generation"); \
        }                                                       \
        v3d_X_thing;                                            \
})

uint32_t
v3d_csd_choose_workgroups_per_supergroup(struct v3d_device_info *devinfo,
                                         bool has_subgroups,
                                         bool has_tsy_barrier,
                                         uint32_t threads,
                                         uint32_t num_wgs,
                                         uint32_t wg_size);

void
v3d_choose_tile_size(const struct v3d_device_info *devinfo,
                     uint32_t color_attachment_count,
                     uint32_t max_internal_bpp,
                     uint32_t total_color_bpp,
                     bool msaa,
                     bool double_buffer,
                     uint32_t *width,
                     uint32_t *height);

uint32_t
v3d_translate_pipe_swizzle(enum pipe_swizzle swizzle);

uint32_t
v3d_hw_prim_type(enum mesa_prim prim_type);

uint32_t
v3d_internal_bpp_words(uint32_t internal_bpp);

/* Some configuration packets want the size on log2, but starting at 0 for
 * size 8.
 */
static inline uint8_t
log2_tile_size(uint32_t size)
{
        switch(size) {
        case 8:
                return 0;
        case 16:
                return 1;
        case 32:
                return 2;
        case 64:
                return 3;
        default:
                unreachable("Unsupported tile width/height");
        }
}

uint32_t
v3d_compute_rt_row_row_stride_128_bits(uint32_t tile_width,
                                       uint32_t bpp);

static inline int
v3d_ioctl(int fd, unsigned long request, void *arg)
{
#if USE_V3D_SIMULATOR
        return v3d_simulator_ioctl(fd, request, arg);
#else
        return drmIoctl(fd, request, arg);
#endif
}
#endif
