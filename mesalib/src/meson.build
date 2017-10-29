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

inc_common = include_directories(
  '../include', '.', 'mapi', 'mesa', 'gallium/include', 'gallium/auxiliary')
inc_mesa = include_directories('mesa')
inc_mapi = include_directories('mapi')
inc_src = include_directories('.')
inc_gallium = include_directories('gallium/include')
inc_gallium_aux = include_directories('gallium/auxiliary')

libglsl_util = static_library(
  'glsl_util',
  files('mesa/main/extensions_table.c', 'mesa/main/imports.c',
        'mesa/program/prog_parameter.c', 'mesa/program/symbol_table.c',
        'mesa/program/dummy_errors.c'),
  include_directories : [inc_common],
  c_args : [c_vis_args],
  build_by_default : false,
)

sha1_h = custom_target(
  'git_sha1.h',
  output : 'git_sha1.h',
  command : [prog_python2, git_sha1_gen_py, '--output', '@OUTPUT@'],
  build_always : true, # commit sha1 can change without having touched these files
)

subdir('gtest')
subdir('util')
subdir('mapi/glapi/gen')
subdir('mapi')
# TODO: opengl
subdir('compiler')
subdir('egl/wayland/wayland-drm')
subdir('vulkan')
subdir('amd')
if with_gallium_vc4
  subdir('broadcom')
endif
subdir('intel')
subdir('mesa')
subdir('loader')
subdir('glx')
if with_platform_wayland
  subdir('egl/wayland/wayland-egl')
endif
if with_gbm
  subdir('gbm')
endif
if with_egl
  subdir('egl')
endif
subdir('gallium')
