# Copyright © 2017 Eric Engestrom
# SPDX-License-Identifier: MIT

git_sha1_gen_py = files('git_sha1_gen.py')
gen_vs_module_defs_py = files('gen_vs_module_defs.py')
gen_vs_module_defs_normal_command = [
  prog_python, gen_vs_module_defs_py,
  '--in_file', '@INPUT@', '--out_file', '@OUTPUT@',
  '--compiler_abi', cc.get_argument_syntax(),
  '--compiler_id', cc.get_id(), '--cpu_family', host_machine.cpu_family()
]
symbols_check = find_program('symbols-check.py')
install_megadrivers_py = find_program('install_megadrivers.py')
install_megadrivers = [
  install_megadrivers_py.full_path(),
  '--libname-suffix', libname_suffix,
]
