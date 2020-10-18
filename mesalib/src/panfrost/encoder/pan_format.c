/*
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include <stdio.h>
#include "panfrost-job.h"
#include "pan_texture.h"

/* Convenience */

#define _V PIPE_BIND_VERTEX_BUFFER
#define _T PIPE_BIND_SAMPLER_VIEW
#define _R PIPE_BIND_RENDER_TARGET
#define _Z PIPE_BIND_DEPTH_STENCIL
#define _VT (_V | _T)
#define _VTR (_V | _T | _R)
#define _TZ (_T | _Z)

struct panfrost_format panfrost_pipe_format_table[PIPE_FORMAT_COUNT] = {
        [PIPE_FORMAT_ETC1_RGB8] 		= { MALI_ETC2_RGB8, _T },
        [PIPE_FORMAT_ETC2_RGB8] 		= { MALI_ETC2_RGB8, _T },
        [PIPE_FORMAT_ETC2_SRGB8] 		= { MALI_ETC2_RGB8, _T },
        [PIPE_FORMAT_ETC2_R11_UNORM] 		= { MALI_ETC2_R11_UNORM, _T },
        [PIPE_FORMAT_ETC2_RGBA8] 		= { MALI_ETC2_RGBA8, _T },
        [PIPE_FORMAT_ETC2_SRGBA8] 		= { MALI_ETC2_RGBA8, _T },
        [PIPE_FORMAT_ETC2_RG11_UNORM] 		= { MALI_ETC2_RG11_UNORM, _T },
        [PIPE_FORMAT_ETC2_R11_SNORM] 		= { MALI_ETC2_R11_SNORM, _T },
        [PIPE_FORMAT_ETC2_RG11_SNORM] 		= { MALI_ETC2_RG11_SNORM, _T },
        [PIPE_FORMAT_ETC2_RGB8A1] 		= { MALI_ETC2_RGB8A1, _T },
        [PIPE_FORMAT_ETC2_SRGB8A1] 		= { MALI_ETC2_RGB8A1, _T },

