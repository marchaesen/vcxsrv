# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

opencl_link_args = []
opencl_link_deps = []
opencl_version = '1'

if with_ld_version_script
  opencl_link_args += [
    '-Wl,--version-script', join_paths(meson.current_source_dir(), 'opencl.sym')
  ]
  opencl_link_deps += files('opencl.sym')
endif

llvm_libdir = dep_llvm.get_variable(cmake : 'LLVM_LIBRARY_DIR', configtool: 'libdir')
opencl_libname = with_opencl_icd ? 'MesaOpenCL' : 'OpenCL'

polly_dep = null_dep
polly_isl_dep = null_dep
if dep_llvm.version().version_compare('>=10.0.0')
  polly_dep = cpp.find_library('Polly', dirs : llvm_libdir, required : false)
  polly_isl_dep = cpp.find_library('PollyISL', dirs : llvm_libdir, required : false)
endif

ocldef_in = files(opencl_libname + '.def.in')[0]
ocldef = custom_target(
  'ocldef.def',
  input: ocldef_in,
  output : 'ocldef.def',
  command : gen_vs_module_defs_normal_command,
)

libopencl = shared_library(
  opencl_libname,
  [],
  vs_module_defs : ocldef,
  link_args : [ld_args_gc_sections, opencl_link_args],
  link_depends : opencl_link_deps,
  link_whole : libclover,
  link_with : [libpipe_loader_dynamic, libgallium],
  dependencies : [
    idep_mesautil,
    dep_clock, dep_dl, dep_unwind, dep_elf, dep_clang, polly_dep, polly_isl_dep, dep_version
  ],
  name_prefix : host_machine.system() == 'windows' ? '' : [],  # otherwise mingw will create libOpenCL-1.dll or libMesaOpenCL-1.dll
  version : '@0@.0.0'.format(opencl_version),
  soversion : host_machine.system() == 'windows' ? '' : opencl_version,
  install : true,
)

if with_opencl_icd
  _config = configuration_data()
  _config.set('OPENCL_LIBNAME', 'MesaOpenCL')
  _config.set('OPENCL_VERSION', opencl_version)
  configure_file(
    configuration : _config,
    input : 'mesa.icd.in',
    output : 'mesa.icd',
    install : true,
    install_tag : 'runtime',
    install_dir : join_paths(get_option('sysconfdir'), 'OpenCL', 'vendors'),
  )

  # .so is hardcoded in the icd as well
  devenv.prepend(
    'OCL_ICD_FILENAMES',
    meson.current_build_dir() / 'libMesaOpenCL.so.@0@'.format(opencl_version)
  )
endif
