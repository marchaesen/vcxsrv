project(
  'pest_meta',
  'rust',
  version : '2.7.11',
  license :'MIT OR Apache-2.0',
)

once_cell = subproject('once_cell').get_variable('lib')
pest = subproject('pest').get_variable('lib')

lib = static_library(
  'pest_meta',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  link_with : [once_cell, pest],
  rust_abi : 'rust',
  native : true,
)

deb_pest_meta = declare_dependency(
  link_with : [lib, once_cell, pest],
)
