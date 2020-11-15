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
  'inc/addrinterface.h',
  'inc/addrtypes.h',
  'src/addrinterface.cpp',
  'src/core/addrcommon.h',
  'src/core/addrelemlib.cpp',
  'src/core/addrelemlib.h',
  'src/core/addrlib.cpp',
  'src/core/addrlib.h',
  'src/core/addrlib1.cpp',
  'src/core/addrlib1.h',
  'src/core/addrlib2.cpp',
  'src/core/addrlib2.h',
  'src/core/addrobject.cpp',
  'src/core/addrobject.h',
  'src/core/coord.cpp',
  'src/core/coord.h',
  'src/gfx9/gfx9addrlib.cpp',
  'src/gfx9/gfx9addrlib.h',
  'src/gfx10/gfx10addrlib.cpp',
  'src/gfx10/gfx10addrlib.h',
  'src/gfx10/gfx10SwizzlePattern.h',
  'src/amdgpu_asic_addr.h',
  'src/chip/gfx10/gfx10_gb_reg.h',
  'src/chip/gfx9/gfx9_gb_reg.h',
  'src/chip/r800/si_gb_reg.h',
  'src/r800/ciaddrlib.cpp',
  'src/r800/ciaddrlib.h',
  'src/r800/egbaddrlib.cpp',
  'src/r800/egbaddrlib.h',
  'src/r800/siaddrlib.cpp',
  'src/r800/siaddrlib.h',
)

cpp_args_addrlib = []
if cpp.get_id() != 'msvc'
  cpp_args_addrlib += '-Wno-unused-variable'
  cpp_args_addrlib += '-Wno-unused-local-typedefs'
  cpp_args_addrlib += '-Wno-unused-but-set-variable'
  cpp_args_addrlib += '-Wno-maybe-uninitialized'
endif

libamdgpu_addrlib = static_library(
  'addrlib',
  files_addrlib,
  include_directories : [
    include_directories(
      'inc', 'src', 'src/core', 'src/chip/gfx9', 'src/chip/r800',
      'src/chip/gfx10',
    ),
    inc_amd_common, inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux,
  ],
  cpp_args : cpp_args_addrlib,
  gnu_symbol_visibility : 'hidden',
)
