# Copyright 2023 Google LLC
# SPDX-License-Identifier: MIT

import os, re, sys
from generator import *
from pathlib import Path, PurePosixPath

import cereal
from cereal.wrapperdefs import VULKAN_STREAM_TYPE
from cereal.wrapperdefs import VULKAN_STREAM_TYPE_GUEST

# CerealGenerator - generates set of driver sources
# while being agnostic to the stream implementation
from reg import GroupInfo, TypeInfo, EnumInfo

VK_CEREAL_FLAG_HOST = 1
VK_CEREAL_FLAG_GUEST = 2
VK_CEREAL_FLAG_ALL = VK_CEREAL_FLAG_GUEST | VK_CEREAL_FLAG_HOST

SUPPORTED_FEATURES = [
    "VK_VERSION_1_0",
    "VK_VERSION_1_1",
    "VK_VERSION_1_2",
    "VK_VERSION_1_3",
    # Instance extensions
    "VK_KHR_get_physical_device_properties2",
    "VK_KHR_external_semaphore_capabilities",
    "VK_KHR_external_memory_capabilities",
    "VK_KHR_external_fence_capabilities",
    "VK_EXT_debug_utils",
    "VK_EXT_debug_report",
    "VK_EXT_validation_features",
    # Device extensions
    "VK_EXT_external_memory_host",
    "VK_KHR_storage_buffer_storage_class",
    "VK_KHR_vulkan_memory_model",
    "VK_KHR_buffer_device_address",
    "VK_KHR_maintenance1",
    "VK_KHR_maintenance2",
    "VK_KHR_maintenance3",
    "VK_KHR_bind_memory2",
    "VK_KHR_dedicated_allocation",
    "VK_KHR_get_memory_requirements2",
    "VK_KHR_sampler_ycbcr_conversion",
    "VK_KHR_shader_float16_int8",
    "VK_AMD_gpu_shader_half_float",
    "VK_NV_shader_subgroup_partitioned",
    "VK_KHR_shader_subgroup_extended_types",
    "VK_EXT_provoking_vertex",
    "VK_KHR_line_rasterization",
    "VK_EXT_transform_feedback",
    "VK_EXT_primitive_topology_list_restart",
    "VK_EXT_index_type_uint8",
    "VK_EXT_load_store_op_none",
    "VK_EXT_swapchain_colorspace",
    "VK_EXT_custom_border_color",
    "VK_EXT_shader_stencil_export",
    "VK_KHR_image_format_list",
    "VK_KHR_incremental_present",
    "VK_KHR_pipeline_executable_properties",
    "VK_EXT_queue_family_foreign",
    "VK_EXT_scalar_block_layout",
    "VK_KHR_external_semaphore",
    "VK_KHR_external_semaphore_fd",
    "VK_KHR_external_memory",
    "VK_KHR_external_fence",
    "VK_KHR_external_fence_fd",
    "VK_EXT_device_memory_report",
    "VK_KHR_create_renderpass2",
    "VK_KHR_imageless_framebuffer",
    "VK_KHR_descriptor_update_template",
    "VK_EXT_depth_clip_enable",
    "VK_EXT_robustness2",
    # see aosp/2736079 + b/268351352
    "VK_EXT_swapchain_maintenance1",
    "VK_KHR_maintenance5",
    "VK_EXT_host_image_copy",
    "VK_EXT_image_compression_control",
    "VK_EXT_image_compression_control_swapchain",
    "VK_EXT_image_drm_format_modifier",
    # VK1.3 extensions: see b/298704840
    "VK_KHR_copy_commands2",
    "VK_KHR_dynamic_rendering",
    "VK_KHR_format_feature_flags2",
    "VK_KHR_maintenance4",
    "VK_KHR_shader_integer_dot_product",
    "VK_KHR_shader_non_semantic_info",
    "VK_KHR_shader_terminate_invocation",
    "VK_KHR_synchronization2",
    "VK_KHR_zero_initialize_workgroup_memory",
    "VK_EXT_4444_formats",
    "VK_EXT_extended_dynamic_state",
    "VK_EXT_extended_dynamic_state2",
    "VK_EXT_image_robustness",
    "VK_EXT_inline_uniform_block",
    "VK_EXT_pipeline_creation_cache_control",
    "VK_EXT_pipeline_creation_feedback",
    "VK_EXT_private_data",
    "VK_EXT_shader_demote_to_helper_invocation",
    "VK_EXT_subgroup_size_control",
    "VK_EXT_texel_buffer_alignment",
    "VK_EXT_texture_compression_astc_hdr",
    "VK_EXT_tooling_info",
    "VK_EXT_ycbcr_2plane_444_formats",
    # Host dispatch
    "VK_KHR_surface",
    "VK_KHR_swapchain",
    "VK_KHR_xcb_surface",
    "VK_KHR_win32_surface",
    "VK_EXT_metal_surface",
    "VK_EXT_metal_objects",
    "VK_EXT_external_memory_metal",
    "VK_KHR_external_semaphore_win32",
    "VK_KHR_external_memory_win32",
    "VK_NV_device_diagnostic_checkpoints",
    "VK_KHR_ray_tracing_pipeline",
    "VK_KHR_pipeline_library",
    # Android
    "VK_ANDROID_native_buffer",
    "VK_ANDROID_external_memory_android_hardware_buffer",
    "VK_KHR_android_surface",
    # Linux
    "VK_KHR_external_memory_fd",
    # Custom
    "VK_GOOGLE_gfxstream",
    # Used in tests without proper support checks
    "VK_EXT_graphics_pipeline_library",
    # Used by guest ANGLE
    "VK_EXT_vertex_attribute_divisor",
    # QNX
    "VK_QNX_external_memory_screen_buffer",
    # b/320855472 Chrome
    "VK_EXT_fragment_density_map",
    # b/349122558 Zink
    "VK_EXT_color_write_enable",
]

