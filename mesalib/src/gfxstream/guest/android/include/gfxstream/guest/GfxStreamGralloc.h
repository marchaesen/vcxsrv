/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if defined(ANDROID)

#include <cutils/native_handle.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>

typedef struct AHardwareBuffer AHardwareBuffer;

namespace gfxstream {

constexpr uint32_t kGlRGB = 0x1907;
constexpr uint32_t kGlRGBA = 0x1908;
constexpr uint32_t kGlRGB565 = 0x8D62;

// Enums mirrored from Android to avoid extra build dependencies.
enum {
    GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM           = 1,
    GFXSTREAM_AHB_FORMAT_R8G8B8X8_UNORM           = 2,
    GFXSTREAM_AHB_FORMAT_R8G8B8_UNORM             = 3,
    GFXSTREAM_AHB_FORMAT_R5G6B5_UNORM             = 4,
    GFXSTREAM_AHB_FORMAT_B8G8R8A8_UNORM           = 5,
    GFXSTREAM_AHB_FORMAT_B5G5R5A1_UNORM           = 6,
    GFXSTREAM_AHB_FORMAT_B4G4R4A4_UNORM           = 7,
    GFXSTREAM_AHB_FORMAT_R16G16B16A16_FLOAT       = 0x16,
    GFXSTREAM_AHB_FORMAT_R10G10B10A2_UNORM        = 0x2b,
    GFXSTREAM_AHB_FORMAT_BLOB                     = 0x21,
    GFXSTREAM_AHB_FORMAT_D16_UNORM                = 0x30,
    GFXSTREAM_AHB_FORMAT_D24_UNORM                = 0x31,
    GFXSTREAM_AHB_FORMAT_D24_UNORM_S8_UINT        = 0x32,
    GFXSTREAM_AHB_FORMAT_D32_FLOAT                = 0x33,
    GFXSTREAM_AHB_FORMAT_D32_FLOAT_S8_UINT        = 0x34,
    GFXSTREAM_AHB_FORMAT_S8_UINT                  = 0x35,
    GFXSTREAM_AHB_FORMAT_Y8Cb8Cr8_420             = 0x23,
    GFXSTREAM_AHB_FORMAT_YV12                     = 0x32315659,
    GFXSTREAM_AHB_FORMAT_IMPLEMENTATION_DEFINED   = 0x22,
    GFXSTREAM_AHB_FORMAT_R8_UNORM                 = 0x38,
};

enum GfxstreamAhbDataspace {
    GFXSTREAM_AHB_DATASPACE_UNKNOWN = 0,
    GFXSTREAM_AHB_DATASPACE_ARBITRARY = 1,
    GFXSTREAM_AHB_DATASPACE_STANDARD_SHIFT = 16,
    GFXSTREAM_AHB_DATASPACE_STANDARD_MASK = 4128768,                      // (63 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_UNSPECIFIED = 0,                     // (0 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_BT709 = 65536,                       // (1 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_BT601_625 = 131072,                  // (2 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_BT601_625_UNADJUSTED = 196608,       // (3 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_BT601_525 = 262144,                  // (4 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_BT601_525_UNADJUSTED = 327680,       // (5 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_BT2020 = 393216,                     // (6 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE = 458752,  // (7 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_BT470M = 524288,                     // (8 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_FILM = 589824,                       // (9 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_DCI_P3 = 655360,                     // (10 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_STANDARD_ADOBE_RGB = 720896,                  // (11 << STANDARD_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_SHIFT = 22,
    GFXSTREAM_AHB_DATASPACE_TRANSFER_MASK = 130023424,       // (31 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_UNSPECIFIED = 0,        // (0 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_LINEAR = 4194304,       // (1 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_SRGB = 8388608,         // (2 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_SMPTE_170M = 12582912,  // (3 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_GAMMA2_2 = 16777216,    // (4 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_GAMMA2_6 = 20971520,    // (5 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_GAMMA2_8 = 25165824,    // (6 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_ST2084 = 29360128,      // (7 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_TRANSFER_HLG = 33554432,         // (8 << TRANSFER_SHIFT)
    GFXSTREAM_AHB_DATASPACE_RANGE_SHIFT = 27,
    GFXSTREAM_AHB_DATASPACE_RANGE_MASK = 939524096,      // (7 << RANGE_SHIFT)
    GFXSTREAM_AHB_DATASPACE_RANGE_UNSPECIFIED = 0,       // (0 << RANGE_SHIFT)
    GFXSTREAM_AHB_DATASPACE_RANGE_FULL = 134217728,      // (1 << RANGE_SHIFT)
    GFXSTREAM_AHB_DATASPACE_RANGE_LIMITED = 268435456,   // (2 << RANGE_SHIFT)
    GFXSTREAM_AHB_DATASPACE_RANGE_EXTENDED = 402653184,  // (3 << RANGE_SHIFT)
    GFXSTREAM_AHB_DATASPACE_SRGB_LINEAR = 512,
    GFXSTREAM_AHB_DATASPACE_V0_SRGB_LINEAR =
        138477568,  // ((STANDARD_BT709 | TRANSFER_LINEAR) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_V0_SCRGB_LINEAR =
        406913024,  // ((STANDARD_BT709 | TRANSFER_LINEAR) | RANGE_EXTENDED)
    GFXSTREAM_AHB_DATASPACE_SRGB = 513,
    GFXSTREAM_AHB_DATASPACE_V0_SRGB = 142671872,  // ((STANDARD_BT709 | TRANSFER_SRGB) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_V0_SCRGB =
        411107328,  // ((STANDARD_BT709 | TRANSFER_SRGB) | RANGE_EXTENDED)
    GFXSTREAM_AHB_DATASPACE_JFIF = 257,
    GFXSTREAM_AHB_DATASPACE_V0_JFIF =
        146931712,  // ((STANDARD_BT601_625 | TRANSFER_SMPTE_170M) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_BT601_625 = 258,
    GFXSTREAM_AHB_DATASPACE_V0_BT601_625 =
        281149440,  // ((STANDARD_BT601_625 | TRANSFER_SMPTE_170M) | RANGE_LIMITED)
    GFXSTREAM_AHB_DATASPACE_BT601_525 = 259,
    GFXSTREAM_AHB_DATASPACE_V0_BT601_525 =
        281280512,  // ((STANDARD_BT601_525 | TRANSFER_SMPTE_170M) | RANGE_LIMITED)
    GFXSTREAM_AHB_DATASPACE_BT709 = 260,
    GFXSTREAM_AHB_DATASPACE_V0_BT709 =
        281083904,  // ((STANDARD_BT709 | TRANSFER_SMPTE_170M) | RANGE_LIMITED)
    GFXSTREAM_AHB_DATASPACE_DCI_P3_LINEAR =
        139067392,  // ((STANDARD_DCI_P3 | TRANSFER_LINEAR) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_DCI_P3 =
        155844608,  // ((STANDARD_DCI_P3 | TRANSFER_GAMMA2_6) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_DISPLAY_P3_LINEAR =
        139067392,  // ((STANDARD_DCI_P3 | TRANSFER_LINEAR) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_DISPLAY_P3 =
        143261696,  // ((STANDARD_DCI_P3 | TRANSFER_SRGB) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_ADOBE_RGB =
        151715840,  // ((STANDARD_ADOBE_RGB | TRANSFER_GAMMA2_2) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_BT2020_LINEAR =
        138805248,  // ((STANDARD_BT2020 | TRANSFER_LINEAR) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_BT2020 =
        147193856,  // ((STANDARD_BT2020 | TRANSFER_SMPTE_170M) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_BT2020_PQ =
        163971072,  // ((STANDARD_BT2020 | TRANSFER_ST2084) | RANGE_FULL)
    GFXSTREAM_AHB_DATASPACE_DEPTH = 4096,
    GFXSTREAM_AHB_DATASPACE_SENSOR = 4097,
};

enum GrallocType {
    GRALLOC_TYPE_GOLDFISH = 1,
    GRALLOC_TYPE_MINIGBM = 2,
    GRALLOC_TYPE_EMULATED = 3,
};

// Abstraction for gralloc handle conversion
class Gralloc {
   public:
    virtual ~Gralloc() {}

