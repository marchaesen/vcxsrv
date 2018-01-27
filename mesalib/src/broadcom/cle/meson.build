# Copyright © 2017 Broadcom
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

v3d_versions = [
  21,
  33,
  41,
  42,
]

v3d_xml_files = []
foreach v: v3d_versions
  v3d_xml_files += 'v3d_packet_v@0@.xml'.format(v)
endforeach

v3d_xml_h = custom_target(
  'v3d_xml.h',
  input : ['../../intel/genxml/gen_zipped_file.py', v3d_xml_files],
  output : 'v3d_xml.h',
  command : [prog_python2, '@INPUT@'],
  capture : true,
)

v3d_xml_pack = []
foreach f : v3d_xml_files
  _name = '@0@_pack.h'.format(f.split('.')[0])
  v3d_xml_pack += custom_target(
    _name,
    input : ['gen_pack_header.py', f],
    output : _name,
    command : [prog_python2, '@INPUT@'],
    capture : true,
  )
endforeach

libbroadcom_cle = static_library(
  ['broadcom_cle', v3d_xml_h],
  'v3d_decoder.c',
  include_directories : [inc_common, inc_broadcom],
  c_args : [c_vis_args, no_override_init_args],
  dependencies : [dep_libdrm, dep_valgrind],
  build_by_default : false,
)
