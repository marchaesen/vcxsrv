/*
 * Copyright © 2019 Broadcom
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

#ifndef V3D_LIMITS_H
#define V3D_LIMITS_H

/* Number of channels a QPU thread executes in parallel.  Also known as
 * gl_SubGroupSizeARB.
 */
#define V3D_CHANNELS 16

#define V3D_MAX_FS_INPUTS 64
#define V3D_MAX_GS_INPUTS 64
#define V3D_MAX_VS_INPUTS 64
#define V3D_MAX_ANY_STAGE_INPUTS MAX3(V3D_MAX_VS_INPUTS, \
                                      V3D_MAX_GS_INPUTS, \
                                      V3D_MAX_FS_INPUTS)

/* For now we need to maintain a different limits for OpenGL and Vulkan due
 * some OpenGL CTS tests hitting register allocation when trying to use all
 * the texture available.
 *
 * FIXME: nir_schedule should be able to handle that. When fixed it would be
 * simpler to keep just one limit
 */
#define V3D_VULKAN_MAX_TEXTURE_SAMPLERS 24
#define V3D_OPENGL_MAX_TEXTURE_SAMPLERS 16

/* Not specifically a hardware limit, just coordination between compiler and
 * driver.
 */
#define V3D_MAX_TEXTURE_SAMPLERS MAX2(V3D_VULKAN_MAX_TEXTURE_SAMPLERS, \
                                      V3D_OPENGL_MAX_TEXTURE_SAMPLERS)

/* The HW can do 16384 (15), but we run into hangs when we expose that. */
#define V3D_MAX_MIP_LEVELS 13

#define V3D_MAX_SAMPLES 4

#define V3D_MAX_DRAW_BUFFERS 4

#define V3D_MAX_POINT_SIZE 512.0f
#define V3D_MAX_LINE_WIDTH 32

#endif /* V3D_LIMITS_H */
