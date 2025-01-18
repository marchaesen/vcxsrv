srcs = [
    'xwayland.c',
    'xwayland-input.c',
    'xwayland-input.h',
    'xwayland-cursor.c',
    'xwayland-cursor.h',
    'xwayland-drm-lease.h',
    'xwayland-drm-lease.c',
    'xwayland-glamor.h',
    'xwayland-pixmap.c',
    'xwayland-pixmap.h',
    'xwayland-present.c',
    'xwayland-present.h',
    'xwayland-screen.c',
    'xwayland-screen.h',
    'xwayland-shm.c',
    'xwayland-shm.h',
    'xwayland-types.h',
    'xwayland-output.c',
    'xwayland-output.h',
    'xwayland-cvt.c',
    'xwayland-cvt.h',
    'xwayland-vidmode.c',
    'xwayland-vidmode.h',
    'xwayland-window.c',
    'xwayland-window.h',
    'xwayland-window-buffers.c',
    'xwayland-window-buffers.h',
    '../../mi/miinitext.c',
    '../../mi/miinitext.h',
]

scanner_dep = dependency('wayland-scanner', native: true)
scanner = find_program(scanner_dep.get_variable(pkgconfig : 'wayland_scanner'))

protocols_dep = dependency('wayland-protocols', version: wayland_protocols_req)
protodir = protocols_dep.get_variable(pkgconfig : 'pkgdatadir')

pointer_xml = join_paths(protodir, 'unstable', 'pointer-constraints', 'pointer-constraints-unstable-v1.xml')
relative_xml = join_paths(protodir, 'unstable', 'relative-pointer', 'relative-pointer-unstable-v1.xml')
gestures_xml = join_paths(protodir, 'unstable', 'pointer-gestures', 'pointer-gestures-unstable-v1.xml')
tablet_xml = join_paths(protodir, 'unstable', 'tablet', 'tablet-unstable-v2.xml')
kbgrab_xml = join_paths(protodir, 'unstable', 'xwayland-keyboard-grab', 'xwayland-keyboard-grab-unstable-v1.xml')
xdg_output_xml = join_paths(protodir, 'unstable', 'xdg-output', 'xdg-output-unstable-v1.xml')
dmabuf_xml = join_paths(protodir, 'unstable', 'linux-dmabuf', 'linux-dmabuf-unstable-v1.xml')
viewporter_xml = join_paths(protodir, 'stable', 'viewporter', 'viewporter.xml')
xdg_shell_xml = join_paths(protodir, 'stable', 'xdg-shell', 'xdg-shell.xml')
drm_lease_xml = join_paths(protodir, 'staging', 'drm-lease', 'drm-lease-v1.xml')
shortcuts_inhibit_xml = join_paths(protodir, 'unstable', 'keyboard-shortcuts-inhibit', 'keyboard-shortcuts-inhibit-unstable-v1.xml')
xwayland_shell_xml = join_paths(protodir, 'staging', 'xwayland-shell', 'xwayland-shell-v1.xml')
tearing_xml = join_paths(protodir, 'staging', 'tearing-control', 'tearing-control-v1.xml')
fractional_scale_xml = join_paths(protodir, 'staging', 'fractional-scale', 'fractional-scale-v1.xml')
syncobj_xml = join_paths(protodir, 'staging', 'linux-drm-syncobj', 'linux-drm-syncobj-v1.xml')

proto_xml = [
    relative_xml,
    pointer_xml,
    gestures_xml,
    tablet_xml,
    kbgrab_xml,
    xdg_output_xml,
    dmabuf_xml,
    viewporter_xml,
    xdg_shell_xml,
    drm_lease_xml,
    shortcuts_inhibit_xml,
    xwayland_shell_xml,
    tearing_xml,
    fractional_scale_xml,
    syncobj_xml,
]

client_header = generator(scanner,
    output : '@BASENAME@-client-protocol.h',
    arguments : ['client-header', '@INPUT@', '@OUTPUT@']
)

