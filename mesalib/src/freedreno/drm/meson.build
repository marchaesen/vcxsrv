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

libfreedreno_drm_files = files(
  'freedreno_bo.c',
  'freedreno_bo_cache.c',
  'freedreno_device.c',
  'freedreno_drmif.h',
  'freedreno_pipe.c',
  'freedreno_priv.h',
  'freedreno_ringbuffer.c',
  'freedreno_ringbuffer.h',
  'msm_bo.c',
  'msm_device.c',
  'msm_pipe.c',
  'msm_priv.h',
  'msm_ringbuffer.c',
  'msm_ringbuffer_sp.c',
)

libfreedreno_drm = static_library(
  'freedreno_drm',
  [
    libfreedreno_drm_files,
    freedreno_xml_header_files,
  ],
  include_directories : [
    inc_freedreno,
    inc_include,
    inc_src,
    inc_mapi,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm,
    dep_valgrind,
  ],
  build_by_default : false,
)

