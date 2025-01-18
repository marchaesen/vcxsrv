/*
 * Copyright 2025 Mesa3D authors
 * SPDX-License-Identifier: MIT
 */

#include "WindowsVirtGpu.h"

WindowsVirtGpuResourceMapping::WindowsVirtGpuResourceMapping(VirtGpuResourcePtr blob, uint8_t* ptr,
                                                             uint64_t size)
    : mBlob(blob), mPtr(ptr), mSize(size) {}

WindowsVirtGpuResourceMapping::~WindowsVirtGpuResourceMapping(void) {}

uint8_t* WindowsVirtGpuResourceMapping::asRawPtr(void) { return mPtr; }
