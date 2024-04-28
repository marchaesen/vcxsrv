project('fontconfig', 'c',
  version: '2.15.0',
  meson_version : '>= 1.3.0',
  default_options: [ 'buildtype=debugoptimized'],
)

fs = import('fs')

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
math_dep = cc.find_library('m', required: false)

conf = configuration_data()

freetype_dep = dependency('freetype2', method: 'pkg-config', version: freetype_req, required: false)

# Give another shot using CMake
if not freetype_dep.found()
  freetype_dep = dependency('freetype', method: 'cmake', version: freetype_req_cmake,
    fallback: ['freetype2', 'freetype_dep'], default_options: 'werror=false')
endif

if freetype_dep.type_name() == 'internal'
  conf.set('FREETYPE_PCF_LONG_FAMILY_NAMES', false)
else
  if cc.compiles(files('meson-cc-tests/freetype-pcf-long-family-names.c'),
		         dependencies: freetype_dep)
    conf.set('FREETYPE_PCF_LONG_FAMILY_NAMES', true)
  else
    conf.set('FREETYPE_PCF_LONG_FAMILY_NAMES', false)
  endif
endif

jsonc_dep = dependency('json-c', required: false)

xml_dep = dependency('', required: false)
xmlbackend = get_option('xml-backend')
xmltype = ''
if xmlbackend == 'auto' or xmlbackend == 'expat'
  # Linking expat should not be so difficult... see: https://github.com/mesonbuild/meson/issues/10516
  xml_dep = dependency('expat', required: false)
  if not xml_dep.found()
    xml_dep = cc.find_library('expat', required : false)
    if not xml_dep.found() and xmlbackend == 'expat'
      xml_dep = dependency('expat', method: 'system', required: true, fallback: ['expat', 'expat_dep'])
    else
      xmltype = 'expat'
    endif
  endif
endif
if (xmlbackend == 'auto' and not xml_dep.found()) or xmlbackend == 'libxml2'
  xml_dep = dependency('libxml-2.0', required: true)
  conf.set('ENABLE_LIBXML2', 1)
endif
if xmltype == ''
  xmltype = xml_dep.name()
endif

pkgmod = import('pkgconfig')
python3 = import('python').find_installation()

check_headers = [
  ['dirent.h'],
  ['dlfcn.h'],
  ['fcntl.h'],
  ['inttypes.h'],
  ['stdint.h'],
  ['stdio.h'],
  ['stdlib.h'],
  ['strings.h'],
  ['string.h'],
  ['unistd.h'],
  ['sys/statvfs.h'],
  ['sys/vfs.h'],
  ['sys/statfs.h'],
  ['sys/stat.h'],
  ['sys/types.h'],
  ['sys/param.h'],
  ['sys/mount.h'],
  ['time.h'],
  ['wchar.h'],
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
  ['strerror'],
  ['strerror_r'],
  ['mmap'],
  ['vprintf'],
  ['vsnprintf'],
  ['vsprintf'],
  ['getpagesize'],
  ['getpid'],
  ['dcgettext'],
  ['gettext'],
  ['localtime_r'],
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
  ['struct statvfs', 'f_fstypename', ['sys/statvfs.h']],
  ['struct statfs', 'f_flags', ['sys/vfs.h', 'sys/statfs.h', 'sys/param.h', 'sys/mount.h']],
  ['struct statfs', 'f_fstypename', ['sys/vfs.h', 'sys/statfs.h', 'sys/param.h', 'sys/mount.h']],
  ['struct stat', 'st_mtim', ['sys/stat.h']],
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

deps = [freetype_dep, xml_dep]
incbase = include_directories('.')

# For compatibility to autoconf (regardless of the usage in fontconfig)
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_TARNAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(), meson.project_version()))
conf.set_quoted('PACKAGE_BUGREPORT', 'https://gitlab.freedesktop.org/fontconfig/fontconfig/issues/new')
conf.set_quoted('PACKAGE_URL', '')