    virtual GrallocType getGrallocType() = 0;
    virtual uint32_t createColorBuffer(int width, int height, uint32_t glformat) = 0;

    virtual void acquire(AHardwareBuffer* ahb) = 0;
    virtual void release(AHardwareBuffer* ahb) = 0;

    virtual int allocate(uint32_t width, uint32_t height, uint32_t ahbFormat, uint64_t usage,
                         AHardwareBuffer** outputAhb) = 0;

    virtual int lock(AHardwareBuffer* ahb, uint8_t** ptr) = 0;
    struct LockedPlane {
        uint8_t* data = nullptr;
        uint32_t pixelStrideBytes = 0;
        uint32_t rowStrideBytes = 0;
    };
    // If AHB is a YUV format, always returns Y, then U, then V.
    virtual int lockPlanes(AHardwareBuffer* ahb, std::vector<LockedPlane>* ahbPlanes) = 0;
    virtual int unlock(AHardwareBuffer* ahb) = 0;

    virtual const native_handle_t* getNativeHandle(const AHardwareBuffer* ahb) = 0;

    virtual uint32_t getHostHandle(const native_handle_t* handle) = 0;
    virtual uint32_t getHostHandle(const AHardwareBuffer* handle) = 0;

    virtual int getFormat(const native_handle_t* handle) = 0;
    virtual int getFormat(const AHardwareBuffer* handle) = 0;

    virtual uint32_t getFormatDrmFourcc(const AHardwareBuffer* /*handle*/) {
        // Equal to DRM_FORMAT_INVALID -- see <drm_fourcc.h>
        return 0;
    }
    virtual uint32_t getFormatDrmFourcc(const native_handle_t* /*handle*/) {
        // Equal to DRM_FORMAT_INVALID -- see <drm_fourcc.h>
        return 0;
    }

    virtual uint32_t getWidth(const AHardwareBuffer* ahb) = 0;
    virtual uint32_t getHeight(const AHardwareBuffer* ahb) = 0;

    virtual size_t getAllocatedSize(const native_handle_t* handle) = 0;
    virtual size_t getAllocatedSize(const AHardwareBuffer* handle) = 0;

    virtual int getId(const AHardwareBuffer* ahb, uint64_t* id) = 0;

    virtual bool treatBlobAsImage() { return false; }

    virtual int32_t getDataspace(const AHardwareBuffer* ahb) {
        return GFXSTREAM_AHB_DATASPACE_UNKNOWN;
    }
};

Gralloc* createPlatformGralloc(int32_t descriptor = -1);

}  // namespace gfxstream

#endif  // defined(ANDROID)
