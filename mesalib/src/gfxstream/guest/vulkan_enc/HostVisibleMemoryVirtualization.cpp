/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#include "HostVisibleMemoryVirtualization.h"

#include <set>

#include "ResourceTracker.h"
#include "Resources.h"
#include "VkEncoder.h"
#include "aemu/base/SubAllocator.h"

using android::base::SubAllocator;

namespace gfxstream {
namespace vk {

bool isHostVisible(const VkPhysicalDeviceMemoryProperties* memoryProps, uint32_t index) {
    return memoryProps->memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
}

CoherentMemory::CoherentMemory(VirtGpuResourceMappingPtr blobMapping, uint64_t size,
                               VkDevice device, VkDeviceMemory memory)
    : mSize(size), mBlobMapping(blobMapping), mDevice(device), mMemory(memory) {
    mAllocator =
        std::make_unique<android::base::SubAllocator>(blobMapping->asRawPtr(), mSize, 4096);
}

#if defined(__ANDROID__)
CoherentMemory::CoherentMemory(GoldfishAddressSpaceBlockPtr block, uint64_t gpuAddr, uint64_t size,
                               VkDevice device, VkDeviceMemory memory)
    : mSize(size), mBlock(block), mDevice(device), mMemory(memory) {
    void* address = block->mmap(gpuAddr);
    mAllocator = std::make_unique<android::base::SubAllocator>(address, mSize, kLargestPageSize);
}
#endif  // defined(__ANDROID__)

CoherentMemory::~CoherentMemory() {
    ResourceTracker::getThreadLocalEncoder()->vkFreeMemorySyncGOOGLE(mDevice, mMemory, nullptr,
                                                                     false);
}

VkDeviceMemory CoherentMemory::getDeviceMemory() const { return mMemory; }

bool CoherentMemory::subAllocate(uint64_t size, uint8_t** ptr, uint64_t& offset) {
    auto address = mAllocator->alloc(size);
    if (!address) return false;

    *ptr = (uint8_t*)address;
    offset = mAllocator->getOffset(address);
    return true;
}

bool CoherentMemory::release(uint8_t* ptr) {
    mAllocator->free(ptr);
    return true;
}

}  // namespace vk
}  // namespace gfxstream
