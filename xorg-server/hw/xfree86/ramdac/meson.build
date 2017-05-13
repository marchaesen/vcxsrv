srcs_xorg_ramdac = [
    'xf86RamDac.c',
    'xf86RamDacCmap.c',
    'xf86CursorRD.c',
    'xf86HWCurs.c',
    'IBM.c',
    'BT.c',
    'TI.c',
]

xorg_ramdac = static_library('xorg_ramdac',
    srcs_xorg_ramdac,
    include_directories: [inc, xorg_inc],
    dependencies: common_dep,
    c_args: xorg_c_args,
)

install_data(
    [
        'BT.h',
        'IBM.h',
        'TI.h',
        'xf86Cursor.h',
        'xf86RamDac.h',
    ],
    install_dir: xorgsdkdir,
)
