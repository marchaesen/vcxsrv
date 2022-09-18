/*
 * Copyright 2008 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#ifndef AMD_FAMILY_H
#define AMD_FAMILY_H

#ifdef __cplusplus
extern "C" {
#endif

enum radeon_family
{
   CHIP_UNKNOWN = 0,
   /* R3xx-based cores. (GFX2) */
   CHIP_R300,
   CHIP_R350,
   CHIP_RV350,
   CHIP_RV370,
   CHIP_RV380,
   CHIP_RS400,
   CHIP_RC410,
   CHIP_RS480,
   /* R4xx-based cores. (GFX2) */
   CHIP_R420,
   CHIP_R423,
   CHIP_R430,
   CHIP_R480,
   CHIP_R481,
   CHIP_RV410,
   CHIP_RS600,
   CHIP_RS690,
   CHIP_RS740,
   /* R5xx-based cores. (GFX2) */
   CHIP_RV515,
   CHIP_R520,
   CHIP_RV530,
   CHIP_R580,
   CHIP_RV560,
   CHIP_RV570,
   /* GFX3 (R6xx) */
   CHIP_R600,
   CHIP_RV610,
   CHIP_RV630,
   CHIP_RV670,
   CHIP_RV620,
   CHIP_RV635,
   CHIP_RS780,
   CHIP_RS880,
   /* GFX3 (R7xx) */
   CHIP_RV770,
   CHIP_RV730,
   CHIP_RV710,
   CHIP_RV740,
   /* GFX4 (Evergreen) */
   CHIP_CEDAR,
   CHIP_REDWOOD,
   CHIP_JUNIPER,
   CHIP_CYPRESS,
   CHIP_HEMLOCK,
   CHIP_PALM,
   CHIP_SUMO,
   CHIP_SUMO2,
   CHIP_BARTS,
   CHIP_TURKS,
   CHIP_CAICOS,
   /* GFX5 (Northern Islands) */
   CHIP_CAYMAN,
   CHIP_ARUBA,
   /* GFX6 (Southern Islands) */
   CHIP_TAHITI,
   CHIP_PITCAIRN,
   CHIP_VERDE,
   CHIP_OLAND,
   CHIP_HAINAN,
   /* GFX7 (Sea Islands) */
   CHIP_BONAIRE,
   CHIP_KAVERI,
   CHIP_KABINI,
   CHIP_HAWAII,         /* Radeon 290, 390 */
   /* GFX8 (Volcanic Islands & Polaris) */
   CHIP_TONGA,          /* Radeon 285, 380 */
   CHIP_ICELAND,
   CHIP_CARRIZO,
   CHIP_FIJI,           /* Radeon Fury */
   CHIP_STONEY,
   CHIP_POLARIS10,      /* Radeon 470, 480, 570, 580, 590 */
   CHIP_POLARIS11,      /* Radeon 460, 560 */
   CHIP_POLARIS12,      /* Radeon 540, 550 */
   CHIP_VEGAM,
   /* GFX9 (Vega) */
   CHIP_VEGA10,         /* Vega 56, 64 */
   CHIP_VEGA12,
   CHIP_VEGA20,         /* Radeon VII, MI50 */
   CHIP_RAVEN,          /* Ryzen 2000, 3000 */
   CHIP_RAVEN2,         /* Ryzen 2200U, 3200U */
   CHIP_RENOIR,         /* Ryzen 4000, 5000 */
   CHIP_ARCTURUS,       /* MI100 */
   CHIP_ALDEBARAN,      /* MI200 */
   /* GFX10.1 (RDNA 1) */
   CHIP_NAVI10,         /* Radeon 5600, 5700 */
   CHIP_NAVI12,         /* Radeon Pro 5600M */
   CHIP_NAVI14,         /* Radeon 5300, 5500 */
   /* GFX10.3 (RDNA 2) */
   CHIP_NAVI21,         /* Radeon 6800, 6900 (formerly "Sienna Cichlid") */
   CHIP_NAVI22,         /* Radeon 6700 (formerly "Navy Flounder") */
   CHIP_VANGOGH,        /* Steam Deck */
   CHIP_NAVI23,         /* Radeon 6600 (formerly "Dimgrey Cavefish") */
   CHIP_NAVI24,         /* Radeon 6400, 6500 (formerly "Beige Goby") */
   CHIP_REMBRANDT,      /* Ryzen 6000 (formerly "Yellow Carp") */
   CHIP_GFX1036,
   CHIP_GFX1100,
   CHIP_GFX1101,
   CHIP_GFX1102,
   CHIP_GFX1103,
   CHIP_LAST,
};

enum amd_gfx_level
{
   CLASS_UNKNOWN = 0,
   R300,
   R400,
   R500,
   R600,
   R700,
   EVERGREEN,
   CAYMAN,
   GFX6,
   GFX7,
   GFX8,
   GFX9,
   GFX10,
   GFX10_3,
   GFX11,

   NUM_GFX_VERSIONS,
};

enum amd_ip_type
{
   AMD_IP_GFX = 0,
   AMD_IP_COMPUTE,
   AMD_IP_SDMA,
   AMD_IP_UVD,
   AMD_IP_VCE,
   AMD_IP_UVD_ENC,
   AMD_IP_VCN_DEC,
   AMD_IP_VCN_ENC,
   AMD_IP_VCN_UNIFIED = AMD_IP_VCN_ENC,
   AMD_IP_VCN_JPEG,
   AMD_NUM_IP_TYPES,
};

enum amd_vram_type {
   AMD_VRAM_TYPE_UNKNOWN = 0,
   AMD_VRAM_TYPE_GDDR1,
   AMD_VRAM_TYPE_DDR2,
   AMD_VRAM_TYPE_GDDR3,
   AMD_VRAM_TYPE_GDDR4,
   AMD_VRAM_TYPE_GDDR5,
   AMD_VRAM_TYPE_HBM,
   AMD_VRAM_TYPE_DDR3,
   AMD_VRAM_TYPE_DDR4,
   AMD_VRAM_TYPE_GDDR6,
   AMD_VRAM_TYPE_DDR5,
};

const char *ac_get_family_name(enum radeon_family family);

#ifdef __cplusplus
}
#endif

#endif
