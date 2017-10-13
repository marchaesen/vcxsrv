xorg_exa = shared_module('exa',
    'examodule.c',
    include_directories: [inc, xorg_inc],
    dependencies: common_dep,
    link_with: [libxserver_exa, e],
    c_args: xorg_c_args,
    install: true,
    install_dir: module_dir,
)
