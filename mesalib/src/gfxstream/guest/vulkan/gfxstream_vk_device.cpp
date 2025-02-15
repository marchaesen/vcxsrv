/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>

#include "GfxStreamConnectionManager.h"
#include "GfxStreamRenderControl.h"
#include "GfxStreamVulkanConnection.h"
#include "ResourceTracker.h"
#include "VkEncoder.h"
#include "gfxstream_vk_entrypoints.h"
#include "gfxstream_vk_private.h"
#include "util/detect_os.h"
#include "util/perf/cpu_trace.h"
#include "vk_sync_dummy.h"
#include "vk_util.h"

uint32_t gSeqno = 0;
uint32_t gNoRenderControlEnc = 0;

static gfxstream::vk::VkEncoder* getVulkanEncoder(GfxStreamConnectionManager* mgr) {
    if (!gNoRenderControlEnc) {
        int32_t ret = renderControlInit(mgr, nullptr);
        if (ret) {
            mesa_loge("Failed to initialize renderControl when getting VK encoder");
            return nullptr;
        }
    }

    gfxstream::vk::VkEncoder* vkEncoder =
        (gfxstream::vk::VkEncoder*)mgr->getEncoder(GFXSTREAM_CONNECTION_VULKAN);

    if (vkEncoder == nullptr) {
        auto stream = mgr->getStream();
        int32_t ret = mgr->addConnection(GFXSTREAM_CONNECTION_VULKAN,
                                         std::make_unique<GfxStreamVulkanConnection>(stream));
        if (ret) {
            return nullptr;
        }

        vkEncoder = (gfxstream::vk::VkEncoder*)mgr->getEncoder(GFXSTREAM_CONNECTION_VULKAN);
    }

    return vkEncoder;
}

static GfxStreamConnectionManager* getConnectionManager(void) {
    auto transport = renderControlGetTransport();
    return GfxStreamConnectionManager::getThreadLocalInstance(transport, kCapsetGfxStreamVulkan);
}

