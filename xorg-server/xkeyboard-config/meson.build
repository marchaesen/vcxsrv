project('xkeyboard-config',
        version: '2.44',
        license: 'MIT/Expat',
        meson_version: '>= 0.56.0')

dir_xkb_base    = ''
dir_xkb_rules   = join_paths(dir_xkb_base, 'rules')

i18n = import('i18n')

# KcG.T only need to be installed as-is
foreach dir: ['compat', 'geometry', 'keycodes', 'types']
    install_subdir(dir,
                   exclude_files: ['custom'],
                   install_dir: dir_xkb_base)
endforeach

# Rules and symbols are a bit more complicated
subdir('rules')
subdir('symbols')

# If needed, copy our data files over to the build directory
# so we can use the builddir as XKB_CONFIG_ROOT
pymod = import('python')
python = pymod.find_installation('python3',
                                 modules: ['pytest'],
                                 required: false)
pytest = find_program('pytest-3', 'pytest', required: false)
enable_pytest = python.found() and pytest.found()
if get_option('non-latin-layouts-list') or enable_pytest
    foreach dir: ['compat', 'geometry', 'keycodes', 'symbols', 'types']
        # Copy directory but exclude some symbols files that are generated
        # to include compatibility sections
        run_command(
            'tests/copydir.py',
            dir,
            dir == 'symbols' ? compat_symbols : [],
            check: true
        )
    endforeach
endif

# Latin layout list
# Needed e.g. for a distribution installer, in order to check if user can
# input required Latin characters and if not add automatically a US layout.
if get_option('non-latin-layouts-list')
    python = pymod.find_installation('python3',
                                     modules: ['yaml'],
                                     required: true)
    xkbcli = find_program('xkbcli', required: true)
    pyregistry = find_program('scripts/registry.py', required: true)

    # Both rules sets are expected to provide the same results, but are
    # nevertheless generated for completeness.
    foreach ruleset: ['base', 'evdev']
        custom_target('non-latin-layouts-list-@0@'.format(ruleset),
                      build_by_default: true,
                      command: [pyregistry,
                                '--xkb-root', meson.project_build_root(),
                                '--rules', ruleset,
                                'layouts',
                                '--non-latin',
                                '--csv', '@OUTPUT@'],
                      output: 'non-latin-layouts-@0@.csv'.format(ruleset),
                      install: false)
    endforeach
endif

# pytest suite
if enable_pytest
    pytest_args = ['--verbose', '--log-level=DEBUG']
    if get_option('compat-rules')
        pytest_args += [
            'tests',
            '--layout-compat-config', layout_mappings,
            '--layout-compat-config', variant_mappings,
        ]
    endif
    # use pytest xdist if available, it really speeds up the tests cases
    optional_python_modules = ['xdist']
    if pymod.find_installation('python3', modules: optional_python_modules, required: false).found()
        pytest_args += ['-n', 'auto']
    endif

    test('pytest', pytest,
         args: pytest_args,
         env: ['XKB_CONFIG_ROOT=@0@'.format(meson.project_build_root())],
         workdir: meson.project_source_root(),
         timeout: 60)
endif

subdir('po')
