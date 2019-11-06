/*
 * Copyright © 2007-2019 Advanced Micro Devices, Inc.
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

#if !defined (__GFX10_GB_REG_H__)
#define __GFX10_GB_REG_H__

/*
*    gfx10_gb_reg.h
*
*    Register Spec Release:  1.0
*
*/

//
// Make sure the necessary endian defines are there.
//
#include "util/u_endian.h"

#if UTIL_ARCH_LITTLE_ENDIAN
#define LITTLEENDIAN_CPU
#elif UTIL_ARCH_BIG_ENDIAN
#define BIGENDIAN_CPU
#endif

union GB_ADDR_CONFIG
{
    struct
    {
#if defined(LITTLEENDIAN_CPU)
        unsigned int                       NUM_PIPES : 3;
        unsigned int            PIPE_INTERLEAVE_SIZE : 3;
        unsigned int            MAX_COMPRESSED_FRAGS : 2;
        unsigned int                       NUM_PKRS  : 3;
        unsigned int                                 : 21;
#elif defined(BIGENDIAN_CPU)
        unsigned int                                 : 21;
        unsigned int                       NUM_PKRS  : 3;
        unsigned int            MAX_COMPRESSED_FRAGS : 2;
        unsigned int            PIPE_INTERLEAVE_SIZE : 3;
        unsigned int                       NUM_PIPES : 3;
#endif
    } bitfields, bits;
    unsigned int    u32All;
    int             i32All;
    float           f32All;
};

#endif

