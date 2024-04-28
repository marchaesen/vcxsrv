fc_sources = [
  'fcatomic.c',
  'fccache.c',
  'fccfg.c',
  'fccharset.c',
  'fccompat.c',
  'fcdbg.c',
  'fcdefault.c',
  'fcdir.c',
  'fcformat.c',
  'fcfreetype.c',
  'fcfs.c',
  'fcptrlist.c',
  'fchash.c',
  'fcinit.c',
  'fclang.c',
  'fclist.c',
  'fcmatch.c',
  'fcmatrix.c',
  'fcname.c',
  'fcobjs.c',
  'fcpat.c',
  'fcrange.c',
  'fcserialize.c',
  'fcstat.c',
  'fcstr.c',
  'fcweight.c',
  'fcxml.c',
  'ftglue.c',
]

fcobjshash_h = cc.preprocess('fcobjshash.gperf.h', include_directories: incbase)
fcobjshash_gperf = custom_target(
  input: fcobjshash_h,
  output: 'fcobjshash.gperf',
  command: ['cutout.py', '@INPUT@', '@OUTPUT@'],
  build_by_default: true,
)

fcobjshash_h = custom_target('fcobjshash.h',
  input: fcobjshash_gperf,
  output: 'fcobjshash.h',
  command: [gperf, '--pic', '-m', '100', '@INPUT@', '--output-file', '@OUTPUT@']
)

# Define FcPublic appropriately for exports on windows
fc_c_shared_args = []

if host_machine.system() == 'windows' and get_option('default_library') in ['both', 'shared']
  fc_c_shared_args += '-DFcPublic=__declspec(dllexport)'
  fc_c_shared_args += '-DDLL_EXPORT'
endif

libfontconfig = library('fontconfig',
  fc_sources, alias_headers, ft_alias_headers, fclang_h, fccase_h, fcobjshash_h,
  c_shared_args: fc_c_shared_args,
  include_directories: incbase,
  dependencies: [deps, math_dep],
  install: true,
  soversion: soversion,
  version: libversion,
  darwin_versions: osxversion,
)

fontconfig_dep = declare_dependency(link_with: libfontconfig,
  include_directories: incbase,
  dependencies: deps,
)

pkgmod.generate(libfontconfig,
  description: 'Font configuration and customization library',
  filebase: 'fontconfig',
  name: 'Fontconfig',
  requires: ['freetype2 ' + freetype_req],
  version: fc_version,
  variables: [
    'sysconfdir=@0@'.format(join_paths(prefix, get_option('sysconfdir'))),
    'localstatedir=@0@'.format(join_paths(prefix, get_option('localstatedir'))),
    'confdir=@0@'.format(fc_baseconfigdir),
    'cachedir=@0@'.format(fc_cachedir),
  ])
