/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>

#include "CommandBufferStagingStream.h"
#include "GfxStreamConnectionManager.h"
#include "HostVisibleMemoryVirtualization.h"
#include "Sync.h"
#include "VirtGpu.h"
#include "VulkanHandleMapping.h"
#include "VulkanHandles.h"
#include "goldfish_vk_transform_guest.h"
#include "util/perf/cpu_trace.h"

/// Use installed headers or locally defined Fuchsia-specific bits
#ifdef VK_USE_PLATFORM_FUCHSIA

#include <cutils/native_handle.h>
#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <optional>

#include "services/service_connector.h"

#ifndef FUCHSIA_NO_TRACE
#include <lib/trace/event.h>
#endif

#define GET_STATUS_SAFE(result, member) ((result).ok() ? ((result)->member) : ZX_OK)

struct SetBufferCollectionImageConstraintsResult {
    VkResult result;
    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    std::vector<uint32_t> createInfoIndex;
};

struct SetBufferCollectionBufferConstraintsResult {
    VkResult result;
    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
};

#else

typedef uint32_t zx_handle_t;
typedef uint64_t zx_koid_t;
#define ZX_HANDLE_INVALID ((zx_handle_t)0)
#define ZX_KOID_INVALID ((zx_koid_t)0)
#endif  // VK_USE_PLATFORM_FUCHSIA

/// Use installed headers or locally defined Android-specific bits
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#include "AndroidHardwareBuffer.h"
#include "gfxstream/guest/GfxStreamGralloc.h"
#include <android/hardware_buffer.h>
#endif

#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
/// Goldfish sync only used for AEMU -- should replace in virtio-gpu when possibe
#include "gfxstream/guest/goldfish_sync.h"
#endif

// This should be ABI identical with the variant in ResourceTracker.h
struct GfxStreamVkFeatureInfo {
    bool hasDirectMem;
    bool hasVulkan;
    bool hasDeferredVulkanCommands;
    bool hasVulkanNullOptionalStrings;
    bool hasVulkanCreateResourcesWithRequirements;
    bool hasVulkanIgnoredHandles;
    bool hasVirtioGpuNext;
    bool hasVulkanFreeMemorySync;
    bool hasVirtioGpuNativeSync;
    bool hasVulkanShaderFloat16Int8;
    bool hasVulkanAsyncQueueSubmit;
    bool hasVulkanQueueSubmitWithCommands;
    bool hasVulkanBatchedDescriptorSetUpdate;
    bool hasVulkanAsyncQsri;
    bool hasVulkanAuxCommandMemory;
    bool setupComplete;
};

class HostConnection;

namespace gfxstream {
namespace vk {

class VkEncoder;

class ResourceTracker {
   public:
    ResourceTracker();
    ~ResourceTracker();
    static ResourceTracker* get();

    VulkanHandleMapping* createMapping();
    VulkanHandleMapping* destroyMapping();

    using HostConnectionGetFunc = GfxStreamConnectionManager* (*)();
    using VkEncoderGetFunc = VkEncoder* (*)(GfxStreamConnectionManager*);
    using CleanupCallback = std::function<void()>;

    struct ThreadingCallbacks {
        HostConnectionGetFunc hostConnectionGetFunc = nullptr;
        VkEncoderGetFunc vkEncoderGetFunc = nullptr;
    };

    static uint32_t streamFeatureBits;
    static ThreadingCallbacks threadingCallbacks;

#define HANDLE_REGISTER_DECL(type) \
    void register_##type(type);    \
    void unregister_##type(type);

    GOLDFISH_VK_LIST_HANDLE_TYPES(HANDLE_REGISTER_DECL)

    VkResult on_vkEnumerateInstanceExtensionProperties(void* context, VkResult input_result,
                                                       const char* pLayerName,
                                                       uint32_t* pPropertyCount,
                                                       VkExtensionProperties* pProperties);

    VkResult on_vkEnumerateDeviceExtensionProperties(void* context, VkResult input_result,
                                                     VkPhysicalDevice physicalDevice,
                                                     const char* pLayerName,
                                                     uint32_t* pPropertyCount,
                                                     VkExtensionProperties* pProperties);

    VkResult on_vkEnumeratePhysicalDevices(void* context, VkResult input_result,
                                           VkInstance instance, uint32_t* pPhysicalDeviceCount,
                                           VkPhysicalDevice* pPhysicalDevices);

    void on_vkGetPhysicalDeviceFeatures2(void* context, VkPhysicalDevice physicalDevice,
                                         VkPhysicalDeviceFeatures2* pFeatures);
    void on_vkGetPhysicalDeviceFeatures2KHR(void* context, VkPhysicalDevice physicalDevice,
                                            VkPhysicalDeviceFeatures2* pFeatures);
    void on_vkGetPhysicalDeviceProperties(void* context, VkPhysicalDevice physicalDevice,
                                          VkPhysicalDeviceProperties* pProperties);
    void on_vkGetPhysicalDeviceProperties2(void* context, VkPhysicalDevice physicalDevice,
                                           VkPhysicalDeviceProperties2* pProperties);
    void on_vkGetPhysicalDeviceProperties2KHR(void* context, VkPhysicalDevice physicalDevice,
                                              VkPhysicalDeviceProperties2* pProperties);

