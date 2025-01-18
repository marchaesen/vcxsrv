/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vulkan/vulkan.h>

#include "VirtGpu.h"
#include "aemu/base/SubAllocator.h"
#include "goldfish_address_space.h"

constexpr uint64_t kMegaByte = 1048576;

// This needs to be a power of 2 that is at least the min alignment needed
// in HostVisibleMemoryVirtualization.cpp.
// Some Windows drivers require a 64KB alignment for suballocated memory (b:152769369) for YUV
// images.
constexpr uint64_t kLargestPageSize = 65536;

constexpr uint64_t kDefaultHostMemBlockSize = 16 * kMegaByte;  // 16 mb
constexpr uint64_t kHostVisibleHeapSize = 512 * kMegaByte;     // 512 mb

namespace gfxstream {
namespace vk {

bool isHostVisible(const VkPhysicalDeviceMemoryProperties* memoryProps, uint32_t index);

using GoldfishAddressSpaceBlockPtr = std::shared_ptr<GoldfishAddressSpaceBlock>;
using SubAllocatorPtr = std::unique_ptr<android::base::SubAllocator>;

class CoherentMemory {
   public:
    CoherentMemory(VirtGpuResourceMappingPtr blobMapping, uint64_t size, VkDevice device,
                   VkDeviceMemory memory);

#if defined(__ANDROID__)
    CoherentMemory(GoldfishAddressSpaceBlockPtr block, uint64_t gpuAddr, uint64_t size,
                   VkDevice device, VkDeviceMemory memory);
#endif  // defined(__ANDROID__)

    ~CoherentMemory();

    VkDeviceMemory getDeviceMemory() const;

    bool subAllocate(uint64_t size, uint8_t** ptr, uint64_t& offset);
    bool release(uint8_t* ptr);

   private:
    CoherentMemory(CoherentMemory const&);
    void operator=(CoherentMemory const&);

    uint64_t mSize;
    VirtGpuResourceMappingPtr mBlobMapping = nullptr;
    GoldfishAddressSpaceBlockPtr mBlock = nullptr;
    VkDevice mDevice;
    VkDeviceMemory mMemory;
    SubAllocatorPtr mAllocator;
};

using CoherentMemoryPtr = std::shared_ptr<CoherentMemory>;

}  // namespace vk
}  // namespace gfxstream