if host_machine.system() == 'windows'
  conf.set('EXEEXT', '.exe')
else
  conf.set('EXEEXT', '')
endif

i18n = import('i18n')
gettext_args = [ '--msgid-bugs-address=@0@'.format(conf.get('PACKAGE_BUGREPORT')) ]

# Check for libintl.h
opt_nls = get_option('nls')
libintl_dep = dependency('intl', required: false, fallback: ['libintl', 'libintl_dep'])
if libintl_dep.found()
  conf.set('ENABLE_NLS', opt_nls.allowed().to_int())
  deps += [libintl_dep]
else
  opt_nls = opt_nls.require(false)
endif

# Check iconv support
iconv = get_option('iconv')
iconv_dep = dependency('', required: false)
found_iconv = 0
if iconv.allowed()
  iconv_dep = cc.find_library('iconv', required: false)
  if cc.has_header_symbol('iconv.h', 'libiconv_open', dependencies: libintl_dep)
    conf.set('LIBICONV_PLUG', 1)
    found_iconv = 1
  elif cc.has_header('iconv.h') and cc.has_function('iconv_open', dependencies: iconv_dep)
    found_iconv = 1
  else
    iconv.require(false)
  endif
endif
conf.set('USE_ICONV', found_iconv)
deps += [iconv_dep]

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
  conf.set('FLEXIBLE_ARRAY_MEMBER', '/**/')
else
  conf.set('FLEXIBLE_ARRAY_MEMBER', 1)
endif

if cc.compiles(files('meson-cc-tests/pthread-prio-inherit-test.c'))
  conf.set('HAVE_PTHREAD_PRIO_INHERIT', 1)
endif

if cc.links(files('meson-cc-tests/stdatomic-primitives-test.c'), name: 'stdatomic.h atomics')
  conf.set('HAVE_STDATOMIC_PRIMITIVES', 1)
endif

if cc.links(files('meson-cc-tests/intel-atomic-primitives-test.c'), name: 'Intel atomics')
  conf.set('HAVE_INTEL_ATOMIC_PRIMITIVES', 1)
endif

if cc.links(files('meson-cc-tests/solaris-atomic-operations.c'), name: 'Solaris atomic ops')
  conf.set('HAVE_SOLARIS_ATOMIC_OPS', 1)
endif


prefix = get_option('prefix')

fonts_conf = configuration_data()

default_fonts_dirs = get_option('default-fonts-dirs')
if default_fonts_dirs == ['yes']
  if host_machine.system() == 'windows'
    fc_fonts_paths = ['WINDOWSFONTDIR', 'WINDOWSUSERFONTDIR']
  elif host_machine.system() == 'darwin'
    fc_fonts_paths = ['/System/Library/Fonts', '/Library/Fonts', '~/Library/Fonts', '/System/Library/Assets/com_apple_MobileAsset_Font3', '/System/Library/Assets/com_apple_MobileAsset_Font4']
  else
    fc_fonts_paths = ['/usr/share/fonts', '/usr/local/share/fonts']
  endif
else
  fc_fonts_paths = default_fonts_dirs
endif
xml_path = ''
escaped_xml_path = ''
foreach p : fc_fonts_paths
  s = '\t<dir>' + p + '</dir>\n'
  xml_path += s
  # No substitution method for string
  s = '\\t<dir>' + p + '</dir>\\n'
  escaped_xml_path += s
endforeach
conf.set_quoted('FC_DEFAULT_FONTS', escaped_xml_path)
fonts_conf.set('FC_DEFAULT_FONTS', xml_path)

# Add more fonts if available.  By default, add only the directories
# with outline fonts; those with bitmaps can be added as desired in
# local.conf or ~/.fonts.conf
fc_add_fonts = []
additional_fonts_dirs = get_option('additional-fonts-dirs')
if additional_fonts_dirs == ['yes']
  fs = import('fs')
  foreach dir : ['/usr/X11R6/lib/X11', '/usr/X11/lib/X11', '/usr/lib/X11']
    if fs.is_dir(dir / 'fonts')
      fc_add_fonts += [dir / 'fonts']
    endif
  endforeach
