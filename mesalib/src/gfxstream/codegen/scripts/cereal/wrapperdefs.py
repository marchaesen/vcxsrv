# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from .common.codegen import VulkanWrapperGenerator
from .common.vulkantypes import makeVulkanTypeSimple

# Contains definitions for various Vulkan API wrappers. This information is
# shared to make it easier for one kind of wrapper to know how to call
# another one.

API_PREFIX_MARSHAL = "marshal_"
API_PREFIX_UNMARSHAL = "unmarshal_"
API_PREFIX_RESERVEDMARSHAL = "reservedmarshal_"
API_PREFIX_RESERVEDUNMARSHAL = "reservedunmarshal_"

MARSHAL_INPUT_VAR_NAME = "forMarshaling"
UNMARSHAL_INPUT_VAR_NAME = "forUnmarshaling"

API_PREFIX_VALIDATE = "validate_"
API_PREFIX_FRONTEND = "goldfish_frontend_"

VULKAN_STREAM_TYPE = "VulkanStream"
VULKAN_STREAM_TYPE_GUEST = "VulkanStreamGuest"
VULKAN_STREAM_VAR_NAME = "vkStream"

VALIDATE_RESULT_TYPE = "VkResult"
VALIDATE_VAR_NAME = "validateResult"
VALIDATE_GOOD_RESULT = "VK_SUCCESS"

ROOT_TYPE_VAR_NAME = "rootType"
ROOT_TYPE_DEFAULT_VALUE = "VK_STRUCTURE_TYPE_MAX_ENUM"
ROOT_TYPE_TYPE = "VkStructureType"
ROOT_TYPE_PARAM = makeVulkanTypeSimple(
    False, ROOT_TYPE_TYPE, 0, ROOT_TYPE_VAR_NAME)

PARAMETERS_MARSHALING = [
    makeVulkanTypeSimple(False, VULKAN_STREAM_TYPE, 1, VULKAN_STREAM_VAR_NAME),
    ROOT_TYPE_PARAM,
]
PARAMETERS_MARSHALING_GUEST = [
    makeVulkanTypeSimple(False, VULKAN_STREAM_TYPE_GUEST,
                         1, VULKAN_STREAM_VAR_NAME),
    ROOT_TYPE_PARAM,
]
PARAMETERS_VALIDATE = [
    makeVulkanTypeSimple(False, VALIDATE_RESULT_TYPE, 1, VALIDATE_VAR_NAME)
]
PARAMETERS_COUNTING = [
    makeVulkanTypeSimple(False, "size_t", 1, VULKAN_STREAM_VAR_NAME)
]

STRUCT_EXTENSION_PARAM = \
    makeVulkanTypeSimple(True, "void", 1, "structExtension")

STRUCT_EXTENSION_PARAM2 = \
    makeVulkanTypeSimple(True, "void", 1, "structExtension2")

STRUCT_EXTENSION_PARAM_FOR_WRITE = \
    makeVulkanTypeSimple(False, "void", 1, "structExtension_out")

STRUCT_TYPE_API_NAME = "goldfish_vk_struct_type"
EXTENSION_SIZE_API_NAME = "goldfish_vk_extension_struct_size"
EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME = "goldfish_vk_extension_struct_size_with_stream_features"

VOID_TYPE = makeVulkanTypeSimple(False, "void", 0)
STREAM_RET_TYPE = makeVulkanTypeSimple(False, "void", 0)

API_PREFIX_EQUALITY = "checkEqual_"
EQUALITY_VAR_NAMES = ["a", "b"]
EQUALITY_ON_FAIL_VAR = "onFail"
EQUALITY_ON_FAIL_VAR_TYPE = makeVulkanTypeSimple(False, "OnFailCompareFunc", 0,
                                                 EQUALITY_ON_FAIL_VAR)
EQUALITY_RET_TYPE = makeVulkanTypeSimple(False, "void", 0)

RELAXED_APIS = [
    "vkWaitForFences",
    "vkWaitSemaphores",
    "vkWaitSemaphoresKHR",
    "vkQueueWaitIdle",
    "vkDeviceWaitIdle",
    "vkQueueFlushCommandsGOOGLE",
]

STYPE_OVERRIDE = {
    "VkPhysicalDeviceFragmentDensityMapFeaturesEXT": "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT",
    "VkPhysicalDeviceFragmentDensityMapPropertiesEXT": "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT",
    "VkRenderPassFragmentDensityMapCreateInfoEXT": "VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT",
    "VkImportColorBufferGOOGLE": "VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE",
    "VkImportBufferGOOGLE": "VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE",
    "VkCreateBlobGOOGLE": "VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE",
}

MAX_PACKET_LENGTH = "(400 * 1024 * 1024) // 400MB"



