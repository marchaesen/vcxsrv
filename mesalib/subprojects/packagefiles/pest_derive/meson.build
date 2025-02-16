project(
  'pest_derive',
  'rust',
  version : '2.7.11',
  license :'MIT OR Apache-2.0',
)

pest = subproject('pest').get_variable('lib')
pest_generator = subproject('pest_generator').get_variable('lib')

rust = import('rust')

lib = rust.proc_macro(
  'pest_derive',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  link_with : [pest, pest_generator],
)

dep_pest_derive = declare_dependency(
  link_with : [lib, pest, pest_generator],
)
