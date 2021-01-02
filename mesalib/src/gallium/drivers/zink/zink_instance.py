from mako.template import Template
from os import path
from zink_extensions import Extension,Layer
import sys

EXTENSIONS = [
    Extension("VK_EXT_debug_utils"),
    Extension("VK_KHR_maintenance2"),
    Extension("VK_KHR_get_physical_device_properties2"),
    Extension("VK_KHR_draw_indirect_count"),
    Extension("VK_KHR_external_memory_capabilities"),
    Extension("VK_MVK_moltenvk"),
]

LAYERS = [
    # if we have debug_util, allow a validation layer to be added.
    Layer("VK_LAYER_KHRONOS_validation",
      conditions=["have_EXT_debug_utils"]),
    Layer("VK_LAYER_LUNARG_standard_validation",
      conditions=["have_EXT_debug_utils", "!have_layer_KHRONOS_validation"]),
]

REPLACEMENTS = {
    "VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES2_EXTENSION_NAME" : "VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME"
}

header_code = """
#ifndef ZINK_INSTANCE_H
#define ZINK_INSTANCE_H

#include "os/os_process.h"

#include <vulkan/vulkan.h>

#if defined(__APPLE__)
// Source of MVK_VERSION
// Source of VK_EXTX_PORTABILITY_SUBSET_EXTENSION_NAME
#include "MoltenVK/vk_mvk_moltenvk.h"
#endif

struct zink_screen;

struct zink_instance_info {
%for ext in extensions:
   bool have_${ext.name_with_vendor()};
%endfor

%for layer in layers:
   bool have_layer_${layer.pure_name()};
%endfor
};

VkInstance
zink_create_instance(struct zink_screen *screen);

#endif
"""

impl_code = """
#include "zink_instance.h"
#include "zink_screen.h"

VkInstance
zink_create_instance(struct zink_screen *screen)
{
   /* reserve one slot for MoltenVK */
   const char *layers[${len(extensions) + 1}] = { 0 };
   uint32_t num_layers = 0;
   
   const char *extensions[${len(layers) + 1}] = { 0 };
   uint32_t num_extensions = 0;

%for ext in extensions:
   bool have_${ext.name_with_vendor()} = false;
%endfor

%for layer in layers:
   bool have_layer_${layer.pure_name()} = false;
%endfor

#if defined(MVK_VERSION)
   bool have_moltenvk_layer = false;
#endif

   // Build up the extensions from the reported ones but only for the unnamed layer
   uint32_t extension_count = 0;
   if (vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL) == VK_SUCCESS) {
       VkExtensionProperties *extension_props = malloc(extension_count * sizeof(VkExtensionProperties));
       if (extension_props) {
           if (vkEnumerateInstanceExtensionProperties(NULL, &extension_count, extension_props) == VK_SUCCESS) {
              for (uint32_t i = 0; i < extension_count; i++) {
%for ext in extensions:
                 if (!strcmp(extension_props[i].extensionName, ${ext.extension_name_literal()})) {
                    have_${ext.name_with_vendor()} = true;
                    extensions[num_extensions++] = ${ext.extension_name_literal()};
                 }
%endfor
              }
           }
       free(extension_props);
       }
   }

   // Clear have_EXT_debug_utils if we do not want debug info
   if (!(zink_debug & ZINK_DEBUG_VALIDATION)) {
      have_EXT_debug_utils = false;
   }

    // Build up the layers from the reported ones
    uint32_t layer_count = 0;

    if (vkEnumerateInstanceLayerProperties(&layer_count, NULL) == VK_SUCCESS) {
        VkLayerProperties *layer_props = malloc(layer_count * sizeof(VkLayerProperties));
        if (layer_props) {
            if (vkEnumerateInstanceLayerProperties(&layer_count, layer_props) == VK_SUCCESS) {
               for (uint32_t i = 0; i < layer_count; i++) {
%for layer in layers:
                  if (!strcmp(layer_props[i].layerName, ${layer.extension_name_literal()})) {
                     have_layer_${layer.pure_name()} = true;
                  }
%endfor
#if defined(MVK_VERSION)
                  if (!strcmp(layer_props[i].layerName, "MoltenVK")) {
                     have_moltenvk_layer = true;
                     layers[num_layers++] = "MoltenVK";
                  }
#endif
               }
            }
        free(layer_props);
        }
    }

%for ext in extensions:
   screen->instance_info.have_${ext.name_with_vendor()} = have_${ext.name_with_vendor()};
%endfor

%for layer in layers:
<%
    conditions = ""
    if layer.enable_conds:
        for cond in layer.enable_conds:
            conditions += "&& (" + cond + ") "
    conditions = conditions.strip()
%>\
   if (have_layer_${layer.pure_name()} ${conditions}) {
      layers[num_layers++] = ${layer.extension_name_literal()};
      screen->instance_info.have_layer_${layer.pure_name()} = true;
   }
%endfor

   VkApplicationInfo ai = {};
   ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;

   char proc_name[128];
   if (os_get_process_name(proc_name, ARRAY_SIZE(proc_name)))
      ai.pApplicationName = proc_name;
   else
      ai.pApplicationName = "unknown";

   ai.pEngineName = "mesa zink";
   ai.apiVersion = screen->loader_version;

   VkInstanceCreateInfo ici = {};
   ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
   ici.pApplicationInfo = &ai;
   ici.ppEnabledExtensionNames = extensions;
   ici.enabledExtensionCount = num_extensions;
   ici.ppEnabledLayerNames = layers;
   ici.enabledLayerCount = num_layers;

   VkInstance instance = VK_NULL_HANDLE;
   VkResult err = vkCreateInstance(&ici, NULL, &instance);
   if (err != VK_SUCCESS)
      return VK_NULL_HANDLE;

   return instance;
}
"""


def replace_code(code: str, replacement: dict):
    for (k, v) in replacement.items():
        code = code.replace(k, v)
    
    return code


if __name__ == "__main__":
    try:
        header_path = sys.argv[1]
        impl_path = sys.argv[2]

        header_path = path.abspath(header_path)
        impl_path = path.abspath(impl_path)
    except:
        print("usage: %s <path to .h> <path to .c>" % sys.argv[0])
        exit(1)

    extensions = EXTENSIONS
    layers = LAYERS
    replacement = REPLACEMENTS

    with open(header_path, "w") as header_file:
        header = Template(header_code).render(extensions=extensions, layers=layers).strip()
        header = replace_code(header, replacement)
        print(header, file=header_file)

    with open(impl_path, "w") as impl_file:
        impl = Template(impl_code).render(extensions=extensions, layers=layers).strip()
        impl = replace_code(impl, replacement)
        print(impl, file=impl_file)
