/*
 * Copyright 2025 Mesa3D authors
 * SPDX-License-Identifier: MIT
 */

#include "WindowsVirtGpu.h"

WindowsVirtGpuResource::WindowsVirtGpuResource(int64_t deviceHandle, uint32_t blobHandle,
                                               uint32_t resourceHandle, uint64_t size)
    : mDeviceHandle(deviceHandle),
      mBlobHandle(blobHandle),
      mResourceHandle(resourceHandle),
      mSize(size) {}

WindowsVirtGpuResource::~WindowsVirtGpuResource() {}

void WindowsVirtGpuResource::intoRaw() {
    mBlobHandle = INVALID_DESCRIPTOR;
    mResourceHandle = INVALID_DESCRIPTOR;
}

uint32_t WindowsVirtGpuResource::getBlobHandle() const { return mBlobHandle; }

uint32_t WindowsVirtGpuResource::getResourceHandle() const { return mResourceHandle; }

uint64_t WindowsVirtGpuResource::getSize() const { return mSize; }

VirtGpuResourceMappingPtr WindowsVirtGpuResource::createMapping() {
    return nullptr;  // stub constant
}

int WindowsVirtGpuResource::exportBlob(struct VirtGpuExternalHandle& handle) {
    return 0;  // stub constant
}

int WindowsVirtGpuResource::wait() {
    return 0;  // stub constant
}

int WindowsVirtGpuResource::transferToHost(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return 0;  // stub constant
}

int WindowsVirtGpuResource::transferFromHost(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return 0;  // stub constant
}
