fcvalidate = executable('fc-validate', ['fc-validate.c', fcstdint_h, alias_headers, ft_alias_headers],
  include_directories: [incbase, incsrc],
  dependencies: [freetype_dep, libintl_dep],
  link_with: [libfontconfig],
  c_args: c_args,
  install: true,
)

tools_man_pages += ['fc-validate']
