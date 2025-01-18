/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 */
#ifndef __COMMAND_BUFFER_STAGING_STREAM_H
#define __COMMAND_BUFFER_STAGING_STREAM_H

#include <vulkan/vulkan_core.h>

#include <functional>

#include "gfxstream/guest/IOStream.h"

namespace gfxstream {
namespace vk {

class CommandBufferStagingStream : public gfxstream::guest::IOStream {
   public:
    // host will write kSyncDataReadComplete to the sync bytes to indicate memory is no longer being
    // used by host. This is only used with custom allocators. The sync bytes are used to ensure
    // that, during reallocations the guest does not free memory being read by the host. The guest
    // ensures that the sync bytes are marked as read complete before releasing the memory.
    static constexpr size_t kSyncDataSize = 8;
    // indicates read is complete
    static constexpr uint32_t kSyncDataReadComplete = 0X0;
    // indicates read is pending
    static constexpr uint32_t kSyncDataReadPending = 0X1;

    // \struct backing memory structure
    struct Memory {
        VkDeviceMemory deviceMemory =
            VK_NULL_HANDLE;   // device memory associated with allocated memory
        void* ptr = nullptr;  // pointer to allocated memory
        bool operator==(const Memory& rhs) const {
            return (deviceMemory == rhs.deviceMemory) && (ptr == rhs.ptr);
        }
    };

    // allocator
    // param size to allocate
    // return allocated memory
    using Alloc = std::function<Memory(size_t)>;
    // free function
    // param memory to free
    using Free = std::function<void(const Memory&)>;
    // constructor
    // \param allocFn is the allocation function provided.
    // \param freeFn is the free function provided
    explicit CommandBufferStagingStream(const Alloc& allocFn, const Free& freeFn);
    // constructor
    explicit CommandBufferStagingStream();
    ~CommandBufferStagingStream();

    virtual size_t idealAllocSize(size_t len);
    virtual void* allocBuffer(size_t minSize);
    virtual int commitBuffer(size_t size);
    virtual const unsigned char* readFully(void* buf, size_t len);
    virtual const unsigned char* read(void* buf, size_t* inout_len);
    virtual int writeFully(const void* buf, size_t len);
    virtual const unsigned char* commitBufferAndReadFully(size_t size, void* buf, size_t len);

    void getWritten(unsigned char** bufOut, size_t* sizeOut);
    void reset();

    // marks the command buffer stream as flushing. The owner of CommandBufferStagingStream
    // should call markFlushing after finishing writing to the stream.
    // This will mark the sync data to kSyncDataReadPending. This is only applicable when
    // using custom allocators. markFlushing will be a no-op if called
    // when not using custom allocators
    void markFlushing();

    // gets the device memory associated with the stream. This is VK_NULL_HANDLE for default
    // allocation \return device memory
    VkDeviceMemory getDeviceMemory();

   private:
    // underlying memory for data
    Memory m_mem;
    // size of portion of memory available for data.
    // for custom allocation, this size excludes size of sync data.
    size_t m_size;
    // current write position in data buffer
    uint32_t m_writePos;

    // alloc function
    Alloc m_alloc;
    // free function
    Free m_free;

    // realloc function
    // \param size of memory to be allocated
    // \ param reference size to update with actual size allocated. This size can be < requested
    // size for custom allocation to account for sync data
    using Realloc = std::function<Memory(const Memory&, size_t)>;
    Realloc m_realloc;

    // flag tracking use of custom allocation/free
    bool m_usingCustomAlloc = false;

    // adjusted memory location to point to start of data after accounting for metadata
    // \return pointer to data start
    unsigned char* getDataPtr();
};

}  // namespace vk
}  // namespace gfxstream

#endif
