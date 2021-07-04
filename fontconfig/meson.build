project('fontconfig', 'c',
  version: '2.13.94',
  meson_version : '>= 0.56.0',
  default_options: [ 'buildtype=debugoptimized'],
)

fc_version = meson.project_version()
version_arr = fc_version.split('.')
fc_version_major = version_arr[0].to_int()
fc_version_minor = version_arr[1].to_int()
fc_version_micro = version_arr[2].to_int()

# Try and maintain compatibility with the previous libtool versioning
# (this is a bit of a hack, but it should work fine for our case where
# API is added, in which case LT_AGE and LIBT_CURRENT are both increased)
soversion = fc_version_major - 1
curversion = fc_version_minor - 1
libversion = '@0@.@1@.0'.format(soversion, curversion)
defversion = '@0@.@1@'.format(curversion, fc_version_micro)
osxversion = curversion + 1

freetype_req = '>= 21.0.15'
freetype_req_cmake = '>= 2.8.1'

cc = meson.get_compiler('c')


freetype_dep = dependency('freetype2', method: 'pkg-config', version: freetype_req, required: false)

# Give another shot using CMake
if not freetype_dep.found()
  freetype_dep = dependency('freetype', method: 'cmake', version: freetype_req_cmake,
    fallback: ['freetype2', 'freetype_dep'])
endif

expat_dep = dependency('expat',
  fallback: ['expat', 'expat_dep'])

i18n = import('i18n')
pkgmod = import('pkgconfig')
python3 = import('python').find_installation()

check_headers = [
  ['dirent.h'],
  ['fcntl.h'],
  ['stdlib.h'],
  ['string.h'],
  ['unistd.h'],
  ['sys/statvfs.h'],
  ['sys/vfs.h'],
  ['sys/statfs.h'],
  ['sys/param.h'],
  ['sys/mount.h'],
]

check_funcs = [
  ['link'],
  ['mkstemp'],
  ['mkostemp'],
  ['_mktemp_s'],
  ['mkdtemp'],
  ['getopt'],
  ['getopt_long'],
  ['getprogname'],
  ['getexecname'],
  ['rand'],
  ['random'],
  ['lrand48'],
  ['random_r'],
  ['rand_r'],
  ['readlink'],
  ['fstatvfs'],
  ['fstatfs'],
  ['lstat'],
  ['mmap'],
  ['vprintf'],
]

check_freetype_funcs = [
  ['FT_Get_BDF_Property', {'dependencies': freetype_dep}],
  ['FT_Get_PS_Font_Info', {'dependencies': freetype_dep}],
  ['FT_Has_PS_Glyph_Names', {'dependencies': freetype_dep}],
  ['FT_Get_X11_Font_Format', {'dependencies': freetype_dep}],
  ['FT_Done_MM_Var', {'dependencies': freetype_dep}],
]

check_header_symbols = [
  ['posix_fadvise', 'fcntl.h']
]

check_struct_members = [
  ['struct statvfs', 'f_basetype', ['sys/statvfs.h']],
  ['struct statvfs', 'f_fstypename', ['sys/statvfs.']],
  ['struct statfs', 'f_flags', []],
  ['struct statfs', 'f_fstypename', []],
  ['struct dirent', 'd_type', ['sys/types.h', 'dirent.h']],
]

check_sizeofs = [
  ['void *', {'conf-name': 'SIZEOF_VOID_P'}],
]

check_alignofs = [
  ['void *', {'conf-name': 'ALIGNOF_VOID_P'}],
  ['double'],
]

add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

c_args = []

conf = configuration_data()
deps = [freetype_dep, expat_dep]
incbase = include_directories('.')

# We cannot try compiling against an internal dependency
if freetype_dep.type_name() == 'internal'
  foreach func: check_freetype_funcs
    name = func[0]
    conf.set('HAVE_@0@'.format(name.to_upper()), 1)
  endforeach
else
  check_funcs += check_freetype_funcs
endif

foreach check : check_headers
  name = check[0]

  if cc.has_header(name)
    conf.set('HAVE_@0@'.format(name.to_upper().underscorify()), 1)
  endif
endforeach

foreach check : check_funcs
  name = check[0]
  opts = check.length() > 1 ? check[1] : {}
  extra_deps = opts.get('dependencies', [])

  if cc.has_function(name, dependencies: extra_deps)
    conf.set('HAVE_@0@'.format(name.to_upper()), 1)
  endif
