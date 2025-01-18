# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

KNOWN_FUNCTION_OPCODES = {
    "vkCreateInstance": 20000,
    "vkDestroyInstance": 20001,
    "vkEnumeratePhysicalDevices": 20002,
    "vkGetPhysicalDeviceFeatures": 20003,
    "vkGetPhysicalDeviceFormatProperties": 20004,
    "vkGetPhysicalDeviceImageFormatProperties": 20005,
    "vkGetPhysicalDeviceProperties": 20006,
    "vkGetPhysicalDeviceQueueFamilyProperties": 20007,
    "vkGetPhysicalDeviceMemoryProperties": 20008,
    "vkGetInstanceProcAddr": 20009,
    "vkGetDeviceProcAddr": 20010,
    "vkCreateDevice": 20011,
    "vkDestroyDevice": 20012,
    "vkEnumerateInstanceExtensionProperties": 20013,
    "vkEnumerateDeviceExtensionProperties": 20014,
    "vkEnumerateInstanceLayerProperties": 20015,
    "vkEnumerateDeviceLayerProperties": 20016,
    "vkGetDeviceQueue": 20017,
    "vkQueueSubmit": 20018,
    "vkQueueWaitIdle": 20019,
    "vkDeviceWaitIdle": 20020,
    "vkAllocateMemory": 20021,
    "vkFreeMemory": 20022,
    "vkMapMemory": 20023,
    "vkUnmapMemory": 20024,
    "vkFlushMappedMemoryRanges": 20025,
    "vkInvalidateMappedMemoryRanges": 20026,
    "vkGetDeviceMemoryCommitment": 20027,
    "vkBindBufferMemory": 20028,
    "vkBindImageMemory": 20029,
    "vkGetBufferMemoryRequirements": 20030,
    "vkGetImageMemoryRequirements": 20031,
    "vkGetImageSparseMemoryRequirements": 20032,
    "vkGetPhysicalDeviceSparseImageFormatProperties": 20033,
    "vkQueueBindSparse": 20034,
    "vkCreateFence": 20035,
    "vkDestroyFence": 20036,
    "vkResetFences": 20037,
    "vkGetFenceStatus": 20038,
    "vkWaitForFences": 20039,
    "vkCreateSemaphore": 20040,
    "vkDestroySemaphore": 20041,
    "vkCreateEvent": 20042,
    "vkDestroyEvent": 20043,
    "vkGetEventStatus": 20044,
    "vkSetEvent": 20045,
    "vkResetEvent": 20046,
    "vkCreateQueryPool": 20047,
    "vkDestroyQueryPool": 20048,
    "vkGetQueryPoolResults": 20049,
    "vkCreateBuffer": 20050,
    "vkDestroyBuffer": 20051,
    "vkCreateBufferView": 20052,
    "vkDestroyBufferView": 20053,
    "vkCreateImage": 20054,
    "vkDestroyImage": 20055,
    "vkGetImageSubresourceLayout": 20056,
    "vkCreateImageView": 20057,
    "vkDestroyImageView": 20058,
    "vkCreateShaderModule": 20059,
    "vkDestroyShaderModule": 20060,
    "vkCreatePipelineCache": 20061,
    "vkDestroyPipelineCache": 20062,
    "vkGetPipelineCacheData": 20063,
    "vkMergePipelineCaches": 20064,
    "vkCreateGraphicsPipelines": 20065,
    "vkCreateComputePipelines": 20066,
    "vkDestroyPipeline": 20067,
    "vkCreatePipelineLayout": 20068,
    "vkDestroyPipelineLayout": 20069,
    "vkCreateSampler": 20070,
    "vkDestroySampler": 20071,
    "vkCreateDescriptorSetLayout": 20072,
    "vkDestroyDescriptorSetLayout": 20073,
    "vkCreateDescriptorPool": 20074,
    "vkDestroyDescriptorPool": 20075,
    "vkResetDescriptorPool": 20076,
    "vkAllocateDescriptorSets": 20077,
    "vkFreeDescriptorSets": 20078,
    "vkUpdateDescriptorSets": 20079,
    "vkCreateFramebuffer": 20080,
    "vkDestroyFramebuffer": 20081,
    "vkCreateRenderPass": 20082,
    "vkDestroyRenderPass": 20083,
    "vkGetRenderAreaGranularity": 20084,
    "vkCreateCommandPool": 20085,
    "vkDestroyCommandPool": 20086,
    "vkResetCommandPool": 20087,
    "vkAllocateCommandBuffers": 20088,
    "vkFreeCommandBuffers": 20089,
    "vkBeginCommandBuffer": 20090,
    "vkEndCommandBuffer": 20091,
    "vkResetCommandBuffer": 20092,
    "vkCmdBindPipeline": 20093,
    "vkCmdSetViewport": 20094,
    "vkCmdSetScissor": 20095,
    "vkCmdSetLineWidth": 20096,
    "vkCmdSetDepthBias": 20097,
    "vkCmdSetBlendConstants": 20098,
    "vkCmdSetDepthBounds": 20099,
    "vkCmdSetStencilCompareMask": 20100,
    "vkCmdSetStencilWriteMask": 20101,
    "vkCmdSetStencilReference": 20102,
    "vkCmdBindDescriptorSets": 20103,
    "vkCmdBindIndexBuffer": 20104,
    "vkCmdBindVertexBuffers": 20105,
    "vkCmdDraw": 20106,
    "vkCmdDrawIndexed": 20107,
    "vkCmdDrawIndirect": 20108,
    "vkCmdDrawIndexedIndirect": 20109,
    "vkCmdDispatch": 20110,
    "vkCmdDispatchIndirect": 20111,
    "vkCmdCopyBuffer": 20112,
    "vkCmdCopyImage": 20113,
    "vkCmdBlitImage": 20114,
    "vkCmdCopyBufferToImage": 20115,
    "vkCmdCopyImageToBuffer": 20116,
    "vkCmdUpdateBuffer": 20117,
    "vkCmdFillBuffer": 20118,
    "vkCmdClearColorImage": 20119,
    "vkCmdClearDepthStencilImage": 20120,
    "vkCmdClearAttachments": 20121,
    "vkCmdResolveImage": 20122,
    "vkCmdSetEvent": 20123,
    "vkCmdResetEvent": 20124,
    "vkCmdWaitEvents": 20125,
    "vkCmdPipelineBarrier": 20126,
    "vkCmdBeginQuery": 20127,
    "vkCmdEndQuery": 20128,
    "vkCmdResetQueryPool": 20129,
    "vkCmdWriteTimestamp": 20130,
    "vkCmdCopyQueryPoolResults": 20131,
    "vkCmdPushConstants": 20132,
    "vkCmdBeginRenderPass": 20133,
    "vkCmdNextSubpass": 20134,
    "vkCmdEndRenderPass": 20135,
    "vkCmdExecuteCommands": 20136,
    "vkEnumerateInstanceVersion": 20137,
    "vkBindBufferMemory2": 20138,
    "vkBindImageMemory2": 20139,
    "vkGetDeviceGroupPeerMemoryFeatures": 20140,
    "vkCmdSetDeviceMask": 20141,
    "vkCmdDispatchBase": 20142,
    "vkEnumeratePhysicalDeviceGroups": 20143,
    "vkGetImageMemoryRequirements2": 20144,
    "vkGetBufferMemoryRequirements2": 20145,
    "vkGetImageSparseMemoryRequirements2": 20146,
    "vkGetPhysicalDeviceFeatures2": 20147,
    "vkGetPhysicalDeviceProperties2": 20148,
    "vkGetPhysicalDeviceFormatProperties2": 20149,
    "vkGetPhysicalDeviceImageFormatProperties2": 20150,
    "vkGetPhysicalDeviceQueueFamilyProperties2": 20151,
    "vkGetPhysicalDeviceMemoryProperties2": 20152,
    "vkGetPhysicalDeviceSparseImageFormatProperties2": 20153,
    "vkTrimCommandPool": 20154,
    "vkGetDeviceQueue2": 20155,
    "vkCreateSamplerYcbcrConversion": 20156,
    "vkDestroySamplerYcbcrConversion": 20157,
    "vkCreateDescriptorUpdateTemplate": 20158,
    "vkDestroyDescriptorUpdateTemplate": 20159,
    "vkUpdateDescriptorSetWithTemplate": 20160,
    "vkGetPhysicalDeviceExternalBufferProperties": 20161,
    "vkGetPhysicalDeviceExternalFenceProperties": 20162,
    "vkGetPhysicalDeviceExternalSemaphoreProperties": 20163,
    "vkGetDescriptorSetLayoutSupport": 20164,
    "vkDestroySurfaceKHR": 20165,
    "vkGetPhysicalDeviceSurfaceSupportKHR": 20166,
    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR": 20167,
    "vkGetPhysicalDeviceSurfaceFormatsKHR": 20168,
    "vkGetPhysicalDeviceSurfacePresentModesKHR": 20169,
    "vkCreateSwapchainKHR": 20170,
    "vkDestroySwapchainKHR": 20171,
    "vkGetSwapchainImagesKHR": 20172,
    "vkAcquireNextImageKHR": 20173,
    "vkQueuePresentKHR": 20174,
    "vkGetDeviceGroupPresentCapabilitiesKHR": 20175,
    "vkGetDeviceGroupSurfacePresentModesKHR": 20176,
    "vkGetPhysicalDevicePresentRectanglesKHR": 20177,
    "vkAcquireNextImage2KHR": 20178,
    "vkGetPhysicalDeviceDisplayPropertiesKHR": 20179,
    "vkGetPhysicalDeviceDisplayPlanePropertiesKHR": 20180,
    "vkGetDisplayPlaneSupportedDisplaysKHR": 20181,
    "vkGetDisplayModePropertiesKHR": 20182,
    "vkCreateDisplayModeKHR": 20183,
    "vkGetDisplayPlaneCapabilitiesKHR": 20184,
    "vkCreateDisplayPlaneSurfaceKHR": 20185,
    "vkCreateSharedSwapchainsKHR": 20186,
    "vkCreateXlibSurfaceKHR": 20187,
    "vkGetPhysicalDeviceXlibPresentationSupportKHR": 20188,
    "vkCreateXcbSurfaceKHR": 20189,
    "vkGetPhysicalDeviceXcbPresentationSupportKHR": 20190,
    "vkCreateWaylandSurfaceKHR": 20191,
    "vkGetPhysicalDeviceWaylandPresentationSupportKHR": 20192,
    "vkCreateMirSurfaceKHR": 20193,
    "vkGetPhysicalDeviceMirPresentationSupportKHR": 20194,
    "vkCreateAndroidSurfaceKHR": 20195,
    "vkCreateWin32SurfaceKHR": 20196,
    "vkGetPhysicalDeviceWin32PresentationSupportKHR": 20197,
    "vkGetPhysicalDeviceFeatures2KHR": 20198,
    "vkGetPhysicalDeviceProperties2KHR": 20199,
    "vkGetPhysicalDeviceFormatProperties2KHR": 20200,
    "vkGetPhysicalDeviceImageFormatProperties2KHR": 20201,
    "vkGetPhysicalDeviceQueueFamilyProperties2KHR": 20202,
    "vkGetPhysicalDeviceMemoryProperties2KHR": 20203,
    "vkGetPhysicalDeviceSparseImageFormatProperties2KHR": 20204,
    "vkGetDeviceGroupPeerMemoryFeaturesKHR": 20205,
    "vkCmdSetDeviceMaskKHR": 20206,
    "vkCmdDispatchBaseKHR": 20207,
    "vkTrimCommandPoolKHR": 20208,
    "vkEnumeratePhysicalDeviceGroupsKHR": 20209,
    "vkGetPhysicalDeviceExternalBufferPropertiesKHR": 20210,
    "vkGetMemoryWin32HandleKHR": 20211,
    "vkGetMemoryWin32HandlePropertiesKHR": 20212,
    "vkGetMemoryFdKHR": 20213,
    "vkGetMemoryFdPropertiesKHR": 20214,
    "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR": 20215,
    "vkImportSemaphoreWin32HandleKHR": 20216,
    "vkGetSemaphoreWin32HandleKHR": 20217,
    "vkImportSemaphoreFdKHR": 20218,
    "vkGetSemaphoreFdKHR": 20219,
    "vkCmdPushDescriptorSetKHR": 20220,
    "vkCmdPushDescriptorSetWithTemplateKHR": 20221,
    "vkCreateDescriptorUpdateTemplateKHR": 20222,
    "vkDestroyDescriptorUpdateTemplateKHR": 20223,
    "vkUpdateDescriptorSetWithTemplateKHR": 20224,
    "vkCreateRenderPass2KHR": 20225,
    "vkCmdBeginRenderPass2KHR": 20226,
    "vkCmdNextSubpass2KHR": 20227,
    "vkCmdEndRenderPass2KHR": 20228,
    "vkGetSwapchainStatusKHR": 20229,
    "vkGetPhysicalDeviceExternalFencePropertiesKHR": 20230,
    "vkImportFenceWin32HandleKHR": 20231,
    "vkGetFenceWin32HandleKHR": 20232,
    "vkImportFenceFdKHR": 20233,
    "vkGetFenceFdKHR": 20234,
    "vkGetPhysicalDeviceSurfaceCapabilities2KHR": 20235,
    "vkGetPhysicalDeviceSurfaceFormats2KHR": 20236,
    "vkGetPhysicalDeviceDisplayProperties2KHR": 20237,
    "vkGetPhysicalDeviceDisplayPlaneProperties2KHR": 20238,
    "vkGetDisplayModeProperties2KHR": 20239,
    "vkGetDisplayPlaneCapabilities2KHR": 20240,
    "vkGetImageMemoryRequirements2KHR": 20241,
    "vkGetBufferMemoryRequirements2KHR": 20242,
    "vkGetImageSparseMemoryRequirements2KHR": 20243,
    "vkCreateSamplerYcbcrConversionKHR": 20244,
    "vkDestroySamplerYcbcrConversionKHR": 20245,
    "vkBindBufferMemory2KHR": 20246,
    "vkBindImageMemory2KHR": 20247,
    "vkGetDescriptorSetLayoutSupportKHR": 20248,
    "vkCmdDrawIndirectCountKHR": 20249,
    "vkCmdDrawIndexedIndirectCountKHR": 20250,
    "vkGetSwapchainGrallocUsageANDROID": 20251,
    "vkAcquireImageANDROID": 20252,
    "vkQueueSignalReleaseImageANDROID": 20253,
    "vkCreateDebugReportCallbackEXT": 20254,
    "vkDestroyDebugReportCallbackEXT": 20255,
    "vkDebugReportMessageEXT": 20256,
    "vkDebugMarkerSetObjectTagEXT": 20257,
    "vkDebugMarkerSetObjectNameEXT": 20258,
    "vkCmdDebugMarkerBeginEXT": 20259,
    "vkCmdDebugMarkerEndEXT": 20260,
    "vkCmdDebugMarkerInsertEXT": 20261,
    "vkCmdDrawIndirectCountAMD": 20262,
    "vkCmdDrawIndexedIndirectCountAMD": 20263,
    "vkGetShaderInfoAMD": 20264,
    "vkGetPhysicalDeviceExternalImageFormatPropertiesNV": 20265,
    "vkGetMemoryWin32HandleNV": 20266,
    "vkCreateViSurfaceNN": 20267,
    "vkCmdBeginConditionalRenderingEXT": 20268,
    "vkCmdEndConditionalRenderingEXT": 20269,
    "vkCmdProcessCommandsNVX": 20270,
    "vkCmdReserveSpaceForCommandsNVX": 20271,
    "vkCreateIndirectCommandsLayoutNVX": 20272,
    "vkDestroyIndirectCommandsLayoutNVX": 20273,
    "vkCreateObjectTableNVX": 20274,
    "vkDestroyObjectTableNVX": 20275,
    "vkRegisterObjectsNVX": 20276,
    "vkUnregisterObjectsNVX": 20277,
    "vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX": 20278,
    "vkCmdSetViewportWScalingNV": 20279,
    "vkReleaseDisplayEXT": 20280,
    "vkAcquireXlibDisplayEXT": 20281,
    "vkGetRandROutputDisplayEXT": 20282,
    "vkGetPhysicalDeviceSurfaceCapabilities2EXT": 20283,
    "vkDisplayPowerControlEXT": 20284,
    "vkRegisterDeviceEventEXT": 20285,
    "vkRegisterDisplayEventEXT": 20286,
    "vkGetSwapchainCounterEXT": 20287,
    "vkGetRefreshCycleDurationGOOGLE": 20288,
    "vkGetPastPresentationTimingGOOGLE": 20289,
    "vkCmdSetDiscardRectangleEXT": 20290,
    "vkSetHdrMetadataEXT": 20291,
    "vkCreateIOSSurfaceMVK": 20292,
    "vkCreateMacOSSurfaceMVK": 20293,
    "vkSetDebugUtilsObjectNameEXT": 20294,
    "vkSetDebugUtilsObjectTagEXT": 20295,
    "vkQueueBeginDebugUtilsLabelEXT": 20296,
    "vkQueueEndDebugUtilsLabelEXT": 20297,
    "vkQueueInsertDebugUtilsLabelEXT": 20298,
    "vkCmdBeginDebugUtilsLabelEXT": 20299,
    "vkCmdEndDebugUtilsLabelEXT": 20300,
    "vkCmdInsertDebugUtilsLabelEXT": 20301,
    "vkCreateDebugUtilsMessengerEXT": 20302,
    "vkDestroyDebugUtilsMessengerEXT": 20303,
    "vkSubmitDebugUtilsMessageEXT": 20304,
    "vkGetAndroidHardwareBufferPropertiesANDROID": 20305,
    "vkGetMemoryAndroidHardwareBufferANDROID": 20306,
    "vkCmdSetSampleLocationsEXT": 20307,
    "vkGetPhysicalDeviceMultisamplePropertiesEXT": 20308,
    "vkCreateValidationCacheEXT": 20309,
    "vkDestroyValidationCacheEXT": 20310,
    "vkMergeValidationCachesEXT": 20311,
    "vkGetValidationCacheDataEXT": 20312,
    "vkGetMemoryHostPointerPropertiesEXT": 20313,
    "vkCmdWriteBufferMarkerAMD": 20314,
    "vkCmdSetCheckpointNV": 20315,
    "vkGetQueueCheckpointDataNV": 20316,
    "vkMapMemoryIntoAddressSpaceGOOGLE": 20317,
    "vkUpdateDescriptorSetWithTemplateSizedGOOGLE": 20320,
    "vkBeginCommandBufferAsyncGOOGLE": 20321,
    "vkEndCommandBufferAsyncGOOGLE": 20322,
    "vkResetCommandBufferAsyncGOOGLE": 20323,
    "vkCommandBufferHostSyncGOOGLE": 20324,
    "vkCreateImageWithRequirementsGOOGLE": 20325,
    "vkCreateBufferWithRequirementsGOOGLE": 20326,
    "vkGetMemoryHostAddressInfoGOOGLE": 20327,
    "vkFreeMemorySyncGOOGLE": 20328,
    "vkQueueHostSyncGOOGLE": 20329,
    "vkQueueSubmitAsyncGOOGLE": 20330,
    "vkQueueWaitIdleAsyncGOOGLE": 20331,
    "vkQueueBindSparseAsyncGOOGLE": 20332,
    "vkGetLinearImageLayoutGOOGLE": 20333,
    "vkQueueFlushCommandsGOOGLE": 20340,
    "vkGetBlobGOOGLE": 20341,
    "vkGetSemaphoreGOOGLE": 20342,
}

