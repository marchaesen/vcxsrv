if get_option('nls')
  i18n = import('i18n')
  i18n.gettext('xkeyboard-config', preset: 'glib',
               args: ['--keyword=_',
                       '--keyword=N_',
                       '--its=@0@/rules/xkb.its'.format(meson.project_source_root())])
endif
