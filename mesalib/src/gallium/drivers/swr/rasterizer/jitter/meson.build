# Copyright © 2017-2018 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

if dep_llvm.type_name() == 'internal'
  _irbuilder_h = subproject('llvm').get_variable('irbuilder_h')
else
  _llvm_includedir = dep_llvm.get_variable(configtool : 'includedir', cmake : 'LLVM_INCLUDE_DIR')
  _irbuilder_h = join_paths(_llvm_includedir, 'llvm', 'IR', 'IRBuilder.h')
endif

gen_builder_hpp = custom_target(
  'gen_builder.hpp',
  input : [
    swr_gen_llvm_ir_macros_py, _irbuilder_h,
  ],
  output : 'gen_builder.hpp',
  command : [
    prog_python, '@INPUT0@', '--input', '@INPUT1@', '--output', '@OUTPUT@',
    '--gen_h', '--output-dir', '@OUTDIR@'
  ],
  depend_files : swr_gen_builder_depends,
  build_by_default : true,
)

gen_builder_meta_hpp = custom_target(
  'gen_builder_meta.hpp',
  input : '../codegen/gen_llvm_ir_macros.py',
  output : 'gen_builder_meta.hpp',
  command : [
    prog_python, '@INPUT0@', '--gen_meta_h', '--output', '@OUTPUT@',
    '--output-dir', '@OUTDIR@'
  ],
  depend_files : swr_gen_builder_depends,
)

gen_builder_intrin_hpp = custom_target(
  'gen_builder_intrin.hpp',
  input : '../codegen/gen_llvm_ir_macros.py',
  output : 'gen_builder_intrin.hpp',
  command : [
    prog_python, '@INPUT0@', '--gen_intrin_h', '--output', '@OUTPUT@',
    '--output-dir', '@OUTDIR@'
  ],
  depend_files : swr_gen_builder_depends,
)

