/* Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#pragma once
#include "vpe_command.h"

#ifdef __cplusplus
extern "C" {
#endif
/***************************
 * VPE Descriptor
 ***************************/
#define VPE_DESC_CD__SHIFT 16
#define VPE_DESC_CD_MASK   0x000F0000

#define VPE_DESC_ADDR__SHIFT    32
#define VPE_DESC_HIGH_ADDR_MASK 0xFFFFFFFF00000000

/* The lowest bits are reuse and tmz as bit 1 and bit 0.
   Smibs will substract the address with emb gpuva to
   get offset and then reuse bit will be preserved
   So as long as the embedded buffer is allocated
   at correct alignment (currently low addr is [31:2]
   which means we need a 4 byte(2 bit) alignment),
   the offset generated will still cover the
   reuse bit as part of it.
   Ex : Address : 0x200036 GPU Virtual Address : 0x200000
   offset is 0x36 which keeps the reuse bit */
#define VPE_DESC_LOW_ADDR_MASK  0x00000000FFFFFFFF
#define VPE_DESC_REUSE_TMZ_MASK 0x0000000000000003

#define VPE_DESC_NUM_CONFIG_DESCRIPTOR__SHIFT 0
#define VPE_DESC_NUM_CONFIG_DESCRIPTOR_MASK   0x000000FF

#define VPE_DESC_REUSE__MASK 0x00000002

#define VPE_DESC_CMD_HEADER(cd)                                                                    \
    (VPE_CMD_HEADER(VPE_CMD_OPCODE_VPE_DESC, 0) | (((cd) << VPE_DESC_CD__SHIFT) & VPE_DESC_CD_MASK))

/***************************
 * VPE Plane Config
 ***************************/
enum VPE_PLANE_CFG_SUBOP {
    VPE_PLANE_CFG_SUBOP_1_TO_1 = 0x0,
    VPE_PLANE_CFG_SUBOP_2_TO_1 = 0x1,
    VPE_PLANE_CFG_SUBOP_2_TO_2 = 0x2
};

#define VPE_PLANE_CFG_ONE_PLANE  0
#define VPE_PLANE_CFG_TWO_PLANES 1

#define VPE_PLANE_CFG_NPS0__SHIFT 16
#define VPE_PLANE_CFG_NPS0_MASK   0x00030000

#define VPE_PLANE_CFG_NPD0__SHIFT 18
#define VPE_PLANE_CFG_NPD0_MASK   0x000C0000

#define VPE_PLANE_CFG_NPS1__SHIFT 20
#define VPE_PLANE_CFG_NPS1_MASK   0x00300000

#define VPE_PLANE_CFG_NPD1__SHIFT 22
#define VPE_PLANE_CFG_NPD1_MASK   0x00C00000

#define VPE_PLANE_CFG_TMZ__SHIFT 16
#define VPE_PLANE_CFG_TMZ_MASK   0x00010000

#define VPE_PLANE_CFG_SWIZZLE_MODE__SHIFT 3
#define VPE_PLANE_CFG_SWIZZLE_MODE_MASK   0x000000F8

#define VPE_PLANE_CFG_ROTATION__SHIFT 0
#define VPE_PLANE_CFG_ROTATION_MASK   0x00000003

#define VPE_PLANE_CFG_MIRROR__SHIFT 0
#define VPE_PLANE_CFG_MIRROR_MASK   0x00000003

#define VPE_PLANE_ADDR_LO__SHIFT 0
#define VPE_PLANE_ADDR_LO_MASK   0xFFFFFF00

#define VPE_PLANE_CFG_PITCH__SHIFT 0
#define VPE_PLANE_CFG_PITCH_MASK   0x00003FFF

#define VPE_PLANE_CFG_VIEWPORT_Y__SHIFT 16
#define VPE_PLANE_CFG_VIEWPORT_Y_MASK   0x3FFF0000
#define VPE_PLANE_CFG_VIEWPORT_X__SHIFT 0
#define VPE_PLANE_CFG_VIEWPORT_X_MASK   0x00003FFF

#define VPE_PLANE_CFG_VIEWPORT_HEIGHT__SHIFT       16
#define VPE_PLANE_CFG_VIEWPORT_HEIGHT_MASK         0x1FFF0000
#define VPE_PLANE_CFG_VIEWPORT_ELEMENT_SIZE__SHIFT 13
#define VPE_PLANE_CFG_VIEWPORT_ELEMENT_SIZE_MASK   0x0000E000
#define VPE_PLANE_CFG_VIEWPORT_WIDTH__SHIFT        0
#define VPE_PLANE_CFG_VIEWPORT_WIDTH_MASK          0x00001FFF

#define VPE_PLANE_ADDR__SHIFT    32
#define VPE_PLANE_HIGH_ADDR_MASK 0xFFFFFFFF00000000

#define VPE_PLANE_LOW_ADDR_MASK  0x00000000FFFFFFFF
#define VPE_PLANE_REUSE_TMZ_MASK 0x0000000000000003

enum VPE_PLANE_CFG_ELEMENT_SIZE {
    VPE_PLANE_CFG_ELEMENT_SIZE_8BPE  = 0,
    VPE_PLANE_CFG_ELEMENT_SIZE_16BPE = 1,
    VPE_PLANE_CFG_ELEMENT_SIZE_32BPE = 2,
    VPE_PLANE_CFG_ELEMENT_SIZE_64BPE = 3
};

#define VPE_PLANE_CFG_CMD_HEADER(subop, nps0, npd0, nps1, npd1)                                    \
    (VPE_CMD_HEADER(VPE_CMD_OPCODE_PLANE_CFG, subop) |                                             \
        (((nps0) << VPE_PLANE_CFG_NPS0__SHIFT) & VPE_PLANE_CFG_NPS0_MASK) |                        \
        (((npd0) << VPE_PLANE_CFG_NPD0__SHIFT) & VPE_PLANE_CFG_NPD0_MASK) |                        \
        (((nps1) << VPE_PLANE_CFG_NPS1__SHIFT) & VPE_PLANE_CFG_NPS1_MASK) |                        \
        (((npd0) << VPE_PLANE_CFG_NPD1__SHIFT) & VPE_PLANE_CFG_NPD1_MASK))


#ifdef __cplusplus
}
#endif
