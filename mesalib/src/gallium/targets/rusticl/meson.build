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

opencl_version = '1'

librusticl = shared_library(
  'RusticlOpenCL',
  'target.c',
  gnu_symbol_visibility : 'hidden',
  include_directories : [
    inc_include,
    inc_src,
    inc_gallium,
    inc_gallium_aux,
    inc_gallium_drivers,
    inc_gallium_winsys,
  ],
  link_whole : librusticl,
  link_with : [
    libgalliumvl,
    libpipe_loader_static,
    libswdri,
    libswkmsdri,
    libwsw,
    libws_null,
  ],
  dependencies : [
    driver_iris,
    driver_nouveau,
    driver_panfrost,
    driver_swrast,
    idep_nir,
  ],
  version : '@0@.0.0'.format(opencl_version),
  install : true,
)

_config = configuration_data()
_config.set('OPENCL_VERSION', opencl_version)
configure_file(
  configuration : _config,
  input : 'rusticl.icd.in',
  output : 'rusticl.icd',
  install : true,
  install_dir : join_paths(get_option('sysconfdir'), 'OpenCL', 'vendors'),
)
