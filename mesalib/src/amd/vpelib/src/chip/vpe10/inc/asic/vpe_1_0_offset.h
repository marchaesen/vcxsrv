/* Copyright 2024 Advanced Micro Devices, Inc.
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
#ifndef VPE_1_0_OFFSET_H
#define VPE_1_0_OFFSET_H


#define MAX_INSTANCE                                        9
#define MAX_SEGMENT                                         7


struct IP_BASE_INSTANCE
{
    unsigned int segment[MAX_SEGMENT];
};

struct IP_BASE
{
    struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
};


struct IP_SIZE
{
    unsigned int segment[1][MAX_SEGMENT];
};


static const struct IP_BASE VPE_BASE = { { { { 0x00011800, 0x02456C00, 0x3C56C000, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } },
                                        { { 0, 0, 0, 0, 0, 0 } } } };
static const struct IP_SIZE VPE_SIZE = { { { 0x00001400, 0x00000400, 0x00004000, 0, 0, 0, 0 } } };



#define VPE_BASE__INST0_SEG0                       0x00011800
#define VPE_BASE__INST0_SEG1                       0x02456C00
#define VPE_BASE__INST0_SEG2                       0x3C56C000
#define VPE_BASE__INST0_SEG3                       0
#define VPE_BASE__INST0_SEG4                       0
#define VPE_BASE__INST0_SEG5                       0
#define VPE_BASE__INST0_SEG6                       0

#define VPE_BASE__INST1_SEG0                       0
#define VPE_BASE__INST1_SEG1                       0
#define VPE_BASE__INST1_SEG2                       0
#define VPE_BASE__INST1_SEG3                       0
#define VPE_BASE__INST1_SEG4                       0
#define VPE_BASE__INST1_SEG5                       0
#define VPE_BASE__INST1_SEG6                       0

#define VPE_BASE__INST2_SEG0                       0
#define VPE_BASE__INST2_SEG1                       0
#define VPE_BASE__INST2_SEG2                       0
#define VPE_BASE__INST2_SEG3                       0
#define VPE_BASE__INST2_SEG4                       0
#define VPE_BASE__INST2_SEG5                       0
#define VPE_BASE__INST2_SEG6                       0

#define VPE_BASE__INST3_SEG0                       0
#define VPE_BASE__INST3_SEG1                       0
#define VPE_BASE__INST3_SEG2                       0
#define VPE_BASE__INST3_SEG3                       0
#define VPE_BASE__INST3_SEG4                       0
#define VPE_BASE__INST3_SEG5                       0
#define VPE_BASE__INST3_SEG6                       0

#define VPE_BASE__INST4_SEG0                       0
#define VPE_BASE__INST4_SEG1                       0
#define VPE_BASE__INST4_SEG2                       0
#define VPE_BASE__INST4_SEG3                       0
#define VPE_BASE__INST4_SEG4                       0
#define VPE_BASE__INST4_SEG5                       0
#define VPE_BASE__INST4_SEG6                       0

#define VPE_BASE__INST5_SEG0                       0
#define VPE_BASE__INST5_SEG1                       0
#define VPE_BASE__INST5_SEG2                       0
#define VPE_BASE__INST5_SEG3                       0
#define VPE_BASE__INST5_SEG4                       0
#define VPE_BASE__INST5_SEG5                       0
#define VPE_BASE__INST5_SEG6                       0

#define VPE_BASE__INST6_SEG0                       0
#define VPE_BASE__INST6_SEG1                       0
#define VPE_BASE__INST6_SEG2                       0
#define VPE_BASE__INST6_SEG3                       0
#define VPE_BASE__INST6_SEG4                       0
#define VPE_BASE__INST6_SEG5                       0
#define VPE_BASE__INST6_SEG6                       0

#define VPE_BASE__INST7_SEG0                       0
#define VPE_BASE__INST7_SEG1                       0
#define VPE_BASE__INST7_SEG2                       0
#define VPE_BASE__INST7_SEG3                       0
#define VPE_BASE__INST7_SEG4                       0
#define VPE_BASE__INST7_SEG5                       0
#define VPE_BASE__INST7_SEG6                       0

#define VPE_BASE__INST8_SEG0                       0
#define VPE_BASE__INST8_SEG1                       0
#define VPE_BASE__INST8_SEG2                       0
#define VPE_BASE__INST8_SEG3                       0
#define VPE_BASE__INST8_SEG4                       0
#define VPE_BASE__INST8_SEG5                       0
#define VPE_BASE__INST8_SEG6                       0

#endif
