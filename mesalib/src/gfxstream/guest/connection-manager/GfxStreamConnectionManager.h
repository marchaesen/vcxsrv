/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef GFXSTREAM_CONNECTION_MANAGER_H
#define GFXSTREAM_CONNECTION_MANAGER_H

#include <memory>
#include <unordered_map>

#include "GfxStreamConnection.h"
#include "VirtGpu.h"
#include "gfxstream/guest/IOStream.h"

enum GfxStreamConnectionType {
    GFXSTREAM_CONNECTION_GLES = 1,
    GFXSTREAM_CONNECTION_GLES2 = 2,
    GFXSTREAM_CONNECTION_RENDER_CONTROL = 3,
    GFXSTREAM_CONNECTION_VULKAN = 4,
};

enum GfxStreamTransportType {
    GFXSTREAM_TRANSPORT_QEMU_PIPE = 1,
    GFXSTREAM_TRANSPORT_ADDRESS_SPACE = 2,
    GFXSTREAM_TRANSPORT_VIRTIO_GPU_PIPE = 3,
    GFXSTREAM_TRANSPORT_VIRTIO_GPU_ADDRESS_SPACE = 4,
};

class GfxStreamConnectionManager {
   public:
    GfxStreamConnectionManager(GfxStreamTransportType type, VirtGpuCapset capset);
    ~GfxStreamConnectionManager();

    static GfxStreamConnectionManager* getThreadLocalInstance(GfxStreamTransportType type,
                                                              VirtGpuCapset capset);
    void threadLocalExit();

    bool initialize();
    int32_t addConnection(GfxStreamConnectionType type,
                          std::unique_ptr<GfxStreamConnection> connection);
    void* getEncoder(GfxStreamConnectionType type);

    gfxstream::guest::IOStream* getStream();
    gfxstream::guest::IOStream* processPipeStream(GfxStreamTransportType transportType);

   private:
    // intrusively refcounted
    gfxstream::guest::IOStream* mStream = nullptr;
    int32_t mDescriptor = -1;
    GfxStreamTransportType mTransportType;
    VirtGpuCapset mCapset;
    std::unordered_map<GfxStreamConnectionType, std::unique_ptr<GfxStreamConnection>> mConnections;
};

#endif
