project('libintl', 'c')

libintl_cc = meson.get_compiler('c')
libintl_inc = include_directories('/usr/local/include')

if libintl_cc.has_header('libintl.h', include_directories: libintl_inc)
  dep = libintl_cc.find_library('intl', dirs: ['/usr/local/lib'], required: false)
  if dep.found()
    libintl_dep = declare_dependency(dependencies: dep, include_directories: libintl_inc)
  else
    libintl_dep = dependency('', required: false)
  endif
else
  libintl_dep = dependency('', required: false)
endif
