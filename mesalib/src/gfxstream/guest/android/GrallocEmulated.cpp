/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "GrallocEmulated.h"

#include <optional>
#include <unordered_map>

#include "drm_fourcc.h"
#include "util/log.h"

namespace gfxstream {
namespace {

static constexpr int numFds = 0;
static constexpr int numInts = 1;

#define DRM_FORMAT_R8_BLOB fourcc_code('9', '9', '9', '9')

template <typename T, typename N>
T DivideRoundUp(T n, N divisor) {
    const T div = static_cast<T>(divisor);
    const T q = n / div;
    return n % div == 0 ? q : q + 1;
}

template <typename T, typename N>
T Align(T number, N n) {
    return DivideRoundUp(number, n) * n;
}

std::optional<uint32_t> GlFormatToDrmFormat(uint32_t glFormat) {
    switch (glFormat) {
        case kGlRGB:
            return DRM_FORMAT_BGR888;
        case kGlRGB565:
            return DRM_FORMAT_BGR565;
        case kGlRGBA:
            return DRM_FORMAT_ABGR8888;
    }
    return std::nullopt;
}

std::optional<uint32_t> AhbToDrmFormat(uint32_t ahbFormat) {
    switch (ahbFormat) {
        case GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM:
            return DRM_FORMAT_ABGR8888;
        case GFXSTREAM_AHB_FORMAT_R8G8B8X8_UNORM:
            return DRM_FORMAT_XBGR8888;
        case GFXSTREAM_AHB_FORMAT_R8G8B8_UNORM:
            return DRM_FORMAT_BGR888;
        /*
        * Confusingly, AHARDWAREBUFFER_FORMAT_RGB_565 is defined as:
        *
        * "16-bit packed format that has 5-bit R, 6-bit G, and 5-bit B components, in that
        *  order, from the  most-sigfinicant bits to the least-significant bits."
        *
        * so the order of the components is intentionally not flipped between the pixel
        * format and the DRM format.
        */
        case GFXSTREAM_AHB_FORMAT_R5G6B5_UNORM:
            return DRM_FORMAT_RGB565;
        case GFXSTREAM_AHB_FORMAT_B8G8R8A8_UNORM:
            return DRM_FORMAT_ARGB8888;
        case GFXSTREAM_AHB_FORMAT_BLOB:
            return DRM_FORMAT_R8_BLOB;
        case GFXSTREAM_AHB_FORMAT_R8_UNORM:
            return DRM_FORMAT_R8;
        case GFXSTREAM_AHB_FORMAT_YV12:
            return DRM_FORMAT_YVU420;
        case GFXSTREAM_AHB_FORMAT_R16G16B16A16_FLOAT:
            return DRM_FORMAT_ABGR16161616F;
        case GFXSTREAM_AHB_FORMAT_R10G10B10A2_UNORM:
            return DRM_FORMAT_ABGR2101010;
        case GFXSTREAM_AHB_FORMAT_Y8Cb8Cr8_420:
            return DRM_FORMAT_NV12;
    }
    return std::nullopt;
}

struct DrmFormatPlaneInfo {
    uint32_t horizontalSubsampling;
    uint32_t verticalSubsampling;
    uint32_t bytesPerPixel;
};
struct DrmFormatInfo {
    uint32_t androidFormat;
    uint32_t virglFormat;
    bool isYuv;
    uint32_t horizontalAlignmentPixels;
    uint32_t verticalAlignmentPixels;
    std::vector<DrmFormatPlaneInfo> planes;
};
const std::unordered_map<uint32_t, DrmFormatInfo>& GetDrmFormatInfoMap() {
    static const auto* kFormatInfoMap = new std::unordered_map<uint32_t, DrmFormatInfo>({
        {DRM_FORMAT_ABGR8888,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM,
             .virglFormat = VIRGL_FORMAT_R8G8B8A8_UNORM,
             .isYuv = false,
             .horizontalAlignmentPixels = 1,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 4,
                     },
                 },
         }},
        {DRM_FORMAT_ARGB8888,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_B8G8R8A8_UNORM,
             .virglFormat = VIRGL_FORMAT_B8G8R8A8_UNORM,
             .isYuv = false,
             .horizontalAlignmentPixels = 1,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 4,
                     },
                 },
         }},
        {DRM_FORMAT_BGR888,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_R8G8B8_UNORM,
             .virglFormat = VIRGL_FORMAT_R8G8B8_UNORM,
             .isYuv = false,
             .horizontalAlignmentPixels = 1,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 3,
                     },
                 },
         }},
        {DRM_FORMAT_BGR565,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_R5G6B5_UNORM,
             .virglFormat = VIRGL_FORMAT_B5G6R5_UNORM,
             .isYuv = false,
             .horizontalAlignmentPixels = 1,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 2,
                     },
                 },
         }},
        {DRM_FORMAT_R8,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_R8_UNORM,
             .virglFormat = VIRGL_FORMAT_R8_UNORM,
             .isYuv = false,
             .horizontalAlignmentPixels = 1,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 1,
                     },
                 },
         }},
        {DRM_FORMAT_R8_BLOB,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_BLOB,
             .virglFormat = VIRGL_FORMAT_R8_UNORM,
             .isYuv = false,
             .horizontalAlignmentPixels = 1,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 1,
                     },
                 },
         }},
        {DRM_FORMAT_ABGR16161616F,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_R16G16B16A16_FLOAT,
             .virglFormat = VIRGL_FORMAT_R16G16B16A16_FLOAT,
             .isYuv = false,
             .horizontalAlignmentPixels = 1,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 8,
                     },
                 },
         }},
        {DRM_FORMAT_ABGR2101010,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_R10G10B10A2_UNORM,
             .virglFormat = VIRGL_FORMAT_R10G10B10A2_UNORM,
             .isYuv = false,
             .horizontalAlignmentPixels = 1,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 4,
                     },
                 },
         }},
        {DRM_FORMAT_NV12,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_Y8Cb8Cr8_420,
             .virglFormat = VIRGL_FORMAT_NV12,
             .isYuv = true,
             .horizontalAlignmentPixels = 2,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 1,
                     },
                     {
                         .horizontalSubsampling = 2,
                         .verticalSubsampling = 2,
                         .bytesPerPixel = 2,
                     },
                 },
         }},
        {DRM_FORMAT_YVU420,
         {
             .androidFormat = GFXSTREAM_AHB_FORMAT_YV12,
             .virglFormat = VIRGL_FORMAT_YV12,
             .isYuv = true,
             .horizontalAlignmentPixels = 32,
             .verticalAlignmentPixels = 1,
             .planes =
                 {
                     {
                         .horizontalSubsampling = 1,
                         .verticalSubsampling = 1,
                         .bytesPerPixel = 1,
                     },
                     {
                         .horizontalSubsampling = 2,
                         .verticalSubsampling = 2,
                         .bytesPerPixel = 1,
                     },
                     {
                         .horizontalSubsampling = 2,
                         .verticalSubsampling = 2,
                         .bytesPerPixel = 1,
                     },
                 },
         }},
    });
    return *kFormatInfoMap;
}

}  // namespace

