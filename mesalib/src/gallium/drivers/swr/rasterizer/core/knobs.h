/****************************************************************************
 * Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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
 *
 * @file knobs.h
 *
 * @brief Static (Compile-Time) Knobs for Core.
 *
 ******************************************************************************/
#pragma once

#include <stdint.h>
#include <gen_knobs.h>

#define KNOB_ARCH_AVX 0
#define KNOB_ARCH_AVX2 1
#define KNOB_ARCH_AVX512 2

///////////////////////////////////////////////////////////////////////////////
// AVX512 Support
///////////////////////////////////////////////////////////////////////////////

#define ENABLE_AVX512_SIMD16 1
#define USE_SIMD16_FRONTEND 1
#define USE_SIMD16_SHADERS 1 // requires USE_SIMD16_FRONTEND
#define USE_SIMD16_VS 1      // requires USE_SIMD16_SHADERS

///////////////////////////////////////////////////////////////////////////////
// Architecture validation
///////////////////////////////////////////////////////////////////////////////
#if !defined(KNOB_ARCH)
#define KNOB_ARCH KNOB_ARCH_AVX
#endif

#if (KNOB_ARCH == KNOB_ARCH_AVX)
#define KNOB_ARCH_ISA AVX
#define KNOB_ARCH_STR "AVX"
#elif (KNOB_ARCH == KNOB_ARCH_AVX2)
#define KNOB_ARCH_ISA AVX2
#define KNOB_ARCH_STR "AVX2"
#elif (KNOB_ARCH == KNOB_ARCH_AVX512)
#define KNOB_ARCH_ISA AVX512F
#define KNOB_ARCH_STR "AVX512"
#else
#error "Unknown architecture"
#endif

#define KNOB_SIMD_WIDTH 8
#define KNOB_SIMD_BYTES 32

#define KNOB_SIMD16_WIDTH 16
#define KNOB_SIMD16_BYTES 64

#define MAX_KNOB_ARCH_STR_LEN sizeof("AVX512_PLUS_PADDING")

///////////////////////////////////////////////////////////////////////////////
// Configuration knobs
///////////////////////////////////////////////////////////////////////////////
// Maximum supported number of active vertex buffer streams
#define KNOB_NUM_STREAMS 32

// Maximum supported active viewports and scissors
#define KNOB_NUM_VIEWPORTS_SCISSORS 16

// Guardband range used by the clipper
#define KNOB_GUARDBAND_WIDTH 32768.0f
#define KNOB_GUARDBAND_HEIGHT 32768.0f

// Scratch space requirements per worker. Currently only used for TGSM sizing for some stages
#define KNOB_WORKER_SCRATCH_SPACE_SIZE (32 * 1024)

///////////////////////////////
// Macro tile configuration
///////////////////////////////

// raster tile dimensions
#define KNOB_TILE_X_DIM 8
#define KNOB_TILE_X_DIM_SHIFT 3
#define KNOB_TILE_Y_DIM 8
#define KNOB_TILE_Y_DIM_SHIFT 3

// fixed macrotile pixel dimension for now, eventually will be
// dynamically set based on tile format and pixel size
#define KNOB_MACROTILE_X_DIM 32
#define KNOB_MACROTILE_Y_DIM 32
#define KNOB_MACROTILE_X_DIM_FIXED_SHIFT 13
#define KNOB_MACROTILE_Y_DIM_FIXED_SHIFT 13
#define KNOB_MACROTILE_X_DIM_FIXED (KNOB_MACROTILE_X_DIM << 8)
#define KNOB_MACROTILE_Y_DIM_FIXED (KNOB_MACROTILE_Y_DIM << 8)
#define KNOB_MACROTILE_X_DIM_IN_TILES (KNOB_MACROTILE_X_DIM >> KNOB_TILE_X_DIM_SHIFT)
#define KNOB_MACROTILE_Y_DIM_IN_TILES (KNOB_MACROTILE_Y_DIM >> KNOB_TILE_Y_DIM_SHIFT)

// total # of hot tiles available. This should be enough to
// fully render a 16kx16k 128bpp render target
#define KNOB_NUM_HOT_TILES_X 512
#define KNOB_NUM_HOT_TILES_Y 512
#define KNOB_COLOR_HOT_TILE_FORMAT R32G32B32A32_FLOAT
#define KNOB_DEPTH_HOT_TILE_FORMAT R32_FLOAT
#define KNOB_STENCIL_HOT_TILE_FORMAT R8_UINT

// Max scissor rectangle
#define KNOB_MAX_SCISSOR_X KNOB_NUM_HOT_TILES_X* KNOB_MACROTILE_X_DIM
#define KNOB_MAX_SCISSOR_Y KNOB_NUM_HOT_TILES_Y* KNOB_MACROTILE_Y_DIM

#if KNOB_SIMD_WIDTH == 8 && KNOB_TILE_X_DIM < 4
#error "incompatible width/tile dimensions"
#endif

#if ENABLE_AVX512_SIMD16
#if KNOB_SIMD16_WIDTH == 16 && KNOB_TILE_X_DIM < 8
#error "incompatible width/tile dimensions"
#endif
#endif

#if KNOB_SIMD_WIDTH == 8
#define SIMD_TILE_X_DIM 4
#define SIMD_TILE_Y_DIM 2
#else
#error "Invalid simd width"
#endif

#if ENABLE_AVX512_SIMD16
#if KNOB_SIMD16_WIDTH == 16
#define SIMD16_TILE_X_DIM 8
#define SIMD16_TILE_Y_DIM 2
#else
#error "Invalid simd width"
#endif
#endif

///////////////////////////////////////////////////////////////////////////////
// Optimization knobs
///////////////////////////////////////////////////////////////////////////////
#define KNOB_USE_FAST_SRGB TRUE

// enables cut-aware primitive assembler
#define KNOB_ENABLE_CUT_AWARE_PA TRUE

// enables early rasterization (useful for small triangles)
#if !defined(KNOB_ENABLE_EARLY_RAST)
#define KNOB_ENABLE_EARLY_RAST 1
#endif

#if KNOB_ENABLE_EARLY_RAST
#define ER_SIMD_TILE_X_SHIFT 2
#define ER_SIMD_TILE_Y_SHIFT 2
#endif

///////////////////////////////////////////////////////////////////////////////
// Debug knobs
///////////////////////////////////////////////////////////////////////////////
//#define KNOB_ENABLE_RDTSC

// Set to 1 to use the dynamic KNOB_TOSS_XXXX knobs.
#if !defined(KNOB_ENABLE_TOSS_POINTS)
#define KNOB_ENABLE_TOSS_POINTS 0
#endif
