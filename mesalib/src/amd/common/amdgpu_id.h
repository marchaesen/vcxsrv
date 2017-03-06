/*
 * Copyright © 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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

/**
 * This file is included by addrlib. It adds GPU family definitions and
 * macros compatible with addrlib.
 */

#ifndef AMDGPU_ID_H
#define AMDGPU_ID_H

#include "util/u_endian.h"

#if defined(PIPE_ARCH_LITTLE_ENDIAN)
#define LITTLEENDIAN_CPU
#elif defined(PIPE_ARCH_BIG_ENDIAN)
#define BIGENDIAN_CPU
#endif

enum {
	FAMILY_UNKNOWN,
	FAMILY_SI,
	FAMILY_CI,
	FAMILY_KV,
	FAMILY_VI,
	FAMILY_CZ,
	FAMILY_PI,
	FAMILY_LAST,
};

/* SI specific rev IDs */
enum {
	SI_TAHITI_P_A11      = 1,
	SI_TAHITI_P_A0       = SI_TAHITI_P_A11,      /*A0 is alias of A11*/
	SI_TAHITI_P_A21      = 5,
	SI_TAHITI_P_B0       = SI_TAHITI_P_A21,      /*B0 is alias of A21*/
	SI_TAHITI_P_A22      = 6,
	SI_TAHITI_P_B1       = SI_TAHITI_P_A22,      /*B1 is alias of A22*/

	SI_PITCAIRN_PM_A11   = 20,
	SI_PITCAIRN_PM_A0    = SI_PITCAIRN_PM_A11,   /*A0 is alias of A11*/
	SI_PITCAIRN_PM_A12   = 21,
	SI_PITCAIRN_PM_A1    = SI_PITCAIRN_PM_A12,   /*A1 is alias of A12*/

	SI_CAPEVERDE_M_A11   = 40,
	SI_CAPEVERDE_M_A0    = SI_CAPEVERDE_M_A11,   /*A0 is alias of A11*/
	SI_CAPEVERDE_M_A12   = 41,
	SI_CAPEVERDE_M_A1    = SI_CAPEVERDE_M_A12,   /*A1 is alias of A12*/

	SI_OLAND_M_A0        = 60,

	SI_HAINAN_V_A0       = 70,

	SI_UNKNOWN           = 0xFF
};


#define ASICREV_IS_TAHITI_P(eChipRev)	\
	(eChipRev < SI_PITCAIRN_PM_A11)
#define ASICREV_IS_PITCAIRN_PM(eChipRev)	\
	((eChipRev >= SI_PITCAIRN_PM_A11) && (eChipRev < SI_CAPEVERDE_M_A11))
#define ASICREV_IS_CAPEVERDE_M(eChipRev)	\
	((eChipRev >= SI_CAPEVERDE_M_A11) && (eChipRev < SI_OLAND_M_A0))
#define ASICREV_IS_OLAND_M(eChipRev)	\
	((eChipRev >= SI_OLAND_M_A0) && (eChipRev < SI_HAINAN_V_A0))
#define ASICREV_IS_HAINAN_V(eChipRev)	\
(eChipRev >= SI_HAINAN_V_A0)

/* CI specific revIDs */
enum {
	CI_BONAIRE_M_A0 = 20,
	CI_BONAIRE_M_A1 = 21,

	CI_HAWAII_P_A0  = 40,

	CI_UNKNOWN      = 0xFF
};

#define ASICREV_IS_BONAIRE_M(eChipRev)	\
	((eChipRev >= CI_BONAIRE_M_A0) && (eChipRev < CI_HAWAII_P_A0))
#define ASICREV_IS_HAWAII_P(eChipRev)	\
	(eChipRev >= CI_HAWAII_P_A0)