HOST_MODULES = ["goldfish_vk_extension_structs", "goldfish_vk_marshaling",
                "goldfish_vk_reserved_marshaling", "goldfish_vk_deepcopy",
                "goldfish_vk_dispatch", "goldfish_vk_transform", "VkDecoder",
                "VkDecoderSnapshot", "VkSubDecoder"]

# By default, the all wrappers are run all on all features.  In certain cases,
# we wish run wrappers when the module requires it. For example, `VK_GOOGLE_gfxstream`
# shouldn't generate a function table entry since it's an internal interface.
SUPPORTED_MODULES = {
    "VK_EXT_external_memory_host": HOST_MODULES,
    "VK_EXT_debug_utils": HOST_MODULES,
    "VK_EXT_debug_report": HOST_MODULES,
    "VK_EXT_validation_features": HOST_MODULES,
    "VK_KHR_surface": ["goldfish_vk_dispatch"],
    "VK_KHR_xcb_surface": ["goldfish_vk_dispatch"],
    "VK_KHR_win32_surface": ["goldfish_vk_dispatch"],
    "VK_EXT_metal_surface": ["goldfish_vk_dispatch"],
    "VK_EXT_metal_objects": ["goldfish_vk_dispatch"],
    "VK_EXT_external_memory_metal": ["goldfish_vk_dispatch"],
    "VK_KHR_external_semaphore_win32" : ["goldfish_vk_dispatch"],
    "VK_KHR_external_memory_win32" : ["goldfish_vk_dispatch"],
    # Host dispatch for Linux hosts + and entrypoint for guests
    "VK_KHR_external_memory_fd": ["goldfish_vk_dispatch", "func_table"],
    "VK_QNX_external_memory_screen_buffer": ["goldfish_vk_dispatch"],
    "VK_ANDROID_external_memory_android_hardware_buffer": ["func_table"],
    "VK_KHR_android_surface": ["func_table"],
    "VK_EXT_swapchain_maintenance1" : HOST_MODULES,
    "VK_KHR_swapchain" : HOST_MODULES,
    "VK_NV_device_diagnostic_checkpoints": ["goldfish_vk_dispatch"],
    "VK_KHR_ray_tracing_pipeline": HOST_MODULES,
    "VK_KHR_pipeline_library": HOST_MODULES,
}

# These modules will be used when the feature is not supported.
# This is necessary to cover all extensions where needed.
UNSUPPORTED_FEATURE_MODULES = {
    "goldfish_vk_extension_structs",
}


REQUIRED_TYPES = {
    "int",
    "uint16_t",
    "int64_t",
    "double",
    "VkPresentScalingFlagsEXT",
    "VkPresentGravityFlagsEXT",
}

copyrightHeader = """// Copyright (C) 2018 The Android Open Source Project
// Copyright (C) 2018 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
"""

