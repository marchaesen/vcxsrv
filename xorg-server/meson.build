project('xserver', 'c',
        default_options: ['buildtype=debugoptimized'],
        version: '1.19.99.1',
        meson_version: '>= 0.40.0',
)
add_project_arguments('-DHAVE_DIX_CONFIG_H', language: 'c')
cc = meson.get_compiler('c')

add_global_arguments('-fno-strict-aliasing', language : 'c')
add_global_arguments('-fvisibility=hidden', language : 'c')

add_global_link_arguments('-fvisibility=hidden', language : 'c')

# Quiet some EXA warnings.
add_global_arguments('-Wno-discarded-qualifiers', language : 'c')

xproto_dep = dependency('xproto', version: '>= 7.0.31')
randrproto_dep = dependency('randrproto', version: '>= 1.5.0')
renderproto_dep = dependency('renderproto', version: '>= 0.11')
xextproto_dep = dependency('xextproto', version: '>= 7.2.99.901')
inputproto_dep = dependency('inputproto', version: '>= 2.3')
kbproto_dep = dependency('kbproto', version: '>= 1.0.3')
fontsproto_dep = dependency('fontsproto', version: '>= 2.1.3')
fixesproto_dep = dependency('fixesproto', version: '>= 5.0')
damageproto_dep = dependency('damageproto', version: '>= 1.1')
xcmiscproto_dep = dependency('xcmiscproto', version: '>= 1.2.0')
bigreqsproto_dep = dependency('bigreqsproto', version: '>= 1.1.0')
xtrans_dep = dependency('xtrans', version: '>= 1.3.5')

videoproto_dep = dependency('videoproto')
compositeproto_dep = dependency('compositeproto', version: '>= 0.4')
recordproto_dep = dependency('recordproto', version: '>= 1.13.99.1')
scrnsaverproto_dep = dependency('scrnsaverproto', version: '>= 1.1')
resourceproto_dep = dependency('resourceproto', version: '>= 1.2.0')
xf86driproto_dep = dependency('xf86driproto', version: '>= 2.1.0', required: get_option('dri1') == 'yes')
dri2proto_dep = dependency('dri2proto', version: '>= 2.8', required: get_option('dri2') == 'yes')
dri3proto_dep = dependency('dri3proto', version: '>= 1.0', required: get_option('dri3') == 'yes')
xineramaproto_dep = dependency('xineramaproto')
xf86bigfontproto_dep = dependency('xf86bigfontproto', version: '>= 1.2.0')
xf86dgaproto_dep = dependency('xf86dgaproto', version: '>= 2.0.99.1', required: false)
xf86vidmodeproto_dep = dependency('xf86vidmodeproto', version: '>= 2.2.99.1', required: false)
windowswmproto_dep = dependency('windowswmproto', required: false)
applewmproto_dep = dependency('applewmproto', version: '>= 1.4', required: false)
xshmfence_dep = dependency('xshmfence', version: '>= 1.1', required: false)

pixman_dep = dependency('pixman-1')
libbsd_dep = dependency('libbsd', required: false)
xkbfile_dep = dependency('xkbfile')
xfont2_dep = dependency('xfont2', version: '>= 2.0')
nettle_dep = dependency('nettle')

dbus_required = get_option('systemd_logind') == 'yes'
dbus_dep = dependency('dbus-1', version: '>= 1.0', required: dbus_required)

# Resolve default values of some options
xkb_dir = get_option('xkb_dir')
if xkb_dir == ''
    xkb_dir = join_paths(get_option('prefix'), 'share/X11/xkb')
endif

xkb_output_dir = get_option('xkb_output_dir')
if xkb_output_dir == ''
    xkb_output_dir = join_paths(get_option('prefix'), 'share/X11/xkb/compiled')
endif

hal_option = get_option('hal')
glamor_option = get_option('glamor')

build_udev = get_option('udev')
if host_machine.system() == 'windows'
    build_udev = false
    hal_option = 'no'
endif

if get_option('systemd_logind') == 'auto'
    build_systemd_logind = build_udev and dbus_dep.found()
else
    build_systemd_logind = get_option('systemd_logind') == 'yes'
endif

build_xorg = false
if (host_machine.system() != 'darwin' and
    host_machine.system() != 'windows')
    if get_option('xorg') == 'auto'
        build_xorg = (host_machine.system() != 'darwin' and
                      host_machine.system() != 'windows')
    else
        build_xorg = get_option('xorg') == 'yes'
    endif
