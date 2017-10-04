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

# TODO: libglsl_util

# TODO: git_sha. There's a meson builtin for this

inc_common = include_directories(
  '../include', '.', 'mapi', 'mesa', 'gallium/include', 'gallium/auxiliary')
inc_mesa = include_directories('mesa')
inc_mapi = include_directories('mapi')

subdir('gtest')
subdir('util')
#subdir('mapi/glapi/gen')
# TODO: mapi
# TODO: opengl
# TODO: glx
# TODO: osmesa
subdir('compiler')
subdir('egl/wayland/wayland-drm')
subdir('vulkan')
subdir('amd')
subdir('intel')
# TODO: vc4
# TODO: opengl_common
# TODO: dri_glx
# TODO: gbm
# TODO: egl
# TODO: radv
# TODO: gallium
