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


files_swr_common += custom_target(
  'gen_backend_pixel',
  input : swr_gen_backends_py,
  output : [
    'gen_BackendPixelRate0.cpp', 'gen_BackendPixelRate1.cpp',
    'gen_BackendPixelRate2.cpp', 'gen_BackendPixelRate3.cpp',
    'gen_BackendPixelRate.hpp',
  ],
  command : [
    prog_python, '@INPUT@',
    '--outdir', '@OUTDIR@',
    '--dim', '5', '2', '3', '2', '2', '2',
    '--numfiles', '4',
    '--cpp', '--hpp',
  ],
  depend_files : [ swr_gen_backend_files, swr_gen_header_init_files ],
)

files_swr_common += custom_target(
  'gen_backend_raster',
  input : swr_gen_backends_py,
  output : [
    'gen_rasterizer0.cpp', 'gen_rasterizer1.cpp',
    'gen_rasterizer2.cpp', 'gen_rasterizer3.cpp',
    'gen_rasterizer.hpp',
  ],
  command : [
    prog_python, '@INPUT@',
    '--outdir', '@OUTDIR@',
    '--rast',
    '--dim', '5', '2', '2', '3', '5', '2',
    '--numfiles', '4',
    '--cpp', '--hpp',
  ],
  depend_files : [ swr_gen_rasterizer_files, swr_gen_header_init_files ],
)
