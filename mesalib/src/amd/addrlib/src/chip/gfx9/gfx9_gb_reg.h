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

#if !defined (__GFX9_GB_REG_H__)
#define __GFX9_GB_REG_H__

/*
*    gfx9_gb_reg.h
*
*    Register Spec Release:  1.0
*
*/

//
// Make sure the necessary endian defines are there.
//
#if defined(LITTLEENDIAN_CPU)
#elif defined(BIGENDIAN_CPU)
#else
#error "BIGENDIAN_CPU or LITTLEENDIAN_CPU must be defined"
#endif

union GB_ADDR_CONFIG_gfx9 {
    struct {
#if        defined(LITTLEENDIAN_CPU)
        unsigned int                       NUM_PIPES : 3;
        unsigned int            PIPE_INTERLEAVE_SIZE : 3;
        unsigned int            MAX_COMPRESSED_FRAGS : 2;
        unsigned int            BANK_INTERLEAVE_SIZE : 3;
        unsigned int                                 : 1;
        unsigned int                       NUM_BANKS : 3;
        unsigned int                                 : 1;
        unsigned int         SHADER_ENGINE_TILE_SIZE : 3;
        unsigned int              NUM_SHADER_ENGINES : 2;
        unsigned int                        NUM_GPUS : 3;
        unsigned int             MULTI_GPU_TILE_SIZE : 2;
        unsigned int                   NUM_RB_PER_SE : 2;
        unsigned int                        ROW_SIZE : 2;
        unsigned int                 NUM_LOWER_PIPES : 1;
        unsigned int                       SE_ENABLE : 1;
#elif        defined(BIGENDIAN_CPU)
        unsigned int                       SE_ENABLE : 1;
        unsigned int                 NUM_LOWER_PIPES : 1;
        unsigned int                        ROW_SIZE : 2;
        unsigned int                   NUM_RB_PER_SE : 2;
        unsigned int             MULTI_GPU_TILE_SIZE : 2;
        unsigned int                        NUM_GPUS : 3;
        unsigned int              NUM_SHADER_ENGINES : 2;
        unsigned int         SHADER_ENGINE_TILE_SIZE : 3;
        unsigned int                                 : 1;
        unsigned int                       NUM_BANKS : 3;
        unsigned int                                 : 1;
        unsigned int            BANK_INTERLEAVE_SIZE : 3;
        unsigned int            MAX_COMPRESSED_FRAGS : 2;
        unsigned int            PIPE_INTERLEAVE_SIZE : 3;
        unsigned int                       NUM_PIPES : 3;
#endif
    } bitfields, bits;
    unsigned int    u32All;
    signed int    i32All;
    float    f32All;
};

#endif

