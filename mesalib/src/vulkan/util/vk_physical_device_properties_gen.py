COPYRIGHT=u"""
/* Copyright © 2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
"""

import argparse
from collections import OrderedDict
from dataclasses import dataclass
import os
import sys
import typing
import xml.etree.ElementTree as et
import re

import mako
from mako.template import Template

from vk_extensions import get_all_required, filter_api

def str_removeprefix(s, prefix):
    if s.startswith(prefix):
        return s[len(prefix):]
    return s

# Some extensions have been promoted to core, their properties are renamed
# in the following hashtable.
# The hashtable takes the form:
# (VkPhysicalDevice{PropertyStruct}, PropertyName): RenamedPropertyName
# Drivers just have to fill the RenamedPropertyName field in their struct
# vk_properties, the runtime will expose the data with the original/right
# name to consumers.
RENAMED_PROPERTIES = {
    ("DrmPropertiesEXT", "hasPrimary"): "drmHasPrimary",
    ("DrmPropertiesEXT", "primaryMajor"): "drmPrimaryMajor",
    ("DrmPropertiesEXT", "primaryMinor"): "drmPrimaryMinor",
    ("DrmPropertiesEXT", "hasRender"): "drmHasRender",
    ("DrmPropertiesEXT", "renderMajor"): "drmRenderMajor",
    ("DrmPropertiesEXT", "renderMinor"): "drmRenderMinor",
    ("SparseProperties", "residencyStandard2DBlockShape"): "sparseResidencyStandard2DBlockShape",
    ("SparseProperties", "residencyStandard2DMultisampleBlockShape"): "sparseResidencyStandard2DMultisampleBlockShape",
    ("SparseProperties", "residencyStandard3DBlockShape"): "sparseResidencyStandard3DBlockShape",
    ("SparseProperties", "residencyAlignedMipSize"): "sparseResidencyAlignedMipSize",
    ("SparseProperties", "residencyNonResidentStrict"): "sparseResidencyNonResidentStrict",
    ("SubgroupProperties", "supportedStages"): "subgroupSupportedStages",
    ("SubgroupProperties", "supportedOperations"): "subgroupSupportedOperations",
    ("SubgroupProperties", "quadOperationsInAllStages"): "subgroupQuadOperationsInAllStages",
}

OUT_ARRAYS = {
    'pCopySrcLayouts': 'copySrcLayoutCount',
    'pCopyDstLayouts': 'copyDstLayoutCount',
    'pLayeredApis': 'layeredApiCount',
}
OUT_ARRAY_COUNTS = OUT_ARRAYS.values()

SPECIALIZED_PROPERTY_STRUCTS = [
]

# Properties not extending VkPhysicalDeviceProperties2 in the XML,
# but which might still be present (in Android for instance)
ANDROID_PROPERTIES = [
    "VkPhysicalDevicePresentationPropertiesANDROID",
]

@dataclass
class Property:
    decl: str
    name: str
    actual_name: str
    length: str
    is_android: bool

    def __init__(self, p, property_struct_name, is_android=False):
        self.decl = ""
        for element in p:
            if element.tag != "comment":
                self.decl += "".join(element.itertext())
            if element.tail:
                self.decl += re.sub(" +", " ", element.tail)

        self.name = p.find("./name").text
        self.actual_name = RENAMED_PROPERTIES.get((property_struct_name, self.name), self.name)

        length = p.attrib.get("len", "1")
        self.length = RENAMED_PROPERTIES.get((property_struct_name, length), length)

        self.decl = self.decl.replace(self.name, self.actual_name)

        self.is_android = is_android

@dataclass
class PropertyStruct:
    c_type: str
    s_type: str
    name: str
    is_android: bool
    properties: typing.List[Property]

ARRAY_COPY_TEMPLATE = Template("""
         if (${dst_ptr} != NULL) {
            uint32_t count = MIN2(${dst_count}, ${src_count});
            for (uint32_t i = 0; i < count; i++)
               ${dst_ptr}[i] = ${src_ptr}[i];
            ${dst_count} = count;
         } else {
            ${dst_count} = ${src_count};
         }
""")

