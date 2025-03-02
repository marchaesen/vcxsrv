/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#pragma once
#include <stdbool.h>

#define MAX_LUMINANCE 10000.0
#define INPUT_NORMALIZATION_FACTOR 4000 //nits
typedef void* (*TMGAlloc)(unsigned int, void*);
typedef void (*TMGFree)(void*, void*);

struct ToneMapHdrMetaData
{
    unsigned short  redPrimaryX;
    unsigned short  redPrimaryY;
    unsigned short  greenPrimaryX;
    unsigned short  greenPrimaryY;
    unsigned short  bluePrimaryX;
    unsigned short  bluePrimaryY;
    unsigned short  whitePointX;
    unsigned short  whitePointY;
    unsigned int    maxMasteringLuminance;
    unsigned int    minMasteringLuminance;
    unsigned short  maxContentLightLevel;
    unsigned short  maxFrameAverageLightLevel;
};

enum ToneMapTransferFunction {
    TMG_TF_SRGB,
    TMG_TF_BT709,
    TMG_TF_G24,
    TMG_TF_PQ,
    TMG_TF_NormalizedPQ,
    TMG_TF_ModifiedPQ,
    TMG_TF_Linear,
    TMG_TF_HLG
};

enum ToneMapColorPrimaries {
    TMG_CP_BT601,
    TMG_CP_BT709,
    TMG_CP_BT2020,
    TMG_CP_DCIP3
};

enum ToneMapAlgorithm {
    TMG_A_AGM,
    TMG_A_BT2390,
    TMG_A_BT2390_4
};

struct ToneMappingParameters {
    enum ToneMapColorPrimaries   lutColorIn;
    enum ToneMapColorPrimaries   lutColorOut;
    enum ToneMapTransferFunction shaperTf;
    enum ToneMapTransferFunction lutOutTf;
    unsigned short               lutDim;
    unsigned short*              lutData;
    void*                        formattedLutData;
    unsigned short               inputNormalizationFactor;
};

enum TMGReturnCode {
    TMG_RET_OK,
    TMG_RET_ERROR_DUPLICATE_INIT,
    TMG_RET_ERROR_INVALID_PARAM,
    TMG_RET_ERROR_NOT_INITIALIZED,
    TMG_RET_ERROR_GMLIB
};
