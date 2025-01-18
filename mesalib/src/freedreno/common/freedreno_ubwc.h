/*
 * Copyright © Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef __FREEDRENO_UBWC_H__
#define __FREEDRENO_UBWC_H__

#include "util/format/u_format.h"

#include "freedreno_dev_info.h"

enum fd6_ubwc_compat_type {
   FD6_UBWC_UNKNOWN_COMPAT,
   FD6_UBWC_R8G8_UNORM,
   FD6_UBWC_R8G8_INT,
   FD6_UBWC_R8G8B8A8_UNORM,
   FD6_UBWC_R8G8B8A8_INT,
   FD6_UBWC_B8G8R8A8_UNORM,
   FD6_UBWC_R16G16_UNORM,
   FD6_UBWC_R16G16_INT,
   FD6_UBWC_R16G16B16A16_UNORM,
   FD6_UBWC_R16G16B16A16_INT,
   FD6_UBWC_R32_INT,
   FD6_UBWC_R32G32_INT,
   FD6_UBWC_R32G32B32A32_INT,
   FD6_UBWC_R32_FLOAT,
};

static inline enum fd6_ubwc_compat_type
fd6_ubwc_compat_mode(const struct fd_dev_info *info, enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R8G8_UNORM:
   case PIPE_FORMAT_R8G8_SRGB:
      return info->a7xx.ubwc_unorm_snorm_int_compatible ?
         FD6_UBWC_R8G8_INT : FD6_UBWC_R8G8_UNORM;

   case PIPE_FORMAT_R8G8_SNORM:
      return info->a7xx.ubwc_unorm_snorm_int_compatible ?
         FD6_UBWC_R8G8_INT : FD6_UBWC_UNKNOWN_COMPAT;

   case PIPE_FORMAT_R8G8_UINT:
   case PIPE_FORMAT_R8G8_SINT:
      return FD6_UBWC_R8G8_INT;

   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8A8_SRGB:
      return info->a7xx.ubwc_unorm_snorm_int_compatible ?
         FD6_UBWC_R8G8B8A8_INT : FD6_UBWC_R8G8B8A8_UNORM;

   case PIPE_FORMAT_R8G8B8A8_SNORM:
      return info->a7xx.ubwc_unorm_snorm_int_compatible ?
         FD6_UBWC_R8G8B8A8_INT : FD6_UBWC_UNKNOWN_COMPAT;

   case PIPE_FORMAT_R8G8B8A8_UINT:
   case PIPE_FORMAT_R8G8B8A8_SINT:
      return FD6_UBWC_R8G8B8A8_INT;

   case PIPE_FORMAT_R16G16_UNORM:
      return info->a7xx.ubwc_unorm_snorm_int_compatible ?
         FD6_UBWC_R16G16_INT : FD6_UBWC_R16G16_UNORM;

   case PIPE_FORMAT_R16G16_SNORM:
      return info->a7xx.ubwc_unorm_snorm_int_compatible ?
         FD6_UBWC_R16G16_INT : FD6_UBWC_UNKNOWN_COMPAT;

   case PIPE_FORMAT_R16G16_UINT:
   case PIPE_FORMAT_R16G16_SINT:
      return FD6_UBWC_R16G16_INT;

   case PIPE_FORMAT_R16G16B16A16_UNORM:
      return info->a7xx.ubwc_unorm_snorm_int_compatible ?
         FD6_UBWC_R16G16B16A16_INT : FD6_UBWC_R16G16B16A16_UNORM;

   case PIPE_FORMAT_R16G16B16A16_SNORM:
      return info->a7xx.ubwc_unorm_snorm_int_compatible ?
         FD6_UBWC_R16G16B16A16_INT : FD6_UBWC_UNKNOWN_COMPAT;

   case PIPE_FORMAT_R16G16B16A16_UINT:
   case PIPE_FORMAT_R16G16B16A16_SINT:
      return FD6_UBWC_R16G16B16A16_INT;

   case PIPE_FORMAT_R32_UINT:
   case PIPE_FORMAT_R32_SINT:
      return FD6_UBWC_R32_INT;

   case PIPE_FORMAT_R32G32_UINT:
   case PIPE_FORMAT_R32G32_SINT:
      return FD6_UBWC_R32G32_INT;

   case PIPE_FORMAT_R32G32B32A32_UINT:
   case PIPE_FORMAT_R32G32B32A32_SINT:
      return FD6_UBWC_R32G32B32A32_INT;

   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_R32_FLOAT:
      /* TODO: a630 blob allows these, but not a660.  When is it legal? */
      return FD6_UBWC_UNKNOWN_COMPAT;

   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_SRGB:
      /* The blob doesn't list these as compatible, but they surely are.
       * freedreno's happy to cast between them, and zink would really like
       * to.
       */
      return FD6_UBWC_B8G8R8A8_UNORM;

   default:
      return FD6_UBWC_UNKNOWN_COMPAT;
   }
}


#endif
