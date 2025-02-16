project(
  'once_cell',
  'rust',
  version : '1.8.0',
  license :'MIT OR Apache-2.0',
)

rust_args = [
  '--cfg', 'feature="std"'
]

lib = static_library(
  'once_cell',
  'src/lib.rs',
  rust_args : rust_args,
  override_options : ['rust_std=2018', 'build.rust_std=2018'],
  rust_abi : 'rust',
  native : true,
)

dep_once_cell = declare_dependency(
  link_with : [lib],
)
