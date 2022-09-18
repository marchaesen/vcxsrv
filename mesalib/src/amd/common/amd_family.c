/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
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

#include "amd_family.h"

#include "util/macros.h"

const char *ac_get_family_name(enum radeon_family family)
{
   switch (family) {
   case CHIP_TAHITI:
      return "TAHITI";
   case CHIP_PITCAIRN:
      return "PITCAIRN";
   case CHIP_VERDE:
      return "VERDE";
   case CHIP_OLAND:
      return "OLAND";
   case CHIP_HAINAN:
      return "HAINAN";
   case CHIP_BONAIRE:
      return "BONAIRE";
   case CHIP_KABINI:
      return "KABINI";
   case CHIP_KAVERI:
      return "KAVERI";
   case CHIP_HAWAII:
      return "HAWAII";
   case CHIP_TONGA:
      return "TONGA";
   case CHIP_ICELAND:
      return "ICELAND";
   case CHIP_CARRIZO:
      return "CARRIZO";
   case CHIP_FIJI:
      return "FIJI";
   case CHIP_STONEY:
      return "STONEY";
   case CHIP_POLARIS10:
      return "POLARIS10";
   case CHIP_POLARIS11:
      return "POLARIS11";
   case CHIP_POLARIS12:
      return "POLARIS12";
   case CHIP_VEGAM:
      return "VEGAM";
   case CHIP_VEGA10:
      return "VEGA10";
   case CHIP_RAVEN:
      return "RAVEN";
   case CHIP_VEGA12:
      return "VEGA12";
   case CHIP_VEGA20:
      return "VEGA20";
   case CHIP_RAVEN2:
      return "RAVEN2";
   case CHIP_RENOIR:
      return "RENOIR";
   case CHIP_ARCTURUS:
      return "ARCTURUS";
   case CHIP_ALDEBARAN:
      return "ALDEBARAN";
   case CHIP_NAVI10:
      return "NAVI10";
   case CHIP_NAVI12:
      return "NAVI12";
   case CHIP_NAVI14:
      return "NAVI14";
   case CHIP_NAVI21:
      return "NAVI21";
   case CHIP_NAVI22:
      return "NAVI22";
   case CHIP_NAVI23:
      return "NAVI23";
   case CHIP_VANGOGH:
      return "VANGOGH";
   case CHIP_NAVI24:
      return "NAVI24";
   case CHIP_REMBRANDT:
      return "REMBRANDT";
   case CHIP_GFX1036:
      return "GFX1036";
   case CHIP_GFX1100:
      return "GFX1100";
   case CHIP_GFX1101:
      return "GFX1101";
   case CHIP_GFX1102:
      return "GFX1102";
   case CHIP_GFX1103:
      return "GFX1103";
   default:
      unreachable("Unknown GPU family");
   }
}
