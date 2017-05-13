xorg_inc = include_directories(
    'common',
    'ddc',
    'dri2',
    'i2c',
    'int10',
    'loader',
    'modes',
    'os-support',
    'os-support/bus',
    'parser',
    'ramdac',
    'vbe',
    'vgahw',
)

xorg_c_args = []
xorg_c_args += '-DHAVE_XORG_CONFIG_H'
xorg_c_args += '-DXORG_NO_SDKSYMS'

pciaccess_dep = []
if get_option('pciaccess')
    pciaccess_dep = dependency('pciaccess', version: '>= 0.12.901')
endif

subdir('common')
subdir('ddc')
subdir('dixmods')
subdir('exa')
subdir('fbdevhw')
if gbm_dep.found()
    subdir('glamor_egl')
endif
subdir('i2c')
if build_dri1
    subdir('dri')
endif
if build_dri2
    subdir('dri2')
endif

if int10 != 'disabled'
    if int10 == 'x86emu'
        subdir('x86emu')
    endif
    subdir('int10')
endif
subdir('loader')
subdir('modes')
subdir('os-support')
subdir('parser')
subdir('ramdac')
subdir('shadowfb')
if build_vbe
    subdir('vbe')
endif
if build_vgahw
    subdir('vgahw')
endif
if build_modesetting
   subdir('drivers/modesetting')
endif

srcs_xorg = [
    '../../mi/miinitext.c'
]

# Extract all the objects so that all symbols get brought into the
# server.  This prevents us from needing a global table of all symbols
# that should be exported to Xorg modules, at the expense of all
# symbols being included and public

xorg_link = [
    libxserver,
    xorg_common,
    xorg_loader,
    xorg_ddc,
    xorg_dixmods,
    xorg_i2c,
    xorg_modes,
    xorg_os_support,
    xorg_parser,
    xorg_ramdac,
    libxserver_xext_vidmode,
    libxserver_main,
    libxserver_config,
]
if build_dri1
    xorg_link += xorg_dri
endif
if build_dri2
    xorg_link += xorg_dri2
endif

executable(
    'Xorg',
    srcs_xorg,
    include_directories: [inc, xorg_inc],
    link_whole: xorg_link,
    dependencies: [
        pixman_dep,
        m_dep,
        dl_dep,
        pciaccess_dep,
        sha1_dep,
        dependency('xau'),
        xdmcp_dep,
        xfont2_dep,
        xshmfence_dep,
        config_dep,
        libdrm_dep,
    ],
    link_args: '-Wl,--export-dynamic',
    c_args: xorg_c_args,
    install: true,
)