namespace {

static bool instance_extension_table_initialized = false;
static struct vk_instance_extension_table gfxstream_vk_instance_extensions_supported = {};

// Provided by Mesa components only; never encoded/decoded through gfxstream
static const char* const kGuestOnlyInstanceExtension[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(GFXSTREAM_VK_WAYLAND)
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
#if defined(GFXSTREAM_VK_X11)
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

static const char* const kGuestOnlyDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static VkResult SetupInstanceForProcess(void) {
    auto mgr = getConnectionManager();
    if (!mgr) {
        mesa_loge("vulkan: Failed to get host connection\n");
        return VK_ERROR_DEVICE_LOST;
    }

    gfxstream::vk::ResourceTracker::get()->setupCaps(gNoRenderControlEnc);
    gfxstream::vk::ResourceTracker::get()->setupPlatformHelpers();
    // Legacy goldfish path: could be deleted once goldfish not used guest-side.
    if (!gNoRenderControlEnc) {
        struct GfxStreamVkFeatureInfo features = {};
        int32_t ret = renderControlInit(mgr, &features);
        if (ret) {
            mesa_loge("Failed to initialize renderControl ");
            return VK_ERROR_DEVICE_LOST;
        }

        gfxstream::vk::ResourceTracker::get()->setupFeatures(&features);
    }

    gfxstream::vk::ResourceTracker::get()->setThreadingCallbacks({
        .hostConnectionGetFunc = getConnectionManager,
        .vkEncoderGetFunc = getVulkanEncoder,
    });
    gfxstream::vk::ResourceTracker::get()->setSeqnoPtr(&gSeqno);
    gfxstream::vk::VkEncoder* vkEnc = getVulkanEncoder(mgr);
    if (!vkEnc) {
        mesa_loge("vulkan: Failed to get Vulkan encoder\n");
        return VK_ERROR_DEVICE_LOST;
    }

    return VK_SUCCESS;
}

static bool isGuestOnlyInstanceExtension(const char* name) {
    for (auto mesaExt : kGuestOnlyInstanceExtension) {
        if (!strncmp(mesaExt, name, VK_MAX_EXTENSION_NAME_SIZE)) return true;
    }
    return false;
}

static bool isGuestOnlyDeviceExtension(const char* name) {
    for (auto mesaExt : kGuestOnlyDeviceExtensions) {
        if (!strncmp(mesaExt, name, VK_MAX_EXTENSION_NAME_SIZE)) return true;
    }
    return false;
}

// Filtered extension names for encoding
static std::vector<const char*> filteredInstanceExtensionNames(uint32_t count,
                                                               const char* const* extNames) {
    std::vector<const char*> retList;
    for (uint32_t i = 0; i < count; ++i) {
        auto extName = extNames[i];
        if (!isGuestOnlyInstanceExtension(extName)) {
            retList.push_back(extName);
        }
    }
    return retList;
}

static std::vector<const char*> filteredDeviceExtensionNames(uint32_t count,
                                                             const char* const* extNames) {
    std::vector<const char*> retList;
    for (uint32_t i = 0; i < count; ++i) {
        auto extName = extNames[i];
        if (!isGuestOnlyDeviceExtension(extName)) {
            retList.push_back(extName);
        }
    }
    return retList;
}

static void get_device_extensions(VkPhysicalDevice physDevInternal,
                                  struct vk_device_extension_table* deviceExts) {
    VkResult result = (VkResult)0;
    auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
    auto resources = gfxstream::vk::ResourceTracker::get();
    uint32_t numDeviceExts = 0;
    result = resources->on_vkEnumerateDeviceExtensionProperties(vkEnc, VK_SUCCESS, physDevInternal,
                                                                NULL, &numDeviceExts, NULL);
    if (VK_SUCCESS == result) {
        std::vector<VkExtensionProperties> extProps(numDeviceExts);
        result = resources->on_vkEnumerateDeviceExtensionProperties(
            vkEnc, VK_SUCCESS, physDevInternal, NULL, &numDeviceExts, extProps.data());
        if (VK_SUCCESS == result) {
            // device extensions from gfxstream
            for (uint32_t i = 0; i < numDeviceExts; i++) {
                for (uint32_t j = 0; j < VK_DEVICE_EXTENSION_COUNT; j++) {
                    if (0 == strncmp(extProps[i].extensionName,
                                     vk_device_extensions[j].extensionName,
                                     VK_MAX_EXTENSION_NAME_SIZE)) {
                        deviceExts->extensions[j] = true;
                        break;
                    }
                }
            }
            // device extensions from Mesa
            for (uint32_t j = 0; j < VK_DEVICE_EXTENSION_COUNT; j++) {
                if (isGuestOnlyDeviceExtension(vk_device_extensions[j].extensionName)) {
                    deviceExts->extensions[j] = true;
                    break;
                }
            }
        }
    }
}

static VkResult gfxstream_vk_physical_device_init(
    struct gfxstream_vk_physical_device* physical_device, struct gfxstream_vk_instance* instance,
    VkPhysicalDevice internal_object) {
    struct vk_device_extension_table supported_extensions = {};
    get_device_extensions(internal_object, &supported_extensions);

    struct vk_physical_device_dispatch_table dispatch_table;
    memset(&dispatch_table, 0, sizeof(struct vk_physical_device_dispatch_table));
    vk_physical_device_dispatch_table_from_entrypoints(
        &dispatch_table, &gfxstream_vk_physical_device_entrypoints, false);
#if !DETECT_OS_FUCHSIA
    vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                       &wsi_physical_device_entrypoints, false);
#endif

    // Initialize the mesa object
    VkResult result = vk_physical_device_init(&physical_device->vk, &instance->vk,
                                              &supported_extensions, NULL, NULL, &dispatch_table);

    if (VK_SUCCESS == result) {
        // Set the gfxstream-internal object
        physical_device->internal_object = internal_object;
        physical_device->instance = instance;
        // Note: Must use dummy_sync for correct sync object path in WSI operations
        physical_device->sync_types[0] = &vk_sync_dummy_type;
        physical_device->sync_types[1] = NULL;
        physical_device->vk.supported_sync_types = physical_device->sync_types;

        result = gfxstream_vk_wsi_init(physical_device);
    }

    return result;
}

static void gfxstream_vk_physical_device_finish(
    struct gfxstream_vk_physical_device* physical_device) {
    gfxstream_vk_wsi_finish(physical_device);

