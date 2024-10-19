/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#include <sys/mman.h>

#include "LinuxVirtGpu.h"
#include "drm-uapi/virtgpu_drm.h"

LinuxVirtGpuResourceMapping::LinuxVirtGpuResourceMapping(VirtGpuResourcePtr blob, uint8_t* ptr,
                                                         uint64_t size)
    : mBlob(blob), mPtr(ptr), mSize(size) {}

LinuxVirtGpuResourceMapping::~LinuxVirtGpuResourceMapping(void) { munmap(mPtr, mSize); }

uint8_t* LinuxVirtGpuResourceMapping::asRawPtr(void) { return mPtr; }
