flex = find_program('flex')
bison = find_program('bison')

lgen = generator(
    flex,
    output : '@PLAINNAME@.yy.c',
    arguments : ['-o', '@OUTPUT@', '@INPUT@']
)
lfiles = lgen.process('scanner.l')

pgen = generator(
    bison,
    output : ['@BASENAME@.c', '@BASENAME@.h'],
    arguments : ['@INPUT@', '--defines=@OUTPUT1@', '--output=@OUTPUT0@']
)
pfiles = pgen.process('parser.y')

srcs_dmx_config = [
    'dmxparse.c',
    'dmxprint.c',
    'dmxcompat.c',
    'dmxconfig.c',
    pfiles,
    lfiles,
]

dmx_inc = [
    inc,
    include_directories('../')
]

dmx_c_args = [
    '-DHAVE_DMX_CONFIG_H',
    '-DDMX_LOG_STANDALONE',
]

dmx_config = static_library('dmx_config',
    srcs_dmx_config,
    include_directories: dmx_inc,
    dependencies: common_dep,
    link_with: libxlibc,
    c_args: dmx_c_args,
)

executable('xdmxconfig',
    [
        'xdmxconfig.c',
        '../dmxlog.c',
        'Canvas.c',
    ],
    include_directories: [
        inc,
        include_directories('../')
    ],
    dependencies: [
        common_dep,
        dependency('xaw7'),
        dependency('xmu'),
        dependency('xt'),
        dependency('xpm'),
        dependency('x11'),
    ],
    link_with: dmx_config,
    c_args: dmx_c_args,
    install: true,
)

executable('vdltodmx',
    'vdltodmx.c',
    include_directories: dmx_inc,
    link_with: dmx_config,
    c_args: dmx_c_args,
    install: true,
)

executable('dmxtodmx',
    'dmxtodmx.c',
    include_directories: dmx_inc,
    link_with: dmx_config,
    c_args: dmx_c_args,
    install: true,
)
