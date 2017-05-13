srcs_dmx_glx = [
    'compsize.c',
    'g_disptab.c',
    'global.c',
    'glxcmds.c',
    'glxcmdsswap.c',
    'glxext.c',
    'glxfbconfig.c',
    'glxscreens.c',
    'glxsingle.c',
    'glxswap.c',
    'glxvendor.c',
    'glxvisuals.c',
    'g_renderswap.c',
    'render2swap.c',
    'renderpixswap.c',
]

dmx_glx = static_library('dmx_glx',
    srcs_dmx_glx,
    include_directories: [
        inc,
        include_directories('../')
    ],
    dependencies: common_dep,
    c_args: ['-DHAVE_DMX_CONFIG_H', glx_align64],
)
