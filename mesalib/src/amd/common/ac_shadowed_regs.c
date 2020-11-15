/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/* These tables define the set of ranges of registers we shadow when
 * mid command buffer preemption is enabled.
 */

#include "ac_shadowed_regs.h"

#include "ac_debug.h"
#include "sid.h"
#include "util/macros.h"
#include "util/u_debug.h"

#include <stdio.h>

static const struct ac_reg_range Gfx9UserConfigShadowRange[] = {
   {
      R_0300FC_CP_STRMOUT_CNTL,
      4,
   },
   {
      R_0301EC_CP_COHER_START_DELAY,
      4,
   },
   {
      R_030904_VGT_GSVS_RING_SIZE,
      R_030908_VGT_PRIMITIVE_TYPE - R_030904_VGT_GSVS_RING_SIZE + 4,
   },
   {
      R_030920_VGT_MAX_VTX_INDX,
      R_03092C_VGT_MULTI_PRIM_IB_RESET_EN - R_030920_VGT_MAX_VTX_INDX + 4,
   },
   {
      R_030934_VGT_NUM_INSTANCES,
      R_030944_VGT_TF_MEMORY_BASE_HI - R_030934_VGT_NUM_INSTANCES + 4,
   },
   {
      R_030960_IA_MULTI_VGT_PARAM,
      4,
   },
   {
      R_030968_VGT_INSTANCE_BASE_ID,
      4,
   },
   {
      R_030E00_TA_CS_BC_BASE_ADDR,
      R_030E04_TA_CS_BC_BASE_ADDR_HI - R_030E00_TA_CS_BC_BASE_ADDR + 4,
   },
   {
      R_030AD4_PA_STATE_STEREO_X,
      4,
   },
};

static const struct ac_reg_range Gfx9ContextShadowRange[] = {
   {
      R_028000_DB_RENDER_CONTROL,
      R_028084_TA_BC_BASE_ADDR_HI - R_028000_DB_RENDER_CONTROL + 4,
   },
   {
      R_0281E8_COHER_DEST_BASE_HI_0,
      R_02835C_PA_SC_TILE_STEERING_OVERRIDE - R_0281E8_COHER_DEST_BASE_HI_0 + 4,
   },
   {
      R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
      4,
   },
   {
      R_028414_CB_BLEND_RED,
      R_028618_PA_CL_UCP_5_W - R_028414_CB_BLEND_RED + 4,
   },
   {
      R_028644_SPI_PS_INPUT_CNTL_0,
      R_028714_SPI_SHADER_COL_FORMAT - R_028644_SPI_PS_INPUT_CNTL_0 + 4,
   },
   {
      R_028754_SX_PS_DOWNCONVERT,
      R_0287BC_CB_MRT7_EPITCH - R_028754_SX_PS_DOWNCONVERT + 4,
   },
   {
      R_028800_DB_DEPTH_CONTROL,
      R_028820_PA_CL_NANINF_CNTL - R_028800_DB_DEPTH_CONTROL + 4,
   },
   {
      R_02882C_PA_SU_PRIM_FILTER_CNTL,
      R_028840_PA_STEREO_CNTL - R_02882C_PA_SU_PRIM_FILTER_CNTL + 4,
   },
   {
      R_028A00_PA_SU_POINT_SIZE,
      R_028A0C_PA_SC_LINE_STIPPLE - R_028A00_PA_SU_POINT_SIZE + 4,
   },
   {
      R_028A18_VGT_HOS_MAX_TESS_LEVEL,
      R_028A1C_VGT_HOS_MIN_TESS_LEVEL - R_028A18_VGT_HOS_MAX_TESS_LEVEL + 4,
   },
   {
      R_028A40_VGT_GS_MODE,
      R_028A6C_VGT_GS_OUT_PRIM_TYPE - R_028A40_VGT_GS_MODE + 4,
   },
   {
      R_028A84_VGT_PRIMITIVEID_EN,
      4,
   },
   {
      R_028A8C_VGT_PRIMITIVEID_RESET,
      4,
   },
   {
      R_028A94_VGT_GS_MAX_PRIMS_PER_SUBGROUP,
      R_028AD4_VGT_STRMOUT_VTX_STRIDE_0 - R_028A94_VGT_GS_MAX_PRIMS_PER_SUBGROUP + 4,
   },
   {
      R_028AE0_VGT_STRMOUT_BUFFER_SIZE_1,
      R_028AE4_VGT_STRMOUT_VTX_STRIDE_1 - R_028AE0_VGT_STRMOUT_BUFFER_SIZE_1 + 4,
   },
   {
      R_028AF0_VGT_STRMOUT_BUFFER_SIZE_2,
      R_028AF4_VGT_STRMOUT_VTX_STRIDE_2 - R_028AF0_VGT_STRMOUT_BUFFER_SIZE_2 + 4,
   },
   {
      R_028B00_VGT_STRMOUT_BUFFER_SIZE_3,
      R_028B04_VGT_STRMOUT_VTX_STRIDE_3 - R_028B00_VGT_STRMOUT_BUFFER_SIZE_3 + 4,
   },
   {
      R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET,
      R_028B30_VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE - R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET + 4,
   },
   {
      R_028B38_VGT_GS_MAX_VERT_OUT,
      R_028B98_VGT_STRMOUT_BUFFER_CONFIG - R_028B38_VGT_GS_MAX_VERT_OUT + 4,
   },
   {
      R_028BD4_PA_SC_CENTROID_PRIORITY_0,
      R_028E3C_CB_COLOR7_DCC_BASE_EXT - R_028BD4_PA_SC_CENTROID_PRIORITY_0 + 4,
   },
};

static const struct ac_reg_range Gfx9ShShadowRange[] = {
   {
      R_00B020_SPI_SHADER_PGM_LO_PS,
      R_00B0AC_SPI_SHADER_USER_DATA_PS_31 - R_00B020_SPI_SHADER_PGM_LO_PS + 4,
   },
   {
      R_00B11C_SPI_SHADER_LATE_ALLOC_VS,
      R_00B1AC_SPI_SHADER_USER_DATA_VS_31 - R_00B11C_SPI_SHADER_LATE_ALLOC_VS + 4,
   },
   {
      R_00B204_SPI_SHADER_PGM_RSRC4_GS,
      R_00B214_SPI_SHADER_PGM_HI_ES - R_00B204_SPI_SHADER_PGM_RSRC4_GS + 4,
   },
   {
      R_00B220_SPI_SHADER_PGM_LO_GS,
      R_00B22C_SPI_SHADER_PGM_RSRC2_GS - R_00B220_SPI_SHADER_PGM_LO_GS + 4,
   },
   {
      R_00B330_SPI_SHADER_USER_DATA_ES_0,
      R_00B3AC_SPI_SHADER_USER_DATA_ES_31 - R_00B330_SPI_SHADER_USER_DATA_ES_0 + 4,
   },
   {
      R_00B404_SPI_SHADER_PGM_RSRC4_HS,
      R_00B414_SPI_SHADER_PGM_HI_LS - R_00B404_SPI_SHADER_PGM_RSRC4_HS + 4,
   },
   {
      R_00B420_SPI_SHADER_PGM_LO_HS,
      R_00B4AC_SPI_SHADER_USER_DATA_LS_31 - R_00B420_SPI_SHADER_PGM_LO_HS + 4,
   },
};

static const struct ac_reg_range Gfx9CsShShadowRange[] = {
   {
      R_00B810_COMPUTE_START_X,
      R_00B824_COMPUTE_NUM_THREAD_Z - R_00B810_COMPUTE_START_X + 4,
   },
   {
      R_00B82C_COMPUTE_PERFCOUNT_ENABLE,
      R_00B834_COMPUTE_PGM_HI - R_00B82C_COMPUTE_PERFCOUNT_ENABLE + 4,
   },
   {
      R_00B848_COMPUTE_PGM_RSRC1,
      R_00B84C_COMPUTE_PGM_RSRC2 - R_00B848_COMPUTE_PGM_RSRC1 + 4,
   },
   {
      R_00B854_COMPUTE_RESOURCE_LIMITS,
      4,
   },
   {
      R_00B860_COMPUTE_TMPRING_SIZE,
      4,
   },
   {
      R_00B878_COMPUTE_THREAD_TRACE_ENABLE,
      4,
   },
   {
      R_00B900_COMPUTE_USER_DATA_0,
      R_00B93C_COMPUTE_USER_DATA_15 - R_00B900_COMPUTE_USER_DATA_0 + 4,
   },
};

static const struct ac_reg_range Gfx9ShShadowRangeRaven2[] = {
   {
      R_00B018_SPI_SHADER_PGM_CHKSUM_PS,
      4,
   },
   {
      R_00B020_SPI_SHADER_PGM_LO_PS,
      R_00B0AC_SPI_SHADER_USER_DATA_PS_31 - R_00B020_SPI_SHADER_PGM_LO_PS + 4,
   },
   {
      R_00B114_SPI_SHADER_PGM_CHKSUM_VS,
      4,
   },
   {
      R_00B11C_SPI_SHADER_LATE_ALLOC_VS,
      R_00B1AC_SPI_SHADER_USER_DATA_VS_31 - R_00B11C_SPI_SHADER_LATE_ALLOC_VS + 4,
   },
   {
      R_00B200_SPI_SHADER_PGM_CHKSUM_GS,
      R_00B214_SPI_SHADER_PGM_HI_ES - R_00B200_SPI_SHADER_PGM_CHKSUM_GS + 4,
   },
   {
      R_00B220_SPI_SHADER_PGM_LO_GS,
      R_00B22C_SPI_SHADER_PGM_RSRC2_GS - R_00B220_SPI_SHADER_PGM_LO_GS + 4,
   },
   {
      R_00B330_SPI_SHADER_USER_DATA_ES_0,
      R_00B3AC_SPI_SHADER_USER_DATA_ES_31 - R_00B330_SPI_SHADER_USER_DATA_ES_0 + 4,
   },
   {
      R_00B400_SPI_SHADER_PGM_CHKSUM_HS,
      R_00B414_SPI_SHADER_PGM_HI_LS - R_00B400_SPI_SHADER_PGM_CHKSUM_HS + 4,
   },
   {
      R_00B420_SPI_SHADER_PGM_LO_HS,
      R_00B4AC_SPI_SHADER_USER_DATA_LS_31 - R_00B420_SPI_SHADER_PGM_LO_HS + 4,
   },
};

static const struct ac_reg_range Gfx9CsShShadowRangeRaven2[] = {
   {
      R_00B810_COMPUTE_START_X,
      R_00B824_COMPUTE_NUM_THREAD_Z - R_00B810_COMPUTE_START_X + 4,
   },
   {
      R_00B82C_COMPUTE_PERFCOUNT_ENABLE,
      R_00B834_COMPUTE_PGM_HI - R_00B82C_COMPUTE_PERFCOUNT_ENABLE + 4,
   },
   {
      R_00B848_COMPUTE_PGM_RSRC1,
      R_00B84C_COMPUTE_PGM_RSRC2 - R_00B848_COMPUTE_PGM_RSRC1 + 4,
   },
   {
      R_00B854_COMPUTE_RESOURCE_LIMITS,
      4,
   },
   {
      R_00B860_COMPUTE_TMPRING_SIZE,
      4,
   },
   {
      R_00B878_COMPUTE_THREAD_TRACE_ENABLE,
      4,
   },
   {
      R_00B894_COMPUTE_SHADER_CHKSUM,
      4,
   },
   {
      R_00B900_COMPUTE_USER_DATA_0,
      R_00B93C_COMPUTE_USER_DATA_15 - R_00B900_COMPUTE_USER_DATA_0 + 4,
   },
};

static const struct ac_reg_range Nv10ContextShadowRange[] = {
   {
      R_028000_DB_RENDER_CONTROL,
      R_028084_TA_BC_BASE_ADDR_HI - R_028000_DB_RENDER_CONTROL + 4,
   },
   {
      R_0281E8_COHER_DEST_BASE_HI_0,
      R_02835C_PA_SC_TILE_STEERING_OVERRIDE - R_0281E8_COHER_DEST_BASE_HI_0 + 4,
   },
   {
      R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
      R_028618_PA_CL_UCP_5_W - R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX + 4,
   },
   {
      R_028644_SPI_PS_INPUT_CNTL_0,
      R_028714_SPI_SHADER_COL_FORMAT - R_028644_SPI_PS_INPUT_CNTL_0 + 4,
   },
   {
      R_028754_SX_PS_DOWNCONVERT,
      R_02879C_CB_BLEND7_CONTROL - R_028754_SX_PS_DOWNCONVERT + 4,
   },
   {
      R_0287FC_GE_MAX_OUTPUT_PER_SUBGROUP,
      R_028820_PA_CL_NANINF_CNTL - R_0287FC_GE_MAX_OUTPUT_PER_SUBGROUP + 4,
   },
   {
      R_02882C_PA_SU_PRIM_FILTER_CNTL,
      R_028844_PA_STATE_STEREO_X - R_02882C_PA_SU_PRIM_FILTER_CNTL + 4,
   },
   {
      R_028A00_PA_SU_POINT_SIZE,
      R_028A0C_PA_SC_LINE_STIPPLE - R_028A00_PA_SU_POINT_SIZE + 4,
   },
   {
      R_028A18_VGT_HOS_MAX_TESS_LEVEL,
      R_028A1C_VGT_HOS_MIN_TESS_LEVEL - R_028A18_VGT_HOS_MAX_TESS_LEVEL + 4,
   },
   {
      R_028A40_VGT_GS_MODE,
      R_028A6C_VGT_GS_OUT_PRIM_TYPE - R_028A40_VGT_GS_MODE + 4,
   },
   {
      R_028A84_VGT_PRIMITIVEID_EN,
      4,
   },
   {
      R_028A8C_VGT_PRIMITIVEID_RESET,
      4,
   },
   {
      R_028A98_VGT_DRAW_PAYLOAD_CNTL,
      R_028B98_VGT_STRMOUT_BUFFER_CONFIG - R_028A98_VGT_DRAW_PAYLOAD_CNTL + 4,
   },
   {
      R_028BD4_PA_SC_CENTROID_PRIORITY_0,
      R_028EFC_CB_COLOR7_ATTRIB3 - R_028BD4_PA_SC_CENTROID_PRIORITY_0 + 4,
   },
};

static const struct ac_reg_range Nv10UserConfigShadowRange[] = {
   {
      R_0300FC_CP_STRMOUT_CNTL,
      4,
   },
   {
      R_0301EC_CP_COHER_START_DELAY,
      4,
   },
   {
      R_030904_VGT_GSVS_RING_SIZE_UMD,
      R_030908_VGT_PRIMITIVE_TYPE - R_030904_VGT_GSVS_RING_SIZE_UMD + 4,
   },
   {
      R_030964_GE_MAX_VTX_INDX,
      4,
   },
   {
      R_030924_GE_MIN_VTX_INDX,
      R_03092C_GE_MULTI_PRIM_IB_RESET_EN - R_030924_GE_MIN_VTX_INDX + 4,
   },
   {
      R_030934_VGT_NUM_INSTANCES,
      R_030940_VGT_TF_MEMORY_BASE_UMD - R_030934_VGT_NUM_INSTANCES + 4,
   },
   {
      R_03097C_GE_STEREO_CNTL,
      R_030984_VGT_TF_MEMORY_BASE_HI_UMD - R_03097C_GE_STEREO_CNTL + 4,
   },
   {
      R_03096C_GE_CNTL,
      4,
   },
   {
      R_030968_VGT_INSTANCE_BASE_ID,
      4,
   },
   {
      R_030988_GE_USER_VGPR_EN,
      4,
   },
   {
      R_030E00_TA_CS_BC_BASE_ADDR,
      R_030E04_TA_CS_BC_BASE_ADDR_HI - R_030E00_TA_CS_BC_BASE_ADDR + 4,
   },
};

static const struct ac_reg_range Gfx10ShShadowRange[] = {
   {
      R_00B018_SPI_SHADER_PGM_CHKSUM_PS,
      4,
   },
   {
      R_00B020_SPI_SHADER_PGM_LO_PS,
      R_00B0AC_SPI_SHADER_USER_DATA_PS_31 - R_00B020_SPI_SHADER_PGM_LO_PS + 4,
   },
   {
      R_00B0C8_SPI_SHADER_USER_ACCUM_PS_0,
      R_00B0D4_SPI_SHADER_USER_ACCUM_PS_3 - R_00B0C8_SPI_SHADER_USER_ACCUM_PS_0 + 4,
   },
   {
      R_00B114_SPI_SHADER_PGM_CHKSUM_VS,
      4,
   },
   {
      R_00B11C_SPI_SHADER_LATE_ALLOC_VS,
      R_00B1AC_SPI_SHADER_USER_DATA_VS_31 - R_00B11C_SPI_SHADER_LATE_ALLOC_VS + 4,
   },
   {
      R_00B1C8_SPI_SHADER_USER_ACCUM_VS_0,
      R_00B1D4_SPI_SHADER_USER_ACCUM_VS_3 - R_00B1C8_SPI_SHADER_USER_ACCUM_VS_0 + 4,
   },
   {
      R_00B320_SPI_SHADER_PGM_LO_ES,
      R_00B324_SPI_SHADER_PGM_HI_ES - R_00B320_SPI_SHADER_PGM_LO_ES + 4,
   },
   {
      R_00B520_SPI_SHADER_PGM_LO_LS,
      R_00B524_SPI_SHADER_PGM_HI_LS - R_00B520_SPI_SHADER_PGM_LO_LS + 4,
   },
   {
      R_00B200_SPI_SHADER_PGM_CHKSUM_GS,
      4,
   },
   {
      R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
      R_00B2AC_SPI_SHADER_USER_DATA_GS_31 - R_00B21C_SPI_SHADER_PGM_RSRC3_GS + 4,
   },
   {
      R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS,
      R_00B20C_SPI_SHADER_USER_DATA_ADDR_HI_GS - R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS + 4,
   },
   {
      R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS,
      R_00B40C_SPI_SHADER_USER_DATA_ADDR_HI_HS - R_00B408_SPI_SHADER_USER_DATA_ADDR_LO_HS + 4,
   },
   {
      R_00B2C8_SPI_SHADER_USER_ACCUM_ESGS_0,
      R_00B2D4_SPI_SHADER_USER_ACCUM_ESGS_3 - R_00B2C8_SPI_SHADER_USER_ACCUM_ESGS_0 + 4,
   },
   {
      R_00B400_SPI_SHADER_PGM_CHKSUM_HS,
      4,
   },
   {
      R_00B41C_SPI_SHADER_PGM_RSRC3_HS,
      R_00B4AC_SPI_SHADER_USER_DATA_HS_31 - R_00B41C_SPI_SHADER_PGM_RSRC3_HS + 4,
   },
   {
      R_00B4C8_SPI_SHADER_USER_ACCUM_LSHS_0,
      R_00B4D4_SPI_SHADER_USER_ACCUM_LSHS_3 - R_00B4C8_SPI_SHADER_USER_ACCUM_LSHS_0 + 4,
   },
   {
      R_00B0C0_SPI_SHADER_REQ_CTRL_PS,
      4,
   },
   {
      R_00B1C0_SPI_SHADER_REQ_CTRL_VS,
      4,
   },
};

