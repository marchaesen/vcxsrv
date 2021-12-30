# Copyright © 2020 Hoe Hao Cheng
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
# 
# Authors:
#    Hoe Hao Cheng <haochengho12907@gmail.com>
# 

from mako.template import Template
from mako.lookup import TemplateLookup
from os import path
from zink_extensions import Extension,ExtensionRegistry,Version
import sys

# constructor: 
#     Extension(name, alias="", required=False, properties=False, features=False, conditions=None, guard=False)
# The attributes:
#  - required: the generated code debug_prints "ZINK: {name} required!" and
#              returns NULL if the extension is unavailable.
#
#  - properties: enable the detection of extension properties in a physical
#                device in the generated code using vkGetPhysicalDeviceProperties2(),
#                and store the returned properties struct inside
#                `zink_device_info.{alias}_props`.
#                Example: the properties for `VK_EXT_transform_feedback`, is stored in
#                `VkPhysicalDeviceTransformFeedbackPropertiesEXT tf_props`.
#
#  - features: enable the getting extension features in a
#              device. Similar to `properties`, this stores the features
#              struct inside `zink_device_info.{alias}_feats`.
#
#  - conditions: criteria for enabling an extension. This is an array of strings,
#                where each string is a condition, and all conditions have to be true
#                for `zink_device_info.have_{name}` to be true.
#
#                The code generator will replace "$feats" and "$props" with the
#                respective variables, e.g. "$feats.nullDescriptor" becomes 
#                "info->rb2_feats.nullDescriptor" in the final code for VK_EXT_robustness2.
#
#                When empty or None, the extension is enabled when the extensions
#                given by vkEnumerateDeviceExtensionProperties() include the extension.
#
#  - guard: adds a #if defined(`extension_name`)/#endif guard around the code generated for this Extension.
EXTENSIONS = [
    Extension("VK_KHR_maintenance1",
        required=True),
    Extension("VK_KHR_maintenance2"),
    Extension("VK_KHR_maintenance3"),
    Extension("VK_KHR_external_memory"),
    Extension("VK_KHR_external_memory_fd"),
    Extension("VK_EXT_external_memory_dma_buf"),
    Extension("VK_EXT_queue_family_foreign"),
    Extension("VK_EXT_provoking_vertex",
       alias="pv",
       features=True,
       properties=True,
       conditions=["$feats.provokingVertexLast"]),
    Extension("VK_EXT_shader_viewport_index_layer"),
    Extension("VK_KHR_get_memory_requirements2"),
    Extension("VK_EXT_post_depth_coverage"),
    Extension("VK_EXT_shader_subgroup_ballot"),
    Extension("VK_KHR_8bit_storage",
              alias="storage_8bit",
              features=True,
              conditions=["$feats.storageBuffer8BitAccess"]),
    Extension("VK_KHR_16bit_storage",
              alias="storage_16bit",
              features=True,
              conditions=["$feats.storageBuffer16BitAccess"]),
    Extension("VK_KHR_driver_properties",
        alias="driver",
        properties=True),
    Extension("VK_EXT_memory_budget"),
    Extension("VK_KHR_draw_indirect_count"),
    Extension("VK_EXT_fragment_shader_interlock",
       alias="interlock",
       features=True,
       conditions=["$feats.fragmentShaderSampleInterlock", "$feats.fragmentShaderPixelInterlock"]),
    Extension("VK_EXT_sample_locations",
       alias="sample_locations",
       properties=True),
    Extension("VK_EXT_conservative_rasterization",
       alias="cons_raster",
       properties=True,
       conditions=["$props.fullyCoveredFragmentShaderInputVariable"]),
    Extension("VK_KHR_shader_draw_parameters"),
    Extension("VK_KHR_sampler_mirror_clamp_to_edge"),
    Extension("VK_EXT_conditional_rendering",
        alias="cond_render", 
        features=True, 
        conditions=["$feats.conditionalRendering"]),
    Extension("VK_EXT_transform_feedback",
        alias="tf",
        properties=True,
        features=True,
        conditions=["$feats.transformFeedback"]),
    Extension("VK_EXT_index_type_uint8",
        alias="index_uint8",
        features=True,
        conditions=["$feats.indexTypeUint8"]),
    Extension("VK_KHR_imageless_framebuffer",
        alias="imgless",
        features=True,
        conditions=["$feats.imagelessFramebuffer"]),
    Extension("VK_EXT_robustness2",
        alias="rb2",
        properties=True,
        features=True,
        conditions=["$feats.nullDescriptor"]),
    Extension("VK_EXT_image_drm_format_modifier"),
    Extension("VK_EXT_vertex_attribute_divisor",
        alias="vdiv", 
        properties=True,
        features=True,
        conditions=["$feats.vertexAttributeInstanceRateDivisor"]),
    Extension("VK_EXT_calibrated_timestamps"),
    Extension("VK_KHR_shader_clock",
       alias="shader_clock",
       features=True,
       conditions=["$feats.shaderSubgroupClock"]),
    Extension("VK_EXT_sampler_filter_minmax",
        alias="reduction",
	properties=True),
    Extension("VK_EXT_custom_border_color",
        alias="border_color",
        properties=True,
        features=True,
        conditions=["$feats.customBorderColors"]),
    Extension("VK_EXT_blend_operation_advanced",
        alias="blend",
        properties=True,
        # TODO: we can probably support non-premul here with some work?
        conditions=["$props.advancedBlendNonPremultipliedSrcColor", "$props.advancedBlendNonPremultipliedDstColor"]),
    Extension("VK_EXT_extended_dynamic_state",
        alias="dynamic_state",
        features=True,
        conditions=["$feats.extendedDynamicState"]),
    Extension("VK_EXT_extended_dynamic_state2",
        alias="dynamic_state2",
        features=True,
        conditions=["$feats.extendedDynamicState2"]),
    Extension("VK_EXT_pipeline_creation_cache_control",
        alias="pipeline_cache_control",
        features=True,
        conditions=["$feats.pipelineCreationCacheControl"]),
    Extension("VK_EXT_shader_stencil_export",
        alias="stencil_export"),
    Extension("VK_EXTX_portability_subset",
        alias="portability_subset_extx",
        nonstandard=True,
        properties=True,
        features=True,
        guard=True),
    Extension("VK_KHR_timeline_semaphore", alias="timeline", features=True),
    Extension("VK_EXT_4444_formats",
        alias="format_4444",
        features=True),
    Extension("VK_EXT_scalar_block_layout",
        alias="scalar_block_layout",
        features=True,
        conditions=["$feats.scalarBlockLayout"]),
    Extension("VK_KHR_swapchain"),
    Extension("VK_KHR_shader_float16_int8",
              alias="shader_float16_int8",
              features=True),
    Extension("VK_EXT_multi_draw",
              alias="multidraw",
	      features=True,
	      properties=True,
	      conditions=["$feats.multiDraw"]),
    Extension("VK_KHR_push_descriptor",
        alias="push",
        properties=True),
    Extension("VK_KHR_descriptor_update_template",
        alias="template"),
    Extension("VK_EXT_line_rasterization",
        alias="line_rast",
        properties=True,
        features=True),
    Extension("VK_EXT_vertex_input_dynamic_state",
        alias="vertex_input",
	features=True,
	conditions=["$feats.vertexInputDynamicState"]),
    Extension("VK_EXT_primitive_topology_list_restart",
        alias="list_restart",
	features=True,
	conditions=["$feats.primitiveTopologyListRestart"]),
    Extension("VK_KHR_dedicated_allocation",
        alias="dedicated"),
    Extension("VK_EXT_descriptor_indexing",
        alias="desc_indexing",
        features=True,
        properties=True,
        conditions=["$feats.descriptorBindingPartiallyBound"]),
]

