/*
 * Copyright © 2021 Raspberry Pi
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
#ifndef V3DV_META_COMMON_H
#define V3DV_META_COMMON_H

/* Disable level 0 write, just write following mipmaps */
#define V3D_TFU_IOA_DIMTW (1 << 0)
#define V3D_TFU_IOA_FORMAT_SHIFT 3
#define V3D_TFU_IOA_FORMAT_LINEARTILE 3
#define V3D_TFU_IOA_FORMAT_UBLINEAR_1_COLUMN 4
#define V3D_TFU_IOA_FORMAT_UBLINEAR_2_COLUMN 5
#define V3D_TFU_IOA_FORMAT_UIF_NO_XOR 6
#define V3D_TFU_IOA_FORMAT_UIF_XOR 7

#define V3D_TFU_ICFG_NUMMM_SHIFT 5
#define V3D_TFU_ICFG_TTYPE_SHIFT 9

#define V3D_TFU_ICFG_OPAD_SHIFT 22

#define V3D_TFU_ICFG_FORMAT_SHIFT 18
#define V3D_TFU_ICFG_FORMAT_RASTER 0
#define V3D_TFU_ICFG_FORMAT_SAND_128 1
#define V3D_TFU_ICFG_FORMAT_SAND_256 2
#define V3D_TFU_ICFG_FORMAT_LINEARTILE 11
#define V3D_TFU_ICFG_FORMAT_UBLINEAR_1_COLUMN 12
#define V3D_TFU_ICFG_FORMAT_UBLINEAR_2_COLUMN 13
#define V3D_TFU_ICFG_FORMAT_UIF_NO_XOR 14
#define V3D_TFU_ICFG_FORMAT_UIF_XOR 15

/**
 * Copy/Clear operations implemented in v3dv_meta_*.c that use the TLB hardware
 * need to figure out TLB programming from the target image data instead of an
 * actual Vulkan framebuffer object. For the most part, the job's frame tiling
 * information is enough for this, however we still need additional information
 * such us the internal type of our single render target, so we use this
 * auxiliary struct to pass that information around.
 */
struct v3dv_meta_framebuffer {
   /* The internal type of the single render target */
   uint32_t internal_type;

   /* Supertile coverage */
   uint32_t min_x_supertile;
   uint32_t min_y_supertile;
   uint32_t max_x_supertile;
   uint32_t max_y_supertile;

   /* Format info */
   VkFormat vk_format;
   const struct v3dv_format *format;
   uint8_t internal_depth_type;
};

#endif
