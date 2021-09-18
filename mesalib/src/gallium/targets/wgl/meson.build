# Copyright © Microsoft Corporation

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


gallium_wgl_link_args = []
if cc.get_id() == 'gcc' and host_machine.cpu_family() != 'x86_64'
  gallium_wgl_link_args += ['-Wl,--enable-stdcall-fixup']
endif

wgldef = files('gallium_wgl.def')[0]

libgallium_wgl = shared_library(
  'gallium_wgl',
  ['wgl.c'],
  vs_module_defs : wgldef,
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_wgl, inc_gallium_winsys, inc_gallium_winsys_sw, inc_gallium_drivers,
  ],
  link_whole : [libwgl],
  link_with : [
    libgallium, libglsl, libmesa_gallium, libwsgdi, libglapi_static, libglapi
  ],
  dependencies : [
    dep_ws2_32, idep_nir, idep_mesautil, driver_swrast, driver_swr,
    driver_d3d12, driver_zink, winsys_d3d12_wgl
  ],
  link_args : gallium_wgl_link_args,
  name_prefix : 'lib',
  install : true,
)
libgallium_wgl_build_dir = meson.current_build_dir()