static const struct ac_reg_range Gfx10CsShShadowRange[] = {
   {
      R_00B810_COMPUTE_START_X,
      R_00B824_COMPUTE_NUM_THREAD_Z - R_00B810_COMPUTE_START_X + 4,
   },
   {
      R_00B82C_COMPUTE_PERFCOUNT_ENABLE,
      R_00B834_COMPUTE_PGM_HI - R_00B82C_COMPUTE_PERFCOUNT_ENABLE + 4,
   },
   {
      R_00B848_COMPUTE_PGM_RSRC1,
      R_00B84C_COMPUTE_PGM_RSRC2 - R_00B848_COMPUTE_PGM_RSRC1 + 4,
   },
   {
      R_00B854_COMPUTE_RESOURCE_LIMITS,
      4,
   },
   {
      R_00B860_COMPUTE_TMPRING_SIZE,
      4,
   },
   {
      R_00B878_COMPUTE_THREAD_TRACE_ENABLE,
      4,
   },
   {
      R_00B890_COMPUTE_USER_ACCUM_0,
      R_00B8A0_COMPUTE_PGM_RSRC3 - R_00B890_COMPUTE_USER_ACCUM_0 + 4,
   },
   {
      R_00B8A8_COMPUTE_SHADER_CHKSUM,
      4,
   },
   {
      R_00B900_COMPUTE_USER_DATA_0,
      R_00B93C_COMPUTE_USER_DATA_15 - R_00B900_COMPUTE_USER_DATA_0 + 4,
   },
   {
      R_00B9F4_COMPUTE_DISPATCH_TUNNEL,
      4,
   },
};

static const struct ac_reg_range Navi10NonShadowedRanges[] = {
   /* These are not defined in Mesa. */
   /*{
      VGT_DMA_PRIMITIVE_TYPE,
      VGT_DMA_LS_HS_CONFIG - VGT_DMA_PRIMITIVE_TYPE + 4,
   },*/
   /* VGT_INDEX_TYPE and VGT_DMA_INDEX_TYPE are a special case and neither of these should be
      shadowed. */
   {
      R_028A7C_VGT_DMA_INDEX_TYPE,
      4,
   },
   {
      R_03090C_VGT_INDEX_TYPE,
      R_03091C_VGT_STRMOUT_BUFFER_FILLED_SIZE_3 - R_03090C_VGT_INDEX_TYPE + 4,
   },
   {
      R_028A88_VGT_DMA_NUM_INSTANCES,
      4,
   },
   {
      R_00B118_SPI_SHADER_PGM_RSRC3_VS,
      4,
   },
   {
      R_00B01C_SPI_SHADER_PGM_RSRC3_PS,
      4,
   },
   {
      R_00B004_SPI_SHADER_PGM_RSRC4_PS,
      4,
   },
   {
      R_00B104_SPI_SHADER_PGM_RSRC4_VS,
      4,
   },
   {
      R_00B404_SPI_SHADER_PGM_RSRC4_HS,
      4,
   },
   {
      R_00B204_SPI_SHADER_PGM_RSRC4_GS,
      4,
   },
   {
      R_00B858_COMPUTE_DESTINATION_EN_SE0,
      R_00B85C_COMPUTE_DESTINATION_EN_SE1 - R_00B858_COMPUTE_DESTINATION_EN_SE0 + 4,
   },
   {
      R_00B864_COMPUTE_DESTINATION_EN_SE2,
      R_00B868_COMPUTE_DESTINATION_EN_SE3 - R_00B864_COMPUTE_DESTINATION_EN_SE2 + 4,
   },
   {
      R_030800_GRBM_GFX_INDEX,
      4,
   },
   {
      R_031100_SPI_CONFIG_CNTL_REMAP,
      4,
   },
   /* SQ thread trace registers are always not shadowed. */
   {
      R_008D00_SQ_THREAD_TRACE_BUF0_BASE,
      R_008D38_SQ_THREAD_TRACE_HP3D_MARKER_CNTR - R_008D00_SQ_THREAD_TRACE_BUF0_BASE + 4,
   },
   {
      R_030D00_SQ_THREAD_TRACE_USERDATA_0,
      R_030D1C_SQ_THREAD_TRACE_USERDATA_7 - R_030D00_SQ_THREAD_TRACE_USERDATA_0 + 4,
   },
   /* Perf counter registers are always not shadowed. Most of them are in the perf
    * register space but some legacy registers are still outside of it. The SPM
    * registers are in the perf range as well.
    */
   {
      SI_UCONFIG_PERF_REG_OFFSET,
      SI_UCONFIG_PERF_REG_SPACE_SIZE,
   },
   /* These are not defined in Mesa. */
   /*{
      ATC_PERFCOUNTER0_CFG,
      ATC_PERFCOUNTER_HI - ATC_PERFCOUNTER0_CFG + 4,
   },
   {
      RPB_PERFCOUNTER_LO,
      RPB_PERFCOUNTER_RSLT_CNTL - RPB_PERFCOUNTER_LO + 4,
   },
   {
      SDMA0_PERFCOUNTER0_SELECT,
      SDMA0_PERFCOUNTER1_HI - SDMA0_PERFCOUNTER0_SELECT + 4,
   },
   {
      SDMA1_PERFCOUNTER0_SELECT,
      SDMA1_PERFCOUNTER1_HI - SDMA1_PERFCOUNTER0_SELECT + 4,
   },
   {
      GCEA_PERFCOUNTER_LO,
      GCEA_PERFCOUNTER_RSLT_CNTL - GCEA_PERFCOUNTER_LO + 4,
   },
   {
      GUS_PERFCOUNTER_LO,
      GUS_PERFCOUNTER_RSLT_CNTL - GUS_PERFCOUNTER_LO + 4,
   },*/
};

static const struct ac_reg_range Gfx103ContextShadowRange[] = {
   {
      R_028000_DB_RENDER_CONTROL,
      R_028084_TA_BC_BASE_ADDR_HI - R_028000_DB_RENDER_CONTROL + 4,
   },
   {
      R_0281E8_COHER_DEST_BASE_HI_0,
      R_02835C_PA_SC_TILE_STEERING_OVERRIDE - R_0281E8_COHER_DEST_BASE_HI_0 + 4,
   },
   {
      R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
      R_028618_PA_CL_UCP_5_W - R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX + 4,
   },
   {
      R_028644_SPI_PS_INPUT_CNTL_0,
      R_028714_SPI_SHADER_COL_FORMAT - R_028644_SPI_PS_INPUT_CNTL_0 + 4,
   },
   {
      R_028750_SX_PS_DOWNCONVERT_CONTROL,
      R_02879C_CB_BLEND7_CONTROL - R_028750_SX_PS_DOWNCONVERT_CONTROL + 4,
   },
   {
      R_0287FC_GE_MAX_OUTPUT_PER_SUBGROUP,
      R_028820_PA_CL_NANINF_CNTL - R_0287FC_GE_MAX_OUTPUT_PER_SUBGROUP + 4,
   },
   {
      R_02882C_PA_SU_PRIM_FILTER_CNTL,
      R_028848_PA_CL_VRS_CNTL - R_02882C_PA_SU_PRIM_FILTER_CNTL + 4,
   },
   {
      R_028A00_PA_SU_POINT_SIZE,
      R_028A0C_PA_SC_LINE_STIPPLE - R_028A00_PA_SU_POINT_SIZE + 4,
   },
   {
      R_028A18_VGT_HOS_MAX_TESS_LEVEL,
      R_028A1C_VGT_HOS_MIN_TESS_LEVEL - R_028A18_VGT_HOS_MAX_TESS_LEVEL + 4,
   },
   {
      R_028A40_VGT_GS_MODE,
      R_028A6C_VGT_GS_OUT_PRIM_TYPE - R_028A40_VGT_GS_MODE + 4,
   },
   {
      R_028A84_VGT_PRIMITIVEID_EN,
      4,
   },
   {
      R_028A8C_VGT_PRIMITIVEID_RESET,
      4,
   },
   {
      R_028A98_VGT_DRAW_PAYLOAD_CNTL,
      R_028B98_VGT_STRMOUT_BUFFER_CONFIG - R_028A98_VGT_DRAW_PAYLOAD_CNTL + 4,
   },
   {
      R_028BD4_PA_SC_CENTROID_PRIORITY_0,
      R_028EFC_CB_COLOR7_ATTRIB3 - R_028BD4_PA_SC_CENTROID_PRIORITY_0 + 4,
   },
};

static const struct ac_reg_range Gfx103UserConfigShadowRange[] = {
   {
      R_0300FC_CP_STRMOUT_CNTL,
      4,
   },
   {
      R_0301EC_CP_COHER_START_DELAY,
      4,
   },
   {
      R_030904_VGT_GSVS_RING_SIZE_UMD,
      R_030908_VGT_PRIMITIVE_TYPE - R_030904_VGT_GSVS_RING_SIZE_UMD + 4,
   },
   {
      R_030964_GE_MAX_VTX_INDX,
      4,
   },
   {
      R_030924_GE_MIN_VTX_INDX,
      R_03092C_GE_MULTI_PRIM_IB_RESET_EN - R_030924_GE_MIN_VTX_INDX + 4,
   },
   {
      R_030934_VGT_NUM_INSTANCES,
      R_030940_VGT_TF_MEMORY_BASE_UMD - R_030934_VGT_NUM_INSTANCES + 4,
   },
   {
      R_03097C_GE_STEREO_CNTL,
      R_030984_VGT_TF_MEMORY_BASE_HI_UMD - R_03097C_GE_STEREO_CNTL + 4,
   },
   {
      R_03096C_GE_CNTL,
      4,
   },
   {
      R_030968_VGT_INSTANCE_BASE_ID,
      4,
   },
   {
      R_030E00_TA_CS_BC_BASE_ADDR,
      R_030E04_TA_CS_BC_BASE_ADDR_HI - R_030E00_TA_CS_BC_BASE_ADDR + 4,
   },
   {
      R_030988_GE_USER_VGPR_EN,
      0x03098C - R_030988_GE_USER_VGPR_EN + 4,
   },
};

static const struct ac_reg_range Gfx103NonShadowedRanges[] = {
   /* These are not defined in Mesa. */
   /*{
      VGT_DMA_PRIMITIVE_TYPE,
      VGT_DMA_LS_HS_CONFIG - VGT_DMA_PRIMITIVE_TYPE + 4,
   },*/
   /* VGT_INDEX_TYPE and VGT_DMA_INDEX_TYPE are a special case and neither of these should be
      shadowed. */
   {
      R_028A7C_VGT_DMA_INDEX_TYPE,
      4,
   },
   {
      R_03090C_VGT_INDEX_TYPE,
      R_03091C_VGT_STRMOUT_BUFFER_FILLED_SIZE_3 - R_03090C_VGT_INDEX_TYPE + 4,
   },
   {
      R_028A88_VGT_DMA_NUM_INSTANCES,
      4,
   },
   {
      R_00B118_SPI_SHADER_PGM_RSRC3_VS,
      4,
   },
   {
      R_00B01C_SPI_SHADER_PGM_RSRC3_PS,
      4,
   },
   {
      R_00B004_SPI_SHADER_PGM_RSRC4_PS,
      4,
   },
   {
      R_00B104_SPI_SHADER_PGM_RSRC4_VS,
      4,
   },
   {
      R_00B404_SPI_SHADER_PGM_RSRC4_HS,
      4,
   },
   {
      R_00B204_SPI_SHADER_PGM_RSRC4_GS,
      4,
   },
   {
      R_00B858_COMPUTE_DESTINATION_EN_SE0,
      R_00B85C_COMPUTE_DESTINATION_EN_SE1 - R_00B858_COMPUTE_DESTINATION_EN_SE0 + 4,
   },
   {
      R_00B864_COMPUTE_DESTINATION_EN_SE2,
      R_00B868_COMPUTE_DESTINATION_EN_SE3 - R_00B864_COMPUTE_DESTINATION_EN_SE2 + 4,
   },
   {
      R_030800_GRBM_GFX_INDEX,
      4,
   },
   {
      R_031100_SPI_CONFIG_CNTL_REMAP,
      4,
   },
   /* SQ thread trace registers are always not shadowed. */
   {
      R_008D00_SQ_THREAD_TRACE_BUF0_BASE,
      R_008D3C_SQ_THREAD_TRACE_STATUS2 - R_008D00_SQ_THREAD_TRACE_BUF0_BASE + 4,
   },
   {
      R_030D00_SQ_THREAD_TRACE_USERDATA_0,
      R_030D1C_SQ_THREAD_TRACE_USERDATA_7 - R_030D00_SQ_THREAD_TRACE_USERDATA_0 + 4,
   },
   /* Perf counter registers are always not shadowed. Most of them are in the perf
    * register space but some legacy registers are still outside of it. The SPM
    * registers are in the perf range as well.
    */
   {
      SI_UCONFIG_PERF_REG_OFFSET,
      SI_UCONFIG_PERF_REG_SPACE_SIZE,
   },
   /* These are not defined in Mesa. */
   /*{
      mmATC_PERFCOUNTER0_CFG,
      mmATC_PERFCOUNTER_HI - mmATC_PERFCOUNTER0_CFG + 1
   },
   {
      mmRPB_PERFCOUNTER_LO,
      mmRPB_PERFCOUNTER_RSLT_CNTL - mmRPB_PERFCOUNTER_LO + 1
   },*/
};

void ac_get_reg_ranges(enum chip_class chip_class, enum radeon_family family,
                       enum ac_reg_range_type type, unsigned *num_ranges,
                       const struct ac_reg_range **ranges)
{
#define RETURN(array)                                                                              \
   do {                                                                                            \
      *ranges = array;                                                                             \
      *num_ranges = ARRAY_SIZE(array);                                                             \
   } while (0)

   *num_ranges = 0;
   *ranges = NULL;

   switch (type) {
   case SI_REG_RANGE_UCONFIG:
      if (chip_class == GFX10_3)
         RETURN(Gfx103UserConfigShadowRange);
      else if (chip_class == GFX10)
         RETURN(Nv10UserConfigShadowRange);
      else if (chip_class == GFX9)
         RETURN(Gfx9UserConfigShadowRange);
      break;
   case SI_REG_RANGE_CONTEXT:
      if (chip_class == GFX10_3)
         RETURN(Gfx103ContextShadowRange);
      else if (chip_class == GFX10)
         RETURN(Nv10ContextShadowRange);
      else if (chip_class == GFX9)
         RETURN(Gfx9ContextShadowRange);
      break;
   case SI_REG_RANGE_SH:
      if (chip_class == GFX10_3 || chip_class == GFX10)
         RETURN(Gfx10ShShadowRange);
      else if (family == CHIP_RAVEN2 || family == CHIP_RENOIR)
         RETURN(Gfx9ShShadowRangeRaven2);
      else if (chip_class == GFX9)
         RETURN(Gfx9ShShadowRange);
      break;
   case SI_REG_RANGE_CS_SH:
      if (chip_class == GFX10_3 || chip_class == GFX10)
         RETURN(Gfx10CsShShadowRange);
      else if (family == CHIP_RAVEN2 || family == CHIP_RENOIR)
         RETURN(Gfx9CsShShadowRangeRaven2);
      else if (chip_class == GFX9)
         RETURN(Gfx9CsShShadowRange);
      break;
   case SI_REG_RANGE_NON_SHADOWED:
      if (chip_class == GFX10_3)
         RETURN(Gfx103NonShadowedRanges);
      else if (chip_class == GFX10)
         RETURN(Navi10NonShadowedRanges);
      else
         assert(0);
      break;
   default:
      break;
   }
}

/**
 * Emulate CLEAR_STATE.
 */
