/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors: AMD
 *
 */

#include "AGMGenerator.h"

// Function declaration
void AGMGenerator_GMCtor(struct AGMGenerator* p_agm_generator);
void AGMGenerator_GMSetDefault(struct AGMGenerator* p_agm_generator);
enum TMGReturnCode AGMGenerator_SetAgmOptions(
    struct AGMGenerator*                p_agm_generator,
    const struct ToneMapHdrMetaData*    srcMetaData,
    const struct ToneMapHdrMetaData*    dstMetaData,
    const enum ToneMapAlgorithm         tmAlgorithm,
    const struct ToneMappingParameters* tmParams,
    bool                                updateSrcParams,
    bool                                updateDstParams,
    bool                                enableMerge3DLUT);
enum TMGReturnCode AGMGenerator_GMGenerateMap(struct AGMGenerator* p_agm_generator);
enum TMGReturnCode AGMGenerator_GMGenerate3DLUT(struct AGMGenerator* p_agm_generator);

static bool TranslateTfEnum(
    enum ToneMapTransferFunction inTf,
    enum cs_gamma_type* outTf)
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

enum TMGReturnCode AGMGenerator_SetGMAllocator(
    struct AGMGenerator* p_agm_generator,
    TMGAlloc             allocFunc,
    TMGFree              freeFunc,
    void*                memCtx)
{
    p_agm_generator->allocFunc     = allocFunc;
    p_agm_generator->freeFunc      = freeFunc;
    p_agm_generator->memoryContext = memCtx;
    return TMG_RET_OK;
}

enum TMGReturnCode AGMGenerator_ApplyToneMap(
    struct AGMGenerator*                p_agm_generator,
    const struct ToneMapHdrMetaData*    streamMetaData,
    const struct ToneMapHdrMetaData*    dstMetaData,
    const enum ToneMapAlgorithm         tmAlgorithm,
    const struct ToneMappingParameters* tmParams,
    bool                                updateSrcParams,
    bool                                updateDstParams,
    bool                                enableMerge3DLUT)
{
    enum TMGReturnCode ret = TMG_RET_OK;

    if (!p_agm_generator->initalized) {
        AGMGenerator_GMCtor(p_agm_generator);
        AGMGenerator_GMSetDefault(p_agm_generator);
        p_agm_generator->initalized = true;
    }

    if ((ret = AGMGenerator_SetAgmOptions(
        p_agm_generator,
        streamMetaData,
        dstMetaData,
        tmAlgorithm,
        tmParams,
        updateSrcParams,
        updateDstParams,
        enableMerge3DLUT)) != TMG_RET_OK)
        goto exit;

    if ((ret = AGMGenerator_GMGenerateMap(p_agm_generator)) != TMG_RET_OK)
        goto exit;

    if ((ret = AGMGenerator_GMGenerate3DLUT(p_agm_generator)) != TMG_RET_OK)
        goto exit;

exit:
    return ret;
}

