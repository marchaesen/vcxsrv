# Copyright © 2018 Rob Clark

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

inc_freedreno = include_directories(['.', './registers', './registers/adreno', './common'])
inc_freedreno_rnn = include_directories('rnn')

rnn_src_path = meson.source_root() + '/src/freedreno/registers'
rnn_install_path = get_option('datadir') + '/freedreno/registers'
rnn_path = rnn_src_path + ':' + get_option('prefix') + '/' + rnn_install_path

dep_lua = dependency('lua53', required: false)
if not dep_lua.found()
  dep_lua = dependency('lua52', required: false)
endif
if not dep_lua.found()
  dep_lua = dependency('lua', required: false, version : '>=5.2')
endif

dep_libarchive = dependency('libarchive', required: false)
dep_libxml2 = dependency('libxml-2.0', required: false)
prog_gzip = find_program('gzip', required: false)

install_fd_decode_tools = dep_libxml2.found() and prog_gzip.found() and \
  with_tools.contains('freedreno')

subdir('common')
subdir('registers')
subdir('drm')
subdir('ir2')
subdir('ir3')
subdir('fdl')
subdir('perfcntrs')
subdir('computerator')

# Everything that depends on rnn requires (indirectly) libxml2:
if dep_libxml2.found()
  subdir('rnn')
  subdir('decode')
  subdir('afuc')
endif

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif

if with_freedreno_vk
  subdir('vulkan')
endif
