fontations = get_option('fontations')

if (fontations.enabled())
  rust = import('rust')

  generated_fontconfig = rust.bindgen(
    input : '../fontconfig/fontconfig.h',
    output : 'fontconfig.rs',
    include_directories : [ '../' ],
    args : [
      '--merge-extern-blocks',
      '--allowlist-item=(FcCharSet.*|FC_(SLANT|WEIGHT|WIDTH)_.*|FcFontSet(Add|Create|Destroy).*|FcLangSet(Destroy|Copy)|FcWeightFromOpenType.*)',
      '--raw-line=#![allow(nonstandard_style,unused)]',
      '--raw-line= ',
      '--raw-line=pub mod fcint;',
    ],
    c_args : ['-DBINDGEN_IGNORE_VISIBILITY=1'],
  )

  generated_fcint = rust.bindgen(
    input : '../src/fcint.h',
    output : 'fcint.rs',
    include_directories : [ '../' ],
    args : [
      '--merge-extern-blocks',
      '--allowlist-item=(FcPattern.*|FcRange.*|FC_.*_OBJECT|FcCharSet.*|FcFreeTypeLangSet)',
      '--blocklist-type=(FcCharSet|FcLangSet)',
      '--raw-line=#![allow(nonstandard_style,unused)]',
      '--raw-line= ',
      '--raw-line=pub use FcCharSet; pub use FcLangSet;',
    ],
    c_args : ['-DBINDGEN_IGNORE_VISIBILITY=1'],
  )

  bindgen_lib = static_library(
    'fc_fontations_bindgen',
    sources: [generated_fontconfig, generated_fcint],
    rust_abi : 'rust',
  )

  fc_fontations = static_library(
    'fc_fontations',
    sources: ['mod.rs'],
    link_with: [bindgen_lib, libfontconfig],
    rust_abi: 'c',
    dependencies: [
      dependency('libc-0.2-rs'),

    ],

  )

endif