srcs_dmx_input = [
    'dmxdummy.c',
    'dmxbackend.c',
    'dmxconsole.c',
    'dmxcommon.c',
    'dmxinputinit.c',
    'dmxarg.c',
    'dmxevents.c',
    'dmxxinput.c',
    'dmxmotion.c',
    'dmxmap.c',
    'ChkNotMaskEv.c',
]

if cc.has_header('linux/input.h')
    srcs_dmx_input += [
        'usb-keyboard.c',
        'usb-mouse.c',
        'usb-other.c',
        'usb-common.c',
    ]
endif

dmx_input = static_library('dmx_input',
	srcs_dmx_input,
	include_directories: [
            inc,
            include_directories('../')
        ],
	dependencies: common_dep,
        c_args: '-DHAVE_DMX_CONFIG_H',
)