    vk_physical_device_finish(&physical_device->vk);
}

static void gfxstream_vk_destroy_physical_device(struct vk_physical_device* physical_device) {
    gfxstream_vk_physical_device_finish((struct gfxstream_vk_physical_device*)physical_device);
    vk_free(&physical_device->instance->alloc, physical_device);
}

static VkResult gfxstream_vk_enumerate_devices(struct vk_instance* vk_instance) {
    VkResult result = VK_SUCCESS;
    gfxstream_vk_instance* gfxstream_instance = (gfxstream_vk_instance*)vk_instance;
    uint32_t deviceCount = 0;
    auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
    auto resources = gfxstream::vk::ResourceTracker::get();
    result = resources->on_vkEnumeratePhysicalDevices(
        vkEnc, VK_SUCCESS, gfxstream_instance->internal_object, &deviceCount, NULL);
    if (VK_SUCCESS != result) return result;
    std::vector<VkPhysicalDevice> internal_list(deviceCount);
    result = resources->on_vkEnumeratePhysicalDevices(
        vkEnc, VK_SUCCESS, gfxstream_instance->internal_object, &deviceCount, internal_list.data());

    if (VK_SUCCESS == result) {
        for (uint32_t i = 0; i < deviceCount; i++) {
            struct gfxstream_vk_physical_device* gfxstream_physicalDevice =
                (struct gfxstream_vk_physical_device*)vk_zalloc(
                    &gfxstream_instance->vk.alloc, sizeof(struct gfxstream_vk_physical_device), GFXSTREAM_DEFAULT_ALIGN,
                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (!gfxstream_physicalDevice) {
                result = VK_ERROR_OUT_OF_HOST_MEMORY;
                break;
            }
            result = gfxstream_vk_physical_device_init(gfxstream_physicalDevice, gfxstream_instance,
                                                       internal_list[i]);
            if (VK_SUCCESS == result) {
                list_addtail(&gfxstream_physicalDevice->vk.link,
                             &gfxstream_instance->vk.physical_devices.list);
            } else {
                vk_free(&gfxstream_instance->vk.alloc, gfxstream_physicalDevice);
                break;
            }
        }
    }

    return result;
}

static struct vk_instance_extension_table* get_instance_extensions() {
    struct vk_instance_extension_table* const retTablePtr =
        &gfxstream_vk_instance_extensions_supported;
    if (!instance_extension_table_initialized) {
        VkResult result = SetupInstanceForProcess();
        if (VK_SUCCESS == result) {
            auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
            auto resources = gfxstream::vk::ResourceTracker::get();
            uint32_t numInstanceExts = 0;
            result = resources->on_vkEnumerateInstanceExtensionProperties(vkEnc, VK_SUCCESS, NULL,
                                                                          &numInstanceExts, NULL);
            if (VK_SUCCESS == result) {
                std::vector<VkExtensionProperties> extProps(numInstanceExts);
                result = resources->on_vkEnumerateInstanceExtensionProperties(
                    vkEnc, VK_SUCCESS, NULL, &numInstanceExts, extProps.data());
                if (VK_SUCCESS == result) {
                    // instance extensions from gfxstream
                    for (uint32_t i = 0; i < numInstanceExts; i++) {
                        for (uint32_t j = 0; j < VK_INSTANCE_EXTENSION_COUNT; j++) {
                            if (0 == strncmp(extProps[i].extensionName,
                                             vk_instance_extensions[j].extensionName,
                                             VK_MAX_EXTENSION_NAME_SIZE)) {
                                gfxstream_vk_instance_extensions_supported.extensions[j] = true;
                                break;
                            }
                        }
                    }
                    // instance extensions from Mesa
                    for (uint32_t j = 0; j < VK_INSTANCE_EXTENSION_COUNT; j++) {
                        if (isGuestOnlyInstanceExtension(vk_instance_extensions[j].extensionName)) {
                            gfxstream_vk_instance_extensions_supported.extensions[j] = true;
                        }
                    }
                    instance_extension_table_initialized = true;
                }
            }
        }
    }
    return retTablePtr;
}

}  // namespace

VkResult gfxstream_vk_CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkInstance* pInstance) {
    MESA_TRACE_SCOPE("vkCreateInstance");

    struct gfxstream_vk_instance* instance;

    pAllocator = pAllocator ?: vk_default_allocator();
    instance = (struct gfxstream_vk_instance*)vk_zalloc(pAllocator, sizeof(*instance), GFXSTREAM_DEFAULT_ALIGN,
                                                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (NULL == instance) {
        return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
    }

    VkResult result = VK_SUCCESS;
    /* Encoder call */
    {
        result = SetupInstanceForProcess();
        if (VK_SUCCESS != result) {
            vk_free(pAllocator, instance);
            return vk_error(NULL, result);
        }

        // Full local copy of pCreateInfo
        VkInstanceCreateInfo localCreateInfo = *pCreateInfo;
        std::vector<const char*> filteredExts = filteredInstanceExtensionNames(
            localCreateInfo.enabledExtensionCount, localCreateInfo.ppEnabledExtensionNames);
        localCreateInfo.enabledExtensionCount = static_cast<uint32_t>(filteredExts.size());
        localCreateInfo.ppEnabledExtensionNames = filteredExts.data();

        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        result = vkEnc->vkCreateInstance(&localCreateInfo, nullptr, &instance->internal_object,
                                         true /* do lock */);
        if (VK_SUCCESS != result) {
            vk_free(pAllocator, instance);
            return vk_error(NULL, result);
        }
    }

    struct vk_instance_dispatch_table dispatch_table;
    memset(&dispatch_table, 0, sizeof(struct vk_instance_dispatch_table));
    vk_instance_dispatch_table_from_entrypoints(&dispatch_table, &gfxstream_vk_instance_entrypoints,
                                                false);
#if !DETECT_OS_FUCHSIA
    vk_instance_dispatch_table_from_entrypoints(&dispatch_table, &wsi_instance_entrypoints, false);
#endif

    result = vk_instance_init(&instance->vk, get_instance_extensions(), &dispatch_table,
                              pCreateInfo, pAllocator);

    if (result != VK_SUCCESS) {
        vk_free(pAllocator, instance);
        return vk_error(NULL, result);
    }

    // Note: Do not support try_create_for_drm. virtio_gpu DRM device opened in
    // init_renderer above, which can still enumerate multiple physical devices on the host.
    instance->vk.physical_devices.enumerate = gfxstream_vk_enumerate_devices;
    instance->vk.physical_devices.destroy = gfxstream_vk_destroy_physical_device;

    *pInstance = gfxstream_vk_instance_to_handle(instance);
    return VK_SUCCESS;
}

void gfxstream_vk_DestroyInstance(VkInstance _instance, const VkAllocationCallbacks* pAllocator) {
    MESA_TRACE_SCOPE("vkDestroyInstance");
    if (VK_NULL_HANDLE == _instance) return;

    VK_FROM_HANDLE(gfxstream_vk_instance, instance, _instance);

    auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
    vkEnc->vkDestroyInstance(instance->internal_object, pAllocator, true /* do lock */);

    vk_instance_finish(&instance->vk);
    vk_free(&instance->vk.alloc, instance);

    // To make End2EndTests happy, since now the host connection is statically linked to
    // libvulkan_ranchu.so [separate HostConnections now].
#if defined(END2END_TESTS)
    GfxStreamConnectionManager* mgr = getConnectionManager();
    mgr->threadLocalExit();
    VirtGpuDevice::resetInstance();
    gSeqno = 0;
#endif
}

VkResult gfxstream_vk_EnumerateInstanceExtensionProperties(const char* pLayerName,
                                                           uint32_t* pPropertyCount,
                                                           VkExtensionProperties* pProperties) {
    MESA_TRACE_SCOPE("vkvkEnumerateInstanceExtensionProperties");
    (void)pLayerName;

    return vk_enumerate_instance_extension_properties(get_instance_extensions(), pPropertyCount,
                                                      pProperties);
}

VkResult gfxstream_vk_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                         const char* pLayerName,
                                                         uint32_t* pPropertyCount,
                                                         VkExtensionProperties* pProperties) {
    MESA_TRACE_SCOPE("vkEnumerateDeviceExtensionProperties");
    (void)pLayerName;
    VK_FROM_HANDLE(vk_physical_device, pdevice, physicalDevice);

    VK_OUTARRAY_MAKE_TYPED(VkExtensionProperties, out, pProperties, pPropertyCount);

    for (int i = 0; i < VK_DEVICE_EXTENSION_COUNT; i++) {
        if (!pdevice->supported_extensions.extensions[i]) continue;

        vk_outarray_append_typed(VkExtensionProperties, &out, prop) {
            *prop = vk_device_extensions[i];
        }
    }

    return vk_outarray_status(&out);
}