static void gfx9_emulate_clear_state(struct radeon_cmdbuf *cs,
                                     set_context_reg_seq_array_fn set_context_reg_seq_array)
{
   static const uint32_t DbRenderControlGfx9[] = {
      0x0,        // DB_RENDER_CONTROL
      0x0,        // DB_COUNT_CONTROL
      0x0,        // DB_DEPTH_VIEW
      0x0,        // DB_RENDER_OVERRIDE
      0x0,        // DB_RENDER_OVERRIDE2
      0x0,        // DB_HTILE_DATA_BASE
      0x0,        // DB_HTILE_DATA_BASE_HI
      0x0,        // DB_DEPTH_SIZE
      0x0,        // DB_DEPTH_BOUNDS_MIN
      0x0,        // DB_DEPTH_BOUNDS_MAX
      0x0,        // DB_STENCIL_CLEAR
      0x0,        // DB_DEPTH_CLEAR
      0x0,        // PA_SC_SCREEN_SCISSOR_TL
      0x40004000, // PA_SC_SCREEN_SCISSOR_BR
      0x0,        // DB_Z_INFO
      0x0,        // DB_STENCIL_INFO
      0x0,        // DB_Z_READ_BASE
      0x0,        // DB_Z_READ_BASE_HI
      0x0,        // DB_STENCIL_READ_BASE
      0x0,        // DB_STENCIL_READ_BASE_HI
      0x0,        // DB_Z_WRITE_BASE
      0x0,        // DB_Z_WRITE_BASE_HI
      0x0,        // DB_STENCIL_WRITE_BASE
      0x0,        // DB_STENCIL_WRITE_BASE_HI
      0x0,        // DB_DFSM_CONTROL
      0x0,        //
      0x0,        // DB_Z_INFO2
      0x0,        // DB_STENCIL_INFO2
      0x0,        //
      0x0,        //
      0x0,        //
      0x0,        //
      0x0,        // TA_BC_BASE_ADDR
      0x0         // TA_BC_BASE_ADDR_HI
   };
   static const uint32_t CoherDestBaseHi0Gfx9[] = {
      0x0,        // COHER_DEST_BASE_HI_0
      0x0,        // COHER_DEST_BASE_HI_1
      0x0,        // COHER_DEST_BASE_HI_2
      0x0,        // COHER_DEST_BASE_HI_3
      0x0,        // COHER_DEST_BASE_2
      0x0,        // COHER_DEST_BASE_3
      0x0,        // PA_SC_WINDOW_OFFSET
      0x80000000, // PA_SC_WINDOW_SCISSOR_TL
      0x40004000, // PA_SC_WINDOW_SCISSOR_BR
      0xffff,     // PA_SC_CLIPRECT_RULE
      0x0,        // PA_SC_CLIPRECT_0_TL
      0x40004000, // PA_SC_CLIPRECT_0_BR
      0x0,        // PA_SC_CLIPRECT_1_TL
      0x40004000, // PA_SC_CLIPRECT_1_BR
      0x0,        // PA_SC_CLIPRECT_2_TL
      0x40004000, // PA_SC_CLIPRECT_2_BR
      0x0,        // PA_SC_CLIPRECT_3_TL
      0x40004000, // PA_SC_CLIPRECT_3_BR
      0xaa99aaaa, // PA_SC_EDGERULE
      0x0,        // PA_SU_HARDWARE_SCREEN_OFFSET
      0xffffffff, // CB_TARGET_MASK
      0xffffffff, // CB_SHADER_MASK
      0x80000000, // PA_SC_GENERIC_SCISSOR_TL
      0x40004000, // PA_SC_GENERIC_SCISSOR_BR
      0x0,        // COHER_DEST_BASE_0
      0x0,        // COHER_DEST_BASE_1
      0x80000000, // PA_SC_VPORT_SCISSOR_0_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_0_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_1_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_1_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_2_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_2_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_3_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_3_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_4_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_4_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_5_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_5_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_6_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_6_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_7_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_7_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_8_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_8_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_9_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_9_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_10_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_10_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_11_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_11_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_12_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_12_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_13_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_13_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_14_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_14_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_15_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_15_BR
      0x0,        // PA_SC_VPORT_ZMIN_0
      0x3f800000, // PA_SC_VPORT_ZMAX_0
      0x0,        // PA_SC_VPORT_ZMIN_1
      0x3f800000, // PA_SC_VPORT_ZMAX_1
      0x0,        // PA_SC_VPORT_ZMIN_2
      0x3f800000, // PA_SC_VPORT_ZMAX_2
      0x0,        // PA_SC_VPORT_ZMIN_3
      0x3f800000, // PA_SC_VPORT_ZMAX_3
      0x0,        // PA_SC_VPORT_ZMIN_4
      0x3f800000, // PA_SC_VPORT_ZMAX_4
      0x0,        // PA_SC_VPORT_ZMIN_5
      0x3f800000, // PA_SC_VPORT_ZMAX_5
      0x0,        // PA_SC_VPORT_ZMIN_6
      0x3f800000, // PA_SC_VPORT_ZMAX_6
      0x0,        // PA_SC_VPORT_ZMIN_7
      0x3f800000, // PA_SC_VPORT_ZMAX_7
      0x0,        // PA_SC_VPORT_ZMIN_8
      0x3f800000, // PA_SC_VPORT_ZMAX_8
      0x0,        // PA_SC_VPORT_ZMIN_9
      0x3f800000, // PA_SC_VPORT_ZMAX_9
      0x0,        // PA_SC_VPORT_ZMIN_10
      0x3f800000, // PA_SC_VPORT_ZMAX_10
      0x0,        // PA_SC_VPORT_ZMIN_11
      0x3f800000, // PA_SC_VPORT_ZMAX_11
      0x0,        // PA_SC_VPORT_ZMIN_12
      0x3f800000, // PA_SC_VPORT_ZMAX_12
      0x0,        // PA_SC_VPORT_ZMIN_13
      0x3f800000, // PA_SC_VPORT_ZMAX_13
      0x0,        // PA_SC_VPORT_ZMIN_14
      0x3f800000, // PA_SC_VPORT_ZMAX_14
      0x0,        // PA_SC_VPORT_ZMIN_15
      0x3f800000, // PA_SC_VPORT_ZMAX_15
      0x0,        // PA_SC_RASTER_CONFIG
      0x0,        // PA_SC_RASTER_CONFIG_1
      0x0,        //
      0x0         // PA_SC_TILE_STEERING_OVERRIDE
   };
   static const uint32_t VgtMultiPrimIbResetIndxGfx9[] = {
      0x0 // VGT_MULTI_PRIM_IB_RESET_INDX
   };
   static const uint32_t CbBlendRedGfx9[] = {
      0x0,       // CB_BLEND_RED
      0x0,       // CB_BLEND_GREEN
      0x0,       // CB_BLEND_BLUE
      0x0,       // CB_BLEND_ALPHA
      0x0,       // CB_DCC_CONTROL
      0x0,       //
      0x0,       // DB_STENCIL_CONTROL
      0x1000000, // DB_STENCILREFMASK
      0x1000000, // DB_STENCILREFMASK_BF
      0x0,       //
      0x0,       // PA_CL_VPORT_XSCALE
      0x0,       // PA_CL_VPORT_XOFFSET
      0x0,       // PA_CL_VPORT_YSCALE
      0x0,       // PA_CL_VPORT_YOFFSET
      0x0,       // PA_CL_VPORT_ZSCALE
      0x0,       // PA_CL_VPORT_ZOFFSET
      0x0,       // PA_CL_VPORT_XSCALE_1
      0x0,       // PA_CL_VPORT_XOFFSET_1
      0x0,       // PA_CL_VPORT_YSCALE_1
      0x0,       // PA_CL_VPORT_YOFFSET_1
      0x0,       // PA_CL_VPORT_ZSCALE_1
      0x0,       // PA_CL_VPORT_ZOFFSET_1
      0x0,       // PA_CL_VPORT_XSCALE_2
      0x0,       // PA_CL_VPORT_XOFFSET_2
      0x0,       // PA_CL_VPORT_YSCALE_2
      0x0,       // PA_CL_VPORT_YOFFSET_2
      0x0,       // PA_CL_VPORT_ZSCALE_2
      0x0,       // PA_CL_VPORT_ZOFFSET_2
      0x0,       // PA_CL_VPORT_XSCALE_3
      0x0,       // PA_CL_VPORT_XOFFSET_3
      0x0,       // PA_CL_VPORT_YSCALE_3
      0x0,       // PA_CL_VPORT_YOFFSET_3
      0x0,       // PA_CL_VPORT_ZSCALE_3
      0x0,       // PA_CL_VPORT_ZOFFSET_3
      0x0,       // PA_CL_VPORT_XSCALE_4
      0x0,       // PA_CL_VPORT_XOFFSET_4
      0x0,       // PA_CL_VPORT_YSCALE_4
      0x0,       // PA_CL_VPORT_YOFFSET_4
      0x0,       // PA_CL_VPORT_ZSCALE_4
      0x0,       // PA_CL_VPORT_ZOFFSET_4
      0x0,       // PA_CL_VPORT_XSCALE_5
      0x0,       // PA_CL_VPORT_XOFFSET_5
      0x0,       // PA_CL_VPORT_YSCALE_5
      0x0,       // PA_CL_VPORT_YOFFSET_5
      0x0,       // PA_CL_VPORT_ZSCALE_5
      0x0,       // PA_CL_VPORT_ZOFFSET_5
      0x0,       // PA_CL_VPORT_XSCALE_6
      0x0,       // PA_CL_VPORT_XOFFSET_6
      0x0,       // PA_CL_VPORT_YSCALE_6
      0x0,       // PA_CL_VPORT_YOFFSET_6
      0x0,       // PA_CL_VPORT_ZSCALE_6
      0x0,       // PA_CL_VPORT_ZOFFSET_6
      0x0,       // PA_CL_VPORT_XSCALE_7
      0x0,       // PA_CL_VPORT_XOFFSET_7
      0x0,       // PA_CL_VPORT_YSCALE_7
      0x0,       // PA_CL_VPORT_YOFFSET_7
      0x0,       // PA_CL_VPORT_ZSCALE_7
      0x0,       // PA_CL_VPORT_ZOFFSET_7
      0x0,       // PA_CL_VPORT_XSCALE_8
      0x0,       // PA_CL_VPORT_XOFFSET_8
      0x0,       // PA_CL_VPORT_YSCALE_8
      0x0,       // PA_CL_VPORT_YOFFSET_8
      0x0,       // PA_CL_VPORT_ZSCALE_8
      0x0,       // PA_CL_VPORT_ZOFFSET_8
      0x0,       // PA_CL_VPORT_XSCALE_9
      0x0,       // PA_CL_VPORT_XOFFSET_9
      0x0,       // PA_CL_VPORT_YSCALE_9
      0x0,       // PA_CL_VPORT_YOFFSET_9
      0x0,       // PA_CL_VPORT_ZSCALE_9
      0x0,       // PA_CL_VPORT_ZOFFSET_9
      0x0,       // PA_CL_VPORT_XSCALE_10
      0x0,       // PA_CL_VPORT_XOFFSET_10
      0x0,       // PA_CL_VPORT_YSCALE_10
      0x0,       // PA_CL_VPORT_YOFFSET_10
      0x0,       // PA_CL_VPORT_ZSCALE_10
      0x0,       // PA_CL_VPORT_ZOFFSET_10
      0x0,       // PA_CL_VPORT_XSCALE_11
      0x0,       // PA_CL_VPORT_XOFFSET_11
      0x0,       // PA_CL_VPORT_YSCALE_11
      0x0,       // PA_CL_VPORT_YOFFSET_11
      0x0,       // PA_CL_VPORT_ZSCALE_11
      0x0,       // PA_CL_VPORT_ZOFFSET_11
      0x0,       // PA_CL_VPORT_XSCALE_12
      0x0,       // PA_CL_VPORT_XOFFSET_12
      0x0,       // PA_CL_VPORT_YSCALE_12
      0x0,       // PA_CL_VPORT_YOFFSET_12
      0x0,       // PA_CL_VPORT_ZSCALE_12
      0x0,       // PA_CL_VPORT_ZOFFSET_12
      0x0,       // PA_CL_VPORT_XSCALE_13
      0x0,       // PA_CL_VPORT_XOFFSET_13
      0x0,       // PA_CL_VPORT_YSCALE_13
      0x0,       // PA_CL_VPORT_YOFFSET_13
      0x0,       // PA_CL_VPORT_ZSCALE_13
      0x0,       // PA_CL_VPORT_ZOFFSET_13
      0x0,       // PA_CL_VPORT_XSCALE_14
      0x0,       // PA_CL_VPORT_XOFFSET_14
      0x0,       // PA_CL_VPORT_YSCALE_14
      0x0,       // PA_CL_VPORT_YOFFSET_14
      0x0,       // PA_CL_VPORT_ZSCALE_14
      0x0,       // PA_CL_VPORT_ZOFFSET_14
      0x0,       // PA_CL_VPORT_XSCALE_15
      0x0,       // PA_CL_VPORT_XOFFSET_15
      0x0,       // PA_CL_VPORT_YSCALE_15
      0x0,       // PA_CL_VPORT_YOFFSET_15
      0x0,       // PA_CL_VPORT_ZSCALE_15
      0x0,       // PA_CL_VPORT_ZOFFSET_15
      0x0,       // PA_CL_UCP_0_X
      0x0,       // PA_CL_UCP_0_Y
      0x0,       // PA_CL_UCP_0_Z
      0x0,       // PA_CL_UCP_0_W
      0x0,       // PA_CL_UCP_1_X
      0x0,       // PA_CL_UCP_1_Y
      0x0,       // PA_CL_UCP_1_Z
      0x0,       // PA_CL_UCP_1_W
      0x0,       // PA_CL_UCP_2_X
      0x0,       // PA_CL_UCP_2_Y
      0x0,       // PA_CL_UCP_2_Z
      0x0,       // PA_CL_UCP_2_W
      0x0,       // PA_CL_UCP_3_X
      0x0,       // PA_CL_UCP_3_Y
      0x0,       // PA_CL_UCP_3_Z
      0x0,       // PA_CL_UCP_3_W
      0x0,       // PA_CL_UCP_4_X
      0x0,       // PA_CL_UCP_4_Y
      0x0,       // PA_CL_UCP_4_Z
      0x0,       // PA_CL_UCP_4_W
      0x0,       // PA_CL_UCP_5_X
      0x0,       // PA_CL_UCP_5_Y
      0x0,       // PA_CL_UCP_5_Z
      0x0        // PA_CL_UCP_5_W
   };
   static const uint32_t SpiPsInputCntl0Gfx9[] = {
      0x0, // SPI_PS_INPUT_CNTL_0
      0x0, // SPI_PS_INPUT_CNTL_1
      0x0, // SPI_PS_INPUT_CNTL_2
      0x0, // SPI_PS_INPUT_CNTL_3
      0x0, // SPI_PS_INPUT_CNTL_4
      0x0, // SPI_PS_INPUT_CNTL_5
      0x0, // SPI_PS_INPUT_CNTL_6
      0x0, // SPI_PS_INPUT_CNTL_7
      0x0, // SPI_PS_INPUT_CNTL_8
      0x0, // SPI_PS_INPUT_CNTL_9
      0x0, // SPI_PS_INPUT_CNTL_10
      0x0, // SPI_PS_INPUT_CNTL_11
      0x0, // SPI_PS_INPUT_CNTL_12
      0x0, // SPI_PS_INPUT_CNTL_13
      0x0, // SPI_PS_INPUT_CNTL_14
      0x0, // SPI_PS_INPUT_CNTL_15
      0x0, // SPI_PS_INPUT_CNTL_16
      0x0, // SPI_PS_INPUT_CNTL_17
      0x0, // SPI_PS_INPUT_CNTL_18
      0x0, // SPI_PS_INPUT_CNTL_19
      0x0, // SPI_PS_INPUT_CNTL_20
      0x0, // SPI_PS_INPUT_CNTL_21
      0x0, // SPI_PS_INPUT_CNTL_22
      0x0, // SPI_PS_INPUT_CNTL_23
      0x0, // SPI_PS_INPUT_CNTL_24
      0x0, // SPI_PS_INPUT_CNTL_25
      0x0, // SPI_PS_INPUT_CNTL_26
      0x0, // SPI_PS_INPUT_CNTL_27
      0x0, // SPI_PS_INPUT_CNTL_28
      0x0, // SPI_PS_INPUT_CNTL_29
      0x0, // SPI_PS_INPUT_CNTL_30
      0x0, // SPI_PS_INPUT_CNTL_31
      0x0, // SPI_VS_OUT_CONFIG
      0x0, //
      0x0, // SPI_PS_INPUT_ENA
      0x0, // SPI_PS_INPUT_ADDR
      0x0, // SPI_INTERP_CONTROL_0
      0x2, // SPI_PS_IN_CONTROL
      0x0, //
      0x0, // SPI_BARYC_CNTL
      0x0, //
      0x0, // SPI_TMPRING_SIZE
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, // SPI_SHADER_POS_FORMAT
      0x0, // SPI_SHADER_Z_FORMAT
      0x0  // SPI_SHADER_COL_FORMAT
   };
   static const uint32_t SxPsDownconvertGfx9[] = {
      0x0, // SX_PS_DOWNCONVERT
      0x0, // SX_BLEND_OPT_EPSILON
      0x0, // SX_BLEND_OPT_CONTROL
      0x0, // SX_MRT0_BLEND_OPT
      0x0, // SX_MRT1_BLEND_OPT
      0x0, // SX_MRT2_BLEND_OPT
      0x0, // SX_MRT3_BLEND_OPT
      0x0, // SX_MRT4_BLEND_OPT
      0x0, // SX_MRT5_BLEND_OPT
      0x0, // SX_MRT6_BLEND_OPT
      0x0, // SX_MRT7_BLEND_OPT
      0x0, // CB_BLEND0_CONTROL
      0x0, // CB_BLEND1_CONTROL
      0x0, // CB_BLEND2_CONTROL
      0x0, // CB_BLEND3_CONTROL
      0x0, // CB_BLEND4_CONTROL
      0x0, // CB_BLEND5_CONTROL
      0x0, // CB_BLEND6_CONTROL
      0x0, // CB_BLEND7_CONTROL
      0x0, // CB_MRT0_EPITCH
      0x0, // CB_MRT1_EPITCH
      0x0, // CB_MRT2_EPITCH
      0x0, // CB_MRT3_EPITCH
      0x0, // CB_MRT4_EPITCH
      0x0, // CB_MRT5_EPITCH
      0x0, // CB_MRT6_EPITCH
      0x0  // CB_MRT7_EPITCH
   };
   static const uint32_t DbDepthControlGfx9[] = {
      0x0,     // DB_DEPTH_CONTROL
      0x0,     // DB_EQAA
      0x0,     // CB_COLOR_CONTROL
      0x0,     // DB_SHADER_CONTROL
      0x90000, // PA_CL_CLIP_CNTL
      0x4,     // PA_SU_SC_MODE_CNTL
      0x0,     // PA_CL_VTE_CNTL
      0x0,     // PA_CL_VS_OUT_CNTL
      0x0      // PA_CL_NANINF_CNTL
   };
   static const uint32_t PaSuPrimFilterCntlGfx9[] = {
      0x0, // PA_SU_PRIM_FILTER_CNTL
      0x0, // PA_SU_SMALL_PRIM_FILTER_CNTL
      0x0, // PA_CL_OBJPRIM_ID_CNTL
      0x0, // PA_CL_NGG_CNTL
      0x0, // PA_SU_OVER_RASTERIZATION_CNTL
      0x0  // PA_STEREO_CNTL
   };
   static const uint32_t PaSuPointSizeGfx9[] = {
      0x0, // PA_SU_POINT_SIZE
      0x0, // PA_SU_POINT_MINMAX
      0x0, // PA_SU_LINE_CNTL
      0x0  // PA_SC_LINE_STIPPLE
   };
   static const uint32_t VgtHosMaxTessLevelGfx9[] = {
      0x0, // VGT_HOS_MAX_TESS_LEVEL
      0x0  // VGT_HOS_MIN_TESS_LEVEL
   };
   static const uint32_t VgtGsModeGfx9[] = {
      0x0,   // VGT_GS_MODE
      0x0,   // VGT_GS_ONCHIP_CNTL
      0x0,   // PA_SC_MODE_CNTL_0
      0x0,   // PA_SC_MODE_CNTL_1
      0x0,   // VGT_ENHANCE
      0x100, // VGT_GS_PER_ES
      0x80,  // VGT_ES_PER_GS
      0x2,   // VGT_GS_PER_VS
      0x0,   // VGT_GSVS_RING_OFFSET_1
      0x0,   // VGT_GSVS_RING_OFFSET_2
      0x0,   // VGT_GSVS_RING_OFFSET_3
      0x0    // VGT_GS_OUT_PRIM_TYPE
   };
   static const uint32_t VgtPrimitiveidEnGfx9[] = {
      0x0 // VGT_PRIMITIVEID_EN
   };
   static const uint32_t VgtPrimitiveidResetGfx9[] = {
      0x0 // VGT_PRIMITIVEID_RESET
   };
   static const uint32_t VgtGsMaxPrimsPerSubgroupGfx9[] = {
      0x0, // VGT_GS_MAX_PRIMS_PER_SUBGROUP
      0x0, // VGT_DRAW_PAYLOAD_CNTL
      0x0, //
      0x0, // VGT_INSTANCE_STEP_RATE_0
      0x0, // VGT_INSTANCE_STEP_RATE_1
      0x0, //
      0x0, // VGT_ESGS_RING_ITEMSIZE
      0x0, // VGT_GSVS_RING_ITEMSIZE
      0x0, // VGT_REUSE_OFF
      0x0, // VGT_VTX_CNT_EN
      0x0, // DB_HTILE_SURFACE
      0x0, // DB_SRESULTS_COMPARE_STATE0
      0x0, // DB_SRESULTS_COMPARE_STATE1
      0x0, // DB_PRELOAD_CONTROL
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_SIZE_0
      0x0  // VGT_STRMOUT_VTX_STRIDE_0
   };
   static const uint32_t VgtStrmoutBufferSize1Gfx9[] = {
      0x0, // VGT_STRMOUT_BUFFER_SIZE_1
      0x0  // VGT_STRMOUT_VTX_STRIDE_1
   };
   static const uint32_t VgtStrmoutBufferSize2Gfx9[] = {
      0x0, // VGT_STRMOUT_BUFFER_SIZE_2
      0x0  // VGT_STRMOUT_VTX_STRIDE_2
   };
   static const uint32_t VgtStrmoutBufferSize3Gfx9[] = {
      0x0, // VGT_STRMOUT_BUFFER_SIZE_3
      0x0  // VGT_STRMOUT_VTX_STRIDE_3
   };
   static const uint32_t VgtStrmoutDrawOpaqueOffsetGfx9[] = {
      0x0, // VGT_STRMOUT_DRAW_OPAQUE_OFFSET
      0x0, // VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE
      0x0  // VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE
   };
   static const uint32_t VgtGsMaxVertOutGfx9[] = {
      0x0, // VGT_GS_MAX_VERT_OUT
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, // VGT_TESS_DISTRIBUTION
      0x0, // VGT_SHADER_STAGES_EN
      0x0, // VGT_LS_HS_CONFIG
      0x0, // VGT_GS_VERT_ITEMSIZE
      0x0, // VGT_GS_VERT_ITEMSIZE_1
      0x0, // VGT_GS_VERT_ITEMSIZE_2
      0x0, // VGT_GS_VERT_ITEMSIZE_3
      0x0, // VGT_TF_PARAM
      0x0, // DB_ALPHA_TO_MASK
      0x0, // VGT_DISPATCH_DRAW_INDEX
      0x0, // PA_SU_POLY_OFFSET_DB_FMT_CNTL
      0x0, // PA_SU_POLY_OFFSET_CLAMP
      0x0, // PA_SU_POLY_OFFSET_FRONT_SCALE
      0x0, // PA_SU_POLY_OFFSET_FRONT_OFFSET
      0x0, // PA_SU_POLY_OFFSET_BACK_SCALE
      0x0, // PA_SU_POLY_OFFSET_BACK_OFFSET
      0x0, // VGT_GS_INSTANCE_CNT
      0x0, // VGT_STRMOUT_CONFIG
      0x0  // VGT_STRMOUT_BUFFER_CONFIG
   };
   static const uint32_t PaScCentroidPriority0Gfx9[] = {
      0x0,        // PA_SC_CENTROID_PRIORITY_0
      0x0,        // PA_SC_CENTROID_PRIORITY_1
      0x1000,     // PA_SC_LINE_CNTL
      0x0,        // PA_SC_AA_CONFIG
      0x5,        // PA_SU_VTX_CNTL
      0x3f800000, // PA_CL_GB_VERT_CLIP_ADJ
      0x3f800000, // PA_CL_GB_VERT_DISC_ADJ
      0x3f800000, // PA_CL_GB_HORZ_CLIP_ADJ
      0x3f800000, // PA_CL_GB_HORZ_DISC_ADJ
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_3
      0xffffffff, // PA_SC_AA_MASK_X0Y0_X1Y0
      0xffffffff, // PA_SC_AA_MASK_X0Y1_X1Y1
      0x0,        // PA_SC_SHADER_CONTROL
      0x3,        // PA_SC_BINNER_CNTL_0
      0x0,        // PA_SC_BINNER_CNTL_1
      0x100000,   // PA_SC_CONSERVATIVE_RASTERIZATION_CNTL
      0x0,        // PA_SC_NGG_MODE_CNTL
      0x0,        //
      0x1e,       // VGT_VERTEX_REUSE_BLOCK_CNTL
      0x20,       // VGT_OUT_DEALLOC_CNTL
      0x0,        // CB_COLOR0_BASE
      0x0,        // CB_COLOR0_BASE_EXT
      0x0,        // CB_COLOR0_ATTRIB2
      0x0,        // CB_COLOR0_VIEW
      0x0,        // CB_COLOR0_INFO
      0x0,        // CB_COLOR0_ATTRIB
      0x0,        // CB_COLOR0_DCC_CONTROL
      0x0,        // CB_COLOR0_CMASK
      0x0,        // CB_COLOR0_CMASK_BASE_EXT
      0x0,        // CB_COLOR0_FMASK
      0x0,        // CB_COLOR0_FMASK_BASE_EXT
      0x0,        // CB_COLOR0_CLEAR_WORD0
      0x0,        // CB_COLOR0_CLEAR_WORD1
      0x0,        // CB_COLOR0_DCC_BASE
      0x0,        // CB_COLOR0_DCC_BASE_EXT
      0x0,        // CB_COLOR1_BASE
      0x0,        // CB_COLOR1_BASE_EXT
      0x0,        // CB_COLOR1_ATTRIB2
      0x0,        // CB_COLOR1_VIEW
      0x0,        // CB_COLOR1_INFO
      0x0,        // CB_COLOR1_ATTRIB
      0x0,        // CB_COLOR1_DCC_CONTROL
      0x0,        // CB_COLOR1_CMASK
      0x0,        // CB_COLOR1_CMASK_BASE_EXT
      0x0,        // CB_COLOR1_FMASK
      0x0,        // CB_COLOR1_FMASK_BASE_EXT
      0x0,        // CB_COLOR1_CLEAR_WORD0
      0x0,        // CB_COLOR1_CLEAR_WORD1
      0x0,        // CB_COLOR1_DCC_BASE
      0x0,        // CB_COLOR1_DCC_BASE_EXT
      0x0,        // CB_COLOR2_BASE
      0x0,        // CB_COLOR2_BASE_EXT
      0x0,        // CB_COLOR2_ATTRIB2
      0x0,        // CB_COLOR2_VIEW
      0x0,        // CB_COLOR2_INFO
      0x0,        // CB_COLOR2_ATTRIB
      0x0,        // CB_COLOR2_DCC_CONTROL
      0x0,        // CB_COLOR2_CMASK
      0x0,        // CB_COLOR2_CMASK_BASE_EXT
      0x0,        // CB_COLOR2_FMASK
      0x0,        // CB_COLOR2_FMASK_BASE_EXT
      0x0,        // CB_COLOR2_CLEAR_WORD0
      0x0,        // CB_COLOR2_CLEAR_WORD1
      0x0,        // CB_COLOR2_DCC_BASE
      0x0,        // CB_COLOR2_DCC_BASE_EXT
      0x0,        // CB_COLOR3_BASE
      0x0,        // CB_COLOR3_BASE_EXT
      0x0,        // CB_COLOR3_ATTRIB2
      0x0,        // CB_COLOR3_VIEW
      0x0,        // CB_COLOR3_INFO
      0x0,        // CB_COLOR3_ATTRIB
      0x0,        // CB_COLOR3_DCC_CONTROL
      0x0,        // CB_COLOR3_CMASK
      0x0,        // CB_COLOR3_CMASK_BASE_EXT
      0x0,        // CB_COLOR3_FMASK
      0x0,        // CB_COLOR3_FMASK_BASE_EXT
      0x0,        // CB_COLOR3_CLEAR_WORD0
      0x0,        // CB_COLOR3_CLEAR_WORD1
      0x0,        // CB_COLOR3_DCC_BASE
      0x0,        // CB_COLOR3_DCC_BASE_EXT
      0x0,        // CB_COLOR4_BASE
      0x0,        // CB_COLOR4_BASE_EXT
      0x0,        // CB_COLOR4_ATTRIB2
      0x0,        // CB_COLOR4_VIEW
      0x0,        // CB_COLOR4_INFO
      0x0,        // CB_COLOR4_ATTRIB
      0x0,        // CB_COLOR4_DCC_CONTROL
      0x0,        // CB_COLOR4_CMASK
      0x0,        // CB_COLOR4_CMASK_BASE_EXT
      0x0,        // CB_COLOR4_FMASK
      0x0,        // CB_COLOR4_FMASK_BASE_EXT
      0x0,        // CB_COLOR4_CLEAR_WORD0
      0x0,        // CB_COLOR4_CLEAR_WORD1
      0x0,        // CB_COLOR4_DCC_BASE
      0x0,        // CB_COLOR4_DCC_BASE_EXT
      0x0,        // CB_COLOR5_BASE
      0x0,        // CB_COLOR5_BASE_EXT
      0x0,        // CB_COLOR5_ATTRIB2
      0x0,        // CB_COLOR5_VIEW
      0x0,        // CB_COLOR5_INFO
      0x0,        // CB_COLOR5_ATTRIB
      0x0,        // CB_COLOR5_DCC_CONTROL
      0x0,        // CB_COLOR5_CMASK
      0x0,        // CB_COLOR5_CMASK_BASE_EXT
      0x0,        // CB_COLOR5_FMASK
      0x0,        // CB_COLOR5_FMASK_BASE_EXT
      0x0,        // CB_COLOR5_CLEAR_WORD0
      0x0,        // CB_COLOR5_CLEAR_WORD1
      0x0,        // CB_COLOR5_DCC_BASE
      0x0,        // CB_COLOR5_DCC_BASE_EXT
      0x0,        // CB_COLOR6_BASE
      0x0,        // CB_COLOR6_BASE_EXT
      0x0,        // CB_COLOR6_ATTRIB2
      0x0,        // CB_COLOR6_VIEW
      0x0,        // CB_COLOR6_INFO
      0x0,        // CB_COLOR6_ATTRIB
      0x0,        // CB_COLOR6_DCC_CONTROL
      0x0,        // CB_COLOR6_CMASK
      0x0,        // CB_COLOR6_CMASK_BASE_EXT
      0x0,        // CB_COLOR6_FMASK
      0x0,        // CB_COLOR6_FMASK_BASE_EXT
      0x0,        // CB_COLOR6_CLEAR_WORD0
      0x0,        // CB_COLOR6_CLEAR_WORD1
      0x0,        // CB_COLOR6_DCC_BASE
      0x0,        // CB_COLOR6_DCC_BASE_EXT
      0x0,        // CB_COLOR7_BASE
      0x0,        // CB_COLOR7_BASE_EXT
      0x0,        // CB_COLOR7_ATTRIB2
      0x0,        // CB_COLOR7_VIEW
      0x0,        // CB_COLOR7_INFO
      0x0,        // CB_COLOR7_ATTRIB
      0x0,        // CB_COLOR7_DCC_CONTROL
      0x0,        // CB_COLOR7_CMASK
      0x0,        // CB_COLOR7_CMASK_BASE_EXT
      0x0,        // CB_COLOR7_FMASK
      0x0,        // CB_COLOR7_FMASK_BASE_EXT
      0x0,        // CB_COLOR7_CLEAR_WORD0
      0x0,        // CB_COLOR7_CLEAR_WORD1
      0x0,        // CB_COLOR7_DCC_BASE
      0x0         // CB_COLOR7_DCC_BASE_EXT
   };

#define SET(array) ARRAY_SIZE(array), array

   set_context_reg_seq_array(cs, R_028000_DB_RENDER_CONTROL, SET(DbRenderControlGfx9));
   set_context_reg_seq_array(cs, R_0281E8_COHER_DEST_BASE_HI_0, SET(CoherDestBaseHi0Gfx9));
   set_context_reg_seq_array(cs, R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
                             SET(VgtMultiPrimIbResetIndxGfx9));
   set_context_reg_seq_array(cs, R_028414_CB_BLEND_RED, SET(CbBlendRedGfx9));
   set_context_reg_seq_array(cs, R_028644_SPI_PS_INPUT_CNTL_0, SET(SpiPsInputCntl0Gfx9));
   set_context_reg_seq_array(cs, R_028754_SX_PS_DOWNCONVERT, SET(SxPsDownconvertGfx9));
   set_context_reg_seq_array(cs, R_028800_DB_DEPTH_CONTROL, SET(DbDepthControlGfx9));
   set_context_reg_seq_array(cs, R_02882C_PA_SU_PRIM_FILTER_CNTL, SET(PaSuPrimFilterCntlGfx9));
   set_context_reg_seq_array(cs, R_028A00_PA_SU_POINT_SIZE, SET(PaSuPointSizeGfx9));
   set_context_reg_seq_array(cs, R_028A18_VGT_HOS_MAX_TESS_LEVEL, SET(VgtHosMaxTessLevelGfx9));
   set_context_reg_seq_array(cs, R_028A40_VGT_GS_MODE, SET(VgtGsModeGfx9));
   set_context_reg_seq_array(cs, R_028A84_VGT_PRIMITIVEID_EN, SET(VgtPrimitiveidEnGfx9));
   set_context_reg_seq_array(cs, R_028A8C_VGT_PRIMITIVEID_RESET, SET(VgtPrimitiveidResetGfx9));
   set_context_reg_seq_array(cs, R_028A94_VGT_GS_MAX_PRIMS_PER_SUBGROUP,
                             SET(VgtGsMaxPrimsPerSubgroupGfx9));
   set_context_reg_seq_array(cs, R_028AE0_VGT_STRMOUT_BUFFER_SIZE_1,
                             SET(VgtStrmoutBufferSize1Gfx9));
   set_context_reg_seq_array(cs, R_028AF0_VGT_STRMOUT_BUFFER_SIZE_2,
                             SET(VgtStrmoutBufferSize2Gfx9));
   set_context_reg_seq_array(cs, R_028B00_VGT_STRMOUT_BUFFER_SIZE_3,
                             SET(VgtStrmoutBufferSize3Gfx9));
   set_context_reg_seq_array(cs, R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET,
                             SET(VgtStrmoutDrawOpaqueOffsetGfx9));
   set_context_reg_seq_array(cs, R_028B38_VGT_GS_MAX_VERT_OUT, SET(VgtGsMaxVertOutGfx9));
   set_context_reg_seq_array(cs, R_028BD4_PA_SC_CENTROID_PRIORITY_0,
                             SET(PaScCentroidPriority0Gfx9));
}

