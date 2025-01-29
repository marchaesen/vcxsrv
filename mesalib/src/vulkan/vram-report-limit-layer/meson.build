# Copyright © 2019 Intel Corporation
# Copyright © 2024 Igalia S.L.

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

vklayer_files = files(
  'vram_report_limit_layer.c',
)

vklayer_mesa_vram_report_limit = shared_library(
  'VkLayer_MESA_vram_report_limit',
  vklayer_files,
  c_args : no_override_init_args,
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_vulkan_util, idep_mesautil
  ],
  include_directories : [inc_include, inc_util, inc_src],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
  install : true
)

install_data(
  files('VkLayer_MESA_vram_report_limit.json'),
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'explicit_layer.d'),
  install_tag : 'runtime',
)
