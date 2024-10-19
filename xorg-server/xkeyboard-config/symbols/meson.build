# symbols have a few symlinks that we need to skip.
# NOTE: ideally, we would detect them automatically, but it is not
# clear how symlinks are handled on systems that do not support them.
symlinks = [
    'caps',
    'esperanto',
    'grp',
    'japan',
    'korean',
    'lv2',
    'lv3',
    'lv5',
]
excluded_symbols = symlinks + ['meson.build']
if get_option('compat-rules')
    layout_mappings = files('../rules/compat/layoutsMapping.lst')
    variant_mappings = files('../rules/compat/variantsMapping.lst')
    map_variants_py = find_program('../rules/compat/map-variants.py')
    cmd = [
        map_variants_py,
        '--symbols',
        '--has-variant',
        meson.current_build_dir(),
        variant_mappings
    ]
    # Get list of symbols files to generate
    c = run_command(cmd + ['--dry-run'], check: true)
    compat_symbols = c.stdout().strip().split('\n')
    # Generate compat symbols
    custom_target(
        'compat-symbols',
        command: cmd,
        depend_files: [map_variants_py.full_path(), variant_mappings],
        install: true,
        install_dir: dir_xkb_base / 'symbols',
        output: compat_symbols
    )
    excluded_symbols += compat_symbols
else
    compat_symbols = []
endif
install_subdir('.',
               exclude_files: excluded_symbols,
               install_dir: dir_xkb_base / 'symbols')
