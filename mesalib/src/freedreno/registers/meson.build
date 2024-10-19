# Copyright © 2019 Google, Inc
# SPDX-License-Identifier: MIT

xml_files = [
  'adreno.xml',
]

rules_file = 'rules-fd.xsd'
copyright_file = 'freedreno_copyright.xml'

install_files = xml_files + [rules_file, copyright_file]

gen_header_py = files('gen_header.py')
freedreno_schema = files(rules_file)
freedreno_copyright = files(copyright_file)

freedreno_xml_header_files = []

foreach f : xml_files
  _name = f + '.h'
  freedreno_xml_header_files += custom_target(
    _name,
    input: [gen_header_py, f, freedreno_schema, freedreno_copyright],
    output: _name,
    command: [prog_python, '@INPUT0@', '--rnn', rnn_src_path, '--xml', '@INPUT1@', 'c-defines'],
    capture: true,
  )
endforeach

foreach f : install_files
  _gzname = f + '.gz'
  custom_target(
    _gzname,
    input: f,
    output: _gzname,
    command: [prog_gzip, '-kc', '@INPUT@'],
    capture: true,
    install_dir: rnn_install_path,
    install: install_fd_decode_tools,
    build_by_default: install_fd_decode_tools,
  )
endforeach

subdir('adreno')