VkResult gfxstream_vk_CreateDevice(VkPhysicalDevice physicalDevice,
                                   const VkDeviceCreateInfo* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    MESA_TRACE_SCOPE("vkCreateDevice");
    VK_FROM_HANDLE(gfxstream_vk_physical_device, gfxstream_physicalDevice, physicalDevice);
    VkResult result = (VkResult)0;

    /*
     * Android's libvulkan implements VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT, but
     * passes it to the underlying driver anyways.  See:
     *
     * https://android-review.googlesource.com/c/platform/hardware/google/gfxstream/+/2839438
     *
     * and associated bugs. Mesa VK runtime also checks this, so we have to filter out before
     * reaches it.
     */
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT* mutableSwapchainMaintenance1Features =
        vk_find_struct(const_cast<VkDeviceCreateInfo*>(pCreateInfo),
                       PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT);
    if (mutableSwapchainMaintenance1Features) {
        mutableSwapchainMaintenance1Features->swapchainMaintenance1 = VK_FALSE;
    }

    const VkAllocationCallbacks* pMesaAllocator =
        pAllocator ?: &gfxstream_physicalDevice->instance->vk.alloc;
    struct gfxstream_vk_device* gfxstream_device = (struct gfxstream_vk_device*)vk_zalloc(
        pMesaAllocator, sizeof(struct gfxstream_vk_device), GFXSTREAM_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    result = gfxstream_device ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
    if (VK_SUCCESS == result) {
        // Full local copy of pCreateInfo
        VkDeviceCreateInfo localCreateInfo = *pCreateInfo;

        std::vector<const char*> filteredExts = filteredDeviceExtensionNames(
            localCreateInfo.enabledExtensionCount, localCreateInfo.ppEnabledExtensionNames);
        localCreateInfo.enabledExtensionCount = static_cast<uint32_t>(filteredExts.size());
        localCreateInfo.ppEnabledExtensionNames = filteredExts.data();

        /* pNext = VkPhysicalDeviceGroupProperties */
        std::vector<VkPhysicalDevice> initialPhysicalDeviceList;
        VkPhysicalDeviceGroupProperties* mutablePhysicalDeviceGroupProperties = vk_find_struct(&localCreateInfo, PHYSICAL_DEVICE_GROUP_PROPERTIES);
        if (mutablePhysicalDeviceGroupProperties) {
            // Temporarily modify the VkPhysicalDeviceGroupProperties structure to use translated
            // VkPhysicalDevice references for the encoder call
            for (uint32_t physDev = 0;
                 physDev < mutablePhysicalDeviceGroupProperties->physicalDeviceCount; physDev++) {
                initialPhysicalDeviceList.push_back(
                    mutablePhysicalDeviceGroupProperties->physicalDevices[physDev]);
                VK_FROM_HANDLE(gfxstream_vk_physical_device, gfxstream_physicalDevice,
                               mutablePhysicalDeviceGroupProperties->physicalDevices[physDev]);
                mutablePhysicalDeviceGroupProperties->physicalDevices[physDev] =
                    gfxstream_physicalDevice->internal_object;
            }
        }

        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        result = vkEnc->vkCreateDevice(gfxstream_physicalDevice->internal_object, &localCreateInfo,
                                       pAllocator, &gfxstream_device->internal_object,
                                       true /* do lock */);

        if (mutablePhysicalDeviceGroupProperties) {
            // Revert the physicalDevice list in VkPhysicalDeviceGroupProperties to the user-set
            // data
            for (uint32_t physDev = 0;
                 physDev < mutablePhysicalDeviceGroupProperties->physicalDeviceCount; physDev++) {
                initialPhysicalDeviceList.push_back(
                    mutablePhysicalDeviceGroupProperties->physicalDevices[physDev]);
                mutablePhysicalDeviceGroupProperties->physicalDevices[physDev] =
                    initialPhysicalDeviceList[physDev];
            }
        }
    }
    if (VK_SUCCESS == result) {
        struct vk_device_dispatch_table dispatch_table;
        memset(&dispatch_table, 0, sizeof(struct vk_device_dispatch_table));
        vk_device_dispatch_table_from_entrypoints(&dispatch_table, &gfxstream_vk_device_entrypoints,
                                                  false);
#if !DETECT_OS_FUCHSIA
        vk_device_dispatch_table_from_entrypoints(&dispatch_table, &wsi_device_entrypoints, false);
#endif

        result = vk_device_init(&gfxstream_device->vk, &gfxstream_physicalDevice->vk,
                                &dispatch_table, pCreateInfo, pMesaAllocator);
    }
    if (VK_SUCCESS == result) {
        gfxstream_device->physical_device = gfxstream_physicalDevice;
        // TODO: Initialize cmd_dispatch for emulated secondary command buffer support?
        gfxstream_device->vk.command_dispatch_table = &gfxstream_device->cmd_dispatch;
        *pDevice = gfxstream_vk_device_to_handle(gfxstream_device);
    } else {
        vk_free(pMesaAllocator, gfxstream_device);
    }

    return result;
}

void gfxstream_vk_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    MESA_TRACE_SCOPE("vkDestroyDevice");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    if (VK_NULL_HANDLE == device) return;

    auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
    vkEnc->vkDestroyDevice(gfxstream_device->internal_object, pAllocator, true /* do lock */);

    /* Must destroy device queues manually */
    vk_foreach_queue_safe(queue, &gfxstream_device->vk) {
        vk_queue_finish(queue);
        vk_free(&gfxstream_device->vk.alloc, queue);
    }
    vk_device_finish(&gfxstream_device->vk);
    vk_free(&gfxstream_device->vk.alloc, gfxstream_device);
}