        [PIPE_FORMAT_ASTC_4x4]	                = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_5x4]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_5x5]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_6x5]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_6x6]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_8x5]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_8x6]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_8x8]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_10x5]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_10x6]		        = { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_10x8]	        	= { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_10x10]		= { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_12x10]		= { MALI_ASTC_HDR_SUPP, _T },
        [PIPE_FORMAT_ASTC_12x12]		= { MALI_ASTC_HDR_SUPP, _T },

        [PIPE_FORMAT_ASTC_4x4_SRGB]             = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_5x4_SRGB]             = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_5x5_SRGB]             = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_6x5_SRGB]             = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_6x6_SRGB]             = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_8x5_SRGB]             = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_8x6_SRGB]             = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_8x8_SRGB]             = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_10x5_SRGB]            = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_10x6_SRGB]            = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_10x8_SRGB]            = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_10x10_SRGB]           = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_12x10_SRGB]           = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_ASTC_12x12_SRGB]           = { MALI_ASTC_SRGB_SUPP, _T },
        [PIPE_FORMAT_B5G6R5_UNORM] 		= { MALI_RGB565, _VTR },
        [PIPE_FORMAT_B5G5R5X1_UNORM] 		= { MALI_RGB5_X1_UNORM, _VT },
        [PIPE_FORMAT_R5G5B5A1_UNORM] 		= { MALI_RGB5_A1_UNORM, _VTR },

        [PIPE_FORMAT_R10G10B10X2_UNORM] 	= { MALI_RGB10_A2_UNORM, _VTR },
        [PIPE_FORMAT_B10G10R10X2_UNORM] 	= { MALI_RGB10_A2_UNORM, _VTR },
        [PIPE_FORMAT_R10G10B10A2_UNORM] 	= { MALI_RGB10_A2_UNORM, _VTR },
        [PIPE_FORMAT_B10G10R10A2_UNORM] 	= { MALI_RGB10_A2_UNORM, _VTR },
        [PIPE_FORMAT_R10G10B10X2_SNORM] 	= { MALI_RGB10_A2_SNORM, _VT },
        [PIPE_FORMAT_R10G10B10A2_SNORM] 	= { MALI_RGB10_A2_SNORM, _VT },
        [PIPE_FORMAT_B10G10R10A2_SNORM] 	= { MALI_RGB10_A2_SNORM, _VT },
        [PIPE_FORMAT_R10G10B10A2_UINT] 		= { MALI_RGB10_A2UI, _VTR },
        [PIPE_FORMAT_B10G10R10A2_UINT] 		= { MALI_RGB10_A2UI, _VTR },
        [PIPE_FORMAT_R10G10B10A2_USCALED] 	= { MALI_RGB10_A2UI, _VTR },
        [PIPE_FORMAT_B10G10R10A2_USCALED] 	= { MALI_RGB10_A2UI, _VTR },
        [PIPE_FORMAT_R10G10B10A2_SINT] 		= { MALI_RGB10_A2I, _VTR},
        [PIPE_FORMAT_B10G10R10A2_SINT] 		= { MALI_RGB10_A2I, _VTR },
        [PIPE_FORMAT_R10G10B10A2_SSCALED] 	= { MALI_RGB10_A2I, _VTR },
        [PIPE_FORMAT_B10G10R10A2_SSCALED] 	= { MALI_RGB10_A2I, _VTR },

        [PIPE_FORMAT_R8_SSCALED]		= { MALI_R8I, _V },
        [PIPE_FORMAT_R8G8_SSCALED]		= { MALI_RG8I, _V },
        [PIPE_FORMAT_R8G8B8_SSCALED]		= { MALI_RGB8I, _V },
        [PIPE_FORMAT_B8G8R8_SSCALED]		= { MALI_RGB8I, _V },
        [PIPE_FORMAT_R8G8B8A8_SSCALED]		= { MALI_RGBA8I, _V },
        [PIPE_FORMAT_B8G8R8A8_SSCALED]		= { MALI_RGBA8I, _V },
        [PIPE_FORMAT_A8B8G8R8_SSCALED]		= { MALI_RGBA8I, _V },

        [PIPE_FORMAT_R8_USCALED]		= { MALI_R8UI, _V },
        [PIPE_FORMAT_R8G8_USCALED]		= { MALI_RG8UI, _V },
        [PIPE_FORMAT_R8G8B8_USCALED]		= { MALI_RGB8UI, _V },
        [PIPE_FORMAT_B8G8R8_USCALED]		= { MALI_RGB8UI, _V },
        [PIPE_FORMAT_R8G8B8A8_USCALED]		= { MALI_RGBA8UI, _V },
        [PIPE_FORMAT_B8G8R8A8_USCALED]		= { MALI_RGBA8UI, _V },
        [PIPE_FORMAT_A8B8G8R8_USCALED]		= { MALI_RGBA8UI, _V },

        [PIPE_FORMAT_R16_USCALED]		= { MALI_R16UI, _V },
        [PIPE_FORMAT_R16G16_USCALED]		= { MALI_RG16UI, _V },
        [PIPE_FORMAT_R16G16B16_USCALED]		= { MALI_RGB16UI, _V },
        [PIPE_FORMAT_R16G16B16A16_USCALED]	= { MALI_RGBA16UI, _V },
        [PIPE_FORMAT_R16_SSCALED]		= { MALI_R16I, _V },
        [PIPE_FORMAT_R16G16_SSCALED]		= { MALI_RG16I, _V },
        [PIPE_FORMAT_R16G16B16_SSCALED]		= { MALI_RGB16I, _V },
        [PIPE_FORMAT_R16G16B16A16_SSCALED]	= { MALI_RGBA16I, _V },

        [PIPE_FORMAT_R32_USCALED]		= { MALI_R32UI, _V },
        [PIPE_FORMAT_R32G32_USCALED]		= { MALI_RG32UI, _V },
        [PIPE_FORMAT_R32G32B32_USCALED]		= { MALI_RGB32UI, _V },
        [PIPE_FORMAT_R32G32B32A32_USCALED]	= { MALI_RGBA32UI, _V },
        [PIPE_FORMAT_R32_SSCALED]		= { MALI_R32I, _V },
        [PIPE_FORMAT_R32G32_SSCALED]		= { MALI_RG32I, _V },
        [PIPE_FORMAT_R32G32B32_SSCALED]		= { MALI_RGB32I, _V },
        [PIPE_FORMAT_R32G32B32A32_SSCALED]	= { MALI_RGBA32I, _V },

        [PIPE_FORMAT_R3G3B2_UNORM] 		= { MALI_RGB332_UNORM, _VT },

        [PIPE_FORMAT_Z24_UNORM_S8_UINT]		= { MALI_Z24X8_UNORM, _TZ },
        [PIPE_FORMAT_Z24X8_UNORM]		= { MALI_Z24X8_UNORM, _TZ },
        [PIPE_FORMAT_Z32_FLOAT]		        = { MALI_R32F, _TZ },
        [PIPE_FORMAT_Z32_FLOAT_S8X24_UINT]	= { MALI_R32F, _TZ },

        [PIPE_FORMAT_R32_FIXED] 		= { MALI_R32_FIXED, _V },
        [PIPE_FORMAT_R32G32_FIXED] 		= { MALI_RG32_FIXED, _V },
        [PIPE_FORMAT_R32G32B32_FIXED] 		= { MALI_RGB32_FIXED, _V },
        [PIPE_FORMAT_R32G32B32A32_FIXED] 	= { MALI_RGBA32_FIXED, _V },

        [PIPE_FORMAT_R11G11B10_FLOAT] 		= { MALI_R11F_G11F_B10F, _VTR},
        [PIPE_FORMAT_R9G9B9E5_FLOAT] 		= { MALI_R9F_G9F_B9F_E5F, _VT },

        [PIPE_FORMAT_R8_SNORM] 			= { MALI_R8_SNORM, _VT },
        [PIPE_FORMAT_R16_SNORM] 		= { MALI_R16_SNORM, _VT },
        [PIPE_FORMAT_R32_SNORM] 		= { MALI_R32_SNORM, _VT },
        [PIPE_FORMAT_R8G8_SNORM] 		= { MALI_RG8_SNORM, _VT },
        [PIPE_FORMAT_R16G16_SNORM] 		= { MALI_RG16_SNORM, _VT },
        [PIPE_FORMAT_R32G32_SNORM] 		= { MALI_RG32_SNORM, _VT },
        [PIPE_FORMAT_R8G8B8_SNORM] 		= { MALI_RGB8_SNORM, _VT },
        [PIPE_FORMAT_R16G16B16_SNORM] 		= { MALI_RGB16_SNORM, _VT },
        [PIPE_FORMAT_R32G32B32_SNORM] 		= { MALI_RGB32_SNORM, _VT },
        [PIPE_FORMAT_R8G8B8A8_SNORM] 		= { MALI_RGBA8_SNORM, _VT },
        [PIPE_FORMAT_R16G16B16A16_SNORM] 	= { MALI_RGBA16_SNORM, _VT },
        [PIPE_FORMAT_R32G32B32A32_SNORM] 	= { MALI_RGBA32_SNORM, _VT },

        [PIPE_FORMAT_A8_SINT] 			= { MALI_R8I, _VTR },
        [PIPE_FORMAT_I8_SINT] 			= { MALI_R8I, _VTR },
        [PIPE_FORMAT_L8_SINT] 			= { MALI_R8I, _VTR },
        [PIPE_FORMAT_L8A8_SINT] 	        = { MALI_RG8I, _VTR },
        [PIPE_FORMAT_A8_UINT] 			= { MALI_R8UI, _VTR },
        [PIPE_FORMAT_I8_UINT] 			= { MALI_R8UI, _VTR },
        [PIPE_FORMAT_L8_UINT] 			= { MALI_R8UI, _VTR },
        [PIPE_FORMAT_L8A8_UINT] 	        = { MALI_RG8UI, _VTR },

        [PIPE_FORMAT_A16_SINT] 			= { MALI_R16I, _VTR },
        [PIPE_FORMAT_I16_SINT] 			= { MALI_R16I, _VTR },
        [PIPE_FORMAT_L16_SINT] 			= { MALI_R16I, _VTR },
        [PIPE_FORMAT_L16A16_SINT] 	        = { MALI_RG16I, _VTR },
        [PIPE_FORMAT_A16_UINT] 			= { MALI_R16UI, _VTR },
        [PIPE_FORMAT_I16_UINT] 			= { MALI_R16UI, _VTR },
        [PIPE_FORMAT_L16_UINT] 			= { MALI_R16UI, _VTR },
        [PIPE_FORMAT_L16A16_UINT] 	        = { MALI_RG16UI, _VTR },

        [PIPE_FORMAT_A32_SINT] 			= { MALI_R32I, _VTR },
        [PIPE_FORMAT_I32_SINT] 			= { MALI_R32I, _VTR },
        [PIPE_FORMAT_L32_SINT] 			= { MALI_R32I, _VTR },
        [PIPE_FORMAT_L32A32_SINT] 	        = { MALI_RG32I, _VTR },
        [PIPE_FORMAT_A32_UINT] 			= { MALI_R32UI, _VTR },
        [PIPE_FORMAT_I32_UINT] 			= { MALI_R32UI, _VTR },
        [PIPE_FORMAT_L32_UINT] 			= { MALI_R32UI, _VTR },
        [PIPE_FORMAT_L32A32_UINT] 	        = { MALI_RG32UI, _VTR },

        [PIPE_FORMAT_B8G8R8_UINT] 		= { MALI_RGB8UI, _VTR },
        [PIPE_FORMAT_B8G8R8A8_UINT] 		= { MALI_RGBA8UI, _VTR },
        [PIPE_FORMAT_B8G8R8_SINT] 		= { MALI_RGB8I, _VTR },
        [PIPE_FORMAT_B8G8R8A8_SINT] 		= { MALI_RGBA8I, _VTR },
        [PIPE_FORMAT_A8R8G8B8_UINT] 		= { MALI_RGBA8UI, _VTR },
        [PIPE_FORMAT_A8B8G8R8_UINT] 		= { MALI_RGBA8UI, _VTR },

        [PIPE_FORMAT_R8_UINT] 			= { MALI_R8UI, _VTR },
        [PIPE_FORMAT_R16_UINT] 			= { MALI_R16UI, _VTR },
        [PIPE_FORMAT_R32_UINT] 			= { MALI_R32UI, _VTR },
        [PIPE_FORMAT_R8G8_UINT] 		= { MALI_RG8UI, _VTR },
        [PIPE_FORMAT_R16G16_UINT] 		= { MALI_RG16UI, _VTR },
        [PIPE_FORMAT_R32G32_UINT] 		= { MALI_RG32UI, _VTR },
        [PIPE_FORMAT_R8G8B8_UINT] 		= { MALI_RGB8UI, _VTR },
        [PIPE_FORMAT_R16G16B16_UINT] 		= { MALI_RGB16UI, _VTR },
        [PIPE_FORMAT_R32G32B32_UINT] 		= { MALI_RGB32UI, _VTR },
        [PIPE_FORMAT_R8G8B8A8_UINT] 		= { MALI_RGBA8UI, _VTR },
        [PIPE_FORMAT_R16G16B16A16_UINT] 	= { MALI_RGBA16UI, _VTR },
        [PIPE_FORMAT_R32G32B32A32_UINT] 	= { MALI_RGBA32UI, _VTR },

        [PIPE_FORMAT_R32_FLOAT] 		= { MALI_R32F, _VTR },
        [PIPE_FORMAT_R32G32_FLOAT] 		= { MALI_RG32F, _VTR },
        [PIPE_FORMAT_R32G32B32_FLOAT] 		= { MALI_RGB32F, _VTR },
        [PIPE_FORMAT_R32G32B32A32_FLOAT] 	= { MALI_RGBA32F, _VTR },

        [PIPE_FORMAT_R8_UNORM] 			= { MALI_R8_UNORM, _VTR },
        [PIPE_FORMAT_R16_UNORM] 		= { MALI_R16_UNORM, _VTR },
        [PIPE_FORMAT_R32_UNORM] 		= { MALI_R32_UNORM, _VTR },
        [PIPE_FORMAT_R8G8_UNORM] 		= { MALI_RG8_UNORM, _VTR },
        [PIPE_FORMAT_R16G16_UNORM] 		= { MALI_RG16_UNORM, _VTR },
        [PIPE_FORMAT_R32G32_UNORM] 		= { MALI_RG32_UNORM, _VTR },
        [PIPE_FORMAT_R8G8B8_UNORM] 		= { MALI_RGB8_UNORM, _VTR },
        [PIPE_FORMAT_R16G16B16_UNORM] 		= { MALI_RGB16_UNORM, _VTR },
        [PIPE_FORMAT_R32G32B32_UNORM] 		= { MALI_RGB32_UNORM, _VTR },
        [PIPE_FORMAT_R4G4B4A4_UNORM] 		= { MALI_RGBA4_UNORM, _VTR },
        [PIPE_FORMAT_R16G16B16A16_UNORM] 	= { MALI_RGBA16_UNORM, _VTR },
        [PIPE_FORMAT_R32G32B32A32_UNORM] 	= { MALI_RGBA32_UNORM, _VTR },

        [PIPE_FORMAT_B8G8R8A8_UNORM] 		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_B8G8R8X8_UNORM] 		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_A8R8G8B8_UNORM] 		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_X8R8G8B8_UNORM] 		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_A8B8G8R8_UNORM] 		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_X8B8G8R8_UNORM] 		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_R8G8B8X8_UNORM] 		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_R8G8B8A8_UNORM] 		= { MALI_RGBA8_UNORM, _VTR },

        [PIPE_FORMAT_R8G8B8X8_SNORM] 		= { MALI_RGBA8_SNORM, _VT },
        [PIPE_FORMAT_R8G8B8X8_SRGB] 		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_R8G8B8X8_UINT] 		= { MALI_RGBA8UI, _VTR },
        [PIPE_FORMAT_R8G8B8X8_SINT] 		= { MALI_RGBA8I, _VTR },

        [PIPE_FORMAT_L8_UNORM]		        = { MALI_R8_UNORM, _VTR },
        [PIPE_FORMAT_A8_UNORM]		        = { MALI_R8_UNORM, _VTR },
        [PIPE_FORMAT_I8_UNORM]		        = { MALI_R8_UNORM, _VTR },
        [PIPE_FORMAT_L8A8_UNORM]	       	= { MALI_RG8_UNORM, _VTR },
        [PIPE_FORMAT_L16_UNORM]		        = { MALI_R16_UNORM, _VTR },
        [PIPE_FORMAT_A16_UNORM]		        = { MALI_R16_UNORM, _VTR },
        [PIPE_FORMAT_I16_UNORM]		        = { MALI_R16_UNORM, _VTR },
        [PIPE_FORMAT_L16A16_UNORM]	       	= { MALI_RG16_UNORM, _VTR },

        [PIPE_FORMAT_L8_SNORM]		        = { MALI_R8_SNORM, _VT },
        [PIPE_FORMAT_A8_SNORM]		        = { MALI_R8_SNORM, _VT },
        [PIPE_FORMAT_I8_SNORM]		        = { MALI_R8_SNORM, _VT },
        [PIPE_FORMAT_L8A8_SNORM]	       	= { MALI_RG8_SNORM, _VT },
        [PIPE_FORMAT_L16_SNORM]		        = { MALI_R16_SNORM, _VT },
        [PIPE_FORMAT_A16_SNORM]		        = { MALI_R16_SNORM, _VT },
        [PIPE_FORMAT_I16_SNORM]		        = { MALI_R16_SNORM, _VT },
        [PIPE_FORMAT_L16A16_SNORM]	       	= { MALI_RG16_SNORM, _VT },

        [PIPE_FORMAT_L16_FLOAT]		        = { MALI_R16F, _VTR },
        [PIPE_FORMAT_A16_FLOAT]		        = { MALI_R16F, _VTR },
        [PIPE_FORMAT_I16_FLOAT]		        = { MALI_RG16F, _VTR },
        [PIPE_FORMAT_L16A16_FLOAT]	       	= { MALI_RG16F, _VTR },

        [PIPE_FORMAT_L8_SRGB]		        = { MALI_R8_UNORM, _VTR },
        [PIPE_FORMAT_R8_SRGB]		        = { MALI_R8_UNORM, _VTR },
        [PIPE_FORMAT_L8A8_SRGB]		        = { MALI_RG8_UNORM, _VTR },
        [PIPE_FORMAT_R8G8_SRGB]		        = { MALI_RG8_UNORM, _VTR },
        [PIPE_FORMAT_R8G8B8_SRGB]		= { MALI_RGB8_UNORM, _VTR },
        [PIPE_FORMAT_B8G8R8_SRGB]		= { MALI_RGB8_UNORM, _VTR },
        [PIPE_FORMAT_R8G8B8A8_SRGB]		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_A8B8G8R8_SRGB]		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_X8B8G8R8_SRGB]		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_B8G8R8A8_SRGB]		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_B8G8R8X8_SRGB]		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_A8R8G8B8_SRGB]		= { MALI_RGBA8_UNORM, _VTR },
        [PIPE_FORMAT_X8R8G8B8_SRGB]		= { MALI_RGBA8_UNORM, _VTR },

        [PIPE_FORMAT_R8_SINT] 			= { MALI_R8I, _VTR },
        [PIPE_FORMAT_R16_SINT] 			= { MALI_R16I, _VTR },
        [PIPE_FORMAT_R32_SINT] 			= { MALI_R32I, _VTR },
        [PIPE_FORMAT_R16_FLOAT] 		= { MALI_R16F, _VTR },
        [PIPE_FORMAT_R8G8_SINT] 		= { MALI_RG8I, _VTR },
        [PIPE_FORMAT_R16G16_SINT] 		= { MALI_RG16I, _VTR },
        [PIPE_FORMAT_R32G32_SINT] 		= { MALI_RG32I, _VTR },
        [PIPE_FORMAT_R16G16_FLOAT] 		= { MALI_RG16F, _VTR },
        [PIPE_FORMAT_R8G8B8_SINT] 		= { MALI_RGB8I, _VTR },
        [PIPE_FORMAT_R16G16B16_SINT] 		= { MALI_RGB16I, _VTR },
        [PIPE_FORMAT_R32G32B32_SINT] 		= { MALI_RGB32I, _VTR },
        [PIPE_FORMAT_R16G16B16_FLOAT] 		= { MALI_RGB16F, _VTR },
        [PIPE_FORMAT_R8G8B8A8_SINT] 		= { MALI_RGBA8I, _VTR },
        [PIPE_FORMAT_R16G16B16A16_SINT] 	= { MALI_RGBA16I, _VTR },
        [PIPE_FORMAT_R32G32B32A32_SINT] 	= { MALI_RGBA32I, _VTR },
        [PIPE_FORMAT_R16G16B16A16_FLOAT] 	= { MALI_RGBA16F, _VTR },

        [PIPE_FORMAT_R16G16B16X16_UNORM] 	= { MALI_RGBA16_UNORM, _VTR },
        [PIPE_FORMAT_R16G16B16X16_SNORM] 	= { MALI_RGBA16_SNORM, _VT },
        [PIPE_FORMAT_R16G16B16X16_FLOAT] 	= { MALI_RGBA16F, _VTR },
        [PIPE_FORMAT_R16G16B16X16_UINT] 	= { MALI_RGBA16UI, _VTR },
        [PIPE_FORMAT_R16G16B16X16_SINT] 	= { MALI_RGBA16I, _VTR },

        [PIPE_FORMAT_R32G32B32X32_FLOAT] 	= { MALI_RGBA32F, _VTR },
        [PIPE_FORMAT_R32G32B32X32_UINT] 	= { MALI_RGBA32UI, _VTR },
        [PIPE_FORMAT_R32G32B32X32_SINT] 	= { MALI_RGBA32I, _VTR },
};