/**
 * Emulate CLEAR_STATE. Additionally, initialize num_reg_pairs registers specified
 * via reg_offsets and reg_values.
 */
static void gfx10_emulate_clear_state(struct radeon_cmdbuf *cs, unsigned num_reg_pairs,
                                      unsigned *reg_offsets, uint32_t *reg_values,
                                      set_context_reg_seq_array_fn set_context_reg_seq_array)
{
   static const uint32_t DbRenderControlNv10[] = {
      0x0,        // DB_RENDER_CONTROL
      0x0,        // DB_COUNT_CONTROL
      0x0,        // DB_DEPTH_VIEW
      0x0,        // DB_RENDER_OVERRIDE
      0x0,        // DB_RENDER_OVERRIDE2
      0x0,        // DB_HTILE_DATA_BASE
      0x0,        //
      0x0,        // DB_DEPTH_SIZE_XY
      0x0,        // DB_DEPTH_BOUNDS_MIN
      0x0,        // DB_DEPTH_BOUNDS_MAX
      0x0,        // DB_STENCIL_CLEAR
      0x0,        // DB_DEPTH_CLEAR
      0x0,        // PA_SC_SCREEN_SCISSOR_TL
      0x40004000, // PA_SC_SCREEN_SCISSOR_BR
      0x0,        // DB_DFSM_CONTROL
      0x0,        // DB_RESERVED_REG_2
      0x0,        // DB_Z_INFO
      0x0,        // DB_STENCIL_INFO
      0x0,        // DB_Z_READ_BASE
      0x0,        // DB_STENCIL_READ_BASE
      0x0,        // DB_Z_WRITE_BASE
      0x0,        // DB_STENCIL_WRITE_BASE
      0x0,        //
      0x0,        //
      0x0,        //
      0x0,        //
      0x0,        // DB_Z_READ_BASE_HI
      0x0,        // DB_STENCIL_READ_BASE_HI
      0x0,        // DB_Z_WRITE_BASE_HI
      0x0,        // DB_STENCIL_WRITE_BASE_HI
      0x0,        // DB_HTILE_DATA_BASE_HI
      0x0,        // DB_RMI_L2_CACHE_CONTROL
      0x0,        // TA_BC_BASE_ADDR
      0x0         // TA_BC_BASE_ADDR_HI
   };
   static const uint32_t CoherDestBaseHi0Nv10[] = {
      0x0,        // COHER_DEST_BASE_HI_0
      0x0,        // COHER_DEST_BASE_HI_1
      0x0,        // COHER_DEST_BASE_HI_2
      0x0,        // COHER_DEST_BASE_HI_3
      0x0,        // COHER_DEST_BASE_2
      0x0,        // COHER_DEST_BASE_3
      0x0,        // PA_SC_WINDOW_OFFSET
      0x80000000, // PA_SC_WINDOW_SCISSOR_TL
      0x40004000, // PA_SC_WINDOW_SCISSOR_BR
      0xffff,     // PA_SC_CLIPRECT_RULE
      0x0,        // PA_SC_CLIPRECT_0_TL
      0x40004000, // PA_SC_CLIPRECT_0_BR
      0x0,        // PA_SC_CLIPRECT_1_TL
      0x40004000, // PA_SC_CLIPRECT_1_BR
      0x0,        // PA_SC_CLIPRECT_2_TL
      0x40004000, // PA_SC_CLIPRECT_2_BR
      0x0,        // PA_SC_CLIPRECT_3_TL
      0x40004000, // PA_SC_CLIPRECT_3_BR
      0xaa99aaaa, // PA_SC_EDGERULE
      0x0,        // PA_SU_HARDWARE_SCREEN_OFFSET
      0xffffffff, // CB_TARGET_MASK
      0xffffffff, // CB_SHADER_MASK
      0x80000000, // PA_SC_GENERIC_SCISSOR_TL
      0x40004000, // PA_SC_GENERIC_SCISSOR_BR
      0x0,        // COHER_DEST_BASE_0
      0x0,        // COHER_DEST_BASE_1
      0x80000000, // PA_SC_VPORT_SCISSOR_0_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_0_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_1_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_1_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_2_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_2_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_3_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_3_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_4_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_4_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_5_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_5_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_6_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_6_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_7_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_7_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_8_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_8_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_9_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_9_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_10_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_10_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_11_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_11_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_12_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_12_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_13_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_13_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_14_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_14_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_15_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_15_BR
      0x0,        // PA_SC_VPORT_ZMIN_0
      0x3f800000, // PA_SC_VPORT_ZMAX_0
      0x0,        // PA_SC_VPORT_ZMIN_1
      0x3f800000, // PA_SC_VPORT_ZMAX_1
      0x0,        // PA_SC_VPORT_ZMIN_2
      0x3f800000, // PA_SC_VPORT_ZMAX_2
      0x0,        // PA_SC_VPORT_ZMIN_3
      0x3f800000, // PA_SC_VPORT_ZMAX_3
      0x0,        // PA_SC_VPORT_ZMIN_4
      0x3f800000, // PA_SC_VPORT_ZMAX_4
      0x0,        // PA_SC_VPORT_ZMIN_5
      0x3f800000, // PA_SC_VPORT_ZMAX_5
      0x0,        // PA_SC_VPORT_ZMIN_6
      0x3f800000, // PA_SC_VPORT_ZMAX_6
      0x0,        // PA_SC_VPORT_ZMIN_7
      0x3f800000, // PA_SC_VPORT_ZMAX_7
      0x0,        // PA_SC_VPORT_ZMIN_8
      0x3f800000, // PA_SC_VPORT_ZMAX_8
      0x0,        // PA_SC_VPORT_ZMIN_9
      0x3f800000, // PA_SC_VPORT_ZMAX_9
      0x0,        // PA_SC_VPORT_ZMIN_10
      0x3f800000, // PA_SC_VPORT_ZMAX_10
      0x0,        // PA_SC_VPORT_ZMIN_11
      0x3f800000, // PA_SC_VPORT_ZMAX_11
      0x0,        // PA_SC_VPORT_ZMIN_12
      0x3f800000, // PA_SC_VPORT_ZMAX_12
      0x0,        // PA_SC_VPORT_ZMIN_13
      0x3f800000, // PA_SC_VPORT_ZMAX_13
      0x0,        // PA_SC_VPORT_ZMIN_14
      0x3f800000, // PA_SC_VPORT_ZMAX_14
      0x0,        // PA_SC_VPORT_ZMIN_15
      0x3f800000, // PA_SC_VPORT_ZMAX_15
      0x0,        // PA_SC_RASTER_CONFIG
      0x0,        // PA_SC_RASTER_CONFIG_1
      0x0,        //
      0x0         // PA_SC_TILE_STEERING_OVERRIDE
   };
   static const uint32_t VgtMultiPrimIbResetIndxNv10[] = {
      0x0,       // VGT_MULTI_PRIM_IB_RESET_INDX
      0x0,       // CB_RMI_GL2_CACHE_CONTROL
      0x0,       // CB_BLEND_RED
      0x0,       // CB_BLEND_GREEN
      0x0,       // CB_BLEND_BLUE
      0x0,       // CB_BLEND_ALPHA
      0x0,       // CB_DCC_CONTROL
      0x0,       // CB_COVERAGE_OUT_CONTROL
      0x0,       // DB_STENCIL_CONTROL
      0x1000000, // DB_STENCILREFMASK
      0x1000000, // DB_STENCILREFMASK_BF
      0x0,       //
      0x0,       // PA_CL_VPORT_XSCALE
      0x0,       // PA_CL_VPORT_XOFFSET
      0x0,       // PA_CL_VPORT_YSCALE
      0x0,       // PA_CL_VPORT_YOFFSET
      0x0,       // PA_CL_VPORT_ZSCALE
      0x0,       // PA_CL_VPORT_ZOFFSET
      0x0,       // PA_CL_VPORT_XSCALE_1
      0x0,       // PA_CL_VPORT_XOFFSET_1
      0x0,       // PA_CL_VPORT_YSCALE_1
      0x0,       // PA_CL_VPORT_YOFFSET_1
      0x0,       // PA_CL_VPORT_ZSCALE_1
      0x0,       // PA_CL_VPORT_ZOFFSET_1
      0x0,       // PA_CL_VPORT_XSCALE_2
      0x0,       // PA_CL_VPORT_XOFFSET_2
      0x0,       // PA_CL_VPORT_YSCALE_2
      0x0,       // PA_CL_VPORT_YOFFSET_2
      0x0,       // PA_CL_VPORT_ZSCALE_2
      0x0,       // PA_CL_VPORT_ZOFFSET_2
      0x0,       // PA_CL_VPORT_XSCALE_3
      0x0,       // PA_CL_VPORT_XOFFSET_3
      0x0,       // PA_CL_VPORT_YSCALE_3
      0x0,       // PA_CL_VPORT_YOFFSET_3
      0x0,       // PA_CL_VPORT_ZSCALE_3
      0x0,       // PA_CL_VPORT_ZOFFSET_3
      0x0,       // PA_CL_VPORT_XSCALE_4
      0x0,       // PA_CL_VPORT_XOFFSET_4
      0x0,       // PA_CL_VPORT_YSCALE_4
      0x0,       // PA_CL_VPORT_YOFFSET_4
      0x0,       // PA_CL_VPORT_ZSCALE_4
      0x0,       // PA_CL_VPORT_ZOFFSET_4
      0x0,       // PA_CL_VPORT_XSCALE_5
      0x0,       // PA_CL_VPORT_XOFFSET_5
      0x0,       // PA_CL_VPORT_YSCALE_5
      0x0,       // PA_CL_VPORT_YOFFSET_5
      0x0,       // PA_CL_VPORT_ZSCALE_5
      0x0,       // PA_CL_VPORT_ZOFFSET_5
      0x0,       // PA_CL_VPORT_XSCALE_6
      0x0,       // PA_CL_VPORT_XOFFSET_6
      0x0,       // PA_CL_VPORT_YSCALE_6
      0x0,       // PA_CL_VPORT_YOFFSET_6
      0x0,       // PA_CL_VPORT_ZSCALE_6
      0x0,       // PA_CL_VPORT_ZOFFSET_6
      0x0,       // PA_CL_VPORT_XSCALE_7
      0x0,       // PA_CL_VPORT_XOFFSET_7
      0x0,       // PA_CL_VPORT_YSCALE_7
      0x0,       // PA_CL_VPORT_YOFFSET_7
      0x0,       // PA_CL_VPORT_ZSCALE_7
      0x0,       // PA_CL_VPORT_ZOFFSET_7
      0x0,       // PA_CL_VPORT_XSCALE_8
      0x0,       // PA_CL_VPORT_XOFFSET_8
      0x0,       // PA_CL_VPORT_YSCALE_8
      0x0,       // PA_CL_VPORT_YOFFSET_8
      0x0,       // PA_CL_VPORT_ZSCALE_8
      0x0,       // PA_CL_VPORT_ZOFFSET_8
      0x0,       // PA_CL_VPORT_XSCALE_9
      0x0,       // PA_CL_VPORT_XOFFSET_9
      0x0,       // PA_CL_VPORT_YSCALE_9
      0x0,       // PA_CL_VPORT_YOFFSET_9
      0x0,       // PA_CL_VPORT_ZSCALE_9
      0x0,       // PA_CL_VPORT_ZOFFSET_9
      0x0,       // PA_CL_VPORT_XSCALE_10
      0x0,       // PA_CL_VPORT_XOFFSET_10
      0x0,       // PA_CL_VPORT_YSCALE_10
      0x0,       // PA_CL_VPORT_YOFFSET_10
      0x0,       // PA_CL_VPORT_ZSCALE_10
      0x0,       // PA_CL_VPORT_ZOFFSET_10
      0x0,       // PA_CL_VPORT_XSCALE_11
      0x0,       // PA_CL_VPORT_XOFFSET_11
      0x0,       // PA_CL_VPORT_YSCALE_11
      0x0,       // PA_CL_VPORT_YOFFSET_11
      0x0,       // PA_CL_VPORT_ZSCALE_11
      0x0,       // PA_CL_VPORT_ZOFFSET_11
      0x0,       // PA_CL_VPORT_XSCALE_12
      0x0,       // PA_CL_VPORT_XOFFSET_12
      0x0,       // PA_CL_VPORT_YSCALE_12
      0x0,       // PA_CL_VPORT_YOFFSET_12
      0x0,       // PA_CL_VPORT_ZSCALE_12
      0x0,       // PA_CL_VPORT_ZOFFSET_12
      0x0,       // PA_CL_VPORT_XSCALE_13
      0x0,       // PA_CL_VPORT_XOFFSET_13
      0x0,       // PA_CL_VPORT_YSCALE_13
      0x0,       // PA_CL_VPORT_YOFFSET_13
      0x0,       // PA_CL_VPORT_ZSCALE_13
      0x0,       // PA_CL_VPORT_ZOFFSET_13
      0x0,       // PA_CL_VPORT_XSCALE_14
      0x0,       // PA_CL_VPORT_XOFFSET_14
      0x0,       // PA_CL_VPORT_YSCALE_14
      0x0,       // PA_CL_VPORT_YOFFSET_14
      0x0,       // PA_CL_VPORT_ZSCALE_14
      0x0,       // PA_CL_VPORT_ZOFFSET_14
      0x0,       // PA_CL_VPORT_XSCALE_15
      0x0,       // PA_CL_VPORT_XOFFSET_15
      0x0,       // PA_CL_VPORT_YSCALE_15
      0x0,       // PA_CL_VPORT_YOFFSET_15
      0x0,       // PA_CL_VPORT_ZSCALE_15
      0x0,       // PA_CL_VPORT_ZOFFSET_15
      0x0,       // PA_CL_UCP_0_X
      0x0,       // PA_CL_UCP_0_Y
      0x0,       // PA_CL_UCP_0_Z
      0x0,       // PA_CL_UCP_0_W
      0x0,       // PA_CL_UCP_1_X
      0x0,       // PA_CL_UCP_1_Y
      0x0,       // PA_CL_UCP_1_Z
      0x0,       // PA_CL_UCP_1_W
      0x0,       // PA_CL_UCP_2_X
      0x0,       // PA_CL_UCP_2_Y
      0x0,       // PA_CL_UCP_2_Z
      0x0,       // PA_CL_UCP_2_W
      0x0,       // PA_CL_UCP_3_X
      0x0,       // PA_CL_UCP_3_Y
      0x0,       // PA_CL_UCP_3_Z
      0x0,       // PA_CL_UCP_3_W
      0x0,       // PA_CL_UCP_4_X
      0x0,       // PA_CL_UCP_4_Y
      0x0,       // PA_CL_UCP_4_Z
      0x0,       // PA_CL_UCP_4_W
      0x0,       // PA_CL_UCP_5_X
      0x0,       // PA_CL_UCP_5_Y
      0x0,       // PA_CL_UCP_5_Z
      0x0        // PA_CL_UCP_5_W
   };
   static const uint32_t SpiPsInputCntl0Nv10[] = {
      0x0, // SPI_PS_INPUT_CNTL_0
      0x0, // SPI_PS_INPUT_CNTL_1
      0x0, // SPI_PS_INPUT_CNTL_2
      0x0, // SPI_PS_INPUT_CNTL_3
      0x0, // SPI_PS_INPUT_CNTL_4
      0x0, // SPI_PS_INPUT_CNTL_5
      0x0, // SPI_PS_INPUT_CNTL_6
      0x0, // SPI_PS_INPUT_CNTL_7
      0x0, // SPI_PS_INPUT_CNTL_8
      0x0, // SPI_PS_INPUT_CNTL_9
      0x0, // SPI_PS_INPUT_CNTL_10
      0x0, // SPI_PS_INPUT_CNTL_11
      0x0, // SPI_PS_INPUT_CNTL_12
      0x0, // SPI_PS_INPUT_CNTL_13
      0x0, // SPI_PS_INPUT_CNTL_14
      0x0, // SPI_PS_INPUT_CNTL_15
      0x0, // SPI_PS_INPUT_CNTL_16
      0x0, // SPI_PS_INPUT_CNTL_17
      0x0, // SPI_PS_INPUT_CNTL_18
      0x0, // SPI_PS_INPUT_CNTL_19
      0x0, // SPI_PS_INPUT_CNTL_20
      0x0, // SPI_PS_INPUT_CNTL_21
      0x0, // SPI_PS_INPUT_CNTL_22
      0x0, // SPI_PS_INPUT_CNTL_23
      0x0, // SPI_PS_INPUT_CNTL_24
      0x0, // SPI_PS_INPUT_CNTL_25
      0x0, // SPI_PS_INPUT_CNTL_26
      0x0, // SPI_PS_INPUT_CNTL_27
      0x0, // SPI_PS_INPUT_CNTL_28
      0x0, // SPI_PS_INPUT_CNTL_29
      0x0, // SPI_PS_INPUT_CNTL_30
      0x0, // SPI_PS_INPUT_CNTL_31
      0x0, // SPI_VS_OUT_CONFIG
      0x0, //
      0x0, // SPI_PS_INPUT_ENA
      0x0, // SPI_PS_INPUT_ADDR
      0x0, // SPI_INTERP_CONTROL_0
      0x2, // SPI_PS_IN_CONTROL
      0x0, //
      0x0, // SPI_BARYC_CNTL
      0x0, //
      0x0, // SPI_TMPRING_SIZE
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, // SPI_SHADER_IDX_FORMAT
      0x0, // SPI_SHADER_POS_FORMAT
      0x0, // SPI_SHADER_Z_FORMAT
      0x0  // SPI_SHADER_COL_FORMAT
   };
   static const uint32_t SxPsDownconvertNv10[] = {
      0x0, // SX_PS_DOWNCONVERT
      0x0, // SX_BLEND_OPT_EPSILON
      0x0, // SX_BLEND_OPT_CONTROL
      0x0, // SX_MRT0_BLEND_OPT
      0x0, // SX_MRT1_BLEND_OPT
      0x0, // SX_MRT2_BLEND_OPT
      0x0, // SX_MRT3_BLEND_OPT
      0x0, // SX_MRT4_BLEND_OPT
      0x0, // SX_MRT5_BLEND_OPT
      0x0, // SX_MRT6_BLEND_OPT
      0x0, // SX_MRT7_BLEND_OPT
      0x0, // CB_BLEND0_CONTROL
      0x0, // CB_BLEND1_CONTROL
      0x0, // CB_BLEND2_CONTROL
      0x0, // CB_BLEND3_CONTROL
      0x0, // CB_BLEND4_CONTROL
      0x0, // CB_BLEND5_CONTROL
      0x0, // CB_BLEND6_CONTROL
      0x0  // CB_BLEND7_CONTROL
   };
   static const uint32_t GeMaxOutputPerSubgroupNv10[] = {
      0x0,     // GE_MAX_OUTPUT_PER_SUBGROUP
      0x0,     // DB_DEPTH_CONTROL
      0x0,     // DB_EQAA
      0x0,     // CB_COLOR_CONTROL
      0x0,     // DB_SHADER_CONTROL
      0x90000, // PA_CL_CLIP_CNTL
      0x4,     // PA_SU_SC_MODE_CNTL
      0x0,     // PA_CL_VTE_CNTL
      0x0,     // PA_CL_VS_OUT_CNTL
      0x0      // PA_CL_NANINF_CNTL
   };
   static const uint32_t PaSuPrimFilterCntlNv10[] = {
      0x0, // PA_SU_PRIM_FILTER_CNTL
      0x0, // PA_SU_SMALL_PRIM_FILTER_CNTL
      0x0, // PA_CL_OBJPRIM_ID_CNTL
      0x0, // PA_CL_NGG_CNTL
      0x0, // PA_SU_OVER_RASTERIZATION_CNTL
      0x0, // PA_STEREO_CNTL
      0x0  // PA_STATE_STEREO_X
   };
   static const uint32_t PaSuPointSizeNv10[] = {
      0x0, // PA_SU_POINT_SIZE
      0x0, // PA_SU_POINT_MINMAX
      0x0, // PA_SU_LINE_CNTL
      0x0  // PA_SC_LINE_STIPPLE
   };
   static const uint32_t VgtHosMaxTessLevelNv10[] = {
      0x0, // VGT_HOS_MAX_TESS_LEVEL
      0x0  // VGT_HOS_MIN_TESS_LEVEL
   };
   static const uint32_t VgtGsModeNv10[] = {
      0x0,   // VGT_GS_MODE
      0x0,   // VGT_GS_ONCHIP_CNTL
      0x0,   // PA_SC_MODE_CNTL_0
      0x0,   // PA_SC_MODE_CNTL_1
      0x0,   // VGT_ENHANCE
      0x100, // VGT_GS_PER_ES
      0x80,  // VGT_ES_PER_GS
      0x2,   // VGT_GS_PER_VS
      0x0,   // VGT_GSVS_RING_OFFSET_1
      0x0,   // VGT_GSVS_RING_OFFSET_2
      0x0,   // VGT_GSVS_RING_OFFSET_3
      0x0    // VGT_GS_OUT_PRIM_TYPE
   };
   static const uint32_t VgtPrimitiveidEnNv10[] = {
      0x0 // VGT_PRIMITIVEID_EN
   };
   static const uint32_t VgtPrimitiveidResetNv10[] = {
      0x0 // VGT_PRIMITIVEID_RESET
   };
   static const uint32_t VgtDrawPayloadCntlNv10[] = {
      0x0, // VGT_DRAW_PAYLOAD_CNTL
      0x0, //
      0x0, // VGT_INSTANCE_STEP_RATE_0
      0x0, // VGT_INSTANCE_STEP_RATE_1
      0x0, // IA_MULTI_VGT_PARAM
      0x0, // VGT_ESGS_RING_ITEMSIZE
      0x0, // VGT_GSVS_RING_ITEMSIZE
      0x0, // VGT_REUSE_OFF
      0x0, // VGT_VTX_CNT_EN
      0x0, // DB_HTILE_SURFACE
      0x0, // DB_SRESULTS_COMPARE_STATE0
      0x0, // DB_SRESULTS_COMPARE_STATE1
      0x0, // DB_PRELOAD_CONTROL
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_SIZE_0
      0x0, // VGT_STRMOUT_VTX_STRIDE_0
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_OFFSET_0
      0x0, // VGT_STRMOUT_BUFFER_SIZE_1
      0x0, // VGT_STRMOUT_VTX_STRIDE_1
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_OFFSET_1
      0x0, // VGT_STRMOUT_BUFFER_SIZE_2
      0x0, // VGT_STRMOUT_VTX_STRIDE_2
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_OFFSET_2
      0x0, // VGT_STRMOUT_BUFFER_SIZE_3
      0x0, // VGT_STRMOUT_VTX_STRIDE_3
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_OFFSET_3
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, // VGT_STRMOUT_DRAW_OPAQUE_OFFSET
      0x0, // VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE
      0x0, // VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE
      0x0, //
      0x0, // VGT_GS_MAX_VERT_OUT
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, // GE_NGG_SUBGRP_CNTL
      0x0, // VGT_TESS_DISTRIBUTION
      0x0, // VGT_SHADER_STAGES_EN
      0x0, // VGT_LS_HS_CONFIG
      0x0, // VGT_GS_VERT_ITEMSIZE
      0x0, // VGT_GS_VERT_ITEMSIZE_1
      0x0, // VGT_GS_VERT_ITEMSIZE_2
      0x0, // VGT_GS_VERT_ITEMSIZE_3
      0x0, // VGT_TF_PARAM
      0x0, // DB_ALPHA_TO_MASK
      0x0, // VGT_DISPATCH_DRAW_INDEX
      0x0, // PA_SU_POLY_OFFSET_DB_FMT_CNTL
      0x0, // PA_SU_POLY_OFFSET_CLAMP
      0x0, // PA_SU_POLY_OFFSET_FRONT_SCALE
      0x0, // PA_SU_POLY_OFFSET_FRONT_OFFSET
      0x0, // PA_SU_POLY_OFFSET_BACK_SCALE
      0x0, // PA_SU_POLY_OFFSET_BACK_OFFSET
      0x0, // VGT_GS_INSTANCE_CNT
      0x0, // VGT_STRMOUT_CONFIG
      0x0  // VGT_STRMOUT_BUFFER_CONFIG
   };
   static const uint32_t PaScCentroidPriority0Nv10[] = {
      0x0,        // PA_SC_CENTROID_PRIORITY_0
      0x0,        // PA_SC_CENTROID_PRIORITY_1
      0x1000,     // PA_SC_LINE_CNTL
      0x0,        // PA_SC_AA_CONFIG
      0x5,        // PA_SU_VTX_CNTL
      0x3f800000, // PA_CL_GB_VERT_CLIP_ADJ
      0x3f800000, // PA_CL_GB_VERT_DISC_ADJ
      0x3f800000, // PA_CL_GB_HORZ_CLIP_ADJ
      0x3f800000, // PA_CL_GB_HORZ_DISC_ADJ
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_3
      0xffffffff, // PA_SC_AA_MASK_X0Y0_X1Y0
      0xffffffff, // PA_SC_AA_MASK_X0Y1_X1Y1
      0x0,        // PA_SC_SHADER_CONTROL
      0x3,        // PA_SC_BINNER_CNTL_0
      0x0,        // PA_SC_BINNER_CNTL_1
      0x100000,   // PA_SC_CONSERVATIVE_RASTERIZATION_CNTL
      0x0,        // PA_SC_NGG_MODE_CNTL
      0x0,        //
      0x1e,       // VGT_VERTEX_REUSE_BLOCK_CNTL
      0x20,       // VGT_OUT_DEALLOC_CNTL
      0x0,        // CB_COLOR0_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR0_VIEW
      0x0,        // CB_COLOR0_INFO
      0x0,        // CB_COLOR0_ATTRIB
      0x0,        // CB_COLOR0_DCC_CONTROL
      0x0,        // CB_COLOR0_CMASK
      0x0,        //
      0x0,        // CB_COLOR0_FMASK
      0x0,        //
      0x0,        // CB_COLOR0_CLEAR_WORD0
      0x0,        // CB_COLOR0_CLEAR_WORD1
      0x0,        // CB_COLOR0_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR1_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR1_VIEW
      0x0,        // CB_COLOR1_INFO
      0x0,        // CB_COLOR1_ATTRIB
      0x0,        // CB_COLOR1_DCC_CONTROL
      0x0,        // CB_COLOR1_CMASK
      0x0,        //
      0x0,        // CB_COLOR1_FMASK
      0x0,        //
      0x0,        // CB_COLOR1_CLEAR_WORD0
      0x0,        // CB_COLOR1_CLEAR_WORD1
      0x0,        // CB_COLOR1_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR2_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR2_VIEW
      0x0,        // CB_COLOR2_INFO
      0x0,        // CB_COLOR2_ATTRIB
      0x0,        // CB_COLOR2_DCC_CONTROL
      0x0,        // CB_COLOR2_CMASK
      0x0,        //
      0x0,        // CB_COLOR2_FMASK
      0x0,        //
      0x0,        // CB_COLOR2_CLEAR_WORD0
      0x0,        // CB_COLOR2_CLEAR_WORD1
      0x0,        // CB_COLOR2_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR3_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR3_VIEW
      0x0,        // CB_COLOR3_INFO
      0x0,        // CB_COLOR3_ATTRIB
      0x0,        // CB_COLOR3_DCC_CONTROL
      0x0,        // CB_COLOR3_CMASK
      0x0,        //
      0x0,        // CB_COLOR3_FMASK
      0x0,        //
      0x0,        // CB_COLOR3_CLEAR_WORD0
      0x0,        // CB_COLOR3_CLEAR_WORD1
      0x0,        // CB_COLOR3_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR4_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR4_VIEW
      0x0,        // CB_COLOR4_INFO
      0x0,        // CB_COLOR4_ATTRIB
      0x0,        // CB_COLOR4_DCC_CONTROL
      0x0,        // CB_COLOR4_CMASK
      0x0,        //
      0x0,        // CB_COLOR4_FMASK
      0x0,        //
      0x0,        // CB_COLOR4_CLEAR_WORD0
      0x0,        // CB_COLOR4_CLEAR_WORD1
      0x0,        // CB_COLOR4_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR5_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR5_VIEW
      0x0,        // CB_COLOR5_INFO
      0x0,        // CB_COLOR5_ATTRIB
      0x0,        // CB_COLOR5_DCC_CONTROL
      0x0,        // CB_COLOR5_CMASK
      0x0,        //
      0x0,        // CB_COLOR5_FMASK
      0x0,        //
      0x0,        // CB_COLOR5_CLEAR_WORD0
      0x0,        // CB_COLOR5_CLEAR_WORD1
      0x0,        // CB_COLOR5_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR6_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR6_VIEW
      0x0,        // CB_COLOR6_INFO
      0x0,        // CB_COLOR6_ATTRIB
      0x0,        // CB_COLOR6_DCC_CONTROL
      0x0,        // CB_COLOR6_CMASK
      0x0,        //
      0x0,        // CB_COLOR6_FMASK
      0x0,        //
      0x0,        // CB_COLOR6_CLEAR_WORD0
      0x0,        // CB_COLOR6_CLEAR_WORD1
      0x0,        // CB_COLOR6_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR7_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR7_VIEW
      0x0,        // CB_COLOR7_INFO
      0x0,        // CB_COLOR7_ATTRIB
      0x0,        // CB_COLOR7_DCC_CONTROL
      0x0,        // CB_COLOR7_CMASK
      0x0,        //
      0x0,        // CB_COLOR7_FMASK
      0x0,        //
      0x0,        // CB_COLOR7_CLEAR_WORD0
      0x0,        // CB_COLOR7_CLEAR_WORD1
      0x0,        // CB_COLOR7_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR0_BASE_EXT
      0x0,        // CB_COLOR1_BASE_EXT
      0x0,        // CB_COLOR2_BASE_EXT
      0x0,        // CB_COLOR3_BASE_EXT
      0x0,        // CB_COLOR4_BASE_EXT
      0x0,        // CB_COLOR5_BASE_EXT
      0x0,        // CB_COLOR6_BASE_EXT
      0x0,        // CB_COLOR7_BASE_EXT
      0x0,        // CB_COLOR0_CMASK_BASE_EXT
      0x0,        // CB_COLOR1_CMASK_BASE_EXT
      0x0,        // CB_COLOR2_CMASK_BASE_EXT
      0x0,        // CB_COLOR3_CMASK_BASE_EXT
      0x0,        // CB_COLOR4_CMASK_BASE_EXT
      0x0,        // CB_COLOR5_CMASK_BASE_EXT
      0x0,        // CB_COLOR6_CMASK_BASE_EXT
      0x0,        // CB_COLOR7_CMASK_BASE_EXT
      0x0,        // CB_COLOR0_FMASK_BASE_EXT
      0x0,        // CB_COLOR1_FMASK_BASE_EXT
      0x0,        // CB_COLOR2_FMASK_BASE_EXT
      0x0,        // CB_COLOR3_FMASK_BASE_EXT
      0x0,        // CB_COLOR4_FMASK_BASE_EXT
      0x0,        // CB_COLOR5_FMASK_BASE_EXT
      0x0,        // CB_COLOR6_FMASK_BASE_EXT
      0x0,        // CB_COLOR7_FMASK_BASE_EXT
      0x0,        // CB_COLOR0_DCC_BASE_EXT
      0x0,        // CB_COLOR1_DCC_BASE_EXT
      0x0,        // CB_COLOR2_DCC_BASE_EXT
      0x0,        // CB_COLOR3_DCC_BASE_EXT
      0x0,        // CB_COLOR4_DCC_BASE_EXT
      0x0,        // CB_COLOR5_DCC_BASE_EXT
      0x0,        // CB_COLOR6_DCC_BASE_EXT
      0x0,        // CB_COLOR7_DCC_BASE_EXT
      0x0,        // CB_COLOR0_ATTRIB2
      0x0,        // CB_COLOR1_ATTRIB2
      0x0,        // CB_COLOR2_ATTRIB2
      0x0,        // CB_COLOR3_ATTRIB2
      0x0,        // CB_COLOR4_ATTRIB2
      0x0,        // CB_COLOR5_ATTRIB2
      0x0,        // CB_COLOR6_ATTRIB2
      0x0,        // CB_COLOR7_ATTRIB2
      0x0,        // CB_COLOR0_ATTRIB3
      0x0,        // CB_COLOR1_ATTRIB3
      0x0,        // CB_COLOR2_ATTRIB3
      0x0,        // CB_COLOR3_ATTRIB3
      0x0,        // CB_COLOR4_ATTRIB3
      0x0,        // CB_COLOR5_ATTRIB3
      0x0,        // CB_COLOR6_ATTRIB3
      0x0         // CB_COLOR7_ATTRIB3
   };

   set_context_reg_seq_array(cs, R_028000_DB_RENDER_CONTROL, SET(DbRenderControlNv10));
   set_context_reg_seq_array(cs, R_0281E8_COHER_DEST_BASE_HI_0, SET(CoherDestBaseHi0Nv10));
   set_context_reg_seq_array(cs, R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
                             SET(VgtMultiPrimIbResetIndxNv10));
   set_context_reg_seq_array(cs, R_028644_SPI_PS_INPUT_CNTL_0, SET(SpiPsInputCntl0Nv10));
   set_context_reg_seq_array(cs, R_028754_SX_PS_DOWNCONVERT, SET(SxPsDownconvertNv10));
   set_context_reg_seq_array(cs, R_0287FC_GE_MAX_OUTPUT_PER_SUBGROUP,
                             SET(GeMaxOutputPerSubgroupNv10));
   set_context_reg_seq_array(cs, R_02882C_PA_SU_PRIM_FILTER_CNTL, SET(PaSuPrimFilterCntlNv10));
   set_context_reg_seq_array(cs, R_028A00_PA_SU_POINT_SIZE, SET(PaSuPointSizeNv10));
   set_context_reg_seq_array(cs, R_028A18_VGT_HOS_MAX_TESS_LEVEL, SET(VgtHosMaxTessLevelNv10));
   set_context_reg_seq_array(cs, R_028A40_VGT_GS_MODE, SET(VgtGsModeNv10));
   set_context_reg_seq_array(cs, R_028A84_VGT_PRIMITIVEID_EN, SET(VgtPrimitiveidEnNv10));
   set_context_reg_seq_array(cs, R_028A8C_VGT_PRIMITIVEID_RESET, SET(VgtPrimitiveidResetNv10));
   set_context_reg_seq_array(cs, R_028A98_VGT_DRAW_PAYLOAD_CNTL, SET(VgtDrawPayloadCntlNv10));
   set_context_reg_seq_array(cs, R_028BD4_PA_SC_CENTROID_PRIORITY_0,
                             SET(PaScCentroidPriority0Nv10));

   for (unsigned i = 0; i < num_reg_pairs; i++)
      set_context_reg_seq_array(cs, reg_offsets[i], 1, &reg_values[i]);
}

