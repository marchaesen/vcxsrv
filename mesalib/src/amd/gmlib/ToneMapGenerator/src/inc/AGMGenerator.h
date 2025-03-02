/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#pragma once
#include "ToneMapTypes.h"
#include "gm_api_funcs.h"

/* Replace CPP class: AGMGenerator */
struct AGMGenerator {
    TMGAlloc           allocFunc;
    TMGFree            freeFunc;
    void*              memoryContext;
    bool               initalized;
    struct s_gamut_map agmParams;
    struct s_gm_opts   gamutMapParams;
};

enum TMGReturnCode AGMGenerator_ApplyToneMap(
    struct AGMGenerator*                p_agm_generator,
    const struct ToneMapHdrMetaData*    streamMetaData,
    const struct ToneMapHdrMetaData*    dtMetaData,
    const enum ToneMapAlgorithm         tmAlgorithm,
    const struct ToneMappingParameters* tmParams,
    bool                                updateSrcParams,
    bool                                updateDstParams,
    bool                                enableMerge3DLUT);

enum TMGReturnCode AGMGenerator_SetGMAllocator(
    struct AGMGenerator* p_agm_generator,
    TMGAlloc             allocFunc,
    TMGFree              freeFunc,
    void*                memCtx);

/* Replace ~AGMGenerator() */
void AGMGenerator_Exit(struct AGMGenerator* p_agm_generator);
