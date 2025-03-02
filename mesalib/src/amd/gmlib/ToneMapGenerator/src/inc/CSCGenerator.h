/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#pragma once
#include "csc_api_funcs.h"
#include "ToneMapTypes.h"

static bool TranslateTfEnum(
    enum ToneMapTransferFunction inTf,
    enum cs_gamma_type*          outTf)
{

    switch (inTf) {
    case(TMG_TF_SRGB):
        *outTf = EGT_sRGB;
        break;
    case(TMG_TF_BT709):
        *outTf = EGT_709;
        break;
    case(TMG_TF_G24):
        *outTf = EGT_2_4;
        break;
    case(TMG_TF_HLG):
        *outTf = EGT_HLG;
        break;
    case(TMG_TF_NormalizedPQ):
    case(TMG_TF_PQ):
        *outTf = EGT_PQ;
        break;
    default:
        return false;
    }
    return true;
}

static void CSCCtor(struct s_csc_map* csc_map)
{
    csc_ctor(csc_map);
}

static enum TMGReturnCode CSCSetOptions(
    const struct ToneMapHdrMetaData*    srcMetaData,
    enum ToneMapTransferFunction        inTf,
    const struct ToneMapHdrMetaData*    dstMetaData,
    enum ToneMapTransferFunction        outTf,
    const struct ToneMappingParameters* tmParams,
    bool                                merge3DLUT,
    struct s_csc_api_opts*              csc_opts)
{

    enum TMGReturnCode ret = TMG_RET_OK;
    enum cs_gamma_type inGamma;
    enum cs_gamma_type outGamma;

    if (!TranslateTfEnum(inTf, &inGamma)) {
        ret = TMG_RET_ERROR_INVALID_PARAM;
        goto exit;
    }

    if(!TranslateTfEnum(outTf, &outGamma)) {
        ret = TMG_RET_ERROR_INVALID_PARAM;
        goto exit;
    }

    csc_opts->ptr_3dlut_rgb  = tmParams->lutData;
    csc_opts->num_pnts_3dlut = tmParams->lutDim;
    csc_opts->bitwidth_3dlut = 12;
    csc_opts->en_merge_3dlut = merge3DLUT;


    csc_opts->cs_opts_src.color_space_type = ECST_CUSTOM;
    csc_opts->cs_opts_src.rgbw_xy[0] =
        srcMetaData->redPrimaryX / 50000.0;
    csc_opts->cs_opts_src.rgbw_xy[1] =
        srcMetaData->redPrimaryY / 50000.0;
    csc_opts->cs_opts_src.rgbw_xy[2] =
        srcMetaData->greenPrimaryX / 50000.0;
    csc_opts->cs_opts_src.rgbw_xy[3] =
        srcMetaData->greenPrimaryY / 50000.0;
    csc_opts->cs_opts_src.rgbw_xy[4] =
        srcMetaData->bluePrimaryX / 50000.0;
    csc_opts->cs_opts_src.rgbw_xy[5] =
        srcMetaData->bluePrimaryY / 50000.0;
    csc_opts->cs_opts_src.rgbw_xy[6] =
        srcMetaData->whitePointX / 50000.0;
    csc_opts->cs_opts_src.rgbw_xy[7] =
        srcMetaData->whitePointY / 50000.0;

    csc_opts->cs_opts_src.gamma_type          = inGamma;
    csc_opts->cs_opts_src.luminance_limits[0] = 0.0;
    csc_opts->cs_opts_src.luminance_limits[1] =
        (double)srcMetaData->maxMasteringLuminance;

    if (inTf == TMG_TF_NormalizedPQ)
        csc_opts->cs_opts_src.pq_norm = (double)tmParams->inputNormalizationFactor;
    else
        csc_opts->cs_opts_src.pq_norm = MAX_LUMINANCE;


    csc_opts->cs_opts_dst.color_space_type = ECST_CUSTOM;
    csc_opts->cs_opts_dst.rgbw_xy[0] =
        dstMetaData->redPrimaryX / 50000.0;
    csc_opts->cs_opts_dst.rgbw_xy[1] =
        dstMetaData->redPrimaryY / 50000.0;
    csc_opts->cs_opts_dst.rgbw_xy[2] =
        dstMetaData->greenPrimaryX / 50000.0;
    csc_opts->cs_opts_dst.rgbw_xy[3] =
        dstMetaData->greenPrimaryY / 50000.0;
    csc_opts->cs_opts_dst.rgbw_xy[4] =
        dstMetaData->bluePrimaryX / 50000.0;
    csc_opts->cs_opts_dst.rgbw_xy[5] =
        dstMetaData->bluePrimaryY / 50000.0;
    csc_opts->cs_opts_dst.rgbw_xy[6] =
        dstMetaData->whitePointX / 50000.0;
    csc_opts->cs_opts_dst.rgbw_xy[7] =
        dstMetaData->whitePointY / 50000.0;

    csc_opts->cs_opts_dst.gamma_type          = outGamma;
    csc_opts->cs_opts_dst.luminance_limits[0] = 0.0;
    csc_opts->cs_opts_dst.luminance_limits[1] =
        (double)dstMetaData->maxMasteringLuminance;

    if (outTf == TMG_TF_NormalizedPQ)
        csc_opts->cs_opts_dst.pq_norm = (double)tmParams->inputNormalizationFactor;
    else
        csc_opts->cs_opts_dst.pq_norm = MAX_LUMINANCE;

    exit:
    return ret;
}

static void CSCSetDefault(struct s_csc_api_opts* csc_opts)
{
    csc_api_set_def(csc_opts);
}

static void CSCGenerateMap(struct s_csc_api_opts* csc_opts, struct s_csc_map* csc_map)
{
    csc_api_gen_map(csc_opts, csc_map);
}

static enum TMGReturnCode CSCGenerate3DLUT(struct s_csc_api_opts* csc_opts, struct s_csc_map* csc_map)
{
    int retcode = csc_api_gen_3dlut(csc_opts, csc_map);

    return retcode ? TMG_RET_ERROR_GMLIB : TMG_RET_OK;
}

static enum TMGReturnCode CSCGenerator_ApplyCSC(
    const struct ToneMapHdrMetaData* srcMetaData,
    enum ToneMapTransferFunction     inTf,
    const struct ToneMapHdrMetaData* dstMetaData,
    enum ToneMapTransferFunction     outTf,
    struct ToneMappingParameters*    tmParams,
    bool                             enable3DLUTMerge)
{
    struct s_csc_map      csc_map;
    struct s_csc_api_opts csc_opts;

    CSCCtor(&csc_map);
    CSCSetDefault(&csc_opts);
    CSCSetOptions(srcMetaData,
        inTf,
        dstMetaData,
        outTf,
        tmParams,
        enable3DLUTMerge,
        &csc_opts);
    CSCGenerateMap(&csc_opts, &csc_map);

    return CSCGenerate3DLUT(&csc_opts, &csc_map);
}