elif additional_fonts_dirs == ['no']
  # nothing to do
else
  fc_add_fonts = additional_fonts_dirs
endif
xml_path = ''
escaped_xml_path = ''
foreach p : fc_add_fonts
  s = '\t<dir>' + p + '</dir>\n'
  xml_path += s
  # No substitution method for string
  s = '\\t<dir>' + p + '</dir>\\n'
  escaped_xml_path += s
endforeach
conf.set_quoted('FC_FONTPATH', escaped_xml_path)
fonts_conf.set('FC_FONTPATH', xml_path)

fc_cachedir = get_option('cache-dir')
if fc_cachedir in ['yes', 'no', 'default']
  if host_machine.system() == 'windows'
    fc_cachedir = 'LOCAL_APPDATA_FONTCONFIG_CACHE'
  else
    fc_cachedir = join_paths(prefix, get_option('localstatedir'), 'cache', meson.project_name())
  endif
endif

if host_machine.system() != 'windows'
  thread_dep = dependency('threads')
  conf.set('HAVE_PTHREAD', 1)
  deps += [thread_dep]
endif

fc_templatedir = get_option('template-dir')
if fc_templatedir in ['default', 'yes', 'no']
  fc_templatedir = prefix / get_option('datadir') / 'fontconfig/conf.avail'
endif

fc_baseconfigdir = get_option('baseconfig-dir')
if fc_baseconfigdir in ['default', 'yes', 'no']
  fc_baseconfigdir = prefix / get_option('sysconfdir') / 'fonts'
endif

fc_configdir = get_option('config-dir')
if fc_configdir in ['default', 'yes', 'no']
  fc_configdir = fc_baseconfigdir / 'conf.d'
endif

fc_xmldir = get_option('xml-dir')
if fc_xmldir in ['default', 'yes', 'no']
  fc_xmldir = prefix / get_option('datadir') / 'xml/fontconfig'
endif

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

gperf = find_program('gperf', required: false)
gperf_len_type = ''

if gperf.found()
  gperf_test_format = '''
  #include <string.h>
  const char * in_word_set(const char *, @0@);
  @1@
  '''
  gperf_snippet = run_command(gperf, '-L', 'ANSI-C', files('meson-cc-tests/gperf.txt'),
                              check: true).stdout()

  foreach type : ['size_t', 'unsigned']
    if cc.compiles(gperf_test_format.format(type, gperf_snippet))
      gperf_len_type = type
      break
    endif
  endforeach

  if gperf_len_type == ''
    error('unable to determine gperf len type')
  endif
else
  # Fallback to subproject
  gperf = find_program('gperf')
  # assume if we are compiling from the wrap, the size is just size_t
  gperf_len_type = 'size_t'
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

fcstdint_h = fs.copyfile('src/fcstdint.h.in', 'fcstdint.h')

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
if find_program('xgettext', required : opt_nls).found()
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
doc_targets = get_variable('doc_targets', [])

summary({
  'Documentation': (doc_targets.length() > 0 ? doc_targets : false),
  'NLS': not opt_nls.disabled(),
  'Tests': not get_option('tests').disabled(),
  'Tools': not get_option('tools').disabled(),
  'iconv': found_iconv == 1,
  'XML backend': xmltype,
}, section: 'General', bool_yn: true, list_sep: ', ')
summary({
  'Hinting': preferred_hinting,
  'Font directories': fc_fonts_paths,
  'Additional font directories': fc_add_fonts,
}, section: 'Defaults', bool_yn: true, list_sep: ', ')
summary({
  'Cache directory': fc_cachedir,
  'Template directory': fc_templatedir,
  'Base config directory': fc_baseconfigdir,
  'Config directory': fc_configdir,
  'XML directory': fc_xmldir,
}, section: 'Paths', bool_yn: true, list_sep: ', ')
