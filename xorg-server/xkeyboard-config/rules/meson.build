# And some files that are a straight install
install_data('README', 'xkb.dtd', 'xfree98',
             install_dir: dir_xkb_rules)

base_xml = files('base.xml')
base_extras_xml = files('base.extras.xml')

# the actual rules files are generated from a list of parts in a very
# specific order
parts = [
    '0000-hdr.part',
    '0001-lists.part',
    '0002-@0@.lists.part',
    '0004-@0@.model_keycodes.part',
    '0005-layout1_keycodes.part',
    '0006-layout_keycodes.part',
    '0007-options_keycodes.part',
    '0008-modellayout_geometry.part',
    '0009-model_geometry.part',
    '0011-modellayoutvariant_symbols.part',
    '0013-modellayout_symbols.part',
    '0016-modellayout1_symbols.part',
    '0018-modellayout2_symbols.part',
    '0020-modellayout3_symbols.part',
    '0022-modellayout4_symbols.part',
    '0026-@0@.model_symbols.part',
    '0027-@0@.modellayout_symbols1.part',
    '0033-modellayout_compat.part',
    '0034-modellayout1_compat.part',
    '0035-model_types.part',
    '0036-layoutoption_symbols.part',
    '0037-layout1option_symbols.part',
    '0038-layout2option_symbols.part',
    '0039-layout3option_symbols.part',
    '0040-layout4option_symbols.part',
     # 0042-option_symbols.part is generated from base{.extras}.xml
     # 0043-option_compat.part is generated from base{.extras}.xml
     # 0044-option_types.part is generated from base{.extras}.xml
]

rules_parts_generated = []

generate_rules_options_symbols = find_program('generate-options-symbols.py')
generated = [
    ['0042-option_symbols.part', 'symbols'],
    ['0043-option_compat.part', 'compatibility'],
    ['0044-option_types.part', 'types'],
]
foreach g: generated
    filename = g[0]
    section = g[1]
    part = custom_target(filename,
                         build_by_default: true,
                         command: [generate_rules_options_symbols,
                                   '--rules-section=@0@'.format(section),
                                   '--xkb-config-root', meson.project_source_root(),
                                   base_xml, base_extras_xml],
                         output: filename,
                         capture: true,
                         install: false)
    rules_parts_generated += [part]
endforeach

if get_option('compat-rules')
  # non-generated compat parts
  parts += [
      'compat/0028-layoutvariant_compat.part',
      'compat/0029-layout1variant1_compat.part',
      'compat/0030-layout2variant2_compat.part',
      'compat/0031-layout3variant3_compat.part',
      'compat/0032-layout4variant4_compat.part',
      'compat/0041-option_symbols.part',
  ]

  layout_mappings = files('compat/layoutsMapping.lst')
  variant_mappings = files('compat/variantsMapping.lst')
  map_variants_py = find_program('compat/map-variants.py')

  # two sets of files are generated: ml_s.part and mlv_s.part
  # each with the level name in the filename
  lvl_ml_s = {
    '0': '0012-ml_s.part',
    '1': '0015-ml1_s.part',
    '2': '0017-ml2_s.part',
    '3': '0019-ml3_s.part',
    '4': '0021-ml4_s.part',
  }

  lvl_mlv_s = {
    '0': '0010-mlv_s.part',
    '1': '0014-ml1v1_s.part',
    '2': '0023-ml2v2_s.part',
    '3': '0024-ml3v3_s.part',
    '4': '0025-ml4v4_s.part',
  }

  foreach lvl: [0, 1, 2, 3, 4]
      ml_s_file = lvl_ml_s['@0@'.format(lvl)]
      ml_s = custom_target(ml_s_file,
                           build_by_default: true,
                           command: [
                               map_variants_py,
                               '--number=@0@'.format(lvl),
                               '@OUTPUT@',
                               layout_mappings,
                           ],
                           output: ml_s_file,
                           install: false)
      rules_parts_generated += [ml_s]

      mlv_s_file = lvl_mlv_s['@0@'.format(lvl)]
      mlv_s = custom_target(mlv_s_file,
                           build_by_default: true,
                           command: [
                               map_variants_py,
                               '--has-variant',
                               '--number=@0@'.format(lvl),
                               '@OUTPUT@',
                               variant_mappings,
                           ],
                           output: mlv_s_file,
                           install: false)
      rules_parts_generated += [mlv_s]
  endforeach
