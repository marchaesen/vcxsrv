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

inc_loader = include_directories('.')

if with_platform_x11 and with_dri3
  libloader_dri3_helper = static_library(
    'loader_dri3_helper',
    ['loader_dri3_helper.c', 'loader_dri3_helper.h'],
    c_args : c_vis_args,
    include_directories : [inc_include, inc_src, inc_drm_uapi],
    dependencies : [
      dep_libdrm, dep_xcb_dri3, dep_xcb_present, dep_xcb_sync, dep_xshmfence,
    ],
    build_by_default : false,
  )
else
  libloader_dri3_helper = []
endif

libloader = static_library(
  'loader',
  ['loader.c', 'loader.h', 'pci_id_driver_map.c', 'pci_id_driver_map.h',
   xmlpool_options_h],
  c_args : [c_vis_args, '-DUSE_DRICONF'],
  include_directories : [inc_include, inc_src, inc_util],
  dependencies : [dep_libdrm, dep_thread],
  build_by_default : false,
)
