/*
 * Copyright 2011 Google
 * SPDX-License-Identifier: MIT
 */

#ifndef __ADDRESS_SPACE_STREAM_H
#define __ADDRESS_SPACE_STREAM_H

#include "VirtGpu.h"
#include "address_space.h"
#include "address_space_graphics_types.h"
#include "gfxstream/guest/IOStream.h"

using gfxstream::guest::IOStream;

class AddressSpaceStream : public IOStream {
public:
 explicit AddressSpaceStream(address_space_handle_t handle, uint32_t version,
                             struct asg_context context, uint64_t ringOffset,
                             uint64_t writeBufferOffset, struct address_space_ops ops);
 ~AddressSpaceStream();

 virtual size_t idealAllocSize(size_t len);
 virtual void* allocBuffer(size_t minSize);
 virtual int commitBuffer(size_t size);
 virtual const unsigned char* readFully(void* buf, size_t len);
 virtual const unsigned char* read(void* buf, size_t* inout_len);
 virtual int writeFully(const void* buf, size_t len);
 virtual int writeFullyAsync(const void* buf, size_t len);
 virtual const unsigned char* commitBufferAndReadFully(size_t size, void* buf, size_t len);

 void setMapping(VirtGpuResourceMappingPtr mapping) { m_mapping = mapping; }

 void setResourceId(uint32_t id) { m_resourceId = id; }

private:
    bool isInError() const;
    ssize_t speculativeRead(unsigned char* readBuffer, size_t trySize);
    void notifyAvailable();
    uint32_t getRelativeBufferPos(uint32_t pos);
    void advanceWrite();
    void ensureConsumerFinishing();
    void ensureType1Finished();
    void ensureType3Finished();
    int type1Write(uint32_t offset, size_t size);

    void backoff();
    void resetBackoff();

    VirtGpuResourceMappingPtr m_mapping = nullptr;
    struct address_space_ops m_ops;

    unsigned char* m_tmpBuf;
    size_t m_tmpBufSize;
    size_t m_tmpBufXferSize;
    bool m_usingTmpBuf;

    unsigned char* m_readBuf;
    size_t m_read;
    size_t m_readLeft;

    address_space_handle_t m_handle;
    uint32_t m_version;
    struct asg_context m_context;

    uint64_t m_ringOffset;
    uint64_t m_writeBufferOffset;

    uint32_t m_writeBufferSize;
    uint32_t m_writeBufferMask;
    unsigned char* m_buf;
    unsigned char* m_writeStart;
    uint32_t m_writeStep;

    uint32_t m_notifs;
    uint32_t m_written;

    uint64_t m_backoffIters;
    uint64_t m_backoffFactor;

    size_t m_ringStorageSize;
    uint32_t m_resourceId = 0;
};

#endif
