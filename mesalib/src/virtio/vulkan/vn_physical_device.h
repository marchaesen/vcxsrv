/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_PHYSICAL_DEVICE_H
#define VN_PHYSICAL_DEVICE_H

#include "vn_common.h"

#include "util/sparse_array.h"

#include "vn_wsi.h"

struct vn_physical_device_features {
   VkPhysicalDeviceFeatures vulkan_1_0;
   VkPhysicalDeviceVulkan11Features vulkan_1_1;
   VkPhysicalDeviceVulkan12Features vulkan_1_2;
   VkPhysicalDeviceVulkan13Features vulkan_1_3;

   /* Vulkan 1.3: The extensions for the below structs were promoted, but some
    * struct members were omitted from VkPhysicalDeviceVulkan13Features.
    */
   VkPhysicalDevice4444FormatsFeaturesEXT _4444_formats;
   VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state;
   VkPhysicalDeviceExtendedDynamicState2FeaturesEXT extended_dynamic_state_2;
   VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT texel_buffer_alignment;
   VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT ycbcr_2plane_444_formats;

   /* EXT */
   VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional_rendering;
   VkPhysicalDeviceCustomBorderColorFeaturesEXT custom_border_color;
   VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip_enable;
   VkPhysicalDeviceImageViewMinLodFeaturesEXT image_view_min_lod;
   VkPhysicalDeviceIndexTypeUint8FeaturesEXT index_type_uint8;
   VkPhysicalDeviceLineRasterizationFeaturesEXT line_rasterization;
   VkPhysicalDeviceMultiDrawFeaturesEXT multi_draw;
   VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT
      primitive_topology_list_restart;
   VkPhysicalDeviceProvokingVertexFeaturesEXT provoking_vertex;
   VkPhysicalDeviceRobustness2FeaturesEXT robustness_2;
   VkPhysicalDeviceTransformFeedbackFeaturesEXT transform_feedback;
   VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT vertex_attribute_divisor;

   /* vendor */
   VkPhysicalDeviceMutableDescriptorTypeFeaturesVALVE mutable_descriptor_type;
};

struct vn_physical_device_properties {
   VkPhysicalDeviceProperties vulkan_1_0;
   VkPhysicalDeviceVulkan11Properties vulkan_1_1;
   VkPhysicalDeviceVulkan12Properties vulkan_1_2;
   VkPhysicalDeviceVulkan13Properties vulkan_1_3;

   /* EXT */
   VkPhysicalDeviceConservativeRasterizationPropertiesEXT
      conservative_rasterization;
   VkPhysicalDeviceCustomBorderColorPropertiesEXT custom_border_color;
   VkPhysicalDeviceLineRasterizationPropertiesEXT line_rasterization;
   VkPhysicalDeviceMultiDrawPropertiesEXT multi_draw;
   VkPhysicalDeviceProvokingVertexPropertiesEXT provoking_vertex;
   VkPhysicalDeviceRobustness2PropertiesEXT robustness_2;
   VkPhysicalDeviceTransformFeedbackPropertiesEXT transform_feedback;
   VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT
      vertex_attribute_divisor;
};

struct vn_format_properties_entry {
   atomic_bool valid;
   VkFormatProperties properties;
};

struct vn_physical_device {
   struct vn_physical_device_base base;

   struct vn_instance *instance;

   /* Between the driver and the app, properties.properties.apiVersion is what
    * we advertise and is capped by VN_MAX_API_VERSION and others.
    *
    * Between the driver and the renderer, renderer_version is the device
    * version we can use internally.
    */
   uint32_t renderer_version;

   /* Between the driver and the app, base.base.supported_extensions is what
    * we advertise.
    *
    * Between the driver and the renderer, renderer_extensions is what we can
    * use internally (after enabling).
    */
   struct vk_device_extension_table renderer_extensions;
   uint32_t *extension_spec_versions;

   struct vn_physical_device_features features;
   struct vn_physical_device_properties properties;

   VkQueueFamilyProperties2 *queue_family_properties;
   uint32_t queue_family_count;

   VkPhysicalDeviceMemoryProperties2 memory_properties;

   struct {
      VkExternalMemoryHandleTypeFlagBits renderer_handle_type;
      VkExternalMemoryHandleTypeFlags supported_handle_types;
   } external_memory;

   /* syncFdFencing allows driver to query renderer sync_fd features */
   VkExternalFenceFeatureFlags renderer_sync_fd_fence_features;
   VkExternalSemaphoreFeatureFlags renderer_sync_fd_semaphore_features;

   VkExternalFenceHandleTypeFlags external_fence_handles;
   VkExternalSemaphoreHandleTypeFlags external_binary_semaphore_handles;
   VkExternalSemaphoreHandleTypeFlags external_timeline_semaphore_handles;

   struct wsi_device wsi_device;

   simple_mtx_t format_update_mutex;
   struct util_sparse_array format_properties;
};
VK_DEFINE_HANDLE_CASTS(vn_physical_device,
                       base.base.base,
                       VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)

void
vn_physical_device_fini(struct vn_physical_device *physical_dev);

#endif /* VN_PHYSICAL_DEVICE_H */
