/****************************************************************************
* Copyright (C) 2014-2019 Intel Corporation.   All Rights Reserved.
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
* @file SurfaceState.h
* 
* @brief Common definitions for surface state
* 
******************************************************************************/
#pragma once

#include "core/state.h"

//////////////////////////////////////////////////////////////////////////
/// SWR_SURFACE_STATE
//////////////////////////////////////////////////////////////////////////
struct SWR_SURFACE_STATE
{
    gfxptr_t         xpBaseAddress;
    SWR_SURFACE_TYPE type;   // @llvm_enum
    SWR_FORMAT       format; // @llvm_enum
    uint32_t         width;
    uint32_t         height;
    uint32_t         depth;
    uint32_t         numSamples;
    uint32_t         samplePattern;
    uint32_t         pitch;
    uint32_t         qpitch;
    uint32_t minLod; // for sampled surfaces, the most detailed LOD that can be accessed by sampler
    uint32_t maxLod; // for sampled surfaces, the max LOD that can be accessed
    float    resourceMinLod; // for sampled surfaces, the most detailed fractional mip that can be
    // accessed by sampler
    uint32_t lod;            // for render targets, the lod being rendered to
    uint32_t arrayIndex; // for render targets, the array index being rendered to for arrayed surfaces
    SWR_TILE_MODE tileMode; // @llvm_enum
    uint32_t      halign;
    uint32_t      valign;
    uint32_t      xOffset;
    uint32_t      yOffset;

    uint32_t lodOffsets[2][15]; // lod offsets for sampled surfaces

    gfxptr_t     xpAuxBaseAddress; // Used for compression, append/consume counter, etc.
    SWR_AUX_MODE auxMode;          // @llvm_enum


    bool bInterleavedSamples; // are MSAA samples stored interleaved or planar
};