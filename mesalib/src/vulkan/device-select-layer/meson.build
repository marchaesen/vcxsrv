# Copyright © 2019 Intel Corporation

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

vklayer_files = files(
  'device_select_layer.c',
)

vklayer_deps = []
vklayer_flags = []

if with_platform_x11
   vklayer_files += files('device_select_x11.c')
   vklayer_deps += dep_xcb_dri3
   vklayer_flags += [ '-DVK_USE_PLATFORM_XCB_KHR' ]
endif

if with_platform_wayland
   vklayer_files += files('device_select_wayland.c')
   vklayer_files += [ wayland_drm_client_protocol_h, wayland_drm_protocol_c ]
   vklayer_deps += [dep_wayland_client, dep_wl_protocols]
   vklayer_flags += '-DVK_USE_PLATFORM_WAYLAND_KHR'
endif

vklayer_mesa_device_select = shared_library(
  'VkLayer_MESA_device_select',
  vklayer_files,
  c_args : [no_override_init_args, vklayer_flags],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_vulkan_util, idep_mesautil, vklayer_deps, dep_libdrm, dep_dl],
  include_directories : [inc_include, inc_util, inc_vulkan_wsi],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
  install : true
)

install_data(
  files('VkLayer_MESA_device_select.json'),
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
)
