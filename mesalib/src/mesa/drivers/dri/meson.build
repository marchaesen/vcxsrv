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

subdir('common')

dri_drivers = []
dri_link = []
if with_dri_swrast
  subdir('swrast')
endif
if with_dri_i915
  subdir('i915')
endif
if with_dri_i965
  subdir('i965')
endif
if with_dri_r100
  subdir('radeon')
endif
if with_dri_r200
  subdir('r200')
endif
if with_dri_nouveau
  subdir('nouveau')
endif

if dri_drivers != []
  libmesa_dri_drivers = shared_library(
    'mesa_dri_drivers',
    dummy_cpp,  # see meson #2180
    link_whole : dri_drivers,
    link_with : [
      libmegadriver_stub, libdricommon, libxmlconfig, libglapi, libmesa_util,
      libmesa_classic,
    ],
    dependencies : [
      dep_selinux, dep_libdrm, dep_expat, dep_m, dep_thread, dep_dl, idep_nir,
    ],
    link_args : [ld_args_build_id, ld_args_bsymbolic, ld_args_gc_sections],
  )

  meson.add_install_script(
    join_paths(meson.source_root(), 'bin/install_megadrivers.py'),
    libmesa_dri_drivers.full_path(),
    dri_drivers_path,
    dri_link,
  )
endif

# This needs to be installed if any dri drivers (including gallium dri drivers)
# are built.
if with_dri
  dri_req_private = []
  if dep_libdrm.found()
    dri_req_private = ['libdrm >= ' + dep_libdrm.version()]
  endif

  pkg.generate(
    name : 'dri',
    filebase : 'dri',
    description : 'Direct Rendering Infrastructure',
    version : meson.project_version(),
    variables : ['dridriverdir=${prefix}/' + dri_drivers_path],
    requires_private : dri_req_private,
  )
endif