void gfxstream_vk_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex,
                                 VkQueue* pQueue) {
    MESA_TRACE_SCOPE("vkGetDeviceQueue");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    struct gfxstream_vk_queue* gfxstream_queue = (struct gfxstream_vk_queue*)vk_zalloc(
        &gfxstream_device->vk.alloc, sizeof(struct gfxstream_vk_queue), GFXSTREAM_DEFAULT_ALIGN,
        VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
    VkResult result = gfxstream_queue ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
    if (VK_SUCCESS == result) {
        VkDeviceQueueCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .queueFamilyIndex = queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = NULL,
        };
        result =
            vk_queue_init(&gfxstream_queue->vk, &gfxstream_device->vk, &createInfo, queueIndex);
    }
    if (VK_SUCCESS == result) {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        vkEnc->vkGetDeviceQueue(gfxstream_device->internal_object, queueFamilyIndex, queueIndex,
                                &gfxstream_queue->internal_object, true /* do lock */);

        gfxstream_queue->device = gfxstream_device;
        *pQueue = gfxstream_vk_queue_to_handle(gfxstream_queue);
    } else {
        *pQueue = VK_NULL_HANDLE;
    }
}

void gfxstream_vk_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo,
                                  VkQueue* pQueue) {
    MESA_TRACE_SCOPE("vkGetDeviceQueue2");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    struct gfxstream_vk_queue* gfxstream_queue = (struct gfxstream_vk_queue*)vk_zalloc(
        &gfxstream_device->vk.alloc, sizeof(struct gfxstream_vk_queue), GFXSTREAM_DEFAULT_ALIGN,
        VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
    VkResult result = gfxstream_queue ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
    if (VK_SUCCESS == result) {
        VkDeviceQueueCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = NULL,
            .flags = pQueueInfo->flags,
            .queueFamilyIndex = pQueueInfo->queueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = NULL,
        };
        result = vk_queue_init(&gfxstream_queue->vk, &gfxstream_device->vk, &createInfo,
                               pQueueInfo->queueIndex);
    }
    if (VK_SUCCESS == result) {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        vkEnc->vkGetDeviceQueue2(gfxstream_device->internal_object, pQueueInfo,
                                 &gfxstream_queue->internal_object, true /* do lock */);

        gfxstream_queue->device = gfxstream_device;
        *pQueue = gfxstream_vk_queue_to_handle(gfxstream_queue);
    } else {
        *pQueue = VK_NULL_HANDLE;
    }
}