#undef _VTR
#undef _VT
#undef _V
#undef _T
#undef _R

/* Is a format encoded like Z24S8 and therefore compatible for render? */

bool
panfrost_is_z24s8_variant(enum pipe_format fmt)
{
        switch (fmt) {
                case PIPE_FORMAT_Z24_UNORM_S8_UINT:
                case PIPE_FORMAT_Z24X8_UNORM:
                        return true;
                default:
                        return false;
        }
}

/* Translate a PIPE swizzle quad to a 12-bit Mali swizzle code. PIPE
 * swizzles line up with Mali swizzles for the XYZW01, but PIPE swizzles have
 * an additional "NONE" field that we have to mask out to zero. Additionally,
 * PIPE swizzles are sparse but Mali swizzles are packed */

unsigned
panfrost_translate_swizzle_4(const unsigned char swizzle[4])
{
        unsigned out = 0;

        for (unsigned i = 0; i < 4; ++i) {
                unsigned translated = (swizzle[i] > PIPE_SWIZZLE_1) ? PIPE_SWIZZLE_0 : swizzle[i];
                out |= (translated << (3*i));
        }

        return out;
}

void
panfrost_invert_swizzle(const unsigned char *in, unsigned char *out)
{
        /* First, default to all zeroes to prevent uninitialized junk */

        for (unsigned c = 0; c < 4; ++c)
                out[c] = PIPE_SWIZZLE_0;

        /* Now "do" what the swizzle says */

        for (unsigned c = 0; c < 4; ++c) {
                unsigned char i = in[c];

                /* Who cares? */
                assert(PIPE_SWIZZLE_X == 0);
                if (i > PIPE_SWIZZLE_W)
                        continue;

                /* Invert */
                unsigned idx = i - PIPE_SWIZZLE_X;
                out[idx] = PIPE_SWIZZLE_X + c;
        }
}

enum mali_format
panfrost_format_to_bifrost_blend(const struct util_format_description *desc)
{
        enum mali_format format = panfrost_pipe_format_table[desc->format].hw;
        assert(format);

        switch (format) {
        case MALI_RGBA4_UNORM:
                return MALI_RGBA4;
        case MALI_RGBA8_UNORM:
        case MALI_RGB8_UNORM:
                return MALI_RGBA8_2;
        case MALI_RGB10_A2_UNORM:
                return MALI_RGB10_A2_2;
        default:
                return format;
        }
}
