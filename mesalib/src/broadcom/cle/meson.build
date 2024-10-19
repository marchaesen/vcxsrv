# Copyright © 2017 Broadcom
# SPDX-License-Identifier: MIT

# [version, cle XML file]
v3d_versions = [
  [21, 'vc4_packet.xml'],
  [42, 'v3d_packet.xml'],
  [71, 'v3d_packet.xml']
]

v3d_xml_files = []
v3d_xml_pack = []
foreach _v : v3d_versions
  v = _v[0]
  xmlfile = _v[1]
  _name = 'v3d_packet_v@0@_pack.h'.format(v)
  if not v3d_xml_files.contains(xmlfile)
    v3d_xml_files += xmlfile
  endif
  v3d_xml_pack += custom_target(
    _name,
    input : ['gen_pack_header.py', xmlfile],
    output : _name,
    command : [prog_python, '@INPUT@', '@0@'.format(v)],
    capture : true,
  )
endforeach

v3d_xml_h = custom_target(
  'v3d_xml.h',
  input : ['../../util/gen_zipped_xml_file.py', v3d_xml_files],
  output : 'v3d_xml.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

expat_args = []
if dep_expat.found()
  expat_args += '-DWITH_LIBEXPAT=1'
endif

libbroadcom_cle = static_library(
  'broadcom_cle',
  ['v3d_decoder.c', v3d_xml_h],
  include_directories : [inc_include, inc_src, inc_broadcom],
  c_args : [no_override_init_args, expat_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_libdrm, dep_valgrind, dep_expat, dep_zlib],
  build_by_default : false,
)