endforeach

foreach check : check_header_symbols
  name = check[0]
  header = check[1]

  if cc.has_header_symbol(header, name)
    conf.set('HAVE_@0@'.format(name.to_upper()), 1)
  endif
endforeach

foreach check : check_struct_members
  struct_name = check[0]
  member_name = check[1]
  headers = check[2]

  prefix = ''

  foreach header : headers
    prefix += '#include <@0@>\n'.format(header)
  endforeach

  if cc.has_member(struct_name, member_name, prefix: prefix)
    conf.set('HAVE_@0@_@1@'.format(struct_name, member_name).to_upper().underscorify(), 1)
  endif
endforeach

foreach check : check_sizeofs
  type = check[0]
  opts = check.length() > 1 ? check[1] : {}

  conf_name = opts.get('conf-name', 'SIZEOF_@0@'.format(type.to_upper()))

  conf.set(conf_name, cc.sizeof(type))
endforeach

foreach check : check_alignofs
  type = check[0]
  opts = check.length() > 1 ? check[1] : {}

  conf_name = opts.get('conf-name', 'ALIGNOF_@0@'.format(type.to_upper()))

  conf.set(conf_name, cc.alignment(type))
endforeach

if cc.compiles(files('meson-cc-tests/flexible-array-member-test.c'))
  conf.set('FLEXIBLE_ARRAY_MEMBER', true)
else
  conf.set('FLEXIBLE_ARRAY_MEMBER', 1)
endif

if cc.links(files('meson-cc-tests/intel-atomic-primitives-test.c'), name: 'Intel atomics')
  conf.set('HAVE_INTEL_ATOMIC_PRIMITIVES', 1)
endif

if cc.links(files('meson-cc-tests/solaris-atomic-operations.c'), name: 'Solaris atomic ops')
  conf.set('HAVE_SOLARIS_ATOMIC_OPS', 1)
endif


prefix = get_option('prefix')

fonts_conf = configuration_data()

if host_machine.system() == 'windows'
  fc_fonts_path = ['WINDOWSFONTDIR']
  fc_cachedir = 'LOCAL_APPDATA_FONTCONFIG_CACHE'
else
  if host_machine.system() == 'darwin'
    fc_fonts_path = ['/System/Library/Fonts', '/Library/Fonts', '~/Library/Fonts', '/System/Library/Assets/com_apple_MobileAsset_Font3', '/System/Library/Assets/com_apple_MobileAsset_Font4']
  else
    fc_fonts_path = ['/usr/share/fonts', '/usr/local/share/fonts']
  endif
  fc_cachedir = join_paths(prefix, get_option('localstatedir'), 'cache', meson.project_name())
  thread_dep = dependency('threads')
  conf.set('HAVE_PTHREAD', 1)
  deps += [thread_dep]
endif
xml_path = ''
escaped_xml_path = ''
foreach p : fc_fonts_path
  s = '\t<dir>' + p + '</dir>\n'
  xml_path += s
  # No substitution method for string
  s = '\\t<dir>' + p + '</dir>\\n'
  escaped_xml_path += s
endforeach
conf.set_quoted('FC_DEFAULT_FONTS', escaped_xml_path)
fonts_conf.set('FC_DEFAULT_FONTS', xml_path)

fc_templatedir = join_paths(prefix, get_option('datadir'), 'fontconfig/conf.avail')
fc_baseconfigdir = join_paths(prefix, get_option('sysconfdir'), 'fonts')
fc_configdir = join_paths(fc_baseconfigdir, 'conf.d')
fc_xmldir = join_paths(prefix, get_option('datadir'), 'xml/fontconfig')


conf.set_quoted('CONFIGDIR', fc_configdir)
conf.set_quoted('FC_CACHEDIR', fc_cachedir)
conf.set_quoted('FC_TEMPLATEDIR', fc_templatedir)
conf.set_quoted('FONTCONFIG_PATH', fc_baseconfigdir)
conf.set_quoted('FC_FONTPATH', '')

fonts_conf.set('FC_FONTPATH', '')
fonts_conf.set('FC_CACHEDIR', fc_cachedir)
fonts_conf.set('CONFIGDIR', fc_configdir)
# strip off fc_baseconfigdir prefix if that is the prefix
if fc_configdir.startswith(fc_baseconfigdir + '/')
  fonts_conf.set('CONFIGDIR', fc_configdir.split(fc_baseconfigdir + '/')[1])
