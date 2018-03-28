srcs_xorg_common = [
    'xf86fbBus.c',
    'xf86noBus.c',
    'xf86Configure.c',
    'xf86Bus.c',
    'xf86Config.c',
    'xf86Cursor.c',
    'xf86DPMS.c',
    'xf86Events.c',
    'xf86Globals.c',
    'xf86AutoConfig.c',
    'xf86Option.c',
    'xf86Init.c',
    'xf86VidMode.c',
    'xf86fbman.c',
    'xf86cmap.c',
    'xf86Helper.c',
    'xf86PM.c',
    'xf86RandR.c',
    'xf86Xinput.c',
    'xisb.c',
    'xf86Mode.c',
    'xorgHelper.c',
    'xf86Extensions.c',
]

xorg_sdk_headers = [
    'compiler.h',
    'fourcc.h',
    'xf86.h',
    'xf86Module.h',
    'xf86Opt.h',
    'xf86PciInfo.h',
    'xf86Priv.h',
    'xf86Privstr.h',
    'xf86cmap.h',
    'xf86fbman.h',
    'xf86str.h',
    'xf86Xinput.h',
    'xisb.h',
    'xorgVersion.h',
    'xf86sbusBus.h',
    'xf86VGAarbiter.h',
    'xf86Optionstr.h',
    'xf86platformBus.h',
    'xf86MatchDrivers.h',
    'xaarop.h',
]

if build_dga
    srcs_xorg_common += 'xf86DGA.c'
    xorg_sdk_headers += 'dgaproc.h'
endif

if build_xv
    srcs_xorg_common += ['xf86xv.c', 'xf86xvmc.c']
    xorg_sdk_headers += ['xf86xv.h', 'xf86xvmc.h', 'xf86xvpriv.h']
endif

if build_udev
    srcs_xorg_common += 'xf86platformBus.c'
endif

if get_option('pciaccess')
    srcs_xorg_common += ['xf86pciBus.c', 'xf86VGAarbiter.c']
endif

srcs_xorg_common += configure_file(
    output: 'xf86Build.h',
    command: ['sh', join_paths(meson.current_source_dir(), 'xf86Build.sh'), '@OUTPUT@'],
)

srcs_xorg_common += custom_target(
    'xf86DefModeSet.c',
    output: 'xf86DefModeSet.c',
    input: ['modeline2c.awk', 'vesamodes', 'extramodes'],
    command: [find_program('awk'), '-f', '@INPUT0@', '@INPUT1@', '@INPUT2@'],
    capture: true,
)

xorg_common = static_library('xorg_common',
    srcs_xorg_common,
    include_directories: [inc, xorg_inc],
    dependencies: [
        common_dep,
        dbus_dep,
        pciaccess_dep,
        libdrm_dep,
    ],
    c_args: xorg_c_args,
)

install_data(xorg_sdk_headers, install_dir: xorgsdkdir)
