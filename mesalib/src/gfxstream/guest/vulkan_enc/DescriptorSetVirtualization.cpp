/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "DescriptorSetVirtualization.h"

#include "Resources.h"
#include "util/log.h"

namespace gfxstream {
namespace vk {

void clearReifiedDescriptorSet(ReifiedDescriptorSet* set) {
    set->pool = VK_NULL_HANDLE;
    set->setLayout = VK_NULL_HANDLE;
    set->poolId = -1;
    set->allocationPending = false;
    set->allWrites.clear();
    set->pendingWriteArrayRanges.clear();
}

void initDescriptorWriteTable(const std::vector<VkDescriptorSetLayoutBinding>& layoutBindings,
                              DescriptorWriteTable& table) {
    uint32_t highestBindingNumber = 0;

    for (uint32_t i = 0; i < layoutBindings.size(); ++i) {
        if (layoutBindings[i].binding > highestBindingNumber) {
            highestBindingNumber = layoutBindings[i].binding;
        }
    }

    std::vector<uint32_t> countsEachBinding(highestBindingNumber + 1, 0);

    for (uint32_t i = 0; i < layoutBindings.size(); ++i) {
        countsEachBinding[layoutBindings[i].binding] = layoutBindings[i].descriptorCount;
    }

    table.resize(countsEachBinding.size());

    for (uint32_t i = 0; i < table.size(); ++i) {
        table[i].resize(countsEachBinding[i]);

        for (uint32_t j = 0; j < countsEachBinding[i]; ++j) {
            table[i][j].type = DescriptorWriteType::Empty;
            table[i][j].dstArrayElement = 0;
        }
    }
}

static void initializeReifiedDescriptorSet(VkDescriptorPool pool, VkDescriptorSetLayout setLayout,
                                           ReifiedDescriptorSet* set) {
    set->pendingWriteArrayRanges.clear();

    const auto& layoutInfo = *(as_goldfish_VkDescriptorSetLayout(setLayout)->layoutInfo);

    initDescriptorWriteTable(layoutInfo.bindings, set->allWrites);

    for (size_t i = 0; i < layoutInfo.bindings.size(); ++i) {
        // Bindings can be sparsely defined
        const auto& binding = layoutInfo.bindings[i];
        uint32_t bindingIndex = binding.binding;
        if (set->bindingIsImmutableSampler.size() <= bindingIndex) {
            set->bindingIsImmutableSampler.resize(bindingIndex + 1, false);
        }
        set->bindingIsImmutableSampler[bindingIndex] =
            binding.descriptorCount > 0 &&
            (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
             binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) &&
            binding.pImmutableSamplers;
    }

    set->pool = pool;
    set->setLayout = setLayout;
    set->allocationPending = true;
    set->bindings = layoutInfo.bindings;
}

bool isDescriptorTypeImageInfo(VkDescriptorType descType) {
    return (descType == VK_DESCRIPTOR_TYPE_SAMPLER) ||
           (descType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ||
           (descType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) ||
           (descType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) ||
           (descType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
}

bool isDescriptorTypeBufferInfo(VkDescriptorType descType) {
    return (descType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ||
           (descType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) ||
           (descType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) ||
           (descType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
}

bool isDescriptorTypeBufferView(VkDescriptorType descType) {
    return (descType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) ||
           (descType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
}

bool isDescriptorTypeInlineUniformBlock(VkDescriptorType descType) {
    return descType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT;
}

bool isDescriptorTypeAccelerationStructure(VkDescriptorType descType) {
    return descType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
}

void doEmulatedDescriptorWrite(const VkWriteDescriptorSet* write, ReifiedDescriptorSet* toWrite) {
    VkDescriptorType descType = write->descriptorType;
    uint32_t dstBinding = write->dstBinding;
    uint32_t dstArrayElement = write->dstArrayElement;
    uint32_t descriptorCount = write->descriptorCount;

    DescriptorWriteTable& table = toWrite->allWrites;

    uint32_t arrOffset = dstArrayElement;

    if (isDescriptorTypeImageInfo(descType)) {
        uint32_t i = 0;
        while (i < descriptorCount) {
            assert(dstBinding < table.size());
            if (arrOffset >= table[dstBinding].size()) {
                ++dstBinding;
                arrOffset = 0;
                continue;
            }
            auto& entry = table[dstBinding][arrOffset];
            entry.imageInfo = write->pImageInfo[i];
            entry.type = DescriptorWriteType::ImageInfo;
            entry.descriptorType = descType;
            ++i;
            ++arrOffset;
        }
    } else if (isDescriptorTypeBufferInfo(descType)) {
        uint32_t i = 0;
        while (i < descriptorCount) {
            assert(dstBinding < table.size());
            if (arrOffset >= table[dstBinding].size()) {
                ++dstBinding;
                arrOffset = 0;
                continue;
            }
            auto& entry = table[dstBinding][arrOffset];
            entry.bufferInfo = write->pBufferInfo[i];
            entry.type = DescriptorWriteType::BufferInfo;
            entry.descriptorType = descType;
            ++i;
            ++arrOffset;
        }
    } else if (isDescriptorTypeBufferView(descType)) {
        uint32_t i = 0;
        while (i < descriptorCount) {
            assert(dstBinding < table.size());
            if (arrOffset >= table[dstBinding].size()) {
                ++dstBinding;
                arrOffset = 0;
                continue;
            }
            auto& entry = table[dstBinding][arrOffset];
            entry.bufferView = write->pTexelBufferView[i];
            entry.type = DescriptorWriteType::BufferView;
            entry.descriptorType = descType;
            ++i;
            ++arrOffset;
        }
    } else if (isDescriptorTypeInlineUniformBlock(descType)) {
        const VkWriteDescriptorSetInlineUniformBlock* descInlineUniformBlock =
            static_cast<const VkWriteDescriptorSetInlineUniformBlock*>(write->pNext);
        while (descInlineUniformBlock &&
               descInlineUniformBlock->sType !=
                   VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK) {
            descInlineUniformBlock = static_cast<const VkWriteDescriptorSetInlineUniformBlock*>(
                descInlineUniformBlock->pNext);
        }
        if (!descInlineUniformBlock) {
            mesa_loge("%s: did not find inline uniform block\n", __func__);
            return;
        }
        auto& entry = table[dstBinding][0];
        entry.inlineUniformBlock = *descInlineUniformBlock;
        entry.inlineUniformBlockBuffer.assign(
            static_cast<const uint8_t*>(descInlineUniformBlock->pData),
            static_cast<const uint8_t*>(descInlineUniformBlock->pData) +
                descInlineUniformBlock->dataSize);
        entry.type = DescriptorWriteType::InlineUniformBlock;
        entry.descriptorType = descType;
        entry.dstArrayElement = dstArrayElement;
    } else if (isDescriptorTypeAccelerationStructure(descType)) {
        // TODO
        // Look for pNext inline uniform block or acceleration structure.
        // Append new DescriptorWrite entry that holds the buffer
        mesa_logw("%s: Ignoring emulated write for descriptor type 0x%x\n", __func__, descType);
    }
}

void doEmulatedDescriptorCopy(const VkCopyDescriptorSet* copy, const ReifiedDescriptorSet* src,
                              ReifiedDescriptorSet* dst) {
    const DescriptorWriteTable& srcTable = src->allWrites;
    DescriptorWriteTable& dstTable = dst->allWrites;

    // src/dst may be the same descriptor set, so we need to create a temporary array for that case.
    // (TODO: Maybe just notice the pointers are the same? can aliasing in any other way happen?)

    std::vector<DescriptorWrite> toCopy;
    uint32_t currBinding = copy->srcBinding;
    uint32_t arrOffset = copy->srcArrayElement;
    uint32_t i = 0;
    while (i < copy->descriptorCount) {
        assert(currBinding < srcTable.size());
        if (arrOffset >= srcTable[currBinding].size()) {
            ++currBinding;
            arrOffset = 0;
            continue;
        }
        toCopy.push_back(srcTable[currBinding][arrOffset]);
        ++i;
        ++arrOffset;
    }

    currBinding = copy->dstBinding;
    arrOffset = copy->dstArrayElement;
    i = 0;
    while (i < copy->descriptorCount) {
        assert(currBinding < dstTable.size());
        if (arrOffset >= dstTable[currBinding].size()) {
            ++currBinding;
            arrOffset = 0;
            continue;
        }
        dstTable[currBinding][arrOffset] = toCopy[i];
        ++i;
        ++arrOffset;
    }
}

void doEmulatedDescriptorImageInfoWriteFromTemplate(VkDescriptorType descType, uint32_t binding,
                                                    uint32_t dstArrayElement, uint32_t count,
                                                    const VkDescriptorImageInfo* imageInfos,
                                                    ReifiedDescriptorSet* set) {
    DescriptorWriteTable& table = set->allWrites;

    uint32_t currBinding = binding;
    uint32_t arrOffset = dstArrayElement;
    uint32_t i = 0;
    while (i < count) {
        assert(currBinding < table.size());
        if (arrOffset >= table[currBinding].size()) {
            ++currBinding;
            arrOffset = 0;
            continue;
        }
        auto& entry = table[currBinding][arrOffset];
        entry.imageInfo = imageInfos[i];
        entry.type = DescriptorWriteType::ImageInfo;
        entry.descriptorType = descType;
        ++i;
        ++arrOffset;
    }
}

void doEmulatedDescriptorBufferInfoWriteFromTemplate(VkDescriptorType descType, uint32_t binding,
                                                     uint32_t dstArrayElement, uint32_t count,
                                                     const VkDescriptorBufferInfo* bufferInfos,
                                                     ReifiedDescriptorSet* set) {
    DescriptorWriteTable& table = set->allWrites;

    uint32_t currBinding = binding;
    uint32_t arrOffset = dstArrayElement;
    uint32_t i = 0;
    while (i < count) {
        assert(currBinding < table.size());
        if (arrOffset >= table[currBinding].size()) {
            ++currBinding;
            arrOffset = 0;
            continue;
        }
        auto& entry = table[currBinding][arrOffset];
        entry.bufferInfo = bufferInfos[i];
        entry.type = DescriptorWriteType::BufferInfo;
        entry.descriptorType = descType;
        ++i;
        ++arrOffset;
    }
}

void doEmulatedDescriptorBufferViewWriteFromTemplate(VkDescriptorType descType, uint32_t binding,
                                                     uint32_t dstArrayElement, uint32_t count,
                                                     const VkBufferView* bufferViews,
                                                     ReifiedDescriptorSet* set) {
    DescriptorWriteTable& table = set->allWrites;

    uint32_t currBinding = binding;
    uint32_t arrOffset = dstArrayElement;
    uint32_t i = 0;
    while (i < count) {
        assert(currBinding < table.size());
        if (arrOffset >= table[currBinding].size()) {
            ++currBinding;
            arrOffset = 0;
            continue;
        }
        auto& entry = table[currBinding][arrOffset];
        entry.bufferView = bufferViews[i];
        entry.type = DescriptorWriteType::BufferView;
        entry.descriptorType = descType;
        ++i;
        ++arrOffset;
    }
}

void doEmulatedDescriptorInlineUniformBlockFromTemplate(VkDescriptorType descType, uint32_t binding,
                                                        uint32_t dstArrayElement, uint32_t count,
                                                        const void* pData,
                                                        ReifiedDescriptorSet* set) {
    DescriptorWriteTable& table = set->allWrites;
    auto& entry = table[binding][0];
    entry.dstArrayElement = dstArrayElement;
    entry.inlineUniformBlockBuffer.assign(static_cast<const uint8_t*>(pData),
                                          static_cast<const uint8_t*>(pData) + count);
    entry.type = DescriptorWriteType::InlineUniformBlock;
    entry.descriptorType = descType;
}

static bool isBindingFeasibleForAlloc(
    const DescriptorPoolAllocationInfo::DescriptorCountInfo& countInfo,
    const VkDescriptorSetLayoutBinding& binding) {
    if (binding.descriptorCount && (countInfo.type != binding.descriptorType)) {
        return false;
    }

    uint32_t availDescriptorCount = countInfo.descriptorCount - countInfo.used;

    if (availDescriptorCount < binding.descriptorCount) {
        mesa_logd(
            "%s: Ran out of descriptors of type 0x%x. "
            "Wanted %u from layout but "
            "we only have %u free (total in pool: %u)\n",
            __func__, binding.descriptorType, binding.descriptorCount,
            countInfo.descriptorCount - countInfo.used, countInfo.descriptorCount);
        return false;
    }

    return true;
}

static bool isBindingFeasibleForFree(
    const DescriptorPoolAllocationInfo::DescriptorCountInfo& countInfo,
    const VkDescriptorSetLayoutBinding& binding) {
    if (countInfo.type != binding.descriptorType) return false;
    if (countInfo.used < binding.descriptorCount) {
        mesa_logd(
            "%s: Was a descriptor set double freed? "
            "Ran out of descriptors of type 0x%x. "
            "Wanted to free %u from layout but "
            "we only have %u used (total in pool: %u)\n",
            __func__, binding.descriptorType, binding.descriptorCount, countInfo.used,
            countInfo.descriptorCount);
        return false;
    }
    return true;
}

static void allocBindingFeasible(const VkDescriptorSetLayoutBinding& binding,
                                 DescriptorPoolAllocationInfo::DescriptorCountInfo& poolState) {
    poolState.used += binding.descriptorCount;
}

static void freeBindingFeasible(const VkDescriptorSetLayoutBinding& binding,
                                DescriptorPoolAllocationInfo::DescriptorCountInfo& poolState) {
    poolState.used -= binding.descriptorCount;
}

static VkResult validateDescriptorSetAllocation(const VkDescriptorSetAllocateInfo* pAllocateInfo) {
    VkDescriptorPool pool = pAllocateInfo->descriptorPool;
    DescriptorPoolAllocationInfo* poolInfo = as_goldfish_VkDescriptorPool(pool)->allocInfo;

    // Check the number of sets available.
    auto setsAvailable = poolInfo->maxSets - poolInfo->usedSets;

    if (setsAvailable < pAllocateInfo->descriptorSetCount) {
        mesa_logd(
            "%s: Error: VkDescriptorSetAllocateInfo wants %u sets "
            "but we only have %u available. "
            "Bailing with VK_ERROR_OUT_OF_POOL_MEMORY.\n",
            __func__, pAllocateInfo->descriptorSetCount, setsAvailable);
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    }

    // Perform simulated allocation and error out with
    // VK_ERROR_OUT_OF_POOL_MEMORY if it fails.
    std::vector<DescriptorPoolAllocationInfo::DescriptorCountInfo> descriptorCountCopy =
        poolInfo->descriptorCountInfo;

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        if (!pAllocateInfo->pSetLayouts[i]) {
            mesa_logd("%s: Error: Tried to allocate a descriptor set with null set layout.\n",
                  __func__);
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        auto setLayoutInfo =
            as_goldfish_VkDescriptorSetLayout(pAllocateInfo->pSetLayouts[i])->layoutInfo;
        if (!setLayoutInfo) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        for (const auto& binding : setLayoutInfo->bindings) {
            bool success = false;
            for (auto& pool : descriptorCountCopy) {
                if (!isBindingFeasibleForAlloc(pool, binding)) continue;

                success = true;
                allocBindingFeasible(binding, pool);
                break;
            }

            if (!success) {
                return VK_ERROR_OUT_OF_POOL_MEMORY;
            }
        }
    }
    return VK_SUCCESS;
}

void applyDescriptorSetAllocation(VkDescriptorPool pool, VkDescriptorSetLayout setLayout) {
    auto allocInfo = as_goldfish_VkDescriptorPool(pool)->allocInfo;
    auto setLayoutInfo = as_goldfish_VkDescriptorSetLayout(setLayout)->layoutInfo;

    ++allocInfo->usedSets;

    for (const auto& binding : setLayoutInfo->bindings) {
        for (auto& countForPool : allocInfo->descriptorCountInfo) {
            if (!isBindingFeasibleForAlloc(countForPool, binding)) continue;
            allocBindingFeasible(binding, countForPool);
            break;
        }
    }
}

void removeDescriptorSetAllocation(VkDescriptorPool pool,
                                   const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    auto allocInfo = as_goldfish_VkDescriptorPool(pool)->allocInfo;

    if (0 == allocInfo->usedSets) {
        mesa_logd("%s: Warning: a descriptor set was double freed.\n", __func__);
        return;
    }

    --allocInfo->usedSets;

    for (const auto& binding : bindings) {
        for (auto& countForPool : allocInfo->descriptorCountInfo) {
            if (!isBindingFeasibleForFree(countForPool, binding)) continue;
            freeBindingFeasible(binding, countForPool);
            break;
        }
    }
}

void fillDescriptorSetInfoForPool(VkDescriptorPool pool, VkDescriptorSetLayout setLayout,
                                  VkDescriptorSet set) {
    DescriptorPoolAllocationInfo* allocInfo = as_goldfish_VkDescriptorPool(pool)->allocInfo;

    ReifiedDescriptorSet* newReified = new ReifiedDescriptorSet;
    newReified->poolId = as_goldfish_VkDescriptorSet(set)->underlying;
    newReified->allocationPending = true;

    as_goldfish_VkDescriptorSet(set)->reified = newReified;

    allocInfo->allocedPoolIds.insert(newReified->poolId);
    allocInfo->allocedSets.insert(set);

    initializeReifiedDescriptorSet(pool, setLayout, newReified);
}

VkResult validateAndApplyVirtualDescriptorSetAllocation(
    const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pSets) {
    VkResult validateRes = validateDescriptorSetAllocation(pAllocateInfo);

    if (validateRes != VK_SUCCESS) return validateRes;

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        applyDescriptorSetAllocation(pAllocateInfo->descriptorPool, pAllocateInfo->pSetLayouts[i]);
    }

    VkDescriptorPool pool = pAllocateInfo->descriptorPool;
    DescriptorPoolAllocationInfo* allocInfo = as_goldfish_VkDescriptorPool(pool)->allocInfo;

    if (allocInfo->freePoolIds.size() < pAllocateInfo->descriptorSetCount) {
        mesa_loge(
            "%s: FATAL: Somehow out of descriptor pool IDs. Wanted %u IDs but only have %u free "
            "IDs remaining. The count for maxSets was %u and used was %u\n",
            __func__, pAllocateInfo->descriptorSetCount, (uint32_t)allocInfo->freePoolIds.size(),
            allocInfo->maxSets, allocInfo->usedSets);
        abort();
    }

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        uint64_t id = allocInfo->freePoolIds.back();
        allocInfo->freePoolIds.pop_back();

        VkDescriptorSet newSet = new_from_host_VkDescriptorSet((VkDescriptorSet)id);
        pSets[i] = newSet;

        fillDescriptorSetInfoForPool(pool, pAllocateInfo->pSetLayouts[i], newSet);
    }

    return VK_SUCCESS;
}

bool removeDescriptorSetFromPool(VkDescriptorSet set, bool usePoolIds) {
    ReifiedDescriptorSet* reified = as_goldfish_VkDescriptorSet(set)->reified;

    VkDescriptorPool pool = reified->pool;
    DescriptorPoolAllocationInfo* allocInfo = as_goldfish_VkDescriptorPool(pool)->allocInfo;

    if (usePoolIds) {
        // Look for the set's pool Id in the pool. If not found, then this wasn't really allocated,
        // and bail.
        if (allocInfo->allocedPoolIds.find(reified->poolId) == allocInfo->allocedPoolIds.end()) {
            return false;
        }
    }

    const std::vector<VkDescriptorSetLayoutBinding>& bindings = reified->bindings;
    removeDescriptorSetAllocation(pool, bindings);

    if (usePoolIds) {
        allocInfo->freePoolIds.push_back(reified->poolId);
        allocInfo->allocedPoolIds.erase(reified->poolId);
    }
    allocInfo->allocedSets.erase(set);

    return true;
}

std::vector<VkDescriptorSet> clearDescriptorPool(VkDescriptorPool pool, bool usePoolIds) {
    std::vector<VkDescriptorSet> toClear;
    for (auto set : as_goldfish_VkDescriptorPool(pool)->allocInfo->allocedSets) {
        toClear.push_back(set);
    }

    for (auto set : toClear) {
        removeDescriptorSetFromPool(set, usePoolIds);
    }

    return toClear;
}

}  // namespace vk
}  // namespace gfxstream
