project(
  'roxmltree',
  'rust',
  version : '0.20.0',
  license :'MIT OR Apache-2.0',
)

lib = static_library(
  'roxmltree',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi : 'rust',
  native : true,
)

dep_roxmltree = declare_dependency(
  link_with : [lib],
)
