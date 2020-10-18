# Copyright © 2017 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

vtn_gather_types_c = custom_target(
  'vtn_gather_types.c',
  input : files('vtn_gather_types_c.py', 'spirv.core.grammar.json'),
  output : 'vtn_gather_types.c',
  command : [prog_python, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
)

spirv_info_c = custom_target(
  'spirv_info.c',
  input : files('spirv_info_c.py', 'spirv.core.grammar.json'),
  output : 'spirv_info.c',
  command : [prog_python, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
)

vtn_generator_ids_h = custom_target(
  'vtn_generator_ids.h',
  input : files('vtn_generator_ids_h.py', 'spir-v.xml'),
  output : 'vtn_generator_ids.h',
  command : [prog_python, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
)
