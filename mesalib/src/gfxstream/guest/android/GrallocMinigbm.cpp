/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "GrallocMinigbm.h"

#include <cros_gralloc/cros_gralloc_handle.h>
#include <stdlib.h>
#include <sys/user.h>
#include <unistd.h>
#include <vndk/hardware_buffer.h>

#include <cinttypes>
#include <cstring>

#include "VirtGpu.h"
#include "util/log.h"

namespace gfxstream {

MinigbmGralloc::MinigbmGralloc(int32_t descriptor) {
    mDevice.reset(createPlatformVirtGpuDevice(kCapsetNone, descriptor));
}

GrallocType MinigbmGralloc::getGrallocType() { return GRALLOC_TYPE_MINIGBM; }

uint32_t MinigbmGralloc::createColorBuffer(int width, int height, uint32_t glformat) {
    // Only supported format for pbuffers in gfxstream should be RGBA8
    const uint32_t kVirglFormatRGBA = 67;  // VIRGL_FORMAT_R8G8B8A8_UNORM;
    uint32_t virtgpu_format = 0;
    uint32_t bpp = 0;
    switch (glformat) {
        case kGlRGB:
            mesa_logi("Note: egl wanted GL_RGB, still using RGBA");
            virtgpu_format = kVirglFormatRGBA;
            bpp = 4;
            break;
        case kGlRGBA:
            virtgpu_format = kVirglFormatRGBA;
            bpp = 4;
            break;
        default:
            mesa_logi("Note: egl wanted 0x%x, still using RGBA", glformat);
            virtgpu_format = kVirglFormatRGBA;
            bpp = 4;
            break;
    }

    uint32_t stride = bpp * width;
    auto resource = mDevice->createResource(width, height, stride, stride * height, virtgpu_format,
                                            PIPE_TEXTURE_2D, VIRGL_BIND_RENDER_TARGET);

    uint32_t handle = resource->getResourceHandle();
    resource->intoRaw();
    return handle;
}

int MinigbmGralloc::allocate(uint32_t width, uint32_t height, uint32_t format, uint64_t usage,
                             AHardwareBuffer** outputAhb) {
    struct AHardwareBuffer_Desc desc = {
        .width = width,
        .height = height,
        .layers = 1,
        .format = format,
        .usage = usage,
    };

    return AHardwareBuffer_allocate(&desc, outputAhb);
}

void MinigbmGralloc::acquire(AHardwareBuffer* ahb) { AHardwareBuffer_acquire(ahb); }

void MinigbmGralloc::release(AHardwareBuffer* ahb) { AHardwareBuffer_release(ahb); }

int MinigbmGralloc::lock(AHardwareBuffer* ahb, uint8_t** ptr) {
    return AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_RARELY, -1, nullptr,
                                reinterpret_cast<void**>(ptr));
}

int MinigbmGralloc::lockPlanes(AHardwareBuffer* ahb, std::vector<LockedPlane>* ahbPlanes) {
    mesa_loge("%s: unimplemented", __func__);
    return -1;
}

int MinigbmGralloc::unlock(AHardwareBuffer* ahb) { return AHardwareBuffer_unlock(ahb, nullptr); }

uint32_t MinigbmGralloc::getHostHandle(const native_handle_t* handle) {
    cros_gralloc_handle const* cros_handle = reinterpret_cast<cros_gralloc_handle const*>(handle);
    struct VirtGpuExternalHandle hnd = {
        .osHandle = dup(cros_handle->fds[0]),
        .type = kMemHandleDmabuf,
    };

    auto resource = mDevice->importBlob(hnd);
    if (!resource) {
        return 0;
    }

    if (resource->wait()) {
        return 0;
    }

    return resource->getResourceHandle();
}

uint32_t MinigbmGralloc::getHostHandle(const AHardwareBuffer* ahb) {
    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(ahb);
    return getHostHandle(handle);
}

const native_handle_t* MinigbmGralloc::getNativeHandle(const AHardwareBuffer* ahb) {
    return AHardwareBuffer_getNativeHandle(ahb);
}

int MinigbmGralloc::getFormat(const native_handle_t* handle) {
    return ((cros_gralloc_handle*)handle)->droid_format;
}

int MinigbmGralloc::getFormat(const AHardwareBuffer* ahb) {
    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(ahb);

    return ((cros_gralloc_handle*)handle)->droid_format;
}

uint32_t MinigbmGralloc::getFormatDrmFourcc(const native_handle_t* handle) {
    return ((cros_gralloc_handle*)handle)->format;
}

uint32_t MinigbmGralloc::getFormatDrmFourcc(const AHardwareBuffer* ahb) {
    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(ahb);
    return getFormatDrmFourcc(handle);
}

uint32_t MinigbmGralloc::getWidth(const AHardwareBuffer* ahb) {
    AHardwareBuffer_Desc desc = {};
    AHardwareBuffer_describe(ahb, &desc);
    return desc.width;
}

uint32_t MinigbmGralloc::getHeight(const AHardwareBuffer* ahb) {
    AHardwareBuffer_Desc desc = {};
    AHardwareBuffer_describe(ahb, &desc);
    return desc.height;
}

size_t MinigbmGralloc::getAllocatedSize(const native_handle_t* handle) {
    cros_gralloc_handle const* cros_handle = reinterpret_cast<cros_gralloc_handle const*>(handle);
    struct VirtGpuExternalHandle hnd = {
        .osHandle = dup(cros_handle->fds[0]),
        .type = kMemHandleDmabuf,
    };

    auto resource = mDevice->importBlob(hnd);
    if (!resource) {
        return 0;
    }

    if (resource->wait()) {
        return 0;
    }

    return resource->getSize();
}

size_t MinigbmGralloc::getAllocatedSize(const AHardwareBuffer* ahb) {
    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(ahb);
    return getAllocatedSize(handle);
}

int MinigbmGralloc::getId(const AHardwareBuffer* ahb, uint64_t* id) {
#if ANDROID_API_LEVEL >= 31
    return AHardwareBuffer_getId(ahb, id);
#else
    (void)ahb;
    *id = 0;
    return 0;
#endif
}

int32_t MinigbmGralloc::getDataspace(const AHardwareBuffer* ahb) {
#if ANDROID_API_LEVEL >= 34
    return AHardwareBuffer_getDataSpace(ahb);
#else
    (void)ahb;
    return GFXSTREAM_AHB_DATASPACE_UNKNOWN;
#endif
}

}  // namespace gfxstream