EmulatedAHardwareBuffer::EmulatedAHardwareBuffer(uint32_t width, uint32_t height,
                                                 uint32_t drmFormat, VirtGpuResourcePtr resource)
    : mRefCount(1), mWidth(width), mHeight(height), mDrmFormat(drmFormat), mResource(resource) {}

EmulatedAHardwareBuffer::~EmulatedAHardwareBuffer() {}

uint32_t EmulatedAHardwareBuffer::getResourceId() const { return mResource->getResourceHandle(); }

uint32_t EmulatedAHardwareBuffer::getWidth() const { return mWidth; }

uint32_t EmulatedAHardwareBuffer::getHeight() const { return mHeight; }

int EmulatedAHardwareBuffer::getAndroidFormat() const {
    const auto& formatInfosMap = GetDrmFormatInfoMap();
    auto formatInfoIt = formatInfosMap.find(mDrmFormat);
    if (formatInfoIt == formatInfosMap.end()) {
        mesa_loge("Unhandled DRM format:%u", mDrmFormat);
        return -1;
    }
    const auto& formatInfo = formatInfoIt->second;
    return formatInfo.androidFormat;
}

uint32_t EmulatedAHardwareBuffer::getDrmFormat() const { return mDrmFormat; }

AHardwareBuffer* EmulatedAHardwareBuffer::asAHardwareBuffer() {
    return reinterpret_cast<AHardwareBuffer*>(this);
}

