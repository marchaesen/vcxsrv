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
inc_amd_common = include_directories('amd/common')

libglsl_util = static_library(
  'glsl_util',
  files(
    'mesa/main/extensions_table.c', 'mesa/main/imports.c',
    'mesa/program/prog_parameter.c', 'mesa/program/symbol_table.c',
    'mesa/program/dummy_errors.c',
  ),
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
subdir('mapi')
# TODO: opengl
subdir('compiler')
subdir('egl/wayland/wayland-drm')
subdir('vulkan')
if with_gallium_radeonsi or with_amd_vk
  subdir('amd')
endif
if with_gallium_vc4
  subdir('broadcom')
endif
if with_dri_i965 or with_intel_vk
  subdir('intel')
endif
subdir('mesa')
subdir('loader')
if with_platform_haiku
  subdir('hgl')
endif
if with_glx != 'disabled'
  subdir('glx')
endif
if with_gbm
  subdir('gbm')
else
  inc_gbm = []
endif
if with_egl
  subdir('egl')
endif
if with_gallium
  subdir('gallium')
endif

# This must be after at least mesa, glx, and gallium, since libgl will be
# defined in one of those subdirs depending on the glx provider.
if with_glx != 'disabled'
  pkg.generate(
    name : 'gl',
    description : 'Mesa OpenGL Library',
    version : meson.project_version(),
    libraries : libgl,
    libraries_private : gl_priv_libs,
    requires_private : gl_priv_reqs,
    variables : ['glx_tls=yes'],
  )
endif
