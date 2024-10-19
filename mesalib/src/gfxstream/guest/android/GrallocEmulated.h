/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "VirtGpu.h"
#include "gfxstream/guest/GfxStreamGralloc.h"

namespace gfxstream {

using EGLClientBuffer = void*;

class EmulatedAHardwareBuffer {
   public:
    EmulatedAHardwareBuffer(uint32_t width, uint32_t height, uint32_t drmFormat,
                            VirtGpuResourcePtr resource);

    ~EmulatedAHardwareBuffer();

    uint32_t getResourceId() const;

    uint32_t getWidth() const;

    uint32_t getHeight() const;

    int getAndroidFormat() const;

    uint32_t getDrmFormat() const;

    AHardwareBuffer* asAHardwareBuffer();

    buffer_handle_t asBufferHandle();

    EGLClientBuffer asEglClientBuffer();

    void acquire();
    void release();

    int lock(uint8_t** ptr);
    int lockPlanes(std::vector<Gralloc::LockedPlane>* ahbPlanes);
    int unlock();

   private:
    uint32_t mRefCount;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mDrmFormat;
    VirtGpuResourcePtr mResource;
    std::optional<VirtGpuResourceMappingPtr> mMapped;
};

class EmulatedGralloc : public Gralloc {
   public:
    EmulatedGralloc(int32_t descriptor);
    ~EmulatedGralloc();

    GrallocType getGrallocType() override;
    uint32_t createColorBuffer(int width, int height, uint32_t glFormat) override;

    int allocate(uint32_t width, uint32_t height, uint32_t format, uint64_t usage,
                 AHardwareBuffer** outputAhb) override;

    AHardwareBuffer* allocate(uint32_t width, uint32_t height, uint32_t format);

    void acquire(AHardwareBuffer* ahb) override;
    void release(AHardwareBuffer* ahb) override;

    int lock(AHardwareBuffer* ahb, uint8_t** ptr) override;
    int lockPlanes(AHardwareBuffer* ahb, std::vector<LockedPlane>* ahbPlanes) override;
    int unlock(AHardwareBuffer* ahb) override;

    uint32_t getHostHandle(const native_handle_t* handle) override;
    uint32_t getHostHandle(const AHardwareBuffer* handle) override;

    const native_handle_t* getNativeHandle(const AHardwareBuffer* ahb) override;

    int getFormat(const native_handle_t* handle) override;
    int getFormat(const AHardwareBuffer* handle) override;

    uint32_t getFormatDrmFourcc(const AHardwareBuffer* handle) override;

    uint32_t getWidth(const AHardwareBuffer* ahb) override;
    uint32_t getHeight(const AHardwareBuffer* ahb) override;

    size_t getAllocatedSize(const native_handle_t*) override;
    size_t getAllocatedSize(const AHardwareBuffer*) override;

    int getId(const AHardwareBuffer* ahb, uint64_t* id) override;

   private:
    std::unique_ptr<VirtGpuDevice> mDevice;
    std::vector<std::unique_ptr<EmulatedAHardwareBuffer>> mOwned;
};

}  // namespace gfxstream
