/*
 * Copyright 2016 Intel
 * SPDX-License-Identifier: MIT
 */
#ifndef VK_FORMAT_INFO_H
#define VK_FORMAT_INFO_H

#include <stdbool.h>
#include <drm_fourcc.h>
#define DRM_FORMAT_YVU420_ANDROID fourcc_code('9', '9', '9', '7')
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#include <system/graphics.h>
#else
/* See system/graphics.h. */
enum {
    HAL_PIXEL_FORMAT_YV12 = 842094169,
};
#endif

#if !defined(__INTRODUCED_IN)
#define __INTRODUCED_IN(__api_level) /* nothing */
#endif
#include <vndk/hardware_buffer.h>
#include <vulkan/vulkan.h>
#include "util/log.h"

namespace gfxstream {
namespace vk {

/* See i915_private_android_types.h in minigbm. */
#define HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL 0x100

// TODO(b/167698976): We should not use OMX_COLOR_FormatYUV420Planar
// but we seem to miss a format translation somewhere.

#define OMX_COLOR_FormatYUV420Planar 0x13

static inline VkFormat vk_format_from_fourcc(unsigned fourcc_format) {
    switch (fourcc_format) {
        case DRM_FORMAT_R8:
            return VK_FORMAT_R8_UNORM;
        case DRM_FORMAT_ABGR8888:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case DRM_FORMAT_XBGR8888:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case DRM_FORMAT_BGR888:
            return VK_FORMAT_R8G8B8_UNORM;
        case DRM_FORMAT_RGB565:
            return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case DRM_FORMAT_ABGR16161616F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case DRM_FORMAT_ABGR2101010:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case DRM_FORMAT_P010:
            return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
        case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
            return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU420_ANDROID:
        case DRM_FORMAT_YVU420:
            return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

static inline unsigned android_format_from_vk(VkFormat vk_format) {
    switch (vk_format) {
        case VK_FORMAT_R8_UNORM:
            return AHARDWAREBUFFER_FORMAT_R8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8_UNORM:
            return AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
            return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
            return HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL;
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
            return HAL_PIXEL_FORMAT_YV12;
        case VK_FORMAT_D16_UNORM:
            return AHARDWAREBUFFER_FORMAT_D16_UNORM;
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return AHARDWAREBUFFER_FORMAT_D24_UNORM;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT;
        case VK_FORMAT_D32_SFLOAT:
            return AHARDWAREBUFFER_FORMAT_D32_FLOAT;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT;
        default:
            return AHARDWAREBUFFER_FORMAT_BLOB;
    }
}

static inline bool android_format_is_yuv(unsigned android_format) {
    switch (android_format) {
        case AHARDWAREBUFFER_FORMAT_BLOB:
        case AHARDWAREBUFFER_FORMAT_R8_UNORM:
        case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
        case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
        case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
        case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
        case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
        case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:
        case AHARDWAREBUFFER_FORMAT_D16_UNORM:
        case AHARDWAREBUFFER_FORMAT_D24_UNORM:
        case AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT:
        case AHARDWAREBUFFER_FORMAT_D32_FLOAT:
        case AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT:
        case AHARDWAREBUFFER_FORMAT_S8_UINT:
            return false;
        case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
        case OMX_COLOR_FormatYUV420Planar:
        case HAL_PIXEL_FORMAT_YV12:
#if __ANDROID_API__ >= 30
        case AHARDWAREBUFFER_FORMAT_YCbCr_P010:
#endif
        case AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:
            return true;
        default:
            mesa_loge("%s: unhandled format: %d", __FUNCTION__, android_format);
            return false;
    }
}

static inline VkImageAspectFlags vk_format_aspects(VkFormat format) {
    switch (format) {
        case VK_FORMAT_UNDEFINED:
            return 0;

        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;

        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;

        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
            return (VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT |
                    VK_IMAGE_ASPECT_PLANE_2_BIT);

        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
            return (VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT);

        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static inline bool vk_format_is_color(VkFormat format) {
    return vk_format_aspects(format) == VK_IMAGE_ASPECT_COLOR_BIT;
}

static inline bool vk_format_is_depth_or_stencil(VkFormat format) {
    const VkImageAspectFlags aspects = vk_format_aspects(format);
    return aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
}

static inline bool vk_format_has_depth(VkFormat format) {
    const VkImageAspectFlags aspects = vk_format_aspects(format);
    return aspects & VK_IMAGE_ASPECT_DEPTH_BIT;
}

}  // namespace vk
}  // namespace gfxstream

#endif /* VK_FORMAT_INFO_H */
