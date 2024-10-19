/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gfxstream/guest/GfxStreamGralloc.h"

namespace gfxstream {

class GoldfishGralloc : public Gralloc {
   public:
    GrallocType getGrallocType() override;
    uint32_t createColorBuffer(int width, int height, uint32_t glformat) override;

    int allocate(uint32_t width, uint32_t height, uint32_t format, uint64_t usage,
                 AHardwareBuffer** outputAhb) override;

    void acquire(AHardwareBuffer* ahb) override;
    void release(AHardwareBuffer* ahb) override;

    int lock(AHardwareBuffer* ahb, uint8_t** ptr) override;
    int lockPlanes(AHardwareBuffer* ahb, std::vector<LockedPlane>* ahbPlanes) override;
    int unlock(AHardwareBuffer* ahb) override;

    uint32_t getHostHandle(native_handle_t const* handle) override;
    uint32_t getHostHandle(const AHardwareBuffer* handle) override;

    const native_handle_t* getNativeHandle(const AHardwareBuffer* ahb) override;

    int getFormat(const native_handle_t* handle) override;
    int getFormat(const AHardwareBuffer* handle) override;

    uint32_t getFormatDrmFourcc(const native_handle_t* handle) override;
    uint32_t getFormatDrmFourcc(const AHardwareBuffer* handle) override;

    uint32_t getWidth(const AHardwareBuffer* ahb) override;
    uint32_t getHeight(const AHardwareBuffer* ahb) override;

    size_t getAllocatedSize(const native_handle_t* handle) override;
    size_t getAllocatedSize(const AHardwareBuffer* handle) override;

    int getId(const AHardwareBuffer* ahb, uint64_t* id) override;

    bool treatBlobAsImage() override;
};

}  // namespace gfxstream