/* KV specific rev IDs */
enum {
	KV_SPECTRE_A0      = 0x01,       /* KV1 with Spectre GFX core, 8-8-1-2 (CU-Pix-Primitive-RB) */
	KV_SPOOKY_A0       = 0x41,       /* KV2 with Spooky GFX core, including downgraded from Spectre core, 3-4-1-1 (CU-Pix-Primitive-RB) */
	KB_KALINDI_A0      = 0x81,       /* KB with Kalindi GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
	KB_KALINDI_A1      = 0x82,       /* KB with Kalindi GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
	BV_KALINDI_A2      = 0x85,       /* BV with Kalindi GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
	ML_GODAVARI_A0     = 0xa1,      /* ML with Godavari GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
	ML_GODAVARI_A1     = 0xa2,      /* ML with Godavari GFX core, 2-4-1-1 (CU-Pix-Primitive-RB) */
	KV_UNKNOWN = 0xFF
};

#define ASICREV_IS_SPECTRE(eChipRev)	\
	((eChipRev >= KV_SPECTRE_A0) && (eChipRev < KV_SPOOKY_A0))         /* identify all versions of SPRECTRE and supported features set */
#define ASICREV_IS_SPOOKY(eChipRev)	\
	((eChipRev >= KV_SPOOKY_A0) && (eChipRev < KB_KALINDI_A0))          /* identify all versions of SPOOKY and supported features set */
#define ASICREV_IS_KALINDI(eChipRev)	\
	((eChipRev >= KB_KALINDI_A0) && (eChipRev < KV_UNKNOWN))           /* identify all versions of KALINDI and supported features set */

/* Following macros are subset of ASICREV_IS_KALINDI macro */
#define ASICREV_IS_KALINDI_BHAVANI(eChipRev)	\
	((eChipRev >= BV_KALINDI_A2) && (eChipRev < ML_GODAVARI_A0))   /* identify all versions of BHAVANI and supported features set */
#define ASICREV_IS_KALINDI_GODAVARI(eChipRev)	\
	((eChipRev >= ML_GODAVARI_A0) && (eChipRev < KV_UNKNOWN)) /* identify all versions of GODAVARI and supported features set */

/* VI specific rev IDs */
enum {
	VI_ICELAND_M_A0   = 1,

	VI_TONGA_P_A0     = 20,
	VI_TONGA_P_A1     = 21,

	VI_FIJI_P_A0      = 60,

	VI_POLARIS10_P_A0 = 80,

	VI_POLARIS11_M_A0 = 90,

	VI_POLARIS12_V_A0 = 100,

	VI_UNKNOWN        = 0xFF
};


#define ASICREV_IS_ICELAND_M(eChipRev)	\
	(eChipRev < VI_TONGA_P_A0)
#define ASICREV_IS_TONGA_P(eChipRev)	\
	((eChipRev >= VI_TONGA_P_A0) && (eChipRev < VI_FIJI_P_A0))
#define ASICREV_IS_FIJI_P(eChipRev)	\
	((eChipRev >= VI_FIJI_P_A0)  && (eChipRev < VI_POLARIS10_P_A0))
#define ASICREV_IS_POLARIS10_P(eChipRev)\
	((eChipRev >= VI_POLARIS10_P_A0) && (eChipRev < VI_POLARIS11_M_A0))
#define ASICREV_IS_POLARIS11_M(eChipRev)   \
	(eChipRev >= VI_POLARIS11_M_A0 && eChipRev < VI_POLARIS12_V_A0)
#define ASICREV_IS_POLARIS12_V(eChipRev)\
	(eChipRev >= VI_POLARIS12_V_A0)

/* CZ specific rev IDs */
enum {
	CARRIZO_A0   = 0x01,
    STONEY_A0    = 0x61,
	CZ_UNKNOWN      = 0xFF
};

#define ASICREV_IS_CARRIZO(eChipRev) \
	((eChipRev >= CARRIZO_A0) && (eChipRev < STONEY_A0))

#define ASICREV_IS_STONEY(eChipRev) \
	((eChipRev >= STONEY_A0) && (eChipRev < CZ_UNKNOWN))

#endif /* AMDGPU_ID_H */