buffer_handle_t EmulatedAHardwareBuffer::asBufferHandle() {
    return reinterpret_cast<buffer_handle_t>(this);
}

EGLClientBuffer EmulatedAHardwareBuffer::asEglClientBuffer() {
    return reinterpret_cast<EGLClientBuffer>(this);
}

void EmulatedAHardwareBuffer::acquire() { ++mRefCount; }

void EmulatedAHardwareBuffer::release() {
    --mRefCount;
    if (mRefCount == 0) {
        delete this;
    }
}

int EmulatedAHardwareBuffer::lock(uint8_t** ptr) {
    if (!mMapped) {
        mMapped = mResource->createMapping();
        if (!mMapped) {
            mesa_loge("Failed to lock EmulatedAHardwareBuffer: failed to create mapping.");
            return -1;
        }

        mResource->transferFromHost(0, 0, mWidth, mHeight);
        mResource->wait();
    }

    *ptr = (*mMapped)->asRawPtr();
    return 0;
}

int EmulatedAHardwareBuffer::lockPlanes(std::vector<Gralloc::LockedPlane>* ahbPlanes) {
    uint8_t* data = 0;
    int ret = lock(&data);
    if (ret) {
        return ret;
    }

    const auto& formatInfosMap = GetDrmFormatInfoMap();
    auto formatInfoIt = formatInfosMap.find(mDrmFormat);
    if (formatInfoIt == formatInfosMap.end()) {
        mesa_loge("Failed to lock: failed to find format info for drm format:%u", mDrmFormat);
        return -1;
    }
    const auto& formatInfo = formatInfoIt->second;

    const uint32_t alignedWidth = Align(mWidth, formatInfo.horizontalAlignmentPixels);
    const uint32_t alignedHeight = Align(mHeight, formatInfo.verticalAlignmentPixels);
    uint32_t cumulativeSize = 0;
    for (const DrmFormatPlaneInfo& planeInfo : formatInfo.planes) {
        const uint32_t planeWidth = DivideRoundUp(alignedWidth, planeInfo.horizontalSubsampling);
        const uint32_t planeHeight = DivideRoundUp(alignedHeight, planeInfo.verticalSubsampling);
        const uint32_t planeBpp = planeInfo.bytesPerPixel;
        const uint32_t planeStrideBytes = planeWidth * planeBpp;
        const uint32_t planeSizeBytes = planeHeight * planeStrideBytes;
        ahbPlanes->emplace_back(Gralloc::LockedPlane{
            .data = data + cumulativeSize,
            .pixelStrideBytes = planeBpp,
            .rowStrideBytes = planeStrideBytes,
        });
        cumulativeSize += planeSizeBytes;
    }

    if (mDrmFormat == DRM_FORMAT_NV12) {
        const auto& uPlane = (*ahbPlanes)[1];
        auto vPlane = uPlane;
        vPlane.data += 1;

        ahbPlanes->push_back(vPlane);
    } else if (mDrmFormat == DRM_FORMAT_YVU420) {
        // Note: lockPlanes() always returns Y, then U, then V but YV12 is Y, then V, then U.
        auto& plane1 = (*ahbPlanes)[1];
        auto& plane2 = (*ahbPlanes)[2];
        std::swap(plane1, plane2);
    }

    return 0;
}