# We put the long generated commands in a separate paragraph, so that the formatter won't mess up
# with other texts.
autogeneratedHeaderTemplate = """
// Autogenerated module %s
//
// %s
//
// Please do not modify directly;
// re-run gfxstream-protocols/scripts/generate-vulkan-sources.sh,
// or directly from Python by defining:
// VULKAN_REGISTRY_XML_DIR : Directory containing vk.xml
// VULKAN_REGISTRY_SCRIPTS_DIR : Directory containing genvk.py
// CEREAL_OUTPUT_DIR: Where to put the generated sources.
//
// python3 $VULKAN_REGISTRY_SCRIPTS_DIR/genvk.py -registry $VULKAN_REGISTRY_XML_DIR/vk.xml cereal -o $CEREAL_OUTPUT_DIR
//
"""
namespaceBegin ="""
namespace gfxstream {
namespace vk {\n
"""

namespaceEnd = """
}  // namespace vk
}  // namespace gfxstream
"""

def banner_command(argv):
    """Return sanitized command-line description.
       |argv| must be a list of command-line parameters, e.g. sys.argv.
       Return a string corresponding to the command, with platform-specific
       paths removed."""

    def makePosixRelative(someArg):
        # Do not use relative for /tmp/ to avoid effects of checkout location
        if os.path.exists(someArg) and someArg != "/tmp/":
            return str(PurePosixPath(Path(os.path.relpath(someArg))))
        return someArg

    return ' '.join(map(makePosixRelative, argv))

def envGetOrDefault(key, default=None):
    if key in os.environ:
        return os.environ[key]
    return default

