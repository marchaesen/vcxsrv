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

vulkan_wsi_args = []
vulkan_wsi_deps = []

files_vulkan_wsi = files(
  'wsi_common.c',
  'wsi_common.h',
  'wsi_common_private.h',
  'wsi_common_queue.h',
)
if with_platform_x11
  vulkan_wsi_args += ['-DVK_USE_PLATFORM_XCB_KHR', '-DVK_USE_PLATFORM_XLIB_KHR']
  vulkan_wsi_deps += [
    dep_xcb,
    dep_x11_xcb,
    dep_xcb_dri2,
    dep_xcb_dri3,
    dep_xcb_present,
    dep_xcb_sync,
    dep_xshmfence,
  ]
  files_vulkan_wsi += files(
    'wsi_common_x11.c',
    'wsi_common_x11.h',
  )
endif

if with_platform_wayland
  vulkan_wsi_deps += dep_wayland_client
  vulkan_wsi_args += ['-DVK_USE_PLATFORM_WAYLAND_KHR']
  files_vulkan_wsi += files(
    'wsi_common_wayland.c',
    'wsi_common_wayland.h',
  )
  files_vulkan_wsi += [
    wayland_drm_client_protocol_h,
    wayland_drm_protocol_c,
    linux_dmabuf_unstable_v1_client_protocol_h,
    linux_dmabuf_unstable_v1_protocol_c,
  ]
endif

libvulkan_wsi = static_library(
  'vulkan_wsi',
  files_vulkan_wsi,
  include_directories : [inc_common, inc_vulkan_util, inc_drm_uapi],
  dependencies : [vulkan_wsi_deps, dep_libdrm],
  c_args : [c_vis_args, vulkan_wsi_args],
  build_by_default : false,
)