int EmulatedAHardwareBuffer::unlock() {
    if (!mMapped) {
        mesa_loge("Failed to unlock EmulatedAHardwareBuffer: never locked?");
        return -1;
    }
    mResource->transferToHost(0, 0, mWidth, mHeight);
    mResource->wait();
    mMapped.reset();
    return 0;
}

EmulatedGralloc::EmulatedGralloc(int32_t descriptor) {
    mDevice.reset(createPlatformVirtGpuDevice(kCapsetNone, descriptor));
}

EmulatedGralloc::~EmulatedGralloc() { mOwned.clear(); }

GrallocType EmulatedGralloc::getGrallocType() { return GRALLOC_TYPE_EMULATED; }

uint32_t EmulatedGralloc::createColorBuffer(int width, int height, uint32_t glFormat) {
    auto drmFormat = GlFormatToDrmFormat(glFormat);
    if (!drmFormat) {
        mesa_loge("Unhandled format");
        return -1;
    }

    auto ahb = allocate(width, height, *drmFormat);
    if (ahb == nullptr) {
        return -1;
    }

    EmulatedAHardwareBuffer* rahb = reinterpret_cast<EmulatedAHardwareBuffer*>(ahb);

    mOwned.emplace_back(rahb);

    return rahb->getResourceId();
}

int EmulatedGralloc::allocate(uint32_t width, uint32_t height, uint32_t ahbFormat, uint64_t usage,
                              AHardwareBuffer** outputAhb) {
    (void)usage;

    auto drmFormat = AhbToDrmFormat(ahbFormat);
    if (!drmFormat) {
        mesa_loge("Unhandled AHB format:%u", ahbFormat);
        return -1;
    }

    *outputAhb = allocate(width, height, *drmFormat);
    if (*outputAhb == nullptr) {
        return -1;
    }

    return 0;
}

AHardwareBuffer* EmulatedGralloc::allocate(uint32_t width, uint32_t height, uint32_t drmFormat) {
    mesa_loge("Allocating AHB w:%u, h:%u, format %u", width, height, drmFormat);

    const auto& formatInfosMap = GetDrmFormatInfoMap();
    auto formatInfoIt = formatInfosMap.find(drmFormat);
    if (formatInfoIt == formatInfosMap.end()) {
        mesa_loge("Failed to allocate: failed to find format info for drm format:%u", drmFormat);
        return nullptr;
    }
    const auto& formatInfo = formatInfoIt->second;

    const uint32_t alignedWidth = Align(width, formatInfo.horizontalAlignmentPixels);
    const uint32_t alignedHeight = Align(height, formatInfo.verticalAlignmentPixels);
    uint32_t stride = 0;
    uint32_t size = 0;
    for (uint32_t i = 0; i < formatInfo.planes.size(); i++) {
        const DrmFormatPlaneInfo& planeInfo = formatInfo.planes[i];
        const uint32_t planeWidth = DivideRoundUp(alignedWidth, planeInfo.horizontalSubsampling);
        const uint32_t planeHeight = DivideRoundUp(alignedHeight, planeInfo.verticalSubsampling);
        const uint32_t planeBpp = planeInfo.bytesPerPixel;
        const uint32_t planeStrideBytes = planeWidth * planeBpp;
        const uint32_t planeSizeBytes = planeHeight * planeStrideBytes;
        size += planeSizeBytes;
        if (i == 0) stride = planeStrideBytes;
    }

    const uint32_t bind = (drmFormat == DRM_FORMAT_R8_BLOB || drmFormat == DRM_FORMAT_NV12 ||
                           drmFormat == DRM_FORMAT_YVU420)
                              ? VIRGL_BIND_LINEAR
                              : VIRGL_BIND_RENDER_TARGET;

    auto resource = mDevice->createResource(width, height, stride, size, formatInfo.virglFormat,
                                            PIPE_TEXTURE_2D, bind);
    if (!resource) {
        mesa_loge("Failed to allocate: failed to create virtio resource.");
        return nullptr;
    }

    resource->wait();

    return reinterpret_cast<AHardwareBuffer*>(
        new EmulatedAHardwareBuffer(width, height, drmFormat, std::move(resource)));
}

