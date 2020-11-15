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

files_vulkan_wsi = files('wsi_common.c')

if dep_libdrm.found()
  files_vulkan_wsi += files('wsi_common_drm.c')
endif

if with_platform_x11
  files_vulkan_wsi += files('wsi_common_x11.c')
endif

if with_platform_wayland
  files_vulkan_wsi += files('wsi_common_wayland.c')
  files_vulkan_wsi += [
    wayland_drm_client_protocol_h,
    wayland_drm_protocol_c,
    linux_dmabuf_unstable_v1_client_protocol_h,
    linux_dmabuf_unstable_v1_protocol_c,
  ]
endif

if system_has_kms_drm and not with_platform_android
  files_vulkan_wsi += files('wsi_common_display.c')
endif

libvulkan_wsi = static_library(
  'vulkan_wsi',
  files_vulkan_wsi,
  include_directories : [inc_include, inc_src],
  dependencies : [vulkan_wsi_deps, dep_libdrm, idep_vulkan_util, idep_xmlconfig],
  c_args : [vulkan_wsi_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)