# constructor: Versions(device_version(major, minor, patch), struct_version(major, minor))
# The attributes:
#  - device_version: Vulkan version, as tuple, to use with 
#                    VK_MAKE_VERSION(version_major, version_minor, version_patch)
#
#  - struct_version: Vulkan version, as tuple, to use with structures and macros
VERSIONS = [
    Version((1,1,0), (1,1)),
    Version((1,2,0), (1,2)),
]

# There exists some inconsistencies regarding the enum constants, fix them.
# This is basically generated_code.replace(key, value).
REPLACEMENTS = {
    "ROBUSTNESS2": "ROBUSTNESS_2",
    "PROPERTIES_PROPERTIES": "PROPERTIES",
    "EXTENDED_DYNAMIC_STATE2": "EXTENDED_DYNAMIC_STATE_2",
}


# This template provides helper functions for the other templates.
# Right now, the following functions are defined:
# - guard(ext) : surrounds the body with an if-def guard according to
#                `ext.extension_name()` if `ext.guard` is True.
include_template = """
<%def name="guard_(ext, body)">
%if ext.guard:
#ifdef ${ext.extension_name()}
%endif
   ${capture(body)|trim}
%if ext.guard:
#endif
%endif
</%def>

## This ugliness is here to prevent mako from adding tons of excessive whitespace
<%def name="guard(ext)">${capture(guard_, ext, body=caller.body).strip('\\r\\n')}</%def>
"""

