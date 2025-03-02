/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#include "ToneMapGenerator.h"
#include "AGMGenerator.h"
#include "CSCGenerator.h"
#include <stdlib.h>
#include <string.h>

/* Defines comes from ColorPrimaryTable.h */
struct ToneMapHdrMetaData BT2020Container = {
    (unsigned short)(0.708 * 50000),  (unsigned short)(0.292 * 50000),
    (unsigned short)(0.17 * 50000),   (unsigned short)(0.797 * 50000),
    (unsigned short)(0.131 * 50000),  (unsigned short)(0.046 * 50000),
    (unsigned short)(0.3127 * 50000), (unsigned short)(0.3290 * 50000),
    (unsigned int)(10000 * 10000),    (unsigned int)(0.05 * 10000),
    (unsigned short)10000,
    (unsigned short)10000
};

struct ToneMapHdrMetaData DCIP3Container = {
    (unsigned short)(0.68 * 50000),   (unsigned short)(0.32 * 50000),
    (unsigned short)(0.265 * 50000),  (unsigned short)(0.69 * 50000),
    (unsigned short)(0.15 * 50000),   (unsigned short)(0.06 * 50000),
    (unsigned short)(0.3127 * 50000), (unsigned short)(0.3290 * 50000),
    (unsigned int)(10000 * 10000),    (unsigned int)(0.05 * 10000),
    (unsigned short)10000,
    (unsigned short)10000
};

struct ToneMapHdrMetaData BT709Container = {
    (unsigned short)(0.64 * 50000),   (unsigned short)(0.33 * 50000),
    (unsigned short)(0.30 * 50000),   (unsigned short)(0.60 * 50000),
    (unsigned short)(0.15 * 50000),   (unsigned short)(0.06 * 50000),
    (unsigned short)(0.3127 * 50000), (unsigned short)(0.3290 * 50000),
    (unsigned int)(10000 * 10000),    (unsigned int)(0.05 * 10000),
    (unsigned short)10000,
    (unsigned short)10000
};

struct ToneMapHdrMetaData BT601Container = {
    (unsigned short)(0.63 * 50000),   (unsigned short)(0.34 * 50000),
    (unsigned short)(0.31 * 50000),   (unsigned short)(0.595 * 50000),
    (unsigned short)(0.155 * 50000),  (unsigned short)(0.07 * 50000),
    (unsigned short)(0.3127 * 50000), (unsigned short)(0.3290 * 50000),
    (unsigned int)(10000 * 10000),    (unsigned int)(0.05 * 10000),
    (unsigned short)10000,
    (unsigned short)10000
};


//Function declaration
enum ToneMapColorPrimaries ToneMapGenerator_GetLutColorIn(void);
enum ToneMapColorPrimaries ToneMapGenerator_GetLutColorOut(
    enum ToneMapTransferFunction outputContainerGamma,
    enum ToneMapColorPrimaries   outputContainerPrimaries);
enum ToneMapTransferFunction ToneMapGenerator_GetShaperTf(
    enum ToneMapTransferFunction inputContainerGamma);
enum ToneMapTransferFunction ToneMapGenerator_GetLutOutTf(
    enum ToneMapTransferFunction outputContainerGamma,
    enum ToneMapColorPrimaries   outputContainerPrimaries);
unsigned short ToneMapGenerator_GetInputNormFactor(
    const struct ToneMapHdrMetaData* streamMetaData);
bool ToneMapGenerator_CacheSrcTmParams(
    struct ToneMapGenerator* p_tmGenerator,
    const struct ToneMapHdrMetaData* streamMetaData,
    enum ToneMapTransferFunction inputContainerGamma);
bool ToneMapGenerator_CacheDstTmParams(
    struct ToneMapGenerator* p_tmGenerator,
    const struct ToneMapHdrMetaData* dstMetaData,
    enum ToneMapTransferFunction outputContainerGamma,
    enum ToneMapColorPrimaries   outputContainerPrimaries);
