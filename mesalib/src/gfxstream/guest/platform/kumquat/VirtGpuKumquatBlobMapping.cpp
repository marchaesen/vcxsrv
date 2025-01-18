/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "VirtGpuKumquat.h"
#include "util/log.h"

VirtGpuKumquatResourceMapping::VirtGpuKumquatResourceMapping(VirtGpuResourcePtr blob,
                                                             struct virtgpu_kumquat* virtGpu,
                                                             uint8_t* ptr, uint64_t size)
    : mBlob(blob), mVirtGpu(virtGpu), mPtr(ptr), mSize(size) {}

VirtGpuKumquatResourceMapping::~VirtGpuKumquatResourceMapping(void) {
    int32_t ret = virtgpu_kumquat_resource_unmap(mVirtGpu, mBlob->getBlobHandle());
    if (ret) {
        mesa_loge("failed to unmap buffer");
    }
}

uint8_t* VirtGpuKumquatResourceMapping::asRawPtr(void) { return mPtr; }