/* The loader wants us to expose a second GetInstanceProcAddr function
 * to work around certain LD_PRELOAD issues seen in apps.
 */
extern "C" PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);

extern "C" PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return gfxstream_vk_GetInstanceProcAddr(instance, pName);
}

PFN_vkVoidFunction gfxstream_vk_GetInstanceProcAddr(VkInstance _instance, const char* pName) {
    VK_FROM_HANDLE(gfxstream_vk_instance, instance, _instance);
    return vk_instance_get_proc_addr(&instance->vk, &gfxstream_vk_instance_entrypoints, pName);
}

PFN_vkVoidFunction gfxstream_vk_GetDeviceProcAddr(VkDevice _device, const char* pName) {
    MESA_TRACE_SCOPE("vkGetDeviceProcAddr");
    VK_FROM_HANDLE(gfxstream_vk_device, device, _device);
    return vk_device_get_proc_addr(&device->vk, pName);
}

VkResult gfxstream_vk_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkDeviceMemory* pMemory) {
    MESA_TRACE_SCOPE("vkAllocateMemory");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    VkResult vkAllocateMemory_VkResult_return = (VkResult)0;
    /* VkMemoryDedicatedAllocateInfo */
    VkMemoryDedicatedAllocateInfo* dedicatedAllocInfoPtr = vk_find_struct(
        const_cast<VkMemoryAllocateInfo*>(pAllocateInfo), MEMORY_DEDICATED_ALLOCATE_INFO);
    if (dedicatedAllocInfoPtr) {
        if (dedicatedAllocInfoPtr->buffer) {
            VK_FROM_HANDLE(gfxstream_vk_buffer, gfxstream_buffer, dedicatedAllocInfoPtr->buffer);
            dedicatedAllocInfoPtr->buffer = gfxstream_buffer->internal_object;
        }
    }
    {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        auto resources = gfxstream::vk::ResourceTracker::get();
        vkAllocateMemory_VkResult_return =
            resources->on_vkAllocateMemory(vkEnc, VK_SUCCESS, gfxstream_device->internal_object,
                                           pAllocateInfo, pAllocator, pMemory);
    }
    return vkAllocateMemory_VkResult_return;
}

