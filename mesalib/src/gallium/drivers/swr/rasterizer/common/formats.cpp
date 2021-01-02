/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
 * @file formats.cpp
 *
 * @brief auto-generated file
 *
 * DO NOT EDIT
 *
 ******************************************************************************/

#include "formats.h"

// lookup table for unorm8 srgb -> float conversion
const uint32_t srgb8Table[256] = {
    0x00000000, 0x399f22b4, 0x3a1f22b4, 0x3a6eb40f, 0x3a9f22b4, 0x3ac6eb61, 0x3aeeb40f, 0x3b0b3e5e,
    0x3b1f22b4, 0x3b33070b, 0x3b46eb61, 0x3b5b518d, 0x3b70f18d, 0x3b83e1c6, 0x3b8fe616, 0x3b9c87fd,
    0x3ba9c9b5, 0x3bb7ad6f, 0x3bc63549, 0x3bd5635f, 0x3be539c1, 0x3bf5ba70, 0x3c0373b5, 0x3c0c6152,
    0x3c15a703, 0x3c1f45be, 0x3c293e6b, 0x3c3391f7, 0x3c3e4149, 0x3c494d43, 0x3c54b6c7, 0x3c607eb1,
    0x3c6ca5dc, 0x3c792d22, 0x3c830aa8, 0x3c89af9f, 0x3c9085db, 0x3c978dc5, 0x3c9ec7c0, 0x3ca63431,
    0x3cadd37d, 0x3cb5a601, 0x3cbdac20, 0x3cc5e639, 0x3cce54ab, 0x3cd6f7d3, 0x3cdfd00e, 0x3ce8ddb9,
    0x3cf22131, 0x3cfb9ac6, 0x3d02a56c, 0x3d0798df, 0x3d0ca7e7, 0x3d11d2b0, 0x3d171965, 0x3d1c7c31,
    0x3d21fb3c, 0x3d2796b2, 0x3d2d4ebe, 0x3d332384, 0x3d39152e, 0x3d3f23e6, 0x3d454fd4, 0x3d4b991f,
    0x3d51ffef, 0x3d58846a, 0x3d5f26b7, 0x3d65e6fe, 0x3d6cc564, 0x3d73c20f, 0x3d7add25, 0x3d810b66,
    0x3d84b795, 0x3d887330, 0x3d8c3e4a, 0x3d9018f6, 0x3d940345, 0x3d97fd4a, 0x3d9c0716, 0x3da020bb,
    0x3da44a4b, 0x3da883d7, 0x3daccd70, 0x3db12728, 0x3db59110, 0x3dba0b38, 0x3dbe95b5, 0x3dc33092,
    0x3dc7dbe2, 0x3dcc97b6, 0x3dd1641f, 0x3dd6412c, 0x3ddb2eef, 0x3de02d77, 0x3de53cd5, 0x3dea5d19,
    0x3def8e55, 0x3df4d093, 0x3dfa23e8, 0x3dff8861, 0x3e027f07, 0x3e054282, 0x3e080ea5, 0x3e0ae379,
    0x3e0dc107, 0x3e10a755, 0x3e13966c, 0x3e168e53, 0x3e198f11, 0x3e1c98ae, 0x3e1fab32, 0x3e22c6a3,
    0x3e25eb09, 0x3e29186c, 0x3e2c4ed2, 0x3e2f8e45, 0x3e32d6c8, 0x3e362865, 0x3e398322, 0x3e3ce706,
    0x3e405419, 0x3e43ca62, 0x3e4749e8, 0x3e4ad2b1, 0x3e4e64c6, 0x3e52002b, 0x3e55a4e9, 0x3e595307,
    0x3e5d0a8b, 0x3e60cb7c, 0x3e6495e0, 0x3e6869bf, 0x3e6c4720, 0x3e702e08, 0x3e741e7f, 0x3e78188c,
    0x3e7c1c38, 0x3e8014c2, 0x3e82203c, 0x3e84308d, 0x3e8645ba, 0x3e885fc5, 0x3e8a7eb2, 0x3e8ca283,
    0x3e8ecb3d, 0x3e90f8e1, 0x3e932b74, 0x3e9562f8, 0x3e979f71, 0x3e99e0e2, 0x3e9c274e, 0x3e9e72b7,
    0x3ea0c322, 0x3ea31892, 0x3ea57308, 0x3ea7d289, 0x3eaa3718, 0x3eaca0b7, 0x3eaf0f69, 0x3eb18333,
    0x3eb3fc16, 0x3eb67a15, 0x3eb8fd34, 0x3ebb8576, 0x3ebe12e1, 0x3ec0a571, 0x3ec33d2d, 0x3ec5da17,
    0x3ec87c33, 0x3ecb2383, 0x3ecdd00b, 0x3ed081cd, 0x3ed338cc, 0x3ed5f50b, 0x3ed8b68d, 0x3edb7d54,
    0x3ede4965, 0x3ee11ac1, 0x3ee3f16b, 0x3ee6cd67, 0x3ee9aeb6, 0x3eec955d, 0x3eef815d, 0x3ef272ba,
    0x3ef56976, 0x3ef86594, 0x3efb6717, 0x3efe6e02, 0x3f00bd2b, 0x3f02460c, 0x3f03d1a5, 0x3f055ff8,
    0x3f06f106, 0x3f0884cf, 0x3f0a1b57, 0x3f0bb49d, 0x3f0d50a2, 0x3f0eef69, 0x3f1090f2, 0x3f123540,
    0x3f13dc53, 0x3f15862d, 0x3f1732cf, 0x3f18e23b, 0x3f1a9471, 0x3f1c4973, 0x3f1e0143, 0x3f1fbbe1,
    0x3f217950, 0x3f23398f, 0x3f24fca2, 0x3f26c288, 0x3f288b43, 0x3f2a56d5, 0x3f2c253f, 0x3f2df681,
    0x3f2fca9e, 0x3f31a197, 0x3f337b6c, 0x3f355820, 0x3f3737b3, 0x3f391a26, 0x3f3aff7e, 0x3f3ce7b7,
    0x3f3ed2d4, 0x3f40c0d6, 0x3f42b1c0, 0x3f44a592, 0x3f469c4d, 0x3f4895f3, 0x3f4a9284, 0x3f4c9203,
    0x3f4e9470, 0x3f5099cd, 0x3f52a21a, 0x3f54ad59, 0x3f56bb8c, 0x3f58ccb3, 0x3f5ae0cf, 0x3f5cf7e2,
    0x3f5f11ee, 0x3f612ef2, 0x3f634eef, 0x3f6571ec, 0x3f6797e1, 0x3f69c0d8, 0x3f6beccb, 0x3f6e1bc2,
    0x3f704db6, 0x3f7282b1, 0x3f74baae, 0x3f76f5b3, 0x3f7933b9, 0x3f7b74cb, 0x3f7db8e0, 0x3f800000,
};

