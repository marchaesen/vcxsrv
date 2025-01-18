/*
 * Copyright 2025 Mesa3D authors
 * SPDX-License-Identifier: MIT
 */

#include "WindowsVirtGpu.h"

WindowsVirtGpuDevice::WindowsVirtGpuDevice(enum VirtGpuCapset capset, int32_t descriptor)
    : VirtGpuDevice(capset) {}

WindowsVirtGpuDevice::~WindowsVirtGpuDevice() {}

struct VirtGpuCaps WindowsVirtGpuDevice::getCaps(void) { return mCaps; }

int64_t WindowsVirtGpuDevice::getDeviceHandle(void) { return mDeviceHandle; }

VirtGpuResourcePtr WindowsVirtGpuDevice::createResource(uint32_t width, uint32_t height,
                                                        uint32_t stride, uint32_t size,
                                                        uint32_t virglFormat, uint32_t target,
                                                        uint32_t bind) {
    return nullptr;  // stub constant
}

VirtGpuResourcePtr WindowsVirtGpuDevice::createBlob(const struct VirtGpuCreateBlob& blobCreate) {
    return nullptr;  // stub constant
}

VirtGpuResourcePtr WindowsVirtGpuDevice::importBlob(const struct VirtGpuExternalHandle& handle) {
    return nullptr;  // stub constant
}

int WindowsVirtGpuDevice::execBuffer(struct VirtGpuExecBuffer& execbuffer,
                                     const VirtGpuResource* blob) {
    return 0;  // stub constant
}

VirtGpuDevice* osCreateVirtGpuDevice(enum VirtGpuCapset capset, int32_t descriptor) {
    return nullptr;  // stub constant
}
