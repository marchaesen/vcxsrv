COPYRIGHT=u"""
/* Copyright 2024 Valve Corporation
 * Copyright 2021 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
"""

import argparse
from vk_physical_device_features_gen import get_renamed_feature, str_removeprefix
import os
import sys
import xml.etree.ElementTree as et

import mako
from mako.template import Template

TEMPLATE_C = Template(COPYRIGHT + """
/* This file generated from ${filename}, don't edit directly. */

#include "vk_physical_device.h"
#include "vk_instance.h"
#include "vk_shader.h"

/* for spirv_supported_capabilities */
#include "compiler/spirv/spirv_info.h"

struct spirv_capabilities
vk_physical_device_get_spirv_capabilities(const struct vk_physical_device *pdev)
{
   const struct vk_features *f = &pdev->supported_features;
   const struct vk_device_extension_table *e = &pdev->supported_extensions;
   const struct vk_properties *p = &pdev->properties;
   uint32_t api_version = pdev->instance->app_info.api_version;

   struct spirv_capabilities caps = { false, };

   /* We |= for everything because some caps have multiple names but the
    * same enum value and they sometimes have different enables in the
    * Vulkan spec.  To handle this, we just | all the enables together.
    */
% for cap in caps:
    caps.${cap} |= ${' | '.join(caps[cap])};
% endfor

   return caps;
}
""")

# These don't exist in the SPIR-V headers for one reason or another.
NON_EXISTANT_CAPS = [
    # This isn't a cap, it's an execution mode.
    #
    # https://gitlab.khronos.org/vulkan/vulkan/-/merge_requests/6618
    'MaximallyReconvergesKHR',

    # This extension got published but never got merged to SPIRV-Headers
    #
    # https://gitlab.khronos.org/spirv/spirv-extensions/-/merge_requests/238
    'ClusterCullingShadingHUAWEI',

    # Exclude the one beta cap.
    'ShaderEnqueueAMDX',
]

def process_enable(enab):
    attrib = enab.attrib

    if 'property' in attrib:
        if attrib['value'] == 'VK_TRUE':
            return f"p->{attrib['member']}"
        else:
            return f"(p->{attrib['member']} & {attrib['value']})"
    elif 'extension' in attrib:
        return f"e->{str_removeprefix(attrib['extension'], 'VK_')}"
    elif 'feature' in attrib:
        feat = get_renamed_feature(attrib['struct'], attrib['feature'])
        return f"f->{feat}"
    else:
        version = attrib['version']
        return f"(api_version >= VK_API_{str_removeprefix(version, 'VK_')})"

def get_capabilities(doc, beta):
    caps = {}

    for cap in doc.findall('./spirvcapabilities/spirvcapability'):
        name = cap.attrib['name']
        if name in NON_EXISTANT_CAPS:
            continue

        enables = cap.findall('enable')
        lst = caps.setdefault(name, [])
        lst += [process_enable(x) for x in enables]

    # Remove duplicates
    for cap in caps:
        caps[cap] = list(dict.fromkeys(caps[cap]))

    return caps


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', required=True, help='Output C file.')
    parser.add_argument('--beta', required=True, help='Enable beta extensions.')
    parser.add_argument('--xml', required=True, help='Vulkan API XML file.')
    args = parser.parse_args()

    environment = {
        'filename': os.path.basename(__file__),
        'caps': get_capabilities(et.parse(args.xml), args.beta),
    }

    try:
        with open(args.out_c, 'w', encoding='utf-8') as f:
            f.write(TEMPLATE_C.render(**environment))
    except Exception:
        # In the event there's an error, this uses some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        print(mako.exceptions.text_error_template().render(), file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