enum TMGReturnCode ToneMapGenerator_GenerateLutData(
    struct ToneMapGenerator*         p_tmGenerator,
    const struct ToneMapHdrMetaData* streamMetaData,
    const struct ToneMapHdrMetaData* dstMetaData,
    enum ToneMapAlgorithm            tmAlgorithm,
    bool                             updateSrcParams,
    bool                             updateDstParams,
    struct ToneMappingParameters*    tmParams);
struct ToneMapHdrMetaData ToneMapGenerator_GetColorContainerData(
    enum ToneMapColorPrimaries containerColor);
bool ToneMapGenerator_ContentEqualsContainer(
    const struct ToneMapHdrMetaData* contentMetaData,
    const struct ToneMapHdrMetaData* containerPrimaries);


enum TMGReturnCode ToneMapGenerator_GenerateToneMappingParameters(
    struct ToneMapGenerator*         p_tmGenerator,
    const struct ToneMapHdrMetaData* streamMetaData,
    const struct ToneMapHdrMetaData* dstMetaData,
    enum ToneMapTransferFunction     inputContainerGamma,
    enum ToneMapTransferFunction     outputContainerGamma,
    enum ToneMapColorPrimaries       outputContainerPrimaries,
    unsigned short                   lutDim,
    struct ToneMappingParameters*    tmParams)
{

    enum TMGReturnCode ret = TMG_RET_OK;
    bool updateSrcParams;
    bool updateDstParams;

    if (!p_tmGenerator->memAllocSet) {
        ret = TMG_RET_ERROR_NOT_INITIALIZED;
        goto exit;
    }

    tmParams->lutOutTf                 = ToneMapGenerator_GetLutOutTf(outputContainerGamma, outputContainerPrimaries);
    tmParams->lutColorIn               = ToneMapGenerator_GetLutColorIn();
    tmParams->lutColorOut              = ToneMapGenerator_GetLutColorOut(outputContainerGamma, outputContainerPrimaries);
    tmParams->shaperTf                 = ToneMapGenerator_GetShaperTf(inputContainerGamma);
    tmParams->formattedLutData         = NULL;
    tmParams->lutDim                   = lutDim;
    tmParams->inputNormalizationFactor = ToneMapGenerator_GetInputNormFactor(streamMetaData);

    updateSrcParams = ToneMapGenerator_CacheSrcTmParams(p_tmGenerator, streamMetaData, inputContainerGamma);
    updateDstParams = ToneMapGenerator_CacheDstTmParams(p_tmGenerator, dstMetaData, outputContainerGamma, outputContainerPrimaries);

    ret = ToneMapGenerator_GenerateLutData(p_tmGenerator, streamMetaData, dstMetaData, p_tmGenerator->tmAlgo, updateSrcParams, updateDstParams,  tmParams);

exit:
    return ret;
}

enum ToneMapColorPrimaries ToneMapGenerator_GetLutColorIn()
{
    return TMG_CP_BT2020;
}

enum ToneMapColorPrimaries ToneMapGenerator_GetLutColorOut(
    enum ToneMapTransferFunction outputContainerGamma,
    enum ToneMapColorPrimaries   outputContainerPrimaries)
{
    enum ToneMapColorPrimaries lutOutPrimaries;

    if (outputContainerGamma == TMG_TF_Linear)
        lutOutPrimaries = TMG_CP_BT2020;
    else
        lutOutPrimaries = outputContainerPrimaries;

    return lutOutPrimaries;
}

enum ToneMapTransferFunction ToneMapGenerator_GetShaperTf(
    enum ToneMapTransferFunction inputContainerGamma)
{
    enum ToneMapTransferFunction shaperTf;

    switch (inputContainerGamma) {
    case(TMG_TF_PQ):
    case(TMG_TF_Linear):
        shaperTf = TMG_TF_NormalizedPQ;
        break;
    default:
        shaperTf = inputContainerGamma;
        break;
    }