# ---- methods overriding base class ----
# beginFile(genOpts)
# endFile()
# beginFeature(interface, emit)
# endFeature()
# genType(typeinfo,name)
# genStruct(typeinfo,name)
# genGroup(groupinfo,name)
# genEnum(enuminfo, name)
# genCmd(cmdinfo)
class CerealGenerator(OutputGenerator):

    """Generate serialization code"""
    def __init__(self, errFile = sys.stderr,
                       warnFile = sys.stderr,
                       diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)

        self.typeInfo = cereal.VulkanTypeInfo(self)

        self.modules = {}
        self.protos = {}
        self.moduleList = []
        self.protoList = []

        self.wrappers = []

        self.codegen = cereal.CodeGen()
        self.featureSupported = False
        self.supportedModules = None

        self.guestBaseLibDirPrefix = "aemu/base"
        self.baseLibDirPrefix = "aemu/base"
        self.utilsHeaderDirPrefix = "utils"

        # The cereal variant should be an environmental variable of one of
        # the following:
        #    - "guest"
        #    - "host"
        #    - "both"
        cerealVariant = envGetOrDefault("CEREAL_VARIANT", "both")
        if cerealVariant == "guest":
          self.cerealFlags = VK_CEREAL_FLAG_GUEST
        elif cerealVariant == "host":
          self.cerealFlags = VK_CEREAL_FLAG_HOST
        else:
          self.cerealFlags = VK_CEREAL_FLAG_ALL

        # THe host always needs all possible guest struct definitions, while the guest only needs
        # platform sepcific headers.
        self.hostCommonExtraVulkanHeaders = '#include "vk_android_native_buffer_gfxstream.h"'

        encoderInclude = f"""
#include "goldfish_vk_private_defs.h"
#include <memory>

namespace gfxstream {{
namespace guest {{
class IOStream;
}}  // namespace guest
}}  // namespace gfxstream
"""
        encoderImplInclude = f"""
#include "Resources.h"
#include "ResourceTracker.h"
#include "Validation.h"
#include "%s.h"
#include "gfxstream/guest/IOStream.h"

#include "{self.guestBaseLibDirPrefix}/AlignedBuf.h"
#include "{self.guestBaseLibDirPrefix}/BumpPool.h"

#include "goldfish_vk_marshaling_guest.h"
#include "goldfish_vk_reserved_marshaling_guest.h"
#include "goldfish_vk_deepcopy_guest.h"
#include "goldfish_vk_counting_guest.h"
#include "goldfish_vk_private_defs.h"
#include "goldfish_vk_transform_guest.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

""" % VULKAN_STREAM_TYPE_GUEST

        functableImplInclude = """
#include "VkEncoder.h"
#include "ResourceTracker.h"
#include "gfxstream_vk_entrypoints.h"
#include "gfxstream_vk_private.h"

#include "goldfish_vk_private_defs.h"

#include <cstring>

// Stuff we are not going to use but if included,
// will cause compile errors. These are Android Vulkan
// required extensions, but the approach will be to
// implement them completely on the guest side.
#undef VK_KHR_android_surface
#if defined(LINUX_GUEST_BUILD) || defined(__Fuchsia__)
#undef VK_ANDROID_native_buffer
#endif
"""
        marshalIncludeGuest = """
#include "goldfish_vk_marshaling_guest.h"
#include "goldfish_vk_private_defs.h"
#include "%s.h"

// Stuff we are not going to use but if included,
// will cause compile errors. These are Android Vulkan
// required extensions, but the approach will be to
// implement them completely on the guest side.
#undef VK_KHR_android_surface
#undef VK_ANDROID_external_memory_android_hardware_buffer
""" % VULKAN_STREAM_TYPE_GUEST

        reservedmarshalIncludeGuest = """
#include "goldfish_vk_marshaling_guest.h"
#include "goldfish_vk_private_defs.h"
#include "%s.h"

// Stuff we are not going to use but if included,
// will cause compile errors. These are Android Vulkan
// required extensions, but the approach will be to
// implement them completely on the guest side.
#undef VK_KHR_android_surface
#undef VK_ANDROID_external_memory_android_hardware_buffer
""" % VULKAN_STREAM_TYPE_GUEST

        reservedmarshalImplIncludeGuest = """
#include "Resources.h"
"""

        vulkanStreamIncludeHost = f"""
{self.hostCommonExtraVulkanHeaders}
#include "goldfish_vk_private_defs.h"

#include "%s.h"
#include "{self.baseLibDirPrefix}/files/StreamSerializing.h"
""" % VULKAN_STREAM_TYPE

        poolInclude = f"""
{self.hostCommonExtraVulkanHeaders}
#include "goldfish_vk_private_defs.h"
#include "{self.baseLibDirPrefix}/BumpPool.h"
using android::base::Allocator;
using android::base::BumpPool;
"""
        transformIncludeGuest = """
#include "goldfish_vk_private_defs.h"
"""
        transformInclude = f"""
{self.hostCommonExtraVulkanHeaders}
#include "goldfish_vk_private_defs.h"
#include "goldfish_vk_extension_structs.h"
"""
        transformImplIncludeGuest = """
#include "ResourceTracker.h"
"""
        transformImplInclude = """
#include "VkDecoderGlobalState.h"
"""
        deepcopyInclude = """
#include "vk_util.h"
"""
        poolIncludeGuest = f"""
#include "goldfish_vk_private_defs.h"
#include "{self.guestBaseLibDirPrefix}/BumpPool.h"
using android::base::Allocator;
using android::base::BumpPool;
// Stuff we are not going to use but if included,
// will cause compile errors. These are Android Vulkan
// required extensions, but the approach will be to
// implement them completely on the guest side.
#undef VK_KHR_android_surface
#undef VK_ANDROID_external_memory_android_hardware_buffer
"""
        dispatchHeaderDefs = f"""
{self.hostCommonExtraVulkanHeaders}
#include "goldfish_vk_private_defs.h"
namespace gfxstream {{
namespace vk {{

struct VulkanDispatch;

}} // namespace vk
}} // namespace gfxstream
using DlOpenFunc = void* (void);
using DlSymFunc = void* (void*, const char*);
"""

        extensionStructsInclude = f"""
{self.hostCommonExtraVulkanHeaders}
#include "goldfish_vk_private_defs.h"
#include "host-common/GfxstreamFatalError.h"
#include "vulkan/vk_enum_string_helper.h"
"""

        extensionStructsIncludeGuest = """
#include "vk_platform_compat.h"
#include "goldfish_vk_private_defs.h"
// Stuff we are not going to use but if included,
// will cause compile errors. These are Android Vulkan
// required extensions, but the approach will be to
// implement them completely on the guest side.
#undef VK_KHR_android_surface
#undef VK_ANDROID_external_memory_android_hardware_buffer
"""
        commonCerealImplIncludes = """
#include "goldfish_vk_extension_structs.h"
#include "goldfish_vk_private_defs.h"
#include <string.h>
"""
        commonCerealIncludesGuest = """
#include "vk_platform_compat.h"
"""
        commonCerealImplIncludesGuest = """
#include "goldfish_vk_extension_structs_guest.h"
#include "goldfish_vk_private_defs.h"

#include <cstring>
"""
        countingIncludes = """
#include "vk_platform_compat.h"
#include "goldfish_vk_private_defs.h"
"""

        dispatchImplIncludes = """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
"""

        decoderSnapshotHeaderIncludes = f"""
#include <memory>
#include "{self.utilsHeaderDirPrefix}/GfxApiLogger.h"
#include "{self.baseLibDirPrefix}/HealthMonitor.h"
#include "goldfish_vk_private_defs.h"
"""
        decoderSnapshotImplIncludes = f"""
#include "VulkanHandleMapping.h"
#include "VkDecoderGlobalState.h"
#include "VkReconstruction.h"

#include "{self.baseLibDirPrefix}/synchronization/Lock.h"
"""

        decoderHeaderIncludes = f"""
#include "VkDecoderContext.h"
#include "ProcessResources.h"

#include <memory>

namespace android {{
namespace base {{
class BumpPool;
}} // namespace android
}} // namespace base

"""

        decoderImplIncludes = f"""
#include "common/goldfish_vk_marshaling.h"
#include "common/goldfish_vk_reserved_marshaling.h"
#include "goldfish_vk_private_defs.h"
#include "common/goldfish_vk_transform.h"

#include "{self.baseLibDirPrefix}/BumpPool.h"
#include "{self.baseLibDirPrefix}/system/System.h"
#include "{self.baseLibDirPrefix}/Metrics.h"
#include "render-utils/IOStream.h"
#include "FrameBuffer.h"
#include "gfxstream/host/Tracing.h"
#include "host-common/feature_control.h"
#include "host-common/GfxstreamFatalError.h"
#include "host-common/logging.h"

#include "VkDecoderGlobalState.h"
#include "VkDecoderSnapshot.h"

#include "VulkanDispatch.h"
#include "%s.h"

#include <functional>
#include <optional>
#include <unordered_map>
""" % VULKAN_STREAM_TYPE

        def createVkExtensionStructureTypePreamble(extensionName: str) -> str:
            return f"""
#define {extensionName}_ENUM(type,id) \
    ((type)(1000000000 + (1000 * ({extensionName}_NUMBER - 1)) + (id)))
"""
        self.guest_encoder_tag = "guest_encoder"
        self.host_tag = "host"

        default_guest_abs_encoder_destination = \
            os.path.join(
                os.getcwd(),
                "..", "..",
                "device", "generic", "goldfish-opengl",
                "system", "vulkan_enc")
        self.guest_abs_encoder_destination = \
            envGetOrDefault("GFXSTREAM_GUEST_ENCODER_DIR",
                            default_guest_abs_encoder_destination)

        default_host_abs_decoder_destination = \
            os.path.join(
                os.getcwd(),
                "android", "android-emugl", "host",
                "libs", "libOpenglRender", "vulkan")
        self.host_abs_decoder_destination = \
            envGetOrDefault("GFXSTREAM_HOST_DECODER_DIR",
                            default_host_abs_decoder_destination)
        self.host_script_destination = envGetOrDefault("GFXSTREAM_SCRIPTS_DIR")

        if self.cerealFlags & VK_CEREAL_FLAG_GUEST:
            self.addGuestEncoderModule(
                "VkEncoder",
                extraHeader = encoderInclude,
                extraImpl = encoderImplInclude)

            self.addGuestEncoderModule("goldfish_vk_extension_structs_guest",
                                       extraHeader=extensionStructsIncludeGuest)
            self.addGuestEncoderModule("goldfish_vk_marshaling_guest",
                                       extraHeader=commonCerealIncludesGuest + marshalIncludeGuest,
                                       extraImpl=commonCerealImplIncludesGuest)
            self.addGuestEncoderModule("goldfish_vk_reserved_marshaling_guest",
                                       extraHeader=commonCerealIncludesGuest + reservedmarshalIncludeGuest,
                                       extraImpl=commonCerealImplIncludesGuest + reservedmarshalImplIncludeGuest)
            self.addGuestEncoderModule("goldfish_vk_deepcopy_guest",
                                       extraHeader=commonCerealIncludesGuest + poolIncludeGuest,
                                       extraImpl=commonCerealImplIncludesGuest + deepcopyInclude)
            self.addGuestEncoderModule("goldfish_vk_counting_guest",
                                       extraHeader=countingIncludes,
                                       extraImpl=commonCerealImplIncludesGuest)
            self.addGuestEncoderModule("goldfish_vk_transform_guest",
                                       extraHeader=commonCerealIncludesGuest + transformIncludeGuest,
                                       extraImpl=commonCerealImplIncludesGuest + transformImplIncludeGuest)
            self.addGuestEncoderModule(
                "vulkan_gfxstream_structure_type", headerOnly=True, suppressFeatureGuards=True,
                moduleName="vulkan_gfxstream_structure_type_guest", useNamespace=False,
                suppressVulkanHeaders=True,
                extraHeader=createVkExtensionStructureTypePreamble('VK_GOOGLE_GFXSTREAM'))

            self.addGuestEncoderModule("func_table", extraImpl=functableImplInclude, implOnly = True,
                                    useNamespace = False)

            self.addWrapper(cereal.VulkanEncoder, "VkEncoder")
            self.addWrapper(cereal.VulkanExtensionStructs, "goldfish_vk_extension_structs_guest", variant = "guest")
            self.addWrapper(cereal.VulkanMarshaling, "goldfish_vk_marshaling_guest", variant = "guest")
            self.addWrapper(cereal.VulkanReservedMarshaling, "goldfish_vk_reserved_marshaling_guest", variant = "guest")
            self.addWrapper(cereal.VulkanDeepcopy, "goldfish_vk_deepcopy_guest")
            self.addWrapper(cereal.VulkanCounting, "goldfish_vk_counting_guest")
            self.addWrapper(cereal.VulkanTransform, "goldfish_vk_transform_guest")
            self.addWrapper(cereal.VulkanFuncTable, "func_table")
            self.addWrapper(cereal.VulkanGfxstreamStructureType,
                            "vulkan_gfxstream_structure_type_guest")

        if self.cerealFlags & VK_CEREAL_FLAG_HOST:
            self.addCppModule("common", "goldfish_vk_extension_structs",
                           extraHeader=extensionStructsInclude)
            self.addCppModule("common", "goldfish_vk_marshaling",
                           extraHeader=vulkanStreamIncludeHost,
                           extraImpl=commonCerealImplIncludes)
            self.addCppModule("common", "goldfish_vk_reserved_marshaling",
                           extraHeader=vulkanStreamIncludeHost,
                           extraImpl=commonCerealImplIncludes)
            self.addCppModule("common", "goldfish_vk_deepcopy",
                           extraHeader=poolInclude,
                           extraImpl=commonCerealImplIncludes + deepcopyInclude)
            self.addCppModule("common", "goldfish_vk_dispatch",
                           extraHeader=dispatchHeaderDefs,
                           extraImpl=dispatchImplIncludes)
            self.addCppModule("common", "goldfish_vk_transform",
                           extraHeader=transformInclude,
                           extraImpl=transformImplInclude)
            self.addHostModule("VkDecoder",
                               extraHeader=decoderHeaderIncludes,
                               extraImpl=decoderImplIncludes,
                               useNamespace=False)
            self.addHostModule("VkDecoderSnapshot",
                               extraHeader=decoderSnapshotHeaderIncludes,
                               extraImpl=decoderSnapshotImplIncludes,
                               useNamespace=False)
            self.addHostModule("VkSubDecoder",
                               extraHeader="",
                               extraImpl="",
                               useNamespace=False,
                               implOnly=True)

            self.addModule(cereal.PyScript(self.host_tag, "vulkan_printer", customAbsDir=Path(
                self.host_script_destination) / "print_gfx_logs"), moduleName="ApiLogDecoder")
            self.addHostModule(
                "vulkan_gfxstream_structure_type", headerOnly=True, suppressFeatureGuards=True,
                moduleName="vulkan_gfxstream_structure_type_host", useNamespace=False,
                suppressVulkanHeaders=True,
                extraHeader=createVkExtensionStructureTypePreamble('VK_GOOGLE_GFXSTREAM'))
            self.addHostModule(
                "vk_android_native_buffer_structure_type", headerOnly=True, suppressFeatureGuards=True,
                useNamespace=False, suppressVulkanHeaders=True,
                extraHeader=createVkExtensionStructureTypePreamble('VK_ANDROID_NATIVE_BUFFER'))

            self.addWrapper(cereal.VulkanExtensionStructs, "goldfish_vk_extension_structs", variant = "host")
            self.addWrapper(cereal.VulkanMarshaling, "goldfish_vk_marshaling")
            self.addWrapper(cereal.VulkanReservedMarshaling, "goldfish_vk_reserved_marshaling", variant = "host")
            self.addWrapper(cereal.VulkanDeepcopy, "goldfish_vk_deepcopy")
            self.addWrapper(cereal.VulkanDispatch, "goldfish_vk_dispatch")
            self.addWrapper(cereal.VulkanTransform, "goldfish_vk_transform", resourceTrackerTypeName="VkDecoderGlobalState")
            self.addWrapper(cereal.VulkanDecoder, "VkDecoder")
            self.addWrapper(cereal.VulkanDecoderSnapshot, "VkDecoderSnapshot")
            self.addWrapper(cereal.VulkanSubDecoder, "VkSubDecoder")
            self.addWrapper(cereal.ApiLogDecoder, "ApiLogDecoder")
            self.addWrapper(cereal.VulkanGfxstreamStructureType, "vulkan_gfxstream_structure_type_host")
            self.addWrapper(cereal.VulkanAndroidNativeBufferStructureType,
                            "vk_android_native_buffer_structure_type")

    def addGuestEncoderModule(
            self, basename, extraHeader="", extraImpl="", useNamespace=True, headerOnly=False,
            suppressFeatureGuards=False, moduleName=None, suppressVulkanHeaders=False, implOnly=False):
        if not os.path.exists(self.guest_abs_encoder_destination):
            print("Path [%s] not found (guest encoder path), skipping" % self.guest_abs_encoder_destination)
            return
        self.addCppModule(self.guest_encoder_tag, basename, extraHeader=extraHeader,
                       extraImpl=extraImpl, customAbsDir=self.guest_abs_encoder_destination,
                       useNamespace=useNamespace, implOnly=implOnly, headerOnly=headerOnly,
                       suppressFeatureGuards=suppressFeatureGuards, moduleName=moduleName,
                       suppressVulkanHeaders=suppressVulkanHeaders)

    def addHostModule(
            self, basename, extraHeader="", extraImpl="", useNamespace=True, implOnly=False,
            suppress=False, headerOnly=False, suppressFeatureGuards=False, moduleName=None,
            suppressVulkanHeaders=False):
        if not os.path.exists(self.host_abs_decoder_destination):
            print("Path [%s] not found (host encoder path), skipping" %
                  self.host_abs_decoder_destination)
            return
        if not suppressVulkanHeaders:
            extraHeader = self.hostCommonExtraVulkanHeaders + '\n' + extraHeader
        self.addCppModule(
            self.host_tag, basename, extraHeader=extraHeader, extraImpl=extraImpl,
            customAbsDir=self.host_abs_decoder_destination, useNamespace=useNamespace,
            implOnly=implOnly, suppress=suppress, headerOnly=headerOnly,
            suppressFeatureGuards=suppressFeatureGuards, moduleName=moduleName,
            suppressVulkanHeaders=suppressVulkanHeaders)

    def addModule(self, module, moduleName=None):
        if moduleName is None:
            moduleName = module.basename
        self.moduleList.append(moduleName)
        self.modules[moduleName] = module

    def addCppModule(
            self, directory, basename, extraHeader="", extraImpl="", customAbsDir=None,
            useNamespace=True, implOnly=False, suppress=False, headerOnly=False,
            suppressFeatureGuards=False, moduleName=None, suppressVulkanHeaders=False):
        module = cereal.Module(
            directory, basename, customAbsDir=customAbsDir, suppress=suppress, implOnly=implOnly,
            headerOnly=headerOnly, suppressFeatureGuards=suppressFeatureGuards)
        self.addModule(module, moduleName=moduleName)
        module.headerPreamble = copyrightHeader
        module.headerPreamble += \
                autogeneratedHeaderTemplate % \
                (basename, "(header) generated by %s" % banner_command(sys.argv))

        module.headerPreamble += "#pragma once\n"
        if (not suppressVulkanHeaders):
            module.headerPreamble += "#include <vulkan/vulkan.h>\n"
            module.headerPreamble += '#include "vulkan_gfxstream.h"\n'
            module.headerPreamble += '#include "vk_android_native_buffer_gfxstream.h"\n'
        module.headerPreamble += extraHeader + '\n'
        if useNamespace:
            module.headerPreamble += namespaceBegin

        module.implPreamble = copyrightHeader
        module.implPreamble += \
                autogeneratedHeaderTemplate % \
                (basename, "(impl) generated by %s" % \
                    banner_command(sys.argv))
        if not implOnly:
            module.implPreamble += '\n#include "%s.h"' % \
                (basename)

        module.implPreamble += extraImpl

        if useNamespace:
            module.implPreamble += namespaceBegin
            module.implPostamble += namespaceEnd
            module.headerPostamble += namespaceEnd

    def addWrapper(self, moduleType, moduleName, **kwargs):
        if moduleName not in self.modules:
            print(f'Unknown module: {moduleName}. All known modules are: {", ".join(self.modules)}.')
            return
        self.wrappers.append(
            (moduleType(
                self.modules[moduleName],
                self.typeInfo, **kwargs),
             moduleName)
            )

    def forEachModule(self, func):
        for moduleName in self.moduleList:
            func(self.modules[moduleName])

    def forEachWrapper(self, func, supportedModules):
        for wrapper in self.wrappers:
            if supportedModules is None:
                func(wrapper[0])
            elif wrapper[1] in supportedModules:
                func(wrapper[0])