if scanner_dep.version().version_compare('>= 1.14.91')
    scanner_argument = 'private-code'
else
    scanner_argument = 'code'
endif

code = generator(scanner,
    output : '@BASENAME@-protocol.c',
    arguments : [scanner_argument, '@INPUT@', '@OUTPUT@']
)

foreach xml : proto_xml
    srcs += client_header.process(xml)
    srcs += code.process(xml)
endforeach

if build_ei
    xwayland_dep += libei_dep
    srcs += [ 'xwayland-xtest.c', 'xwayland-xtest.h' ]

    if build_ei_portal
        xwayland_dep += liboeffis_dep
    endif
endif

xwayland_glamor = []
if build_xwayland_glamor
    srcs += [
              'xwayland-glamor.c',
              'xwayland-dmabuf.h',
              'xwayland-dmabuf.c',
              'xwayland-glamor-gbm.c',
              'xwayland-glamor-gbm.h'
            ]

    if build_xv
        srcs += 'xwayland-glamor-xv.c'
    endif

    srcs += client_header.process('drm.xml')
    srcs += code.process('drm.xml')
    xwayland_dep += gbm_dep
    xwayland_glamor += glamor
endif

wayland_inc = [ inc, ]
if build_glx
    wayland_inc += glx_inc
endif

if libdrm_dep.found()
    xwayland_dep += libdrm_dep
endif

if have_libdecor
    xwayland_dep += libdecor_dep
endif

xwayland_server = executable(
    'Xwayland',
    srcs,
    include_directories: wayland_inc,
    dependencies: [
        common_dep,
        epoll_dep,
        xwayland_dep,
        xwaylandproto_dep,
    ],
    link_with: [
        libxserver_main,
        xwayland_glamor,
        libxserver_fb,
        libxserver,
        libxserver_xext_vidmode,
        libxserver_xkb_stubs,
        libxserver_xi_stubs,
        libxserver_glx,
        libglxvnd,
    ],
    install: true,
    install_dir: xwayland_path
)

xwayland_vars = [
    'have_glamor=' + build_glamor.to_string(),
    'have_glamor_api=' + build_glamor.to_string(),
    'have_eglstream=false',
    'have_initfd=true',
    'have_listenfd=true',
    'have_verbose=true',
    'have_terminate_delay=true',
    'have_no_touch_pointer_emulation=true',
    'have_force_xrandr_emulation=true',
    'have_geometry=true',
    'have_fullscreen=true',
    'have_host_grab=true',
    'have_decorate=' + have_libdecor.to_string(),
    'have_enable_ei_portal=' + build_ei_portal.to_string(),
    'have_byteswappedclients=true',
    'have_hidpi=true',
]

pkgconfig = import('pkgconfig')

pkgconfig.generate(
    filebase: 'xwayland',
    name: 'Xwayland',
    description: 'X Server for Wayland',
    dataonly: true,
    variables: [
        'exec_prefix=${prefix}',
        'xwayland=' + xwayland_path / xwayland_server.name(),
    ] + xwayland_vars,
)

xwayland_manpage = configure_file(
    input: 'man/Xwayland.man',
    output: 'Xwayland.1',
    configuration: manpage_config,
)
install_man(xwayland_manpage)

desktop_data = configuration_data()
desktop_data.set('XWAYLAND', xwayland_path / xwayland_server.name())
desktop_data.set('DECORATE', have_libdecor ? '-decorate' : '')
desktop_file = configure_file(
    input: 'desktop/org.freedesktop.Xwayland.desktop.in',
    output: 'org.freedesktop.Xwayland.desktop',
    configuration: desktop_data,
)
datadir = join_paths(get_option('prefix'), get_option('datadir'))
desktopdir = join_paths(datadir, 'applications')
install_data(desktop_file, install_dir : desktopdir)

meson.override_find_program('Xwayland', xwayland_server)
meson.override_dependency('xwayland', declare_dependency(
    variables: [
        'xwayland=' + xwayland_server.full_path(),
    ] + xwayland_vars,
))
