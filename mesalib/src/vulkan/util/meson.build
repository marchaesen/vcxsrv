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

files_vulkan_util = files(
  'vk_alloc.h',
  'vk_util.c',
  'vk_util.h',
)

vk_enum_to_str = custom_target(
  'vk_enum_to_str',
  input : ['gen_enum_to_str.py', vk_api_xml[0]],
  output : ['vk_enum_to_str.c', 'vk_enum_to_str.h'],
  command : [prog_python2, '@INPUT0@', '--xml', '@INPUT1@',
             '--outdir', meson.current_build_dir()],
)

libvulkan_util = static_library(
  'vulkan_util',
  [files_vulkan_util, vk_enum_to_str],
  include_directories : [inc_common, inc_vulkan],
  c_args : [c_vis_args],
  build_by_default : false,
)
