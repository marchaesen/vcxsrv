/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <lib/zx/vmo.h>
#include <msd-virtio-gpu/magma-virtio-gpu-defs.h>
#include <os_dirent.h>
#include <services/service_connector.h>

#include <climits>
#include <cstdio>
#include <cstdlib>

#include "FuchsiaVirtGpu.h"
#include "Sync.h"
#include "util/log.h"

FuchsiaVirtGpuDevice::FuchsiaVirtGpuDevice(enum VirtGpuCapset capset, magma_device_t device)
    : VirtGpuDevice(capset), device_(device) {
    memset(&mCaps, 0, sizeof(struct VirtGpuCaps));

    // Hard-coded values that may be assumed on Fuchsia.
    mCaps.params[kParam3D] = 1;
    mCaps.params[kParamCapsetFix] = 1;
    mCaps.params[kParamResourceBlob] = 1;
    mCaps.params[kParamHostVisible] = 1;
    mCaps.params[kParamCrossDevice] = 0;
    mCaps.params[kParamContextInit] = 1;
    mCaps.params[kParamSupportedCapsetIds] = 0;
    mCaps.params[kParamExplicitDebugName] = 0;
    mCaps.params[kParamCreateGuestHandle] = 0;

    if (capset == kCapsetGfxStreamVulkan) {
        uint64_t query_id = kMagmaVirtioGpuQueryCapset;
        query_id |= static_cast<uint64_t>(kCapsetGfxStreamVulkan) << 32;
        constexpr uint16_t kVersion = 0;
        query_id |= static_cast<uint64_t>(kVersion) << 16;

        magma_handle_t buffer;
        magma_status_t status = magma_device_query(device_, query_id, &buffer, nullptr);
        if (status == MAGMA_STATUS_OK) {
            zx::vmo capset_info(buffer);
            zx_status_t status =
                capset_info.read(&mCaps.vulkanCapset, /*offset=*/0, sizeof(struct vulkanCapset));
            mesa_logi("Got capset result, read status %d", status);
        } else {
            mesa_loge("Query(%lu) failed: status %d, expected buffer result", query_id, status);
        }

        // We always need an ASG blob in some cases, so always define blobAlignment
        if (!mCaps.vulkanCapset.blobAlignment) {
            mCaps.vulkanCapset.blobAlignment = 4096;
        }
    }
}

FuchsiaVirtGpuDevice::~FuchsiaVirtGpuDevice() { magma_device_release(device_); }

int64_t FuchsiaVirtGpuDevice::getDeviceHandle(void) { return device_; }

VirtGpuResourcePtr FuchsiaVirtGpuDevice::createBlob(const struct VirtGpuCreateBlob& blobCreate) {
    mesa_loge("%s: unimplemented", __func__);
    return nullptr;
}

VirtGpuResourcePtr FuchsiaVirtGpuDevice::createResource(uint32_t width, uint32_t height,
                                                        uint32_t stride, uint32_t size,
                                                        uint32_t virglFormat, uint32_t target,
                                                        uint32_t bind) {
    mesa_loge("%s: unimplemented", __func__);
    return nullptr;
}

VirtGpuResourcePtr FuchsiaVirtGpuDevice::importBlob(const struct VirtGpuExternalHandle& handle) {
    mesa_loge("%s: unimplemented", __func__);
    return nullptr;
}

int FuchsiaVirtGpuDevice::execBuffer(struct VirtGpuExecBuffer& execbuffer,
                                     const VirtGpuResource* blob) {
    mesa_loge("%s: unimplemented", __func__);
    return 0;
}

struct VirtGpuCaps FuchsiaVirtGpuDevice::getCaps(void) { return {}; }

VirtGpuDevice* osCreateVirtGpuDevice(enum VirtGpuCapset capset, int32_t descriptor) {
    // We don't handle the VirtioGpuPipeStream case.
    if (descriptor >= 0) {
        mesa_loge("Fuchsia: fd not handled");
        return nullptr;
    }

    const char kDevGpu[] = "/loader-gpu-devices/class/gpu";

    struct os_dirent* de;
    os_dir_t* dir = os_opendir(kDevGpu);
    if (!dir) {
        mesa_loge("Error opening %s", kDevGpu);
        return nullptr;
    }

    mesa_logi("Opened dir %s", kDevGpu);

    VirtGpuDevice* gpu_device = nullptr;

    while ((de = os_readdir(dir)) != NULL) {
        mesa_logi("Got name %s", de->d_name);

        if (strcmp(de->d_name, ".") == 0) {
            continue;
        }
        // extra +1 ensures space for null termination
        char name[sizeof(kDevGpu) + sizeof('/') + sizeof(de->d_name) + 1];
        snprintf(name, sizeof(name), "%s/%s", kDevGpu, de->d_name);

        zx_handle_t device_channel = GetConnectToServiceFunction()(name);
        if (device_channel == ZX_HANDLE_INVALID) {
            mesa_loge("Failed to open device: %s", name);
            continue;
        }

        magma_device_t magma_device;
        magma_status_t status = magma_device_import(device_channel, &magma_device);
        if (status != MAGMA_STATUS_OK) {
            mesa_loge("magma_device_import failed: %d", status);
            continue;
        }

        gpu_device = new FuchsiaVirtGpuDevice(capset, magma_device);
        break;
    }
    os_closedir(dir);

    return gpu_device;
}

namespace gfxstream {

SyncHelper* osCreateSyncHelper() { return nullptr; }

}  // namespace gfxstream
