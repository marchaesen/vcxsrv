/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ToneMapGenerator.h"
#include "AGMGenerator.h"
#include "tonemap_adaptor.h"

static void VPEFree3DLut(void* memToFree, void* pDevice)
{
   free(memToFree);
}

static void* VPEAlloc3DLut(unsigned int allocSize, void* pDevice)
{
    return calloc(1, allocSize);
}

void* tm_create(void)
{
    struct ToneMapGenerator* p_tmGenerator = (struct ToneMapGenerator*)calloc(1, sizeof(struct ToneMapGenerator));
    if (!p_tmGenerator)
        return NULL;

    p_tmGenerator->tmAlgo = TMG_A_AGM;
    p_tmGenerator->memAllocSet = false;
    p_tmGenerator->agmGenerator.initalized = false;

    return (void*)p_tmGenerator;
}

void tm_destroy(void** pp_tmGenerator)
{
    struct ToneMapGenerator* p_tmGenerator;

    if (!pp_tmGenerator || ((*pp_tmGenerator) == NULL))
        return;

    p_tmGenerator = *pp_tmGenerator;
    AGMGenerator_Exit(&p_tmGenerator->agmGenerator);

    free(p_tmGenerator);
    *pp_tmGenerator = NULL;
}

int tm_generate3DLut(struct tonemap_param* pInparam, void* pformattedLutData)
{
    enum TMGReturnCode               result;
    struct ToneMappingParameters     tmParams;

    tmParams.lutData = (uint16_t *)pformattedLutData;

    ToneMapGenerator_SetInternalAllocators(
                    (struct ToneMapGenerator*)pInparam->tm_handle,
                    (TMGAlloc)(VPEAlloc3DLut),
                    (TMGFree)(VPEFree3DLut),
                    (void*)(NULL));

    result = ToneMapGenerator_GenerateToneMappingParameters(
                    (struct ToneMapGenerator*)pInparam->tm_handle,
                    &pInparam->streamMetaData,
                    &pInparam->dstMetaData,
                    pInparam->inputContainerGamma,
                    pInparam->outputContainerGamma,
                    pInparam->outputContainerPrimaries,
                    pInparam->lutDim,
                    &tmParams
    );

    return (int)result;
}