endif
xorgsdkdir = join_paths(get_option('prefix'), get_option('includedir'), 'xorg')

build_xwayland = false
if (host_machine.system() != 'darwin' and
    host_machine.system() != 'windows')
    if get_option('xwayland') != 'no'
        xwayland_required = get_option('xwayland') == 'yes'

        xwayland_dep = [
            dependency('wayland-client', version: '>= 1.3.0', required: xwayland_required),
            dependency('wayland-protocols', version: '>= 1.9', required: xwayland_required),
            dependency('libdrm', version: '>= 2.3.1', required: xwayland_required),
            dependency('epoxy', required: xwayland_required),
        ]

        build_xwayland = true
        # check for all the deps being found, to handle 'auto' mode.
        foreach d: xwayland_dep
            if not d.found()
                build_xwayland = false
            endif
        endforeach
    endif
endif

build_xnest = false
if (host_machine.system() != 'darwin' and
    host_machine.system() != 'windows')
    if get_option('xnest') != 'no'
        xnest_required = get_option('xnest') == 'yes'

        xnest_dep = [
            dependency('xext', version: '>= 1.0.99.4', required: xnest_required),
            dependency('x11', required: xnest_required),
            dependency('xau', required: xnest_required),
        ]

        build_xnest = true
        # check for all the deps being found, to handle 'auto' mode.
        foreach d: xnest_dep
            if not d.found()
                build_xnest = false
            endif
        endforeach
    endif
endif

build_xwin = false
if get_option('xwin') == 'auto'
    if (host_machine.system() == 'cygwin' or
        host_machine.system() == 'windows')
            build_xwin = true
    endif
else
    build_xwin = get_option('xwin') == 'yes'
endif

# XXX: Finish these.
build_xquartz = false

if get_option('ipv6') == 'auto'
    build_ipv6 = cc.has_function('getaddrinfo')
else
    build_ipv6 = get_option('ipv6') == 'yes'
endif

int10 = get_option('int10')
if int10 == 'auto'
    int10 = 'x86emu'
    if host_machine.cpu() == 'powerpc' and host_machine.system() == 'freebsd'
        int10 = 'stub'
    endif
    if host_machine.cpu() == 'arm'
        int10 = 'stub'
    endif
endif

hal_dep = ''
if hal_option == 'auto'
    if not build_udev
        hal_dep = dependency('hal', required: false)
        build_hal = hal_dep.found()
    else
        build_hal = false
    endif
else
    build_hal = hal_option == 'yes'
    if build_hal
        hal_dep = dependency('hal')
    endif
endif

if build_udev and build_hal
    error('Hotplugging through both libudev and hal not allowed')
endif

build_dbus = build_hal or build_systemd_logind

udev_dep = dependency('', required:false)
if build_udev
    udev_dep = dependency('libudev', version: '>= 143')
endif

log_dir = get_option('log_dir')
if log_dir == ''
    log_dir = join_paths(get_option('prefix'), get_option('localstatedir'), 'log')
endif

module_dir = get_option('module_dir')
if module_dir == ''
    module_dir = join_paths(get_option('libdir'), 'xorg/modules')
endif

if glamor_option == 'auto'
    build_glamor = build_xorg or build_xwayland
else
    build_glamor = get_option('glamor') == 'yes'
endif

gbm_dep = dependency('', required:false)
if build_glamor
    gbm_dep = dependency('gbm', version: '>= 10.2', required: false)
    if gbm_dep.found()
        gbm_linear_dep = dependency('gbm', version: '>= 10.6', required: false)
    endif
endif

# XXX: Add more sha1 options, because Linux is about choice
sha1_dep = nettle_dep

xdmcp_dep = ''
if get_option('xdmcp')
    xdmcp_dep = dependency('xdmcp')
endif

build_glx = get_option('glx')

if get_option('dri1') == 'auto'
    build_dri1 = xf86driproto_dep.found()
else
    build_dri1 = get_option('dri1') == 'yes'
endif

if get_option('dri2') == 'auto'
    build_dri2 = dri2proto_dep.found()
else
    build_dri2 = get_option('dri2') == 'yes'
endif

if get_option('dri3') == 'auto'
    build_dri3 = dri3proto_dep.found() and xshmfence_dep.found()
else
    build_dri3 = get_option('dri3') == 'yes'
    if build_dri3
       if not xshmfence_dep.found()
           error('DRI3 requested, but xshmfence not found')
       endif
    endif