    void on_vkGetPhysicalDeviceMemoryProperties(
        void* context, VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceMemoryProperties* pMemoryProperties);
    void on_vkGetPhysicalDeviceMemoryProperties2(
        void* context, VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceMemoryProperties2* pMemoryProperties);
    void on_vkGetPhysicalDeviceMemoryProperties2KHR(
        void* context, VkPhysicalDevice physicalDevice,
        VkPhysicalDeviceMemoryProperties2* pMemoryProperties);
    void on_vkGetDeviceQueue(void* context, VkDevice device, uint32_t queueFamilyIndex,
                             uint32_t queueIndex, VkQueue* pQueue);
    void on_vkGetDeviceQueue2(void* context, VkDevice device, const VkDeviceQueueInfo2* pQueueInfo,
                              VkQueue* pQueue);

    VkResult on_vkCreateInstance(void* context, VkResult input_result,
                                 const VkInstanceCreateInfo* createInfo,
                                 const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
    VkResult on_vkCreateDevice(void* context, VkResult input_result,
                               VkPhysicalDevice physicalDevice,
                               const VkDeviceCreateInfo* pCreateInfo,
                               const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
    void on_vkDestroyDevice_pre(void* context, VkDevice device,
                                const VkAllocationCallbacks* pAllocator);

    VkResult on_vkAllocateMemory(void* context, VkResult input_result, VkDevice device,
                                 const VkMemoryAllocateInfo* pAllocateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);
    void on_vkFreeMemory(void* context, VkDevice device, VkDeviceMemory memory,
                         const VkAllocationCallbacks* pAllocator);

    VkResult on_vkMapMemory(void* context, VkResult input_result, VkDevice device,
                            VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                            VkMemoryMapFlags, void** ppData);

    void on_vkUnmapMemory(void* context, VkDevice device, VkDeviceMemory memory);

    VkResult on_vkCreateImage(void* context, VkResult input_result, VkDevice device,
                              const VkImageCreateInfo* pCreateInfo,
                              const VkAllocationCallbacks* pAllocator, VkImage* pImage);
    void on_vkDestroyImage(void* context, VkDevice device, VkImage image,
                           const VkAllocationCallbacks* pAllocator);

    void on_vkGetImageMemoryRequirements(void* context, VkDevice device, VkImage image,
                                         VkMemoryRequirements* pMemoryRequirements);
    void on_vkGetImageMemoryRequirements2(void* context, VkDevice device,
                                          const VkImageMemoryRequirementsInfo2* pInfo,
                                          VkMemoryRequirements2* pMemoryRequirements);
    void on_vkGetImageMemoryRequirements2KHR(void* context, VkDevice device,
                                             const VkImageMemoryRequirementsInfo2* pInfo,
                                             VkMemoryRequirements2* pMemoryRequirements);
    void on_vkGetImageSubresourceLayout(void* context, VkDevice device, VkImage image,
                                        const VkImageSubresource* pSubresource,
                                        VkSubresourceLayout* pLayout);

    VkResult on_vkBindImageMemory(void* context, VkResult input_result, VkDevice device,
                                  VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
    VkResult on_vkBindImageMemory2(void* context, VkResult input_result, VkDevice device,
                                   uint32_t bindingCount, const VkBindImageMemoryInfo* pBindInfos);
    VkResult on_vkBindImageMemory2KHR(void* context, VkResult input_result, VkDevice device,
                                      uint32_t bindingCount,
                                      const VkBindImageMemoryInfo* pBindInfos);

    VkResult on_vkCreateBuffer(void* context, VkResult input_result, VkDevice device,
                               const VkBufferCreateInfo* pCreateInfo,
                               const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer);
    void on_vkDestroyBuffer(void* context, VkDevice device, VkBuffer buffer,
                            const VkAllocationCallbacks* pAllocator);

    void on_vkGetBufferMemoryRequirements(void* context, VkDevice device, VkBuffer buffer,
                                          VkMemoryRequirements* pMemoryRequirements);
    void on_vkGetBufferMemoryRequirements2(void* context, VkDevice device,
                                           const VkBufferMemoryRequirementsInfo2* pInfo,
                                           VkMemoryRequirements2* pMemoryRequirements);
    void on_vkGetBufferMemoryRequirements2KHR(void* context, VkDevice device,
                                              const VkBufferMemoryRequirementsInfo2* pInfo,
                                              VkMemoryRequirements2* pMemoryRequirements);

    VkResult on_vkBindBufferMemory(void* context, VkResult input_result, VkDevice device,
                                   VkBuffer buffer, VkDeviceMemory memory,
                                   VkDeviceSize memoryOffset);
    VkResult on_vkBindBufferMemory2(void* context, VkResult input_result, VkDevice device,
                                    uint32_t bindInfoCount,
                                    const VkBindBufferMemoryInfo* pBindInfos);
    VkResult on_vkBindBufferMemory2KHR(void* context, VkResult input_result, VkDevice device,
                                       uint32_t bindInfoCount,
                                       const VkBindBufferMemoryInfo* pBindInfos);

    VkResult on_vkCreateSemaphore(void* context, VkResult, VkDevice device,
                                  const VkSemaphoreCreateInfo* pCreateInfo,
                                  const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);
    void on_vkDestroySemaphore(void* context, VkDevice device, VkSemaphore semaphore,
                               const VkAllocationCallbacks* pAllocator);
    VkResult on_vkGetSemaphoreFdKHR(void* context, VkResult, VkDevice device,
                                    const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd);
    VkResult on_vkImportSemaphoreFdKHR(void* context, VkResult, VkDevice device,
                                       const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo);

    VkResult on_vkQueueSubmit(void* context, VkResult input_result, VkQueue queue,
                              uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);

    VkResult on_vkQueueSubmit2(void* context, VkResult input_result, VkQueue queue,
                               uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence);

    VkResult on_vkQueueWaitIdle(void* context, VkResult input_result, VkQueue queue);

    void unwrap_vkCreateImage_pCreateInfo(const VkImageCreateInfo* pCreateInfo,
                                          VkImageCreateInfo* local_pCreateInfo);

    void unwrap_vkAcquireImageANDROID_nativeFenceFd(int fd, int* fd_out);

    void unwrap_VkBindImageMemory2_pBindInfos(uint32_t bindInfoCount,
                                              const VkBindImageMemoryInfo* inputBindInfos,
                                              VkBindImageMemoryInfo* outputBindInfos);

    VkResult on_vkGetMemoryFdKHR(void* context, VkResult input_result, VkDevice device,
                                 const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd);
    VkResult on_vkGetMemoryFdPropertiesKHR(void* context, VkResult input_result, VkDevice device,
                                           VkExternalMemoryHandleTypeFlagBits handleType, int fd,
                                           VkMemoryFdPropertiesKHR* pMemoryFdProperties);

#ifdef VK_USE_PLATFORM_FUCHSIA
    VkResult on_vkGetMemoryZirconHandleFUCHSIA(void* context, VkResult input_result,
                                               VkDevice device,
                                               const VkMemoryGetZirconHandleInfoFUCHSIA* pInfo,
                                               uint32_t* pHandle);
    VkResult on_vkGetMemoryZirconHandlePropertiesFUCHSIA(
        void* context, VkResult input_result, VkDevice device,
        VkExternalMemoryHandleTypeFlagBits handleType, uint32_t handle,
        VkMemoryZirconHandlePropertiesFUCHSIA* pProperties);
    VkResult on_vkGetSemaphoreZirconHandleFUCHSIA(
        void* context, VkResult input_result, VkDevice device,
        const VkSemaphoreGetZirconHandleInfoFUCHSIA* pInfo, uint32_t* pHandle);
    VkResult on_vkImportSemaphoreZirconHandleFUCHSIA(
        void* context, VkResult input_result, VkDevice device,
        const VkImportSemaphoreZirconHandleInfoFUCHSIA* pInfo);
    VkResult on_vkCreateBufferCollectionFUCHSIA(void* context, VkResult input_result,
                                                VkDevice device,
                                                const VkBufferCollectionCreateInfoFUCHSIA* pInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkBufferCollectionFUCHSIA* pCollection);
    void on_vkDestroyBufferCollectionFUCHSIA(void* context, VkResult input_result, VkDevice device,
                                             VkBufferCollectionFUCHSIA collection,
                                             const VkAllocationCallbacks* pAllocator);
    VkResult on_vkSetBufferCollectionBufferConstraintsFUCHSIA(
        void* context, VkResult input_result, VkDevice device, VkBufferCollectionFUCHSIA collection,
        const VkBufferConstraintsInfoFUCHSIA* pBufferConstraintsInfo);
    VkResult on_vkSetBufferCollectionImageConstraintsFUCHSIA(
        void* context, VkResult input_result, VkDevice device, VkBufferCollectionFUCHSIA collection,
        const VkImageConstraintsInfoFUCHSIA* pImageConstraintsInfo);
    VkResult on_vkGetBufferCollectionPropertiesFUCHSIA(
        void* context, VkResult input_result, VkDevice device, VkBufferCollectionFUCHSIA collection,
        VkBufferCollectionPropertiesFUCHSIA* pProperties);

    VkResult setBufferCollectionImageConstraintsFUCHSIA(
        VkEncoder* enc, VkDevice device,
        fidl::WireSyncClient<fuchsia_sysmem::BufferCollection>* pCollection,
        const VkImageConstraintsInfoFUCHSIA* pImageConstraintsInfo);

    VkResult setBufferCollectionBufferConstraintsFUCHSIA(
        fidl::WireSyncClient<fuchsia_sysmem::BufferCollection>* pCollection,
        const VkBufferConstraintsInfoFUCHSIA* pBufferConstraintsInfo);

#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
    VkResult on_vkGetAndroidHardwareBufferPropertiesANDROID(
        void* context, VkResult input_result, VkDevice device, const AHardwareBuffer* buffer,
        VkAndroidHardwareBufferPropertiesANDROID* pProperties);
    VkResult on_vkGetMemoryAndroidHardwareBufferANDROID(
        void* context, VkResult input_result, VkDevice device,
        const VkMemoryGetAndroidHardwareBufferInfoANDROID* pInfo, struct AHardwareBuffer** pBuffer);
#endif

    VkResult on_vkCreateSamplerYcbcrConversion(
        void* context, VkResult input_result, VkDevice device,
        const VkSamplerYcbcrConversionCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion);
    void on_vkDestroySamplerYcbcrConversion(void* context, VkDevice device,
                                            VkSamplerYcbcrConversion ycbcrConversion,
                                            const VkAllocationCallbacks* pAllocator);
    VkResult on_vkCreateSamplerYcbcrConversionKHR(
        void* context, VkResult input_result, VkDevice device,
        const VkSamplerYcbcrConversionCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion);
    void on_vkDestroySamplerYcbcrConversionKHR(void* context, VkDevice device,
                                               VkSamplerYcbcrConversion ycbcrConversion,
                                               const VkAllocationCallbacks* pAllocator);

    VkResult on_vkCreateSampler(void* context, VkResult input_result, VkDevice device,
                                const VkSamplerCreateInfo* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator, VkSampler* pSampler);

    void on_vkGetPhysicalDeviceExternalFenceProperties(
        void* context, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo,
        VkExternalFenceProperties* pExternalFenceProperties);

    void on_vkGetPhysicalDeviceExternalFencePropertiesKHR(
        void* context, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo,
        VkExternalFenceProperties* pExternalFenceProperties);

    VkResult on_vkCreateFence(void* context, VkResult input_result, VkDevice device,
                              const VkFenceCreateInfo* pCreateInfo,
                              const VkAllocationCallbacks* pAllocator, VkFence* pFence);

    void on_vkDestroyFence(void* context, VkDevice device, VkFence fence,
                           const VkAllocationCallbacks* pAllocator);

    VkResult on_vkResetFences(void* context, VkResult input_result, VkDevice device,
                              uint32_t fenceCount, const VkFence* pFences);

    VkResult on_vkImportFenceFdKHR(void* context, VkResult input_result, VkDevice device,
                                   const VkImportFenceFdInfoKHR* pImportFenceFdInfo);

    VkResult on_vkGetFenceFdKHR(void* context, VkResult input_result, VkDevice device,
                                const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd);

    VkResult on_vkGetFenceStatus(void* context, VkResult input_result, VkDevice device,
                                 VkFence fence);

    VkResult on_vkWaitForFences(void* context, VkResult input_result, VkDevice device,
                                uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll,
                                uint64_t timeout);

    VkResult on_vkCreateDescriptorPool(void* context, VkResult input_result, VkDevice device,
                                       const VkDescriptorPoolCreateInfo* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator,
                                       VkDescriptorPool* pDescriptorPool);

    void on_vkDestroyDescriptorPool(void* context, VkDevice device, VkDescriptorPool descriptorPool,
                                    const VkAllocationCallbacks* pAllocator);

    VkResult on_vkResetDescriptorPool(void* context, VkResult input_result, VkDevice device,
                                      VkDescriptorPool descriptorPool,
                                      VkDescriptorPoolResetFlags flags);

    VkResult on_vkAllocateDescriptorSets(void* context, VkResult input_result, VkDevice device,
                                         const VkDescriptorSetAllocateInfo* pAllocateInfo,
                                         VkDescriptorSet* pDescriptorSets);

    VkResult on_vkFreeDescriptorSets(void* context, VkResult input_result, VkDevice device,
                                     VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
                                     const VkDescriptorSet* pDescriptorSets);

    VkResult on_vkCreateDescriptorSetLayout(void* context, VkResult input_result, VkDevice device,
                                            const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDescriptorSetLayout* pSetLayout);

    void on_vkUpdateDescriptorSets(void* context, VkDevice device, uint32_t descriptorWriteCount,
                                   const VkWriteDescriptorSet* pDescriptorWrites,
                                   uint32_t descriptorCopyCount,
                                   const VkCopyDescriptorSet* pDescriptorCopies);

    VkResult on_vkMapMemoryIntoAddressSpaceGOOGLE_pre(void* context, VkResult input_result,
                                                      VkDevice device, VkDeviceMemory memory,
                                                      uint64_t* pAddress);
    VkResult on_vkMapMemoryIntoAddressSpaceGOOGLE(void* context, VkResult input_result,
                                                  VkDevice device, VkDeviceMemory memory,
                                                  uint64_t* pAddress);

    VkResult on_vkCreateDescriptorUpdateTemplate(
        void* context, VkResult input_result, VkDevice device,
        const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate);

    VkResult on_vkCreateDescriptorUpdateTemplateKHR(
        void* context, VkResult input_result, VkDevice device,
        const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate);

    void on_vkUpdateDescriptorSetWithTemplate(void* context, VkDevice device,
                                              VkDescriptorSet descriptorSet,
                                              VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                              const void* pData);

    void on_vkUpdateDescriptorSetWithTemplateKHR(void* context, VkDevice device,
                                              VkDescriptorSet descriptorSet,
                                              VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                              const void* pData);

    VkResult on_vkGetPhysicalDeviceImageFormatProperties2(
        void* context, VkResult input_result, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
        VkImageFormatProperties2* pImageFormatProperties);

    VkResult on_vkGetPhysicalDeviceImageFormatProperties2KHR(
        void* context, VkResult input_result, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
        VkImageFormatProperties2* pImageFormatProperties);

    void on_vkGetPhysicalDeviceExternalBufferProperties(
        void* context, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo,
        VkExternalBufferProperties* pExternalBufferProperties);

    void on_vkGetPhysicalDeviceExternalBufferPropertiesKHR(
        void* context, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo,
        VkExternalBufferPropertiesKHR* pExternalBufferProperties);

    void on_vkGetPhysicalDeviceExternalSemaphoreProperties(
        void* context, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
        VkExternalSemaphoreProperties* pExternalSemaphoreProperties);

    void on_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR(
        void* context, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
        VkExternalSemaphoreProperties* pExternalSemaphoreProperties);

    void registerEncoderCleanupCallback(const VkEncoder* encoder, void* handle,
                                        CleanupCallback callback);
    void unregisterEncoderCleanupCallback(const VkEncoder* encoder, void* handle);
    void onEncoderDeleted(const VkEncoder* encoder);

    uint32_t syncEncodersForCommandBuffer(VkCommandBuffer commandBuffer, VkEncoder* current);
    uint32_t syncEncodersForQueue(VkQueue queue, VkEncoder* currentEncoder);

    CommandBufferStagingStream::Alloc getAlloc();
    CommandBufferStagingStream::Free getFree();

    VkResult on_vkBeginCommandBuffer(void* context, VkResult input_result,
                                     VkCommandBuffer commandBuffer,
                                     const VkCommandBufferBeginInfo* pBeginInfo);
    VkResult on_vkEndCommandBuffer(void* context, VkResult input_result,
                                   VkCommandBuffer commandBuffer);
    VkResult on_vkResetCommandBuffer(void* context, VkResult input_result,
                                     VkCommandBuffer commandBuffer,
                                     VkCommandBufferResetFlags flags);

    VkResult on_vkCreateImageView(void* context, VkResult input_result, VkDevice device,
                                  const VkImageViewCreateInfo* pCreateInfo,
                                  const VkAllocationCallbacks* pAllocator, VkImageView* pView);

    void on_vkCmdExecuteCommands(void* context, VkCommandBuffer commandBuffer,
                                 uint32_t commandBufferCount,
                                 const VkCommandBuffer* pCommandBuffers);

    void on_vkCmdBindDescriptorSets(void* context, VkCommandBuffer commandBuffer,
                                    VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
                                    uint32_t firstSet, uint32_t descriptorSetCount,
                                    const VkDescriptorSet* pDescriptorSets,
                                    uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);

    void on_vkCmdPipelineBarrier(
        void* context, VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
        uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
        uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers,
        uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);

    void on_vkDestroyDescriptorSetLayout(void* context, VkDevice device,
                                         VkDescriptorSetLayout descriptorSetLayout,
                                         const VkAllocationCallbacks* pAllocator);

    VkResult on_vkAllocateCommandBuffers(void* context, VkResult input_result, VkDevice device,
                                         const VkCommandBufferAllocateInfo* pAllocateInfo,
                                         VkCommandBuffer* pCommandBuffers);

    VkResult on_vkQueueSignalReleaseImageANDROID(void* context, VkResult input_result,
                                                 VkQueue queue, uint32_t waitSemaphoreCount,
                                                 const VkSemaphore* pWaitSemaphores, VkImage image,
                                                 int* pNativeFenceFd);

    VkResult on_vkCreateGraphicsPipelines(void* context, VkResult input_result, VkDevice device,
                                          VkPipelineCache pipelineCache, uint32_t createInfoCount,
                                          const VkGraphicsPipelineCreateInfo* pCreateInfos,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkPipeline* pPipelines);

    uint8_t* getMappedPointer(VkDeviceMemory memory);
    VkDeviceSize getMappedSize(VkDeviceMemory memory);
    VkDeviceSize getNonCoherentExtendedSize(VkDevice device, VkDeviceSize basicSize) const;
    bool isValidMemoryRange(const VkMappedMemoryRange& range);

    void setupFeatures(const struct GfxStreamVkFeatureInfo* features);
    void setupCaps(uint32_t& noRenderControlEnc);
    void setupPlatformHelpers();

    void setThreadingCallbacks(const ThreadingCallbacks& callbacks);
    bool hostSupportsVulkan() const;
    bool usingDirectMapping() const;
    uint32_t getStreamFeatures() const;
    uint32_t getApiVersionFromInstance(VkInstance instance);
    uint32_t getApiVersionFromDevice(VkDevice device);
    bool hasInstanceExtension(VkInstance instance, const std::string& name);
    bool hasDeviceExtension(VkDevice instance, const std::string& name);
    VkDevice getDevice(VkCommandBuffer commandBuffer) const;
    void addToCommandPool(VkCommandPool commandPool, uint32_t commandBufferCount,
                          VkCommandBuffer* pCommandBuffers);
    void resetCommandPoolStagingInfo(VkCommandPool commandPool);

#ifdef __GNUC__
#define ALWAYS_INLINE_GFXSTREAM
#elif
#define ALWAYS_INLINE_GFXSTREAM __attribute__((always_inline))
#endif

    static VkEncoder* getCommandBufferEncoder(VkCommandBuffer commandBuffer);
    static VkEncoder* getQueueEncoder(VkQueue queue);
    static VkEncoder* getThreadLocalEncoder();

    static void setSeqnoPtr(uint32_t* seqnoptr);
    static ALWAYS_INLINE_GFXSTREAM uint32_t nextSeqno();
    static ALWAYS_INLINE_GFXSTREAM uint32_t getSeqno();

    // Transforms
    void deviceMemoryTransform_tohost(VkDeviceMemory* memory, uint32_t memoryCount,
                                      VkDeviceSize* offset, uint32_t offsetCount,
                                      VkDeviceSize* size, uint32_t sizeCount, uint32_t* typeIndex,
                                      uint32_t typeIndexCount, uint32_t* typeBits,
                                      uint32_t typeBitsCount);
    void deviceMemoryTransform_fromhost(VkDeviceMemory* memory, uint32_t memoryCount,
                                        VkDeviceSize* offset, uint32_t offsetCount,
                                        VkDeviceSize* size, uint32_t sizeCount, uint32_t* typeIndex,
                                        uint32_t typeIndexCount, uint32_t* typeBits,
                                        uint32_t typeBitsCount);

    void transformImpl_VkExternalMemoryProperties_fromhost(VkExternalMemoryProperties* pProperties,
                                                           uint32_t);
    void transformImpl_VkExternalMemoryProperties_tohost(VkExternalMemoryProperties* pProperties,
                                                         uint32_t);
    void transformImpl_VkImageCreateInfo_fromhost(const VkImageCreateInfo*, uint32_t);
    void transformImpl_VkImageCreateInfo_tohost(const VkImageCreateInfo*, uint32_t);

#define DEFINE_TRANSFORMED_TYPE_PROTOTYPE(type)          \
    void transformImpl_##type##_tohost(type*, uint32_t); \
    void transformImpl_##type##_fromhost(type*, uint32_t);

    LIST_TRIVIAL_TRANSFORMED_TYPES(DEFINE_TRANSFORMED_TYPE_PROTOTYPE)

   private:
    VulkanHandleMapping* mCreateMapping = nullptr;
    VulkanHandleMapping* mDestroyMapping = nullptr;

    uint32_t getColorBufferMemoryIndex(void* context, VkDevice device);
    const VkPhysicalDeviceMemoryProperties& getPhysicalDeviceMemoryProperties(
        void* context, VkDevice device, VkPhysicalDevice physicalDevice);

    VkResult on_vkGetPhysicalDeviceImageFormatProperties2_common(
        bool isKhr, void* context, VkResult input_result, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
        VkImageFormatProperties2* pImageFormatProperties);

    void on_vkGetPhysicalDeviceExternalBufferProperties_common(
        bool isKhr, void* context, VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo,
        VkExternalBufferProperties* pExternalBufferProperties);

    template <typename VkSubmitInfoType>
    VkResult on_vkQueueSubmitTemplate(void* context, VkResult input_result, VkQueue queue,
                                      uint32_t submitCount, const VkSubmitInfoType* pSubmits,
                                      VkFence fence);

    void freeDescriptorSetsIfHostAllocated(VkEncoder* enc, VkDevice device,
                                           uint32_t descriptorSetCount,
                                           const VkDescriptorSet* sets);
    void clearDescriptorPoolAndUnregisterDescriptorSets(void* context, VkDevice device,
                                                        VkDescriptorPool pool);

    void setDeviceInfo(VkDevice device, VkPhysicalDevice physdev, VkPhysicalDeviceProperties props,
                       VkPhysicalDeviceMemoryProperties memProps, uint32_t enabledExtensionCount,
                       const char* const* ppEnabledExtensionNames, const void* pNext);

    void setDeviceMemoryInfo(VkDevice device, VkDeviceMemory memory, VkDeviceSize allocationSize,
                             uint8_t* ptr, uint32_t memoryTypeIndex, void* ahw, bool imported,
                             zx_handle_t vmoHandle, VirtGpuResourcePtr blobPtr);

    void setImageInfo(VkImage image, VkDevice device, const VkImageCreateInfo* pCreateInfo);

    bool supportsDeferredCommands() const;
    bool supportsAsyncQueueSubmit() const;
    bool supportsCreateResourcesWithRequirements() const;

    int getHostInstanceExtensionIndex(const std::string& extName) const;
    int getHostDeviceExtensionIndex(const std::string& extName) const;

    CoherentMemoryPtr createCoherentMemory(VkDevice device, VkDeviceMemory mem,
                                           const VkMemoryAllocateInfo& hostAllocationInfo,
                                           VkEncoder* enc, VkResult& res);
    VkResult allocateCoherentMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                                    VkEncoder* enc, VkDeviceMemory* pMemory);

    VkResult getCoherentMemory(const VkMemoryAllocateInfo* pAllocateInfo, VkEncoder* enc,
                               VkDevice device, VkDeviceMemory* pMemory);

    void transformImageMemoryRequirements2ForGuest(VkImage image, VkMemoryRequirements2* reqs2);

    void transformBufferMemoryRequirements2ForGuest(VkBuffer buffer, VkMemoryRequirements2* reqs2);

    void flushCommandBufferPendingCommandsBottomUp(void* context, VkQueue queue,
                                                   const std::vector<VkCommandBuffer>& workingSet);

    template <class VkSubmitInfoType>
    void flushStagingStreams(void* context, VkQueue queue, uint32_t submitCount,
                             const VkSubmitInfoType* pSubmits);

    VkResult vkQueueSubmitEnc(VkEncoder* enc, VkQueue queue, uint32_t submitCount,
                              const VkSubmitInfo* pSubmits, VkFence fence);

    VkResult vkQueueSubmitEnc(VkEncoder* enc, VkQueue queue, uint32_t submitCount,
                              const VkSubmitInfo2* pSubmits, VkFence fence);

    VkResult initDescriptorUpdateTemplateBuffers(
        const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
        VkDescriptorUpdateTemplate descriptorUpdateTemplate);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    VkResult exportSyncFdForQSRILocked(VkImage image, int* fd);
#endif

    void setInstanceInfo(VkInstance instance, uint32_t enabledExtensionCount,
                         const char* const* ppEnabledExtensionNames, uint32_t apiVersion);

    void resetCommandBufferStagingInfo(VkCommandBuffer commandBuffer, bool alsoResetPrimaries,
                                       bool alsoClearPendingDescriptorSets);

    void resetCommandBufferPendingTopology(VkCommandBuffer commandBuffer);

    void clearCommandPool(VkCommandPool commandPool);

    void ensureSyncDeviceFd(void);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    void unwrap_VkNativeBufferANDROID(const VkNativeBufferANDROID* inputNativeInfo,
                                      VkNativeBufferANDROID* outputNativeInfo);

    void unwrap_VkBindImageMemorySwapchainInfoKHR(
        const VkBindImageMemorySwapchainInfoKHR* inputBimsi,
        VkBindImageMemorySwapchainInfoKHR* outputBimsi);
#endif

#if defined(VK_USE_PLATFORM_FUCHSIA)
    VkResult getBufferCollectionImageCreateInfoIndexLocked(
        VkBufferCollectionFUCHSIA collection, fuchsia_sysmem::wire::BufferCollectionInfo2& info,
        uint32_t* outCreateInfoIndex);

    SetBufferCollectionImageConstraintsResult setBufferCollectionImageConstraintsImpl(
        VkEncoder* enc, VkDevice device,
        fidl::WireSyncClient<fuchsia_sysmem::BufferCollection>* pCollection,
        const VkImageConstraintsInfoFUCHSIA* pImageConstraintsInfo);

    VkResult setBufferCollectionConstraintsFUCHSIA(
        VkEncoder* enc, VkDevice device,
        fidl::WireSyncClient<fuchsia_sysmem::BufferCollection>* collection,
        const VkImageCreateInfo* pImageInfo);
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
    uint64_t getAHardwareBufferId(AHardwareBuffer* ahw);
#endif

    void unregister_VkDescriptorSet_locked(VkDescriptorSet set);

#define HANDLE_DEFINE_TRIVIAL_INFO_STRUCT(type) \
    struct type##_Info {                        \
        uint32_t unused;                        \
    };
    GOLDFISH_VK_LIST_TRIVIAL_HANDLE_TYPES(HANDLE_DEFINE_TRIVIAL_INFO_STRUCT)

    struct VkInstance_Info {
        uint32_t highestApiVersion;
        std::set<std::string> enabledExtensions;
        // Fodder for vkEnumeratePhysicalDevices.
        std::vector<VkPhysicalDevice> physicalDevices;
    };

    struct VkDevice_Info {
        VkPhysicalDevice physdev;
        VkPhysicalDeviceProperties props;
        VkPhysicalDeviceMemoryProperties memProps;
        uint32_t apiVersion;
        std::set<std::string> enabledExtensions;
        std::vector<std::pair<PFN_vkDeviceMemoryReportCallbackEXT, void*>>
            deviceMemoryReportCallbacks;
    };

    struct VkDeviceMemory_Info {
        bool dedicated = false;
        bool imported = false;

#ifdef VK_USE_PLATFORM_ANDROID_KHR
        AHardwareBuffer* ahw = nullptr;
#endif
        zx_handle_t vmoHandle = ZX_HANDLE_INVALID;
        VkDevice device;

        uint8_t* ptr = nullptr;

        uint64_t blobId = 0;
        uint64_t allocationSize = 0;
        uint32_t memoryTypeIndex = 0;
        uint64_t coherentMemorySize = 0;
        uint64_t coherentMemoryOffset = 0;

#if defined(__ANDROID__)
        GoldfishAddressSpaceBlockPtr goldfishBlock = nullptr;
#endif  // defined(__ANDROID__)
        CoherentMemoryPtr coherentMemory = nullptr;
        VirtGpuResourcePtr blobPtr = nullptr;
    };

    struct VkCommandBuffer_Info {
        uint32_t placeholder;
    };

    struct VkQueue_Info {
        VkDevice device;
    };

    // custom guest-side structs for images/buffers because of AHardwareBuffer :((
    struct VkImage_Info {
        VkDevice device;
        VkImageCreateInfo createInfo;
        bool external = false;
        VkExternalMemoryImageCreateInfo externalCreateInfo;
        VkDeviceMemory currentBacking = VK_NULL_HANDLE;
        VkDeviceSize currentBackingOffset = 0;
        VkDeviceSize currentBackingSize = 0;
        bool baseRequirementsKnown = false;
        VkMemoryRequirements baseRequirements;
#ifdef VK_USE_PLATFORM_ANDROID_KHR
        bool hasExternalFormat = false;
        unsigned externalFourccFormat = 0;
        std::vector<int> pendingQsriSyncFds;
#endif
#ifdef VK_USE_PLATFORM_FUCHSIA
        bool isSysmemBackedMemory = false;
#endif
#ifdef LINUX_GUEST_BUILD
        bool isDmaBufImage = false;
        VkImage linearPeerImage = VK_NULL_HANDLE;
#endif
    };

    struct VkBuffer_Info {
        VkDevice device;
        VkBufferCreateInfo createInfo;
        bool external = false;
        VkExternalMemoryBufferCreateInfo externalCreateInfo;
        VkDeviceMemory currentBacking = VK_NULL_HANDLE;
        VkDeviceSize currentBackingOffset = 0;
        VkDeviceSize currentBackingSize = 0;
        bool baseRequirementsKnown = false;
        VkMemoryRequirements baseRequirements;
#ifdef VK_USE_PLATFORM_FUCHSIA
        bool isSysmemBackedMemory = false;
#endif
    };

    struct VkSemaphore_Info {
        VkDevice device;
        zx_handle_t eventHandle = ZX_HANDLE_INVALID;
        zx_koid_t eventKoid = ZX_KOID_INVALID;
        std::optional<int> syncFd = {};
    };

    struct VkDescriptorUpdateTemplate_Info {
        uint32_t templateEntryCount = 0;
        VkDescriptorUpdateTemplateEntry* templateEntries;

        uint32_t imageInfoCount = 0;
        uint32_t bufferInfoCount = 0;
        uint32_t bufferViewCount = 0;
        uint32_t inlineUniformBlockCount = 0;
        uint32_t* imageInfoIndices;
        uint32_t* bufferInfoIndices;
        uint32_t* bufferViewIndices;
        VkDescriptorImageInfo* imageInfos;
        VkDescriptorBufferInfo* bufferInfos;
        VkBufferView* bufferViews;
        std::vector<uint8_t> inlineUniformBlockBuffer;
        std::vector<uint32_t> inlineUniformBlockBytesPerBlocks;  // bytes per uniform block
    };

    struct VkFence_Info {
        VkDevice device;
        bool external = false;
        VkExportFenceCreateInfo exportFenceCreateInfo;
#if defined(VK_USE_PLATFORM_ANDROID_KHR) || defined(__linux__)
        // Note: -1 means already signaled.
        std::optional<int> syncFd;
#endif
    };

    struct VkDescriptorPool_Info {
        uint32_t unused;
    };

    struct VkDescriptorSet_Info {
        uint32_t unused;
    };

    struct VkDescriptorSetLayout_Info {
        uint32_t unused;
    };

    struct VkCommandPool_Info {
        uint32_t unused;
    };

    struct VkSampler_Info {
        uint32_t unused;
    };

    struct VkBufferCollectionFUCHSIA_Info {
#ifdef VK_USE_PLATFORM_FUCHSIA
        std::optional<fuchsia_sysmem::wire::BufferCollectionConstraints> constraints;
        std::optional<VkBufferCollectionPropertiesFUCHSIA> properties;

        // the index of corresponding createInfo for each image format
        // constraints in |constraints|.
        std::vector<uint32_t> createInfoIndex;
#endif  // VK_USE_PLATFORM_FUCHSIA
    };

    VkDescriptorImageInfo filterNonexistentSampler(const VkDescriptorImageInfo& inputInfo);

    void emitDeviceMemoryReport(VkDevice_Info info, VkDeviceMemoryReportEventTypeEXT type,
                                uint64_t memoryObjectId, VkDeviceSize size, VkObjectType objectType,
                                uint64_t objectHandle, uint32_t heapIndex = 0);

    void transformImageMemoryRequirementsForGuestLocked(VkImage image, VkMemoryRequirements* reqs);
    CoherentMemoryPtr freeCoherentMemoryLocked(VkDeviceMemory memory, VkDeviceMemory_Info& info);

    std::recursive_mutex mLock;

    std::optional<const VkPhysicalDeviceMemoryProperties> mCachedPhysicalDeviceMemoryProps;

    struct GfxStreamVkFeatureInfo mFeatureInfo = {};

#if defined(__ANDROID__)
    std::unique_ptr<GoldfishAddressSpaceBlockProvider> mGoldfishAddressSpaceBlockProvider;
#endif  // defined(__ANDROID__)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    std::unique_ptr<gfxstream::Gralloc> mGralloc = nullptr;
#endif

    std::unique_ptr<gfxstream::SyncHelper> mSyncHelper = nullptr;

    struct VirtGpuCaps mCaps;
    std::vector<VkExtensionProperties> mHostInstanceExtensions;
    std::vector<VkExtensionProperties> mHostDeviceExtensions;

    // 32 bits only for now, upper bits may be used later.
    std::atomic<uint32_t> mAtomicId = 0;
#if defined(VK_USE_PLATFORM_ANDROID_KHR) || defined(__linux__)
    int mSyncDeviceFd = -1;
#endif

#ifdef VK_USE_PLATFORM_FUCHSIA
    fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> mControlDevice;
    fidl::WireSyncClient<fuchsia_sysmem::Allocator> mSysmemAllocator;
#endif

#define HANDLE_REGISTER_DECLARATION(type) std::unordered_map<type, type##_Info> info_##type;

    GOLDFISH_VK_LIST_HANDLE_TYPES(HANDLE_REGISTER_DECLARATION)

    std::unordered_map<const VkEncoder*, std::unordered_map<void*, CleanupCallback>>
        mEncoderCleanupCallbacks;
};

}  // namespace vk
}  // namespace gfxstream