/**
 * Emulate CLEAR_STATE. Additionally, initialize num_reg_pairs registers specified
 * via reg_offsets and reg_values.
 */
static void gfx103_emulate_clear_state(struct radeon_cmdbuf *cs, unsigned num_reg_pairs,
                                       unsigned *reg_offsets, uint32_t *reg_values,
                                       set_context_reg_seq_array_fn set_context_reg_seq_array)
{
   static const uint32_t DbRenderControlGfx103[] = {
      0x0,        // DB_RENDER_CONTROL
      0x0,        // DB_COUNT_CONTROL
      0x0,        // DB_DEPTH_VIEW
      0x0,        // DB_RENDER_OVERRIDE
      0x0,        // DB_RENDER_OVERRIDE2
      0x0,        // DB_HTILE_DATA_BASE
      0x0,        //
      0x0,        // DB_DEPTH_SIZE_XY
      0x0,        // DB_DEPTH_BOUNDS_MIN
      0x0,        // DB_DEPTH_BOUNDS_MAX
      0x0,        // DB_STENCIL_CLEAR
      0x0,        // DB_DEPTH_CLEAR
      0x0,        // PA_SC_SCREEN_SCISSOR_TL
      0x40004000, // PA_SC_SCREEN_SCISSOR_BR
      0x0,        // DB_DFSM_CONTROL
      0x0,        // DB_RESERVED_REG_2
      0x0,        // DB_Z_INFO
      0x0,        // DB_STENCIL_INFO
      0x0,        // DB_Z_READ_BASE
      0x0,        // DB_STENCIL_READ_BASE
      0x0,        // DB_Z_WRITE_BASE
      0x0,        // DB_STENCIL_WRITE_BASE
      0x0,        //
      0x0,        //
      0x0,        //
      0x0,        //
      0x0,        // DB_Z_READ_BASE_HI
      0x0,        // DB_STENCIL_READ_BASE_HI
      0x0,        // DB_Z_WRITE_BASE_HI
      0x0,        // DB_STENCIL_WRITE_BASE_HI
      0x0,        // DB_HTILE_DATA_BASE_HI
      0x0,        // DB_RMI_L2_CACHE_CONTROL
      0x0,        // TA_BC_BASE_ADDR
      0x0         // TA_BC_BASE_ADDR_HI
   };
   static const uint32_t CoherDestBaseHi0Gfx103[] = {
      0x0,        // COHER_DEST_BASE_HI_0
      0x0,        // COHER_DEST_BASE_HI_1
      0x0,        // COHER_DEST_BASE_HI_2
      0x0,        // COHER_DEST_BASE_HI_3
      0x0,        // COHER_DEST_BASE_2
      0x0,        // COHER_DEST_BASE_3
      0x0,        // PA_SC_WINDOW_OFFSET
      0x80000000, // PA_SC_WINDOW_SCISSOR_TL
      0x40004000, // PA_SC_WINDOW_SCISSOR_BR
      0xffff,     // PA_SC_CLIPRECT_RULE
      0x0,        // PA_SC_CLIPRECT_0_TL
      0x40004000, // PA_SC_CLIPRECT_0_BR
      0x0,        // PA_SC_CLIPRECT_1_TL
      0x40004000, // PA_SC_CLIPRECT_1_BR
      0x0,        // PA_SC_CLIPRECT_2_TL
      0x40004000, // PA_SC_CLIPRECT_2_BR
      0x0,        // PA_SC_CLIPRECT_3_TL
      0x40004000, // PA_SC_CLIPRECT_3_BR
      0xaa99aaaa, // PA_SC_EDGERULE
      0x0,        // PA_SU_HARDWARE_SCREEN_OFFSET
      0xffffffff, // CB_TARGET_MASK
      0xffffffff, // CB_SHADER_MASK
      0x80000000, // PA_SC_GENERIC_SCISSOR_TL
      0x40004000, // PA_SC_GENERIC_SCISSOR_BR
      0x0,        // COHER_DEST_BASE_0
      0x0,        // COHER_DEST_BASE_1
      0x80000000, // PA_SC_VPORT_SCISSOR_0_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_0_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_1_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_1_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_2_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_2_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_3_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_3_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_4_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_4_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_5_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_5_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_6_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_6_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_7_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_7_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_8_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_8_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_9_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_9_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_10_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_10_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_11_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_11_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_12_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_12_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_13_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_13_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_14_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_14_BR
      0x80000000, // PA_SC_VPORT_SCISSOR_15_TL
      0x40004000, // PA_SC_VPORT_SCISSOR_15_BR
      0x0,        // PA_SC_VPORT_ZMIN_0
      0x3f800000, // PA_SC_VPORT_ZMAX_0
      0x0,        // PA_SC_VPORT_ZMIN_1
      0x3f800000, // PA_SC_VPORT_ZMAX_1
      0x0,        // PA_SC_VPORT_ZMIN_2
      0x3f800000, // PA_SC_VPORT_ZMAX_2
      0x0,        // PA_SC_VPORT_ZMIN_3
      0x3f800000, // PA_SC_VPORT_ZMAX_3
      0x0,        // PA_SC_VPORT_ZMIN_4
      0x3f800000, // PA_SC_VPORT_ZMAX_4
      0x0,        // PA_SC_VPORT_ZMIN_5
      0x3f800000, // PA_SC_VPORT_ZMAX_5
      0x0,        // PA_SC_VPORT_ZMIN_6
      0x3f800000, // PA_SC_VPORT_ZMAX_6
      0x0,        // PA_SC_VPORT_ZMIN_7
      0x3f800000, // PA_SC_VPORT_ZMAX_7
      0x0,        // PA_SC_VPORT_ZMIN_8
      0x3f800000, // PA_SC_VPORT_ZMAX_8
      0x0,        // PA_SC_VPORT_ZMIN_9
      0x3f800000, // PA_SC_VPORT_ZMAX_9
      0x0,        // PA_SC_VPORT_ZMIN_10
      0x3f800000, // PA_SC_VPORT_ZMAX_10
      0x0,        // PA_SC_VPORT_ZMIN_11
      0x3f800000, // PA_SC_VPORT_ZMAX_11
      0x0,        // PA_SC_VPORT_ZMIN_12
      0x3f800000, // PA_SC_VPORT_ZMAX_12
      0x0,        // PA_SC_VPORT_ZMIN_13
      0x3f800000, // PA_SC_VPORT_ZMAX_13
      0x0,        // PA_SC_VPORT_ZMIN_14
      0x3f800000, // PA_SC_VPORT_ZMAX_14
      0x0,        // PA_SC_VPORT_ZMIN_15
      0x3f800000, // PA_SC_VPORT_ZMAX_15
      0x0,        // PA_SC_RASTER_CONFIG
      0x0,        // PA_SC_RASTER_CONFIG_1
      0x0,        //
      0x0         // PA_SC_TILE_STEERING_OVERRIDE
   };
   static const uint32_t VgtMultiPrimIbResetIndxGfx103[] = {
      0x0,       // VGT_MULTI_PRIM_IB_RESET_INDX
      0x0,       // CB_RMI_GL2_CACHE_CONTROL
      0x0,       // CB_BLEND_RED
      0x0,       // CB_BLEND_GREEN
      0x0,       // CB_BLEND_BLUE
      0x0,       // CB_BLEND_ALPHA
      0x0,       // CB_DCC_CONTROL
      0x0,       // CB_COVERAGE_OUT_CONTROL
      0x0,       // DB_STENCIL_CONTROL
      0x1000000, // DB_STENCILREFMASK
      0x1000000, // DB_STENCILREFMASK_BF
      0x0,       //
      0x0,       // PA_CL_VPORT_XSCALE
      0x0,       // PA_CL_VPORT_XOFFSET
      0x0,       // PA_CL_VPORT_YSCALE
      0x0,       // PA_CL_VPORT_YOFFSET
      0x0,       // PA_CL_VPORT_ZSCALE
      0x0,       // PA_CL_VPORT_ZOFFSET
      0x0,       // PA_CL_VPORT_XSCALE_1
      0x0,       // PA_CL_VPORT_XOFFSET_1
      0x0,       // PA_CL_VPORT_YSCALE_1
      0x0,       // PA_CL_VPORT_YOFFSET_1
      0x0,       // PA_CL_VPORT_ZSCALE_1
      0x0,       // PA_CL_VPORT_ZOFFSET_1
      0x0,       // PA_CL_VPORT_XSCALE_2
      0x0,       // PA_CL_VPORT_XOFFSET_2
      0x0,       // PA_CL_VPORT_YSCALE_2
      0x0,       // PA_CL_VPORT_YOFFSET_2
      0x0,       // PA_CL_VPORT_ZSCALE_2
      0x0,       // PA_CL_VPORT_ZOFFSET_2
      0x0,       // PA_CL_VPORT_XSCALE_3
      0x0,       // PA_CL_VPORT_XOFFSET_3
      0x0,       // PA_CL_VPORT_YSCALE_3
      0x0,       // PA_CL_VPORT_YOFFSET_3
      0x0,       // PA_CL_VPORT_ZSCALE_3
      0x0,       // PA_CL_VPORT_ZOFFSET_3
      0x0,       // PA_CL_VPORT_XSCALE_4
      0x0,       // PA_CL_VPORT_XOFFSET_4
      0x0,       // PA_CL_VPORT_YSCALE_4
      0x0,       // PA_CL_VPORT_YOFFSET_4
      0x0,       // PA_CL_VPORT_ZSCALE_4
      0x0,       // PA_CL_VPORT_ZOFFSET_4
      0x0,       // PA_CL_VPORT_XSCALE_5
      0x0,       // PA_CL_VPORT_XOFFSET_5
      0x0,       // PA_CL_VPORT_YSCALE_5
      0x0,       // PA_CL_VPORT_YOFFSET_5
      0x0,       // PA_CL_VPORT_ZSCALE_5
      0x0,       // PA_CL_VPORT_ZOFFSET_5
      0x0,       // PA_CL_VPORT_XSCALE_6
      0x0,       // PA_CL_VPORT_XOFFSET_6
      0x0,       // PA_CL_VPORT_YSCALE_6
      0x0,       // PA_CL_VPORT_YOFFSET_6
      0x0,       // PA_CL_VPORT_ZSCALE_6
      0x0,       // PA_CL_VPORT_ZOFFSET_6
      0x0,       // PA_CL_VPORT_XSCALE_7
      0x0,       // PA_CL_VPORT_XOFFSET_7
      0x0,       // PA_CL_VPORT_YSCALE_7
      0x0,       // PA_CL_VPORT_YOFFSET_7
      0x0,       // PA_CL_VPORT_ZSCALE_7
      0x0,       // PA_CL_VPORT_ZOFFSET_7
      0x0,       // PA_CL_VPORT_XSCALE_8
      0x0,       // PA_CL_VPORT_XOFFSET_8
      0x0,       // PA_CL_VPORT_YSCALE_8
      0x0,       // PA_CL_VPORT_YOFFSET_8
      0x0,       // PA_CL_VPORT_ZSCALE_8
      0x0,       // PA_CL_VPORT_ZOFFSET_8
      0x0,       // PA_CL_VPORT_XSCALE_9
      0x0,       // PA_CL_VPORT_XOFFSET_9
      0x0,       // PA_CL_VPORT_YSCALE_9
      0x0,       // PA_CL_VPORT_YOFFSET_9
      0x0,       // PA_CL_VPORT_ZSCALE_9
      0x0,       // PA_CL_VPORT_ZOFFSET_9
      0x0,       // PA_CL_VPORT_XSCALE_10
      0x0,       // PA_CL_VPORT_XOFFSET_10
      0x0,       // PA_CL_VPORT_YSCALE_10
      0x0,       // PA_CL_VPORT_YOFFSET_10
      0x0,       // PA_CL_VPORT_ZSCALE_10
      0x0,       // PA_CL_VPORT_ZOFFSET_10
      0x0,       // PA_CL_VPORT_XSCALE_11
      0x0,       // PA_CL_VPORT_XOFFSET_11
      0x0,       // PA_CL_VPORT_YSCALE_11
      0x0,       // PA_CL_VPORT_YOFFSET_11
      0x0,       // PA_CL_VPORT_ZSCALE_11
      0x0,       // PA_CL_VPORT_ZOFFSET_11
      0x0,       // PA_CL_VPORT_XSCALE_12
      0x0,       // PA_CL_VPORT_XOFFSET_12
      0x0,       // PA_CL_VPORT_YSCALE_12
      0x0,       // PA_CL_VPORT_YOFFSET_12
      0x0,       // PA_CL_VPORT_ZSCALE_12
      0x0,       // PA_CL_VPORT_ZOFFSET_12
      0x0,       // PA_CL_VPORT_XSCALE_13
      0x0,       // PA_CL_VPORT_XOFFSET_13
      0x0,       // PA_CL_VPORT_YSCALE_13
      0x0,       // PA_CL_VPORT_YOFFSET_13
      0x0,       // PA_CL_VPORT_ZSCALE_13
      0x0,       // PA_CL_VPORT_ZOFFSET_13
      0x0,       // PA_CL_VPORT_XSCALE_14
      0x0,       // PA_CL_VPORT_XOFFSET_14
      0x0,       // PA_CL_VPORT_YSCALE_14
      0x0,       // PA_CL_VPORT_YOFFSET_14
      0x0,       // PA_CL_VPORT_ZSCALE_14
      0x0,       // PA_CL_VPORT_ZOFFSET_14
      0x0,       // PA_CL_VPORT_XSCALE_15
      0x0,       // PA_CL_VPORT_XOFFSET_15
      0x0,       // PA_CL_VPORT_YSCALE_15
      0x0,       // PA_CL_VPORT_YOFFSET_15
      0x0,       // PA_CL_VPORT_ZSCALE_15
      0x0,       // PA_CL_VPORT_ZOFFSET_15
      0x0,       // PA_CL_UCP_0_X
      0x0,       // PA_CL_UCP_0_Y
      0x0,       // PA_CL_UCP_0_Z
      0x0,       // PA_CL_UCP_0_W
      0x0,       // PA_CL_UCP_1_X
      0x0,       // PA_CL_UCP_1_Y
      0x0,       // PA_CL_UCP_1_Z
      0x0,       // PA_CL_UCP_1_W
      0x0,       // PA_CL_UCP_2_X
      0x0,       // PA_CL_UCP_2_Y
      0x0,       // PA_CL_UCP_2_Z
      0x0,       // PA_CL_UCP_2_W
      0x0,       // PA_CL_UCP_3_X
      0x0,       // PA_CL_UCP_3_Y
      0x0,       // PA_CL_UCP_3_Z
      0x0,       // PA_CL_UCP_3_W
      0x0,       // PA_CL_UCP_4_X
      0x0,       // PA_CL_UCP_4_Y
      0x0,       // PA_CL_UCP_4_Z
      0x0,       // PA_CL_UCP_4_W
      0x0,       // PA_CL_UCP_5_X
      0x0,       // PA_CL_UCP_5_Y
      0x0,       // PA_CL_UCP_5_Z
      0x0        // PA_CL_UCP_5_W
   };
   static const uint32_t SpiPsInputCntl0Gfx103[] = {
      0x0, // SPI_PS_INPUT_CNTL_0
      0x0, // SPI_PS_INPUT_CNTL_1
      0x0, // SPI_PS_INPUT_CNTL_2
      0x0, // SPI_PS_INPUT_CNTL_3
      0x0, // SPI_PS_INPUT_CNTL_4
      0x0, // SPI_PS_INPUT_CNTL_5
      0x0, // SPI_PS_INPUT_CNTL_6
      0x0, // SPI_PS_INPUT_CNTL_7
      0x0, // SPI_PS_INPUT_CNTL_8
      0x0, // SPI_PS_INPUT_CNTL_9
      0x0, // SPI_PS_INPUT_CNTL_10
      0x0, // SPI_PS_INPUT_CNTL_11
      0x0, // SPI_PS_INPUT_CNTL_12
      0x0, // SPI_PS_INPUT_CNTL_13
      0x0, // SPI_PS_INPUT_CNTL_14
      0x0, // SPI_PS_INPUT_CNTL_15
      0x0, // SPI_PS_INPUT_CNTL_16
      0x0, // SPI_PS_INPUT_CNTL_17
      0x0, // SPI_PS_INPUT_CNTL_18
      0x0, // SPI_PS_INPUT_CNTL_19
      0x0, // SPI_PS_INPUT_CNTL_20
      0x0, // SPI_PS_INPUT_CNTL_21
      0x0, // SPI_PS_INPUT_CNTL_22
      0x0, // SPI_PS_INPUT_CNTL_23
      0x0, // SPI_PS_INPUT_CNTL_24
      0x0, // SPI_PS_INPUT_CNTL_25
      0x0, // SPI_PS_INPUT_CNTL_26
      0x0, // SPI_PS_INPUT_CNTL_27
      0x0, // SPI_PS_INPUT_CNTL_28
      0x0, // SPI_PS_INPUT_CNTL_29
      0x0, // SPI_PS_INPUT_CNTL_30
      0x0, // SPI_PS_INPUT_CNTL_31
      0x0, // SPI_VS_OUT_CONFIG
      0x0, //
      0x0, // SPI_PS_INPUT_ENA
      0x0, // SPI_PS_INPUT_ADDR
      0x0, // SPI_INTERP_CONTROL_0
      0x2, // SPI_PS_IN_CONTROL
      0x0, //
      0x0, // SPI_BARYC_CNTL
      0x0, //
      0x0, // SPI_TMPRING_SIZE
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, // SPI_SHADER_IDX_FORMAT
      0x0, // SPI_SHADER_POS_FORMAT
      0x0, // SPI_SHADER_Z_FORMAT
      0x0  // SPI_SHADER_COL_FORMAT
   };
   static const uint32_t SxPsDownconvertControlGfx103[] = {
      0x0, // SX_PS_DOWNCONVERT_CONTROL
      0x0, // SX_PS_DOWNCONVERT
      0x0, // SX_BLEND_OPT_EPSILON
      0x0, // SX_BLEND_OPT_CONTROL
      0x0, // SX_MRT0_BLEND_OPT
      0x0, // SX_MRT1_BLEND_OPT
      0x0, // SX_MRT2_BLEND_OPT
      0x0, // SX_MRT3_BLEND_OPT
      0x0, // SX_MRT4_BLEND_OPT
      0x0, // SX_MRT5_BLEND_OPT
      0x0, // SX_MRT6_BLEND_OPT
      0x0, // SX_MRT7_BLEND_OPT
      0x0, // CB_BLEND0_CONTROL
      0x0, // CB_BLEND1_CONTROL
      0x0, // CB_BLEND2_CONTROL
      0x0, // CB_BLEND3_CONTROL
      0x0, // CB_BLEND4_CONTROL
      0x0, // CB_BLEND5_CONTROL
      0x0, // CB_BLEND6_CONTROL
      0x0  // CB_BLEND7_CONTROL
   };
   static const uint32_t GeMaxOutputPerSubgroupGfx103[] = {
      0x0,     // GE_MAX_OUTPUT_PER_SUBGROUP
      0x0,     // DB_DEPTH_CONTROL
      0x0,     // DB_EQAA
      0x0,     // CB_COLOR_CONTROL
      0x0,     // DB_SHADER_CONTROL
      0x90000, // PA_CL_CLIP_CNTL
      0x4,     // PA_SU_SC_MODE_CNTL
      0x0,     // PA_CL_VTE_CNTL
      0x0,     // PA_CL_VS_OUT_CNTL
      0x0      // PA_CL_NANINF_CNTL
   };
   static const uint32_t PaSuPrimFilterCntlGfx103[] = {
      0x0, // PA_SU_PRIM_FILTER_CNTL
      0x0, // PA_SU_SMALL_PRIM_FILTER_CNTL
      0x0, //
      0x0, // PA_CL_NGG_CNTL
      0x0, // PA_SU_OVER_RASTERIZATION_CNTL
      0x0, // PA_STEREO_CNTL
      0x0, // PA_STATE_STEREO_X
      0x0  //
   };
   static const uint32_t PaSuPointSizeGfx103[] = {
      0x0, // PA_SU_POINT_SIZE
      0x0, // PA_SU_POINT_MINMAX
      0x0, // PA_SU_LINE_CNTL
      0x0  // PA_SC_LINE_STIPPLE
   };
   static const uint32_t VgtHosMaxTessLevelGfx103[] = {
      0x0, // VGT_HOS_MAX_TESS_LEVEL
      0x0  // VGT_HOS_MIN_TESS_LEVEL
   };
   static const uint32_t VgtGsModeGfx103[] = {
      0x0,   // VGT_GS_MODE
      0x0,   // VGT_GS_ONCHIP_CNTL
      0x0,   // PA_SC_MODE_CNTL_0
      0x0,   // PA_SC_MODE_CNTL_1
      0x0,   // VGT_ENHANCE
      0x100, // VGT_GS_PER_ES
      0x80,  // VGT_ES_PER_GS
      0x2,   // VGT_GS_PER_VS
      0x0,   // VGT_GSVS_RING_OFFSET_1
      0x0,   // VGT_GSVS_RING_OFFSET_2
      0x0,   // VGT_GSVS_RING_OFFSET_3
      0x0    // VGT_GS_OUT_PRIM_TYPE
   };
   static const uint32_t VgtPrimitiveidEnGfx103[] = {
      0x0 // VGT_PRIMITIVEID_EN
   };
   static const uint32_t VgtPrimitiveidResetGfx103[] = {
      0x0 // VGT_PRIMITIVEID_RESET
   };
   static const uint32_t VgtDrawPayloadCntlGfx103[] = {
      0x0, // VGT_DRAW_PAYLOAD_CNTL
      0x0, //
      0x0, // VGT_INSTANCE_STEP_RATE_0
      0x0, // VGT_INSTANCE_STEP_RATE_1
      0x0, // IA_MULTI_VGT_PARAM
      0x0, // VGT_ESGS_RING_ITEMSIZE
      0x0, // VGT_GSVS_RING_ITEMSIZE
      0x0, // VGT_REUSE_OFF
      0x0, // VGT_VTX_CNT_EN
      0x0, // DB_HTILE_SURFACE
      0x0, // DB_SRESULTS_COMPARE_STATE0
      0x0, // DB_SRESULTS_COMPARE_STATE1
      0x0, // DB_PRELOAD_CONTROL
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_SIZE_0
      0x0, // VGT_STRMOUT_VTX_STRIDE_0
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_OFFSET_0
      0x0, // VGT_STRMOUT_BUFFER_SIZE_1
      0x0, // VGT_STRMOUT_VTX_STRIDE_1
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_OFFSET_1
      0x0, // VGT_STRMOUT_BUFFER_SIZE_2
      0x0, // VGT_STRMOUT_VTX_STRIDE_2
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_OFFSET_2
      0x0, // VGT_STRMOUT_BUFFER_SIZE_3
      0x0, // VGT_STRMOUT_VTX_STRIDE_3
      0x0, //
      0x0, // VGT_STRMOUT_BUFFER_OFFSET_3
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, // VGT_STRMOUT_DRAW_OPAQUE_OFFSET
      0x0, // VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE
      0x0, // VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE
      0x0, //
      0x0, // VGT_GS_MAX_VERT_OUT
      0x0, //
      0x0, //
      0x0, //
      0x0, //
      0x0, // GE_NGG_SUBGRP_CNTL
      0x0, // VGT_TESS_DISTRIBUTION
      0x0, // VGT_SHADER_STAGES_EN
      0x0, // VGT_LS_HS_CONFIG
      0x0, // VGT_GS_VERT_ITEMSIZE
      0x0, // VGT_GS_VERT_ITEMSIZE_1
      0x0, // VGT_GS_VERT_ITEMSIZE_2
      0x0, // VGT_GS_VERT_ITEMSIZE_3
      0x0, // VGT_TF_PARAM
      0x0, // DB_ALPHA_TO_MASK
      0x0, //
      0x0, // PA_SU_POLY_OFFSET_DB_FMT_CNTL
      0x0, // PA_SU_POLY_OFFSET_CLAMP
      0x0, // PA_SU_POLY_OFFSET_FRONT_SCALE
      0x0, // PA_SU_POLY_OFFSET_FRONT_OFFSET
      0x0, // PA_SU_POLY_OFFSET_BACK_SCALE
      0x0, // PA_SU_POLY_OFFSET_BACK_OFFSET
      0x0, // VGT_GS_INSTANCE_CNT
      0x0, // VGT_STRMOUT_CONFIG
      0x0  // VGT_STRMOUT_BUFFER_CONFIG
   };
   static const uint32_t PaScCentroidPriority0Gfx103[] = {
      0x0,        // PA_SC_CENTROID_PRIORITY_0
      0x0,        // PA_SC_CENTROID_PRIORITY_1
      0x1000,     // PA_SC_LINE_CNTL
      0x0,        // PA_SC_AA_CONFIG
      0x5,        // PA_SU_VTX_CNTL
      0x3f800000, // PA_CL_GB_VERT_CLIP_ADJ
      0x3f800000, // PA_CL_GB_VERT_DISC_ADJ
      0x3f800000, // PA_CL_GB_HORZ_CLIP_ADJ
      0x3f800000, // PA_CL_GB_HORZ_DISC_ADJ
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_3
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_1
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_2
      0x0,        // PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_3
      0xffffffff, // PA_SC_AA_MASK_X0Y0_X1Y0
      0xffffffff, // PA_SC_AA_MASK_X0Y1_X1Y1
      0x0,        // PA_SC_SHADER_CONTROL
      0x3,        // PA_SC_BINNER_CNTL_0
      0x0,        // PA_SC_BINNER_CNTL_1
      0x100000,   // PA_SC_CONSERVATIVE_RASTERIZATION_CNTL
      0x0,        // PA_SC_NGG_MODE_CNTL
      0x0,        //
      0x1e,       // VGT_VERTEX_REUSE_BLOCK_CNTL
      0x20,       // VGT_OUT_DEALLOC_CNTL
      0x0,        // CB_COLOR0_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR0_VIEW
      0x0,        // CB_COLOR0_INFO
      0x0,        // CB_COLOR0_ATTRIB
      0x0,        // CB_COLOR0_DCC_CONTROL
      0x0,        // CB_COLOR0_CMASK
      0x0,        //
      0x0,        // CB_COLOR0_FMASK
      0x0,        //
      0x0,        // CB_COLOR0_CLEAR_WORD0
      0x0,        // CB_COLOR0_CLEAR_WORD1
      0x0,        // CB_COLOR0_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR1_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR1_VIEW
      0x0,        // CB_COLOR1_INFO
      0x0,        // CB_COLOR1_ATTRIB
      0x0,        // CB_COLOR1_DCC_CONTROL
      0x0,        // CB_COLOR1_CMASK
      0x0,        //
      0x0,        // CB_COLOR1_FMASK
      0x0,        //
      0x0,        // CB_COLOR1_CLEAR_WORD0
      0x0,        // CB_COLOR1_CLEAR_WORD1
      0x0,        // CB_COLOR1_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR2_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR2_VIEW
      0x0,        // CB_COLOR2_INFO
      0x0,        // CB_COLOR2_ATTRIB
      0x0,        // CB_COLOR2_DCC_CONTROL
      0x0,        // CB_COLOR2_CMASK
      0x0,        //
      0x0,        // CB_COLOR2_FMASK
      0x0,        //
      0x0,        // CB_COLOR2_CLEAR_WORD0
      0x0,        // CB_COLOR2_CLEAR_WORD1
      0x0,        // CB_COLOR2_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR3_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR3_VIEW
      0x0,        // CB_COLOR3_INFO
      0x0,        // CB_COLOR3_ATTRIB
      0x0,        // CB_COLOR3_DCC_CONTROL
      0x0,        // CB_COLOR3_CMASK
      0x0,        //
      0x0,        // CB_COLOR3_FMASK
      0x0,        //
      0x0,        // CB_COLOR3_CLEAR_WORD0
      0x0,        // CB_COLOR3_CLEAR_WORD1
      0x0,        // CB_COLOR3_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR4_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR4_VIEW
      0x0,        // CB_COLOR4_INFO
      0x0,        // CB_COLOR4_ATTRIB
      0x0,        // CB_COLOR4_DCC_CONTROL
      0x0,        // CB_COLOR4_CMASK
      0x0,        //
      0x0,        // CB_COLOR4_FMASK
      0x0,        //
      0x0,        // CB_COLOR4_CLEAR_WORD0
      0x0,        // CB_COLOR4_CLEAR_WORD1
      0x0,        // CB_COLOR4_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR5_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR5_VIEW
      0x0,        // CB_COLOR5_INFO
      0x0,        // CB_COLOR5_ATTRIB
      0x0,        // CB_COLOR5_DCC_CONTROL
      0x0,        // CB_COLOR5_CMASK
      0x0,        //
      0x0,        // CB_COLOR5_FMASK
      0x0,        //
      0x0,        // CB_COLOR5_CLEAR_WORD0
      0x0,        // CB_COLOR5_CLEAR_WORD1
      0x0,        // CB_COLOR5_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR6_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR6_VIEW
      0x0,        // CB_COLOR6_INFO
      0x0,        // CB_COLOR6_ATTRIB
      0x0,        // CB_COLOR6_DCC_CONTROL
      0x0,        // CB_COLOR6_CMASK
      0x0,        //
      0x0,        // CB_COLOR6_FMASK
      0x0,        //
      0x0,        // CB_COLOR6_CLEAR_WORD0
      0x0,        // CB_COLOR6_CLEAR_WORD1
      0x0,        // CB_COLOR6_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR7_BASE
      0x0,        //
      0x0,        //
      0x0,        // CB_COLOR7_VIEW
      0x0,        // CB_COLOR7_INFO
      0x0,        // CB_COLOR7_ATTRIB
      0x0,        // CB_COLOR7_DCC_CONTROL
      0x0,        // CB_COLOR7_CMASK
      0x0,        //
      0x0,        // CB_COLOR7_FMASK
      0x0,        //
      0x0,        // CB_COLOR7_CLEAR_WORD0
      0x0,        // CB_COLOR7_CLEAR_WORD1
      0x0,        // CB_COLOR7_DCC_BASE
      0x0,        //
      0x0,        // CB_COLOR0_BASE_EXT
      0x0,        // CB_COLOR1_BASE_EXT
      0x0,        // CB_COLOR2_BASE_EXT
      0x0,        // CB_COLOR3_BASE_EXT
      0x0,        // CB_COLOR4_BASE_EXT
      0x0,        // CB_COLOR5_BASE_EXT
      0x0,        // CB_COLOR6_BASE_EXT
      0x0,        // CB_COLOR7_BASE_EXT
      0x0,        // CB_COLOR0_CMASK_BASE_EXT
      0x0,        // CB_COLOR1_CMASK_BASE_EXT
      0x0,        // CB_COLOR2_CMASK_BASE_EXT
      0x0,        // CB_COLOR3_CMASK_BASE_EXT
      0x0,        // CB_COLOR4_CMASK_BASE_EXT
      0x0,        // CB_COLOR5_CMASK_BASE_EXT
      0x0,        // CB_COLOR6_CMASK_BASE_EXT
      0x0,        // CB_COLOR7_CMASK_BASE_EXT
      0x0,        // CB_COLOR0_FMASK_BASE_EXT
      0x0,        // CB_COLOR1_FMASK_BASE_EXT
      0x0,        // CB_COLOR2_FMASK_BASE_EXT
      0x0,        // CB_COLOR3_FMASK_BASE_EXT
      0x0,        // CB_COLOR4_FMASK_BASE_EXT
      0x0,        // CB_COLOR5_FMASK_BASE_EXT
      0x0,        // CB_COLOR6_FMASK_BASE_EXT
      0x0,        // CB_COLOR7_FMASK_BASE_EXT
      0x0,        // CB_COLOR0_DCC_BASE_EXT
      0x0,        // CB_COLOR1_DCC_BASE_EXT
      0x0,        // CB_COLOR2_DCC_BASE_EXT
      0x0,        // CB_COLOR3_DCC_BASE_EXT
      0x0,        // CB_COLOR4_DCC_BASE_EXT
      0x0,        // CB_COLOR5_DCC_BASE_EXT
      0x0,        // CB_COLOR6_DCC_BASE_EXT
      0x0,        // CB_COLOR7_DCC_BASE_EXT
      0x0,        // CB_COLOR0_ATTRIB2
      0x0,        // CB_COLOR1_ATTRIB2
      0x0,        // CB_COLOR2_ATTRIB2
      0x0,        // CB_COLOR3_ATTRIB2
      0x0,        // CB_COLOR4_ATTRIB2
      0x0,        // CB_COLOR5_ATTRIB2
      0x0,        // CB_COLOR6_ATTRIB2
      0x0,        // CB_COLOR7_ATTRIB2
      0x0,        // CB_COLOR0_ATTRIB3
      0x0,        // CB_COLOR1_ATTRIB3
      0x0,        // CB_COLOR2_ATTRIB3
      0x0,        // CB_COLOR3_ATTRIB3
      0x0,        // CB_COLOR4_ATTRIB3
      0x0,        // CB_COLOR5_ATTRIB3
      0x0,        // CB_COLOR6_ATTRIB3
      0x0         // CB_COLOR7_ATTRIB3
   };

   set_context_reg_seq_array(cs, R_028000_DB_RENDER_CONTROL, SET(DbRenderControlGfx103));
   set_context_reg_seq_array(cs, R_0281E8_COHER_DEST_BASE_HI_0, SET(CoherDestBaseHi0Gfx103));
   set_context_reg_seq_array(cs, R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
                             SET(VgtMultiPrimIbResetIndxGfx103));
   set_context_reg_seq_array(cs, R_028644_SPI_PS_INPUT_CNTL_0, SET(SpiPsInputCntl0Gfx103));
   set_context_reg_seq_array(cs, R_028750_SX_PS_DOWNCONVERT_CONTROL,
                             SET(SxPsDownconvertControlGfx103));
   set_context_reg_seq_array(cs, R_0287FC_GE_MAX_OUTPUT_PER_SUBGROUP,
                             SET(GeMaxOutputPerSubgroupGfx103));
   set_context_reg_seq_array(cs, R_02882C_PA_SU_PRIM_FILTER_CNTL, SET(PaSuPrimFilterCntlGfx103));
   set_context_reg_seq_array(cs, R_028A00_PA_SU_POINT_SIZE, SET(PaSuPointSizeGfx103));
   set_context_reg_seq_array(cs, R_028A18_VGT_HOS_MAX_TESS_LEVEL, SET(VgtHosMaxTessLevelGfx103));
   set_context_reg_seq_array(cs, R_028A40_VGT_GS_MODE, SET(VgtGsModeGfx103));
   set_context_reg_seq_array(cs, R_028A84_VGT_PRIMITIVEID_EN, SET(VgtPrimitiveidEnGfx103));
   set_context_reg_seq_array(cs, R_028A8C_VGT_PRIMITIVEID_RESET, SET(VgtPrimitiveidResetGfx103));
   set_context_reg_seq_array(cs, R_028A98_VGT_DRAW_PAYLOAD_CNTL, SET(VgtDrawPayloadCntlGfx103));
   set_context_reg_seq_array(cs, R_028BD4_PA_SC_CENTROID_PRIORITY_0,
                             SET(PaScCentroidPriority0Gfx103));

   for (unsigned i = 0; i < num_reg_pairs; i++)
      set_context_reg_seq_array(cs, reg_offsets[i], 1, &reg_values[i]);
}

