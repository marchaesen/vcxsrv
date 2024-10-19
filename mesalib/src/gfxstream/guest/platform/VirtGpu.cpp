/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#include "VirtGpu.h"

#include <cstdlib>

#include "Sync.h"
#include "util/log.h"

namespace {

static VirtGpuDevice* sDevice = nullptr;

}  // namespace

VirtGpuDevice* createPlatformVirtGpuDevice(enum VirtGpuCapset capset, int32_t descriptor) {
    if (getenv("VIRTGPU_KUMQUAT")) {
        return kumquatCreateVirtGpuDevice(capset, descriptor);
    } else {
        return osCreateVirtGpuDevice(capset, descriptor);
    }
}

VirtGpuDevice* VirtGpuDevice::getInstance(enum VirtGpuCapset capset, int32_t descriptor) {
    // If kCapsetNone is passed, we return a device that was created with any capset.
    // Otherwise, the created device's capset must match the requested capset.
    // We could support multiple capsets with a map of devices but that case isn't needed
    // currently, and with multiple devices it's unclear how to handle kCapsetNone.
    if (capset != kCapsetNone && sDevice && sDevice->capset() != capset) {
        mesa_loge("Requested VirtGpuDevice capset %u, already created capset %u", capset,
                  sDevice->capset());
        return nullptr;
    }
    if (!sDevice) {
        sDevice = createPlatformVirtGpuDevice(capset, descriptor);
    }
    return sDevice;
}

void VirtGpuDevice::resetInstance() {
    if (sDevice) {
        delete sDevice;
        sDevice = nullptr;
    }
}

namespace gfxstream {

SyncHelper* createPlatformSyncHelper() {
    if (getenv("VIRTGPU_KUMQUAT")) {
        return kumquatCreateSyncHelper();
    } else {
        return osCreateSyncHelper();
    }
}

}  // namespace gfxstream
