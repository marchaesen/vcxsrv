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

libgl = shared_library(
  'GL',
  files(
    'GLView.cpp', 'GLRenderer.cpp', 'GLRendererRoster.cpp', 'GLDispatcher.cpp',
  ),
  link_args : [ld_args_bsymbolic, ld_args_gc_sections],
  include_directories : [
    inc_src, inc_mapi, inc_mesa, inc_include, inc_glapi, inc_haikugl,
    inc_gl_internal, include_directories('/system/develop/headers/private')
  ],
  link_with : [libglapi_static, libglapi],
  dependencies : cpp.find_library('be'),
  install : true,
)

# TODO: We need some tests here