void ac_emulate_clear_state(const struct radeon_info *info, struct radeon_cmdbuf *cs,
                            set_context_reg_seq_array_fn set_context_reg_seq_array)
{
   /* Set context registers same as CLEAR_STATE to initialize shadow memory. */
   unsigned reg_offset = R_02835C_PA_SC_TILE_STEERING_OVERRIDE;
   uint32_t reg_value = info->pa_sc_tile_steering_override;

   if (info->chip_class == GFX10_3) {
      gfx103_emulate_clear_state(cs, 1, &reg_offset, &reg_value, set_context_reg_seq_array);
   } else if (info->chip_class == GFX10) {
      gfx10_emulate_clear_state(cs, 1, &reg_offset, &reg_value, set_context_reg_seq_array);
   } else if (info->chip_class == GFX9) {
      gfx9_emulate_clear_state(cs, set_context_reg_seq_array);
   } else {
      unreachable("unimplemented");
   }
}

/* Debug helper to find if any registers are missing in the tables above.
 * Call this in the driver whenever you set a register.
 */
void ac_check_shadowed_regs(enum chip_class chip_class, enum radeon_family family,
                            unsigned reg_offset, unsigned count)
{
   bool found = false;
   bool shadowed = false;

   for (unsigned type = 0; type < SI_NUM_ALL_REG_RANGES && !found; type++) {
      const struct ac_reg_range *ranges;
      unsigned num_ranges;

      ac_get_reg_ranges(chip_class, family, type, &num_ranges, &ranges);

      for (unsigned i = 0; i < num_ranges; i++) {
         unsigned end_reg_offset = reg_offset + count * 4;
         unsigned end_range_offset = ranges[i].offset + ranges[i].size;

         /* Test if the ranges interect. */
         if (MAX2(ranges[i].offset, reg_offset) < MIN2(end_range_offset, end_reg_offset)) {
            /* Assertion: A register can be listed only once. */
            assert(!found);
            found = true;
            shadowed = type != SI_REG_RANGE_NON_SHADOWED;
         }
      }
   }

   if (reg_offset == R_00B858_COMPUTE_DESTINATION_EN_SE0 ||
       reg_offset == R_00B864_COMPUTE_DESTINATION_EN_SE2)
      return;

   if (!found || !shadowed) {
      printf("register %s: ", !found ? "not found" : "not shadowed");
      if (count > 1) {
         printf("%s .. %s\n", ac_get_register_name(chip_class, reg_offset),
                ac_get_register_name(chip_class, reg_offset + (count - 1) * 4));
      } else {
         printf("%s\n", ac_get_register_name(chip_class, reg_offset));
      }
   }
}

