/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#include "Sync.h"
#include "VirtGpu.h"
#include "util/log.h"

VirtGpuDevice* kumquatCreateVirtGpuDevice(enum VirtGpuCapset capset, int32_t descriptor) {
    mesa_loge("Using stub implementation of kumquat");
    return nullptr;
}

namespace gfxstream {

SyncHelper* kumquatCreateSyncHelper() { return nullptr; }

}  // namespace gfxstream