header_code = """
<%namespace name="helpers" file="helpers"/>

#ifndef ZINK_DEVICE_INFO_H
#define ZINK_DEVICE_INFO_H

#include "util/u_memory.h"

#include <vulkan/vulkan.h>

struct zink_screen;

struct zink_device_info {
   uint32_t device_version;

%for ext in extensions:
<%helpers:guard ext="${ext}">
   bool have_${ext.name_with_vendor()};
</%helpers:guard>
%endfor
%for version in versions:
   bool have_vulkan${version.struct()};
%endfor

   VkPhysicalDeviceFeatures2 feats;
%for version in versions:
   VkPhysicalDeviceVulkan${version.struct()}Features feats${version.struct()};
%endfor

   VkPhysicalDeviceProperties props;
%for version in versions:
   VkPhysicalDeviceVulkan${version.struct()}Properties props${version.struct()};
%endfor

   VkPhysicalDeviceMemoryProperties mem_props;

%for ext in extensions:
<%helpers:guard ext="${ext}">
%if ext.has_features:
   ${ext.physical_device_struct("Features")} ${ext.field("feats")};
%endif
%if ext.has_properties:
   ${ext.physical_device_struct("Properties")} ${ext.field("props")};
%endif
</%helpers:guard>
%endfor

    const char *extensions[${len(extensions)}];
    uint32_t num_extensions;
};

bool
zink_get_physical_device_info(struct zink_screen *screen);

void
zink_verify_device_extensions(struct zink_screen *screen);

/* stub functions that get inserted into the dispatch table if they are not
 * properly loaded.
 */
%for ext in extensions:
%if registry.in_registry(ext.name):
%for cmd in registry.get_registry_entry(ext.name).device_commands:
void zink_stub_${cmd.lstrip("vk")}(void);
%endfor
%endif
%endfor

#endif
"""


