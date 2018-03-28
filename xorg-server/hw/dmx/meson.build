srcs = [
    'dmx.c',
    'dmxcb.c',
    'dmxcmap.c',
    'dmxcursor.c',
    'dmxdpms.c',
    'dmxextension.c',
    'dmxfont.c',
    'dmxgc.c',
    'dmxgcops.c',
    'dmxinit.c',
    'dmxinput.c',
    'dmxlog.c',
    'dmxpict.c',
    'dmxpixmap.c',
    'dmxprop.c',
    'dmxscrinit.c',
    'dmxstat.c',
    'dmxsync.c',
    'dmxvisual.c',
    'dmxwindow.c',
    '../../mi/miinitext.c',
    '../../Xext/panoramiX.c',
]

subdir('config')
subdir('input')
subdir('examples')
# XXX: subdir('doc')
# XXX: subdir('doxygen')

install_man(configure_file(
    input: 'man/Xdmx.man',
    output: 'Xdmx.1',
    configuration: manpage_config,
))

install_man(configure_file(
    input: 'config/man/dmxtodmx.man',
    output: 'dmxtodmx.1',
    configuration: manpage_config,
))

install_man(configure_file(
    input: 'config/man/vdltodmx.man',
    output: 'vdltodmx.1',
    configuration: manpage_config,
))

install_man(configure_file(
    input: 'config/man/xdmxconfig.man',
    output: 'xdmxconfig.1',
    configuration: manpage_config,
))

dmx_dep = [
    common_dep,
    dependency('dmxproto', version: '>= 2.2.99.1'),
    dependency('xext', version: '>= 1.0.99.4'),
    dependency('xau'),
    dependency('xmuu'),
    dependency('xfixes'),
    dependency('xrender'),
    dependency('xi', version: '>= 1.2.99.1'),
    xdmcp_dep,
    dl_dep,
]

dmx_glx = []
if build_glx
    srcs += 'dmx_glxvisuals.c'
    subdir('glxProxy')
endif

executable(
    'Xdmx',
    srcs,
    include_directories: inc,
    dependencies: dmx_dep,
    link_with: [
        libxserver_main,
        libxserver_fb,
        libxserver,
        libxserver_xkb_stubs,
        dmx_input,
        dmx_config,
        dmx_glx,
    ],
    c_args: '-DHAVE_DMX_CONFIG_H',
    install: true,
)
