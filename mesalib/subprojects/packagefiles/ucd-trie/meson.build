project(
  'ucd_trie',
  'rust',
  version : '0.1.6',
  license : '(MIT or Apache-2.0) AND Unicode-DFS-2016)',
)

lib = static_library(
  'ucd_trie',
  'src/lib.rs',
  override_options : ['rust_std=2018', 'build.rust_std=2018'],
  rust_abi : 'rust',
  native : true,
)
