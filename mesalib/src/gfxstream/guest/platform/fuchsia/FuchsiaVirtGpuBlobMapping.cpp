/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "FuchsiaVirtGpu.h"

FuchsiaVirtGpuResourceMapping::FuchsiaVirtGpuResourceMapping(VirtGpuResourcePtr blob, uint8_t* ptr,
                                                             uint64_t size) {}

FuchsiaVirtGpuResourceMapping::~FuchsiaVirtGpuResourceMapping(void) {}

uint8_t* FuchsiaVirtGpuResourceMapping::asRawPtr(void) { return nullptr; }
