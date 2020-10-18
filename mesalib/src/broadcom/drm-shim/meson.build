# Copyright © 2019 Broadcom
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

libv3d_noop_drm_shim = shared_library(
  ['v3d_noop_drm_shim'],
  'v3d_noop.c',
  include_directories: [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  dependencies: dep_drm_shim,
  gnu_symbol_visibility : 'hidden',
  install : true,
)

dep_v3dv3 = dependency('v3dv3', required: false)
if dep_v3dv3.found()
  v3dv3_c_args = '-DUSE_V3D_SIMULATOR'

  inc_gallium_v3d = include_directories('../../gallium/drivers/v3d')

  per_version_libs = []
  foreach ver : v3d_versions
    per_version_libs += static_library(
        'libv3d_drm_shim-v' + ver,
        [
                            'v3dx.c',
                            v3d_xml_pack
        ],
        include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_broadcom, inc_gallium_v3d, inc_simulator],
        c_args : [no_override_init_args, '-DV3D_VERSION=' + ver, v3dv3_c_args],
        gnu_symbol_visibility : 'hidden',
        dependencies: [dep_valgrind, dep_thread, dep_v3dv3],
    )
  endforeach

  libv3d_drm_shim = shared_library(
    ['v3d_drm_shim'],
    [
      'v3d.c',
      '../simulator/v3d_simulator_wrapper.cpp',
    ],
    dependencies: [idep_mesautil, dep_dl, dep_drm_shim, dep_v3dv3],
    link_with: per_version_libs,
    include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_broadcom, inc_gallium_v3d, inc_simulator],
    c_args : [no_override_init_args, '-std=gnu99', v3dv3_c_args],
    gnu_symbol_visibility : 'hidden',
    cpp_args : [v3dv3_c_args]
  )
endif
