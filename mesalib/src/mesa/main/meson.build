# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

main_dispatch_h = custom_target(
  'dispatch.h',
  input : [files('../../mapi/glapi/gen/gl_table.py'), gl_and_es_api_files],
  output : 'dispatch.h',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@', '-m', 'remap_table'],
  depend_files : glapi_gen_depends,
  capture : true,
)

main_marshal_generated_h = custom_target(
  'marshal_generated.h',
  input : [files('../../mapi/glapi/gen/gl_marshal_h.py'), gl_and_es_api_files],
  output : 'marshal_generated.h',
  command : [prog_python, '@INPUT0@', '@INPUT1@', sizeof_pointer],
  depend_files : files('../../mapi/glapi/gen/marshal_XML.py') + glapi_gen_depends,
  capture : true,
)

main_remap_helper_h = custom_target(
  'remap_helper.h',
  input : [files('../../mapi/glapi/gen/remap_helper.py'), gl_and_es_api_files],
  output : 'remap_helper.h',
  command : [prog_python, '@INPUT0@', '-f', '@INPUT1@'],
  depend_files : glapi_gen_depends,
  capture : true,
)

if _shader_replacement != ''
  # shader replacement
  shader_replacement_h = custom_target(
    'shader_replacement.h',
    input: [files(_shader_replacement + '/process_shaders.py')],
    output: 'shader_replacement.h',
    command: [prog_python, '@INPUT0@', _shader_replacement, '@OUTPUT@'],
    build_always: true,
  )
endif
