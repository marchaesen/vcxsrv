/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#pragma once
#include "ToneMapTypes.h"
#include "AGMGenerator.h"

struct SrcTmParams {
    struct ToneMapHdrMetaData    streamMetaData;
    enum ToneMapTransferFunction inputContainerGamma;
};

struct DstTmParams {
    struct ToneMapHdrMetaData    dstMetaData;
    enum ToneMapTransferFunction outputContainerGamma;
    enum ToneMapColorPrimaries   outputContainerPrimaries;
};

struct ToneMapGenerator {
    struct AGMGenerator agmGenerator;
    enum ToneMapAlgorithm tmAlgo;
    bool memAllocSet;
    struct SrcTmParams cachedSrcTmParams;
    struct DstTmParams cachedDstTmParams;
};

enum TMGReturnCode ToneMapGenerator_GenerateToneMappingParameters(
    struct ToneMapGenerator* p_tmGenerator,
    const struct ToneMapHdrMetaData* streamMetaData,
    const struct ToneMapHdrMetaData* dstMetaData,
    enum ToneMapTransferFunction inputContainerGamma,
    enum ToneMapTransferFunction outputContainerGamma,
    enum ToneMapColorPrimaries  outputContainerPrimaries,
    unsigned short lutDim,
    struct ToneMappingParameters* tmParams);

enum TMGReturnCode ToneMapGenerator_SetInternalAllocators(
    struct ToneMapGenerator* p_tmGenerator,
    TMGAlloc                 allocFunc,
    TMGFree                  freeFunc,
    void*                    memCtx);
