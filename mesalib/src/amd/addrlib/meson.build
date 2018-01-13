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

files_addrlib = files(
  'addrinterface.cpp',
  'addrinterface.h',
  'addrtypes.h',
  'core/addrcommon.h',
  'core/addrelemlib.cpp',
  'core/addrelemlib.h',
  'core/addrlib.cpp',
  'core/addrlib.h',
  'core/addrlib1.cpp',
  'core/addrlib1.h',
  'core/addrlib2.cpp',
  'core/addrlib2.h',
  'core/addrobject.cpp',
  'core/addrobject.h',
  'gfx9/chip/gfx9_enum.h',
  'gfx9/coord.cpp',
  'gfx9/coord.h',
  'gfx9/gfx9addrlib.cpp',
  'gfx9/gfx9addrlib.h',
  'amdgpu_asic_addr.h',
  'inc/chip/gfx9/gfx9_gb_reg.h',
  'inc/chip/r800/si_gb_reg.h',
  'r800/chip/si_ci_vi_merged_enum.h',
  'r800/ciaddrlib.cpp',
  'r800/ciaddrlib.h',
  'r800/egbaddrlib.cpp',
  'r800/egbaddrlib.h',
  'r800/siaddrlib.cpp',
  'r800/siaddrlib.h',
)

libamdgpu_addrlib = static_library(
  'addrlib',
  files_addrlib,
  include_directories : [
    include_directories(
      'core', 'inc/chip/gfx9', 'inc/chip/r800', 'gfx9/chip', 'r800/chip',
    ),
    inc_amd_common, inc_src,
  ],
  cpp_args : cpp_vis_args,
)
