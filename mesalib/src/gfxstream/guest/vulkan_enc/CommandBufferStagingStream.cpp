/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "CommandBufferStagingStream.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <vector>

#include "util/log.h"

static const size_t kReadSize = 512 * 1024;
static const size_t kWriteOffset = kReadSize;

namespace gfxstream {
namespace vk {

CommandBufferStagingStream::CommandBufferStagingStream()
    : IOStream(1048576), m_size(0), m_writePos(0) {
    // use default allocators
    m_alloc = [](size_t size) -> Memory {
        return {
            .deviceMemory = VK_NULL_HANDLE,  // no device memory for malloc
            .ptr = malloc(size),
        };
    };
    m_free = [](const Memory& mem) { free(mem.ptr); };
    m_realloc = [](const Memory& mem, size_t size) -> Memory {
        return {.deviceMemory = VK_NULL_HANDLE, .ptr = realloc(mem.ptr, size)};
    };
}

CommandBufferStagingStream::CommandBufferStagingStream(const Alloc& allocFn, const Free& freeFn)
    : CommandBufferStagingStream() {
    m_usingCustomAlloc = true;
    // for custom allocation, allocate metadata memory at the beginning.
    // m_alloc, m_free and m_realloc wraps sync data logic

    // \param size to allocate
    // \return ptr starting at data
    m_alloc = [&allocFn](size_t size) -> Memory {
        // allocation requested size + sync data size

        // <---sync bytes--><----Data--->
        // |———————————————|————————————|
        // |0|1|2|3|4|5|6|7|............|
        // |———————————————|————————————|
        // ꜛ               ꜛ
        // allocated ptr   ptr to data [dataPtr]

        Memory memory;
        if (!allocFn) {
            mesa_loge("Custom allocation (%zu bytes) failed\n", size);
            return memory;
        }

        // custom allocation/free requires metadata for sync between host/guest
        const size_t totalSize = size + kSyncDataSize;
        memory = allocFn(totalSize);
        if (!memory.ptr) {
            mesa_loge("Custom allocation (%zu bytes) failed\n", size);
            return memory;
        }

        // set sync data to read complete
        uint32_t* syncDWordPtr = reinterpret_cast<uint32_t*>(memory.ptr);
        __atomic_store_n(syncDWordPtr, kSyncDataReadComplete, __ATOMIC_RELEASE);
        return memory;
    };

    m_free = [&freeFn](const Memory& mem) {
        if (!freeFn) {
            mesa_loge("Custom free for memory(%p) failed\n", mem.ptr);
            return;
        }
        freeFn(mem);
    };

    // \param ptr is the data pointer currently allocated
    // \return dataPtr starting at data
    m_realloc = [this](const Memory& mem, size_t size) -> Memory {
        // realloc requires freeing previously allocated memory
        // read sync DWORD to ensure host is done reading this memory
        // before releasing it.

        size_t hostWaits = 0;

        uint32_t* syncDWordPtr = reinterpret_cast<uint32_t*>(mem.ptr);
        while (__atomic_load_n(syncDWordPtr, __ATOMIC_ACQUIRE) != kSyncDataReadComplete) {
            hostWaits++;
            usleep(10);
            if (hostWaits > 1000) {
                mesa_logd("%s: warning, stalled on host decoding on this command buffer stream\n",
                          __func__);
            }
        }

        // for custom allocation/free, memory holding metadata must be copied
        // along with stream data
        // <---sync bytes--><----Data--->
        // |———————————————|————————————|
        // |0|1|2|3|4|5|6|7|............|
        // |———————————————|————————————|
        // ꜛ               ꜛ
        // [copyLocation]  ptr to data [ptr]

        const size_t toCopySize = m_writePos + kSyncDataSize;
        unsigned char* copyLocation = static_cast<unsigned char*>(mem.ptr);
        std::vector<uint8_t> tmp(copyLocation, copyLocation + toCopySize);
        m_free(mem);

        // get new buffer and copy previous stream data to it
        Memory newMemory = m_alloc(size);
        unsigned char* newBuf = static_cast<unsigned char*>(newMemory.ptr);
        if (!newBuf) {
            mesa_loge("Custom allocation (%zu bytes) failed\n", size);
            return newMemory;
        }
        // copy previous data
        memcpy(newBuf, tmp.data(), toCopySize);

        return newMemory;
    };
}

CommandBufferStagingStream::~CommandBufferStagingStream() {
    flush();
    if (m_mem.ptr) m_free(m_mem);
}

unsigned char* CommandBufferStagingStream::getDataPtr() {
    if (!m_mem.ptr) return nullptr;
    const size_t metadataSize = m_usingCustomAlloc ? kSyncDataSize : 0;
    return static_cast<unsigned char*>(m_mem.ptr) + metadataSize;
}

void CommandBufferStagingStream::markFlushing() {
    if (!m_usingCustomAlloc) {
        return;
    }
    uint32_t* syncDWordPtr = reinterpret_cast<uint32_t*>(m_mem.ptr);
    __atomic_store_n(syncDWordPtr, kSyncDataReadPending, __ATOMIC_RELEASE);
}

size_t CommandBufferStagingStream::idealAllocSize(size_t len) {
    if (len > 1048576) return len;
    return 1048576;
}

void* CommandBufferStagingStream::allocBuffer(size_t minSize) {
    size_t allocSize = (1048576 < minSize ? minSize : 1048576);
    // Initial case: blank
    if (!m_mem.ptr) {
        m_mem = m_alloc(allocSize);
        m_size = allocSize;
        return getDataPtr();
    }

    // Calculate remaining
    size_t remaining = m_size - m_writePos;
    // check if there is at least minSize bytes left in buffer
    // if not, reallocate a buffer of big enough size
    if (remaining < minSize) {
        size_t newAllocSize = m_size * 2 + allocSize;
        m_mem = m_realloc(m_mem, newAllocSize);
        m_size = newAllocSize;

        return (void*)(getDataPtr() + m_writePos);
    }

    // for custom allocations, host should have finished reading
    // data from command buffer since command buffers are flushed
    // on queue submit.
    // allocBuffer should not be called on command buffers that are currently
    // being read by the host
    if (m_usingCustomAlloc) {
        uint32_t* syncDWordPtr = reinterpret_cast<uint32_t*>(m_mem.ptr);
        if (__atomic_load_n(syncDWordPtr, __ATOMIC_ACQUIRE) != kSyncDataReadComplete) {
            mesa_loge("FATAL: allocBuffer() called but previous read not complete");
            abort();
        }
    }

    return (void*)(getDataPtr() + m_writePos);
}

int CommandBufferStagingStream::commitBuffer(size_t size) {
    m_writePos += size;
    return 0;
}

const unsigned char* CommandBufferStagingStream::readFully(void*, size_t) {
    // Not supported
    mesa_loge("CommandBufferStagingStream::%s: Fatal: not supported\n", __func__);
    abort();
    return nullptr;
}

const unsigned char* CommandBufferStagingStream::read(void*, size_t*) {
    // Not supported
    mesa_loge("CommandBufferStagingStream::%s: Fatal: not supported\n", __func__);
    abort();
    return nullptr;
}

int CommandBufferStagingStream::writeFully(const void*, size_t) {
    // Not supported
    mesa_loge("CommandBufferStagingStream::%s: Fatal: not supported\n", __func__);
    abort();
    return 0;
}

const unsigned char* CommandBufferStagingStream::commitBufferAndReadFully(size_t, void*, size_t) {
    // Not supported
    mesa_loge("CommandBufferStagingStream::%s: Fatal: not supported\n", __func__);
    abort();
    return nullptr;
}

void CommandBufferStagingStream::getWritten(unsigned char** bufOut, size_t* sizeOut) {
    *bufOut = getDataPtr();
    *sizeOut = m_writePos;
}

void CommandBufferStagingStream::reset() {
    m_writePos = 0;
    IOStream::rewind();
}

VkDeviceMemory CommandBufferStagingStream::getDeviceMemory() { return m_mem.deviceMemory; }

}  // namespace vk
}  // namespace gfxstream