CUSTOM_MARSHAL_TYPES = {
    "VkAccelerationStructureInstanceKHR": {
        "common": """
typedef struct VkAccelerationStructureInstanceKHRWithoutBitFields {
    VkTransformMatrixKHR          transform;
    uint32_t                      dwords[2];
    uint64_t                      accelerationStructureReference;
} VkAccelerationStructureInstanceKHRWithoutBitFields;
""",
        "marshaling": """
const VkAccelerationStructureInstanceKHRWithoutBitFields* {newInputVarName} = (const VkAccelerationStructureInstanceKHRWithoutBitFields*)({inputVarName});
marshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transform));
for (uint32_t i = 0; i < 2; i++) {{
    {streamVarName}->write((uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
}}
{streamVarName}->write((uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
""",
        "unmarshaling": """
VkAccelerationStructureInstanceKHRWithoutBitFields* {newInputVarName} = (VkAccelerationStructureInstanceKHRWithoutBitFields*)({inputVarName});
unmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transform));
for (uint32_t i = 0; i < 2; i++) {{
    {streamVarName}->read((uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
}}
{streamVarName}->read((uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
""",
        "reservedmarshaling": """
(void)vkStream;
const VkAccelerationStructureInstanceKHRWithoutBitFields* {newInputVarName} = (const VkAccelerationStructureInstanceKHRWithoutBitFields*)({inputVarName});
reservedmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transform), ptr);
for (uint32_t i = 0; i < 2; i++) {{
    memcpy(*ptr, (uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
}}
memcpy(*ptr, (uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
*ptr += sizeof(uint64_t);
""",
        "reservedunmarshaling": """
VkAccelerationStructureInstanceKHRWithoutBitFields* {newInputVarName} = (VkAccelerationStructureInstanceKHRWithoutBitFields*)({inputVarName});
reservedunmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transform), ptr);
for (uint32_t i = 0; i < 2; i++) {{
    memcpy((uint32_t*)&({newInputVarName}->dwords[i]), *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
}}
memcpy((uint64_t*)&{newInputVarName}->accelerationStructureReference, *ptr, sizeof(uint64_t));
*ptr += sizeof(uint64_t);
""",
    },
    "VkAccelerationStructureMatrixMotionInstanceNV": {
        "common": """
typedef struct VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields {
    VkTransformMatrixKHR          transformT0;
    VkTransformMatrixKHR          transformT1;
    uint32_t                      dwords[2];
    uint64_t                      accelerationStructureReference;
} VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields;
""",
        "marshaling": """
const VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields* {newInputVarName} = (const VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields*)({inputVarName});
marshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transformT0));
marshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transformT1));
for (uint32_t i = 0; i < 2; i++) {{
    {streamVarName}->write((uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
}}
{streamVarName}->write((uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
""",
        "unmarshaling": """
VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields* {newInputVarName} = (VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields*)({inputVarName});
unmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transformT0));
unmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transformT1));
for (uint32_t i = 0; i < 2; i++) {{
    {streamVarName}->read((uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
}}
{streamVarName}->read((uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
""",
        "reservedmarshaling": """
(void)vkStream;
const VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields* {newInputVarName} = (const VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields*)({inputVarName});
reservedmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transformT0), ptr);
reservedmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transformT1), ptr);
for (uint32_t i = 0; i < 2; i++) {{
    memcpy(*ptr, (uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
}}
memcpy(*ptr, (uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
*ptr += sizeof(uint64_t);
""",
        "reservedunmarshaling": """
VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields* {newInputVarName} = (VkAccelerationStructureMatrixMotionInstanceNVWithoutBitFields*)({inputVarName});
reservedunmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transformT0), ptr);
reservedunmarshal_VkTransformMatrixKHR({streamVarName}, {rootTypeVarName}, (VkTransformMatrixKHR*)(&{newInputVarName}->transformT1), ptr);
for (uint32_t i = 0; i < 2; i++) {{
    memcpy((uint32_t*)&({newInputVarName}->dwords[i]), *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
}}
memcpy((uint64_t*)&{newInputVarName}->accelerationStructureReference, *ptr, sizeof(uint64_t));
*ptr += sizeof(uint64_t);
""",
    },
    "VkAccelerationStructureSRTMotionInstanceNV": {
        "common": """
typedef struct VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields {
    VkSRTDataNV          transformT0;
    VkSRTDataNV          transformT1;
    uint32_t             dwords[2];
    uint64_t             accelerationStructureReference;
} VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields;
""",
        "marshaling": """
const VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields* {newInputVarName} = (const VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields*)({inputVarName});
marshal_VkSRTDataNV({streamVarName}, {rootTypeVarName}, (VkSRTDataNV*)(&{newInputVarName}->transformT0));
marshal_VkSRTDataNV({streamVarName}, {rootTypeVarName}, (VkSRTDataNV*)(&{newInputVarName}->transformT1));
for (uint32_t i = 0; i < 2; i++) {{
    {streamVarName}->write((uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
}}
{streamVarName}->write((uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
""",
        "unmarshaling": """
VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields* {newInputVarName} = (VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields*)({inputVarName});
unmarshal_VkSRTDataNV({streamVarName}, {rootTypeVarName}, (VkSRTDataNV*)(&{newInputVarName}->transformT0));
unmarshal_VkSRTDataNV({streamVarName}, {rootTypeVarName}, (VkSRTDataNV*)(&{newInputVarName}->transformT1));
for (uint32_t i = 0; i < 2; i++) {{
    {streamVarName}->read((uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
}}
{streamVarName}->read((uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
""",
        "reservedmarshaling": """
(void)vkStream;
const VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields* {newInputVarName} = (const VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields*)({inputVarName});
reservedmarshal_VkSRTDataNV({streamVarName}, {rootTypeVarName}, (VkSRTDataNV*)(&{newInputVarName}->transformT0), ptr);
reservedmarshal_VkSRTDataNV({streamVarName}, {rootTypeVarName}, (VkSRTDataNV*)(&{newInputVarName}->transformT1), ptr);
for (uint32_t i = 0; i < 2; i++) {{
    memcpy(*ptr, (uint32_t*)&({newInputVarName}->dwords[i]), sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
}}
memcpy(*ptr, (uint64_t*)&{newInputVarName}->accelerationStructureReference, sizeof(uint64_t));
*ptr += sizeof(uint64_t);
""",
        "reservedunmarshaling": """
VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields* {newInputVarName} = (VkAccelerationStructureSRTMotionInstanceNVWithoutBitFields*)({inputVarName});
reservedunmarshal_VkSRTDataNV({streamVarName}, {rootTypeVarName}, (VkSRTDataNV*)(&{newInputVarName}->transformT0), ptr);
reservedunmarshal_VkSRTDataNV({streamVarName}, {rootTypeVarName}, (VkSRTDataNV*)(&{newInputVarName}->transformT1), ptr);
for (uint32_t i = 0; i < 2; i++) {{
    memcpy((uint32_t*)&({newInputVarName}->dwords[i]), *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
}}
memcpy((uint64_t*)&{newInputVarName}->accelerationStructureReference, *ptr, sizeof(uint64_t));
*ptr += sizeof(uint64_t);
""",
    },
    "VkXcbSurfaceCreateInfoKHR": {
        "common": """
// This struct should never be marshaled / unmarshaled.
__builtin_trap();
""",
        "marshaling": "",
        "unmarshaling": "",
        "reservedmarshaling": "",
        "reservedunmarshaling": "",
    },
    "VkMetalSurfaceCreateInfoEXT": {
        "common": """
// This struct should never be marshaled / unmarshaled.
__builtin_trap();
""",
        "marshaling": "",
        "unmarshaling": "",
        "reservedmarshaling": "",
        "reservedunmarshaling": "",
    },
}
