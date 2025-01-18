deps_gbm_dri = []

# TODO: fix includes to delete this
incs_gbm_dri = [inc_mesa]

if with_platform_wayland
  deps_gbm_dri += dep_wayland_server
  incs_gbm_dri += inc_wayland_drm
endif

libgbm_dri = shared_library(
  'dri_gbm',
  files('gbm_dri.c', 'gbm_driint.h'),
  include_directories : [incs_gbm, incs_gbm_dri, inc_st_dri, inc_gallium_aux],
  link_args : [ld_args_gc_sections],
  link_with : [libloader, libgallium_dri],
  dependencies : [deps_gbm_dri, dep_dl, dep_libdrm, idep_mesautil, idep_xmlconfig],
  gnu_symbol_visibility : 'hidden',
  install : true,
  install_dir: join_paths(get_option('libdir'), 'gbm'),
  name_prefix : '',
)

devenv.set('GBM_BACKENDS_PATH', meson.current_build_dir())