def copy_property(dst_prefix, dst_name, src_prefix, src_name, decl, setter=False):
    if not setter:
       if src_name in OUT_ARRAY_COUNTS:
           assert dst_name in OUT_ARRAY_COUNTS
           # Skip these as we'll fill them out along with the data
           return ""
       elif src_name in OUT_ARRAYS:
           assert dst_name in OUT_ARRAYS

           return ARRAY_COPY_TEMPLATE.render(
               dst_ptr=dst_prefix + dst_name,
               dst_count=dst_prefix + OUT_ARRAYS[dst_name],
               src_ptr=src_prefix + src_name,
               src_count=src_prefix + OUT_ARRAYS[src_name]
           )

    assert "*" not in decl or setter
    dst = dst_prefix + dst_name
    src = src_prefix + src_name

    if "[" in decl:
        return "memcpy(%s, %s, sizeof(%s));" % (dst, src, dst)
    else:
        return "%s = %s;" % (dst, src)

TEMPLATE_H = Template(COPYRIGHT + """
/* This file generated from ${filename}, don"t edit directly. */
#ifndef VK_PROPERTIES_H
#define VK_PROPERTIES_H

#if DETECT_OS_ANDROID
#include "vulkan/vk_android_native_buffer.h"
#endif /* DETECT_OS_ANDROID */

#ifdef __cplusplus
extern "C" {
#endif

struct vk_properties {
% for prop in all_properties:
% if prop.is_android:
#if DETECT_OS_ANDROID
% endif
   ${prop.decl};
% if prop.is_android:
#endif /* DETECT_OS_ANDROID */
% endif
% endfor
};

void
vk_set_physical_device_properties_struct(struct vk_properties *all_properties,
                                         const VkBaseInStructure *pProperties);

#ifdef __cplusplus
}
#endif

#endif
""")

TEMPLATE_C = Template(COPYRIGHT + """
/* This file generated from ${filename}, don"t edit directly. */

#include "vk_common_entrypoints.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_physical_device_properties.h"
#include "vk_util.h"

VKAPI_ATTR void VKAPI_CALL
vk_common_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                       VkPhysicalDeviceProperties2 *pProperties)
{
   VK_FROM_HANDLE(vk_physical_device, pdevice, physicalDevice);

% for prop in pdev_properties:
   ${copy_property("pProperties->properties.", prop.name, "pdevice->properties.", prop.actual_name, prop.decl)}
% endfor

   vk_foreach_struct(ext, pProperties->pNext) {
      switch ((int32_t)ext->sType) {
% for property_struct in property_structs:
% if property_struct.is_android:
#if DETECT_OS_ANDROID
% endif
% if property_struct.name not in SPECIALIZED_PROPERTY_STRUCTS:
      case ${property_struct.s_type}: {
         ${property_struct.c_type} *properties = (void *)ext;
% for prop in property_struct.properties:
         ${copy_property("properties->", prop.name, "pdevice->properties.", prop.actual_name, prop.decl)}
% endfor
         break;
      }
% if property_struct.is_android:
#endif /* DETECT_OS_ANDROID */
% endif
% endif
% endfor

      /* Specialized property handling defined in vk_physical_device_properties_gen.py */

      default:
         break;
      }
   }
}

void
vk_set_physical_device_properties_struct(struct vk_properties *all_properties,
                                         const VkBaseInStructure *pProperties)
{
   switch ((int32_t)pProperties->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2: {
         const VkPhysicalDeviceProperties *properties = &((const VkPhysicalDeviceProperties2 *)pProperties)->properties;
% for prop in pdev_properties:
         ${copy_property("all_properties->", prop.actual_name, "properties->", prop.name, prop.decl, True)}
% endfor
         break;
      }

% for property_struct in property_structs:
% if property_struct.is_android:
#if DETECT_OS_ANDROID
% endif
% if property_struct.name not in SPECIALIZED_PROPERTY_STRUCTS:
      case ${property_struct.s_type}: {
         const ${property_struct.c_type} *properties = (const ${property_struct.c_type} *)pProperties;
% for prop in property_struct.properties:
         ${copy_property("all_properties->", prop.actual_name, "properties->", prop.name, prop.decl, True)}
% endfor
         break;
      }
% if property_struct.is_android:
#endif /* DETECT_OS_ANDROID */
% endif
% endif
% endfor

      /* Don't assume anything with this struct type, and just copy things over */

      default:
         break;
      }
}

""")

def get_pdev_properties(doc, struct_name):
    _type = doc.find(".types/type[@name=\"VkPhysicalDevice%s\"]" % struct_name)
    if _type is not None:
        properties = []
        for p in _type.findall("./member"):
            properties.append(Property(p, struct_name))
        return properties
    return None

