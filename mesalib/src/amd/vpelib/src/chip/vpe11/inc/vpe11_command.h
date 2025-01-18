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
#include "vpe10_command.h"

#ifdef __cplusplus
    extern "C" {
#endif

#undef VPE_DESC_CD__SHIFT
#undef VPE_DESC_CD_MASK
#undef VPE_DESC_CMD_HEADER

#define VPE_DESC_CD__SHIFT 16
#define VPE_DESC_CD_MASK   0x001F0000

#define VPE_DESC_CMD_HEADER(cd)                                                                    \
    (VPE_CMD_HEADER(VPE_CMD_OPCODE_VPE_DESC, 0) | (((cd) << VPE_DESC_CD__SHIFT) & VPE_DESC_CD_MASK))

        // Collaborate sync Command Header
#define VPE_COLLABORATE_SYNC_HEADER_MASK                 0x000000FF
#define VPE_COLLABORATE_SYNC_DATA_MASK(collaborate_data) ((collaborate_data) & 0xFFFFFFFF)
#define VPE_COLLABORATE_SYNC_CMD_HEADER                                                            \
    (VPE_CMD_HEADER(VPE_CMD_OPCODE_COLLABORATE_SYNC, 0) & VPE_COLLABORATE_SYNC_HEADER_MASK)   

#ifdef __cplusplus
}
#endif
