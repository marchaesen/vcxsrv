# Copyright © 2017 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

vk_api_xml = files('registry/vk.xml')
vulkan_icd_symbols = files('vulkan-icd-symbols.txt')

inc_vulkan_wsi = include_directories('wsi')
inc_vulkan_util = include_directories('util')

vulkan_wsi_args = []
vulkan_wsi_deps = []
vulkan_wsi_list = []

if with_platform_x11
  vulkan_wsi_args += ['-DVK_USE_PLATFORM_XCB_KHR', '-DVK_USE_PLATFORM_XLIB_KHR']
  vulkan_wsi_deps += [
    dep_xcb,
    dep_x11_xcb,
    dep_xcb_dri3,
    dep_xcb_present,
    dep_xcb_sync,
    dep_xshmfence,
  ]
  vulkan_wsi_list += ['xcb', 'x11']
endif
if with_platform_wayland
  vulkan_wsi_args += ['-DVK_USE_PLATFORM_WAYLAND_KHR']
  vulkan_wsi_deps += dep_wayland_client
  vulkan_wsi_list += ['wayland']
endif
if system_has_kms_drm and not with_platform_android
  vulkan_wsi_args += '-DVK_USE_PLATFORM_DISPLAY_KHR'
  vulkan_wsi_deps += [dep_libdrm]
  vulkan_wsi_list += ['drm']
endif
if with_xlib_lease
  vulkan_wsi_args += '-DVK_USE_PLATFORM_XLIB_XRANDR_EXT'
  vulkan_wsi_deps += [dep_xcb_xrandr, dep_xlib_xrandr]
  vulkan_wsi_list += ['xlib_xrandr']
endif


subdir('util')
subdir('wsi')
if with_vulkan_overlay_layer
  subdir('overlay-layer')
endif
if get_option('vulkan-device-select-layer')
  subdir('device-select-layer')
endif