    return shaperTf;
}

enum ToneMapTransferFunction ToneMapGenerator_GetLutOutTf(
    enum ToneMapTransferFunction outputContainerGamma,
    enum ToneMapColorPrimaries   outputContainerPrimaries)
{
    enum ToneMapTransferFunction lutOutTf;

    if (outputContainerGamma == TMG_TF_Linear ||
        outputContainerGamma == TMG_TF_PQ)
        lutOutTf = TMG_TF_PQ;
    else
        lutOutTf = outputContainerGamma;

    return lutOutTf;
}

struct ToneMapHdrMetaData ToneMapGenerator_GetColorContainerData(enum ToneMapColorPrimaries containerColor) {

    switch (containerColor) {
    case (TMG_CP_BT601):
        return BT601Container;
        break;
    case (TMG_CP_BT709):
        return BT709Container;
        break;
    case (TMG_CP_BT2020):
        return BT2020Container;
        break;
    case (TMG_CP_DCIP3):
        return DCIP3Container;
        break;
    default:
        return BT2020Container;
        break;
    }

}

unsigned short ToneMapGenerator_GetInputNormFactor(const struct ToneMapHdrMetaData* streamMetaData) {

    unsigned short normFactor;

    if (streamMetaData->maxMasteringLuminance < INPUT_NORMALIZATION_FACTOR)
        normFactor = INPUT_NORMALIZATION_FACTOR;
    else
        normFactor = streamMetaData->maxMasteringLuminance;

    return normFactor;
}

bool ToneMapGenerator_ContentEqualsContainer(
    const struct ToneMapHdrMetaData* contentMetaData,
    const struct ToneMapHdrMetaData* containerPrimaries)
{

    if (abs(contentMetaData->bluePrimaryX  - containerPrimaries->redPrimaryX)   < 2 &&
        abs(contentMetaData->redPrimaryY   - containerPrimaries->redPrimaryY)   < 2 &&
        abs(contentMetaData->greenPrimaryX - containerPrimaries->greenPrimaryX) < 2 &&
        abs(contentMetaData->greenPrimaryY - containerPrimaries->greenPrimaryY) < 2 &&
        abs(contentMetaData->bluePrimaryX  - containerPrimaries->bluePrimaryX)  < 2 &&
        abs(contentMetaData->bluePrimaryY  - containerPrimaries->bluePrimaryY)  < 2)
        return true;
    else
        return false;
}

/*
    Tone map generation consists of three steps:
    1. Container to content color space conversion.
    2. Tone mapping and gamut mapping operation.
    3. Content to output container color space conversion.

    These operations are cascaded one after the other. The enable3DLUTMerge will tell each module
    whether or not to start from scratch, or use the previous blocks output as the nextbloack input.

    The terminology "Content Color Space / Container Color Space" is used to distinguish
    between the color volume of the content and the color volume of the container. 
    For example, the content color volume might be DCIP3 and the Container might be BT2020.
    CSC step changes the representation of the content to align with its color volume.
*/
enum TMGReturnCode ToneMapGenerator_GenerateLutData(
    struct ToneMapGenerator*         p_tmGenerator,
    const struct ToneMapHdrMetaData* streamMetaData,
    const struct ToneMapHdrMetaData* dstMetaData,
    enum ToneMapAlgorithm            tmAlgorithm,
    bool                             updateSrcParams,
    bool                             updateDstParams,
    struct ToneMappingParameters*    tmParams)
{

    bool enable3DLUTMerge           = false;
    struct ToneMapHdrMetaData lutContainer = ToneMapGenerator_GetColorContainerData(tmParams->lutColorIn);