endif

libdrm_required = (get_option('dri1') == 'yes') or (get_option('dri2') == 'yes') or (get_option('dri3') == 'yes')
libdrm_dep = dependency('libdrm', version: '>= 2.3.1', required: libdrm_required)
build_modesetting = false
if libdrm_dep.found()
  if build_dri2
    # 2.4.65 is required for drmGetDevice
    dependency('libdrm', version: '>= 2.4.65')
  endif
  # 2.4.46 is required for cursor hotspot support.
  build_modesetting = libdrm_dep.version().version_compare('>= 2.4.46')
endif

build_vbe = false
if get_option('vbe') == 'auto'
    if (host_machine.system() != 'darwin' and
        host_machine.system() != 'windows' and
        host_machine.system() != 'cygwin')
        build_vbe = true
    endif
else
    build_vbe = get_option('vbe') == 'yes'
endif

build_vgahw = false
if get_option('vgahw') == 'auto'
    if (host_machine.system() != 'darwin' and
        host_machine.system() != 'windows' and
        host_machine.system() != 'cygwin')
        build_vgahw = true
    endif
else
    build_vgahw = get_option('vgahw') == 'yes'
endif

# XXX: Allow configuration of these.
build_apm = false
build_acpi = false
build_dpms = true
build_mitshm = true
build_res = true
build_screensaver = true
build_xace = true
build_xsecurity = true
build_xf86bigfont = true
build_xinerama = true
build_xselinux = false
build_xv = true
build_dga = false
build_xf86vidmode = xf86vidmodeproto_dep.found()

m_dep = cc.find_library('m', required : false)
dl_dep = cc.find_library('dl', required : false)

common_dep = [
    xproto_dep,
    randrproto_dep,
    renderproto_dep,
    xextproto_dep,
    inputproto_dep,
    kbproto_dep,
    fontsproto_dep,
    fixesproto_dep,
    damageproto_dep,
    xcmiscproto_dep,
    bigreqsproto_dep,
    xtrans_dep,

    videoproto_dep,
    compositeproto_dep,
    recordproto_dep,
    scrnsaverproto_dep,
    resourceproto_dep,
    xf86driproto_dep,
    dri2proto_dep,
    dri3proto_dep,
    xineramaproto_dep,
    xf86bigfontproto_dep,
    xf86dgaproto_dep,
    xf86vidmodeproto_dep,
    windowswmproto_dep,
    applewmproto_dep,

    pixman_dep,
    libbsd_dep,
    xkbfile_dep,
    xfont2_dep,
    xdmcp_dep,
]

inc = include_directories(
    'Xext',
    'Xi',
    'composite',
    'damageext',
    'exa',
    'fb',
    'glamor',
    'mi',
    'miext/damage',
    'miext/shadow',
    'miext/sync',
    'dbe',
    'dri3',
    'include',
    'present',
    'randr',
    'render',
    'xfixes',
)

glx_inc = include_directories('glx')

top_srcdir_inc = include_directories('.')

serverconfigdir = join_paths(get_option('libdir'), '/xorg')

# Include must come first, as it sets up dix-config.h
subdir('include')

# X server core
subdir('config')
subdir('dix')
subdir('dri3')
subdir('glx')
subdir('fb')
subdir('mi')
subdir('os')
# X extensions
subdir('composite')
subdir('damageext')
subdir('dbe')
subdir('miext/damage')
subdir('miext/shadow')
subdir('miext/sync')
subdir('present')
if build_xwin or build_xquartz
    subdir('pseudoramiX')
endif
subdir('randr')
subdir('record')
subdir('render')
subdir('xfixes')
subdir('xkb')
subdir('Xext')
subdir('Xi')
# other
if build_glamor
    subdir('glamor')
endif
if build_xorg or get_option('xephyr')
    subdir('exa')
endif

# Common static libraries of all X servers
libxserver = [
    libxserver_mi,
    libxserver_dix,

    libxserver_composite,
    libxserver_damageext,
    libxserver_dbe,
    libxserver_randr,
    libxserver_miext_damage,
    libxserver_render,
    libxserver_present,
    libxserver_xext,
    libxserver_miext_sync,
    libxserver_xfixes,
    libxserver_xi,
    libxserver_xkb,
    libxserver_record,

    libxserver_os,
]

libxserver += libxserver_dri3

subdir('hw')
