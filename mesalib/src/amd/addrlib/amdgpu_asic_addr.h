/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
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

#ifndef _AMDGPU_ASIC_ADDR_H
#define _AMDGPU_ASIC_ADDR_H

#define ATI_VENDOR_ID         0x1002
#define AMD_VENDOR_ID         0x1022

// AMDGPU_VENDOR_IS_AMD(vendorId)
#define AMDGPU_VENDOR_IS_AMD(v) ((v == ATI_VENDOR_ID) || (v == AMD_VENDOR_ID))

#define FAMILY_UNKNOWN 0x00
#define FAMILY_TN      0x69
#define FAMILY_SI      0x6E
#define FAMILY_CI      0x78
#define FAMILY_KV      0x7D
#define FAMILY_VI      0x82
#define FAMILY_POLARIS 0x82
#define FAMILY_CZ      0x87
#define FAMILY_AI      0x8D
#define FAMILY_RV      0x8E

// AMDGPU_FAMILY_IS(familyId, familyName)
#define FAMILY_IS(f, fn)     (f == FAMILY_##fn)
#define FAMILY_IS_TN(f)      FAMILY_IS(f, TN)
#define FAMILY_IS_SI(f)      FAMILY_IS(f, SI)
#define FAMILY_IS_CI(f)      FAMILY_IS(f, CI)
#define FAMILY_IS_KV(f)      FAMILY_IS(f, KV)
#define FAMILY_IS_VI(f)      FAMILY_IS(f, VI)
#define FAMILY_IS_POLARIS(f) FAMILY_IS(f, POLARIS)
#define FAMILY_IS_CZ(f)      FAMILY_IS(f, CZ)
#define FAMILY_IS_AI(f)      FAMILY_IS(f, AI)
#define FAMILY_IS_RV(f)      FAMILY_IS(f, RV)

#define AMDGPU_UNKNOWN          0xFF

#define AMDGPU_TAHITI_RANGE     0x05, 0x14
#define AMDGPU_PITCAIRN_RANGE   0x15, 0x28
#define AMDGPU_CAPEVERDE_RANGE  0x29, 0x3C
#define AMDGPU_OLAND_RANGE      0x3C, 0x46
#define AMDGPU_HAINAN_RANGE     0x46, 0xFF

#define AMDGPU_BONAIRE_RANGE    0x14, 0x28
#define AMDGPU_HAWAII_RANGE     0x28, 0x3C

#define AMDGPU_SPECTRE_RANGE    0x01, 0x41
#define AMDGPU_SPOOKY_RANGE     0x41, 0x81
#define AMDGPU_KALINDI_RANGE    0x81, 0xA1
#define AMDGPU_GODAVARI_RANGE   0xA1, 0xFF

#define AMDGPU_ICELAND_RANGE    0x01, 0x14
#define AMDGPU_TONGA_RANGE      0x14, 0x28
#define AMDGPU_FIJI_RANGE       0x3C, 0x50

#define AMDGPU_POLARIS10_RANGE  0x50, 0x5A
#define AMDGPU_POLARIS11_RANGE  0x5A, 0x64
#define AMDGPU_POLARIS12_RANGE  0x64, 0x6E
#define AMDGPU_VEGAM_RANGE      0x6E, 0xFF

#define AMDGPU_CARRIZO_RANGE    0x01, 0x21
#define AMDGPU_BRISTOL_RANGE    0x10, 0x21
#define AMDGPU_STONEY_RANGE     0x61, 0xFF

#define AMDGPU_VEGA10_RANGE     0x01, 0x14
#define AMDGPU_VEGA12_RANGE     0x14, 0x28

#define AMDGPU_RAVEN_RANGE      0x01, 0x81

#define AMDGPU_EXPAND_FIX(x) x
#define AMDGPU_RANGE_HELPER(val, min, max) ((val >= min) && (val < max))
#define AMDGPU_IN_RANGE(val, ...)   AMDGPU_EXPAND_FIX(AMDGPU_RANGE_HELPER(val, __VA_ARGS__))


// ASICREV_IS(eRevisionId, revisionName)
#define ASICREV_IS(r, rn)              AMDGPU_IN_RANGE(r, AMDGPU_##rn##_RANGE)
#define ASICREV_IS_TAHITI_P(r)         ASICREV_IS(r, TAHITI)
#define ASICREV_IS_PITCAIRN_PM(r)      ASICREV_IS(r, PITCAIRN)
#define ASICREV_IS_CAPEVERDE_M(r)      ASICREV_IS(r, CAPEVERDE)
#define ASICREV_IS_OLAND_M(r)          ASICREV_IS(r, OLAND)
#define ASICREV_IS_HAINAN_V(r)         ASICREV_IS(r, HAINAN)

#define ASICREV_IS_BONAIRE_M(r)        ASICREV_IS(r, BONAIRE)
#define ASICREV_IS_HAWAII_P(r)         ASICREV_IS(r, HAWAII)

#define ASICREV_IS_SPECTRE(r)          ASICREV_IS(r, SPECTRE)
#define ASICREV_IS_SPOOKY(r)           ASICREV_IS(r, SPOOKY)
#define ASICREV_IS_KALINDI(r)          ASICREV_IS(r, KALINDI)
#define ASICREV_IS_KALINDI_GODAVARI(r) ASICREV_IS(r, GODAVARI)

#define ASICREV_IS_ICELAND_M(r)        ASICREV_IS(r, ICELAND)
#define ASICREV_IS_TONGA_P(r)          ASICREV_IS(r, TONGA)
#define ASICREV_IS_FIJI_P(r)           ASICREV_IS(r, FIJI)

#define ASICREV_IS_POLARIS10_P(r)      ASICREV_IS(r, POLARIS10)
#define ASICREV_IS_POLARIS11_M(r)      ASICREV_IS(r, POLARIS11)
#define ASICREV_IS_POLARIS12_V(r)      ASICREV_IS(r, POLARIS12)
#define ASICREV_IS_VEGAM_P(r)          ASICREV_IS(r, VEGAM)

#define ASICREV_IS_CARRIZO(r)          ASICREV_IS(r, CARRIZO)
#define ASICREV_IS_CARRIZO_BRISTOL(r)  ASICREV_IS(r, BRISTOL)
#define ASICREV_IS_STONEY(r)           ASICREV_IS(r, STONEY)

#define ASICREV_IS_VEGA10_M(r)         ASICREV_IS(r, VEGA10)
#define ASICREV_IS_VEGA10_P(r)         ASICREV_IS(r, VEGA10)
#define ASICREV_IS_VEGA12_P(r)         ASICREV_IS(r, VEGA12)
#define ASICREV_IS_VEGA12_p(r)         ASICREV_IS(r, VEGA12)

#define ASICREV_IS_RAVEN(r)            ASICREV_IS(r, RAVEN)

#endif // _AMDGPU_ASIC_ADDR_H
