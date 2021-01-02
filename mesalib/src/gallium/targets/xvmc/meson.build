# Copyright © 2017-2018 Intel Corporation

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

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

xvmc_link_args = []
xvmc_link_depends = []
xvmc_drivers = []

if with_ld_version_script
  xvmc_link_args += ['-Wl,--version-script', join_paths(meson.current_source_dir(), 'xvmc.sym')]
  xvmc_link_depends += files('xvmc.sym')
endif

libxvmc_gallium = shared_library(
  'XvMCgallium',
  'target.c',
  gnu_symbol_visibility : 'hidden',
  link_args : [xvmc_link_args, ld_args_gc_sections],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_gallium_winsys, inc_gallium_drivers,
  ],
  link_whole : [libxvmc_st],
  link_with : [
    libgalliumvlwinsys, libgalliumvl, libgallium,
    libpipe_loader_static, libws_null, libwsw, libswdri, libswkmsdri,
  ],
  dependencies : [idep_mesautil, driver_r600, driver_nouveau],
  link_depends : xvmc_link_depends,
  # Will be deleted during installation, see install_megadrivers.py
  install : true,
  install_dir : xvmc_drivers_path,
  name_suffix : 'so',
  version : '@0@.@1@.0'.format(XVMC_MAJOR, XVMC_MINOR),
)

foreach d : [[with_gallium_r600, 'r600'], [with_gallium_nouveau, 'nouveau']]
  if d[0]
    xvmc_drivers += 'libXvMC@0@.so.@1@.@2@.0'.format(d[1], XVMC_MAJOR, XVMC_MINOR)
  endif
endforeach

meson.add_install_script(
  install_megadrivers_py.path(),
  libxvmc_gallium.full_path(),
  xvmc_drivers_path,
  xvmc_drivers,
)
