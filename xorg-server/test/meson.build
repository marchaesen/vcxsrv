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
subdir('sync')