enum TMGReturnCode AGMGenerator_SetAgmOptions(
    struct AGMGenerator*                p_agm_generator,
    const struct ToneMapHdrMetaData*    srcMetaData,
    const struct ToneMapHdrMetaData*    dstMetaData,
    const enum ToneMapAlgorithm         tmAlgorithm,
    const struct ToneMappingParameters* tmParams,
    bool                                updateSrcParams,
    bool                                updateDstParams,
    bool                                enableMerge3DLUT)
{
    enum TMGReturnCode ret = TMG_RET_OK;
    enum cs_gamma_type inGamma;
    enum cs_gamma_type outGamma;

    if (!TranslateTfEnum(tmParams->shaperTf, &inGamma)) {
        ret = TMG_RET_ERROR_INVALID_PARAM;
        goto exit;
    }

    if (!TranslateTfEnum(tmParams->lutOutTf, &outGamma)) {
        ret = TMG_RET_ERROR_INVALID_PARAM;
        goto exit;
    }

    if (tmAlgorithm == TMG_A_AGM) {
        p_agm_generator->gamutMapParams.gamut_map_mode = EGMM_TM_CHTO;
        p_agm_generator->gamutMapParams.hue_rot_mode   = EHRM_HR;
    }
    else {
        p_agm_generator->gamutMapParams.gamut_map_mode = EGMM_TM;
        p_agm_generator->gamutMapParams.hue_rot_mode   = EHRM_NONE;
    }

    p_agm_generator->gamutMapParams.update_msk = updateSrcParams ? GM_UPDATE_SRC : 0;
    p_agm_generator->gamutMapParams.update_msk = updateDstParams ? (p_agm_generator->gamutMapParams.update_msk | GM_UPDATE_DST) : p_agm_generator->gamutMapParams.update_msk;

    p_agm_generator->gamutMapParams.ptr_3dlut_rgb     = tmParams->lutData;
    p_agm_generator->gamutMapParams.num_pnts_3dlut    = tmParams->lutDim;
    p_agm_generator->gamutMapParams.bitwidth_3dlut    = 12;
    p_agm_generator->gamutMapParams.en_merge_3dlut    = enableMerge3DLUT;
    p_agm_generator->gamutMapParams.mode              = GM_PQTAB_GBD;
    p_agm_generator->gamutMapParams.en_tm_scale_color = 1;
    p_agm_generator->gamutMapParams.num_hue_pnts      = GM_NUM_HUE;
    p_agm_generator->gamutMapParams.num_edge_pnts     = GM_NUM_EDGE;
    p_agm_generator->gamutMapParams.num_int_pnts      = GM_NUM_INT;
    p_agm_generator->gamutMapParams.org2_perc_c       = GM_ORG2_PERC;
    p_agm_generator->gamutMapParams.step_samp         = 0.0005; // GM_STEP_SAMP = 0.0001;
    p_agm_generator->gamutMapParams.show_pix_mode     = ESPM_NONE;

    for (int i = 0; i < GM_NUM_PRIM; i++) {
        p_agm_generator->gamutMapParams.vec_org1_factor[i] = gm_vec_org13_factor_def[i][0];
        p_agm_generator->gamutMapParams.vec_org3_factor[i] = gm_vec_org13_factor_def[i][1];
    }

    p_agm_generator->gamutMapParams.cs_opts_src.color_space_type = ECST_CUSTOM;
    p_agm_generator->gamutMapParams.cs_opts_src.rgbw_xy[0] =
        srcMetaData->redPrimaryX / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_src.rgbw_xy[1] =
        srcMetaData->redPrimaryY / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_src.rgbw_xy[2] =
        srcMetaData->greenPrimaryX / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_src.rgbw_xy[3] =
        srcMetaData->greenPrimaryY / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_src.rgbw_xy[4] =
        srcMetaData->bluePrimaryX / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_src.rgbw_xy[5] =
        srcMetaData->bluePrimaryY / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_src.rgbw_xy[6] =
        srcMetaData->whitePointX / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_src.rgbw_xy[7] =
        srcMetaData->whitePointY / 50000.0;

    p_agm_generator->gamutMapParams.cs_opts_src.gamma_type = inGamma;
    p_agm_generator->gamutMapParams.cs_opts_src.luminance_limits[0] = 0;
    p_agm_generator->gamutMapParams.cs_opts_src.luminance_limits[1] =
        (double)srcMetaData->maxMasteringLuminance;

    if (tmParams->shaperTf == TMG_TF_NormalizedPQ) {
        p_agm_generator->gamutMapParams.cs_opts_src.pq_norm = (double)tmParams->inputNormalizationFactor;
    }
    else {
        p_agm_generator->gamutMapParams.cs_opts_src.pq_norm = MAX_LUMINANCE;
    }


    p_agm_generator->gamutMapParams.cs_opts_dst.color_space_type = ECST_CUSTOM;
    p_agm_generator->gamutMapParams.cs_opts_dst.rgbw_xy[0] =
        dstMetaData->redPrimaryX / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_dst.rgbw_xy[1] =
        dstMetaData->redPrimaryY / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_dst.rgbw_xy[2] =
        dstMetaData->greenPrimaryX / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_dst.rgbw_xy[3] =
        dstMetaData->greenPrimaryY / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_dst.rgbw_xy[4] =
        dstMetaData->bluePrimaryX / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_dst.rgbw_xy[5] =
        dstMetaData->bluePrimaryY / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_dst.rgbw_xy[6] =
        dstMetaData->whitePointX / 50000.0;
    p_agm_generator->gamutMapParams.cs_opts_dst.rgbw_xy[7] =
        dstMetaData->whitePointY / 50000.0;

    p_agm_generator->gamutMapParams.cs_opts_dst.gamma_type          = outGamma;
    p_agm_generator->gamutMapParams.cs_opts_dst.mode                = 0;
    p_agm_generator->gamutMapParams.cs_opts_dst.luminance_limits[0] = 0;
    p_agm_generator->gamutMapParams.cs_opts_dst.luminance_limits[1] =
        (double)dstMetaData->maxMasteringLuminance;

    if (tmParams->lutOutTf == TMG_TF_NormalizedPQ) {
        p_agm_generator->gamutMapParams.cs_opts_dst.pq_norm = (double)tmParams->inputNormalizationFactor;
    }
    else {
        p_agm_generator->gamutMapParams.cs_opts_dst.pq_norm = MAX_LUMINANCE;
    }

    // Correct Luminance Bounds if Neccessary
    if (p_agm_generator->gamutMapParams.cs_opts_src.luminance_limits[0] > p_agm_generator->gamutMapParams.cs_opts_dst.luminance_limits[0]) {
        p_agm_generator->gamutMapParams.cs_opts_src.luminance_limits[0] = p_agm_generator->gamutMapParams.cs_opts_dst.luminance_limits[0];
        p_agm_generator->gamutMapParams.update_msk |= GM_UPDATE_SRC;
    }
    if (p_agm_generator->gamutMapParams.cs_opts_src.luminance_limits[1] < p_agm_generator->gamutMapParams.cs_opts_dst.luminance_limits[1]) {
        p_agm_generator->gamutMapParams.cs_opts_src.luminance_limits[1] = p_agm_generator->gamutMapParams.cs_opts_dst.luminance_limits[1];
        p_agm_generator->gamutMapParams.update_msk |= GM_UPDATE_SRC;
    }

exit: 
    return ret;
}

void AGMGenerator_GMSetDefault(struct AGMGenerator* p_agm_generator)
{
    gm_api_set_def(&p_agm_generator->gamutMapParams);
}

enum TMGReturnCode AGMGenerator_GMGenerateMap(struct AGMGenerator* p_agm_generator)
{
    int retcode = gm_api_gen_map(&p_agm_generator->gamutMapParams, &p_agm_generator->agmParams);

    return retcode ? TMG_RET_ERROR_GMLIB : TMG_RET_OK;
}

enum TMGReturnCode AGMGenerator_GMGenerate3DLUT(struct AGMGenerator* p_agm_generator)
{
    int retcode = gm_api_gen_3dlut(&p_agm_generator->gamutMapParams, &p_agm_generator->agmParams);

    return retcode ? TMG_RET_ERROR_GMLIB : TMG_RET_OK;
}

void AGMGenerator_GMCtor(struct AGMGenerator* p_agm_generator)
{
    gm_ctor(&p_agm_generator->agmParams, p_agm_generator->allocFunc, p_agm_generator->freeFunc, p_agm_generator->memoryContext);
}

void AGMGenerator_Exit(struct AGMGenerator* p_agm_generator)
{
    gm_dtor(&p_agm_generator->agmParams);
}