endif

# It will automatically fallback to subproject if not found on system
gperf = find_program('gperf')

sh = find_program('sh', required : false)

if not sh.found() # host_machine.system() == 'windows' or not sh.found()
  # TODO: This is not always correct
  if cc.get_id() == 'msvc'
    gperf_len_type = 'size_t'
  else
    gperf_len_type = 'unsigned'
  endif
else
  gperf_test_format = '''
  #include <string.h>
  const char * in_word_set(const char *, @0@);
  @1@
  '''
  gperf_snippet_format = 'echo foo,bar | @0@ -L ANSI-C'
  gperf_snippet = run_command(sh, '-c', gperf_snippet_format.format(gperf.path()))
  gperf_test = gperf_test_format.format('size_t', gperf_snippet.stdout())

  if cc.compiles(gperf_test)
    gperf_len_type = 'size_t'
  else
    gperf_test = gperf_test_format.format('unsigned', gperf_snippet.stdout())
    if cc.compiles(gperf_test)
      gperf_len_type = 'unsigned'
    else
      error('unable to determine gperf len type')
    endif
  endif
endif

message('gperf len type is @0@'.format(gperf_len_type))

conf.set('FC_GPERF_SIZE_T', gperf_len_type,
  description : 'The type of gperf "len" parameter')

conf.set('_GNU_SOURCE', true)

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())

incsrc = include_directories('src')

# We assume stdint.h is available
foreach t : ['uint64_t', 'int32_t', 'uintptr_t', 'intptr_t']
  if not cc.has_type(t, prefix: '#include <stdint.h>')
    error('Sanity check failed: type @0@ not provided via stdint.h'.format(t))
  endif
endforeach

fcstdint_h = configure_file(
  input: 'src/fcstdint.h.in',
  output: 'fcstdint.h',
  copy: true)

makealias = files('src/makealias.py')[0]

alias_headers = custom_target('alias_headers',
  output: ['fcalias.h', 'fcaliastail.h'],
  input: ['fontconfig/fontconfig.h', 'src/fcdeprecate.h', 'fontconfig/fcprivate.h'],
  command: [python3, makealias, join_paths(meson.current_source_dir(), 'src'), '@OUTPUT@', '@INPUT@'],
)

ft_alias_headers = custom_target('ft_alias_headers',
  output: ['fcftalias.h', 'fcftaliastail.h'],
  input: ['fontconfig/fcfreetype.h'],
  command: [python3, makealias, join_paths(meson.current_source_dir(), 'src'), '@OUTPUT@', '@INPUT@']
)

tools_man_pages = []

# Do not reorder
subdir('fc-case')
subdir('fc-lang')
subdir('src')

if not get_option('tools').disabled()
  subdir('fc-cache')
  subdir('fc-cat')
  subdir('fc-conflist')
  subdir('fc-list')
  subdir('fc-match')
  subdir('fc-pattern')
  subdir('fc-query')
  subdir('fc-scan')
  subdir('fc-validate')
endif

if not get_option('tests').disabled()
  subdir('test')
endif

subdir('conf.d')
subdir('its')

# xgettext is optional (on Windows for instance)
if find_program('xgettext', required : get_option('nls')).found()
  subdir('po')
  subdir('po-conf')
endif

if not get_option('doc').disabled()
  subdir('doc')
endif

configure_file(output: 'config.h', configuration: conf)

configure_file(output: 'fonts.conf',
  input: 'fonts.conf.in',
  configuration: fonts_conf,
  install_dir: fc_baseconfigdir,
  install: true)

install_data('fonts.dtd',
  install_dir: join_paths(get_option('prefix'), get_option('datadir'), 'xml/fontconfig')
)

fc_headers = [
  'fontconfig/fontconfig.h',
  'fontconfig/fcfreetype.h',
  'fontconfig/fcprivate.h',
]

install_headers(fc_headers, subdir: meson.project_name())

# Summary
if meson.version().version_compare('>= 0.53')
  doc_targets = get_variable('doc_targets', [])

  summary({
         'Documentation': (doc_targets.length() > 0 ? doc_targets : false),
         'NLS': not get_option('nls').disabled(),
         'Tests': not get_option('tests').disabled(),
         'Tools': not get_option('tools').disabled(),
         }, section: 'General', bool_yn: true, list_sep: ', ')
endif
