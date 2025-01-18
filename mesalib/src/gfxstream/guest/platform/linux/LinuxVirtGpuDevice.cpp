/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>

#include "LinuxVirtGpu.h"
#include "drm-uapi/virtgpu_drm.h"
#include "util/log.h"
#include "virtgpu_gfxstream_protocol.h"

#define VIRTGPU_PARAM_CREATE_FENCE_PASSING 9  /* Fence passing */
#define VIRTGPU_PARAM_CREATE_GUEST_HANDLE 10  /* Host OS handle can be created from guest memory. */

#define PARAM(x) \
    (struct VirtGpuParam) { x, #x, 0 }

static inline uint32_t align_up(uint32_t n, uint32_t a) { return ((n + a - 1) / a) * a; }

LinuxVirtGpuDevice::LinuxVirtGpuDevice(enum VirtGpuCapset capset, int32_t descriptor)
    : VirtGpuDevice(capset) {
    struct VirtGpuParam params[] = {
        PARAM(VIRTGPU_PARAM_3D_FEATURES),          PARAM(VIRTGPU_PARAM_CAPSET_QUERY_FIX),
        PARAM(VIRTGPU_PARAM_RESOURCE_BLOB),        PARAM(VIRTGPU_PARAM_HOST_VISIBLE),
        PARAM(VIRTGPU_PARAM_CROSS_DEVICE),         PARAM(VIRTGPU_PARAM_CONTEXT_INIT),
        PARAM(VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs), PARAM(VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME),
        PARAM(VIRTGPU_PARAM_CREATE_FENCE_PASSING),
        PARAM(VIRTGPU_PARAM_CREATE_GUEST_HANDLE),
    };

    int ret;
    struct drm_virtgpu_get_caps get_caps = {0};
    struct drm_virtgpu_context_init init = {0};
    struct drm_virtgpu_context_set_param ctx_set_params[3] = {{0}};
    const char* processName = nullptr;

    memset(&mCaps, 0, sizeof(struct VirtGpuCaps));

#ifdef __ANDROID__
    processName = getprogname();
#endif

    if (descriptor < 0) {
        mDeviceHandle = static_cast<int64_t>(drmOpenRender(128));
        if (mDeviceHandle < 0) {
            mesa_loge("Failed to open rendernode: %s", strerror(errno));
            return;
        }
    } else {
        mDeviceHandle = dup(descriptor);
        if (mDeviceHandle < 0) {
            mesa_loge("Failed to dup rendernode: %s", strerror(errno));
            return;
        }
    }

    for (uint32_t i = 0; i < kParamMax; i++) {
        struct drm_virtgpu_getparam get_param = {0};
        get_param.param = params[i].param;
        get_param.value = (uint64_t)(uintptr_t)&params[i].value;

        ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_GETPARAM, &get_param);
        if (ret) {
            mesa_logi("virtgpu backend not enabling %s", params[i].name);
            continue;
        }

        mCaps.params[i] = params[i].value;
    }

    get_caps.cap_set_id = static_cast<uint32_t>(capset);
    switch (capset) {
        case kCapsetGfxStreamVulkan:
            get_caps.size = sizeof(struct vulkanCapset);
            get_caps.addr = (unsigned long long)&mCaps.vulkanCapset;
            break;
        case kCapsetGfxStreamMagma:
            get_caps.size = sizeof(struct magmaCapset);
            get_caps.addr = (unsigned long long)&mCaps.magmaCapset;
            break;
        case kCapsetGfxStreamGles:
            get_caps.size = sizeof(struct vulkanCapset);
            get_caps.addr = (unsigned long long)&mCaps.glesCapset;
            break;
        case kCapsetGfxStreamComposer:
            get_caps.size = sizeof(struct vulkanCapset);
            get_caps.addr = (unsigned long long)&mCaps.composerCapset;
            break;
        default:
            get_caps.size = 0;
    }

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_GET_CAPS, &get_caps);
    if (ret) {
        // Don't fail get capabilities just yet, AEMU doesn't use this API
        // yet (b/272121235);
        mesa_loge("DRM_IOCTL_VIRTGPU_GET_CAPS failed with %s", strerror(errno));
    }

    // We always need an ASG blob in some cases, so always define blobAlignment
    if (!mCaps.vulkanCapset.blobAlignment) {
        mCaps.vulkanCapset.blobAlignment = 4096;
    }

    ctx_set_params[0].param = VIRTGPU_CONTEXT_PARAM_NUM_RINGS;
    ctx_set_params[0].value = 2;
    init.num_params = 1;

    if (capset != kCapsetNone) {
        ctx_set_params[init.num_params].param = VIRTGPU_CONTEXT_PARAM_CAPSET_ID;
        ctx_set_params[init.num_params].value = static_cast<uint32_t>(capset);
        init.num_params++;
    }

    if (mCaps.params[kParamExplicitDebugName] && processName) {
        ctx_set_params[init.num_params].param = VIRTGPU_CONTEXT_PARAM_DEBUG_NAME;
        ctx_set_params[init.num_params].value = reinterpret_cast<uint64_t>(processName);
        init.num_params++;
    }

    init.ctx_set_params = (unsigned long long)&ctx_set_params[0];
    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &init);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_CONTEXT_INIT failed with %s, continuing without context...",
                  strerror(errno));
    }
}

