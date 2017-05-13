srcs_xext = [
    'bigreq.c',
    'geext.c',
    'shape.c',
    'sleepuntil.c',
    'sync.c',
    'xcmisc.c',
    'xtest.c',
]

if build_dpms
    srcs_xext += 'dpms.c'
endif

if build_mitshm
    srcs_xext += 'shm.c'
endif

if build_res
    srcs_xext += ['hashtable.c', 'xres.c']
endif

if build_screensaver
    srcs_xext += 'saver.c'
endif

if build_xace
    srcs_xext += 'xace.c'
endif

if build_xf86bigfont
    srcs_xext += 'xf86bigfont.c'
endif

if build_xinerama
    srcs_xext += ['panoramiX.c', 'panoramiXprocs.c', 'panoramiXSwap.c']
endif

if build_xsecurity
    srcs_xext += ['security.c']
endif

if build_xselinux
    srcs_xext += ['xselinux_ext.c', 'xselinux_hooks.c', 'xselinux_label.c']
endif

if build_xv
    srcs_xext += ['xvmain.c', 'xvdisp.c', 'xvmc.c']
endif

libxserver_xext = static_library('libxserver_xext',
    srcs_xext,
    include_directories: inc,
    dependencies: common_dep,
)

libxserver_xext_vidmode = static_library('libxserver_xext_vidmode',
    'vidmode.c',
    include_directories: inc,
    dependencies: common_dep,
)