impl_code = """
<%namespace name="helpers" file="helpers"/>

#include "zink_device_info.h"
#include "zink_screen.h"

bool
zink_get_physical_device_info(struct zink_screen *screen) 
{
   struct zink_device_info *info = &screen->info;
%for ext in extensions:
<%helpers:guard ext="${ext}">
   bool support_${ext.name_with_vendor()} = false;
</%helpers:guard>
%endfor
   uint32_t num_extensions = 0;

   // get device memory properties
   vkGetPhysicalDeviceMemoryProperties(screen->pdev, &info->mem_props);

   // enumerate device supported extensions
   if (vkEnumerateDeviceExtensionProperties(screen->pdev, NULL, &num_extensions, NULL) == VK_SUCCESS) {
      if (num_extensions > 0) {
         VkExtensionProperties *extensions = MALLOC(sizeof(VkExtensionProperties) * num_extensions);
         if (!extensions) goto fail;
         vkEnumerateDeviceExtensionProperties(screen->pdev, NULL, &num_extensions, extensions);

         for (uint32_t i = 0; i < num_extensions; ++i) {
         %for ext in extensions:
         <%helpers:guard ext="${ext}">
            if (!strcmp(extensions[i].extensionName, "${ext.name}")) {
         %if not (ext.has_features or ext.has_properties):
               info->have_${ext.name_with_vendor()} = true;
         %else:
               support_${ext.name_with_vendor()} = true;
         %endif
            }
         </%helpers:guard>
         %endfor
         }

         FREE(extensions);
      }
   }

   // get device features
   if (screen->vk.GetPhysicalDeviceFeatures2) {
      // check for device extension features
      info->feats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

%for version in versions:
%if version.device_version < (1,2,0):
      if (VK_MAKE_VERSION(1,2,0) <= screen->vk_version) {
         /* VkPhysicalDeviceVulkan11Features was added in 1.2, not 1.1 as one would think */
%else:
      if (${version.version()} <= screen->vk_version) {
%endif
         info->feats${version.struct()}.sType = ${version.stype("FEATURES")};
         info->feats${version.struct()}.pNext = info->feats.pNext;
         info->feats.pNext = &info->feats${version.struct()};
         info->have_vulkan${version.struct()} = true;
      }
%endfor

%for ext in extensions:
%if ext.has_features:
<%helpers:guard ext="${ext}">
      if (support_${ext.name_with_vendor()}) {
         info->${ext.field("feats")}.sType = ${ext.stype("FEATURES")};
         info->${ext.field("feats")}.pNext = info->feats.pNext;
         info->feats.pNext = &info->${ext.field("feats")};
      }
</%helpers:guard>
%endif
%endfor

      screen->vk.GetPhysicalDeviceFeatures2(screen->pdev, &info->feats);
   } else {
      vkGetPhysicalDeviceFeatures(screen->pdev, &info->feats.features);
   }

   // check for device properties
   if (screen->vk.GetPhysicalDeviceProperties2) {
      VkPhysicalDeviceProperties2 props = {0};
      props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

%for version in versions:
%if version.device_version < (1,2,0):
      if (VK_MAKE_VERSION(1,2,0) <= screen->vk_version) {
         /* VkPhysicalDeviceVulkan11Properties was added in 1.2, not 1.1 as one would think */
%else:
      if (${version.version()} <= screen->vk_version) {
%endif
         info->props${version.struct()}.sType = ${version.stype("PROPERTIES")};
         info->props${version.struct()}.pNext = props.pNext;
         props.pNext = &info->props${version.struct()};
      }
%endfor

%for ext in extensions:
%if ext.has_properties:
<%helpers:guard ext="${ext}">
      if (support_${ext.name_with_vendor()}) {
         info->${ext.field("props")}.sType = ${ext.stype("PROPERTIES")};
         info->${ext.field("props")}.pNext = props.pNext;
         props.pNext = &info->${ext.field("props")};
      }
</%helpers:guard>
%endif
%endfor

      // note: setting up local VkPhysicalDeviceProperties2.
      screen->vk.GetPhysicalDeviceProperties2(screen->pdev, &props);
   }

   // enable the extensions if they match the conditions given by ext.enable_conds 
   if (screen->vk.GetPhysicalDeviceProperties2) {
        %for ext in extensions:
<%helpers:guard ext="${ext}">
<%
    conditions = ""
    if ext.enable_conds:
        for cond in ext.enable_conds:
            cond = cond.replace("$feats", "info->" + ext.field("feats"))
            cond = cond.replace("$props", "info->" + ext.field("props"))
            conditions += "&& (" + cond + ")\\n"
    conditions = conditions.strip()
%>\
      info->have_${ext.name_with_vendor()} |= support_${ext.name_with_vendor()}
         ${conditions};
</%helpers:guard>
        %endfor
   }

   // generate extension list
   num_extensions = 0;

%for ext in extensions:
<%helpers:guard ext="${ext}">
   if (info->have_${ext.name_with_vendor()}) {
       info->extensions[num_extensions++] = "${ext.name}";
%if ext.is_required:
   } else {
       debug_printf("ZINK: ${ext.name} required!\\n");
       goto fail;
%endif
   }
</%helpers:guard>
%endfor

   info->num_extensions = num_extensions;

   return true;

fail:
   return false;
}

void
zink_verify_device_extensions(struct zink_screen *screen)
{
%for ext in extensions:
%if registry.in_registry(ext.name):
   if (screen->info.have_${ext.name_with_vendor()}) {
%for cmd in registry.get_registry_entry(ext.name).device_commands:
      if (!screen->vk.${cmd.lstrip("vk")}) {
#ifndef NDEBUG
         screen->vk.${cmd.lstrip("vk")} = (PFN_${cmd})zink_stub_${cmd.lstrip("vk")};
#else
         screen->vk.${cmd.lstrip("vk")} = (PFN_${cmd})zink_stub_function_not_loaded;
#endif
      }
%endfor
   }
%endif
%endfor
}

#ifndef NDEBUG
/* generated stub functions */
## remember the stub functions that are already generated
<% generated_funcs = set() %>

%for ext in extensions:
%if registry.in_registry(ext.name):
%for cmd in registry.get_registry_entry(ext.name).device_commands:
##
## some functions are added by multiple extensions, which creates duplication
## and thus redefinition of stubs (eg. vkCmdPushDescriptorSetWithTemplateKHR)
##
%if cmd in generated_funcs:
   <% continue %>
%else:
   <% generated_funcs.add(cmd) %>
%endif
void
zink_stub_${cmd.lstrip("vk")}()
{
   mesa_loge("ZINK: ${cmd} is not loaded properly!");
   abort();
}
%endfor
%endif
%endfor
#endif
"""


