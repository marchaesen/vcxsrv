COPYRIGHT = """\
/*
 * Copyright 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
"""

import argparse
import copy
import re
import xml.etree.cElementTree as et

from mako.template import Template

MAX_API_VERSION = '1.1.70'

class Extension:
    def __init__(self, name, ext_version, enable):
        self.name = name
        self.ext_version = int(ext_version)
        if enable is True:
            self.enable = 'true';
        elif enable is False:
            self.enable = 'false';
        else:
            self.enable = enable;

# On Android, we disable all surface and swapchain extensions. Android's Vulkan
# loader implements VK_KHR_surface and VK_KHR_swapchain, and applications
# cannot access the driver's implementation. Moreoever, if the driver exposes
# the those extension strings, then tests dEQP-VK.api.info.instance.extensions
# and dEQP-VK.api.info.device fail due to the duplicated strings.
EXTENSIONS = [
    Extension('VK_ANDROID_native_buffer',                 5, 'ANDROID && device->rad_info.has_syncobj_wait_for_submit'),
    Extension('VK_KHR_bind_memory2',                      1, True),
    Extension('VK_KHR_dedicated_allocation',              1, True),
    Extension('VK_KHR_descriptor_update_template',        1, True),
    Extension('VK_KHR_device_group',                      1, True),
    Extension('VK_KHR_device_group_creation',             1, True),
    Extension('VK_KHR_draw_indirect_count',               1, True),
    Extension('VK_KHR_external_fence',                    1, 'device->rad_info.has_syncobj_wait_for_submit'),
    Extension('VK_KHR_external_fence_capabilities',       1, True),
    Extension('VK_KHR_external_fence_fd',                 1, 'device->rad_info.has_syncobj_wait_for_submit'),
    Extension('VK_KHR_external_memory',                   1, True),
    Extension('VK_KHR_external_memory_capabilities',      1, True),
    Extension('VK_KHR_external_memory_fd',                1, True),
    Extension('VK_KHR_external_semaphore',                1, 'device->rad_info.has_syncobj'),
    Extension('VK_KHR_external_semaphore_capabilities',   1, True),
    Extension('VK_KHR_external_semaphore_fd',             1, 'device->rad_info.has_syncobj'),
    Extension('VK_KHR_get_memory_requirements2',          1, True),
    Extension('VK_KHR_get_physical_device_properties2',   1, True),
    Extension('VK_KHR_get_surface_capabilities2',         1, 'RADV_HAS_SURFACE'),
    Extension('VK_KHR_image_format_list',                 1, True),
    Extension('VK_KHR_incremental_present',               1, 'RADV_HAS_SURFACE'),
    Extension('VK_KHR_maintenance1',                      1, True),
    Extension('VK_KHR_maintenance2',                      1, True),
    Extension('VK_KHR_maintenance3',                      1, True),
    Extension('VK_KHR_push_descriptor',                   1, True),
    Extension('VK_KHR_relaxed_block_layout',              1, True),
    Extension('VK_KHR_sampler_mirror_clamp_to_edge',      1, True),
    Extension('VK_KHR_shader_draw_parameters',            1, True),
    Extension('VK_KHR_storage_buffer_storage_class',      1, True),
    Extension('VK_KHR_surface',                          25, 'RADV_HAS_SURFACE'),
    Extension('VK_KHR_swapchain',                        68, 'RADV_HAS_SURFACE'),
    Extension('VK_KHR_variable_pointers',                 1, True),
    Extension('VK_KHR_wayland_surface',                   6, 'VK_USE_PLATFORM_WAYLAND_KHR'),
    Extension('VK_KHR_xcb_surface',                       6, 'VK_USE_PLATFORM_XCB_KHR'),
    Extension('VK_KHR_xlib_surface',                      6, 'VK_USE_PLATFORM_XLIB_KHR'),
    Extension('VK_KHR_multiview',                         1, True),
    Extension('VK_EXT_debug_report',                      9, True),
    Extension('VK_EXT_depth_range_unrestricted',          1, True),
    Extension('VK_EXT_descriptor_indexing',               2, True),
    Extension('VK_EXT_discard_rectangles',                1, True),
    Extension('VK_EXT_external_memory_dma_buf',           1, True),
    Extension('VK_EXT_external_memory_host',              1, 'device->rad_info.has_userptr'),
    Extension('VK_EXT_global_priority',                   1, 'device->rad_info.has_ctx_priority'),
    Extension('VK_EXT_sampler_filter_minmax',             1, 'device->rad_info.chip_class >= CIK'),
    Extension('VK_EXT_shader_viewport_index_layer',       1, True),
    Extension('VK_EXT_vertex_attribute_divisor',          1, True),
    Extension('VK_AMD_draw_indirect_count',               1, True),
    Extension('VK_AMD_gcn_shader',                        1, True),
    Extension('VK_AMD_rasterization_order',               1, 'device->has_out_of_order_rast'),
    Extension('VK_AMD_shader_core_properties',            1, True),
    Extension('VK_AMD_shader_info',                       1, True),
    Extension('VK_AMD_shader_trinary_minmax',             1, True),
]

