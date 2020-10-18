/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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

#ifndef RADV_CONSTANTS_H
#define RADV_CONSTANTS_H

#define ATI_VENDOR_ID 0x1002

#define MAX_VBS         32
#define MAX_VERTEX_ATTRIBS 32
#define MAX_RTS          8
#define MAX_VIEWPORTS   16
#define MAX_SCISSORS    16
#define MAX_DISCARD_RECTANGLES 4
#define MAX_SAMPLE_LOCATIONS 32
#define MAX_PUSH_CONSTANTS_SIZE 128
#define MAX_PUSH_DESCRIPTORS 32
#define MAX_DYNAMIC_UNIFORM_BUFFERS 16
#define MAX_DYNAMIC_STORAGE_BUFFERS 8
#define MAX_DYNAMIC_BUFFERS (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)
#define MAX_SAMPLES_LOG2 4
#define NUM_META_FS_KEYS 12
#define RADV_MAX_DRM_DEVICES 8
#define MAX_VIEWS        8
#define MAX_SO_STREAMS 4
#define MAX_SO_BUFFERS 4
#define MAX_SO_OUTPUTS 64
#define MAX_INLINE_UNIFORM_BLOCK_SIZE (4ull * 1024 * 1024)
#define MAX_INLINE_UNIFORM_BLOCK_COUNT 64
#define MAX_BIND_POINTS 2 /* compute + graphics */

#define NUM_DEPTH_CLEAR_PIPELINES 3
#define NUM_DEPTH_DECOMPRESS_PIPELINES 3

/*
 * This is the point we switch from using CP to compute shader
 * for certain buffer operations.
 */
#define RADV_BUFFER_OPS_CS_THRESHOLD 4096

#define RADV_BUFFER_UPDATE_THRESHOLD 1024

/* descriptor index into scratch ring offsets */
#define RING_SCRATCH 0
#define RING_ESGS_VS 1
#define RING_ESGS_GS 2
#define RING_GSVS_VS 3
#define RING_GSVS_GS 4
#define RING_HS_TESS_FACTOR 5
#define RING_HS_TESS_OFFCHIP 6
#define RING_PS_SAMPLE_POSITIONS 7

/* max number of descriptor sets */
#define MAX_SETS 32

/* Make sure everything is addressable by a signed 32-bit int, and
 * our largest descriptors are 96 bytes.
 */
#define RADV_MAX_PER_SET_DESCRIPTORS ((1ull << 31 ) / 96)

/* Our buffer size fields allow only 2**32 - 1. We round that down to a multiple
 * of 4 bytes so we can align buffer sizes up.
 */
#define RADV_MAX_MEMORY_ALLOCATION_SIZE 0xFFFFFFFCull

/* Number of invocations in each subgroup. */
#define RADV_SUBGROUP_SIZE 64

#endif /* RADV_CONSTANTS_H */

