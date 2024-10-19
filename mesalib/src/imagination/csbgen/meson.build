# Copyright © 2022 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

pvr_xml_files = [
  'rogue_cdm.xml',
  'rogue_cr.xml',
  'rogue_ipf.xml',
  'rogue_kmd_stream.xml',
  'rogue_lls.xml',
  'rogue_pbestate.xml',
  'rogue_pds.xml',
  'rogue_ppp.xml',
  'rogue_texstate.xml',
  'rogue_vdm.xml',
]

pvr_xml_pack = []
foreach f : pvr_xml_files
  _name = '@0@.h'.format(f.split('.')[0])
  pvr_xml_pack += custom_target(
    _name,
    input : ['gen_pack_header.py', f],
    output : _name,
    command : [prog_python, '@INPUT@'],
    capture : true,
  )
endforeach

dep_csbgen = declare_dependency(sources : [pvr_xml_pack])