/* Debug helper to print all shadowed registers and their current values read
 * by umr. This can be used to verify whether register shadowing doesn't affect
 * apps that don't enable it, because the shadowed register tables might contain
 * registers that the driver doesn't set.
 */
void ac_print_shadowed_regs(const struct radeon_info *info)
{
   if (!debug_get_bool_option("AMD_PRINT_SHADOW_REGS", false))
      return;

   for (unsigned type = 0; type < SI_NUM_SHADOWED_REG_RANGES; type++) {
      const struct ac_reg_range *ranges;
      unsigned num_ranges;

      ac_get_reg_ranges(info->chip_class, info->family, type, &num_ranges, &ranges);

      for (unsigned i = 0; i < num_ranges; i++) {
         for (unsigned j = 0; j < ranges[i].size / 4; j++) {
            unsigned offset = ranges[i].offset + j * 4;

            const char *name = ac_get_register_name(info->chip_class, offset);
            unsigned value = -1;
            char cmd[1024];

            snprintf(cmd, sizeof(cmd), "umr -r 0x%x", offset);
            FILE *p = popen(cmd, "r");
            if (p) {
               ASSERTED int r = fscanf(p, "%x", &value);
               assert(r == 1);
               pclose(p);
            }

            printf("0x%X %s = 0x%X\n", offset, name, value);
         }
         printf("--------------------------------------------\n");
      }
   }
}
