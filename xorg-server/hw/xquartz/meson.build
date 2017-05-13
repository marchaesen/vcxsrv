add_languages('objc')

srcs = [
    'X11Application.m',
    'X11Controller.m',
    'applewm.c',
    'darwin.c',
    'darwinEvents.c',
    'darwinXinput.c',
    'keysym2ucs.c',
    'quartz.c',
    'quartzCocoa.m',
    'quartzKeyboard.c',
    'quartzStartup.c',
    'quartzRandR.c',
    'console_redirect.c',
    '../../mi/miinitext.c',
]

executable(
    'Xquartz',
    srcs,
    include_directories: inc,
    link_with: [
        libxserver_main,
        libxserver,
        libxserver_xkb_stubs,
        libxserver_xi_stubs,
        libxserver_glx,
        libxserver_pseudoramix,
    ],
    c_args: [
        '-DXFree86Server',
        # XXXX: BUILD_DATE
        '-DXSERVER_VERSION="' + meson_project.version() + '"',
        '-DINXQUARTZ',
        '-DUSE_NEW_CLUT',
    ],
    install: true,
)
