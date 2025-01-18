/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (c) 2007-2020 Broadcom. All Rights Reserved. The term
 * "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * svga3d_shaderdefs.h --
 *
 *    SVGA3D byte code format and limit definitions.
 */

#ifndef __SVGA3D_SHADER_DEFS__
#define __SVGA3D_SHADER_DEFS__

#include "svga3d_types.h"

#if defined __cplusplus
extern "C" {
#endif

#define SVGA3D_INPUTREG_MAX          16
#define SVGA3D_OUTPUTREG_MAX         12
#define SVGA3D_VERTEX_SAMPLERREG_MAX 4
#define SVGA3D_PIXEL_SAMPLERREG_MAX  16
#define SVGA3D_SAMPLERREG_MAX                                                  \
   (SVGA3D_PIXEL_SAMPLERREG_MAX + SVGA3D_VERTEX_SAMPLERREG_MAX)
#define SVGA3D_TEMPREG_MAX 32
#define SVGA3D_ADDRREG_MAX 1
#define SVGA3D_PREDREG_MAX 1

#define SVGA3D_MAX_SRC_REGS      4
#define SVGA3D_MAX_NESTING_LEVEL 32

#define SVGA3D_VS_TYPE 0xFFFE
#define SVGA3D_PS_TYPE 0xFFFF

typedef struct {
   union {
      struct {
         uint32 minor : 8;
         uint32 major : 8;
         uint32 type  : 16;
      };

      uint32 value;
   };
} SVGA3dShaderVersion;

#define SVGA3D_VS_10 ((uint32)((SVGA3D_VS_TYPE << 16) | 1 << 8))
#define SVGA3D_VS_11 ((uint32)(SVGA3D_VS_10 | 1))
#define SVGA3D_VS_20 ((uint32)((SVGA3D_VS_TYPE << 16) | 2 << 8))
#define SVGA3D_VS_21 ((uint32)(SVGA3D_VS_20 | 1))
#define SVGA3D_VS_30 ((uint32)((SVGA3D_VS_TYPE << 16) | 3 << 8))

#define SVGA3D_PS_10 ((uint32)((SVGA3D_PS_TYPE << 16) | 1 << 8))
#define SVGA3D_PS_11 ((uint32)(SVGA3D_PS_10 | 1))
#define SVGA3D_PS_12 ((uint32)(SVGA3D_PS_10 | 2))
#define SVGA3D_PS_13 ((uint32)(SVGA3D_PS_10 | 3))
#define SVGA3D_PS_14 ((uint32)(SVGA3D_PS_10 | 4))
#define SVGA3D_PS_20 ((uint32)((SVGA3D_PS_TYPE << 16) | 2 << 8))
#define SVGA3D_PS_21 ((uint32)(SVGA3D_PS_20 | 1))
#define SVGA3D_PS_30 ((uint32)((SVGA3D_PS_TYPE << 16) | 3 << 8))

typedef enum {
   SVGA3DPSVERSION_NONE = 0,
   SVGA3DPSVERSION_ENABLED = 1,
   SVGA3DPSVERSION_11 = 3,
   SVGA3DPSVERSION_12 = 5,
   SVGA3DPSVERSION_13 = 7,
   SVGA3DPSVERSION_14 = 9,
   SVGA3DPSVERSION_20 = 11,
   SVGA3DPSVERSION_30 = 13,
   SVGA3DPSVERSION_40 = 15,
   SVGA3DPSVERSION_MAX
} SVGA3dPixelShaderVersion;

typedef enum {
   SVGA3DVSVERSION_NONE = 0,
   SVGA3DVSVERSION_ENABLED = 1,
   SVGA3DVSVERSION_11 = 3,
   SVGA3DVSVERSION_20 = 5,
   SVGA3DVSVERSION_30 = 7,
   SVGA3DVSVERSION_40 = 9,
   SVGA3DVSVERSION_MAX
} SVGA3dVertexShaderVersion;

typedef enum {
   SVGA3DOP_NOP = 0,
   SVGA3DOP_MOV = 1,
   SVGA3DOP_ADD = 2,
   SVGA3DOP_SUB = 3,
   SVGA3DOP_MAD = 4,
   SVGA3DOP_MUL = 5,
   SVGA3DOP_RCP = 6,
   SVGA3DOP_RSQ = 7,
   SVGA3DOP_DP3 = 8,
   SVGA3DOP_DP4 = 9,
   SVGA3DOP_MIN = 10,
   SVGA3DOP_MAX = 11,
   SVGA3DOP_SLT = 12,
   SVGA3DOP_SGE = 13,
   SVGA3DOP_EXP = 14,
   SVGA3DOP_LOG = 15,
   SVGA3DOP_LIT = 16,
   SVGA3DOP_DST = 17,
   SVGA3DOP_LRP = 18,
   SVGA3DOP_FRC = 19,
   SVGA3DOP_M4x4 = 20,
   SVGA3DOP_M4x3 = 21,
   SVGA3DOP_M3x4 = 22,
   SVGA3DOP_M3x3 = 23,
   SVGA3DOP_M3x2 = 24,
   SVGA3DOP_CALL = 25,
   SVGA3DOP_CALLNZ = 26,
   SVGA3DOP_LOOP = 27,
   SVGA3DOP_RET = 28,
   SVGA3DOP_ENDLOOP = 29,
   SVGA3DOP_LABEL = 30,
   SVGA3DOP_DCL = 31,
   SVGA3DOP_POW = 32,
   SVGA3DOP_CRS = 33,
   SVGA3DOP_SGN = 34,
   SVGA3DOP_ABS = 35,
   SVGA3DOP_NRM = 36,
   SVGA3DOP_SINCOS = 37,
   SVGA3DOP_REP = 38,
   SVGA3DOP_ENDREP = 39,
   SVGA3DOP_IF = 40,
   SVGA3DOP_IFC = 41,
   SVGA3DOP_ELSE = 42,
   SVGA3DOP_ENDIF = 43,
   SVGA3DOP_BREAK = 44,
   SVGA3DOP_BREAKC = 45,
   SVGA3DOP_MOVA = 46,
   SVGA3DOP_DEFB = 47,
   SVGA3DOP_DEFI = 48,

   SVGA3DOP_TEXCOORD = 64,
   SVGA3DOP_TEXKILL = 65,
   SVGA3DOP_TEX = 66,
   SVGA3DOP_TEXBEM = 67,
   SVGA3DOP_TEXBEML = 68,
   SVGA3DOP_TEXREG2AR = 69,
   SVGA3DOP_TEXREG2GB = 70,
   SVGA3DOP_TEXM3x2PAD = 71,
   SVGA3DOP_TEXM3x2TEX = 72,
   SVGA3DOP_TEXM3x3PAD = 73,
   SVGA3DOP_TEXM3x3TEX = 74,
   SVGA3DOP_RESERVED0 = 75,
   SVGA3DOP_TEXM3x3SPEC = 76,
   SVGA3DOP_TEXM3x3VSPEC = 77,
   SVGA3DOP_EXPP = 78,
   SVGA3DOP_LOGP = 79,
   SVGA3DOP_CND = 80,
   SVGA3DOP_DEF = 81,
   SVGA3DOP_TEXREG2RGB = 82,
   SVGA3DOP_TEXDP3TEX = 83,
   SVGA3DOP_TEXM3x2DEPTH = 84,
   SVGA3DOP_TEXDP3 = 85,
   SVGA3DOP_TEXM3x3 = 86,
   SVGA3DOP_TEXDEPTH = 87,
   SVGA3DOP_CMP = 88,
   SVGA3DOP_BEM = 89,
   SVGA3DOP_DP2ADD = 90,
   SVGA3DOP_DSX = 91,
   SVGA3DOP_DSY = 92,
   SVGA3DOP_TEXLDD = 93,
   SVGA3DOP_SETP = 94,
   SVGA3DOP_TEXLDL = 95,
   SVGA3DOP_BREAKP = 96,
   SVGA3DOP_LAST_INST,
   SVGA3DOP_PHASE = 0xFFFD,
   SVGA3DOP_COMMENT = 0xFFFE,
   SVGA3DOP_END = 0xFFFF,
} SVGA3dShaderOpCodeType;

typedef enum {
   SVGA3DOPCONT_NONE,
   SVGA3DOPCONT_PROJECT,
   SVGA3DOPCONT_BIAS,
} SVGA3dShaderOpCodeControlFnType;

typedef enum {
   SVGA3DOPCOMP_RESERVED0 = 0,
   SVGA3DOPCOMP_GT,
   SVGA3DOPCOMP_EQ,
   SVGA3DOPCOMP_GE,
   SVGA3DOPCOMP_LT,
   SVGA3DOPCOMP_NE,
   SVGA3DOPCOMP_LE,
   SVGA3DOPCOMP_RESERVED1
} SVGA3dShaderOpCodeCompFnType;

typedef enum {
   SVGA3DREG_TEMP = 0,
   SVGA3DREG_INPUT,
   SVGA3DREG_CONST,
   SVGA3DREG_ADDR,
   SVGA3DREG_TEXTURE = 3,
   SVGA3DREG_RASTOUT,
   SVGA3DREG_ATTROUT,
   SVGA3DREG_TEXCRDOUT,
   SVGA3DREG_OUTPUT = 6,
   SVGA3DREG_CONSTINT,
   SVGA3DREG_COLOROUT,
   SVGA3DREG_DEPTHOUT,
   SVGA3DREG_SAMPLER,
   SVGA3DREG_CONST2,
   SVGA3DREG_CONST3,
   SVGA3DREG_CONST4,
   SVGA3DREG_CONSTBOOL,
   SVGA3DREG_LOOP,
   SVGA3DREG_TEMPFLOAT16,
   SVGA3DREG_MISCTYPE,
   SVGA3DREG_LABEL,
   SVGA3DREG_PREDICATE,
} SVGA3dShaderRegType;

typedef enum {
   SVGA3DRASTOUT_POSITION = 0,
   SVGA3DRASTOUT_FOG,
   SVGA3DRASTOUT_PSIZE
} SVGA3dShaderRastOutRegType;

typedef enum {
   SVGA3DMISCREG_POSITION = 0,
   SVGA3DMISCREG_FACE
} SVGA3DShaderMiscRegType;

typedef enum {
   SVGA3DSAMP_UNKNOWN = 0,
   SVGA3DSAMP_2D = 2,
   SVGA3DSAMP_CUBE,
   SVGA3DSAMP_VOLUME,
   SVGA3DSAMP_2D_SHADOW,
   SVGA3DSAMP_MAX,
} SVGA3dShaderSamplerType;

#define SVGA3DWRITEMASK_0   1
#define SVGA3DWRITEMASK_1   2
#define SVGA3DWRITEMASK_2   4
#define SVGA3DWRITEMASK_3   8
#define SVGA3DWRITEMASK_ALL 15

#define SVGA3DDSTMOD_NONE             0
#define SVGA3DDSTMOD_SATURATE         1
#define SVGA3DDSTMOD_PARTIALPRECISION 2

#define SVGA3DDSTMOD_MSAMPCENTROID 4

typedef enum {
   SVGA3DDSTSHFSCALE_X1 = 0,
   SVGA3DDSTSHFSCALE_X2 = 1,
   SVGA3DDSTSHFSCALE_X4 = 2,
   SVGA3DDSTSHFSCALE_X8 = 3,
   SVGA3DDSTSHFSCALE_D8 = 13,
   SVGA3DDSTSHFSCALE_D4 = 14,
   SVGA3DDSTSHFSCALE_D2 = 15
} SVGA3dShaderDstShfScaleType;

#define SVGA3DSWIZZLE_REPLICATEX 0x00
#define SVGA3DSWIZZLE_REPLICATEY 0x55
#define SVGA3DSWIZZLE_REPLICATEZ 0xAA
#define SVGA3DSWIZZLE_REPLICATEW 0xFF
#define SVGA3DSWIZZLE_NONE       0xE4
#define SVGA3DSWIZZLE_YZXW       0xC9
#define SVGA3DSWIZZLE_ZXYW       0xD2
#define SVGA3DSWIZZLE_WXYZ       0x1B

typedef enum {
   SVGA3DSRCMOD_NONE = 0,
   SVGA3DSRCMOD_NEG,
   SVGA3DSRCMOD_BIAS,
   SVGA3DSRCMOD_BIASNEG,
   SVGA3DSRCMOD_SIGN,
   SVGA3DSRCMOD_SIGNNEG,
   SVGA3DSRCMOD_COMP,
   SVGA3DSRCMOD_X2,
   SVGA3DSRCMOD_X2NEG,
   SVGA3DSRCMOD_DZ,
   SVGA3DSRCMOD_DW,
   SVGA3DSRCMOD_ABS,
   SVGA3DSRCMOD_ABSNEG,
   SVGA3DSRCMOD_NOT,
} SVGA3dShaderSrcModType;

typedef struct {
   union {
      struct {
         uint32 comment_op   : 16;
         uint32 comment_size : 16;
      };

      struct {
         uint32 op         : 16;
         uint32 control    : 3;
         uint32 reserved2  : 5;
         uint32 size       : 4;
         uint32 predicated : 1;
         uint32 reserved1  : 1;
         uint32 coissue    : 1;
         uint32 reserved0  : 1;
      };

      uint32 value;
   };
} SVGA3dShaderInstToken;

typedef struct {
   union {
      struct {
         uint32 num        : 11;
         uint32 type_upper : 2;
         uint32 relAddr    : 1;
         uint32 reserved1  : 2;
         uint32 mask       : 4;
         uint32 dstMod     : 4;
         uint32 shfScale   : 4;
         uint32 type_lower : 3;
         uint32 reserved0  : 1;
      };

      uint32 value;
   };
} SVGA3dShaderDestToken;

typedef struct {
   union {
      struct {
         uint32 num        : 11;
         uint32 type_upper : 2;
         uint32 relAddr    : 1;
         uint32 reserved1  : 2;
         uint32 swizzle    : 8;
         uint32 srcMod     : 4;
         uint32 type_lower : 3;
         uint32 reserved0  : 1;
      };

      uint32 value;
   };
} SVGA3dShaderSrcToken;

typedef struct {
   union {
      struct {
         union {
            struct {
               uint32 usage     : 5;
               uint32 reserved1 : 11;
               uint32 index     : 4;
               uint32 reserved0 : 12;
            };

            struct {
               uint32 reserved3 : 27;
               uint32 type      : 4;
               uint32 reserved2 : 1;
            };
         };

         SVGA3dShaderDestToken dst;
      };

      uint32 values[2];
   };
} SVGA3DOpDclArgs;

typedef struct {
   union {
      struct {
         SVGA3dShaderDestToken dst;

         union {
            float constValues[4];
            int constIValues[4];
            Bool constBValue;
         };
      };

      uint32 values[5];
   };
} SVGA3DOpDefArgs;

typedef union {
   uint32 value;
   SVGA3dShaderInstToken inst;
   SVGA3dShaderDestToken dest;
   SVGA3dShaderSrcToken src;
} SVGA3dShaderToken;

typedef struct {
   SVGA3dShaderVersion version;

} SVGA3dShaderProgram;

static const uint32 SVGA3D_INPUT_REG_POSITION_VS11 = 0;
static const uint32 SVGA3D_INPUT_REG_PSIZE_VS11 = 1;
static const uint32 SVGA3D_INPUT_REG_FOG_VS11 = 3;
static const uint32 SVGA3D_INPUT_REG_FOG_MASK_VS11 = SVGA3DWRITEMASK_3;
static const uint32 SVGA3D_INPUT_REG_COLOR_BASE_VS11 = 2;
static const uint32 SVGA3D_INPUT_REG_TEXCOORD_BASE_VS11 = 4;

static const uint32 SVGA3D_INPUT_REG_COLOR_BASE_PS11 = 0;
static const uint32 SVGA3D_INPUT_REG_TEXCOORD_BASE_PS11 = 2;
static const uint32 SVGA3D_OUTPUT_REG_DEPTH_PS11 = 0;
static const uint32 SVGA3D_OUTPUT_REG_COLOR_PS11 = 1;

static const uint32 SVGA3D_INPUT_REG_COLOR_BASE_PS20 = 0;
static const uint32 SVGA3D_INPUT_REG_COLOR_NUM_PS20 = 2;
static const uint32 SVGA3D_INPUT_REG_TEXCOORD_BASE_PS20 = 2;
static const uint32 SVGA3D_INPUT_REG_TEXCOORD_NUM_PS20 = 8;
static const uint32 SVGA3D_OUTPUT_REG_COLOR_BASE_PS20 = 1;
static const uint32 SVGA3D_OUTPUT_REG_COLOR_NUM_PS20 = 4;
static const uint32 SVGA3D_OUTPUT_REG_DEPTH_BASE_PS20 = 0;
static const uint32 SVGA3D_OUTPUT_REG_DEPTH_NUM_PS20 = 1;

static INLINE SVGA3dShaderRegType
SVGA3dShaderGetRegType(uint32 token)
{
   SVGA3dShaderSrcToken src;
   src.value = token;
   return (SVGA3dShaderRegType)(src.type_upper << 3 | src.type_lower);
}

#if defined __cplusplus
}
#endif

#endif
