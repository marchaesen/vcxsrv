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
import os.path
import re
import sys

VULKAN_UTIL = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../vulkan/util'))
sys.path.append(VULKAN_UTIL)

from vk_extensions import *
from vk_extensions_gen import *

MAX_API_VERSION = '1.2.131'

# On Android, we disable all surface and swapchain extensions. Android's Vulkan
# loader implements VK_KHR_surface and VK_KHR_swapchain, and applications
# cannot access the driver's implementation. Moreoever, if the driver exposes
# the those extension strings, then tests dEQP-VK.api.info.instance.extensions
# and dEQP-VK.api.info.device fail due to the duplicated strings.
EXTENSIONS = [
    Extension('VK_KHR_bind_memory2',                      1, True),
    Extension('VK_KHR_create_renderpass2',                1, True),
    Extension('VK_KHR_dedicated_allocation',              1, True),
    Extension('VK_KHR_get_display_properties2',           1, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_KHR_get_memory_requirements2',          1, True),
    Extension('VK_KHR_get_physical_device_properties2',   1, True),
    Extension('VK_KHR_get_surface_capabilities2',         1, 'TU_HAS_SURFACE'),
    Extension('VK_KHR_maintenance1',                      1, True),
    Extension('VK_KHR_maintenance2',                      1, True),
    Extension('VK_KHR_maintenance3',                      1, True),
    Extension('VK_KHR_sampler_mirror_clamp_to_edge',      1, True),
    Extension('VK_KHR_sampler_ycbcr_conversion',          1, True),
    Extension('VK_KHR_surface',                          25, 'TU_HAS_SURFACE'),
    Extension('VK_KHR_swapchain',                        68, 'TU_HAS_SURFACE'),
    Extension('VK_KHR_wayland_surface',                   6, 'VK_USE_PLATFORM_WAYLAND_KHR'),
    Extension('VK_KHR_xcb_surface',                       6, 'VK_USE_PLATFORM_XCB_KHR'),
    Extension('VK_KHR_xlib_surface',                      6, 'VK_USE_PLATFORM_XLIB_KHR'),
    Extension('VK_KHR_display',                          23, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_EXT_direct_mode_display',               1, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_EXT_acquire_xlib_display',              1, 'VK_USE_PLATFORM_XLIB_XRANDR_EXT'),
    Extension('VK_EXT_display_surface_counter',           1, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_EXT_display_control',                   1, 'VK_USE_PLATFORM_DISPLAY_KHR'),
    Extension('VK_EXT_debug_report',                      9, True),
    Extension('VK_KHR_external_memory_capabilities',      1, True),
    Extension('VK_KHR_external_memory',                   1, True),
    Extension('VK_KHR_external_memory_fd',                1, True),
    Extension('VK_EXT_external_memory_dma_buf',           1, True),
    Extension('VK_EXT_image_drm_format_modifier',         1, True),
    Extension('VK_EXT_sample_locations',                  1, 'device->gpu_id == 650'),
    Extension('VK_EXT_sampler_filter_minmax',             1, True),
    Extension('VK_EXT_transform_feedback',                1, True),
    Extension('VK_ANDROID_native_buffer',                 1, 'ANDROID'),
    Extension('VK_KHR_external_fence',                    1, True),
    Extension('VK_KHR_external_fence_fd',                 1, True),
    Extension('VK_KHR_external_semaphore',                1, True),
    Extension('VK_KHR_external_semaphore_capabilities',   1, True),
    Extension('VK_KHR_external_semaphore_fd',             1, True),
    Extension('VK_IMG_filter_cubic',                      1, 'device->gpu_id == 650'),
    Extension('VK_EXT_filter_cubic',                      1, 'device->gpu_id == 650'),
    Extension('VK_EXT_index_type_uint8',                  1, True),
    Extension('VK_EXT_vertex_attribute_divisor',          1, True),
    Extension('VK_KHR_shader_draw_parameters',            1, True),
    Extension('VK_KHR_variable_pointers',                 1, True),
    Extension('VK_EXT_private_data',                      1, True),
    Extension('VK_EXT_shader_stencil_export',             1, True),
    Extension('VK_EXT_depth_clip_enable',                 1, True),
    Extension('VK_KHR_draw_indirect_count',               1, True),
    Extension('VK_EXT_4444_formats',                      1, True),
    Extension('VK_EXT_conditional_rendering',             1, True),
    Extension('VK_EXT_custom_border_color',              12, True),
    Extension('VK_KHR_multiview',                         1, True),
    Extension('VK_EXT_host_query_reset',                  1, True),
    Extension('VK_EXT_shader_viewport_index_layer',       1, True),
    Extension('VK_EXT_extended_dynamic_state',            1, True),
    Extension('VK_KHR_push_descriptor',                   1, True),
    Extension('VK_KHR_incremental_present',               1, 'TU_HAS_SURFACE'),
    Extension('VK_KHR_image_format_list',                 1, True),
]

MAX_API_VERSION = VkVersion(MAX_API_VERSION)
API_VERSIONS = [ ApiVersion(MAX_API_VERSION,  True) ]

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

    gen_extensions('tu', args.xml_files, API_VERSIONS, MAX_API_VERSION, EXTENSIONS, args.out_c, args.out_h)
