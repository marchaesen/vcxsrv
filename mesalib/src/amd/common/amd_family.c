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
      return "tahiti";
   case CHIP_PITCAIRN:
      return "pitcairn";
   case CHIP_VERDE:
      return "verde";
   case CHIP_OLAND:
      return "oland";
   case CHIP_HAINAN:
      return "hainan";
   case CHIP_BONAIRE:
      return "bonaire";
   case CHIP_KABINI:
      return "kabini";
   case CHIP_KAVERI:
      return "kaveri";
   case CHIP_HAWAII:
      return "hawaii";
   case CHIP_TONGA:
      return "tonga";
   case CHIP_ICELAND:
      return "iceland";
   case CHIP_CARRIZO:
      return "carrizo";
   case CHIP_FIJI:
      return "fiji";
   case CHIP_STONEY:
      return "stoney";
   case CHIP_POLARIS10:
      return "polaris10";
   case CHIP_POLARIS11:
      return "polaris11";
   case CHIP_POLARIS12:
      return "polaris12";
   case CHIP_VEGAM:
      return "vegam";
   case CHIP_VEGA10:
      return "vega10";
   case CHIP_RAVEN:
      return "raven";
   case CHIP_VEGA12:
      return "vega12";
   case CHIP_VEGA20:
      return "vega20";
   case CHIP_RAVEN2:
      return "raven2";
   case CHIP_RENOIR:
      return "renoir";
   case CHIP_ARCTURUS:
      return "arcturus";
   case CHIP_ALDEBARAN:
      return "aldebaran";
   case CHIP_NAVI10:
      return "navi10";
   case CHIP_NAVI12:
      return "navi12";
   case CHIP_NAVI14:
      return "navi14";
   case CHIP_SIENNA_CICHLID:
      return "sienna_cichlid";
   case CHIP_NAVY_FLOUNDER:
      return "navy_flounder";
   case CHIP_DIMGREY_CAVEFISH:
      return "dimgrey_cavefish";
   case CHIP_VANGOGH:
      return "vangogh";
   case CHIP_BEIGE_GOBY:
      return "beige_goby";
   case CHIP_YELLOW_CARP:
      return "yellow_carp";
   default:
      unreachable("Unknown GPU family");
   }
}
