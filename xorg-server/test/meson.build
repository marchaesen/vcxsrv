simple_xinit = executable(
    'simple-xinit',
    'simple-xinit.c',
    include_directories: inc,
)

piglit_env = environment()
piglit_env.set('XSERVER_DIR', meson.source_root())
piglit_env.set('XSERVER_BUILDDIR', meson.build_root())

if get_option('xvfb')
    test('xvfb-piglit', find_program('scripts/xvfb-piglit.sh'),
        env: piglit_env,
        timeout: 1200,
    )

    if get_option('xephyr') and build_glamor
        test('xephyr-glamor',
            find_program('scripts/xephyr-glamor-piglit.sh'),
            env: piglit_env,
            timeout: 1200,
        )
    endif
endif

subdir('bigreq')
subdir('damage')
subdir('sync')

if build_xorg
# Tests that require at least some DDX functions in order to fully link
# For now, requires xf86 ddx, could be adjusted to use another
    unit_sources = [
     '../mi/miinitext.c',
     'fixes.c',
     'input.c',
     'list.c',
     'misc.c',
     'signal-logging.c',
     'string.c',
     'test_xkb.c',
     'tests-common.c',
     'tests.c',
     'touch.c',
     'xfree86.c',
     'xtest.c',
    ]
    unit_defines = ['-DXORG_TESTS']
    unit_includes = [inc, xorg_inc]

    if build_res
        unit_sources += ['hashtabletest.c']
        unit_defines += ['-DRES_TESTS']
    endif

    if meson.version().version_compare('>=0.46.0') and meson.get_compiler('c').has_link_argument('-Wl,-wrap')
       unit_sources += [
        'xi1/protocol-xchangedevicecontrol.c',
        'xi2/protocol-common.c',
        'xi2/protocol-xiqueryversion.c',
        'xi2/protocol-xiquerydevice.c',
        'xi2/protocol-xiselectevents.c',
        'xi2/protocol-xigetselectedevents.c',
        'xi2/protocol-xisetclientpointer.c',
        'xi2/protocol-xigetclientpointer.c',
        'xi2/protocol-xiquerypointer.c',
        'xi2/protocol-xipassivegrabdevice.c',
        'xi2/protocol-xiwarppointer.c',
        'xi2/protocol-eventconvert.c',
        'xi2/xi2.c',
       ]
       unit_defines += ['-DLDWRAP_TESTS']
       unit_includes += [include_directories('xi1', 'xi2')]
       ldwraps = [
        '-Wl,-wrap,dixLookupWindow',
        '-Wl,-wrap,dixLookupClient',
        '-Wl,-wrap,WriteToClient',
        '-Wl,-wrap,dixLookupWindow',
        '-Wl,-wrap,XISetEventMask',
        '-Wl,-wrap,AddResource',
        '-Wl,-wrap,GrabButton',
       ]
    else
       ldwraps = []
       message('ld -wrap required for xi1 & xi2 unit tests, skipping')
    endif

    unit = executable('tests',
         unit_sources,
         c_args: unit_defines,
         dependencies: pixman_dep,
         include_directories: unit_includes,
         link_args: ldwraps,
         link_with: xorg_link,
    )

    test('unit', unit)
endif
