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
import xml.etree.cElementTree as et

from mako.template import Template

from v3dv_extensions import *

platform_defines = []

def _init_exts_from_xml(xml):
    """ Walk the Vulkan XML and fill out extra extension information. """

    xml = et.parse(xml)

    ext_name_map = {}
    for ext in EXTENSIONS:
        ext_name_map[ext.name] = ext

    # KHR_display is missing from the list.
    platform_defines.append('VK_USE_PLATFORM_DISPLAY_KHR')
    for platform in xml.findall('./platforms/platform'):
        platform_defines.append(platform.attrib['protect'])

    for ext_elem in xml.findall('.extensions/extension'):
        ext_name = ext_elem.attrib['name']
        if ext_name not in ext_name_map:
            continue

        ext = ext_name_map[ext_name]
        ext.type = ext_elem.attrib['type']

_TEMPLATE_H = Template(COPYRIGHT + """

#ifndef V3DV_EXTENSIONS_H
#define V3DV_EXTENSIONS_H

#include "stdbool.h"

#define V3DV_INSTANCE_EXTENSION_COUNT ${len(instance_extensions)}

extern const VkExtensionProperties v3dv_instance_extensions[];

struct v3dv_instance_extension_table {
   union {
      bool extensions[V3DV_INSTANCE_EXTENSION_COUNT];
      struct {
%for ext in instance_extensions:
         bool ${ext.name[3:]};
%endfor
      };
   };
};

extern const struct v3dv_instance_extension_table v3dv_instance_extensions_supported;


#define V3DV_DEVICE_EXTENSION_COUNT ${len(device_extensions)}

extern const VkExtensionProperties v3dv_device_extensions[];

struct v3dv_device_extension_table {
   union {
      bool extensions[V3DV_DEVICE_EXTENSION_COUNT];
      struct {
%for ext in device_extensions:
        bool ${ext.name[3:]};
%endfor
      };
   };
};

struct v3dv_physical_device;

void
v3dv_physical_device_get_supported_extensions(const struct v3dv_physical_device *device,
                                             struct v3dv_device_extension_table *extensions);

#endif /* V3DV_EXTENSIONS_H */
""")

_TEMPLATE_C = Template(COPYRIGHT + """
#include "v3dv_private.h"

#include "vk_util.h"

/* Convert the VK_USE_PLATFORM_* defines to booleans */
%for platform_define in platform_defines:
#ifdef ${platform_define}
#   undef ${platform_define}
#   define ${platform_define} true
#else
#   define ${platform_define} false
#endif
%endfor

/* And ANDROID too */
#ifdef ANDROID
#   undef ANDROID
#   define ANDROID true
#else
#   define ANDROID false
#endif

#define V3DV_HAS_SURFACE (VK_USE_PLATFORM_WAYLAND_KHR || \\
                         VK_USE_PLATFORM_XCB_KHR || \\
                         VK_USE_PLATFORM_XLIB_KHR || \\
                         VK_USE_PLATFORM_DISPLAY_KHR)

static const uint32_t MAX_API_VERSION = ${MAX_API_VERSION.c_vk_version()};

const VkExtensionProperties v3dv_instance_extensions[V3DV_INSTANCE_EXTENSION_COUNT] = {
%for ext in instance_extensions:
   {"${ext.name}", ${ext.ext_version}},
%endfor
};

const struct v3dv_instance_extension_table v3dv_instance_extensions_supported = {
%for ext in instance_extensions:
   .${ext.name[3:]} = ${ext.enable},
%endfor
};

uint32_t
v3dv_physical_device_api_version(struct v3dv_physical_device *device)
{
    uint32_t version = 0;

    uint32_t override = vk_get_version_override();
    if (override)
        return MIN2(override, MAX_API_VERSION);

%for version in API_VERSIONS:
    if (!(${version.enable}))
        return version;
    version = ${version.version.c_vk_version()};

%endfor
    return version;
}

const VkExtensionProperties v3dv_device_extensions[V3DV_DEVICE_EXTENSION_COUNT] = {
%for ext in device_extensions:
   {"${ext.name}", ${ext.ext_version}},
%endfor
};

void
v3dv_physical_device_get_supported_extensions(const struct v3dv_physical_device *device,
                                             struct v3dv_device_extension_table *extensions)
{
   *extensions = (struct v3dv_device_extension_table) {
%for ext in device_extensions:
      .${ext.name[3:]} = ${ext.enable},
%endfor
   };
}
""")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', help='Output C file.')
    parser.add_argument('--out-h', help='Output H file.')
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
        'API_VERSIONS': API_VERSIONS,
        'MAX_API_VERSION': MAX_API_VERSION,
        'instance_extensions': [e for e in EXTENSIONS if e.type == 'instance'],
        'device_extensions': [e for e in EXTENSIONS if e.type == 'device'],
        'platform_defines': platform_defines,
    }

    if args.out_h:
        with open(args.out_h, 'w') as f:
            f.write(_TEMPLATE_H.render(**template_env))

    if args.out_c:
        with open(args.out_c, 'w') as f:
            f.write(_TEMPLATE_C.render(**template_env))