void EmulatedGralloc::acquire(AHardwareBuffer* ahb) {
    auto* rahb = reinterpret_cast<EmulatedAHardwareBuffer*>(ahb);
    rahb->acquire();
}

void EmulatedGralloc::release(AHardwareBuffer* ahb) {
    auto* rahb = reinterpret_cast<EmulatedAHardwareBuffer*>(ahb);
    rahb->release();
}

int EmulatedGralloc::lock(AHardwareBuffer* ahb, uint8_t** ptr) {
    auto* rahb = reinterpret_cast<EmulatedAHardwareBuffer*>(ahb);
    return rahb->lock(ptr);
}

int EmulatedGralloc::lockPlanes(AHardwareBuffer* ahb, std::vector<LockedPlane>* ahbPlanes) {
    auto* rahb = reinterpret_cast<EmulatedAHardwareBuffer*>(ahb);
    return rahb->lockPlanes(ahbPlanes);
}

int EmulatedGralloc::unlock(AHardwareBuffer* ahb) {
    auto* rahb = reinterpret_cast<EmulatedAHardwareBuffer*>(ahb);
    return rahb->unlock();
}

uint32_t EmulatedGralloc::getHostHandle(const native_handle_t* handle) {
    const auto* ahb = reinterpret_cast<const EmulatedAHardwareBuffer*>(handle);
    return ahb->getResourceId();
}

uint32_t EmulatedGralloc::getHostHandle(const AHardwareBuffer* handle) {
    const auto* ahb = reinterpret_cast<const EmulatedAHardwareBuffer*>(handle);
    return ahb->getResourceId();
}

const native_handle_t* EmulatedGralloc::getNativeHandle(const AHardwareBuffer* ahb) {
    return reinterpret_cast<const native_handle_t*>(ahb);
}

int EmulatedGralloc::getFormat(const native_handle_t* handle) {
    const auto* ahb = reinterpret_cast<const EmulatedAHardwareBuffer*>(handle);
    return ahb->getAndroidFormat();
}

int EmulatedGralloc::getFormat(const AHardwareBuffer* handle) {
    const auto* ahb = reinterpret_cast<const EmulatedAHardwareBuffer*>(handle);
    return ahb->getAndroidFormat();
}

uint32_t EmulatedGralloc::getFormatDrmFourcc(const AHardwareBuffer* handle) {
    const auto* ahb = reinterpret_cast<const EmulatedAHardwareBuffer*>(handle);
    return ahb->getDrmFormat();
}

uint32_t EmulatedGralloc::getWidth(const AHardwareBuffer* handle) {
    const auto* ahb = reinterpret_cast<const EmulatedAHardwareBuffer*>(handle);
    return ahb->getWidth();
}

uint32_t EmulatedGralloc::getHeight(const AHardwareBuffer* handle) {
    const auto* ahb = reinterpret_cast<const EmulatedAHardwareBuffer*>(handle);
    return ahb->getHeight();
}

size_t EmulatedGralloc::getAllocatedSize(const native_handle_t*) {
    mesa_loge("Unimplemented.");
    return 0;
}

size_t EmulatedGralloc::getAllocatedSize(const AHardwareBuffer*) {
    mesa_loge("Unimplemented.");
    return 0;
}

int EmulatedGralloc::getId(const AHardwareBuffer* ahb, uint64_t* id) {
    const auto* rahb = reinterpret_cast<const EmulatedAHardwareBuffer*>(ahb);
    *id = rahb->getResourceId();
    return 0;
}

Gralloc* createPlatformGralloc(int32_t descriptor) { return new EmulatedGralloc(descriptor); }

}  // namespace gfxstream
