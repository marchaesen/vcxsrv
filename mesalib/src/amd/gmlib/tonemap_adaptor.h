/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ToneMapGenerator/inc/ToneMapTypes.h"

struct tonemap_param
{
    void*                        tm_handle;
    struct ToneMapHdrMetaData    streamMetaData;
    struct ToneMapHdrMetaData    dstMetaData;
    enum ToneMapTransferFunction inputContainerGamma;
    enum ToneMapTransferFunction outputContainerGamma;
    enum ToneMapColorPrimaries   outputContainerPrimaries;
    unsigned short               lutDim;
};

void* tm_create(void);
void  tm_destroy(void** pp_tmGenerator);
int   tm_generate3DLut(struct tonemap_param* pInparam, void* pformattedLutData);

#ifdef __cplusplus
}
#endif