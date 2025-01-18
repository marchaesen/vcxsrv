/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "GfxStreamConnectionManager.h"

#include <cerrno>

#include "GoldfishAddressSpaceStream.h"
#include "QemuPipeStream.h"
#include "VirtGpu.h"
#include "VirtioGpuAddressSpaceStream.h"
#include "VirtioGpuPipeStream.h"
#include "util/log.h"

#define STREAM_BUFFER_SIZE (4 * 1024 * 1024)

struct ThreadInfo {
    std::unique_ptr<GfxStreamConnectionManager> mgr;
};

static thread_local ThreadInfo sThreadInfo;

GfxStreamConnectionManager::GfxStreamConnectionManager(GfxStreamTransportType type,
                                                       VirtGpuCapset capset)
    : mTransportType(type), mCapset(capset) {}

GfxStreamConnectionManager::~GfxStreamConnectionManager() {}

bool GfxStreamConnectionManager::initialize() {
    switch (mTransportType) {
        case GFXSTREAM_TRANSPORT_ADDRESS_SPACE: {
            mStream = createGoldfishAddressSpaceStream(STREAM_BUFFER_SIZE);
            if (!mStream) {
                mesa_loge("Failed to create AddressSpaceStream for host connection\n");
                return false;
            }
            break;
        }
        case GFXSTREAM_TRANSPORT_QEMU_PIPE: {
            mStream = new QemuPipeStream(STREAM_BUFFER_SIZE);
            if (mStream->connect() < 0) {
                mesa_loge("Failed to connect to host (QemuPipeStream)\n");
                return false;
            }

            break;
        }
        case GFXSTREAM_TRANSPORT_VIRTIO_GPU_PIPE: {
            VirtioGpuPipeStream* pipeStream =
                new VirtioGpuPipeStream(STREAM_BUFFER_SIZE, INVALID_DESCRIPTOR);
            if (!pipeStream) {
                mesa_loge("Failed to create VirtioGpu for host connection\n");
                return false;
            }
            if (pipeStream->connect() < 0) {
                mesa_loge("Failed to connect to host (VirtioGpu)\n");
                return false;
            }

            mDescriptor = pipeStream->getRendernodeFd();
            VirtGpuDevice::getInstance(mCapset);
            mStream = pipeStream;
            break;
        }
        case GFXSTREAM_TRANSPORT_VIRTIO_GPU_ADDRESS_SPACE: {
            // Use kCapsetGfxStreamVulkan for now, Ranchu HWC needs to be modified to pass in
            // right capset.
            auto device = VirtGpuDevice::getInstance(kCapsetGfxStreamVulkan);
            mDescriptor = device->getDeviceHandle();
            mStream = createVirtioGpuAddressSpaceStream(kCapsetGfxStreamVulkan);
            if (!mStream) {
                mesa_loge("Failed to create virtgpu AddressSpaceStream\n");
                return false;
            }

            break;
        }
        default:
            return false;
    }

    // send zero 'clientFlags' to the host.  This is actually part of the gfxstream protocol.
    unsigned int* pClientFlags = (unsigned int*)mStream->allocBuffer(sizeof(unsigned int));
    *pClientFlags = 0;
    mStream->commitBuffer(sizeof(unsigned int));

    return true;
}

GfxStreamConnectionManager* GfxStreamConnectionManager::getThreadLocalInstance(
    GfxStreamTransportType type, VirtGpuCapset capset) {
    if (sThreadInfo.mgr == nullptr) {
        sThreadInfo.mgr = std::make_unique<GfxStreamConnectionManager>(type, capset);
        if (!sThreadInfo.mgr->initialize()) {
            sThreadInfo.mgr = nullptr;
            return nullptr;
        }
    }

    return sThreadInfo.mgr.get();
}

void GfxStreamConnectionManager::threadLocalExit() {
    if (sThreadInfo.mgr == nullptr) {
        return;
    }

    sThreadInfo.mgr.reset();
}

int32_t GfxStreamConnectionManager::addConnection(GfxStreamConnectionType type,
                                                  std::unique_ptr<GfxStreamConnection> connection) {
    if (mConnections.find(type) != mConnections.end()) {
        return -EINVAL;
    }

    mConnections[type] = std::move(connection);
    return 0;
}

void* GfxStreamConnectionManager::getEncoder(GfxStreamConnectionType type) {
    auto iterator = mConnections.find(type);
    if (iterator == mConnections.end()) {
        return nullptr;
    }

    return iterator->second->getEncoder();
}

gfxstream::guest::IOStream* GfxStreamConnectionManager::getStream() { return mStream; }

gfxstream::guest::IOStream* GfxStreamConnectionManager::processPipeStream(
    GfxStreamTransportType transportType) {
    switch (transportType) {
        case GFXSTREAM_TRANSPORT_ADDRESS_SPACE:
        case GFXSTREAM_TRANSPORT_QEMU_PIPE:
            return new QemuPipeStream(STREAM_BUFFER_SIZE);
        case GFXSTREAM_TRANSPORT_VIRTIO_GPU_ADDRESS_SPACE:
        case GFXSTREAM_TRANSPORT_VIRTIO_GPU_PIPE:
            return new VirtioGpuPipeStream(STREAM_BUFFER_SIZE, mDescriptor);
        default:
            return nullptr;
    }
}