VkResult gfxstream_vk_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                                       VkLayerProperties* pProperties) {
    MESA_TRACE_SCOPE("vkEnumerateInstanceLayerProperties");
    auto result = SetupInstanceForProcess();
    if (VK_SUCCESS != result) {
        return vk_error(NULL, result);
    }

    VkResult vkEnumerateInstanceLayerProperties_VkResult_return = (VkResult)0;
    {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        vkEnumerateInstanceLayerProperties_VkResult_return =
            vkEnc->vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties,
                                                      true /* do lock */);
    }
    return vkEnumerateInstanceLayerProperties_VkResult_return;
}

VkResult gfxstream_vk_EnumerateInstanceVersion(uint32_t* pApiVersion) {
    MESA_TRACE_SCOPE("vkEnumerateInstanceVersion");
    auto result = SetupInstanceForProcess();
    if (VK_SUCCESS != result) {
        return vk_error(NULL, result);
    }

    VkResult vkEnumerateInstanceVersion_VkResult_return = (VkResult)0;
    {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        vkEnumerateInstanceVersion_VkResult_return =
            vkEnc->vkEnumerateInstanceVersion(pApiVersion, true /* do lock */);
    }
    return vkEnumerateInstanceVersion_VkResult_return;
}

static bool vk_descriptor_type_has_descriptor_buffer(VkDescriptorType type) {
    switch (type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return true;
        default:
            return false;
    }
}

