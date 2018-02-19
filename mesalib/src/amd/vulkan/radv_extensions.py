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

MAX_API_VERSION = '1.0.57'

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
    Extension('VK_KHR_get_surface_capabilities2',         1, True),
    Extension('VK_KHR_image_format_list',                 1, True),
    Extension('VK_KHR_incremental_present',               1, True),
    Extension('VK_KHR_maintenance1',                      1, True),
    Extension('VK_KHR_maintenance2',                      1, True),
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
    Extension('VK_KHX_multiview',                         1, '!ANDROID'),
    Extension('VK_EXT_debug_report',                      9, True),
    Extension('VK_EXT_discard_rectangles',                1, True),
    Extension('VK_EXT_external_memory_dma_buf',           1, True),
    Extension('VK_EXT_external_memory_host',              1, 'device->rad_info.has_userptr'),
    Extension('VK_EXT_global_priority',                   1, 'device->rad_info.has_ctx_priority'),
    Extension('VK_AMD_draw_indirect_count',               1, True),
    Extension('VK_AMD_rasterization_order',               1, 'device->rad_info.chip_class >= VI && device->rad_info.max_se >= 2'),
    Extension('VK_AMD_shader_info',                       1, True),
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
        ver_list = [str(self.major), str(self.minor), str(self.patch)]
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

        # Workaround for VK_ANDROID_native_buffer. Its <extension> element in
        # vk.xml lists it as supported="disabled" and provides only a stub
        # definition.  Its <extension> element in Mesa's custom
        # vk_android_native_buffer.xml, though, lists it as
        # supported='android-vendor' and fully defines the extension. We want
        # to skip the <extension> element in vk.xml.
        if ext_elem.attrib['supported'] == 'disabled':
            assert ext_name == 'VK_ANDROID_native_buffer'
            continue

        ext = ext_name_map[ext_name]
        ext.type = ext_elem.attrib['type']

_TEMPLATE = Template(COPYRIGHT + """
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

bool
radv_instance_extension_supported(const char *name)
{
%for ext in instance_extensions:
    if (strcmp(name, "${ext.name}") == 0)
        return ${ext.enable};
%endfor
    return false;
}

VkResult radv_EnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
    VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

%for ext in instance_extensions:
    if (${ext.enable}) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "${ext.name}",
                .specVersion = ${ext.ext_version},
            };
        }
    }
%endfor

    return vk_outarray_status(&out);
}

uint32_t
radv_physical_device_api_version(struct radv_physical_device *dev)
{
    return ${MAX_API_VERSION.c_vk_version()};
}

bool
radv_physical_device_extension_supported(struct radv_physical_device *device,
                                        const char *name)
{
%for ext in device_extensions:
    if (strcmp(name, "${ext.name}") == 0)
        return ${ext.enable};
%endfor
    return false;
}

VkResult radv_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
    RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
    VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);
    (void)device;

%for ext in device_extensions:
    if (${ext.enable}) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "${ext.name}",
                .specVersion = ${ext.ext_version},
            };
        }
    }
%endfor

    return vk_outarray_status(&out);
}
""")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', help='Output C file.', required=True)
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

    with open(args.out, 'w') as f:
        f.write(_TEMPLATE.render(**template_env))