    if (!ToneMapGenerator_ContentEqualsContainer(streamMetaData, &lutContainer)) {
        lutContainer.maxMasteringLuminance = streamMetaData->maxMasteringLuminance;
        lutContainer.minMasteringLuminance = streamMetaData->minMasteringLuminance;

        CSCGenerator_ApplyCSC(
            &lutContainer,
            tmParams->shaperTf,
            streamMetaData,
            tmParams->shaperTf,
            tmParams,
            enable3DLUTMerge);

        enable3DLUTMerge = true;
    }

    AGMGenerator_ApplyToneMap(
        &p_tmGenerator->agmGenerator,
        streamMetaData,
        dstMetaData,
        tmAlgorithm,
        tmParams,
        updateSrcParams,
        updateDstParams,
        enable3DLUTMerge);

    enable3DLUTMerge = true;

    lutContainer = ToneMapGenerator_GetColorContainerData(tmParams->lutColorOut);
    if (!ToneMapGenerator_ContentEqualsContainer(dstMetaData, &lutContainer)) {
        lutContainer.maxMasteringLuminance = dstMetaData->maxMasteringLuminance;
        lutContainer.minMasteringLuminance = dstMetaData->minMasteringLuminance;

        CSCGenerator_ApplyCSC(
            dstMetaData,
            tmParams->lutOutTf,
            &lutContainer,
            tmParams->lutOutTf,
            tmParams,
            enable3DLUTMerge
            );
    }

    return TMG_RET_OK;
}

bool ToneMapGenerator_CacheSrcTmParams(
    struct ToneMapGenerator* p_tmGenerator,
    const struct ToneMapHdrMetaData* streamMetaData,
    enum ToneMapTransferFunction inputContainerGamma)
{
    bool updateSrcParams =  memcmp(streamMetaData, &p_tmGenerator->cachedSrcTmParams.streamMetaData, sizeof(struct ToneMapHdrMetaData)) ||
        inputContainerGamma != p_tmGenerator->cachedSrcTmParams.inputContainerGamma;

    if (updateSrcParams) {
        memcpy(&p_tmGenerator->cachedSrcTmParams.streamMetaData, streamMetaData, sizeof(struct ToneMapHdrMetaData));
        p_tmGenerator->cachedSrcTmParams.inputContainerGamma = inputContainerGamma;
    }

    return updateSrcParams;
}

bool ToneMapGenerator_CacheDstTmParams(
    struct ToneMapGenerator* p_tmGenerator,
    const struct ToneMapHdrMetaData* dstMetaData,
    enum ToneMapTransferFunction outputContainerGamma,
    enum ToneMapColorPrimaries   outputContainerPrimaries)
{
    bool updateDstParams = memcmp(dstMetaData, &p_tmGenerator->cachedDstTmParams.dstMetaData, sizeof(struct ToneMapHdrMetaData)) ||
        outputContainerGamma != p_tmGenerator->cachedDstTmParams.outputContainerGamma ||
        outputContainerPrimaries != p_tmGenerator->cachedDstTmParams.outputContainerPrimaries;

    if (updateDstParams){
        memcpy(&p_tmGenerator->cachedDstTmParams.dstMetaData, dstMetaData, sizeof(struct ToneMapHdrMetaData));
        p_tmGenerator->cachedDstTmParams.outputContainerGamma     = outputContainerGamma;
        p_tmGenerator->cachedDstTmParams.outputContainerPrimaries = outputContainerPrimaries;
        p_tmGenerator->cachedDstTmParams.outputContainerPrimaries = outputContainerPrimaries;
    }

    return updateDstParams;
}

enum TMGReturnCode ToneMapGenerator_SetInternalAllocators(
    struct ToneMapGenerator* p_tmGenerator,
    TMGAlloc                 allocFunc,
    TMGFree                  freeFunc,
    void*                    memCtx)
{
    enum TMGReturnCode ret = AGMGenerator_SetGMAllocator(
        &p_tmGenerator->agmGenerator,
        allocFunc,
        freeFunc,
        memCtx);

    p_tmGenerator->memAllocSet = true;

    return ret;
}