static std::vector<VkWriteDescriptorSet> transformDescriptorSetList(
    const VkWriteDescriptorSet* pDescriptorSets, uint32_t descriptorSetCount,
    std::vector<std::vector<VkDescriptorBufferInfo>>& bufferInfos) {
    std::vector<VkWriteDescriptorSet> outDescriptorSets(descriptorSetCount);
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        const auto& srcDescriptorSet = pDescriptorSets[i];
        const uint32_t descriptorCount = srcDescriptorSet.descriptorCount;

        VkWriteDescriptorSet& outDescriptorSet = outDescriptorSets[i];
        outDescriptorSet = srcDescriptorSet;

        bufferInfos.push_back(std::vector<VkDescriptorBufferInfo>());
        bufferInfos[i].resize(descriptorCount);
        memset(&bufferInfos[i][0], 0, sizeof(VkDescriptorBufferInfo) * descriptorCount);
        for (uint32_t j = 0; j < descriptorCount; ++j) {
            const auto* srcBufferInfo = srcDescriptorSet.pBufferInfo;
            if (srcBufferInfo) {
                bufferInfos[i][j] = srcBufferInfo[j];
                bufferInfos[i][j].buffer = VK_NULL_HANDLE;
                if (vk_descriptor_type_has_descriptor_buffer(srcDescriptorSet.descriptorType) &&
                    srcBufferInfo[j].buffer) {
                    VK_FROM_HANDLE(gfxstream_vk_buffer, gfxstreamBuffer, srcBufferInfo[j].buffer);
                    bufferInfos[i][j].buffer = gfxstreamBuffer->internal_object;
                }
            }
        }
        outDescriptorSet.pBufferInfo = bufferInfos[i].data();
    }
    return outDescriptorSets;
}

void gfxstream_vk_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                       const VkWriteDescriptorSet* pDescriptorWrites,
                                       uint32_t descriptorCopyCount,
                                       const VkCopyDescriptorSet* pDescriptorCopies) {
    MESA_TRACE_SCOPE("vkUpdateDescriptorSets");
    VK_FROM_HANDLE(gfxstream_vk_device, gfxstream_device, device);
    {
        auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder();
        std::vector<std::vector<VkDescriptorBufferInfo>> descriptorBufferInfoStorage;
        std::vector<VkWriteDescriptorSet> internal_pDescriptorWrites = transformDescriptorSetList(
            pDescriptorWrites, descriptorWriteCount, descriptorBufferInfoStorage);
        auto resources = gfxstream::vk::ResourceTracker::get();
        resources->on_vkUpdateDescriptorSets(
            vkEnc, gfxstream_device->internal_object, descriptorWriteCount,
            internal_pDescriptorWrites.data(), descriptorCopyCount, pDescriptorCopies);
    }
}
