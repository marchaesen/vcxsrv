/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "FuchsiaVirtGpu.h"
#include "util/log.h"

FuchsiaVirtGpuResource::FuchsiaVirtGpuResource(int64_t deviceHandle, uint32_t blobHandle,
                                               uint32_t resourceHandle, uint64_t size) {}

FuchsiaVirtGpuResource::~FuchsiaVirtGpuResource(void) {}

uint32_t FuchsiaVirtGpuResource::getBlobHandle() const {
    mesa_loge("%s: unimplemented", __func__);
    return 0;
}

uint32_t FuchsiaVirtGpuResource::getResourceHandle() const {
    mesa_loge("%s: unimplemented", __func__);
    return 0;
}

uint64_t FuchsiaVirtGpuResource::getSize() const {
    mesa_loge("%s: unimplemented", __func__);
    return 0;
}

VirtGpuResourceMappingPtr FuchsiaVirtGpuResource::createMapping(void) {
    mesa_loge("%s: unimplemented", __func__);
    return nullptr;
}

int FuchsiaVirtGpuResource::wait() { return -1; }

int FuchsiaVirtGpuResource::exportBlob(struct VirtGpuExternalHandle& handle) {
    mesa_loge("%s: unimplemented", __func__);
    return 0;
}

int FuchsiaVirtGpuResource::transferFromHost(uint32_t offset, uint32_t size) {
    mesa_loge("%s: unimplemented", __func__);
    return 0;
}

int FuchsiaVirtGpuResource::transferToHost(uint32_t offset, uint32_t size) {
    mesa_loge("%s: unimplemented", __func__);
    return 0;
}
