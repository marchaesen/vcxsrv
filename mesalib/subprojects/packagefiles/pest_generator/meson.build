project(
  'pest_generator',
  'rust',
  version : '2.7.11',
  license :'MIT OR Apache-2.0',
)

pest = subproject('pest').get_variable('lib')
pest_meta = subproject('pest_meta').get_variable('lib')
quote = subproject('quote').get_variable('lib')
pm2 = subproject('proc-macro2').get_variable('lib')
syn = subproject('syn').get_variable('lib')

rust_args = [
  '--cfg', 'feature="std"',
]

lib = static_library(
  'pest_generator',
  'src/lib.rs',
  rust_args : rust_args,
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  link_with : [pest, pest_meta, pm2, quote, syn],
  rust_abi : 'rust',
  native : true,
)

dep_pest_generator = declare_dependency(
  link_with : [lib, pest, pest_meta, quote, syn],
)
