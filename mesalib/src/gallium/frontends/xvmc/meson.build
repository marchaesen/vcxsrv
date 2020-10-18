# Copyright © 2017, 2019 Intel Corporation

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

XVMC_MAJOR = 1
XVMC_MINOR = 0

libxvmc_st = static_library(
  'xvmc_st',
  files('attributes.c', 'block.c', 'context.c', 'surface.c', 'subpicture.c'),
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  dependencies : [
    dep_xvmc, dep_x11_xcb, dep_xcb, dep_xcb_dri2, dep_xcb_dri3, dep_xv,
    dep_libdrm,
  ],
)

# These tests will not work without a working xvmc configuration.
if with_tools.contains('xvmc')
  # pkg-config was added in 1.0.12, so allow fallback
  dep_xvmcw = dependency('xvmc-wrapper', required : false)
  if not dep_xvmcw.found()
    dep_xvmcw = cc.find_library('XvMCW')
  endif
  dep_real_xvmc = dependency('xvmc')

  foreach x : ['context', 'surface', 'subpicture', 'blocks', 'rendering']
    _name = 'xvmc_@0@'.format(x)
    executable(
      _name,
      files('tests/test_@0@.c'.format(x), 'tests/testlib.c'),
      dependencies : [
        dep_real_xvmc, dep_x11_xcb, dep_xcb, dep_xcb_dri2, dep_xcb_dri3,
        dep_xvmcw, dep_xv,
      ],
      install: true,
    )
  endforeach

  executable(
    'xvmc_bench',
    files('tests/xvmc_bench.c', 'tests/testlib.c'),
    dependencies : [
      dep_real_xvmc, dep_x11_xcb, dep_xcb, dep_xcb_dri2, dep_xcb_dri3,
      dep_xvmcw, dep_xv,
    ],
    install: true,
  )
endif
