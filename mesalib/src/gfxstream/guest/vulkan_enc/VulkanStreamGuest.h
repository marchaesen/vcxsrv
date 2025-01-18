/*
 * Copyright 2018 Google LLC
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <inttypes.h>

#include <memory>
#include <vector>

#include "ResourceTracker.h"
#include "VulkanHandleMapping.h"
#include "aemu/base/BumpPool.h"
#include "aemu/base/files/Stream.h"
#include "aemu/base/files/StreamSerializing.h"
#include "gfxstream/guest/IOStream.h"
#include "goldfish_vk_private_defs.h"

namespace gfxstream {
namespace vk {

class VulkanStreamGuest : public android::base::Stream {
   public:
    VulkanStreamGuest(gfxstream::guest::IOStream* stream);
    ~VulkanStreamGuest();

    // Returns whether the connection is valid.
    bool valid();

    // General allocation function
    void alloc(void** ptrAddr, size_t bytes);

    // Utility functions to load strings or
    // string arrays in place with allocation.
    void loadStringInPlace(char** forOutput);
    void loadStringArrayInPlace(char*** forOutput);

    // When we load a string and are using a reserved pointer.
    void loadStringInPlaceWithStreamPtr(char** forOutput, uint8_t** streamPtr);
    void loadStringArrayInPlaceWithStreamPtr(char*** forOutput, uint8_t** streamPtr);

    ssize_t read(void* buffer, size_t size) override;
    ssize_t write(const void* buffer, size_t size) override;

    void writeLarge(const void* buffer, size_t size);

    // Frees everything that got alloc'ed.
    void clearPool();

    void setHandleMapping(VulkanHandleMapping* mapping);
    void unsetHandleMapping();
    VulkanHandleMapping* handleMapping() const;

    void flush();

    uint32_t getFeatureBits() const;

    void incStreamRef();
    bool decStreamRef();

    uint8_t* reserve(size_t size);

   private:
    android::base::BumpPool mPool;
    std::vector<uint8_t> mWriteBuffer;
    gfxstream::guest::IOStream* mStream = nullptr;
    DefaultHandleMapping mDefaultHandleMapping;
    VulkanHandleMapping* mCurrentHandleMapping;
    uint32_t mFeatureBits = 0;
};

class VulkanCountingStream : public VulkanStreamGuest {
   public:
    VulkanCountingStream();
    ~VulkanCountingStream();

    ssize_t read(void* buffer, size_t size) override;
    ssize_t write(const void* buffer, size_t size) override;

    size_t bytesWritten() const { return m_written; }
    size_t bytesRead() const { return m_read; }

    void rewind();

   private:
    size_t m_written = 0;
    size_t m_read = 0;
};

}  // namespace vk
}  // namespace gfxstream
