/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "GrallocGoldfish.h"

#include <gralloc_cb_bp.h>
#include <vndk/hardware_buffer.h>

namespace gfxstream {

GrallocType GoldfishGralloc::getGrallocType() { return GRALLOC_TYPE_GOLDFISH; }

uint32_t GoldfishGralloc::createColorBuffer(int width, int height, uint32_t glformat) { return 0; }

int GoldfishGralloc::allocate(uint32_t width, uint32_t height, uint32_t format, uint64_t usage,
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

void GoldfishGralloc::acquire(AHardwareBuffer* ahb) { AHardwareBuffer_acquire(ahb); }

void GoldfishGralloc::release(AHardwareBuffer* ahb) { AHardwareBuffer_release(ahb); }

int GoldfishGralloc::lock(AHardwareBuffer* ahb, uint8_t** ptr) {
    return AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_RARELY, -1, nullptr,
                                reinterpret_cast<void**>(ptr));
}

int GoldfishGralloc::lockPlanes(AHardwareBuffer* ahb, std::vector<LockedPlane>* ahbPlanes) {
    return -1;
}

int GoldfishGralloc::unlock(AHardwareBuffer* ahb) { return AHardwareBuffer_unlock(ahb, nullptr); }

uint32_t GoldfishGralloc::getHostHandle(native_handle_t const* handle) {
    const uint32_t INVALID_HOST_HANDLE = 0;

    const cb_handle_t* cb = cb_handle_t::from(handle);
    if (cb) {
        return cb->hostHandle;
    } else {
        return INVALID_HOST_HANDLE;
    }
}

uint32_t GoldfishGralloc::getHostHandle(const AHardwareBuffer* ahb) {
    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(ahb);
    return getHostHandle(handle);
}

const native_handle_t* GoldfishGralloc::getNativeHandle(const AHardwareBuffer* ahb) {
    return AHardwareBuffer_getNativeHandle(ahb);
}

int GoldfishGralloc::getFormat(const native_handle_t* handle) {
    return cb_handle_t::from(handle)->format;
}

int GoldfishGralloc::getFormat(const AHardwareBuffer* ahb) {
    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(ahb);
    return getFormat(handle);
}

uint32_t GoldfishGralloc::getFormatDrmFourcc(const native_handle_t* handle) {
    return cb_handle_t::from(handle)->drmformat;
}

uint32_t GoldfishGralloc::getFormatDrmFourcc(const AHardwareBuffer* ahb) {
    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(ahb);
    return getFormatDrmFourcc(handle);
}

uint32_t GoldfishGralloc::getWidth(const AHardwareBuffer* ahb) {
    AHardwareBuffer_Desc desc = {};
    AHardwareBuffer_describe(ahb, &desc);
    return desc.width;
}

uint32_t GoldfishGralloc::getHeight(const AHardwareBuffer* ahb) {
    AHardwareBuffer_Desc desc = {};
    AHardwareBuffer_describe(ahb, &desc);
    return desc.height;
}

size_t GoldfishGralloc::getAllocatedSize(const native_handle_t* handle) {
    return static_cast<size_t>(cb_handle_t::from(handle)->allocatedSize());
}

size_t GoldfishGralloc::getAllocatedSize(const AHardwareBuffer* ahb) {
    const native_handle_t* handle = AHardwareBuffer_getNativeHandle(ahb);
    return getAllocatedSize(handle);
}

int GoldfishGralloc::getId(const AHardwareBuffer* ahb, uint64_t* id) {
#if ANDROID_API_LEVEL >= 31
    return AHardwareBuffer_getId(ahb, id);
#else
    (void)ahb;
    *id = 0;
    return 0;
#endif
}

bool GoldfishGralloc::treatBlobAsImage() { return true; }

}  // namespace gfxstream