## Overrides####################################################################

    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)

        self.forEachModule(lambda m: m.begin(self.genOpts.directory))
        self.forEachWrapper(lambda w: w.onBegin(), None)

    def endFile(self):
        OutputGenerator.endFile(self)

        self.typeInfo.onEnd()

        self.forEachWrapper(lambda w: w.onEnd(), None)
        self.forEachModule(lambda m: m.end())

    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)

        for supportedFeature in SUPPORTED_FEATURES:
            if self.featureName == supportedFeature:
                self.featureSupported = True

        if self.featureSupported == False and UNSUPPORTED_FEATURE_MODULES:
            self.featureSupported = True
            self.supportedModules = UNSUPPORTED_FEATURE_MODULES
        elif self.featureSupported == False:
            return
        else:
            self.supportedModules = SUPPORTED_MODULES.get(self.featureName)

        self.typeInfo.onBeginFeature(self.featureName, self.featureType)

        self.forEachModule(
            lambda m: m.appendHeader("#ifdef %s\n" % self.featureName)
            if isinstance(m, cereal.Module) and not m.suppressFeatureGuards else None)
        self.forEachModule(
            lambda m: m.appendImpl("#ifdef %s\n" % self.featureName)
            if isinstance(m, cereal.Module) and not m.suppressFeatureGuards else None)
        self.forEachWrapper(lambda w: w.onBeginFeature(self.featureName, self.featureType), self.supportedModules)
        # functable needs to understand the feature type (device vs instance) of each cmd
        for features in interface.findall('require'):
            for c in features.findall('command'):
                self.forEachWrapper(lambda w: w.onFeatureNewCmd(c.get('name')), self.supportedModules)

    def endFeature(self):
        # Finish processing in superclass
        OutputGenerator.endFeature(self)

        if self.featureSupported == False:
            return

        self.featureSupported = False

        self.typeInfo.onEndFeature()

        self.forEachModule(lambda m: m.appendHeader("#endif\n") if isinstance(
            m, cereal.Module) and not m.suppressFeatureGuards else None)
        self.forEachModule(lambda m: m.appendImpl("#endif\n") if isinstance(
            m, cereal.Module) and not m.suppressFeatureGuards else None)
        self.forEachWrapper(lambda w: w.onEndFeature(), self.supportedModules)

    def genType(self, typeinfo: TypeInfo, name, alias):
        OutputGenerator.genType(self, typeinfo, name, alias)

        # Maybe this check can be removed if we refactor other things inside
        # the cereal subdirectory.
        if self.featureSupported == False and name in REQUIRED_TYPES:
            self.typeInfo.onGenType(typeinfo, name, alias)
            return

        if self.featureSupported == False:
            return

        self.typeInfo.onGenType(typeinfo, name, alias)
        self.forEachWrapper(lambda w: w.onGenType(typeinfo, name, alias), self.supportedModules)

    def genStruct(self, typeinfo, typeName, alias):
        OutputGenerator.genStruct(self, typeinfo, typeName, alias)
        if self.featureSupported == False:
            return

        self.typeInfo.onGenStruct(typeinfo, typeName, alias)
        self.forEachWrapper(lambda w: w.onGenStruct(typeinfo, typeName, alias), self.supportedModules)

    def genGroup(self, groupinfo: GroupInfo, groupName, alias = None):
        OutputGenerator.genGroup(self, groupinfo, groupName, alias)
        if self.featureSupported == False:
            return

        self.typeInfo.onGenGroup(groupinfo, groupName, alias)
        self.forEachWrapper(lambda w: w.onGenGroup(groupinfo, groupName, alias), self.supportedModules)

    def genEnum(self, enuminfo: EnumInfo, name, alias):
        OutputGenerator.genEnum(self, enuminfo, name, alias)
        if self.featureSupported == False:
            return
        self.typeInfo.onGenEnum(enuminfo, name, alias)
        self.forEachWrapper(lambda w: w.onGenEnum(enuminfo, name, alias), self.supportedModules)

    def genCmd(self, cmdinfo, name, alias):
        OutputGenerator.genCmd(self, cmdinfo, name, alias)
        if self.featureSupported == False:
            return

        self.typeInfo.onGenCmd(cmdinfo, name, alias)
        self.forEachWrapper(lambda w: w.onGenCmd(cmdinfo, name, alias), self.supportedModules)
