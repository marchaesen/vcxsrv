srcs = [
    'xwayland.c',
    'xwayland-input.c',
    'xwayland-cursor.c',
    'xwayland-shm.c',
    'xwayland-output.c',
    'xwayland-cvt.c',
    'xwayland-vidmode.c',
    '../../mi/miinitext.c',
]

scanner_dep = dependency('wayland-scanner', native: true)
scanner = find_program(scanner_dep.get_pkgconfig_variable('wayland_scanner'))

protocols_dep = dependency('wayland-protocols')
protodir = protocols_dep.get_pkgconfig_variable('pkgdatadir')

pointer_xml = join_paths(protodir, 'unstable', 'pointer-constraints', 'pointer-constraints-unstable-v1.xml')
relative_xml = join_paths(protodir, 'unstable', 'relative-pointer', 'relative-pointer-unstable-v1.xml')
tablet_xml = join_paths(protodir, 'unstable', 'tablet', 'tablet-unstable-v2.xml')
kbgrab_xml = join_paths(protodir, 'unstable', 'xwayland-keyboard-grab', 'xwayland-keyboard-grab-unstable-v1.xml')

client_header = generator(scanner,
    output : '@BASENAME@-client-protocol.h',
    arguments : ['client-header', '@INPUT@', '@OUTPUT@']
)
code = generator(scanner,
    output : '@BASENAME@-protocol.c',
    arguments : ['code', '@INPUT@', '@OUTPUT@']
)
srcs += client_header.process(relative_xml)
srcs += client_header.process(pointer_xml)
srcs += client_header.process(tablet_xml)
srcs += client_header.process(kbgrab_xml)
srcs += code.process(relative_xml)
srcs += code.process(pointer_xml)
srcs += code.process(tablet_xml)
srcs += code.process(kbgrab_xml)

xwayland_glamor = []
if gbm_dep.found()
    srcs += 'xwayland-glamor.c'
    if build_xv
        srcs += 'xwayland-glamor-xv.c'
    endif

    srcs += client_header.process('drm.xml')
    srcs += code.process('drm.xml')
    xwayland_dep += gbm_dep
    xwayland_glamor += glamor
endif

executable(
    'Xwayland',
    srcs,
    include_directories: inc,
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
    ],
    install: true,
)