LinuxVirtGpuDevice::~LinuxVirtGpuDevice() { close(mDeviceHandle); }

struct VirtGpuCaps LinuxVirtGpuDevice::getCaps(void) { return mCaps; }

int64_t LinuxVirtGpuDevice::getDeviceHandle(void) { return mDeviceHandle; }

VirtGpuResourcePtr LinuxVirtGpuDevice::createResource(uint32_t width, uint32_t height,
                                                      uint32_t stride, uint32_t size,
                                                      uint32_t virglFormat, uint32_t target,
                                                      uint32_t bind) {
    drm_virtgpu_resource_create create = {
        .target = target,
        .format = virglFormat,
        .bind = bind,
        .width = width,
        .height = height,
        .depth = 1U,
        .array_size = 1U,
        .last_level = 0,
        .nr_samples = 0,
        .size = size,
        .stride = stride,
    };

    int ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE, &create);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_RESOURCE_CREATE failed with %s", strerror(errno));
        return nullptr;
    }

    return std::make_shared<LinuxVirtGpuResource>(
        mDeviceHandle, create.bo_handle, create.res_handle, static_cast<uint64_t>(create.size));
}

VirtGpuResourcePtr LinuxVirtGpuDevice::createBlob(const struct VirtGpuCreateBlob& blobCreate) {
    int ret;
    struct drm_virtgpu_resource_create_blob create = {0};

    create.size = blobCreate.size;
    create.blob_mem = blobCreate.blobMem;
    create.blob_flags = blobCreate.flags;
    create.blob_id = blobCreate.blobId;
    create.cmd = (uint64_t)(uintptr_t)blobCreate.blobCmd;
    create.cmd_size = blobCreate.blobCmdSize;

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &create);
    if (ret < 0) {
        mesa_loge("DRM_VIRTGPU_RESOURCE_CREATE_BLOB failed with %s", strerror(errno));
        return nullptr;
    }

    return std::make_shared<LinuxVirtGpuResource>(mDeviceHandle, create.bo_handle,
                                                  create.res_handle, blobCreate.size);
}

VirtGpuResourcePtr LinuxVirtGpuDevice::importBlob(const struct VirtGpuExternalHandle& handle) {
    struct drm_virtgpu_resource_info info = {0};
    uint32_t blobHandle;
    int ret;

    ret = drmPrimeFDToHandle(mDeviceHandle, handle.osHandle, &blobHandle);
    close(handle.osHandle);
    if (ret) {
        mesa_loge("DRM_IOCTL_PRIME_FD_TO_HANDLE failed: %s", strerror(errno));
        return nullptr;
    }

    info.bo_handle = blobHandle;
    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_RESOURCE_INFO, &info);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_RESOURCE_INFO failed: %s", strerror(errno));
        return nullptr;
    }

    return std::make_shared<LinuxVirtGpuResource>(mDeviceHandle, blobHandle, info.res_handle,
                                                  static_cast<uint64_t>(info.size));
}

int LinuxVirtGpuDevice::execBuffer(struct VirtGpuExecBuffer& execbuffer,
                                   const VirtGpuResource* blob) {
    int ret;
    struct drm_virtgpu_execbuffer exec = {0};
    uint32_t blobHandle;

    exec.flags = execbuffer.flags;
    exec.size = execbuffer.command_size;
    exec.ring_idx = execbuffer.ring_idx;
    exec.command = (uint64_t)(uintptr_t)(execbuffer.command);
    exec.fence_fd = -1;

    if (blob) {
        blobHandle = blob->getBlobHandle();
        exec.bo_handles = (uint64_t)(uintptr_t)(&blobHandle);
        exec.num_bo_handles = 1;
    }

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_EXECBUFFER, &exec);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_EXECBUFFER failed: %s", strerror(errno));
        return ret;
    }

    if (execbuffer.flags & kFenceOut) {
        execbuffer.handle.osHandle = exec.fence_fd;
        execbuffer.handle.type = kFenceHandleSyncFd;
    }

    return 0;
}

VirtGpuDevice* osCreateVirtGpuDevice(enum VirtGpuCapset capset, int32_t descriptor) {
    return new LinuxVirtGpuDevice(capset, descriptor);
}
