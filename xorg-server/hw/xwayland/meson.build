srcs = [
    'xwayland.c',
    'xwayland-input.c',
    'xwayland-input.h',
    'xwayland-cursor.c',
    'xwayland-cursor.h',
    'xwayland-drm-lease.h',
    'xwayland-drm-lease.c',
    'xwayland-glamor.h',
    'xwayland-glx.h',
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

xwayland_glamor = []
eglstream_srcs = []
if build_glamor
    srcs += 'xwayland-glamor.c'
    if build_glx
        srcs += 'xwayland-glx.c'
    endif
    if gbm_dep.found()
        srcs += 'xwayland-glamor-gbm.c'
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
    srcs += 'xwayland-present.c'
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

xwayland_server = executable(
    'Xwayland',
    srcs,
    include_directories: wayland_inc,
    dependencies: [
        common_dep,
        xwayland_dep,
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

xwayland_data = configuration_data()
xwayland_data.set('prefix', get_option('prefix'))
xwayland_data.set('exec_prefix', '${prefix}')
xwayland_data.set('PACKAGE_VERSION', meson.project_version())
xwayland_data.set('xwayland_path', xwayland_path)
xwayland_data.set('have_glamor', build_glamor ? 'true' : 'false')
xwayland_data.set('have_eglstream', build_eglstream ? 'true' : 'false')
configure_file(
    input: 'xwayland.pc.in',
    output: 'xwayland.pc',
    configuration: xwayland_data,
    install_dir: join_paths(get_option('prefix'),
                            get_option('libdir'),
                            'pkgconfig'),
)

xwayland_manpage = configure_file(
    input: 'man/Xwayland.man',
    output: 'Xwayland.1',
    configuration: manpage_config,
)
install_man(xwayland_manpage)
