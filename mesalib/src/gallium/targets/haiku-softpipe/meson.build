# Copyright © 2017 Dylan Baker

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

libswpipe = shared_library(
  'swpipe',
  files('SoftwareRenderer.cpp', 'GalliumContext.cpp'),
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_haikugl, inc_gallium_drivers, inc_gallium_winsys,
    include_directories('../../frontends/hgl'),
    include_directories('/boot/system/develop/headers/private')
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_bsymbolic, ld_args_gc_sections],
  link_with : [
    libglapi, libswhgl, libsthgl, libcompiler,
    libmesa_gallium, libglsl, libgallium, libgl
  ],
  dependencies : [
    driver_swrast, cpp.find_library('be'), cpp.find_library('translation'),
    cpp.find_library('network'), dep_unwind, idep_mesautil, idep_nir,
  ]
)