def filter_api(elem, api):
    if "api" not in elem.attrib:
        return True

    return api in elem.attrib["api"].split(",")

def get_property_structs(doc, api, beta):
    property_structs = OrderedDict()

    required = get_all_required(doc, "type", api, beta)

    # parse all struct types where structextends VkPhysicalDeviceProperties2
    for _type in doc.findall("./types/type[@category=\"struct\"]"):
        full_name = _type.attrib.get("name")

        if _type.attrib.get("structextends") != "VkPhysicalDeviceProperties2":
            if full_name not in ANDROID_PROPERTIES:
                continue

        if full_name not in required:
            continue

        guard = required[full_name].guard
        is_android = full_name in ANDROID_PROPERTIES

        if (guard is not None
            # Skip beta extensions if not enabled
            and (guard != "VK_ENABLE_BETA_EXTENSIONS" or beta != "true")
            # Include android properties if included in ANDROID_PROPERTIES
            and not is_android):
            continue

        # find Vulkan structure type
        for elem in _type:
            if "STRUCTURE_TYPE" in str(elem.attrib):
                s_type = elem.attrib.get("values")

        name = str_removeprefix(full_name, "VkPhysicalDevice")

        # collect a list of properties
        properties = []

        for p in _type.findall("./member"):
            if not filter_api(p, api):
                continue

            m_name = p.find("./name").text
            if m_name == "pNext":
                pass
            elif m_name == "sType":
                s_type = p.attrib.get("values")
            else:
                properties.append(Property(p, name, is_android))

        property_struct = PropertyStruct(c_type=full_name, s_type=s_type,
            name=name, properties=properties, is_android=is_android)
        property_structs[property_struct.c_type] = property_struct

    return property_structs.values()

def get_property_structs_from_xml(xml_files, beta, api="vulkan"):
    diagnostics = []

    pdev_properties = None
    property_structs = []

    for filename in xml_files:
        doc = et.parse(filename)
        property_structs += get_property_structs(doc, api, beta)
        if not pdev_properties:
            pdev_properties = get_pdev_properties(doc, "Properties")
            pdev_properties = [prop for prop in pdev_properties if prop.name != "limits" and prop.name != "sparseProperties"]

            limits = get_pdev_properties(doc, "Limits")
            for limit in limits:
                limit.name = "limits." + limit.name
            pdev_properties += limits

            sparse_properties = get_pdev_properties(doc, "SparseProperties")
            for prop in sparse_properties:
                prop.name = "sparseProperties." + prop.name
            pdev_properties += sparse_properties

    # Gather all properties, make sure that aliased declarations match up.
    property_names = OrderedDict()
    all_properties = []
    for prop in pdev_properties:
        property_names[prop.actual_name] = prop
        all_properties.append(prop)

    for property_struct in property_structs:
        for prop in property_struct.properties:
            if prop.actual_name not in property_names:
                property_names[prop.actual_name] = prop
                all_properties.append(prop)
            elif prop.decl != property_names[prop.actual_name].decl:
                diagnostics.append("Declaration mismatch ('%s' vs. '%s')" % (prop.decl, property_names[prop.actual_name].decl))

    return pdev_properties, property_structs, all_properties


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-c", required=True, help="Output C file.")
    parser.add_argument("--out-h", required=True, help="Output H file.")
    parser.add_argument("--beta", required=True, help="Enable beta extensions.")
    parser.add_argument("--xml",
                        help="Vulkan API XML file.",
                        required=True, action="append", dest="xml_files")
    args = parser.parse_args()

    pdev_properties, property_structs, all_properties = get_property_structs_from_xml(args.xml_files, args.beta)

    environment = {
        "filename": os.path.basename(__file__),
        "pdev_properties": pdev_properties,
        "property_structs": property_structs,
        "all_properties": all_properties,
        "copy_property": copy_property,
        "SPECIALIZED_PROPERTY_STRUCTS": SPECIALIZED_PROPERTY_STRUCTS,
    }

    try:
        with open(args.out_c, "w", encoding='utf-8') as f:
            f.write(TEMPLATE_C.render(**environment))
        with open(args.out_h, "w", encoding='utf-8') as f:
            f.write(TEMPLATE_H.render(**environment))
    except Exception:
        # In the event there"s an error, this uses some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        print(mako.exceptions.text_error_template().render(), file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
