/*
************************************************************************************************************************
*
*  Copyright (C) 2022-2024 Advanced Micro Devices, Inc. All rights reserved.
*  SPDX-License-Identifier: MIT
*
***********************************************************************************************************************/

/**
************************************************************************************************************************
* @file  gfx12SwizzlePattern.h
* @brief swizzle pattern for gfx12.
************************************************************************************************************************
*/

#ifndef __GFX12_SWIZZLE_PATTERN_H__
#define __GFX12_SWIZZLE_PATTERN_H__

namespace Addr
{
namespace V3
{
    const ADDR_SW_PATINFO GFX12_SW_256B_2D_1xAA_PATINFO[] =
    {
        {   0,    0,    0,    0, } , // 1 BPE @ SW_256B_2D_1xAA
        {   1,    0,    0,    0, } , // 2 BPE @ SW_256B_2D_1xAA
        {   2,    0,    0,    0, } , // 4 BPE @ SW_256B_2D_1xAA
        {   3,    0,    0,    0, } , // 8 BPE @ SW_256B_2D_1xAA
        {   4,    0,    0,    0, } , // 16 BPE @ SW_256B_2D_1xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_256B_2D_2xAA_PATINFO[] =
    {
        {   5,    0,    0,    0, } , // 1 BPE @ SW_256B_2D_2xAA
        {   6,    0,    0,    0, } , // 2 BPE @ SW_256B_2D_2xAA
        {   7,    0,    0,    0, } , // 4 BPE @ SW_256B_2D_2xAA
        {   8,    0,    0,    0, } , // 8 BPE @ SW_256B_2D_2xAA
        {   9,    0,    0,    0, } , // 16 BPE @ SW_256B_2D_2xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_256B_2D_4xAA_PATINFO[] =
    {
        {  10,    0,    0,    0, } , // 1 BPE @ SW_256B_2D_4xAA
        {  11,    0,    0,    0, } , // 2 BPE @ SW_256B_2D_4xAA
        {  12,    0,    0,    0, } , // 4 BPE @ SW_256B_2D_4xAA
        {  13,    0,    0,    0, } , // 8 BPE @ SW_256B_2D_4xAA
        {  14,    0,    0,    0, } , // 16 BPE @ SW_256B_2D_4xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_256B_2D_8xAA_PATINFO[] =
    {
        {  15,    0,    0,    0, } , // 1 BPE @ SW_256B_2D_8xAA
        {  16,    0,    0,    0, } , // 2 BPE @ SW_256B_2D_8xAA
        {  17,    0,    0,    0, } , // 4 BPE @ SW_256B_2D_8xAA
        {  18,    0,    0,    0, } , // 8 BPE @ SW_256B_2D_8xAA
        {  19,    0,    0,    0, } , // 16 BPE @ SW_256B_2D_8xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_4KB_2D_1xAA_PATINFO[] =
    {
        {   0,    1,    0,    0, } , // 1 BPE @ SW_4KB_2D_1xAA
        {   1,    2,    0,    0, } , // 2 BPE @ SW_4KB_2D_1xAA
        {   2,    3,    0,    0, } , // 4 BPE @ SW_4KB_2D_1xAA
        {   3,    4,    0,    0, } , // 8 BPE @ SW_4KB_2D_1xAA
        {   4,    5,    0,    0, } , // 16 BPE @ SW_4KB_2D_1xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_4KB_2D_2xAA_PATINFO[] =
    {
        {   5,    2,    0,    0, } , // 1 BPE @ SW_4KB_2D_2xAA
        {   6,    3,    0,    0, } , // 2 BPE @ SW_4KB_2D_2xAA
        {   7,    4,    0,    0, } , // 4 BPE @ SW_4KB_2D_2xAA
        {   8,    5,    0,    0, } , // 8 BPE @ SW_4KB_2D_2xAA
        {   9,    6,    0,    0, } , // 16 BPE @ SW_4KB_2D_2xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_4KB_2D_4xAA_PATINFO[] =
    {
        {  10,    3,    0,    0, } , // 1 BPE @ SW_4KB_2D_4xAA
        {  11,    4,    0,    0, } , // 2 BPE @ SW_4KB_2D_4xAA
        {  12,    5,    0,    0, } , // 4 BPE @ SW_4KB_2D_4xAA
        {  13,    6,    0,    0, } , // 8 BPE @ SW_4KB_2D_4xAA
        {  14,    7,    0,    0, } , // 16 BPE @ SW_4KB_2D_4xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_4KB_2D_8xAA_PATINFO[] =
    {
        {  15,    4,    0,    0, } , // 1 BPE @ SW_4KB_2D_8xAA
        {  16,    5,    0,    0, } , // 2 BPE @ SW_4KB_2D_8xAA
        {  17,    6,    0,    0, } , // 4 BPE @ SW_4KB_2D_8xAA
        {  18,    7,    0,    0, } , // 8 BPE @ SW_4KB_2D_8xAA
        {  19,    8,    0,    0, } , // 16 BPE @ SW_4KB_2D_8xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_64KB_2D_1xAA_PATINFO[] =
    {
        {   0,    1,    1,    0, } , // 1 BPE @ SW_64KB_2D_1xAA
        {   1,    2,    2,    0, } , // 2 BPE @ SW_64KB_2D_1xAA
        {   2,    3,    3,    0, } , // 4 BPE @ SW_64KB_2D_1xAA
        {   3,    4,    4,    0, } , // 8 BPE @ SW_64KB_2D_1xAA
        {   4,    5,    5,    0, } , // 16 BPE @ SW_64KB_2D_1xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_64KB_2D_2xAA_PATINFO[] =
    {
        {   5,    2,    2,    0, } , // 1 BPE @ SW_64KB_2D_2xAA
        {   6,    3,    3,    0, } , // 2 BPE @ SW_64KB_2D_2xAA
        {   7,    4,    4,    0, } , // 4 BPE @ SW_64KB_2D_2xAA
        {   8,    5,    5,    0, } , // 8 BPE @ SW_64KB_2D_2xAA
        {   9,    6,    6,    0, } , // 16 BPE @ SW_64KB_2D_2xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_64KB_2D_4xAA_PATINFO[] =
    {
        {  10,    3,    3,    0, } , // 1 BPE @ SW_64KB_2D_4xAA
        {  11,    4,    4,    0, } , // 2 BPE @ SW_64KB_2D_4xAA
        {  12,    5,    5,    0, } , // 4 BPE @ SW_64KB_2D_4xAA
        {  13,    6,    6,    0, } , // 8 BPE @ SW_64KB_2D_4xAA
        {  14,    7,    7,    0, } , // 16 BPE @ SW_64KB_2D_4xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_64KB_2D_8xAA_PATINFO[] =
    {
        {  15,    4,    4,    0, } , // 1 BPE @ SW_64KB_2D_8xAA
        {  16,    5,    5,    0, } , // 2 BPE @ SW_64KB_2D_8xAA
        {  17,    6,    6,    0, } , // 4 BPE @ SW_64KB_2D_8xAA
        {  18,    7,    7,    0, } , // 8 BPE @ SW_64KB_2D_8xAA
        {  19,    8,    8,    0, } , // 16 BPE @ SW_64KB_2D_8xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_256KB_2D_1xAA_PATINFO[] =
    {
        {   0,    1,    1,    1, } , // 1 BPE @ SW_256KB_2D_1xAA
        {   1,    2,    2,    2, } , // 2 BPE @ SW_256KB_2D_1xAA
        {   2,    3,    3,    3, } , // 4 BPE @ SW_256KB_2D_1xAA
        {   3,    4,    4,    4, } , // 8 BPE @ SW_256KB_2D_1xAA
        {   4,    5,    5,    5, } , // 16 BPE @ SW_256KB_2D_1xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_256KB_2D_2xAA_PATINFO[] =
    {
        {   5,    2,    2,    2, } , // 1 BPE @ SW_256KB_2D_2xAA
        {   6,    3,    3,    3, } , // 2 BPE @ SW_256KB_2D_2xAA
        {   7,    4,    4,    4, } , // 4 BPE @ SW_256KB_2D_2xAA
        {   8,    5,    5,    5, } , // 8 BPE @ SW_256KB_2D_2xAA
        {   9,    6,    6,    6, } , // 16 BPE @ SW_256KB_2D_2xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_256KB_2D_4xAA_PATINFO[] =
    {
        {  10,    3,    3,    3, } , // 1 BPE @ SW_256KB_2D_4xAA
        {  11,    4,    4,    4, } , // 2 BPE @ SW_256KB_2D_4xAA
        {  12,    5,    5,    5, } , // 4 BPE @ SW_256KB_2D_4xAA
        {  13,    6,    6,    6, } , // 8 BPE @ SW_256KB_2D_4xAA
        {  14,    7,    7,    7, } , // 16 BPE @ SW_256KB_2D_4xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_256KB_2D_8xAA_PATINFO[] =
    {
        {  15,    4,    4,    4, } , // 1 BPE @ SW_256KB_2D_8xAA
        {  16,    5,    5,    5, } , // 2 BPE @ SW_256KB_2D_8xAA
        {  17,    6,    6,    6, } , // 4 BPE @ SW_256KB_2D_8xAA
        {  18,    7,    7,    7, } , // 8 BPE @ SW_256KB_2D_8xAA
        {  19,    8,    8,    8, } , // 16 BPE @ SW_256KB_2D_8xAA
    };

    const ADDR_SW_PATINFO GFX12_SW_4KB_3D_PATINFO[] =
    {
        {  20,    9,    0,    0, } , // 1 BPE @ SW_4KB_3D
        {  21,   10,    0,    0, } , // 2 BPE @ SW_4KB_3D
        {  22,   11,    0,    0, } , // 4 BPE @ SW_4KB_3D
        {  23,   12,    0,    0, } , // 8 BPE @ SW_4KB_3D
        {  24,   13,    0,    0, } , // 16 BPE @ SW_4KB_3D
    };

    const ADDR_SW_PATINFO GFX12_SW_64KB_3D_PATINFO[] =
    {
        {  20,    9,    9,    0, } , // 1 BPE @ SW_64KB_3D
        {  21,   10,   10,    0, } , // 2 BPE @ SW_64KB_3D
        {  22,   11,   11,    0, } , // 4 BPE @ SW_64KB_3D
        {  23,   12,   12,    0, } , // 8 BPE @ SW_64KB_3D
        {  24,   13,   13,    0, } , // 16 BPE @ SW_64KB_3D
    };

    const ADDR_SW_PATINFO GFX12_SW_256KB_3D_PATINFO[] =
    {
        {  20,    9,    9,    9, } , // 1 BPE @ SW_256KB_3D
        {  21,   10,   10,    9, } , // 2 BPE @ SW_256KB_3D
        {  22,   11,   11,   10, } , // 4 BPE @ SW_256KB_3D
        {  23,   12,   12,   11, } , // 8 BPE @ SW_256KB_3D
        {  24,   13,   13,   11, } , // 16 BPE @ SW_256KB_3D
    };


    const UINT_64 GFX12_SW_PATTERN_NIBBLE1[][8] =
    {
        {X0,            X1,            Y0,            X2,            Y1,            Y2,            X3,            Y3,            }, // 0
        {0,             X0,            Y0,            X1,            Y1,            X2,            Y2,            X3,            }, // 1
        {0,             0,             X0,            Y0,            X1,            Y1,            X2,            Y2,            }, // 2
        {0,             0,             0,             X0,            Y0,            X1,            X2,            Y1,            }, // 3
        {0,             0,             0,             0,             X0,            Y0,            X1,            Y1,            }, // 4
        {S0,            X0,            Y0,            X1,            Y1,            X2,            Y2,            X3,            }, // 5
        {0,             S0,            X0,            Y0,            X1,            Y1,            X2,            Y2,            }, // 6
        {0,             0,             S0,            X0,            Y0,            X1,            Y1,            X2,            }, // 7
        {0,             0,             0,             S0,            X0,            Y0,            X1,            Y1,            }, // 8
        {0,             0,             0,             0,             S0,            X0,            Y0,            X1,            }, // 9
        {S0,            S1,            X0,            Y0,            X1,            Y1,            X2,            Y2,            }, // 10
        {0,             S0,            S1,            X0,            Y0,            X1,            Y1,            X2,            }, // 11
        {0,             0,             S0,            S1,            X0,            Y0,            X1,            Y1,            }, // 12
        {0,             0,             0,             S0,            S1,            X0,            Y0,            X1,            }, // 13
        {0,             0,             0,             0,             S0,            S1,            X0,            Y0,            }, // 14
        {S0,            S1,            S2,            X0,            Y0,            X1,            Y1,            X2,            }, // 15
        {0,             S0,            S1,            S2,            X0,            Y0,            X1,            Y1,            }, // 16
        {0,             0,             S0,            S1,            S2,            X0,            Y0,            X1,            }, // 17
        {0,             0,             0,             S0,            S1,            S2,            X0,            Y0,            }, // 18
        {0,             0,             0,             0,             S0,            S1,            S2,            X0,            }, // 19
        {X0,            X1,            Z0,            Y0,            Y1,            Z1,            X2,            Z2,            }, // 20
        {0,             X0,            Z0,            Y0,            X1,            Z1,            Y1,            Z2,            }, // 21
        {0,             0,             X0,            Y0,            X1,            Z0,            Y1,            Z1,            }, // 22
        {0,             0,             0,             X0,            Y0,            Z0,            X1,            Z1,            }, // 23
        {0,             0,             0,             0,             X0,            Z0,            Y0,            Z1,            }, // 24
    };

    const UINT_64 GFX12_SW_PATTERN_NIBBLE2[][4] =
    {
        {0,             0,             0,             0,             }, // 0
        {Y4,            X4,            Y5,            X5,            }, // 1
        {Y3,            X4,            Y4,            X5,            }, // 2
        {Y3,            X3,            Y4,            X4,            }, // 3
        {Y2,            X3,            Y3,            X4,            }, // 4
        {Y2,            X2,            Y3,            X3,            }, // 5
        {Y1,            X2,            Y2,            X3,            }, // 6
        {Y1,            X1,            Y2,            X2,            }, // 7
        {Y0,            X1,            Y1,            X2,            }, // 8
        {Y2,            X3,            Z3,            Y3,            }, // 9
        {Y2,            X2,            Z3,            Y3,            }, // 10
        {Y2,            X2,            Z2,            Y3,            }, // 11
        {Y1,            X2,            Z2,            Y2,            }, // 12
        {Y1,            X1,            Z2,            Y2,            }, // 13
    };

    const UINT_64 GFX12_SW_PATTERN_NIBBLE3[][4] =
    {
        {0,             0,             0,             0,             }, // 0
        {Y6,            X6,            Y7,            X7,            }, // 1
        {Y5,            X6,            Y6,            X7,            }, // 2
        {Y5,            X5,            Y6,            X6,            }, // 3
        {Y4,            X5,            Y5,            X6,            }, // 4
        {Y4,            X4,            Y5,            X5,            }, // 5
        {Y3,            X4,            Y4,            X5,            }, // 6
        {Y3,            X3,            Y4,            X4,            }, // 7
        {Y2,            X3,            Y3,            X4,            }, // 8
        {X4,            Z4,            Y4,            X5,            }, // 9
        {X3,            Z4,            Y4,            X4,            }, // 10
        {X3,            Z3,            Y4,            X4,            }, // 11
        {X3,            Z3,            Y3,            X4,            }, // 12
        {X2,            Z3,            Y3,            X3,            }, // 13
    };

    const UINT_64 GFX12_SW_PATTERN_NIBBLE4[][2] =
    {
        {0,             0,             }, // 0
        {Y8,            X8,            }, // 1
        {Y7,            X8,            }, // 2
        {Y7,            X7,            }, // 3
        {Y6,            X7,            }, // 4
        {Y6,            X6,            }, // 5
        {Y5,            X6,            }, // 6
        {Y5,            X5,            }, // 7
        {Y4,            X5,            }, // 8
        {Z5,            Y5,            }, // 9
        {Z4,            Y5,            }, // 10
        {Z4,            Y4,            }, // 11
    };

} // V3
} // Addr

#endif
