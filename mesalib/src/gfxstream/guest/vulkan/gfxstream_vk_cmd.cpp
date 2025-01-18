/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "ResourceTracker.h"
#include "VkEncoder.h"
#include "gfxstream_vk_private.h"
#include "util/perf/cpu_trace.h"

VkResult gfxstream_vk_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo,
                                        const VkAllocationCallbacks* pAllocator,
                                        VkCommandPool* pCommandPool) {
    MESA_TRACE_SCOPE("vkCreateCommandPool");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    VkResult result = (VkResult)0;
    struct gfxstream_vk_command_pool* gfxstream_pCommandPool =
        (gfxstream_vk_command_pool*)vk_zalloc2(&gfxstream_device->vk.alloc, pAllocator,
                                               sizeof(gfxstream_vk_command_pool), 8,
                                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    result = gfxstream_pCommandPool ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
    if (VK_SUCCESS == result) {
        result = vk_command_pool_init(&gfxstream_device->vk, &gfxstream_pCommandPool->vk,
                                      pCreateInfo, pAllocator);
    }
    if (VK_SUCCESS == result) {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        result = vkEnc->vkCreateCommandPool(gfxstream_device->internal_object, pCreateInfo,
                                            pAllocator, &gfxstream_pCommandPool->internal_object,
                                            true /* do lock */);
    }
    *pCommandPool = gfxstream_vk_command_pool_to_handle(gfxstream_pCommandPool);
    return result;
}

void gfxstream_vk_DestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                     const VkAllocationCallbacks* pAllocator) {
    MESA_TRACE_SCOPE("vkDestroyCommandPool");
    if (VK_NULL_HANDLE == commandPool) {
        return;
    }
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    VK_FROM_HANDLE(gfxstream_vk_command_pool, gfxstream_commandPool, commandPool);
    {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        vkEnc->vkDestroyCommandPool(gfxstream_device->internal_object,
                                    gfxstream_commandPool->internal_object, pAllocator,
                                    true /* do lock */);
    }
    vk_command_pool_finish(&gfxstream_commandPool->vk);
    vk_free(&gfxstream_commandPool->vk.alloc, gfxstream_commandPool);
}

VkResult gfxstream_vk_ResetCommandPool(VkDevice device, VkCommandPool commandPool,
                                       VkCommandPoolResetFlags flags) {
    MESA_TRACE_SCOPE("vkResetCommandPool");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    VK_FROM_HANDLE(gfxstream_vk_command_pool, gfxstream_commandPool, commandPool);
    VkResult vkResetCommandPool_VkResult_return = (VkResult)0;
    {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        vkResetCommandPool_VkResult_return = vkEnc->vkResetCommandPool(
            gfxstream_device->internal_object, gfxstream_commandPool->internal_object, flags,
            true /* do lock */);
        if (vkResetCommandPool_VkResult_return == VK_SUCCESS) {
            gfxstream::vk::ResourceTracker::get()->resetCommandPoolStagingInfo(
                gfxstream_commandPool->internal_object);
        }
    }
    return vkResetCommandPool_VkResult_return;
}

static VkResult vk_command_buffer_createOp(struct vk_command_pool*, VkCommandBufferLevel,
                                           struct vk_command_buffer**);
static void vk_command_buffer_resetOp(struct vk_command_buffer*, VkCommandBufferResetFlags);
static void vk_command_buffer_destroyOp(struct vk_command_buffer*);

static vk_command_buffer_ops gfxstream_vk_commandBufferOps = {
    .create = vk_command_buffer_createOp,
    .reset = vk_command_buffer_resetOp,
    .destroy = vk_command_buffer_destroyOp};

