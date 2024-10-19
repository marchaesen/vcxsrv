/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lib/magma/magma.h>

#include "VirtGpu.h"

class FuchsiaVirtGpuResource : public std::enable_shared_from_this<FuchsiaVirtGpuResource>,
                               public VirtGpuResource {
   public:
    FuchsiaVirtGpuResource(int64_t deviceHandle, uint32_t blobHandle, uint32_t resourceHandle,
                           uint64_t size);
    ~FuchsiaVirtGpuResource();

    uint32_t getResourceHandle() const override;
    uint32_t getBlobHandle() const override;
    uint64_t getSize() const override;
    int wait() override;

    int exportBlob(struct VirtGpuExternalHandle& handle) override;
    int transferFromHost(uint32_t offset, uint32_t size) override;
    int transferToHost(uint32_t offset, uint32_t size) override;

    VirtGpuResourceMappingPtr createMapping(void) override;
};

class FuchsiaVirtGpuResourceMapping : public VirtGpuResourceMapping {
   public:
    FuchsiaVirtGpuResourceMapping(VirtGpuResourcePtr blob, uint8_t* ptr, uint64_t size);
    ~FuchsiaVirtGpuResourceMapping(void);

    uint8_t* asRawPtr(void) override;
};

class FuchsiaVirtGpuDevice : public VirtGpuDevice {
   public:
    FuchsiaVirtGpuDevice(enum VirtGpuCapset capset, magma_device_t device);
    ~FuchsiaVirtGpuDevice();

    int64_t getDeviceHandle(void) override;

    struct VirtGpuCaps getCaps(void) override;

    VirtGpuResourcePtr createBlob(const struct VirtGpuCreateBlob& blobCreate) override;
    VirtGpuResourcePtr createResource(uint32_t width, uint32_t height, uint32_t stride,
                                      uint32_t size, uint32_t virglFormat, uint32_t target,
                                      uint32_t bind) override;
    VirtGpuResourcePtr importBlob(const struct VirtGpuExternalHandle& handle) override;

    int execBuffer(struct VirtGpuExecBuffer& execbuffer, const VirtGpuResource* blob) override;

   private:
    magma_device_t device_;
    struct VirtGpuCaps mCaps;
};
