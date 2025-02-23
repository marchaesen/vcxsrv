/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "LinuxVirtGpu.h"
#include "drm-uapi/virtgpu_drm.h"
#include "util/log.h"

// As per the warning in xf86drm.h, callers of drmPrimeFDToHandle are expected to perform
// reference counting on the underlying GEM handle that is returned. With Vulkan, for example,
// it is entirely possible that an FD, which points to the same underlying GEM handle, is both
// exported then imported across Vulkan objects. As the VirtGpuResource is stored as a
// shared_ptr with it's own ref-countng, the ref-counting for the underyling GEM has to be
// internal to this implementation. Otherwise, a GEM handle which is active in another Vulkan object
// in the same process, may be erroneously closed in the desctructor of one of the shared ptrs.
static std::mutex sDrmObjectRefMutex;
static std::unordered_map<uint32_t, int> sDrmObjectRefMap;

LinuxVirtGpuResource::LinuxVirtGpuResource(int64_t deviceHandle, uint32_t blobHandle,
                                           uint32_t resourceHandle, uint64_t size)
    : mDeviceHandle(deviceHandle),
      mBlobHandle(blobHandle),
      mResourceHandle(resourceHandle),
      mSize(size) {
    const std::lock_guard<std::mutex> lock(sDrmObjectRefMutex);
    auto refMapIt = sDrmObjectRefMap.find(blobHandle);
    if (refMapIt == sDrmObjectRefMap.end()) {
        sDrmObjectRefMap[blobHandle] = 1;
    } else {
        refMapIt->second++;
    }
}

LinuxVirtGpuResource::~LinuxVirtGpuResource() {
    if (mBlobHandle == INVALID_DESCRIPTOR) {
        return;
    }

    const std::lock_guard<std::mutex> lock(sDrmObjectRefMutex);
    auto refMapIt = sDrmObjectRefMap.find(mBlobHandle);
    if (refMapIt == sDrmObjectRefMap.end()) {
        mesa_logw(
            "LinuxVirtGpuResource::~LinuxVirtGpuResource() could not find the blobHandle: %d in "
            "internal map",
            mBlobHandle);
        return;
    }

    refMapIt->second--;
    if (refMapIt->second <= 0) {
        sDrmObjectRefMap.erase(refMapIt);

        struct drm_gem_close gem_close {
            .handle = mBlobHandle, .pad = 0,
        };

        int ret = drmIoctl(mDeviceHandle, DRM_IOCTL_GEM_CLOSE, &gem_close);
        if (ret) {
            mesa_loge("DRM_IOCTL_GEM_CLOSE failed with : [%s, blobHandle %u, resourceHandle: %u]",
                      strerror(errno), mBlobHandle, mResourceHandle);
        }
    }
}

void LinuxVirtGpuResource::intoRaw() {
    mBlobHandle = INVALID_DESCRIPTOR;
    mResourceHandle = INVALID_DESCRIPTOR;
}

uint32_t LinuxVirtGpuResource::getBlobHandle() const { return mBlobHandle; }

uint32_t LinuxVirtGpuResource::getResourceHandle() const { return mResourceHandle; }

uint64_t LinuxVirtGpuResource::getSize() const { return mSize; }

VirtGpuResourceMappingPtr LinuxVirtGpuResource::createMapping() {
    int ret;
    struct drm_virtgpu_map map {
        .handle = mBlobHandle, .pad = 0,
    };

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_MAP, &map);
    if (ret) {
        mesa_loge("DRM_IOCTL_VIRTGPU_MAP failed with %s", strerror(errno));
        return nullptr;
    }

    uint8_t* ptr = static_cast<uint8_t*>(
        mmap64(nullptr, mSize, PROT_WRITE | PROT_READ, MAP_SHARED, mDeviceHandle, map.offset));

    if (ptr == MAP_FAILED) {
        mesa_loge("mmap64 failed with (%s)", strerror(errno));
        return nullptr;
    }

    return std::make_shared<LinuxVirtGpuResourceMapping>(shared_from_this(), ptr, mSize);
}

int LinuxVirtGpuResource::exportBlob(struct VirtGpuExternalHandle& handle) {
    int ret, fd;

    uint32_t flags = DRM_CLOEXEC;
    ret = drmPrimeHandleToFD(mDeviceHandle, mBlobHandle, flags, &fd);
    if (ret) {
        mesa_loge("drmPrimeHandleToFD failed with %s", strerror(errno));
        return ret;
    }

    handle.osHandle = static_cast<int64_t>(fd);
    handle.type = kMemHandleDmabuf;
    return 0;
}

int LinuxVirtGpuResource::wait() {
    int ret;
    struct drm_virtgpu_3d_wait wait_3d = {0};

    int retry = 0;
    do {
        if (retry > 0 && (retry % 10 == 0)) {
            mesa_loge("DRM_IOCTL_VIRTGPU_WAIT failed with EBUSY for %d times.", retry);
        }
        wait_3d.handle = mBlobHandle;
        ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_WAIT, &wait_3d);
        ++retry;
    } while (ret < 0 && errno == EBUSY);

    if (ret < 0) {
        mesa_loge("DRM_IOCTL_VIRTGPU_WAIT failed with %s", strerror(errno));
        return ret;
    }

    return 0;
}

int LinuxVirtGpuResource::transferToHost(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    int ret;
    struct drm_virtgpu_3d_transfer_to_host xfer = {0};

    xfer.box.x = x;
    xfer.box.y = y;
    xfer.box.w = w;
    xfer.box.h = h;
    xfer.box.d = 1;
    xfer.bo_handle = mBlobHandle;

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST, &xfer);
    if (ret < 0) {
        mesa_loge("DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST failed with %s", strerror(errno));
        return ret;
    }

    return 0;
}

int LinuxVirtGpuResource::transferFromHost(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    int ret;
    struct drm_virtgpu_3d_transfer_from_host xfer = {0};

    xfer.box.x = x;
    xfer.box.y = y;
    xfer.box.w = w;
    xfer.box.h = h;
    xfer.box.d = 1;
    xfer.bo_handle = mBlobHandle;

    ret = drmIoctl(mDeviceHandle, DRM_IOCTL_VIRTGPU_TRANSFER_FROM_HOST, &xfer);
    if (ret < 0) {
        mesa_loge("DRM_IOCTL_VIRTGPU_TRANSFER_FROM_HOST failed with %s", strerror(errno));
        return ret;
    }

    return 0;
}
