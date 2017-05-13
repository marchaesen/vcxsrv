shared_module('vbe',
    [ 'vbe.c', 'vbeModes.c', 'vbe_module.c' ],
    include_directories: [ inc, xorg_inc ],
    dependencies: common_dep,
    c_args: xorg_c_args,
    install: true,
    install_dir: module_dir,
)

install_data(['vbe.h', 'vbeModes.h'], install_dir: xorgsdkdir)