VkResult vk_command_buffer_createOp(struct vk_command_pool* commandPool, VkCommandBufferLevel level,
                                    struct vk_command_buffer** pCommandBuffer) {
    VkResult result = VK_SUCCESS;
    struct gfxstream_vk_command_buffer* gfxstream_commandBuffer =
        (struct gfxstream_vk_command_buffer*)vk_zalloc(&commandPool->alloc,
                                                       sizeof(struct gfxstream_vk_command_buffer),
                                                       8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (gfxstream_commandBuffer) {
        result =
            vk_command_buffer_init(commandPool, &gfxstream_commandBuffer->vk,
                                   &gfxstream_vk_commandBufferOps, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        if (VK_SUCCESS == result) {
            *pCommandBuffer = &gfxstream_commandBuffer->vk;
        }
    } else {
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return result;
}

void vk_command_buffer_resetOp(struct vk_command_buffer* commandBuffer,
                               VkCommandBufferResetFlags flags) {
    (void)flags;
    vk_command_buffer_reset(commandBuffer);
}

void vk_command_buffer_destroyOp(struct vk_command_buffer* commandBuffer) {
    vk_command_buffer_finish(commandBuffer);
    vk_free(&commandBuffer->pool->alloc, commandBuffer);
}

VkResult gfxstream_vk_AllocateCommandBuffers(VkDevice device,
                                             const VkCommandBufferAllocateInfo* pAllocateInfo,
                                             VkCommandBuffer* pCommandBuffers) {
    MESA_TRACE_SCOPE("vkAllocateCommandBuffers");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    VK_FROM_HANDLE(gfxstream_vk_command_pool, gfxstream_commandPool, pAllocateInfo->commandPool);
    VkResult result = (VkResult)0;
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        pCommandBuffers[i] = VK_NULL_HANDLE;
    }
    std::vector<gfxstream_vk_command_buffer*> gfxstream_commandBuffers(
        pAllocateInfo->commandBufferCount);
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        result =
            vk_command_buffer_createOp(&gfxstream_commandPool->vk, VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                       (vk_command_buffer**)&gfxstream_commandBuffers[i]);
        if (VK_SUCCESS == result) {
            gfxstream_commandBuffers[i]->vk.level = pAllocateInfo->level;
        } else {
            break;
        }
    }
    if (VK_SUCCESS == result) {
        // Create gfxstream-internal commandBuffer array
        std::vector<VkCommandBuffer> internal_objects(pAllocateInfo->commandBufferCount);
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        auto resources = gfxstream::vk::ResourceTracker::get();
        VkCommandBufferAllocateInfo internal_allocateInfo;
        internal_allocateInfo = *pAllocateInfo;
        internal_allocateInfo.commandPool = gfxstream_commandPool->internal_object;
        result = resources->on_vkAllocateCommandBuffers(
            vkEnc, VK_SUCCESS, gfxstream_device->internal_object, &internal_allocateInfo,
            internal_objects.data());
        if (result == VK_SUCCESS) {
            gfxstream::vk::ResourceTracker::get()->addToCommandPool(
                gfxstream_commandPool->internal_object, pAllocateInfo->commandBufferCount,
                internal_objects.data());
            for (uint32_t i = 0; i < (uint32_t)internal_objects.size(); i++) {
                gfxstream_commandBuffers[i]->internal_object = internal_objects[i];
                // TODO: Also vk_command_buffer_init() on every mesa command buffer?
                pCommandBuffers[i] =
                    gfxstream_vk_command_buffer_to_handle(gfxstream_commandBuffers[i]);
            }
        }
    }
    return result;
}

void gfxstream_vk_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                     uint32_t commandBufferCount,
                                     const VkCommandBuffer* pCommandBuffers) {
    MESA_TRACE_SCOPE("vkFreeCommandBuffers");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    VK_FROM_HANDLE(gfxstream_vk_command_pool, gfxstream_commandPool, commandPool);
    {
        // Set up internal commandBuffer array for gfxstream-internal call
        std::vector<VkCommandBuffer> internal_objects;
        internal_objects.reserve(commandBufferCount);
        for (uint32_t i = 0; i < commandBufferCount; i++) {
            VK_FROM_HANDLE(gfxstream_vk_command_buffer, gfxstream_commandBuffer,
                           pCommandBuffers[i]);
            if (gfxstream_commandBuffer) {
                internal_objects.push_back(gfxstream_commandBuffer->internal_object);
            }
        }
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        vkEnc->vkFreeCommandBuffers(gfxstream_device->internal_object,
                                    gfxstream_commandPool->internal_object,
                                    internal_objects.size(),
                                    internal_objects.data(), true /* do lock */);
    }
    for (uint32_t i = 0; i < commandBufferCount; i++) {
        VK_FROM_HANDLE(gfxstream_vk_command_buffer, gfxstream_commandBuffer, pCommandBuffers[i]);
        if (gfxstream_commandBuffer) {
            vk_command_buffer_destroyOp(&gfxstream_commandBuffer->vk);
        }
    }
}

void gfxstream_vk_CmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                               uint32_t firstCounterBuffer,
                                               uint32_t counterBufferCount,
                                               const VkBuffer* pCounterBuffers,
                                               const VkDeviceSize* pCounterBufferOffsets) {
    MESA_TRACE_SCOPE("vkCmdBeginTransformFeedbackEXT");
    VK_FROM_HANDLE(gfxstream_vk_command_buffer, gfxstream_commandBuffer, commandBuffer);
    auto vkEnc = gfxstream::vk::ResourceTracker::getCommandBufferEncoder(
        gfxstream_commandBuffer->internal_object);
    std::vector<VkBuffer> internal_pCounterBuffers(counterBufferCount);
    for (uint32_t i = 0; i < counterBufferCount; ++i) {
        if (pCounterBuffers && pCounterBuffers[i]) {
            VK_FROM_HANDLE(gfxstream_vk_buffer, gfxstream_pCounterBuffers, pCounterBuffers[i]);
            internal_pCounterBuffers[i] = gfxstream_pCounterBuffers->internal_object;
        }
    }
    vkEnc->vkCmdBeginTransformFeedbackEXT(gfxstream_commandBuffer->internal_object,
                                          firstCounterBuffer, counterBufferCount,
                                          pCounterBuffers ? internal_pCounterBuffers.data() : NULL,
                                          pCounterBufferOffsets, true /* do lock */);
}
