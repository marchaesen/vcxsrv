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
 * @file streamout_jit.h
 *
 * @brief Definition of the streamout jitter
 *
 * Notes:
 *
 ******************************************************************************/
#pragma once

#include "common/formats.h"
#include "core/state.h"

//////////////////////////////////////////////////////////////////////////
/// STREAMOUT_DECL - Stream decl
//////////////////////////////////////////////////////////////////////////
struct STREAMOUT_DECL
{
    // Buffer that stream maps to.
    DWORD bufferIndex;

    // attribute to stream
    uint32_t attribSlot;

    // attribute component mask
    uint32_t componentMask;

    // indicates this decl is a hole
    bool hole;
};

//////////////////////////////////////////////////////////////////////////
/// STREAMOUT_STREAM - Stream decls
//////////////////////////////////////////////////////////////////////////
struct STREAMOUT_STREAM
{
    // number of decls for this stream
    uint32_t numDecls;

    // array of numDecls decls
    STREAMOUT_DECL decl[128];
};

//////////////////////////////////////////////////////////////////////////
/// State required for streamout jit
//////////////////////////////////////////////////////////////////////////
struct STREAMOUT_COMPILE_STATE
{
    // number of verts per primitive
    uint32_t numVertsPerPrim;
    uint32_t
        offsetAttribs; ///< attrib offset to subtract from all STREAMOUT_DECL::attribSlot values.

    uint64_t streamMask;

    // stream decls
    STREAMOUT_STREAM stream;

    bool operator==(const STREAMOUT_COMPILE_STATE& other) const
    {
        if (numVertsPerPrim != other.numVertsPerPrim)
            return false;
        if (stream.numDecls != other.stream.numDecls)
            return false;

        for (uint32_t i = 0; i < stream.numDecls; ++i)
        {
            if (stream.decl[i].bufferIndex != other.stream.decl[i].bufferIndex)
                return false;
            if (stream.decl[i].attribSlot != other.stream.decl[i].attribSlot)
                return false;
            if (stream.decl[i].componentMask != other.stream.decl[i].componentMask)
                return false;
            if (stream.decl[i].hole != other.stream.decl[i].hole)
                return false;
        }

        return true;
    }
};