class VkVersion:
    def __init__(self, string):
        split = string.split('.')
        self.major = int(split[0])
        self.minor = int(split[1])
        if len(split) > 2:
            assert len(split) == 3
            self.patch = int(split[2])
        else:
            self.patch = None

        # Sanity check.  The range bits are required by the definition of the
        # VK_MAKE_VERSION macro
        assert self.major < 1024 and self.minor < 1024
        assert self.patch is None or self.patch < 4096
        assert(str(self) == string)

    def __str__(self):
        ver_list = [str(self.major), str(self.minor)]
        if self.patch is not None:
            ver_list.append(str(self.patch))
        return '.'.join(ver_list)

    def c_vk_version(self):
        patch = self.patch if self.patch is not None else 0
        ver_list = [str(self.major), str(self.minor), str(patch)]
        return 'VK_MAKE_VERSION(' + ', '.join(ver_list) + ')'

    def __int_ver(self):
        # This is just an expansion of VK_VERSION
        patch = self.patch if self.patch is not None else 0
        return (self.major << 22) | (self.minor << 12) | patch

    def __cmp__(self, other):
        # If only one of them has a patch version, "ignore" it by making
        # other's patch version match self.
        if (self.patch is None) != (other.patch is None):
            other = copy.copy(other)
            other.patch = self.patch

        return self.__int_ver().__cmp__(other.__int_ver())

MAX_API_VERSION = VkVersion(MAX_API_VERSION)

def _init_exts_from_xml(xml):
    """ Walk the Vulkan XML and fill out extra extension information. """

    xml = et.parse(xml)

    ext_name_map = {}
    for ext in EXTENSIONS:
        ext_name_map[ext.name] = ext

    for ext_elem in xml.findall('.extensions/extension'):
        ext_name = ext_elem.attrib['name']
        if ext_name not in ext_name_map:
            continue

        ext = ext_name_map[ext_name]
        ext.type = ext_elem.attrib['type']

_TEMPLATE_H = Template(COPYRIGHT + """
#ifndef RADV_EXTENSIONS_H
#define RADV_EXTENSIONS_H

enum {
   RADV_INSTANCE_EXTENSION_COUNT = ${len(instance_extensions)},
   RADV_DEVICE_EXTENSION_COUNT = ${len(device_extensions)},
};

struct radv_instance_extension_table {
   union {
      bool extensions[RADV_INSTANCE_EXTENSION_COUNT];
      struct {
%for ext in instance_extensions:
        bool ${ext.name[3:]};
%endfor
      };
   };
};

struct radv_device_extension_table {
   union {
      bool extensions[RADV_DEVICE_EXTENSION_COUNT];
      struct {
%for ext in device_extensions:
        bool ${ext.name[3:]};
%endfor
      };
   };
};

extern const VkExtensionProperties radv_instance_extensions[RADV_INSTANCE_EXTENSION_COUNT];
extern const VkExtensionProperties radv_device_extensions[RADV_DEVICE_EXTENSION_COUNT];
extern const struct radv_instance_extension_table radv_supported_instance_extensions;


struct radv_physical_device;

void radv_fill_device_extension_table(const struct radv_physical_device *device,
                                      struct radv_device_extension_table* table);
#endif
""")

_TEMPLATE_C = Template(COPYRIGHT + """
#include "radv_private.h"

#include "vk_util.h"

/* Convert the VK_USE_PLATFORM_* defines to booleans */
%for platform in ['ANDROID', 'WAYLAND', 'XCB', 'XLIB']:
#ifdef VK_USE_PLATFORM_${platform}_KHR
#   undef VK_USE_PLATFORM_${platform}_KHR
#   define VK_USE_PLATFORM_${platform}_KHR true
#else
#   define VK_USE_PLATFORM_${platform}_KHR false
#endif
%endfor

/* And ANDROID too */
#ifdef ANDROID
#   undef ANDROID
#   define ANDROID true
#else
#   define ANDROID false
#endif

#define RADV_HAS_SURFACE (VK_USE_PLATFORM_WAYLAND_KHR || \\
                         VK_USE_PLATFORM_XCB_KHR || \\
                         VK_USE_PLATFORM_XLIB_KHR)

const VkExtensionProperties radv_instance_extensions[RADV_INSTANCE_EXTENSION_COUNT] = {
%for ext in instance_extensions:
   {"${ext.name}", ${ext.ext_version}},
%endfor
};

const VkExtensionProperties radv_device_extensions[RADV_DEVICE_EXTENSION_COUNT] = {
%for ext in device_extensions:
   {"${ext.name}", ${ext.ext_version}},
%endfor
};

const struct radv_instance_extension_table radv_supported_instance_extensions = {
%for ext in instance_extensions:
   .${ext.name[3:]} = ${ext.enable},
%endfor
};

void radv_fill_device_extension_table(const struct radv_physical_device *device,
                                      struct radv_device_extension_table* table)
{
%for ext in device_extensions:
   table->${ext.name[3:]} = ${ext.enable};
%endfor
}

VkResult radv_EnumerateInstanceVersion(
    uint32_t*                                   pApiVersion)
{
    *pApiVersion = ${MAX_API_VERSION.c_vk_version()};
    return VK_SUCCESS;
}

uint32_t
radv_physical_device_api_version(struct radv_physical_device *dev)
{
    if (!ANDROID && dev->rad_info.has_syncobj_wait_for_submit)
        return VK_MAKE_VERSION(1, 1, 70);
    return VK_MAKE_VERSION(1, 0, 68);
}
""")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', help='Output C file.', required=True)
    parser.add_argument('--out-h', help='Output H file.', required=True)
    parser.add_argument('--xml',
                        help='Vulkan API XML file.',
                        required=True,
                        action='append',
                        dest='xml_files')
    args = parser.parse_args()

    for filename in args.xml_files:
        _init_exts_from_xml(filename)

    for ext in EXTENSIONS:
        assert ext.type == 'instance' or ext.type == 'device'

    template_env = {
        'MAX_API_VERSION': MAX_API_VERSION,
        'instance_extensions': [e for e in EXTENSIONS if e.type == 'instance'],
        'device_extensions': [e for e in EXTENSIONS if e.type == 'device'],
    }

    with open(args.out_c, 'w') as f:
        f.write(_TEMPLATE_C.render(**template_env))
    with open(args.out_h, 'w') as f:
        f.write(_TEMPLATE_H.render(**template_env))
