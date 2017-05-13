srcs_present = [
    'present.c',
    'present_event.c',
    'present_fake.c',
    'present_fence.c',
    'present_notify.c',
    'present_request.c',
    'present_screen.c',
]

libxserver_present = static_library('libxserver_present',
    srcs_present,
    include_directories: inc,
    dependencies: [
        common_dep,
        dependency('presentproto', version: '>= 1.0')
    ],
    c_args: '-DHAVE_XORG_CONFIG_H'
)