// order must match SWR_FORMAT
const SWR_FORMAT_INFO gFormatInfo[] = {

    // R32G32B32A32_FLOAT (0x0)
    {
        "R32G32B32A32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 32, 32},             // Bits per component
        128,                          // Bits per element
        16,                           // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32B32A32_SINT (0x1)
    {
        "R32G32B32A32_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 32, 32},             // Bits per component
        128,                          // Bits per element
        16,                           // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32B32A32_UINT (0x2)
    {
        "R32G32B32A32_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 32, 32},             // Bits per component
        128,                          // Bits per element
        16,                           // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x3)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x4)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R64G64_FLOAT (0x5)
    {
        "R64G64_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {64, 64, 0, 0},               // Bits per component
        128,                          // Bits per element
        16,                           // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32B32X32_FLOAT (0x6)
    {
        "R32G32B32X32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 32, 32},             // Bits per component
        128,                          // Bits per element
        16,                           // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32B32A32_SSCALED (0x7)
    {
        "R32G32B32A32_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 32, 32},             // Bits per component
        128,                          // Bits per element
        16,                           // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32B32A32_USCALED (0x8)
    {
        "R32G32B32A32_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 32, 32},             // Bits per component
        128,                          // Bits per element
        16,                           // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x9)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x10)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x11)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x12)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x13)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x14)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x15)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x16)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x17)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x18)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x19)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R32G32B32A32_SFIXED (0x20)
    {
        "R32G32B32A32_SFIXED",
        {SWR_TYPE_SFIXED, SWR_TYPE_SFIXED, SWR_TYPE_SFIXED, SWR_TYPE_SFIXED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 32, 32},             // Bits per component
        128,                          // Bits per element
        16,                           // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x21)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x22)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x23)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x24)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x25)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x26)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x27)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x28)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x29)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x2A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x2B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x2C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x2D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x2E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x2F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x30)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x31)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x32)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x33)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x34)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x35)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x36)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x37)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x38)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x39)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x3A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x3B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x3C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x3D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x3E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x3F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R32G32B32_FLOAT (0x40)
    {
        "R32G32B32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {32, 32, 32, 0},              // Bits per component
        96,                           // Bits per element
        12,                           // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32B32_SINT (0x41)
    {
        "R32G32B32_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {32, 32, 32, 0},              // Bits per component
        96,                           // Bits per element
        12,                           // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32B32_UINT (0x42)
    {
        "R32G32B32_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {32, 32, 32, 0},              // Bits per component
        96,                           // Bits per element
        12,                           // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x43)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x44)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R32G32B32_SSCALED (0x45)
    {
        "R32G32B32_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {32, 32, 32, 0},              // Bits per component
        96,                           // Bits per element
        12,                           // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32B32_USCALED (0x46)
    {
        "R32G32B32_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {32, 32, 32, 0},              // Bits per component
        96,                           // Bits per element
        12,                           // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x47)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x48)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x49)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x4A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x4B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x4C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x4D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x4E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x4F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R32G32B32_SFIXED (0x50)
    {
        "R32G32B32_SFIXED",
        {SWR_TYPE_SFIXED, SWR_TYPE_SFIXED, SWR_TYPE_SFIXED, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {32, 32, 32, 0},              // Bits per component
        96,                           // Bits per element
        12,                           // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x51)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x52)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x53)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x54)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x55)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x56)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x57)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x58)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x59)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x5A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x5B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x5C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x5D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x5E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x5F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x60)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x61)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x62)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x63)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x64)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x65)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x66)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x67)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x68)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x69)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x6A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x6B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x6C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x6D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x6E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x6F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x70)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x71)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x72)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x73)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x74)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x75)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x76)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x77)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x78)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x79)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x7A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x7B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x7C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x7D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x7E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x7F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R16G16B16A16_UNORM (0x80)
    {
        "R16G16B16A16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {0, 1, 2, 3},             // Swizzle
        {16, 16, 16, 16},         // Bits per component
        64,                       // Bits per element
        8,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 65535.0f,
         1.0f / 65535.0f,
         1.0f / 65535.0f,
         1.0f / 65535.0f}, // To float scale factor
        1,                 // bcWidth
        1,                 // bcHeight
    },

    // R16G16B16A16_SNORM (0x81)
    {
        "R16G16B16A16_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {0, 1, 2, 3},             // Swizzle
        {16, 16, 16, 16},         // Bits per component
        64,                       // Bits per element
        8,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 32767.0f,
         1.0f / 32767.0f,
         1.0f / 32767.0f,
         1.0f / 32767.0f}, // To float scale factor
        1,                 // bcWidth
        1,                 // bcHeight
    },

    // R16G16B16A16_SINT (0x82)
    {
        "R16G16B16A16_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {16, 16, 16, 16},             // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16B16A16_UINT (0x83)
    {
        "R16G16B16A16_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {16, 16, 16, 16},             // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16B16A16_FLOAT (0x84)
    {
        "R16G16B16A16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {16, 16, 16, 16},             // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32_FLOAT (0x85)
    {
        "R32G32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32_SINT (0x86)
    {
        "R32G32_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32_UINT (0x87)
    {
        "R32G32_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32_FLOAT_X8X24_TYPELESS (0x88)
    {
        "R32_FLOAT_X8X24_TYPELESS",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNUSED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // X32_TYPELESS_G8X24_UINT (0x89)
    {
        "X32_TYPELESS_G8X24_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UNUSED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // L32A32_FLOAT (0x8A)
    {
        "L32A32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 3, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x8B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x8C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R64_FLOAT (0x8D)
    {
        "R64_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {64, 0, 0, 0},                // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16B16X16_UNORM (0x8E)
    {
        "R16G16B16X16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},     // Defaults for missing components
        {0, 1, 2, 3},              // Swizzle
        {16, 16, 16, 16},          // Bits per component
        64,                        // Bits per element
        8,                         // Bytes per element
        4,                         // Num components
        false,                     // isSRGB
        false,                     // isBC
        false,                     // isSubsampled
        false,                     // isLuminance
        {true, true, true, false}, // Is normalized?
        {1.0f / 65535.0f, 1.0f / 65535.0f, 1.0f / 65535.0f, 1.0f}, // To float scale factor
        1,                                                         // bcWidth
        1,                                                         // bcHeight
    },

    // R16G16B16X16_FLOAT (0x8F)
    {
        "R16G16B16X16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {16, 16, 16, 16},             // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x90)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // L32X32_FLOAT (0x91)
    {
        "L32X32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 3, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // I32X32_FLOAT (0x92)
    {
        "I32X32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 3, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16B16A16_SSCALED (0x93)
    {
        "R16G16B16A16_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {16, 16, 16, 16},             // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16B16A16_USCALED (0x94)
    {
        "R16G16B16A16_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {16, 16, 16, 16},             // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32_SSCALED (0x95)
    {
        "R32G32_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32G32_USCALED (0x96)
    {
        "R32G32_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x97)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x98)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x99)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x9A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x9B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x9C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x9D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x9E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x9F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R32G32_SFIXED (0xA0)
    {
        "R32G32_SFIXED",
        {SWR_TYPE_SFIXED, SWR_TYPE_SFIXED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {32, 32, 0, 0},               // Bits per component
        64,                           // Bits per element
        8,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0xA1)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA2)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA3)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA4)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA5)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA7)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA8)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xA9)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xAA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xAB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xAC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xAD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xAE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xAF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB0)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB1)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB2)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB3)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB4)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB5)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB7)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB8)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xB9)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xBA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xBB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xBC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xBD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xBE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xBF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // B8G8R8A8_UNORM (0xC0)
    {
        "B8G8R8A8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {2, 1, 0, 3},             // Swizzle
        {8, 8, 8, 8},             // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f}, // To float scale factor
        1,                                                            // bcWidth
        1,                                                            // bcHeight
    },

    // B8G8R8A8_UNORM_SRGB (0xC1)
    {
        "B8G8R8A8_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {2, 1, 0, 3},             // Swizzle
        {8, 8, 8, 8},             // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        true,                     // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f}, // To float scale factor
        1,                                                            // bcWidth
        1,                                                            // bcHeight
    },

    // R10G10B10A2_UNORM (0xC2)
    {
        "R10G10B10A2_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {0, 1, 2, 3},             // Swizzle
        {10, 10, 10, 2},          // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f}, // To float scale factor
        1,                                                             // bcWidth
        1,                                                             // bcHeight
    },

    // R10G10B10A2_UNORM_SRGB (0xC3)
    {
        "R10G10B10A2_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {0, 1, 2, 3},             // Swizzle
        {10, 10, 10, 2},          // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        true,                     // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f}, // To float scale factor
        1,                                                             // bcWidth
        1,                                                             // bcHeight
    },

    // R10G10B10A2_UINT (0xC4)
    {
        "R10G10B10A2_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0xC5)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xC6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R8G8B8A8_UNORM (0xC7)
    {
        "R8G8B8A8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {0, 1, 2, 3},             // Swizzle
        {8, 8, 8, 8},             // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f}, // To float scale factor
        1,                                                            // bcWidth
        1,                                                            // bcHeight
    },

    // R8G8B8A8_UNORM_SRGB (0xC8)
    {
        "R8G8B8A8_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {0, 1, 2, 3},             // Swizzle
        {8, 8, 8, 8},             // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        true,                     // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f}, // To float scale factor
        1,                                                            // bcWidth
        1,                                                            // bcHeight
    },

    // R8G8B8A8_SNORM (0xC9)
    {
        "R8G8B8A8_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {0, 1, 2, 3},             // Swizzle
        {8, 8, 8, 8},             // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f}, // To float scale factor
        1,                                                            // bcWidth
        1,                                                            // bcHeight
    },

    // R8G8B8A8_SINT (0xCA)
    {
        "R8G8B8A8_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {8, 8, 8, 8},                 // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8G8B8A8_UINT (0xCB)
    {
        "R8G8B8A8_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {8, 8, 8, 8},                 // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16_UNORM (0xCC)
    {
        "R16G16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                    // Defaults for missing components
        {0, 1, 0, 0},                             // Swizzle
        {16, 16, 0, 0},                           // Bits per component
        32,                                       // Bits per element
        4,                                        // Bytes per element
        2,                                        // Num components
        false,                                    // isSRGB
        false,                                    // isBC
        false,                                    // isSubsampled
        false,                                    // isLuminance
        {true, true, false, false},               // Is normalized?
        {1.0f / 65535.0f, 1.0f / 65535.0f, 0, 0}, // To float scale factor
        1,                                        // bcWidth
        1,                                        // bcHeight
    },

    // R16G16_SNORM (0xCD)
    {
        "R16G16_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                    // Defaults for missing components
        {0, 1, 0, 0},                             // Swizzle
        {16, 16, 0, 0},                           // Bits per component
        32,                                       // Bits per element
        4,                                        // Bytes per element
        2,                                        // Num components
        false,                                    // isSRGB
        false,                                    // isBC
        false,                                    // isSubsampled
        false,                                    // isLuminance
        {true, true, false, false},               // Is normalized?
        {1.0f / 32767.0f, 1.0f / 32767.0f, 0, 0}, // To float scale factor
        1,                                        // bcWidth
        1,                                        // bcHeight
    },

    // R16G16_SINT (0xCE)
    {
        "R16G16_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {16, 16, 0, 0},               // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16_UINT (0xCF)
    {
        "R16G16_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {16, 16, 0, 0},               // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16_FLOAT (0xD0)
    {
        "R16G16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {16, 16, 0, 0},               // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // B10G10R10A2_UNORM (0xD1)
    {
        "B10G10R10A2_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {2, 1, 0, 3},             // Swizzle
        {10, 10, 10, 2},          // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f}, // To float scale factor
        1,                                                             // bcWidth
        1,                                                             // bcHeight
    },

    // B10G10R10A2_UNORM_SRGB (0xD2)
    {
        "B10G10R10A2_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {2, 1, 0, 3},             // Swizzle
        {10, 10, 10, 2},          // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        true,                     // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f}, // To float scale factor
        1,                                                             // bcWidth
        1,                                                             // bcHeight
    },

    // R11G11B10_FLOAT (0xD3)
    {
        "R11G11B10_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {11, 11, 10, 0},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0xD4)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},

    // R10G10B10_FLOAT_A2_UNORM (0xD5)
    {
        "R10G10B10_FLOAT_A2_UNORM",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},           // Defaults for missing components
        {0, 1, 2, 3},                    // Swizzle
        {10, 10, 10, 2},                 // Bits per component
        32,                              // Bits per element
        4,                               // Bytes per element
        4,                               // Num components
        false,                           // isSRGB
        false,                           // isBC
        false,                           // isSubsampled
        false,                           // isLuminance
        {false, false, false, false},    // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f / 3.0f}, // To float scale factor
        1,                               // bcWidth
        1,                               // bcHeight
    },

    // R32_SINT (0xD6)
    {
        "R32_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32_UINT (0xD7)
    {
        "R32_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32_FLOAT (0xD8)
    {
        "R32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R24_UNORM_X8_TYPELESS (0xD9)
    {
        "R24_UNORM_X8_TYPELESS",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},         // Defaults for missing components
        {0, 1, 2, 3},                  // Swizzle
        {24, 0, 0, 0},                 // Bits per component
        32,                            // Bits per element
        4,                             // Bytes per element
        1,                             // Num components
        false,                         // isSRGB
        false,                         // isBC
        false,                         // isSubsampled
        false,                         // isLuminance
        {true, false, false, false},   // Is normalized?
        {1.0f / 16777215.0f, 0, 0, 0}, // To float scale factor
        1,                             // bcWidth
        1,                             // bcHeight
    },

    // X24_TYPELESS_G8_UINT (0xDA)
    {
        "X24_TYPELESS_G8_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {1, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0xDB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xDC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // L32_UNORM (0xDD)
    {
        "L32_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},           // Defaults for missing components
        {0, 0, 0, 0},                    // Swizzle
        {32, 0, 0, 0},                   // Bits per component
        32,                              // Bits per element
        4,                               // Bytes per element
        1,                               // Num components
        false,                           // isSRGB
        false,                           // isBC
        false,                           // isSubsampled
        true,                            // isLuminance
        {true, false, false, false},     // Is normalized?
        {1.0f / 4294967295.0f, 0, 0, 0}, // To float scale factor
        1,                               // bcWidth
        1,                               // bcHeight
    },

    // padding (0xDE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // L16A16_UNORM (0xDF)
    {
        "L16A16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                    // Defaults for missing components
        {0, 3, 0, 0},                             // Swizzle
        {16, 16, 0, 0},                           // Bits per component
        32,                                       // Bits per element
        4,                                        // Bytes per element
        2,                                        // Num components
        false,                                    // isSRGB
        false,                                    // isBC
        false,                                    // isSubsampled
        true,                                     // isLuminance
        {true, true, false, false},               // Is normalized?
        {1.0f / 65535.0f, 1.0f / 65535.0f, 0, 0}, // To float scale factor
        1,                                        // bcWidth
        1,                                        // bcHeight
    },

    // I24X8_UNORM (0xE0)
    {
        "I24X8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                     // Defaults for missing components
        {0, 3, 0, 0},                              // Swizzle
        {24, 8, 0, 0},                             // Bits per component
        32,                                        // Bits per element
        4,                                         // Bytes per element
        2,                                         // Num components
        false,                                     // isSRGB
        false,                                     // isBC
        false,                                     // isSubsampled
        true,                                      // isLuminance
        {true, true, false, false},                // Is normalized?
        {1.0f / 16777215.0f, 1.0f / 255.0f, 0, 0}, // To float scale factor
        1,                                         // bcWidth
        1,                                         // bcHeight
    },

    // L24X8_UNORM (0xE1)
    {
        "L24X8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                     // Defaults for missing components
        {0, 3, 0, 0},                              // Swizzle
        {24, 8, 0, 0},                             // Bits per component
        32,                                        // Bits per element
        4,                                         // Bytes per element
        2,                                         // Num components
        false,                                     // isSRGB
        false,                                     // isBC
        false,                                     // isSubsampled
        true,                                      // isLuminance
        {true, true, false, false},                // Is normalized?
        {1.0f / 16777215.0f, 1.0f / 255.0f, 0, 0}, // To float scale factor
        1,                                         // bcWidth
        1,                                         // bcHeight
    },

    // padding (0xE2)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // I32_FLOAT (0xE3)
    {
        "I32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // L32_FLOAT (0xE4)
    {
        "L32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // A32_FLOAT (0xE5)
    {
        "A32_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {3, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0xE6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xE7)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xE8)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // B8G8R8X8_UNORM (0xE9)
    {
        "B8G8R8X8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},                               // Defaults for missing components
        {2, 1, 0, 3},                                        // Swizzle
        {8, 8, 8, 8},                                        // Bits per component
        32,                                                  // Bits per element
        4,                                                   // Bytes per element
        4,                                                   // Num components
        false,                                               // isSRGB
        false,                                               // isBC
        false,                                               // isSubsampled
        false,                                               // isLuminance
        {true, true, true, false},                           // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f}, // To float scale factor
        1,                                                   // bcWidth
        1,                                                   // bcHeight
    },

    // B8G8R8X8_UNORM_SRGB (0xEA)
    {
        "B8G8R8X8_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},                               // Defaults for missing components
        {2, 1, 0, 3},                                        // Swizzle
        {8, 8, 8, 8},                                        // Bits per component
        32,                                                  // Bits per element
        4,                                                   // Bytes per element
        4,                                                   // Num components
        true,                                                // isSRGB
        false,                                               // isBC
        false,                                               // isSubsampled
        false,                                               // isLuminance
        {true, true, true, false},                           // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f}, // To float scale factor
        1,                                                   // bcWidth
        1,                                                   // bcHeight
    },

    // R8G8B8X8_UNORM (0xEB)
    {
        "R8G8B8X8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},                               // Defaults for missing components
        {0, 1, 2, 3},                                        // Swizzle
        {8, 8, 8, 8},                                        // Bits per component
        32,                                                  // Bits per element
        4,                                                   // Bytes per element
        4,                                                   // Num components
        false,                                               // isSRGB
        false,                                               // isBC
        false,                                               // isSubsampled
        false,                                               // isLuminance
        {true, true, true, false},                           // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f}, // To float scale factor
        1,                                                   // bcWidth
        1,                                                   // bcHeight
    },

    // R8G8B8X8_UNORM_SRGB (0xEC)
    {
        "R8G8B8X8_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},                               // Defaults for missing components
        {0, 1, 2, 3},                                        // Swizzle
        {8, 8, 8, 8},                                        // Bits per component
        32,                                                  // Bits per element
        4,                                                   // Bytes per element
        4,                                                   // Num components
        true,                                                // isSRGB
        false,                                               // isBC
        false,                                               // isSubsampled
        false,                                               // isLuminance
        {true, true, true, false},                           // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f}, // To float scale factor
        1,                                                   // bcWidth
        1,                                                   // bcHeight
    },

    // R9G9B9E5_SHAREDEXP (0xED)
    {
        "R9G9B9E5_SHAREDEXP",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {9, 9, 9, 5},                 // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // B10G10R10X2_UNORM (0xEE)
    {
        "B10G10R10X2_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},                                  // Defaults for missing components
        {2, 1, 0, 3},                                           // Swizzle
        {10, 10, 10, 2},                                        // Bits per component
        32,                                                     // Bits per element
        4,                                                      // Bytes per element
        4,                                                      // Num components
        false,                                                  // isSRGB
        false,                                                  // isBC
        false,                                                  // isSubsampled
        false,                                                  // isLuminance
        {true, true, true, false},                              // Is normalized?
        {1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f}, // To float scale factor
        1,                                                      // bcWidth
        1,                                                      // bcHeight
    },

    // padding (0xEF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // L16A16_FLOAT (0xF0)
    {
        "L16A16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 3, 0, 0},                 // Swizzle
        {16, 16, 0, 0},               // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0xF1)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xF2)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R10G10B10X2_USCALED (0xF3)
    {
        "R10G10B10X2_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8G8B8A8_SSCALED (0xF4)
    {
        "R8G8B8A8_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {8, 8, 8, 8},                 // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8G8B8A8_USCALED (0xF5)
    {
        "R8G8B8A8_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {8, 8, 8, 8},                 // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16_SSCALED (0xF6)
    {
        "R16G16_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {16, 16, 0, 0},               // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16_USCALED (0xF7)
    {
        "R16G16_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {16, 16, 0, 0},               // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32_SSCALED (0xF8)
    {
        "R32_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32_USCALED (0xF9)
    {
        "R32_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0xFA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xFB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xFC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xFD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xFE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0xFF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // B5G6R5_UNORM (0x100)
    {
        "B5G6R5_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                         // Defaults for missing components
        {2, 1, 0, 0},                                  // Swizzle
        {5, 6, 5, 0},                                  // Bits per component
        16,                                            // Bits per element
        2,                                             // Bytes per element
        3,                                             // Num components
        false,                                         // isSRGB
        false,                                         // isBC
        false,                                         // isSubsampled
        false,                                         // isLuminance
        {true, true, true, false},                     // Is normalized?
        {1.0f / 31.0f, 1.0f / 63.0f, 1.0f / 31.0f, 0}, // To float scale factor
        1,                                             // bcWidth
        1,                                             // bcHeight
    },

    // B5G6R5_UNORM_SRGB (0x101)
    {
        "B5G6R5_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                         // Defaults for missing components
        {2, 1, 0, 0},                                  // Swizzle
        {5, 6, 5, 0},                                  // Bits per component
        16,                                            // Bits per element
        2,                                             // Bytes per element
        3,                                             // Num components
        true,                                          // isSRGB
        false,                                         // isBC
        false,                                         // isSubsampled
        false,                                         // isLuminance
        {true, true, true, false},                     // Is normalized?
        {1.0f / 31.0f, 1.0f / 63.0f, 1.0f / 31.0f, 0}, // To float scale factor
        1,                                             // bcWidth
        1,                                             // bcHeight
    },

    // B5G5R5A1_UNORM (0x102)
    {
        "B5G5R5A1_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},                                   // Defaults for missing components
        {2, 1, 0, 3},                                            // Swizzle
        {5, 5, 5, 1},                                            // Bits per component
        16,                                                      // Bits per element
        2,                                                       // Bytes per element
        4,                                                       // Num components
        false,                                                   // isSRGB
        false,                                                   // isBC
        false,                                                   // isSubsampled
        false,                                                   // isLuminance
        {true, true, true, true},                                // Is normalized?
        {1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 1.0f}, // To float scale factor
        1,                                                       // bcWidth
        1,                                                       // bcHeight
    },

    // B5G5R5A1_UNORM_SRGB (0x103)
    {
        "B5G5R5A1_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},                                   // Defaults for missing components
        {2, 1, 0, 3},                                            // Swizzle
        {5, 5, 5, 1},                                            // Bits per component
        16,                                                      // Bits per element
        2,                                                       // Bytes per element
        4,                                                       // Num components
        true,                                                    // isSRGB
        false,                                                   // isBC
        false,                                                   // isSubsampled
        false,                                                   // isLuminance
        {true, true, true, true},                                // Is normalized?
        {1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 1.0f}, // To float scale factor
        1,                                                       // bcWidth
        1,                                                       // bcHeight
    },

    // B4G4R4A4_UNORM (0x104)
    {
        "B4G4R4A4_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},                                    // Defaults for missing components
        {2, 1, 0, 3},                                             // Swizzle
        {4, 4, 4, 4},                                             // Bits per component
        16,                                                       // Bits per element
        2,                                                        // Bytes per element
        4,                                                        // Num components
        false,                                                    // isSRGB
        false,                                                    // isBC
        false,                                                    // isSubsampled
        false,                                                    // isLuminance
        {true, true, true, true},                                 // Is normalized?
        {1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f}, // To float scale factor
        1,                                                        // bcWidth
        1,                                                        // bcHeight
    },

    // B4G4R4A4_UNORM_SRGB (0x105)
    {
        "B4G4R4A4_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},                                    // Defaults for missing components
        {2, 1, 0, 3},                                             // Swizzle
        {4, 4, 4, 4},                                             // Bits per component
        16,                                                       // Bits per element
        2,                                                        // Bytes per element
        4,                                                        // Num components
        true,                                                     // isSRGB
        false,                                                    // isBC
        false,                                                    // isSubsampled
        false,                                                    // isLuminance
        {true, true, true, true},                                 // Is normalized?
        {1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f}, // To float scale factor
        1,                                                        // bcWidth
        1,                                                        // bcHeight
    },

    // R8G8_UNORM (0x106)
    {
        "R8G8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                // Defaults for missing components
        {0, 1, 0, 0},                         // Swizzle
        {8, 8, 0, 0},                         // Bits per component
        16,                                   // Bits per element
        2,                                    // Bytes per element
        2,                                    // Num components
        false,                                // isSRGB
        false,                                // isBC
        false,                                // isSubsampled
        false,                                // isLuminance
        {true, true, false, false},           // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 0, 0}, // To float scale factor
        1,                                    // bcWidth
        1,                                    // bcHeight
    },

    // R8G8_SNORM (0x107)
    {
        "R8G8_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                // Defaults for missing components
        {0, 1, 0, 0},                         // Swizzle
        {8, 8, 0, 0},                         // Bits per component
        16,                                   // Bits per element
        2,                                    // Bytes per element
        2,                                    // Num components
        false,                                // isSRGB
        false,                                // isBC
        false,                                // isSubsampled
        false,                                // isLuminance
        {true, true, false, false},           // Is normalized?
        {1.0f / 127.0f, 1.0f / 127.0f, 0, 0}, // To float scale factor
        1,                                    // bcWidth
        1,                                    // bcHeight
    },

    // R8G8_SINT (0x108)
    {
        "R8G8_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {8, 8, 0, 0},                 // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8G8_UINT (0x109)
    {
        "R8G8_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {8, 8, 0, 0},                 // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16_UNORM (0x10A)
    {
        "R16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {16, 0, 0, 0},               // Bits per component
        16,                          // Bits per element
        2,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 65535.0f, 0, 0, 0},  // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // R16_SNORM (0x10B)
    {
        "R16_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {16, 0, 0, 0},               // Bits per component
        16,                          // Bits per element
        2,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 32767.0f, 0, 0, 0},  // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // R16_SINT (0x10C)
    {
        "R16_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {16, 0, 0, 0},                // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16_UINT (0x10D)
    {
        "R16_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {16, 0, 0, 0},                // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16_FLOAT (0x10E)
    {
        "R16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {16, 0, 0, 0},                // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x10F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x110)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // I16_UNORM (0x111)
    {
        "I16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {16, 0, 0, 0},               // Bits per component
        16,                          // Bits per element
        2,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        true,                        // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 65535.0f, 0, 0, 0},  // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // L16_UNORM (0x112)
    {
        "L16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {16, 0, 0, 0},               // Bits per component
        16,                          // Bits per element
        2,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        true,                        // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 65535.0f, 0, 0, 0},  // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // A16_UNORM (0x113)
    {
        "A16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {3, 0, 0, 0},                // Swizzle
        {16, 0, 0, 0},               // Bits per component
        16,                          // Bits per element
        2,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 65535.0f, 0, 0, 0},  // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // L8A8_UNORM (0x114)
    {
        "L8A8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                // Defaults for missing components
        {0, 3, 0, 0},                         // Swizzle
        {8, 8, 0, 0},                         // Bits per component
        16,                                   // Bits per element
        2,                                    // Bytes per element
        2,                                    // Num components
        false,                                // isSRGB
        false,                                // isBC
        false,                                // isSubsampled
        true,                                 // isLuminance
        {true, true, false, false},           // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 0, 0}, // To float scale factor
        1,                                    // bcWidth
        1,                                    // bcHeight
    },

    // I16_FLOAT (0x115)
    {
        "I16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {16, 0, 0, 0},                // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // L16_FLOAT (0x116)
    {
        "L16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {16, 0, 0, 0},                // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // A16_FLOAT (0x117)
    {
        "A16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {3, 0, 0, 0},                 // Swizzle
        {16, 0, 0, 0},                // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // L8A8_UNORM_SRGB (0x118)
    {
        "L8A8_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                // Defaults for missing components
        {0, 3, 0, 0},                         // Swizzle
        {8, 8, 0, 0},                         // Bits per component
        16,                                   // Bits per element
        2,                                    // Bytes per element
        2,                                    // Num components
        true,                                 // isSRGB
        false,                                // isBC
        false,                                // isSubsampled
        true,                                 // isLuminance
        {true, true, false, false},           // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 0, 0}, // To float scale factor
        1,                                    // bcWidth
        1,                                    // bcHeight
    },

    // padding (0x119)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // B5G5R5X1_UNORM (0x11A)
    {
        "B5G5R5X1_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},                            // Defaults for missing components
        {2, 1, 0, 3},                                     // Swizzle
        {5, 5, 5, 1},                                     // Bits per component
        16,                                               // Bits per element
        2,                                                // Bytes per element
        4,                                                // Num components
        false,                                            // isSRGB
        false,                                            // isBC
        false,                                            // isSubsampled
        false,                                            // isLuminance
        {true, true, true, false},                        // Is normalized?
        {1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f}, // To float scale factor
        1,                                                // bcWidth
        1,                                                // bcHeight
    },

    // B5G5R5X1_UNORM_SRGB (0x11B)
    {
        "B5G5R5X1_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED},
        {0, 0, 0, 0x3f800000},                            // Defaults for missing components
        {2, 1, 0, 3},                                     // Swizzle
        {5, 5, 5, 1},                                     // Bits per component
        16,                                               // Bits per element
        2,                                                // Bytes per element
        4,                                                // Num components
        true,                                             // isSRGB
        false,                                            // isBC
        false,                                            // isSubsampled
        false,                                            // isLuminance
        {true, true, true, false},                        // Is normalized?
        {1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f}, // To float scale factor
        1,                                                // bcWidth
        1,                                                // bcHeight
    },

    // R8G8_SSCALED (0x11C)
    {
        "R8G8_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {8, 8, 0, 0},                 // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8G8_USCALED (0x11D)
    {
        "R8G8_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 0, 0},                 // Swizzle
        {8, 8, 0, 0},                 // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16_SSCALED (0x11E)
    {
        "R16_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {16, 0, 0, 0},                // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16_USCALED (0x11F)
    {
        "R16_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {16, 0, 0, 0},                // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x120)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x121)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x122)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x123)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // A1B5G5R5_UNORM (0x124)
    {
        "A1B5G5R5_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},                                   // Defaults for missing components
        {3, 2, 1, 0},                                            // Swizzle
        {1, 5, 5, 5},                                            // Bits per component
        16,                                                      // Bits per element
        2,                                                       // Bytes per element
        4,                                                       // Num components
        false,                                                   // isSRGB
        false,                                                   // isBC
        false,                                                   // isSubsampled
        false,                                                   // isLuminance
        {true, true, true, true},                                // Is normalized?
        {1.0f / 1.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f}, // To float scale factor
        1,                                                       // bcWidth
        1,                                                       // bcHeight
    },

    // A4B4G4R4_UNORM (0x125)
    {
        "A4B4G4R4_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM},
        {0, 0, 0, 0x3f800000},                                    // Defaults for missing components
        {3, 2, 1, 0},                                             // Swizzle
        {4, 4, 4, 4},                                             // Bits per component
        16,                                                       // Bits per element
        2,                                                        // Bytes per element
        4,                                                        // Num components
        false,                                                    // isSRGB
        false,                                                    // isBC
        false,                                                    // isSubsampled
        false,                                                    // isLuminance
        {true, true, true, true},                                 // Is normalized?
        {1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f}, // To float scale factor
        1,                                                        // bcWidth
        1,                                                        // bcHeight
    },

    // L8A8_UINT (0x126)
    {
        "L8A8_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 3, 0, 0},                 // Swizzle
        {8, 8, 0, 0},                 // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // L8A8_SINT (0x127)
    {
        "L8A8_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 3, 0, 0},                 // Swizzle
        {8, 8, 0, 0},                 // Bits per component
        16,                           // Bits per element
        2,                            // Bytes per element
        2,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 0, 0},           // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x128)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x129)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x12A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x12B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x12C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x12D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x12E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x12F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x130)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x131)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x132)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x133)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x134)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x135)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x136)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x137)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x138)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x139)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x13A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x13B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x13C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x13D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x13E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x13F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R8_UNORM (0x140)
    {
        "R8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {8, 0, 0, 0},                // Bits per component
        8,                           // Bits per element
        1,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // R8_SNORM (0x141)
    {
        "R8_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {8, 0, 0, 0},                // Bits per component
        8,                           // Bits per element
        1,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 127.0f, 0, 0, 0},    // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // R8_SINT (0x142)
    {
        "R8_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8_UINT (0x143)
    {
        "R8_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // A8_UNORM (0x144)
    {
        "A8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {3, 0, 0, 0},                // Swizzle
        {8, 0, 0, 0},                // Bits per component
        8,                           // Bits per element
        1,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // I8_UNORM (0x145)
    {
        "I8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {8, 0, 0, 0},                // Bits per component
        8,                           // Bits per element
        1,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        true,                        // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // L8_UNORM (0x146)
    {
        "L8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {8, 0, 0, 0},                // Bits per component
        8,                           // Bits per element
        1,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        true,                        // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // padding (0x147)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x148)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R8_SSCALED (0x149)
    {
        "R8_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8_USCALED (0x14A)
    {
        "R8_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x14B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // L8_UNORM_SRGB (0x14C)
    {
        "L8_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 0, 0, 0},                // Swizzle
        {8, 0, 0, 0},                // Bits per component
        8,                           // Bits per element
        1,                           // Bytes per element
        1,                           // Num components
        true,                        // isSRGB
        false,                       // isBC
        false,                       // isSubsampled
        true,                        // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        1,                           // bcWidth
        1,                           // bcHeight
    },

    // padding (0x14D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x14E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x14F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x150)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x151)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // L8_UINT (0x152)
    {
        "L8_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // L8_SINT (0x153)
    {
        "L8_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // I8_UINT (0x154)
    {
        "I8_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // I8_SINT (0x155)
    {
        "I8_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        true,                         // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x156)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x157)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x158)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x159)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x15A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x15B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x15C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x15D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x15E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x15F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x160)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x161)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x162)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x163)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x164)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x165)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x166)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x167)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x168)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x169)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x16A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x16B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x16C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x16D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x16E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x16F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x170)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x171)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x172)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x173)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x174)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x175)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x176)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x177)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x178)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x179)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x17A)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x17B)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x17C)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x17D)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x17E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x17F)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // DXT1_RGB_SRGB (0x180)
    {
        "DXT1_RGB_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        64,                          // Bits per element
        8,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // padding (0x181)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x182)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // YCRCB_SWAPUVY (0x183)
    {
        "YCRCB_SWAPUVY",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {8, 8, 8, 8},                 // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        true,                         // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        2,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x184)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x185)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // BC1_UNORM (0x186)
    {
        "BC1_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        64,                          // Bits per element
        8,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC2_UNORM (0x187)
    {
        "BC2_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC3_UNORM (0x188)
    {
        "BC3_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC4_UNORM (0x189)
    {
        "BC4_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        64,                          // Bits per element
        8,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC5_UNORM (0x18A)
    {
        "BC5_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC1_UNORM_SRGB (0x18B)
    {
        "BC1_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        64,                          // Bits per element
        8,                           // Bytes per element
        1,                           // Num components
        true,                        // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC2_UNORM_SRGB (0x18C)
    {
        "BC2_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        true,                        // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC3_UNORM_SRGB (0x18D)
    {
        "BC3_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        true,                        // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // padding (0x18E)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // YCRCB_SWAPUV (0x18F)
    {
        "YCRCB_SWAPUV",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {8, 8, 8, 8},                 // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        true,                         // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        2,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x190)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // DXT1_RGB (0x191)
    {
        "DXT1_RGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        64,                          // Bits per element
        8,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // padding (0x192)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R8G8B8_UNORM (0x193)
    {
        "R8G8B8_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                            // Defaults for missing components
        {0, 1, 2, 0},                                     // Swizzle
        {8, 8, 8, 0},                                     // Bits per component
        24,                                               // Bits per element
        3,                                                // Bytes per element
        3,                                                // Num components
        false,                                            // isSRGB
        false,                                            // isBC
        false,                                            // isSubsampled
        false,                                            // isLuminance
        {true, true, true, false},                        // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 0}, // To float scale factor
        1,                                                // bcWidth
        1,                                                // bcHeight
    },

    // R8G8B8_SNORM (0x194)
    {
        "R8G8B8_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                            // Defaults for missing components
        {0, 1, 2, 0},                                     // Swizzle
        {8, 8, 8, 0},                                     // Bits per component
        24,                                               // Bits per element
        3,                                                // Bytes per element
        3,                                                // Num components
        false,                                            // isSRGB
        false,                                            // isBC
        false,                                            // isSubsampled
        false,                                            // isLuminance
        {true, true, true, false},                        // Is normalized?
        {1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f, 0}, // To float scale factor
        1,                                                // bcWidth
        1,                                                // bcHeight
    },

    // R8G8B8_SSCALED (0x195)
    {
        "R8G8B8_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {8, 8, 8, 0},                 // Bits per component
        24,                           // Bits per element
        3,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8G8B8_USCALED (0x196)
    {
        "R8G8B8_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {8, 8, 8, 0},                 // Bits per component
        24,                           // Bits per element
        3,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R64G64B64A64_FLOAT (0x197)
    {
        "R64G64B64A64_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {64, 64, 64, 64},             // Bits per component
        256,                          // Bits per element
        32,                           // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R64G64B64_FLOAT (0x198)
    {
        "R64G64B64_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {64, 64, 64, 0},              // Bits per component
        192,                          // Bits per element
        24,                           // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // BC4_SNORM (0x199)
    {
        "BC4_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        64,                          // Bits per element
        8,                           // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 127.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC5_SNORM (0x19A)
    {
        "BC5_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 127.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // R16G16B16_FLOAT (0x19B)
    {
        "R16G16B16_FLOAT",
        {SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {16, 16, 16, 0},              // Bits per component
        48,                           // Bits per element
        6,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16B16_UNORM (0x19C)
    {
        "R16G16B16_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                                  // Defaults for missing components
        {0, 1, 2, 0},                                           // Swizzle
        {16, 16, 16, 0},                                        // Bits per component
        48,                                                     // Bits per element
        6,                                                      // Bytes per element
        3,                                                      // Num components
        false,                                                  // isSRGB
        false,                                                  // isBC
        false,                                                  // isSubsampled
        false,                                                  // isLuminance
        {true, true, true, false},                              // Is normalized?
        {1.0f / 65535.0f, 1.0f / 65535.0f, 1.0f / 65535.0f, 0}, // To float scale factor
        1,                                                      // bcWidth
        1,                                                      // bcHeight
    },

    // R16G16B16_SNORM (0x19D)
    {
        "R16G16B16_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                                  // Defaults for missing components
        {0, 1, 2, 0},                                           // Swizzle
        {16, 16, 16, 0},                                        // Bits per component
        48,                                                     // Bits per element
        6,                                                      // Bytes per element
        3,                                                      // Num components
        false,                                                  // isSRGB
        false,                                                  // isBC
        false,                                                  // isSubsampled
        false,                                                  // isLuminance
        {true, true, true, false},                              // Is normalized?
        {1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / 32767.0f, 0}, // To float scale factor
        1,                                                      // bcWidth
        1,                                                      // bcHeight
    },

    // R16G16B16_SSCALED (0x19E)
    {
        "R16G16B16_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {16, 16, 16, 0},              // Bits per component
        48,                           // Bits per element
        6,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16B16_USCALED (0x19F)
    {
        "R16G16B16_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {16, 16, 16, 0},              // Bits per component
        48,                           // Bits per element
        6,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x1A0)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // BC6H_SF16 (0x1A1)
    {
        "BC6H_SF16",
        {SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 127.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC7_UNORM (0x1A2)
    {
        "BC7_UNORM",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC7_UNORM_SRGB (0x1A3)
    {
        "BC7_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        true,                        // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // BC6H_UF16 (0x1A4)
    {
        "BC6H_UF16",
        {SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},       // Defaults for missing components
        {0, 1, 2, 3},                // Swizzle
        {8, 8, 8, 8},                // Bits per component
        128,                         // Bits per element
        16,                          // Bytes per element
        1,                           // Num components
        false,                       // isSRGB
        true,                        // isBC
        false,                       // isSubsampled
        false,                       // isLuminance
        {true, false, false, false}, // Is normalized?
        {1.0f / 255.0f, 0, 0, 0},    // To float scale factor
        4,                           // bcWidth
        4,                           // bcHeight
    },

    // padding (0x1A5)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1A6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1A7)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R8G8B8_UNORM_SRGB (0x1A8)
    {
        "R8G8B8_UNORM_SRGB",
        {SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},                            // Defaults for missing components
        {0, 1, 2, 0},                                     // Swizzle
        {8, 8, 8, 0},                                     // Bits per component
        24,                                               // Bits per element
        3,                                                // Bytes per element
        3,                                                // Num components
        true,                                             // isSRGB
        false,                                            // isBC
        false,                                            // isSubsampled
        false,                                            // isLuminance
        {true, true, true, false},                        // Is normalized?
        {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 0}, // To float scale factor
        1,                                                // bcWidth
        1,                                                // bcHeight
    },

    // padding (0x1A9)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1AA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1AB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1AC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1AD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1AE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1AF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R16G16B16_UINT (0x1B0)
    {
        "R16G16B16_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {16, 16, 16, 0},              // Bits per component
        48,                           // Bits per element
        6,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R16G16B16_SINT (0x1B1)
    {
        "R16G16B16_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {16, 16, 16, 0},              // Bits per component
        48,                           // Bits per element
        6,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R32_SFIXED (0x1B2)
    {
        "R32_SFIXED",
        {SWR_TYPE_SFIXED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 0, 0, 0},                 // Swizzle
        {32, 0, 0, 0},                // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R10G10B10A2_SNORM (0x1B3)
    {
        "R10G10B10A2_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {0, 1, 2, 3},             // Swizzle
        {10, 10, 10, 2},          // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 1.0f}, // To float scale factor
        1,                                                          // bcWidth
        1,                                                          // bcHeight
    },

    // R10G10B10A2_USCALED (0x1B4)
    {
        "R10G10B10A2_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R10G10B10A2_SSCALED (0x1B5)
    {
        "R10G10B10A2_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R10G10B10A2_SINT (0x1B6)
    {
        "R10G10B10A2_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // B10G10R10A2_SNORM (0x1B7)
    {
        "B10G10R10A2_SNORM",
        {SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM},
        {0, 0, 0, 0x3f800000},    // Defaults for missing components
        {2, 1, 0, 3},             // Swizzle
        {10, 10, 10, 2},          // Bits per component
        32,                       // Bits per element
        4,                        // Bytes per element
        4,                        // Num components
        false,                    // isSRGB
        false,                    // isBC
        false,                    // isSubsampled
        false,                    // isLuminance
        {true, true, true, true}, // Is normalized?
        {1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 1.0f}, // To float scale factor
        1,                                                          // bcWidth
        1,                                                          // bcHeight
    },

    // B10G10R10A2_USCALED (0x1B8)
    {
        "B10G10R10A2_USCALED",
        {SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {2, 1, 0, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // B10G10R10A2_SSCALED (0x1B9)
    {
        "B10G10R10A2_SSCALED",
        {SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED},
        {0, 0, 0, 0x3f800000},        // Defaults for missing components
        {2, 1, 0, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // B10G10R10A2_UINT (0x1BA)
    {
        "B10G10R10A2_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {2, 1, 0, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // B10G10R10A2_SINT (0x1BB)
    {
        "B10G10R10A2_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {2, 1, 0, 3},                 // Swizzle
        {10, 10, 10, 2},              // Bits per component
        32,                           // Bits per element
        4,                            // Bytes per element
        4,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 1.0f},     // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x1BC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1BD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1BE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1BF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C0)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C1)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C2)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C3)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C4)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C5)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1C7)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // R8G8B8_UINT (0x1C8)
    {
        "R8G8B8_UINT",
        {SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {8, 8, 8, 0},                 // Bits per component
        24,                           // Bits per element
        3,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // R8G8B8_SINT (0x1C9)
    {
        "R8G8B8_SINT",
        {SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 0},                 // Swizzle
        {8, 8, 8, 0},                 // Bits per component
        24,                           // Bits per element
        3,                            // Bytes per element
        3,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 1.0f, 1.0f, 0},        // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },

    // padding (0x1CA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1CB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1CC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1CD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1CE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1CF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D0)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D1)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D2)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D3)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D4)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D5)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D7)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D8)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1D9)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1DA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1DB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1DC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1DD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1DE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1DF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E0)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E1)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E2)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E3)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E4)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E5)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E7)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E8)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1E9)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1EA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1EB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1EC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1ED)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1EE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1EF)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F0)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F1)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F2)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F3)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F4)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F5)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F6)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F7)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F8)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1F9)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1FA)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1FB)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1FC)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1FD)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // padding (0x1FE)
    {nullptr,
     {SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0},
     0,
     0,
     0,
     false,
     false,
     false,
     false,
     {false, false, false, false},
     {0.0f, 0.0f, 0.0f, 0.0f},
     1,
     1},
    // RAW (0x1FF)
    {
        "RAW",
        {SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN},
        {0, 0, 0, 0x1},               // Defaults for missing components
        {0, 1, 2, 3},                 // Swizzle
        {8, 0, 0, 0},                 // Bits per component
        8,                            // Bits per element
        1,                            // Bytes per element
        1,                            // Num components
        false,                        // isSRGB
        false,                        // isBC
        false,                        // isSubsampled
        false,                        // isLuminance
        {false, false, false, false}, // Is normalized?
        {1.0f, 0, 0, 0},              // To float scale factor
        1,                            // bcWidth
        1,                            // bcHeight
    },
};
