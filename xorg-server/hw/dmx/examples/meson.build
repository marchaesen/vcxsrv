dmxexamples_dep = [
    dependency('xext'),
    dependency('x11'),
]

dmx_dep = dependency('dmx', version: '>= 1.0.99.1')

dmx_examples = [
    ['dmxinfo', [dmx_dep]],
    ['dmxwininfo', [dmx_dep, dependency('xmu')]],
    ['dmxreconfig', [dmx_dep]],
    ['dmxresize', [dmx_dep]],
    ['dmxaddscreen', [dmx_dep]],
    ['dmxrmscreen', [dmx_dep]],
    ['dmxaddinput', [dmx_dep]],
    ['dmxrminput', [dmx_dep]],
]

noinst_dmx_examples = [
    ['xinput', [dmx_dep, dependency('xi')]],
    ['xtest', [dependency('xtst')]],
    ['evi', []],
    ['xled', []],
    ['xbell', []],
    ['res', [dependency('xres')]],
]

foreach ex: dmx_examples
    bin = ex[0]
    extra_dep = ex[1]

    executable(
        bin,
        bin + '.c',
        dependencies: [dmxexamples_dep, extra_dep],
        install: true
    )
endforeach

foreach ex: noinst_dmx_examples
    bin = ex[0]
    extra_dep = ex[1]

    executable(
        bin,
        bin + '.c',
        dependencies: [dmxexamples_dep, extra_dep],
    )
endforeach

if cc.has_header('linux/input.h')
    executable('ev', 'ev.c')
endif
