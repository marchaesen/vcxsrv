/*
 * Copyright 2018 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdlib.h>

#include "VirtGpu.h"
#include "gfxstream/guest/IOStream.h"

/* This file implements an IOStream that uses VIRTGPU TRANSFER* ioctls on a
 * virtio-gpu DRM rendernode device to communicate with a goldfish-pipe
 * service on the host side.
 */

class VirtioGpuPipeStream : public gfxstream::guest::IOStream {
   public:
    explicit VirtioGpuPipeStream(size_t bufsize, int32_t descriptor);
    ~VirtioGpuPipeStream();

    virtual int connect(const char* serviceName = nullptr);
    virtual uint64_t processPipeInit();

    virtual void* allocBuffer(size_t minSize);
    virtual int commitBuffer(size_t size);
    virtual const unsigned char* readFully(void* buf, size_t len);
    virtual const unsigned char* commitBufferAndReadFully(size_t size, void* buf, size_t len);
    virtual const unsigned char* read(void* buf, size_t* inout_len);

    bool valid();
    int getRendernodeFd();
    int recv(void* buf, size_t len);

    virtual int writeFully(const void* buf, size_t len);

   private:
    // sync. Also resets the write position.
    void wait();

    // transfer to/from host ops
    ssize_t transferToHost(const void* buffer, size_t len);
    ssize_t transferFromHost(void* buffer, size_t len);

    int32_t m_fd = -1;
    std::unique_ptr<VirtGpuDevice> m_device;
    VirtGpuResourcePtr m_resource;
    VirtGpuResourceMappingPtr m_resourceMapping;
    unsigned char* m_virtio_mapped;  // user mapping of bo

    // intermediate buffer
    size_t m_bufsize;
    unsigned char* m_buf;

    size_t m_writtenPos;
};
