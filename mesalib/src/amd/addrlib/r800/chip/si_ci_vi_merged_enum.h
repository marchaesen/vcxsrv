/*
 * Copyright © 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */
#if !defined (SI_CI_VI_MERGED_ENUM_HEADER)
#define SI_CI_VI_MERGED_ENUM_HEADER

typedef enum PipeInterleaveSize {
ADDR_CONFIG_PIPE_INTERLEAVE_256B         = 0x00000000,
ADDR_CONFIG_PIPE_INTERLEAVE_512B         = 0x00000001,
} PipeInterleaveSize;

typedef enum RowSize {
ADDR_CONFIG_1KB_ROW                      = 0x00000000,
ADDR_CONFIG_2KB_ROW                      = 0x00000001,
ADDR_CONFIG_4KB_ROW                      = 0x00000002,
} RowSize;

#endif
