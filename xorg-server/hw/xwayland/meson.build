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
scanner = find_program(scanner_dep.get_pkgconfig_variable('wayland_scanner'))

protocols_dep = dependency('wayland-protocols', version: wayland_protocols_req)
protodir = protocols_dep.get_pkgconfig_variable('pkgdatadir')

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
srcs += client_header.process(relative_xml)
srcs += client_header.process(pointer_xml)
srcs += client_header.process(gestures_xml)
srcs += client_header.process(tablet_xml)
srcs += client_header.process(kbgrab_xml)
srcs += client_header.process(xdg_output_xml)
srcs += client_header.process(dmabuf_xml)
srcs += client_header.process(viewporter_xml)
srcs += client_header.process(xdg_shell_xml)
srcs += client_header.process(drm_lease_xml)
srcs += client_header.process(shortcuts_inhibit_xml)
srcs += client_header.process(xwayland_shell_xml)
srcs += client_header.process(tearing_xml)
srcs += code.process(relative_xml)
srcs += code.process(pointer_xml)
srcs += code.process(gestures_xml)
srcs += code.process(tablet_xml)
srcs += code.process(kbgrab_xml)
srcs += code.process(xdg_output_xml)
srcs += code.process(dmabuf_xml)
srcs += code.process(viewporter_xml)
srcs += code.process(xdg_shell_xml)
srcs += code.process(drm_lease_xml)
srcs += code.process(shortcuts_inhibit_xml)
srcs += code.process(xwayland_shell_xml)
srcs += code.process(tearing_xml)

if build_ei
    xwayland_dep += libei_dep
    srcs += [ 'xwayland-xtest.c', 'xwayland-xtest.h' ]

    if build_ei_portal
        xwayland_dep += liboeffis_dep
    endif
endif

xwayland_glamor = []
eglstream_srcs = []
if build_glamor
    srcs += 'xwayland-glamor.c'
    if gbm_dep.found()
        srcs += [
                  'xwayland-glamor-gbm.c',
                  'xwayland-present.c'
                ]
    endif
    if build_eglstream
        eglstream_protodir = eglstream_dep.get_pkgconfig_variable('pkgdatadir')
        eglstream_xml = join_paths(eglstream_protodir, 'wayland-eglstream.xml')
        eglstream_controller_xml = join_paths(eglstream_protodir, 'wayland-eglstream-controller.xml')

        srcs += client_header.process(eglstream_xml)
        srcs += client_header.process(eglstream_controller_xml)
        srcs += code.process(eglstream_xml)
        srcs += code.process(eglstream_controller_xml)

        srcs += 'xwayland-glamor-eglstream.c'
    endif
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
    'have_eglstream=' + build_eglstream.to_string(),
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
]

pkgconfig = import('pkgconfig')

pkgconfig.generate(
    filebase: 'xwayland',
    name: 'Xwayland',
    description: 'X Server for Wayland',
    variables: [
        'exec_prefix=${prefix}',
        'xwayland=' + xwayland_path + '/Xwayland',
    ] + xwayland_vars,
)

xwayland_manpage = configure_file(
    input: 'man/Xwayland.man',
    output: 'Xwayland.1',
    configuration: manpage_config,
)
install_man(xwayland_manpage)

datadir = join_paths(get_option('prefix'), get_option('datadir'))
desktopdir = join_paths(datadir, 'applications')
install_data('desktop/org.freedesktop.Xwayland.desktop', install_dir : desktopdir)

if meson.version().version_compare('>= 0.56.0')
    meson.override_find_program('Xwayland', xwayland_server)

    meson.override_dependency('xwayland', declare_dependency(
        variables: [
            'xwayland=' + xwayland_server.full_path(),
        ] + xwayland_vars,
    ))
endif