def replace_code(code: str, replacement: dict):
    for (k, v) in replacement.items():
        code = code.replace(k, v)
    
    return code


if __name__ == "__main__":
    try:
        header_path = sys.argv[1]
        impl_path = sys.argv[2]
        vkxml_path = sys.argv[3]

        header_path = path.abspath(header_path)
        impl_path = path.abspath(impl_path)
        vkxml_path = path.abspath(vkxml_path)
    except:
        print("usage: %s <path to .h> <path to .c> <path to vk.xml>" % sys.argv[0])
        exit(1)

    registry = ExtensionRegistry(vkxml_path)

    extensions = EXTENSIONS
    versions = VERSIONS
    replacement = REPLACEMENTS

    # Perform extension validation and set core_since for the extension if available
    error_count = 0
    for ext in extensions:
        if not registry.in_registry(ext.name):
            # disable validation for nonstandard extensions
            if ext.is_nonstandard:
                continue

            error_count += 1
            print("The extension {} is not registered in vk.xml - a typo?".format(ext.name))
            continue

        entry = registry.get_registry_entry(ext.name)

        if entry.ext_type != "device":
            error_count += 1
            print("The extension {} is {} extension - expected a device extension.".format(ext.name, entry.ext_type))
            continue

        if ext.has_features:
            if not (entry.features_struct and ext.physical_device_struct("Features") == entry.features_struct):
                error_count += 1
                print("The extension {} does not provide a features struct.".format(ext.name))

        if ext.has_properties:
            if not (entry.properties_struct and ext.physical_device_struct("Properties") == entry.properties_struct):
                error_count += 1
                print("The extension {} does not provide a properties struct.".format(ext.name))
                print(entry.properties_struct, ext.physical_device_struct("Properties"))

        if entry.promoted_in:
            ext.core_since = Version((*entry.promoted_in, 0))

    if error_count > 0:
        print("zink_device_info.py: Found {} error(s) in total. Quitting.".format(error_count))
        exit(1)

    lookup = TemplateLookup()
    lookup.put_string("helpers", include_template)

    with open(header_path, "w") as header_file:
        header = Template(header_code, lookup=lookup).render(extensions=extensions, versions=versions, registry=registry).strip()
        header = replace_code(header, replacement)
        print(header, file=header_file)

    with open(impl_path, "w") as impl_file:
        impl = Template(impl_code, lookup=lookup).render(extensions=extensions, versions=versions, registry=registry).strip()
        impl = replace_code(impl, replacement)
        print(impl, file=impl_file)