endif # compat-rules

MINIMUM_PYTHON_VERSION = '3.11'
MINIMUM_PYTHON_VERSION_WITH_MODULE_FALLBACK = '3.9'
pymod = import('python')
python = pymod.find_installation('python3')
if python.language_version().version_compare('<@0@'.format(MINIMUM_PYTHON_VERSION))
    # Now require strenum module as fallback
    python = pymod.find_installation('python3', modules: ['strenum'])
    if python.language_version().version_compare('<@0@'.format(MINIMUM_PYTHON_VERSION_WITH_MODULE_FALLBACK))
        error('Minimum required Python version: @0@, but got: @1@'.format(
            MINIMUM_PYTHON_VERSION_WITH_MODULE_FALLBACK,
            python.language_version()
        ))
    endif
endif
merge_py = find_program('merge.py')
xml2lst = find_program('xml2lst.pl')
foreach ruleset: ['base', 'evdev']
    # generate the "evdev" and "base" rules files
    #
    # First: copy all the parts over to the build directory, replacing
    # RULESET with the rulename (evdev or base) and prefix it with the
    # ruleset name. So 0000-hdr.part becomes 0000-hdr.part.evdev and
    # 0000-hdr.part.base
    rules_parts = []
    foreach part: parts
      infile = part.format(ruleset)
      p = configure_file(output: '@PLAINNAME@.@0@'.format(ruleset),
                         input: infile,
                         copy: true,
                         install: false)
      rules_parts += p
    endforeach

    # Second: merge those parts together into the actual rules file
    custom_target('rules-@0@'.format(ruleset),
                  build_by_default: true,
                  command: [
                      merge_py,
                      rules_parts + rules_parts_generated,
                  ],
                  depends: rules_parts_generated,
                  output: ruleset,
                  capture: true,
                  install: true,
                  install_dir: dir_xkb_rules)

    # Third: the xml files, simply copied from the base*.xml files
    ruleset_xml = configure_file(output: '@0@.xml'.format(ruleset),
                       input: base_xml,
                       copy: true,
                       install: true,
                       install_dir: dir_xkb_rules)

    # This is used by the man page's meson.build
    if ruleset == 'evdev'
      evdev_ruleset = ruleset_xml
    endif

    configure_file(output: '@0@.extras.xml'.format(ruleset),
                   input: base_extras_xml,
                   copy: true,
                   install: true,
                   install_dir: dir_xkb_rules)

    # Fourth: generate the evdev.lst and base.lst files
    lst_file = '@0@.lst'.format(ruleset)
    custom_target(lst_file,
                  build_by_default: true,
                  command: [xml2lst, ruleset_xml],
                  capture: true,
                  output: lst_file,
                  install: true,
                  install_dir: dir_xkb_rules)

endforeach

if get_option('xorg-rules-symlinks')
  foreach suffix: ['', '.lst', '.xml']
    if meson.version().version_compare('>= 0.61')
      install_symlink('xorg' + suffix,
                      pointing_to: 'base' + suffix,
                      install_dir: dir_xkb_rules)
    else
      meson.add_install_script('sh', '-c',
        'ln -s base@0@ $DESTDIR@1@/xorg@0@'.format(suffix, dir_xkb_rules))
    endif
  endforeach
endif
# Copy the DTD to the build directory, the man page generation expects it in
# the same directory as the input XML file.
configure_file(output: 'xkb.dtd',
               input: 'xkb.dtd',
               copy: true,
               install: false)
